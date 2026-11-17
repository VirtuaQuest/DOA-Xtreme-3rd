// Body settings (docs/MODDING.md, "Body shape"): a girl's bust, bottom, thighs, arms, calves and waist, and how much
// her soft parts move. Read from mods/<mod>/body.ini and the launcher's body.ini (in the data folder, beside
// larecomp.ini; read last, so its settings win). The shape changes her costume model's memory when her character is
// built (Character::Create, tools/patches.py CHAR_FILES_*): the game's files and the other girls stay as they are.
//
//   [MOM]                       the girl: her three-letter code (tools/mods/doaxmod.py girls)
//   bust_size = 0.8             how far her breasts stand out, 1 as they are (their width and height change half as much)
//   bust_jiggle = 1.5           how much they move, on top of their size (bigger breasts move more on their own)
//   bust_physics = MAR          how they move: another girl's soft-body settings for the same swimsuit
//   bust_physics_amount = 0.5   part of the way: 0 her own, 1 the other girl's (the default)
//   butt_size, butt_jiggle      the same for her bottom
//   thigh_size, thigh_jiggle    her thighs' thickness, and how much their soft part moves
//   arm_size, calf_size, waist_size   thickness: arms, calves (below the knee), waist
//   bust_weight = 1.5           how heavy her breasts are, 1 as they are: heavier ones hang lower along gravity, every
//                               frame, however she stands or bends (the further out, the more); lighter ones sit higher
//   bust_contact = 1            how firmly a bigger or heavier bust meets the arms and itself: 1 the breasts give way
//                               to her arms and push each other apart; 0 they pass through, as before
// "bust_physics = none" gives a girl her own physics back over a mod's. The jiggle and physics are served by
// runtime/mods.c (body_physics), as her costumes' _YWRK.ywrk presets.
//
// How: a costume model (it carries the body; eight of the eleven girls wear Kasumi's) skins the body to the skeleton's
// bones, the breasts and bottom to a base bone and soft-body points each side (OPT_Breast_Left_base and
// OPT_ywrk_Left00-17; OPT_Hips_Left_base and OPT_ywrk_hip_Left00-13). A vertex is skinned by its bones' world matrices
// times their inverse bind matrices (BnOfsMtx); multiplying a bone's inverse bind matrix by a scale about a point
// scales what that bone carries, in every pose and with the physics, and a vertex shared with another bone by its
// weight, so edges blend in. Bust and bottom: out from the base along the points' mean direction, across half as much.
// Arms, thighs, calves, waist: across the bone (about the line from its joint to the next), not along it: a bone's
// length is the skeleton's, which the animations move. The colliders the hair meets on the bust and bottom bases follow
// their size. Moving the skin doesn't move the soft-body points, so a bust made bigger would swing no further than
// before: the jiggle follows the size (body_jiggle).
#include "body_shape.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "../hle.h"
#include "../platform.h"

static const struct { const char *key; float lo, hi; } BODY_KEYS[BODY_NVALUES] = {
    [BODY_BUST_SIZE] = {"bust_size", 0.3f, 2.5f},     [BODY_BUST_JIGGLE] = {"bust_jiggle", 0, 2},
    [BODY_BUTT_SIZE] = {"butt_size", 0.5f, 2.5f},     [BODY_BUTT_JIGGLE] = {"butt_jiggle", 0, 2},
    [BODY_THIGH_SIZE] = {"thigh_size", 0.7f, 2.0f},   [BODY_THIGH_JIGGLE] = {"thigh_jiggle", 0, 2},
    [BODY_ARM_SIZE] = {"arm_size", 0.7f, 2.0f},       [BODY_CALF_SIZE] = {"calf_size", 0.7f, 2.0f},
    [BODY_WAIST_SIZE] = {"waist_size", 0.7f, 1.8f},   [BODY_BUST_WEIGHT] = {"bust_weight", 0.5f, 3},
    [BODY_BUST_CONTACT] = {"bust_contact", 0, 1},
};

static BodySettings g_body[16];
static int g_nbody;

// The girls' character ids (CharacterSlot +0x4; 0 is the owner): 2-12 in the order of their packages (00, 00DLC0002 ...
// 00DLC0011). Checked in game: Kasumi 2, Hitomi 8 and Momiji 9.
static const struct { const char *code, *name; uint32_t id; } GIRL_IDS[] = {
    {"KAS", "Kasumi", 2}, {"MAR", "Marie Rose", 3}, {"HON", "Honoka", 4}, {"AYA", "Ayane", 5}, {"NYO", "Nyotengu", 6},
    {"KOK", "Kokoro", 7}, {"HTM", "Hitomi", 8}, {"MOM", "Momiji", 9}, {"HEL", "Helena", 10}, {"MIS", "Misaki", 11},
    {"LEI", "Leifang", 12},
};

const char *body_girl_code(const char *s) {
    for (size_t i = 0; i < sizeof GIRL_IDS / sizeof *GIRL_IDS; i++) {
        const char *n = GIRL_IDS[i].name;
        size_t first = strcspn(n, " ");
        if (!strcasecmp(s, GIRL_IDS[i].code) || !strcasecmp(s, n) || (strlen(s) == first && !strncasecmp(s, n, first)))
            return GIRL_IDS[i].code;
    }
    return NULL;
}

float body_jiggle(const BodySettings *b, int region) {
    static const int jig[BODY_NREGIONS] = {BODY_BUST_JIGGLE, BODY_BUTT_JIGGLE, BODY_THIGH_JIGGLE};
    return region >= 0 && region < BODY_NREGIONS ? b->v[jig[region]] : 1;
}

float body_reach(const BodySettings *b, int region) {
    static const int size[BODY_NREGIONS] = {BODY_BUST_SIZE, BODY_BUTT_SIZE, BODY_THIGH_SIZE};
    return region >= 0 && region < BODY_NREGIONS ? b->v[size[region]] : 1;
}

