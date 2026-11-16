// Mods: runtime/mods.h, docs/MODDING.md.
//
// The game's data is in packages: <package>/package_data.bin (the index: each file's path, size, compression and
// entry in the archive) and <package>/data_*.lnk (the archive: a table of entries, then the data). A replaced file
// that is packed in the archive is appended past the archive's end (aligned as its entries are) and the copies of
// the index record and archive entry are pointed at it, uncompressed - so the game finds it where it looks for the
// original. tools/mods/ktpkg.py documents both formats.
#include "mods.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hle.h"
#include "hle/body_shape.h"
#include "platform.h"

#define MODS_DIR "mods"
#define REC_SIZE 0x2c            // a package index record: 11 u32
#define REC_PATH 0x08            // the path's offset in the index's strings
#define REC_FLAGS 0x14           // F_COMPRESSED: the archive entry is zlib chunks
#define REC_SIZE_FIELD 0x18      // the file's size
#define REC_DIR_HASH 0x24        // 0: a loose file in the RomFS, not in the archive
#define REC_LNK_INDEX 0x28       // its archive entry
#define F_COMPRESSED 0x40000000u

typedef struct {
    char *rel, *host;  // the game file it replaces; the mod's file
    const char *mod;
    uint64_t size;
    RomfsSource whole;  // (served in the file's place)
    RomfsPiece piece;
    const uint8_t *mem;  // (instead of host: made when the game starts, body.ini's bust_physics)
} ModFile;

typedef struct {
    char dir[1100], name[64];  // the image's folder; the package's folder in it ("00", "00DLC00020000000")
    bool ok;
    RomfsSource pd, lnk;  // the changed index and archive
} Pkg;

static ModFile *g_files;
static int g_nfiles, g_nmods;
static bool g_scanned;
static Pkg **g_pkgs;  // (each its own allocation: the layouts keep pointers into them)
static int g_npkgs;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void wr64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }
static uint64_t align_up(uint64_t x, uint64_t a) { return (x + a - 1) / a * a; }

// ------------------------------------------------------------------ the mods folder
// (host: the mod's file; or mem, its bytes)
static void add_file(const char *mod, const char *rel, const char *host, uint64_t size, const uint8_t *mem) {
    for (int i = 0; i < g_nfiles; i++)
        if (!strcmp(g_files[i].rel, rel)) {  // (mods in name order: the later one's file is used)
            if (!mem) LOG("[mods] %s: %s (also in %s: this one is used)\n", mod, rel, g_files[i].mod);
            free(g_files[i].host);
            g_files[i].host = host ? strdup(host) : NULL, g_files[i].mod = mod, g_files[i].size = size;
            g_files[i].mem = mem;
            return;
        }
    static int cap;
    if (g_nfiles == cap) g_files = realloc(g_files, sizeof *g_files * (cap = cap ? cap * 2 : 64));
    g_files[g_nfiles++] = (ModFile){strdup(rel), host ? strdup(host) : NULL, mod, size, .mem = mem};
}

