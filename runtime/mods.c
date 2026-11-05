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
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hle.h"
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
static void add_file(const char *mod, const char *rel, const char *host, uint64_t size) {
    for (int i = 0; i < g_nfiles; i++)
        if (!strcmp(g_files[i].rel, rel)) {  // (mods in name order: the later one's file is used)
            LOG("[mods] %s: %s (also in %s: this one is used)\n", mod, rel, g_files[i].mod);
            free(g_files[i].host);
            g_files[i].host = strdup(host), g_files[i].mod = mod, g_files[i].size = size;
            return;
        }
    static int cap;
    if (g_nfiles == cap) g_files = realloc(g_files, sizeof *g_files * (cap = cap ? cap * 2 : 64));
    g_files[g_nfiles++] = (ModFile){strdup(rel), strdup(host), mod, size};
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
        else if (S_ISREG(st.st_mode)) add_file(mod, rel, host, (uint64_t)st.st_size);
    }
    closedir(d);
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void scan(void) {  // (g_lock held)
    if (g_scanned) return;
    g_scanned = true;
    const char *off = getenv("LARECOMP_NO_MODS");
    if (off && *off && strcmp(off, "0")) { LOG("[mods] off (LARECOMP_NO_MODS)\n"); return; }
    DIR *d = opendir(MODS_DIR);
    if (!d) return;
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
        add_piece(&lnk, &lcap, f->size, NULL, f->host, 0);
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
    scan();
    for (int i = 0; i < g_nfiles && !r; i++) {  // a file replaced whole
        ModFile *f = &g_files[i];
        if (strcmp(f->rel, rel)) continue;
        f->piece = (RomfsPiece){0, f->size, NULL, f->host, 0};
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