static const char *girl_code(uint32_t id) {
    for (size_t i = 0; i < sizeof GIRL_IDS / sizeof *GIRL_IDS; i++)
        if (GIRL_IDS[i].id == id) return GIRL_IDS[i].code;
    return NULL;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void read_ini(const char *mod, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    BodySettings *cur = NULL;
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char *h = strpbrk(line, "#;");
        if (h) *h = 0;
        char *s = trim(line);
        if (!*s) continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) *e = 0;
            s = trim(s + 1);
            for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
            cur = NULL;
            for (int i = 0; i < g_nbody && !cur; i++)
                if (!strcmp(g_body[i].code, s)) cur = &g_body[i];  // (a later file's settings for her win)
            if (!cur && g_nbody < (int)(sizeof g_body / sizeof *g_body)) {
                cur = &g_body[g_nbody++];
                memset(cur, 0, sizeof *cur);
                snprintf(cur->code, sizeof cur->code, "%s", s);
                for (int k = 0; k < BODY_NVALUES; k++) cur->v[k] = 1;
                cur->physics_amount = 1;
            }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || !cur) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        int key = -1;
        for (int i = 0; i < BODY_NVALUES && key < 0; i++)
            if (!strcmp(k, BODY_KEYS[i].key)) key = i;
        if (key >= 0) {
            float x = strtof(v, NULL);
            if (x >= BODY_KEYS[key].lo && x <= BODY_KEYS[key].hi) cur->v[key] = x, n++;
            else LOG("[body] %s: %s: %s %s is out of range (%.1f to %.1f): left as she is\n", mod, cur->code, k, v,
                     BODY_KEYS[key].lo, BODY_KEYS[key].hi);
        } else if (!strcmp(k, "bust_physics")) {
            const char *g = body_girl_code(v);
            if (g) snprintf(cur->physics, sizeof cur->physics, "%s", strcmp(g, cur->code) ? g : ""), n++;
            else if (!*v || !strcasecmp(v, "none")) cur->physics[0] = 0, n++;  // (her own, over a mod's)
            else LOG("[body] %s: %s: bust_physics %s is no girl the game has\n", mod, cur->code, v);
        } else if (!strcmp(k, "bust_physics_amount")) {
            float x = strtof(v, NULL);
            if (x >= 0 && x <= 1) cur->physics_amount = x, n++;
            else LOG("[body] %s: %s: bust_physics_amount %s is out of range (0 to 1)\n", mod, cur->code, v);
        } else LOG("[body] %s: %s: unknown setting %s\n", mod, cur->code, k);
    }
    fclose(f);
    LOG("[body] %s: body.ini, %d settings\n", mod, n);
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void load_once(void) {
    const char *off = getenv("LARECOMP_NO_MODS");
    if (off && *off && strcmp(off, "0")) return;
    DIR *d = opendir("mods");
    char *names[256];
    int n = 0;
    for (struct dirent *e; d && (e = readdir(d)) && n < 256;)
        if (e->d_name[0] != '.' && e->d_name[0] != '_') names[n++] = strdup(e->d_name);
    if (d) closedir(d);
    qsort(names, (size_t)n, sizeof *names, by_name);  // (as the mods load: by name, the later one wins)
    for (int i = 0; i < n; i++) {
        char p[1100];
        snprintf(p, sizeof p, "mods/%s/body.ini", names[i]);
        read_ini(names[i], p);
        free(names[i]);
    }
    // (the data folder's, from the launcher's Mods tab: over every mod's; LARECOMP_BODY_INI=<file> reads that instead,
    // for test runs)
    const char *own = getenv("LARECOMP_BODY_INI");
    read_ini("the launcher", own && *own ? own : "body.ini");
}

static void load(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;  // (the mods' layout and the game both ask)
    pthread_once(&once, load_once);
}

const BodySettings *body_settings(int *n) {
    load();
    *n = g_nbody;
    return g_body;
}

// ---- the costume model (a TMC container: +0x14 chunk count, +0x20 the chunk offsets' table; chunks by their tag)
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static const uint8_t *chunk(const uint8_t *m, const char *tag) {
    uint32_t n = rd32(m + 0x14), t = rd32(m + 0x20);
    if (n > 64 || !t) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = rd32(m + t + 4 * i);
        if (o && !strncmp((const char *)m + o, tag, 8)) return m + o;
    }
    return NULL;
}

static uint8_t *entry(const uint8_t *c, uint32_t i) {  // (a chunk's i-th sub-block)
    uint32_t n = rd32(c + 0x14), t = rd32(c + 0x20);
    if (i >= n || !t) return NULL;
    uint32_t o = rd32(c + t + 4 * i);
    return o ? (uint8_t *)c + o : NULL;
}

static void mat_mul(const float *a, const float *b, float *out) {  // (row vectors: p * a * b)
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            float s = 0;
            for (int m = 0; m < 4; m++) s += a[i * 4 + m] * b[m * 4 + k];
            r[i * 4 + k] = s;
        }
    memcpy(out, r, sizeof r);
}

static struct { uint64_t at; float m[16]; } g_done[8192];  // (what this changed, as it left it: not twice)
static int g_ndone;
static pthread_mutex_t g_done_lock = PTHREAD_MUTEX_INITIALIZER;

static bool once(void *at, void (*change)(void *, const float *), const float *arg) {  // (64 bytes at `at`)
    uint64_t a = (uint64_t)(uintptr_t)at;
    pthread_mutex_lock(&g_done_lock);
    int seen = -1;
    for (int q = 0; q < g_ndone; q++)
        if (g_done[q].at == a) seen = q;
    bool did = !(seen >= 0 && !memcmp(g_done[seen].m, at, 64));  // (this memory has it already)
    if (did) {
        change(at, arg);
        if (seen < 0) seen = g_ndone < 8192 ? g_ndone++ : (int)(a / 64 % 8192);
        g_done[seen].at = a;
        memcpy(g_done[seen].m, at, 64);
    }
    pthread_mutex_unlock(&g_done_lock);
    return did;
}

static bool already(const void *at) {  // (this memory has what once() put there)
    uint64_t a = (uint64_t)(uintptr_t)at;
    bool r = false;
    pthread_mutex_lock(&g_done_lock);
    for (int q = 0; q < g_ndone && !r; q++) r = g_done[q].at == a && !memcmp(g_done[q].m, at, 64);
    pthread_mutex_unlock(&g_done_lock);
    return r;
}

static void scale_bind(void *b, const float *A) { mat_mul(A, (float *)b, (float *)b); }
static void set_matrix(void *b, const float *M) { memcpy(b, M, 64); }

// ---- the colliders the hair stays out of (the costume's ACSCLS table 1: 0x40-byte records, +0x00 a rotation, +0x10
// an ellipsoid's half sizes, +0x20 its centre in the bone's frame, +0x30 the bone), on the body's bones (hips, chest,
// waist, arms, shoulders, neck, sometimes thighs and the breast bases): they follow the shape, or long hair sinks into a
// bigger bust or thicker arms. Seen in the game's colliders (LARECOMP_BODY_PROBE): many costumes have no breast
// colliders, and the hair rests on the chest's - so a bigger bust pushes the chest's out in front (COL_PUSH).
enum { COL_SOFT, COL_LIMB, COL_ALL, COL_PUSH };  // (bust base: bigger by the size, smaller by the volume; a limb: its
                                                 // two thinner sizes; all three; out along v, the far side staying)
typedef struct { int node, how; float s, f, v[3]; } ColReq;

static void push_collider(void *r, const float *v) {  // (v in the bone's frame; the collider's axes taken as the bone's)
    float *f = (float *)r;
    for (int a = 0; a < 3; a++) f[4 + a] += fabsf(v[a]) * 0.5f, f[8 + a] += v[a] * 0.5f;
}

static void scale_collider(void *r, const float *k) {  // (k: three sizes' factors, then the centre's)
    float *f = (float *)r;
    for (int a = 0; a < 3; a++) f[4 + a] *= k[a], f[8 + a] *= k[3];
}