static void walk(const char *mod, const char *host_dir, const char *rel_dir, int depth) {
    DIR *d = opendir(host_dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char host[1100], rel[1024];
        snprintf(host, sizeof host, "%s/%s", host_dir, e->d_name);
        snprintf(rel, sizeof rel, "%s%s%s", rel_dir, *rel_dir ? "/" : "", e->d_name);
        struct stat st;
        if (stat(host, &st)) continue;
        if (S_ISDIR(st.st_mode) && depth < 32) walk(mod, host, rel, depth + 1);
        else if (S_ISREG(st.st_mode)) add_file(mod, rel, host, (uint64_t)st.st_size, NULL);
    }
    closedir(d);
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void body_physics(const char *dir);

static void scan(const char *dir) {  // (g_lock held; dir: the first image laid out, for where the game files are)
    if (g_scanned) return;
    g_scanned = true;
    const char *off = getenv("LARECOMP_NO_MODS");
    if (off && *off && strcmp(off, "0")) { LOG("[mods] off (LARECOMP_NO_MODS)\n"); return; }
    DIR *d = opendir(MODS_DIR);
    if (!d) { body_physics(dir); return; }  // (the launcher's body.ini still counts)
    char **names = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        char p[1100];
        struct stat st;
        snprintf(p, sizeof p, MODS_DIR "/%s", e->d_name);
        if (e->d_name[0] == '.' || stat(p, &st) || !S_ISDIR(st.st_mode)) continue;
        if (e->d_name[0] == '_') { LOG("[mods] %s: off (the name starts with _)\n", e->d_name); continue; }
        if (n == cap) names = realloc(names, sizeof *names * (cap = cap ? cap * 2 : 8));
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    if (n) qsort(names, n, sizeof *names, by_name);
    for (int i = 0; i < n; i++) {  // (the names stay: the files point at them)
        char p[1100];
        snprintf(p, sizeof p, MODS_DIR "/%s/romfs", names[i]);
        int before = g_nfiles;
        walk(names[i], p, "", 0);
        LOG("[mods] %s: %d files\n", names[i], g_nfiles - before);
        g_nmods++;
    }
    body_physics(dir);
}

// ------------------------------------------------------------------ packages
static uint8_t *read_all(const char *path, uint64_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) free(b), b = NULL;
    fclose(f);
    *size = b ? (uint64_t)n : 0;
    return b;
}

// ------------------------------------------------------------------ body.ini's bust_physics (runtime/hle/body_shape.c)
// A girl's costumes' soft-body presets with the breasts' moved toward another girl's for the same swimsuit design,
// served as mod files (in memory). A costume's presets are its <pkg>/character/<girl>/costume/<design>/..._YWRK.ywrk:
// "tdpack", +0x18 the count, the presets' offsets from +0x30; 12 presets (breasts left and right, hips, thighs, then
// the same six again), some costumes the first six. The game stores all of these uncompressed in its archives.
typedef struct { char *rel, *lnk; char code[8], design[64]; uint32_t index; uint64_t size; } Ywrk;

static const int k_breast_presets[] = {0, 1, 6, 7};

// "<pkg>/character/<code>/costume/<design>/<file>_YWRK.ywrk": the code and the design (lower case)
static bool ywrk_path(const char *rel, char *code, size_t ncode, char *design, size_t ndesign) {
    size_t n = strlen(rel);
    const char *c = strstr(rel, "/character/");
    if (n < 10 || strcmp(rel + n - 10, "_YWRK.ywrk") || !c) return false;
    c += 11;
    const char *e = strchr(c, '/');
    if (!e || (size_t)(e - c) >= ncode || strncmp(e, "/costume/", 9)) return false;
    const char *d = e + 9, *de = strchr(d, '/');
    if (!de || (size_t)(de - d) >= ndesign || strchr(de + 1, '/')) return false;
    snprintf(code, ncode, "%.*s", (int)(e - c), c);
    for (size_t i = 0; i < (size_t)(de - d); i++) design[i] = (char)(d[i] >= 'A' && d[i] <= 'Z' ? d[i] + 32 : d[i]);
    design[de - d] = 0;
    return true;
}