static int collider(uint8_t *m, const ColReq *q) {
    const uint8_t *ac = chunk(m, "ACSCLS");
    if (!ac || rd32(ac + 0x14) < 2) return 0;
    const uint8_t *t = entry(ac, 1);
    if (!t) return 0;
    int n = 0;
    for (uint32_t i = 0, cnt = rd32(t + 0x14); i < cnt && i < 256; i++) {
        uint8_t *r = entry(t, i);
        if (!r || rd32(r + 0x30) != (uint32_t)q->node) continue;
        const float *sz = (const float *)(r + 0x10), *rot = (const float *)r;
        if (q->how == COL_PUSH) {  // (only where its rotation keeps its axes on the bone's: 0 or half turns)
            bool square = true;
            for (int a = 0; a < 3; a++) square &= fabsf(sinf(rot[a])) < 0.15f;
            if (square) n += once(r, push_collider, q->v);
            continue;
        }
        float k[4] = {q->s, q->s, q->s, 1};
        if (q->how == COL_SOFT) {  // (a bigger bust: as far out as it goes; a smaller one: by its volume)
            float v = q->s >= 1 ? q->s : cbrtf(q->s * q->f * q->f);
            k[0] = k[1] = k[2] = v, k[3] = q->s;
        } else if (q->how == COL_LIMB) {  // (the longest size runs along the limb: it stays)
            int along = sz[1] >= sz[0] && sz[1] >= sz[2] ? 1 : sz[0] >= sz[2] ? 0 : 2;
            k[along] = 1;
        }
        n += once(r, scale_collider, k);
    }
    return n;
}

// ---- the shape: a matrix per bone, made from the settings, then put on the bones' inverse bind matrices once. The
// soft-body points move with it (moved[]), so the soft body swings from the new surface.
typedef struct {
    uint8_t *m;
    const uint8_t *nl, *gm, *bo, *hl;
    uint32_t nodes;
    float (*A)[16];  // (per node: what its inverse bind matrix gets)
    bool *set, *moved;
    ColReq col[24];
    int ncol;
    float push[2][3];  // (how much further out the bust and the bottom reach, bind space: the chest's and hips' colliders)
} Shape;

static int node(const Shape *s, const char *name) {
    for (uint32_t i = 0; i < s->nodes; i++) {
        const uint8_t *no = entry(s->nl, i);
        if (no && !strcmp((const char *)no + 0x50, name)) return (int)i;
    }
    return -1;
}

static int mot(const Shape *s, const char *part) {  // a skeleton bone by its name after "MOTnn_" ("LeftArm")
    for (uint32_t i = 0; i < s->nodes; i++) {
        const uint8_t *no = entry(s->nl, i);
        const char *nm = no ? (const char *)no + 0x50 : "";
        if (!strncmp(nm, "MOT", 3) && strchr(nm, '_') && !strcmp(strchr(nm, '_') + 1, part)) return (int)i;
    }
    return -1;
}

static void pos(const Shape *s, int n, float *p) {
    const float *w = (const float *)entry(s->gm, (uint32_t)n);
    p[0] = w[12], p[1] = w[13], p[2] = w[14];
}

// The matrix scaling by M (3x3, rows) about the point c: p' = c + (p - c) M
static void about(const float *M, const float *c, float *A) {
    memset(A, 0, 16 * sizeof *A);
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) A[r * 4 + k] = M[r * 3 + k];
    for (int k = 0; k < 3; k++) {
        float cm = 0;
        for (int r = 0; r < 3; r++) cm += c[r] * M[r * 3 + k];
        A[12 + k] = c[k] - cm;
    }
    A[15] = 1;
}

static void put(Shape *s, int n, const float *A) {  // (node n's matrix gets A after what it has)
    if (n < 0) return;
    if (s->set[n]) mat_mul(s->A[n], A, s->A[n]);
    else memcpy(s->A[n], A, 16 * sizeof *A), s->set[n] = true;
}

static void want_collider(Shape *s, int node, int how, float v, float f) {
    if (node >= 0 && s->ncol < (int)(sizeof s->col / sizeof *s->col)) s->col[s->ncol++] = (ColReq){node, how, v, f};
}

static void points(Shape *s, const char *pts, int *list, int *n, int max) {  // ("OPT_ywrk_hip_Left" + digits)
    size_t pl = strlen(pts);
    *n = 0;
    for (uint32_t i = 0; i < s->nodes && *n < max; i++) {
        const uint8_t *no = entry(s->nl, i);
        const char *nm = no ? (const char *)no + 0x50 : "";
        if (!strncmp(nm, pts, pl) && isdigit((unsigned char)nm[pl])) list[(*n)++] = (int)i;
    }
}

// Bust or bottom: the base bone and its soft-body points, out from the base by `size` along the points' mean
// direction, across half as much: M = f I + (s - f) d d^T. -1: the bones aren't there.
static int soft_part(Shape *s, const char *base_name, const char *pts, float size) {
    int base = node(s, base_name), list[48], n;
    points(s, pts, list, &n, 48);
    if (base < 0 || n < 4) return -1;
    float c[3], d[3] = {0, 0, 0}, p[3];
    pos(s, base, c);
    for (int k = 0; k < n; k++) {
        pos(s, list[k], p);
        for (int a = 0; a < 3; a++) d[a] += (p[a] - c[a]) / (float)n;
    }
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-4f) return -1;
    float f = 1 - (1 - size) * 0.5f, M[9], A[16];
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) M[r * 3 + k] = (r == k ? f : 0) + (size - f) * d[r] / len * d[k] / len;
    about(M, c, A);
    for (int k = 0; k < n; k++) put(s, list[k], A), s->moved[list[k]] = true;
    put(s, base, A);
    want_collider(s, base, COL_SOFT, size, f);
    if (size > 1) {  // (half each side: how much further its furthest point reaches, along its direction)
        int part = strstr(pts, "hip") ? 1 : 0;
        float reach = 0;
        for (int k = 0; k < n; k++) {
            pos(s, list[k], p);
            float r = 0;
            for (int a = 0; a < 3; a++) r += (p[a] - c[a]) * d[a] / len;
            if (r > reach) reach = r;
        }
        for (int a = 0; a < 3; a++) s->push[part][a] += 0.5f * (size - 1) * reach * d[a] / len;
    }
    return 0;
}

// A limb or the waist: the bones (NULL-ended names) thicker by t across the line from the joint `from` to `to`, not
// along it: M = t I + (1 - t) a a^T; and soft-body points under it (pts, or NULL), moved with it. -1: the joints
// aren't there.
static int girth(Shape *s, int from, int to, float t, const char *const *bones, const char *pts) {
    if (from < 0 || to < 0) return -1;
    float c[3], e[3], a[3];
    pos(s, from, c);
    pos(s, to, e);
    for (int k = 0; k < 3; k++) a[k] = e[k] - c[k];
    float len = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (len < 1e-4f) return -1;
    float M[9], A[16];
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) M[r * 3 + k] = (r == k ? t : 0) + (1 - t) * a[r] / len * a[k] / len;
    about(M, c, A);
    put(s, from, A);
    for (int i = 0; bones[i]; i++) put(s, node(s, bones[i]), A);
    if (pts) {
        int list[16], n;
        points(s, pts, list, &n, 16);
        for (int k = 0; k < n; k++) put(s, list[k], A), s->moved[list[k]] = true;
    }
    want_collider(s, from, COL_LIMB, t, t);
    return 0;
}

// A soft-body point to where the shape puts it, its skin staying put: its bind world (GlblMtx) and local (HieLay)
// moved by d, its inverse bind matrix A T(-d) B (B: its own; at rest the vertex is v A B^-1 ... = v A, as before)
static void move_point(Shape *s, int n) {
    float *g = (float *)entry(s->gm, (uint32_t)n), *b = (float *)entry(s->bo, (uint32_t)n);
    uint8_t *h = entry(s->hl, (uint32_t)n);
    const float *A = s->A[n];
    float d[3];
    for (int k = 0; k < 3; k++) d[k] = g[12] * A[k] + g[13] * A[4 + k] + g[14] * A[8 + k] + A[12 + k] - g[12 + k];
    int32_t parent;
    memcpy(&parent, h + 0x40, 4);
    const float *pb = parent >= 0 && (uint32_t)parent < s->nodes ? (const float *)entry(s->bo, (uint32_t)parent) : NULL;
    float T[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -d[0], -d[1], -d[2], 1}, Bn[16];
    mat_mul(T, b, Bn);
    mat_mul(A, Bn, Bn);
    once(b, set_matrix, Bn);
    float *l = (float *)h;
    for (int k = 0; k < 3; k++) {  // (d in the parent's frame: d times its inverse bind's rotation)
        l[12 + k] += pb ? d[0] * pb[k] + d[1] * pb[4 + k] + d[2] * pb[8 + k] : d[k];
        g[12 + k] += d[k];
    }
}

// ---- colliders for the breasts. Many costumes have none (the hair meets only the chest's), and their collider table
// (ACSCLS table 1) has no room: a bigger bust gets a copy of the table with one more collider on each breast base,
// sized from where its soft-body points now are, and the ACSCLS chunk is pointed at the copy. Table 1: a 0x30 header
// (+0x10 its size, +0x14 and +0x18 the count, +0x20 the offsets' place), then a nested one at +0x30 (its own +0x10
// size and counts; at its +0x30 a descriptor of 12 bytes per collider: kind 2 = an ellipsoid, the node, the node
// name's hash), then the records' offsets, then the 0x40-byte records (+0 euler angles, +0x10 half sizes, +0x20 the
// offset in the bone's frame, +0x30 the node, +0x34 the hash). A record's bone is found by its index (+0x30;
// FUN_70005b76d0). Each breast's collider sits on its middle soft-body point, so it swings and hangs with the breast. The copies live in guest memory the runtime keeps above the game's heap (the chunk's offsets are 32
// bits from it, so a copy must be above it).
#define BODY_TABLES 0x7310000000ull
#define BODY_TABLES_SIZE 0x8000000ull
static uint64_t g_tab_next = BODY_TABLES, g_tab_end = BODY_TABLES;
RT_ON_RESET(body_tables_reset) { g_tab_next = g_tab_end = BODY_TABLES; }

static uint8_t *tab_alloc(uint64_t n) {
    n = (n + 63) & ~63ull;
    if (g_tab_next + n > BODY_TABLES + BODY_TABLES_SIZE) return NULL;
    while (g_tab_end < g_tab_next + n) plat_commit(g_tab_end, 0x10000), g_tab_end += 0x10000;
    uint8_t *p = (uint8_t *)(uintptr_t)g_tab_next;
    g_tab_next += n;
    memset(p, 0, n);
    return p;
}

static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static int breast_colliders(Shape *s) {
    static const uint32_t hash[2] = {2332990091u, 2332990090u};  // (OPT_Breast_Left_base, _Right_base: the game's ids)
    uint8_t *ac = (uint8_t *)chunk(s->m, "ACSCLS");
    if (!ac || rd32(ac + 0x14) < 2 || !rd32(ac + 0x20)) return 0;
    uint8_t *tb = entry(ac, 1);
    if (!tb || rd32(tb + 0x0c) != 0x30 || rd32(tb + 0x50) != 0x30) return 0;
    uint32_t count = rd32(tb + 0x14);
    if (count > 200) return 0;
    uint8_t add[2][0x40];
    memset(add, 0, sizeof add);
    int nadd = 0;
    for (int side = 0; side < 2; side++) {
        char a[64], pts[64];
        snprintf(a, sizeof a, "OPT_Breast_%s_base", side ? "Right" : "Left");
        snprintf(pts, sizeof pts, "OPT_ywrk_%s", side ? "Right" : "Left");
        int base = node(s, a), list[48], n;
        bool has = false;
        for (uint32_t i = 0; i < count && base >= 0; i++)
            has |= entry(tb, i) && rd32(entry(tb, i) + 0x30) == (uint32_t)base;
        points(s, pts, list, &n, 48);
        if (base < 0 || has || n < 4) continue;
        // (on the soft-body point nearest the points' middle, so it moves with the jiggle and the bust weight: centred
        // on that middle, its half sizes the points' spread about it along the point's axes, plus the skin beyond)
        float c[3] = {0, 0, 0}, p[3], best = 1e9f;
        int mid = list[0];
        for (int k = 0; k < n; k++) {
            pos(s, list[k], p);  // (where the shape has moved it)
            for (int ax = 0; ax < 3; ax++) c[ax] += p[ax] / (float)n;
        }
        for (int k = 0; k < n; k++) {
            pos(s, list[k], p);
            float d2 = (p[0] - c[0]) * (p[0] - c[0]) + (p[1] - c[1]) * (p[1] - c[1]) + (p[2] - c[2]) * (p[2] - c[2]);
            if (d2 < best) best = d2, mid = list[k];
        }
        const float *g = (const float *)entry(s->gm, (uint32_t)mid);  // (its bind world: rotation rows, unchanged)
        float half[3] = {0, 0, 0}, row[3][3];
        for (int r = 0; r < 3; r++) {
            float l = sqrtf(g[r * 4] * g[r * 4] + g[r * 4 + 1] * g[r * 4 + 1] + g[r * 4 + 2] * g[r * 4 + 2]);
            for (int ax = 0; ax < 3; ax++) row[r][ax] = l > 1e-6f ? g[r * 4 + ax] / l : 0;
        }
        for (int k = 0; k < n; k++) {
            pos(s, list[k], p);
            for (int r = 0; r < 3; r++) {
                float l = (p[0] - c[0]) * row[r][0] + (p[1] - c[1]) * row[r][1] + (p[2] - c[2]) * row[r][2];
                if (fabsf(l) > half[r]) half[r] = fabsf(l);
            }
        }
        float *r = (float *)add[nadd];
        pos(s, mid, p);
        for (int ax = 0; ax < 3; ax++) {
            r[4 + ax] = half[ax] * 1.1f + 0.015f;  // (the skin: about 1.5 cm beyond the outer points)
            r[8 + ax] = (c[0] - p[0]) * row[ax][0] + (c[1] - p[1]) * row[ax][1] + (c[2] - p[2]) * row[ax][2];
        }
        r[11] = 1;
        wr32(add[nadd] + 0x30, (uint32_t)mid), wr32(add[nadd] + 0x34, hash[side]);  // (the game goes by the index)
        nadd++;
    }
    if (!nadd) return 0;
    uint32_t nc = count + (uint32_t)nadd, nested = (0x30 + 0xc * nc + 15) & ~15u, offs = (4 * nc + 15) & ~15u;
    uint32_t recs = 0x30 + nested + offs, total = recs + 0x40 * nc;
    uint8_t *nt = tab_alloc(total);
    uint64_t rel = nt ? (uint64_t)(uintptr_t)nt - (uint64_t)(uintptr_t)ac : 0;
    if (!nt || (uintptr_t)nt < (uintptr_t)ac || rel > 0xffffffffull) return 0;
    memcpy(nt, tb, 0x30);
    wr32(nt + 0x10, total), wr32(nt + 0x14, nc), wr32(nt + 0x18, nc), wr32(nt + 0x20, 0x30 + nested);
    memcpy(nt + 0x30, tb + 0x30, 0x30);
    wr32(nt + 0x40, nested), wr32(nt + 0x44, nc), wr32(nt + 0x48, nc);
    memcpy(nt + 0x60, tb + 0x60, 0xc * count);
    for (int k = 0; k < nadd; k++) {
        uint8_t *dsc = nt + 0x60 + 0xc * (count + (uint32_t)k);
        wr32(dsc, 2), wr32(dsc + 4, rd32(add[k] + 0x30)), wr32(dsc + 8, rd32(add[k] + 0x34));
    }
    for (uint32_t i = 0; i < nc; i++) {
        wr32(nt + 0x30 + nested + 4 * i, recs + 0x40 * i);
        const uint8_t *src = i < count ? entry(tb, i) : add[i - count];
        if (src) memcpy(nt + recs + 0x40 * i, src, 0x40);
    }
    wr32(ac + rd32(ac + 0x20) + 4, (uint32_t)rel);
    return nadd;
}