// The costumes' presets in a package (folder: its index and archive) of the girls in `want` (",MOM,MAR,")
static void ywrk_list(const char *folder, const char *want, Ywrk **out, int *n, int *cap) {
    char p[1200], lnk[1200] = "";
    snprintf(p, sizeof p, "%s/package_data.bin", folder);
    uint64_t size = 0;
    uint8_t *pd = read_all(p, &size);
    DIR *d = pd ? opendir(folder) : NULL;
    for (struct dirent *e; d && (e = readdir(d));) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcmp(e->d_name + l - 4, ".lnk")) snprintf(lnk, sizeof lnk, "%s/%s", folder, e->d_name);
    }
    if (d) closedir(d);
    uint32_t btif = pd && size >= 0x48 && !memcmp(pd, "pkginfo", 7) ? rd32(pd + 0x34) : 0;
    if (!btif || !lnk[0] || (uint64_t)btif + 0x30 > size || memcmp(pd + btif, "btif", 4)) { free(pd); return; }
    uint32_t nrec = rd32(pd + btif + 8), recb = rd32(pd + btif + 0x20);
    if ((uint64_t)nrec * REC_SIZE != recb || (uint64_t)btif + 0x30 + recb > size) { free(pd); return; }
    const uint8_t *recs = pd + btif + 0x30;
    const char *strs = (const char *)recs + recb;
    uint64_t strsz = size - (btif + 0x30 + recb);
    for (uint32_t r = 0; r < nrec; r++) {
        const uint8_t *rec = recs + r * REC_SIZE;
        uint32_t so = rd32(rec + REC_PATH);
        Ywrk y = {0};
        char needle[12];
        if (so >= strsz || !memchr(strs + so, 0, strsz - so) ||
            !ywrk_path(strs + so, y.code, sizeof y.code, y.design, sizeof y.design) ||
            (snprintf(needle, sizeof needle, ",%s,", y.code), !strstr(want, needle)) ||
            !rd32(rec + REC_DIR_HASH) || (rd32(rec + REC_FLAGS) & F_COMPRESSED))
            continue;
        y.rel = strdup(strs + so), y.lnk = strdup(lnk), y.index = rd32(rec + REC_LNK_INDEX);
        y.size = rd32(rec + REC_SIZE_FIELD);
        if (*n == *cap) *out = realloc(*out, sizeof **out * (*cap = *cap ? *cap * 2 : 1024));
        (*out)[(*n)++] = y;
    }
    free(pd);
}

// A preset file's bytes (*size): a mod's when one replaces it, else the game's (never one made here: a girl whose
// physics another girl takes gives her own); NULL: unreadable
static uint8_t *ywrk_read(const Ywrk *y, uint64_t *size) {
    for (int i = 0; i < g_nfiles; i++)
        if (!strcmp(g_files[i].rel, y->rel) && g_files[i].host) return read_all(g_files[i].host, size);
    *size = y->size;
    int fd = open(y->lnk, O_RDONLY);
    uint8_t e[0x20], *b = NULL;
    if (fd >= 0 && pread(fd, e, sizeof e, 0x20 + (off_t)y->index * 0x20) == sizeof e && !rd64(e + 24) &&
        rd64(e + 8) == y->size && rd64(e + 16) == y->size && y->size >= 0x40 && y->size < (1u << 24) &&
        (b = malloc(y->size)) && pread(fd, b, y->size, (off_t)rd64(e)) != (ssize_t)y->size)
        free(b), b = NULL;
    if (fd >= 0) close(fd);
    return b;
}

static bool normal_float(float v) { return v == 0 || (fabsf(v) > 1e-20f && fabsf(v) < 1e20f); }

// How much each soft-body point moves: a preset's floats 4-21 (+0x10, one per point of the region, by its bone's
// number; Character::ApplySoftBodyParams), 0 for a point that stays put. The game's own softness levels (the table
// FUN_7000260ec0 reads, per girl: level 2 x0, 3 about x0.4, 5 x1, 8 about x1.5) multiply them, and a factor of 1 or more
// leaves values under 0.1 as they are: so here too. The regions' presets: breasts 0, 1, bottom 2, 3, thighs 4, 5, and
// the same again from 6 (a second variant).
static const int k_region_presets[BODY_NREGIONS][4] = {{0, 1, 6, 7}, {2, 3, 8, 9}, {4, 5, 10, 11}};