// Her shape on the costume model m: the number of matrices changed (0: it had them already), -1: not understood
static int shape(uint8_t *m, const BodySettings *b, char *missing, size_t nmissing) {
    Shape s = {m, chunk(m, "NodeLay"), chunk(m, "GlblMtx"), chunk(m, "BnOfsMtx"), chunk(m, "HieLay"), 0};
    if (!s.nl || !s.gm || !s.bo || !s.hl) return -1;
    s.nodes = rd32(s.nl + 0x14);
    if (s.nodes != rd32(s.gm + 0x14) || s.nodes != rd32(s.bo + 0x14) || s.nodes != rd32(s.hl + 0x14) ||
        s.nodes > 4096)
        return -1;
    s.A = malloc(s.nodes * sizeof *s.A);
    s.set = calloc(s.nodes, sizeof *s.set);
    s.moved = calloc(s.nodes, sizeof *s.moved);
    if (!s.A || !s.set || !s.moved) return free(s.A), free(s.set), free(s.moved), -1;
    missing[0] = 0;
#define MISSING(what) snprintf(missing + strlen(missing), nmissing - strlen(missing), " %s", what)
    for (int side = 0; side < 2; side++) {
        const char *S = side ? "Right" : "Left", *X = side ? "R" : "L";
        char a[64], b2[64], c[64], d[64], e[64];
        if (b->v[BODY_BUST_SIZE] != 1) {
            snprintf(a, sizeof a, "OPT_Breast_%s_base", S), snprintf(b2, sizeof b2, "OPT_ywrk_%s", S);
            if (soft_part(&s, a, b2, b->v[BODY_BUST_SIZE]) < 0) MISSING("bust");
        }
        if (b->v[BODY_BUTT_SIZE] != 1) {
            snprintf(a, sizeof a, "OPT_Hips_%s_base", S), snprintf(b2, sizeof b2, "OPT_ywrk_hip_%s", S);
            if (soft_part(&s, a, b2, b->v[BODY_BUTT_SIZE]) < 0) MISSING("butt");
        }
        if (b->v[BODY_ARM_SIZE] != 1) {  // (upper arm with its twist bones and the elbow's, forearm with the wrist's)
            char arm[32], fore[32], hand[32];
            snprintf(arm, sizeof arm, "%sArm", S), snprintf(fore, sizeof fore, "%sForeArm", S);
            snprintf(hand, sizeof hand, "%sHand", S);
            snprintf(a, sizeof a, "OPT_AS_%s_ArmTwist_A", X), snprintf(b2, sizeof b2, "OPT_AS_%s_ArmTwist_B", X);
            snprintf(c, sizeof c, "OPT_AS_%s_Elbow", X);
            const char *upper[] = {a, b2, c, NULL};
            snprintf(d, sizeof d, "OPT_AS_%s_Wrist_A", X), snprintf(e, sizeof e, "OPT_AS_%s_Wrist_B", X);
            const char *lower[] = {d, e, NULL};
            if (girth(&s, mot(&s, arm), mot(&s, fore), b->v[BODY_ARM_SIZE], upper, NULL) < 0 ||
                girth(&s, mot(&s, fore), mot(&s, hand), b->v[BODY_ARM_SIZE], lower, NULL) < 0)
                MISSING("arms");
        }
        if (b->v[BODY_THIGH_SIZE] != 1) {  // (with its twist bones, the hip's, and the thigh's soft-body points)
            char up[32], leg[32];
            snprintf(up, sizeof up, "%sUpLeg", S), snprintf(leg, sizeof leg, "%sLeg", S);
            snprintf(a, sizeof a, "OPT_AS_%s_UpLeg_A", X), snprintf(b2, sizeof b2, "OPT_AS_%s_UpLeg_B", X);
            snprintf(c, sizeof c, "OPT_AS_%s_Hip", X), snprintf(d, sizeof d, "OPT_ywrk_leg_%s", S);
            const char *bones[] = {a, b2, c, NULL};
            if (girth(&s, mot(&s, up), mot(&s, leg), b->v[BODY_THIGH_SIZE], bones, d) < 0) MISSING("thighs");
        }
        if (b->v[BODY_CALF_SIZE] != 1) {  // (with the knee's bone)
            char leg[32], foot[32];
            snprintf(leg, sizeof leg, "%sLeg", S), snprintf(foot, sizeof foot, "%sFoot", S);
            snprintf(a, sizeof a, "OPT_AS_%s_Knee", X);
            const char *bones[] = {a, NULL};
            if (girth(&s, mot(&s, leg), mot(&s, foot), b->v[BODY_CALF_SIZE], bones, NULL) < 0) MISSING("calves");
        }
    }
    if (b->v[BODY_WAIST_SIZE] != 1) {  // (the waist bone, across the spine's line up to the chest)
        const char *none[] = {NULL};
        int w = mot(&s, "Waist");
        if (girth(&s, w, mot(&s, "Chest"), b->v[BODY_WAIST_SIZE], none, NULL) < 0) MISSING("waist");
        else s.col[s.ncol - 1].how = COL_ALL;
    }
#undef MISSING
    for (int part = 0; part < 2; part++) {  // (the chest's or hips' colliders out in front or behind, in its frame)
        int bone = mot(&s, part ? "Hips" : "Chest");
        const float *w = s.push[part], *B = bone >= 0 ? (const float *)entry(s.bo, (uint32_t)bone) : NULL;
        if (!B || w[0] * w[0] + w[1] * w[1] + w[2] * w[2] < 1e-8f) continue;
        float v[3];
        for (int k = 0; k < 3; k++) v[k] = w[0] * B[k] + w[1] * B[4 + k] + w[2] * B[8 + k];
        want_collider(&s, bone, COL_PUSH, 1, 1);
        memcpy(s.col[s.ncol - 1].v, v, sizeof v);
    }
    // (not twice on the same memory: the game builds a girl again from the files it has loaded)
    int changed = 0, first = -1;
    for (uint32_t i = 0; i < s.nodes && first < 0; i++)
        if (s.set[i]) first = (int)i;
    if (first >= 0 && !already(entry(s.bo, (uint32_t)first))) {
        for (uint32_t i = 0; i < s.nodes; i++)  // (the points first: they need their parents' bind as it was)
            if (s.moved[i]) move_point(&s, (int)i), changed++;
        for (uint32_t i = 0; i < s.nodes; i++)
            if (s.set[i] && !s.moved[i]) changed += once(entry(s.bo, i), scale_bind, s.A[i]);
        for (int q = 0; q < s.ncol; q++) changed += collider(m, &s.col[q]);
        if (b->v[BODY_BUST_SIZE] > 1 || b->v[BODY_BUST_WEIGHT] > 1)  // (after the others: not scaled again)
            changed += breast_colliders(&s);
    }
    free(s.A);
    free(s.set);
    free(s.moved);
    return changed;
}