// Its lengths: the second block (+0x148, 0x1bc bytes; FUN_7000262b80 converts it per softness level) has five arrays
// of one float per point; the one at +0x1e4 (its float 0x27) is the game's own "up to 6 cm" (computed per level and
// capped at 0.06: how far a point may swing), the one at +0x22c (float 0x39) 2 to 20 mm (the point's size, it seems).
// Both are lengths for the part's normal size: they scale with it (reach), or a bigger bust's points swing no further
// and are as small as before, and arms and hair go between them.
static void ywrk_scale(uint8_t *d, uint64_t size, int region, float k, float reach) {
    uint32_t count = rd32(d + 0x18);
    if (memcmp(d, "tdpack", 6) || count > 64 || 0x30 + 4ull * count > size) return;
    for (int j = 0; j < 4; j++) {
        uint32_t i = (uint32_t)k_region_presets[region][j];
        uint64_t o = i < count ? rd32(d + 0x30 + 4 * i) : 0;
        if (!o || o + 0x58 > size) continue;
        for (int f = 4; f < 22 && fabsf(k - 1) > .004f; f++) {
            float x;
            memcpy(&x, d + o + 4 * f, 4);
            if (!normal_float(x) || x <= 0 || (k >= 1 && x < 0.1f)) continue;
            x *= k;
            if (x > 0.95f) x = 0.95f;
            memcpy(d + o + 4 * f, &x, 4);
        }
        if (fabsf(reach - 1) < .004f || o + 0x148 + 4 * (0x39 + 18) > size) continue;
        for (int a = 0; a < 2; a++)
            for (int f = 0; f < 18; f++) {
                uint64_t at = o + 0x148 + 4 * ((a ? 0x39 : 0x27) + f);
                float x;
                memcpy(&x, d + at, 4);
                if (!normal_float(x) || x <= 0 || x > 1) continue;
                x *= reach;
                memcpy(d + at, &x, 4);
            }
    }
}

// Hers with the breast presets moved t of the way to theirs (each float; a field that isn't one, from the nearer);
// false when the two aren't laid out alike
static bool ywrk_blend(uint8_t *mine, const uint8_t *theirs, uint64_t size, float t) {
    if (memcmp(mine, "tdpack", 6) || memcmp(theirs, "tdpack", 6)) return false;
    uint32_t count = rd32(mine + 0x18);
    if (count != rd32(theirs + 0x18) || !count || count > 64 || 0x30 + 4ull * count > size) return false;
    for (size_t k = 0; k < sizeof k_breast_presets / sizeof *k_breast_presets; k++) {
        uint32_t i = (uint32_t)k_breast_presets[k];
        if (i >= count) break;
        uint64_t oa = rd32(mine + 0x30 + 4 * i), ob = rd32(theirs + 0x30 + 4 * i);
        uint64_t ea = i + 1 < count ? rd32(mine + 0x34 + 4 * i) : size;
        uint64_t eb = i + 1 < count ? rd32(theirs + 0x34 + 4 * i) : size;
        if (oa > ea || ob > eb || ea > size || eb > size || ea - oa != eb - ob) return false;
        for (uint64_t o = 0; o + 4 <= ea - oa; o += 4) {
            float x, y;
            memcpy(&x, mine + oa + o, 4), memcpy(&y, theirs + ob + o, 4);
            if (normal_float(x) && normal_float(y)) {
                x += (y - x) * t;
                memcpy(mine + oa + o, &x, 4);
            } else if (t >= 0.5f)
                memcpy(mine + oa + o, theirs + ob + o, 4);
        }
    }
    return true;
}