// ---- bust weight, along gravity, and the breasts' contact with each other: every frame, after the game's soft-body
// update has written the breasts' soft-body points' world matrices (tools/patches.py SOFT_BODY_UPDATE):
//  - weight: each point is pulled down along the world's -y by k (its reach), its reach being how far out it is from
//    its breast's base along the breast's direction now (so the tip most; a breast that faces down, as she bends over,
//    stretches down; one that faces up, as she lies back, settles toward her chest), k = 0.35 (weight - 1);
//  - contact: each point's rest place is its base's matrix now times where it sits from its base in the bind pose;
//    how far a breast has moved toward the other one (sideways, along the line between the two bases) is the mean of
//    its points' moves from rest, the inner ones (nearest the other breast) counting most. When the two together
//    have moved toward each other by more than 5 mm, both are pushed back apart by that much, half each, the inner
//    points most (so the inner sides flatten against each other), times bust_contact. Swaying together (both the same
//    way) moves neither.
// Nothing is taken back: the game writes the points' matrices afresh every frame before its update (taking last
// frame's push back from them fed it into the physics, which ran away). A character is known by her resources record
// (Character +0x8, the one rt_body_files sees); her breasts' bones by the costume model's node numbers (the bone
// matrices' order), noted after her size is put on.
typedef struct {
    uint64_t res;
    int base[2], pts[2][24], npts[2];
    float dir[2][3];        // (the breast's direction from its base, in the base's own frame)
    float local[2][24][3];  // (each point's rest place, in its base's frame)
    float inner[2][24];     // (1: the point nearest the other breast in the bind pose, 0: the furthest)
    float k, contact;
    int arm[2][3];      // (each side's upper arm, forearm and hand joints: MOT08_LeftArm, MOT04_LeftForeArm ...)
    float arm_r[3];     // (their radii: upper arm, forearm, hand)
    float margin;       // (how far the skin reaches beyond a breast point)
    int pushes, updates, arm_pushes;
    float most_push;
} Weight;
static Weight g_weight[32];
static pthread_mutex_t g_weight_lock = PTHREAD_MUTEX_INITIALIZER;

static int model_node(const uint8_t *nl, uint32_t nodes, const char *name) {
    for (uint32_t i = 0; i < nodes; i++) {
        const uint8_t *no = entry(nl, i);
        if (no && !strcmp((const char *)no + 0x50, name)) return (int)i;
    }
    return -1;
}

static void weight_register(uint64_t resources, const BodySettings *b) {
    float k = b ? 0.35f * (b->v[BODY_BUST_WEIGHT] - 1) : 0;
    if (k > 0.7f) k = 0.7f;
    if (k < -0.3f) k = -0.3f;
    // (contact: for a bigger or heavier bust; as she is, the game's own breasts don't meet)
    float contact = b && (b->v[BODY_BUST_SIZE] > 1.004f || fabsf(k) >= 0.005f) ? b->v[BODY_BUST_CONTACT] : 0;
    pthread_mutex_lock(&g_weight_lock);
    Weight *w = NULL;
    for (int i = 0; i < 32 && !w; i++)
        if (g_weight[i].res == resources) w = &g_weight[i];
    if (fabsf(k) < 0.005f && contact < 0.005f) {  // (as she is: forget her)
        if (w) memset(w, 0, sizeof *w);
        pthread_mutex_unlock(&g_weight_lock);
        return;
    }
    for (int i = 0; i < 32 && !w; i++)
        if (!g_weight[i].res) w = &g_weight[i];
    if (!w) w = &g_weight[resources / 64 % 32];
    memset(w, 0, sizeof *w);
    const uint8_t *m = (const uint8_t *)(uintptr_t)*(uint64_t *)(uintptr_t)resources;
    const uint8_t *nl = m ? chunk(m, "NodeLay") : NULL, *gm = m ? chunk(m, "GlblMtx") : NULL;
    uint32_t nodes = nl ? rd32(nl + 0x14) : 0;
    for (int side = 0; side < 2 && gm && nodes <= 4096; side++) {
        char a[64], pts[64];
        snprintf(a, sizeof a, "OPT_Breast_%s_base", side ? "Right" : "Left");
        snprintf(pts, sizeof pts, "OPT_ywrk_%s", side ? "Right" : "Left");
        int base = model_node(nl, nodes, a);
        size_t pl = strlen(pts);
        if (base < 0) continue;
        const float *g = (const float *)entry(gm, (uint32_t)base);
        float d[3] = {0, 0, 0}, row2[3], lo = 1e9f, hi = -1e9f;
        for (int r = 0; r < 3; r++)
            row2[r] = g[r * 4] * g[r * 4] + g[r * 4 + 1] * g[r * 4 + 1] + g[r * 4 + 2] * g[r * 4 + 2];
        int n = 0;
        for (uint32_t i = 0; i < nodes && n < 24; i++) {
            const uint8_t *no = entry(nl, i);
            const char *nm = no ? (const char *)no + 0x50 : "";
            if (strncmp(nm, pts, pl) || !isdigit((unsigned char)nm[pl])) continue;
            const float *p = (const float *)entry(gm, i);
            float o[3];
            for (int ax = 0; ax < 3; ax++) o[ax] = p[12 + ax] - g[12 + ax], d[ax] += o[ax];
            for (int r = 0; r < 3; r++)  // (into the base's frame: its rows are its axes in the bind pose)
                w->local[side][n][r] = row2[r] > 1e-12f ? (o[0] * g[r * 4] + o[1] * g[r * 4 + 1] + o[2] * g[r * 4 + 2]) /
                                                              sqrtf(row2[r])
                                                        : 0;
            w->inner[side][n] = fabsf(p[12]);  // (how far from her middle, x = 0; made 1..0 below)
            lo = fminf(lo, w->inner[side][n]), hi = fmaxf(hi, w->inner[side][n]);
            w->pts[side][n++] = (int)i;
        }
        float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n < 4 || len < 1e-5f) continue;
        for (int i = 0; i < n; i++) w->inner[side][i] = hi - lo > 1e-5f ? (hi - w->inner[side][i]) / (hi - lo) : 1;
        for (int r = 0; r < 3; r++)
            w->dir[side][r] = row2[r] > 1e-12f ? (d[0] * g[r * 4] + d[1] * g[r * 4 + 1] + d[2] * g[r * 4 + 2]) /
                                                     (len * sqrtf(row2[r]))
                                               : 0;
        w->base[side] = base, w->npts[side] = n;
    }
    static const char *const ARM[2][3] = {{"MOT08_LeftArm", "MOT04_LeftForeArm", "MOT05_LeftHand"},
                                          {"MOT14_RightArm", "MOT10_RightForeArm", "MOT11_RightHand"}};
    for (int a = 0; a < 2; a++)
        for (int j = 0; j < 3; j++) w->arm[a][j] = nl && nodes <= 4096 ? model_node(nl, nodes, ARM[a][j]) : -1;
    float t = b ? b->v[BODY_ARM_SIZE] : 1;  // (Kasumi's: about 4.2 cm round the upper arm, 3.3 the forearm)
    w->arm_r[0] = 0.042f * t, w->arm_r[1] = 0.033f * t, w->arm_r[2] = 0.025f * t;
    w->margin = 0.018f * (b ? fmaxf(1, b->v[BODY_BUST_SIZE]) : 1);
    if (w->npts[0] || w->npts[1]) {
        w->res = resources, w->k = k, w->contact = contact;
        LOG("[body] bust weight %.2f (pull %.2f), contact %.2f: %d + %d soft-body points, arms %s\n",
            b->v[BODY_BUST_WEIGHT], k, contact, w->npts[0], w->npts[1],
            w->arm[0][2] >= 0 && w->arm[1][2] >= 0 && w->arm[0][0] >= 0 && w->arm[1][0] >= 0 ? "found" : "not found");
    }
    pthread_mutex_unlock(&g_weight_lock);
}