static void body_physics(const char *dir) {  // (g_lock held)
    int nb;
    const BodySettings *b = body_settings(&nb);
    char want[256] = ",";
    for (int i = 0; i < nb; i++) {
        bool blend = b[i].physics[0] && b[i].physics_amount > 0, jiggle = false;
        for (int r = 0; r < BODY_NREGIONS; r++)
            jiggle |= fabsf(body_jiggle(&b[i], r) - 1) > .004f || fabsf(body_reach(&b[i], r) - 1) > .004f;
        if ((blend || jiggle) && strlen(want) + 16 < sizeof want) strcat(want, b[i].code), strcat(want, ",");
        if (blend && strlen(want) + 16 < sizeof want) strcat(want, b[i].physics), strcat(want, ",");
    }
    if (!want[1]) return;
    // the game files' folder: dir is its romfs/ or aoc/<id>/
    char game[1100], sub[1200];
    snprintf(game, sizeof game, "%s", dir);
    for (int up = 0; up < 2; up++) {
        size_t l = strlen(game);
        while (l > 1 && (game[l - 1] == '/' || game[l - 1] == '\\')) game[--l] = 0;
        char *s = strrchr(game, '/'), *s2 = strrchr(game, '\\');
        if (s2 > s) s = s2;
        const char *base = s ? s + 1 : game;
        bool romfs = !strcmp(base, "romfs"), aoc = !strcmp(base, "aoc");
        if (up == 0 && romfs) { if (s) *s = 0; else strcpy(game, "."); break; }
        if (up == 1 && aoc) { if (s) *s = 0; else strcpy(game, "."); break; }
        if (up == 1 || !s) { LOG("[body] bust_physics: can't tell where the game files are from %s\n", dir); return; }
        *s = 0;  // (aoc/<id>: up to aoc)
    }
    uint64_t t0 = plat_now_ns();
    Ywrk *all = NULL;
    int n = 0, cap = 0;
    const char *tops[2] = {"romfs", "aoc"};
    for (int k = 0; k < 2; k++) {  // romfs/<pkg>, aoc/<id>/<pkg>
        snprintf(sub, sizeof sub, "%s/%s", game, tops[k]);
        DIR *d = opendir(sub);
        for (struct dirent *e; d && (e = readdir(d));) {
            if (e->d_name[0] == '.') continue;
            char f[1300];
            snprintf(f, sizeof f, "%s/%s", sub, e->d_name);
            if (k == 0) { ywrk_list(f, want, &all, &n, &cap); continue; }
            DIR *d2 = opendir(f);
            for (struct dirent *e2; d2 && (e2 = readdir(d2));)
                if (e2->d_name[0] != '.') {
                    char f2[1600];
                    snprintf(f2, sizeof f2, "%s/%s", f, e2->d_name);
                    ywrk_list(f2, want, &all, &n, &cap);
                }
            if (d2) closedir(d2);
        }
        if (d) closedir(d);
    }
    for (int i = 0; i < nb; i++) {
        bool blend = b[i].physics[0] && b[i].physics_amount > 0, jiggle = false;
        float k[BODY_NREGIONS], reach[BODY_NREGIONS];
        for (int r = 0; r < BODY_NREGIONS; r++) {
            k[r] = body_jiggle(&b[i], r), reach[r] = body_reach(&b[i], r);
            jiggle |= fabsf(k[r] - 1) > .004f || fabsf(reach[r] - 1) > .004f;
        }
        if (!blend && !jiggle) continue;
        int done = 0, blended = 0, none = 0, bad = 0;
        for (int a = 0; a < n; a++) {
            if (strcmp(all[a].code, b[i].code)) continue;
            uint64_t na = 0, nt = 0;
            uint8_t *mine = ywrk_read(&all[a], &na), *other = NULL;
            if (!mine) { bad++; continue; }
            const Ywrk *theirs = NULL;
            for (int c = 0; blend && c < n && !theirs; c++)
                if (!strcmp(all[c].code, b[i].physics) && !strcmp(all[c].design, all[a].design)) theirs = &all[c];
            if (blend && !theirs) none++;
            else if (blend) {
                other = ywrk_read(theirs, &nt);
                if (other && na == nt && ywrk_blend(mine, other, na, b[i].physics_amount)) blended++;
                else bad++;
                free(other);
            }
            for (int r = 0; r < BODY_NREGIONS; r++) ywrk_scale(mine, na, r, k[r], reach[r]);
            add_file("body.ini", all[a].rel, NULL, na, mine);  // (kept: the layouts serve it)
            done++;
        }
        char more[256] = "";
        if (blend)
            snprintf(more, sizeof more, "; breasts %.0f%% like %s's in %d", b[i].physics_amount * 100, b[i].physics,
                     blended);
        if (none) snprintf(more + strlen(more), sizeof more - strlen(more), " (%d that %s doesn't have stay hers)",
                           none, b[i].physics);
        if (bad) snprintf(more + strlen(more), sizeof more - strlen(more), "; %d not blended (laid out differently)", bad);
        LOG("[body] %s: %d costumes' soft-body presets: jiggle bust x%.2f, butt x%.2f, thighs x%.2f; lengths x%.2f, "
            "x%.2f, x%.2f%s (%llu ms)\n", b[i].code, done, k[0], k[1], k[2], reach[0], reach[1], reach[2], more,
            (unsigned long long)((plat_now_ns() - t0) / 1000000));
    }
    for (int a = 0; a < n; a++) free(all[a].rel), free(all[a].lnk);
    free(all);
}