static Weight *weight_of(uint64_t part) {  // (g_weight_lock held)
    if (!part) return NULL;
    uint64_t ch = *(const uint64_t *)(uintptr_t)(part + 8), res = ch ? *(const uint64_t *)(uintptr_t)(ch + 8) : 0;
    for (int i = 0; i < 32 && res; i++)
        if (g_weight[i].res == res) return &g_weight[i];
    return NULL;
}

static void axes(const float *B, float a[3][3]) {  // (a matrix's rows, made unit)
    for (int r = 0; r < 3; r++) {
        float l = sqrtf(B[r * 4] * B[r * 4] + B[r * 4 + 1] * B[r * 4 + 1] + B[r * 4 + 2] * B[r * 4 + 2]);
        for (int ax = 0; ax < 3; ax++) a[r][ax] = l > 1e-6f ? B[r * 4 + ax] / l : 0;
    }
}

static void bust_contact(Weight *w, uint64_t mats) {
    const float *B[2];
    float A[2][3][3], n[3], len = 0;
    for (int side = 0; side < 2; side++) B[side] = (const float *)(uintptr_t)(mats + (uint64_t)w->base[side] * 0x40);
    for (int ax = 0; ax < 3; ax++) n[ax] = B[0][12 + ax] - B[1][12 + ax], len += n[ax] * n[ax];  // (right to left)
    len = sqrtf(len);
    if (len < 1e-4f) return;
    for (int ax = 0; ax < 3; ax++) n[ax] /= len;
    float moved[2], sww[2];
    for (int side = 0; side < 2; side++) {
        axes(B[side], A[side]);
        float s = 0, sw = 0;
        sww[side] = 0;
        for (int i = 0; i < w->npts[side]; i++) {
            const float *t = (const float *)(uintptr_t)(mats + (uint64_t)w->pts[side][i] * 0x40) + 12;
            const float *L = w->local[side][i];
            float l = 0;
            for (int ax = 0; ax < 3; ax++) {
                float rest = B[side][12 + ax] + L[0] * A[side][0][ax] + L[1] * A[side][1][ax] + L[2] * A[side][2][ax];
                l += (t[ax] - rest) * n[ax];
            }
            if (!side) l = -l;  // (toward the other breast: the left one toward -n, the right one toward +n)
            float wi = .25f + .75f * w->inner[side][i], wp = .5f + .5f * w->inner[side][i];
            s += wi * l, sw += wi, sww[side] += wi * wp;
        }
        moved[side] = s / sw, sww[side] /= sw;  // (sww: the push's mean share; the push below moves the mean by c sww)
    }
    float over = moved[0] + moved[1] - 0.005f;
    w->updates++, w->most_push = fmaxf(w->most_push, over + 0.005f);
    if (over <= 0) return;
    for (int side = 0; side < 2; side++) {
        float c = w->contact * over * .5f / sww[side], sgn = side ? -1.f : 1.f;
        for (int i = 0; i < w->npts[side]; i++) {
            float *t = (float *)(uintptr_t)(mats + (uint64_t)w->pts[side][i] * 0x40) + 12;
            float push = c * (.5f + .5f * w->inner[side][i]);
            for (int ax = 0; ax < 3; ax++) t[ax] += sgn * push * n[ax];
        }
    }
    w->pushes++;
}

// The arms: each breast point is kept out of the arms by the arm's radius and the skin beyond the point, the breast
// giving way (pressed in where an arm lies on it); capsules from the shoulder joint to the elbow, the elbow to the
// wrist, and the wrist 8 cm on. A point the arm has gone past (on the arm's far side from its breast's base) is put
// back on the base's side.
static int bust_arms(Weight *w, uint64_t mats) {
    int pushed = 0;
    for (int a = 0; a < 2; a++) {
        if (w->arm[a][0] < 0 || w->arm[a][1] < 0 || w->arm[a][2] < 0) continue;
        float J[4][3];
        for (int j = 0; j < 3; j++)
            memcpy(J[j], (const float *)(uintptr_t)(mats + (uint64_t)w->arm[a][j] * 0x40) + 12, 12);
        float h[3], hl = 0;
        for (int ax = 0; ax < 3; ax++) h[ax] = J[2][ax] - J[1][ax], hl += h[ax] * h[ax];
        hl = sqrtf(hl);
        if (hl < 1e-4f) continue;
        for (int ax = 0; ax < 3; ax++) J[3][ax] = J[2][ax] + h[ax] / hl * 0.08f;
        for (int side = 0; side < 2; side++) {
            const float *B = (const float *)(uintptr_t)(mats + (uint64_t)w->base[side] * 0x40) + 12;
            for (int i = 0; i < w->npts[side]; i++) {
                float *t = (float *)(uintptr_t)(mats + (uint64_t)w->pts[side][i] * 0x40) + 12;
                for (int seg = 0; seg < 3; seg++) {
                    const float *P = J[seg], *Q = J[seg + 1];
                    float u[3], v[3], uu = 0, f = 0;
                    for (int ax = 0; ax < 3; ax++) u[ax] = Q[ax] - P[ax], uu += u[ax] * u[ax];
                    for (int ax = 0; ax < 3; ax++) f += (t[ax] - P[ax]) * u[ax];
                    f = uu > 1e-8f ? fminf(1, fmaxf(0, f / uu)) : 0;
                    float c[3], d = 0, sb = 0, lim = w->arm_r[seg] + w->margin;
                    for (int ax = 0; ax < 3; ax++) c[ax] = P[ax] + f * u[ax], v[ax] = t[ax] - c[ax], d += v[ax] * v[ax];
                    d = sqrtf(d);
                    if (d >= lim || d < 1e-5f) continue;
                    for (int ax = 0; ax < 3; ax++) sb += v[ax] * (B[ax] - c[ax]);
                    float to = sb >= 0 ? lim : -lim;  // (the far side: back through to the base's)
                    for (int ax = 0; ax < 3; ax++) t[ax] = c[ax] + v[ax] / d * to;
                    pushed++;
                }
            }
        }
    }
    return pushed;
}

void rt_body_soft_post(uint64_t part, uint64_t mats) {
    pthread_mutex_lock(&g_weight_lock);
    Weight *w = weight_of(part);
    if (w && mats) {
        for (int side = 0; side < 2 && w->k; side++) {
            if (!w->npts[side]) continue;
            const float *B = (const float *)(uintptr_t)(mats + (uint64_t)w->base[side] * 0x40);
            float D[3], len = 0;
            for (int ax = 0; ax < 3; ax++)  // (the breast's direction now: its base's axes as they are)
                D[ax] = w->dir[side][0] * B[ax] + w->dir[side][1] * B[4 + ax] + w->dir[side][2] * B[8 + ax];
            for (int ax = 0; ax < 3; ax++) len += D[ax] * D[ax];
            len = sqrtf(len);
            if (len < 1e-5f) continue;
            for (int i = 0; i < w->npts[side]; i++) {
                float *t = (float *)(uintptr_t)(mats + (uint64_t)w->pts[side][i] * 0x40) + 12, r = 0;
                for (int ax = 0; ax < 3; ax++) r += (t[ax] - B[12 + ax]) * D[ax] / len;
                float pull = r > 0 ? w->k * r : 0;
                t[1] -= pull;
            }
        }
        if (w->contact > 0 && w->npts[0] && w->npts[1]) bust_contact(w, mats);
        if (w->contact > 0) w->arm_pushes += bust_arms(w, mats) > 0;
        static int told;
        if (w->updates && w->updates % 300 == 0 && told < 6) {  // (now and then, to see it working)
            told++;
            LOG("[body] bust contact: they moved toward each other up to %.1f cm; pushed apart in %d of %d updates; arms "
                "pressed in on them in %d\n",
                w->most_push * 100, w->pushes, w->updates, w->arm_pushes);
        }
    }
    pthread_mutex_unlock(&g_weight_lock);
}

void rt_body_files(uint32_t id, uint64_t resources) {
    load();
    const char *code = girl_code(id);
    static uint32_t logged;
    if (!g_nbody || !resources || !code) return;
    if (id < 32 && !(logged >> id & 1)) {
        logged |= 1u << id, LOG("[body] %s: character id %u\n", code, id);
        const char *probe = getenv("LARECOMP_BODY_PROBE");
        for (int i = 0; probe && atoi(probe) && i < 0xd8 / 8; i++) {  // (debugging: what each of her files is)
            const uint8_t *f = (const uint8_t *)(uintptr_t)((const uint64_t *)(uintptr_t)resources)[i];
            if (f) LOG("[body probe] %s file %#x (+%#x): %.8s\n", code, i, i * 8, (const char *)f);
        }
    }
    const BodySettings *b = NULL;
    for (int i = 0; i < g_nbody; i++)
        if (!strcmp(g_body[i].code, code)) b = &g_body[i];
    static const int shaped[] = {BODY_BUST_SIZE, BODY_BUTT_SIZE, BODY_THIGH_SIZE, BODY_ARM_SIZE, BODY_CALF_SIZE,
                                 BODY_WAIST_SIZE};
    bool any = false;
    for (size_t k = 0; b && k < sizeof shaped / sizeof *shaped; k++) any |= b->v[shaped[k]] != 1;
    if (!any) {
        weight_register(resources, b);
        return;
    }
    uint8_t *model = (uint8_t *)(uintptr_t)*(uint64_t *)(uintptr_t)resources;  // (the costume model: resources +0x0)
    if (!model || strncmp((const char *)model, "TMC", 4)) return;
    char missing[96];
    int n = shape(model, b, missing, sizeof missing);
    weight_register(resources, b);  // (after the size: the points' rest places are where it puts them)
    static int told;
    if (told++ < 16) {
        char what[256] = "";
        for (size_t k = 0; k < sizeof shaped / sizeof *shaped; k++)
            if (b->v[shaped[k]] != 1)
                snprintf(what + strlen(what), sizeof what - strlen(what), "%s%s %.2f", *what ? ", " : "",
                         BODY_KEYS[shaped[k]].key, b->v[shaped[k]]);
        LOG("[body] %s: %s: %s%s%s\n", code, what,
            n < 0 ? "her costume model isn't understood, left as it is" : n ? "her bones changed" : "already changed",
            *missing ? "; not found:" : "", missing);
    }
}

// ---- LARECOMP_BODY_PROBE=1 (debugging): every 20 s, the accessory physics' colliders as the game has them
// (*0x7000a8c900: the manager; its collider list at +0xc0: count, capacity, items; each 0x7f0 bytes, its kind at
// +0x7e0), the first 0x80 bytes of each as floats, to see what a body setting reached
void rt_body_probe(void) {
    static int on = -1, n;
    static uint64_t next;
    if (on < 0) on = getenv("LARECOMP_BODY_PROBE") && atoi(getenv("LARECOMP_BODY_PROBE"));
    if (!on || n >= 8 || plat_now_ns() < next) return;
    next = plat_now_ns() + 20000000000ull, n++;
    uint64_t mgr = *(volatile uint64_t *)(uintptr_t)0x7000a8c900ull;
    if (!mgr) return;
    uint64_t count = *(uint64_t *)(uintptr_t)(mgr + 0xc0), items = *(uint64_t *)(uintptr_t)(mgr + 0xd0);
    LOG("[body probe] %llu colliders\n", (unsigned long long)count);
    for (uint64_t i = 0; i < count && i < 64 && items; i++) {
        const uint8_t *c = (const uint8_t *)(uintptr_t)*(uint64_t *)(uintptr_t)(items + 8 * i);
        if (!c) continue;
        char line[1024];
        int k = snprintf(line, sizeof line, "[body probe] %2llu kind %llu:", (unsigned long long)i,
                         (unsigned long long)*(const uint64_t *)(c + 0x7e0));
        for (int f = 0; f < 32 && k > 0 && k < (int)sizeof line - 16; f++) {
            float x;
            memcpy(&x, c + 4 * f, 4);
            k += snprintf(line + k, sizeof line - k, " %.3f", x);
        }
        LOG("%s\n", line);
    }
}