static void add_piece(RomfsSource *s, int *cap, uint64_t size, const uint8_t *mem, const char *path, uint64_t off) {
    if (!size) return;
    if (s->n == *cap) s->pieces = realloc(s->pieces, sizeof *s->pieces * (*cap = *cap ? *cap * 2 : 8));
    s->pieces[s->n++] = (RomfsPiece){s->size, size, mem, path, off};
    s->size += size;
}

static bool is_package_file(const char *rel) {  // (the index and archive themselves are only ever replaced whole)
    const char *b = strrchr(rel, '/');
    b = b ? b + 1 : rel;
    size_t n = strlen(b);
    return !strcmp(b, "package_data.bin") || (n > 4 && !strcmp(b + n - 4, ".lnk"));
}

static void pkg_build(Pkg *p) {
    char pdp[1200], lnkp[1200] = "", folder[1200];
    snprintf(folder, sizeof folder, "%s/%s", p->dir, p->name);
    snprintf(pdp, sizeof pdp, "%s/package_data.bin", folder);
    DIR *d = opendir(folder);
    for (struct dirent *e; d && (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if (n > 4 && !strcmp(e->d_name + n - 4, ".lnk")) snprintf(lnkp, sizeof lnkp, "%s/%s", folder, e->d_name);
    }
    if (d) closedir(d);
    uint64_t pdsize = 0;
    uint8_t *pd = read_all(pdp, &pdsize);
    #define FAIL(fmt, ...) do { LOG("[mods] ERROR: package %s: " fmt, p->name, ##__VA_ARGS__); free(pd); return; } while (0)
    if (!pd || pdsize < 0x48 || memcmp(pd, "pkginfo", 7)) FAIL("can't read its index %s\n", pdp);
    uint32_t btif = rd32(pd + 0x34);
    if ((uint64_t)btif + 0x30 > pdsize || memcmp(pd + btif, "btif", 4)) FAIL("its index has no file table\n");
    uint32_t nrec = rd32(pd + btif + 8), recb = rd32(pd + btif + 0x20);
    if ((uint64_t)nrec * REC_SIZE != recb || (uint64_t)btif + 0x30 + recb > pdsize) FAIL("its file table isn't understood\n");
    uint8_t *recs = pd + btif + 0x30;
    const char *strs = (const char *)recs + recb;
    uint64_t strsz = pdsize - (btif + 0x30 + recb);

    uint8_t h[0x20];
    int fd = lnkp[0] ? open(lnkp, O_RDONLY) : -1;
    struct stat st;
    if (fd < 0 || pread(fd, h, sizeof h, 0) != sizeof h || h[0] != 'b' || fstat(fd, &st)) {
        if (fd >= 0) close(fd);
        FAIL("can't read its archive %s\n", lnkp[0] ? lnkp : "(none)");
    }
    uint64_t count = rd64(h + 8), align = rd64(h + 0x18), lnk_size = (uint64_t)st.st_size;
    uint64_t tab_end = 0x20 + count * 0x20;
    if (!align || align > 0x100000) align = 0x800;
    uint8_t *head = tab_end <= lnk_size ? malloc(tab_end) : NULL;
    bool read_ok = head && pread(fd, head, tab_end, 0) == (ssize_t)tab_end;
    close(fd);
    if (!read_ok) { free(head); FAIL("can't read its archive's table\n"); }

    RomfsSource lnk = {0};
    int lcap = 0, packed = 0, loose = 0;
    add_piece(&lnk, &lcap, tab_end, head, NULL, 0);
    add_piece(&lnk, &lcap, lnk_size - tab_end, NULL, strdup(lnkp), tab_end);
    size_t plen = strlen(p->name);
    for (int i = 0; i < g_nfiles; i++) {
        ModFile *f = &g_files[i];
        if (strncmp(f->rel, p->name, plen) || f->rel[plen] != '/' || is_package_file(f->rel)) continue;
        uint8_t *rec = NULL;
        for (uint32_t r = 0; r < nrec && !rec; r++) {
            uint32_t so = rd32(recs + r * REC_SIZE + REC_PATH);
            if (so < strsz && !strcmp(strs + so, f->rel)) rec = recs + r * REC_SIZE;
        }
        if (!rec) {
            LOG("[mods] %s: %s isn't in package %s's index (only the game's own files can be replaced)\n", f->mod,
                f->rel, p->name);
            continue;
        }
        if (f->size > 0xFFFFFFFFu) { LOG("[mods] %s: %s is over 4 GB: left out\n", f->mod, f->rel); continue; }
        wr32(rec + REC_SIZE_FIELD, (uint32_t)f->size);
        if (!rd32(rec + REC_DIR_HASH)) { loose++; continue; }  // (a loose file: served in its place, mods_source)
        uint32_t idx = rd32(rec + REC_LNK_INDEX);
        if (idx >= count) { LOG("[mods] %s: %s: archive entry %u out of range\n", f->mod, f->rel, idx); continue; }
        wr32(rec + REC_FLAGS, rd32(rec + REC_FLAGS) & ~F_COMPRESSED);
        uint64_t at = align_up(lnk.size, align);
        add_piece(&lnk, &lcap, at - lnk.size, NULL, NULL, 0);  // (zeros up to the alignment)
        add_piece(&lnk, &lcap, f->size, f->mem, f->mem ? NULL : f->host, 0);
        uint8_t *e = head + 0x20 + (uint64_t)idx * 0x20;
        wr64(e, at), wr64(e + 8, f->size), wr64(e + 16, f->size), wr64(e + 24, 0);
        packed++;
    }
    add_piece(&lnk, &lcap, align_up(lnk.size, align) - lnk.size, NULL, NULL, 0);
    wr64(head + 0x10, lnk.size);  // (the archive's size in its header)
    p->lnk = lnk;
    p->pd.pieces = calloc(1, sizeof *p->pd.pieces);
    p->pd.pieces[0] = (RomfsPiece){0, pdsize, pd, NULL, 0};
    p->pd.n = 1, p->pd.size = pdsize;
    p->ok = true;
    LOG("[mods] package %s: %d packed and %d loose files replaced (archive %llu -> %llu bytes)\n", p->name, packed, loose,
        (unsigned long long)lnk_size, (unsigned long long)lnk.size);
    #undef FAIL
}

static Pkg *pkg_get(const char *dir, const char *name, size_t nlen) {
    if (nlen >= sizeof ((Pkg *)0)->name) return NULL;
    bool any = false;  // (a package no mod touches stays as it is)
    for (int i = 0; i < g_nfiles && !any; i++)
        any = !strncmp(g_files[i].rel, name, nlen) && g_files[i].rel[nlen] == '/' && !is_package_file(g_files[i].rel);
    if (!any) return NULL;
    for (int i = 0; i < g_npkgs; i++)
        if (!strcmp(g_pkgs[i]->dir, dir) && strlen(g_pkgs[i]->name) == nlen && !strncmp(g_pkgs[i]->name, name, nlen))
            return g_pkgs[i];
    g_pkgs = realloc(g_pkgs, sizeof *g_pkgs * (g_npkgs + 1));
    Pkg *p = g_pkgs[g_npkgs++] = calloc(1, sizeof *p);
    snprintf(p->dir, sizeof p->dir, "%s", dir);
    memcpy(p->name, name, nlen);
    pkg_build(p);
    return p;
}

const RomfsSource *mods_source(const char *dir, const char *rel, const char *host, uint64_t host_size) {
    (void)host, (void)host_size;
    const RomfsSource *r = NULL;
    pthread_mutex_lock(&g_lock);
    scan(dir);
    for (int i = 0; i < g_nfiles && !r; i++) {  // a file replaced whole
        ModFile *f = &g_files[i];
        if (strcmp(f->rel, rel)) continue;
        f->piece = (RomfsPiece){0, f->size, f->mem, f->mem ? NULL : f->host, 0};
        f->whole = (RomfsSource){&f->piece, 1, f->size};
        r = &f->whole;
        LOG("[mods] %s: %s\n", f->mod, rel);
    }
    const char *slash = strchr(rel, '/');
    if (!r && g_nfiles && slash && !strchr(slash + 1, '/') && is_package_file(rel)) {  // a package's index or archive
        Pkg *p = pkg_get(dir, rel, (size_t)(slash - rel));
        if (p && p->ok) r = !strcmp(slash + 1, "package_data.bin") ? &p->pd : &p->lnk;
    }
    pthread_mutex_unlock(&g_lock);
    return r;
}

int mods_count(void) { return g_nmods; }
int mods_files(void) { return g_nfiles; }

// ------------------------------------------------------------------ reading a pieced file
#define PIECE_FDS 16
static struct { const char *path; int fd; uint64_t used; } g_fds[PIECE_FDS];
static uint64_t g_tick;
static pthread_mutex_t g_fd_lock = PTHREAD_MUTEX_INITIALIZER;

static int piece_fd(const char *path) {  // (g_fd_lock held) the file's descriptor, kept open in a small cache
    int slot = 0;
    for (int k = 0; k < PIECE_FDS; k++) {
        if (g_fds[k].fd > 0 && g_fds[k].path == path) { g_fds[k].used = ++g_tick; return g_fds[k].fd; }
        if (g_fds[k].used < g_fds[slot].used) slot = k;
    }
    if (g_fds[slot].fd > 0) close(g_fds[slot].fd);
    int fd = open(path, O_RDONLY);
    if (fd == 0) {  // (0 marks a free slot)
        int d = dup(fd);
        close(fd);
        fd = d;
    }
    g_fds[slot].path = path, g_fds[slot].fd = fd > 0 ? fd : 0, g_fds[slot].used = ++g_tick;
    return fd;
}

long long romfs_source_read(const RomfsSource *s, uint8_t *dst, uint64_t n, uint64_t off) {
    int lo = 0, hi = s->n - 1, i = s->n;  // the first piece overlapping off
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s->pieces[mid].at + s->pieces[mid].size > off) i = mid, hi = mid - 1;
        else lo = mid + 1;
    }
    uint64_t end = off + n;
    for (; i < s->n && s->pieces[i].at < end; i++) {
        const RomfsPiece *p = &s->pieces[i];
        uint64_t a = off > p->at ? off : p->at, b = end < p->at + p->size ? end : p->at + p->size;
        if (a >= b) continue;
        uint8_t *d = dst + (a - off);
        if (p->mem) memcpy(d, p->mem + (a - p->at), b - a);
        else if (!p->path) memset(d, 0, b - a);
        else {
            pthread_mutex_lock(&g_fd_lock);
            int fd = piece_fd(p->path);
            ssize_t got = fd > 0 ? pread(fd, d, b - a, (off_t)(p->off + (a - p->at))) : -1;
            pthread_mutex_unlock(&g_fd_lock);
            if (got != (ssize_t)(b - a)) {
                LOG("[mods] can't read %s (%s)\n", p->path, strerror(errno));
                return -1;
            }
        }
    }
    return (long long)n;
}
