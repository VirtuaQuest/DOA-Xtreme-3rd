// DOAX3 Recomp launcher (Windows): three tabs, then Play starts build/doax3recomp.exe as tools/run.sh does.
//   Game      the display mode (windowed, borderless fullscreen, fullscreen), the resolution, the 3D resolution, the
//             upscaler, the frame rate, anti-aliasing, texture filtering, Show FPS and background controller input
//   Controls  a controller drawn with the game's button on each of its buttons, the controller button each of the
//             game's buttons is on, and two templates: the game's default (A on the right face button, as on a
//             Switch) and A and B, X and Y swapped (A at the bottom, as on an Xbox pad)
//   Mods      the mods in mods/ (on or off: an off mod's folder name starts with _), and each girl's body settings:
//             bust size, and breast physics like another girl's, in body.ini beside larecomp.ini, which the game reads
//             after every mod's body.ini (runtime/hle/body_shape.c, runtime/mods.c; docs/MODDING.md, "Body shape")
// The Game and Controls choices go into larecomp.ini (fullscreen, fullscreen_mode, window_size, render3d, frame_rate,
// antialias, anisotropy, upscaler, show_fps, background_input: runtime/settings.c; the controller's bind_pad_<button>
// lines, runtime/hle/bindings.c, only for buttons off the game's default, which is what a missing line means), so the
// game's own menu and F11 keep working with them; every other line of the file is kept as it is (bind_key_* too).
// Play saves everything: larecomp.ini, body.ini, then a mod turned on or off is renamed.
// The look: the game's colors, painted (sky over the sea), and the player's own art when it's there, read when the
// launcher starts (never built in: the launcher is shipped prebuilt and has nothing from the game): the home menu
// cover (extracted/control/icon_AmericanEnglish.dat) and the title logo the build takes from the game files
// (build/launcher_art/logo.png, tools/make_launcher_art.py). Without them the title is text.
// Two options, for RecompLauncher (recomplauncher.json), open no window: --prepare builds the game from the player's files
// and --play starts it. Without them the launcher is as it was.
#define WIN32_LEAN_AND_MEAN  // (UNICODE: -municode)
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <math.h>
#include <objbase.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wincodec.h>

enum { ID_MODE = 100, ID_RES, ID_R3D, ID_NOTE, ID_FPS, ID_AA, ID_AF, ID_UPS, ID_SHOWFPS, ID_BGIN, ID_FPSHINT, ID_SAVE,
       ID_SAVENOTE };
enum { ID_TAB0 = 200 };  // (the tabs: ID_TAB0 + TAB_*)
enum { ID_BIND0 = 300, ID_PRESET_SWITCH = 400, ID_PRESET_OTHER, ID_PSTATUS, ID_PADPIC, ID_PADHINT };  // (Controls)
enum { ID_MOD = 500, ID_MOD_ON, ID_MOD_OPEN, ID_MOD_NOTE, ID_GIRL, ID_GIRL_RESET,
       ID_SIZE_LIKE, ID_PHYS, ID_AMOUNT, ID_AMOUNT_VAL, ID_BODY_NOTE, ID_VAL0 = 600, ID_VALTXT0 = 650 };  // (Mods: ID_VAL0 + K_*, its slider)
enum { MODE_WINDOWED, MODE_BORDERLESS, MODE_FULLSCREEN };
enum { TAB_GAME, TAB_CONTROLS, TAB_MODS, NTABS };

static wchar_t g_exe_dir[MAX_PATH], g_root[MAX_PATH], g_game[MAX_PATH], g_ini[MAX_PATH], g_title[0x201];
static HWND g_mode, g_res, g_r3d, g_note, g_fps, g_fpshint, g_aa, g_af, g_ups, g_showfps, g_bgin;
static HFONT g_font, g_bold, g_semi, g_big, g_sym;
static int g_dpi = 96;
static struct { int w, h; } g_sizes[128];
static int g_nsizes, g_desk_w, g_desk_h;

static const wchar_t *const k_tab_names[NTABS] = {L"Game", L"Controls", L"Mods"};
static const wchar_t *const k_modes[] = {L"Windowed", L"Borderless fullscreen", L"Fullscreen"};
static const char *const k_r3d_keys[] = {"auto", "original", "1080p", "1440p", "4k"};  // (larecomp.ini render3d)
static const wchar_t *const k_r3d_names[] = {L"Default for your GPU", L"Original (1600x900)", L"1920x1080",
                                             L"2560x1440", L"3840x2160 (may run out of memory)"};
// Frame rate (frame_rate: 30, 60, 120), anti-aliasing (antialias) and texture filtering (anisotropy)
static const wchar_t *const k_fps_names[] = {L"30 (as on the Switch)", L"60 (experimental)", L"120 (experimental)"};
// The line under Frame rate: for the choice that's selected, or the one the pointer is over in the open list. 60 and
// 120 change Beach Volleyball (the other girls miss the ball more) and some menus' hold-to-scroll (docs/CHANGES.md).
static const wchar_t *const k_fps_hints[] = {
    L"The game's own speed, as on the Switch.",
    L"Experimental: the Volleyball AI may not work as intended, and some menus scroll faster.",
    L"Experimental: the Volleyball AI may not work as intended, and some menus scroll faster."};
static const wchar_t k_fps_tip[] =
    L"60 and 120 fps are experimental. The game is smoother, but the Beach Volleyball AI may not work as intended "
    L"(the other girls miss the ball more and rallies are shorter), and holding a direction in some menus scrolls "
    L"faster. 30 is the game's own frame rate, as on the Switch.";
static int g_fps_hint = -1;  // (which hint the line shows)
static const wchar_t *const k_aa_names[] = {L"Off", L"SMAA"};
static const wchar_t *const k_af_names[] = {L"As on the Switch", L"2x anisotropic", L"4x anisotropic", L"8x anisotropic",
                                            L"16x anisotropic"};
static const int k_af_vals[] = {0, 2, 4, 8, 16};
// Upscaler: how the game's 1080p image is scaled to the window or screen (auto: the game's default for the GPU, FSR 1
// on a dedicated one, bilinear on an integrated one)
static const char *const k_ups_keys[] = {"auto", "fsr1", "bilinear"};
static const wchar_t *const k_ups_names[] = {L"Default for your GPU", L"AMD FSR 1 (sharper)", L"Bilinear (softer)"};
// ---- the controller: the game's buttons (bindings.c k_ini, the first 16) and the controller's buttons by position
// (bindings.c k_pad_ini; the first 16 are the ones drawn). The default: A on the right face button as on a Switch;
// Swapped: A and B swapped, X and Y swapped (A at the bottom, as on an Xbox pad).
enum { PAD_SWITCH, PAD_OTHER, PAD_CUSTOM };
#define NSLOTS 16
static const char *const k_slot_keys[NSLOTS] = {"a",    "b",    "x",   "y",  "lstick", "rstick", "l",     "r",
                                                "zl",   "zr",   "plus", "minus", "left", "up",   "right", "down"};
static const wchar_t *const k_slot_names[NSLOTS] = {
    L"A", L"B", L"X", L"Y", L"L stick press", L"R stick press", L"L", L"R", L"ZL", L"ZR", L"Plus (+)",
    L"Minus (-)", L"D-pad Left", L"D-pad Up", L"D-pad Right", L"D-pad Down"};
static const wchar_t *const k_slot_glyph[NSLOTS] = {  // (on the drawn controller)
    L"A", L"B", L"X", L"Y", L"LS", L"RS", L"L", L"R", L"ZL", L"ZR", L"+", L"\x2212", L"\x2190", L"\x2191", L"\x2192",
    L"\x2193"};
static const char *const k_slot_default[NSLOTS] = {
    "east", "south", "north", "west", "lstick", "rstick", "lshoulder", "rshoulder", "ltrigger", "rtrigger", "start",
    "back", "dpad_left", "dpad_up", "dpad_right", "dpad_down"};
static const char *const k_face_other[4] = {"south", "east", "west", "north"};  // (A, B, X, Y in Swapped)
// The Controls tab's two columns, top to bottom, about where the default puts each button on the controller
static const int k_col_left[8] = {8, 6, 11, 4, 13, 12, 14, 15}, k_col_right[8] = {9, 7, 10, 2, 3, 0, 1, 5};
static const struct { const char *key; const wchar_t *name; } k_hosts[] = {
    {"south", L"Bottom (Xbox A, PS Cross)"},
    {"east", L"Right (Xbox B, PS Circle)"},
    {"west", L"Left (Xbox X, PS Square)"},
    {"north", L"Top (Xbox Y, PS Triangle)"},
    {"lshoulder", L"LB / L1 / L"},
    {"rshoulder", L"RB / R1 / R"},
    {"ltrigger", L"LT / L2 / ZL"},
    {"rtrigger", L"RT / R2 / ZR"},
    {"start", L"Start / Options / +"},
    {"back", L"Back / Share / -"},
    {"lstick", L"Left stick press"},
    {"rstick", L"Right stick press"},
    {"dpad_left", L"D-pad Left"},
    {"dpad_up", L"D-pad Up"},
    {"dpad_right", L"D-pad Right"},
    {"dpad_down", L"D-pad Down"},
    {"rpaddle1", L"Right paddle 1 (Elite P1)"},
    {"rpaddle2", L"Right paddle 2 (Elite P2)"},
    {"lpaddle1", L"Left paddle 1 (Elite P3)"},
    {"lpaddle2", L"Left paddle 2 (Elite P4)"},
    {"misc1", L"Share / Mute / Capture"},
    {"touchpad", L"PS touchpad press"},
    {"guide", L"Guide / PS / Home"},
    {"misc2", L"Extra button 2"},
    {"misc3", L"Extra button 3"},
    {"misc4", L"Extra button 4"},
    {"misc5", L"Extra button 5"},
    {"misc6", L"Extra button 6"},
};
#define NHOSTS ((int)(sizeof k_hosts / sizeof *k_hosts))
#define NDRAWN 16  // (the controller buttons in the picture: k_hosts' first 16)
static const wchar_t *const k_pad_names[] = {L"Default (Switch layout)", L"Swapped (A\x2194" L"B, X\x2194" L"Y)",
                                             L"Custom"};
// Bindings as k_hosts indexes: the game's default, Swapped, and the launcher's (saved with Play)
static int g_def[NSLOTS], g_other[NSLOTS], g_bind[NSLOTS];

// ---- the girls (body.ini sections: runtime/hle/body_shape.c GIRL_IDS). bust: how far her breasts stand out next to
// Kasumi's body, which all but Marie Rose and Honoka wear (tools/mods/doaxmod.py body measures it).
#define NGIRLS 11
static const struct { const char *code, *name; float bust; } k_girls[NGIRLS] = {
    {"KAS", "Kasumi", 1}, {"MAR", "Marie Rose", .63f}, {"HON", "Honoka", 1.06f}, {"AYA", "Ayane", 1},
    {"NYO", "Nyotengu", 1}, {"KOK", "Kokoro", 1}, {"HTM", "Hitomi", 1}, {"MOM", "Momiji", 1}, {"HEL", "Helena", 1},
    {"MIS", "Misaki", 1}, {"LEI", "Leifang", 1},
};
#define CALMEST 1  // (Marie Rose: her breasts move least)

static int px(int v) { return MulDiv(v, g_dpi, 96); }
static int pxf(float v) { return (int)floorf(v * g_dpi / 96.f + .5f); }

static BOOL exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

// ---- larecomp.ini: "key = value" lines
static char *g_lines[512];
static int g_nlines;

static void ini_load(void) {
    FILE *f = _wfopen(g_ini, L"rb");
    if (!f) return;
    char line[1024];
    while (g_nlines < 512 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        g_lines[g_nlines++] = _strdup(line);
    }
    fclose(f);
}

static int ini_find(const char *key) {
    size_t n = strlen(key);
    for (int i = 0; i < g_nlines; i++) {
        const char *s = g_lines[i];
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, key, n) && (s[n] == ' ' || s[n] == '=')) return i;
    }
    return -1;
}

static const char *ini_get(const char *key) {  // (the value, or "")
    int i = ini_find(key);
    if (i < 0) return "";
    const char *v = strchr(g_lines[i], '=');
    if (!v) return "";
    for (v++; *v == ' '; v++) {}
    return v;
}

static void ini_set(const char *key, const char *val) {
    char line[256];
    snprintf(line, sizeof line, "%s = %s", key, val);
    int i = ini_find(key);
    if (i >= 0) free(g_lines[i]), g_lines[i] = _strdup(line);
    else if (g_nlines < 512) g_lines[g_nlines++] = _strdup(line);
}

static void ini_del(const char *key) {
    int i = ini_find(key);
    if (i < 0) return;
    free(g_lines[i]);
    memmove(g_lines + i, g_lines + i + 1, (g_nlines - i - 1) * sizeof *g_lines);
    g_nlines--;
}

static BOOL ini_save(void) {
    FILE *f = _wfopen(g_ini, L"wb");
    if (!f) return FALSE;
    for (int i = 0; i < g_nlines; i++) fprintf(f, "%s\n", g_lines[i]);
    return fclose(f) == 0;
}

// ---- what the launcher finds: the game program next to it, the data folder (the one with extracted/) here or above
static BOOL find_paths(wchar_t *err, size_t n) {
    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    *wcsrchr(g_exe_dir, L'\\') = 0;
    swprintf(g_game, MAX_PATH, L"%ls\\doax3recomp.exe", g_exe_dir);
    if (!exists(g_game))  // (a release folder: the launcher on top, the game program built into build/)
        swprintf(g_game, MAX_PATH, L"%ls\\build\\doax3recomp.exe", g_exe_dir);
    if (!exists(g_game)) {
        swprintf(err, n, L"doax3recomp.exe isn't next to the launcher (%ls) or in build there. Build it first: run "
                 L"Setup.bat.", g_exe_dir);
        return FALSE;
    }
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\extracted", g_exe_dir);
    wcscpy(g_root, g_exe_dir);
    if (!exists(p)) {  // (the build tree: build/ under the repo root)
        wchar_t *s = wcsrchr(g_root, L'\\');
        if (s) *s = 0;
        swprintf(p, MAX_PATH, L"%ls\\extracted", g_root);
        if (!exists(p)) {
            swprintf(err, n, L"The game files (an \"extracted\" folder) aren't next to the launcher or in the folder "
                     L"above it. Extract them with tools/nsp_extract.py first.");
            return FALSE;
        }
    }
    swprintf(g_ini, MAX_PATH, L"%ls\\larecomp.ini", g_root);
    return TRUE;
}

// The game's name from its control.nacp (16 titles of 0x200 bytes of name and 0x100 of publisher; the first one set)
static void load_title(void) {
    wcscpy(g_title, L"DEAD OR ALIVE Xtreme 3 Scarlet");
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\extracted\\control\\control.nacp", g_root);
    FILE *f = _wfopen(p, L"rb");
    if (!f) return;
    char name[0x201] = {0};
    for (int i = 0; i < 16; i++)
        if (fseek(f, i * 0x300L, SEEK_SET) || fread(name, 1, 0x200, f) != 0x200 || name[0]) break;
    fclose(f);
    if (name[0]) MultiByteToWideChar(CP_UTF8, 0, name, -1, g_title, 0x201);
}

static int size_cmp(const void *a, const void *b) {
    const int *x = a, *y = b;
    return x[0] != y[0] ? y[0] - x[0] : y[1] - x[1];
}

static void load_sizes(void) {  // the primary display's modes (unique sizes, largest first)
    DEVMODEW dm = {.dmSize = sizeof dm};
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm)) g_desk_w = dm.dmPelsWidth, g_desk_h = dm.dmPelsHeight;
    for (DWORD i = 0; EnumDisplaySettingsW(NULL, i, &dm) && g_nsizes < 128; i++) {
        int w = dm.dmPelsWidth, h = dm.dmPelsHeight, dup = 0;
        if (w < 800 || h < 600 || (g_desk_w && (w > g_desk_w || h > g_desk_h))) continue;
        for (int k = 0; k < g_nsizes; k++) dup |= g_sizes[k].w == w && g_sizes[k].h == h;
        if (!dup) g_sizes[g_nsizes].w = w, g_sizes[g_nsizes++].h = h;
    }
    if (!g_nsizes) g_sizes[0].w = 1280, g_sizes[0].h = 720, g_nsizes = 1;
    qsort(g_sizes, g_nsizes, sizeof *g_sizes, size_cmp);
}

static int size_index(int w, int h) {
    for (int i = 0; i < g_nsizes; i++)
        if (g_sizes[i].w == w && g_sizes[i].h == h) return i;
    return -1;
}

// ---- controller bindings
static int host_index(const char *v) {  // (a larecomp.ini value: -1 when it's no button the game knows)
    size_t n = strcspn(v, " \t");
    for (int h = 0; h < NHOSTS; h++)
        if (strlen(k_hosts[h].key) == n && !strncmp(v, k_hosts[h].key, n)) return h;
    return -1;
}

static void init_binds(void) {
    for (int i = 0; i < NSLOTS; i++) g_def[i] = g_other[i] = host_index(k_slot_default[i]);
    for (int i = 0; i < 4; i++) g_other[i] = host_index(k_face_other[i]);
    memcpy(g_bind, g_def, sizeof g_bind);
}

static int pad_layout(const int *b) {
    return !memcmp(b, g_def, sizeof g_def) ? PAD_SWITCH : !memcmp(b, g_other, sizeof g_other) ? PAD_OTHER : PAD_CUSTOM;
}

// As the game's own menu (bindings.c assign): the game button `slot` goes onto the controller button `host`, and a game
// button that was on it takes `slot`'s old button, so no two share one. The one that moved (-1: none).
static int bind_assign(int *b, int slot, int host) {
    int moved = -1;
    for (int i = 0; i < NSLOTS; i++)
        if (i != slot && b[i] == host) moved = i, b[i] = b[slot];
    b[slot] = host;
    return moved;
}

// ---- body settings, girl by girl: the launcher's body.ini (beside larecomp.ini), which the game reads after every
// mod's, so its settings win; and the mods (mods/<name>, off when the name starts with _), some with a body.ini of
// their own (runtime/hle/body_shape.c)
#define MAXMODS 64
// The settings: the numbers (1: as she is; runtime/hle/body_shape.c BODY_KEYS has the same names and ranges), then the
// breast physics (K_PHYS: the k_girls index whose physics she takes, -1 her own; K_AMOUNT: how far)
enum { K_BUST, K_BUST_JIG, K_BUTT, K_BUTT_JIG, K_THIGH, K_THIGH_JIG, K_ARM, K_CALF, K_WAIST, K_WEIGHT, K_CONTACT, NVAL, K_PHYS = NVAL,
       K_AMOUNT, NKEYS };
static const struct { const char *key; const wchar_t *label; float lo, hi; BOOL pct; } k_vals[NVAL] = {
    {"bust_size", L"Bust size", .3f, 2.5f, FALSE},   {"bust_jiggle", L"Bust jiggle", 0, 2, TRUE},
    {"butt_size", L"Butt size", .5f, 2.5f, FALSE},   {"butt_jiggle", L"Butt jiggle", 0, 2, TRUE},
    {"thigh_size", L"Thighs", .7f, 2.0f, FALSE},     {"thigh_jiggle", L"Thigh jiggle", 0, 2, TRUE},
    {"arm_size", L"Arms", .7f, 2.0f, FALSE},         {"calf_size", L"Calves", .7f, 2.0f, FALSE},
    {"waist_size", L"Waist", .7f, 1.8f, FALSE},       {"bust_weight", L"Bust weight", .5f, 3, TRUE},
    {"bust_contact", L"Bust contact", 0, 1, TRUE},
};
// A girl's settings in one body.ini; has[]: the ones the file sets ("bust_physics = none" sets her own over a mod's)
typedef struct { float v[NVAL], amount; int phys; BOOL has[NKEYS]; } Body;
typedef struct {
    wchar_t name[MAX_PATH];  // (without an off mod's _)
    BOOL on, was_on;         // (was_on: as its folder is named now)
    Body body[NGIRLS];
    BOOL changes[NGIRLS];  // (the girls whose game files it replaces)
} Mod;
static Mod g_mods[MAXMODS];
static Body g_body[NGIRLS];  // (the launcher's own body.ini)
static BOOL g_body_dirty;
static int g_nmod, g_cur_mod = -1, g_girl;

static void body_reset(Body *b) {
    memset(b, 0, sizeof *b);
    for (int k = 0; k < NVAL; k++) b->v[k] = 1;
    b->amount = 1, b->phys = -1;
}

static BOOL body_any(const Body *b) {
    for (int k = 0; k < NKEYS; k++)
        if (b->has[k]) return TRUE;
    return FALSE;
}

static BOOL body_not_default(const Body *b) {  // (she isn't as the game has her)
    for (int k = 0; k < NVAL; k++)
        if (fabsf(b->v[k] - 1) > .004f) return TRUE;
    return b->phys >= 0;
}

static int girl_index(const char *s) {  // (a code or a name, as runtime/hle/body_shape.c reads them)
    for (int i = 0; i < NGIRLS; i++) {
        size_t first = strcspn(k_girls[i].name, " ");
        if (!_stricmp(s, k_girls[i].code) || !_stricmp(s, k_girls[i].name) ||
            (strlen(s) == first && !_strnicmp(s, k_girls[i].name, first)))
            return i;
    }
    return -1;
}

static void mod_folder(const Mod *m, BOOL on, wchar_t *out) {
    swprintf(out, MAX_PATH, L"%ls\\mods\\%ls%ls", g_root, on ? L"" : L"_", m->name);
}

static void body_path(wchar_t *out) { swprintf(out, MAX_PATH + 16, L"%ls\\body.ini", g_root); }

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void body_load(const wchar_t *path, Body *body) {
    for (int g = 0; g < NGIRLS; g++) body_reset(&body[g]);
    FILE *f = _wfopen(path, L"rb");
    if (!f) return;
    char line[512];
    int cur = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "#;")] = 0;
        char *s = trim(line);
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) *e = 0;
            cur = girl_index(trim(s + 1));
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || cur < 0) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        Body *b = &body[cur];
        float x = (float)atof(v);
        int g = girl_index(v);
        for (int i = 0; i < NVAL; i++)
            if (!strcmp(k, k_vals[i].key) && x >= k_vals[i].lo && x <= k_vals[i].hi) b->v[i] = x, b->has[i] = TRUE;
        if (!strcmp(k, "bust_physics") && (g >= 0 || !*v || !_stricmp(v, "none")))
            b->phys = g != cur ? g : -1, b->has[K_PHYS] = TRUE;
        else if (!strcmp(k, "bust_physics_amount") && x >= 0 && x <= 1) b->amount = x, b->has[K_AMOUNT] = TRUE;
    }
    fclose(f);
}

static void body_over(Body *r, const Body *b) {  // (b's settings over r's)
    for (int k = 0; k < NVAL; k++)
        if (b->has[k]) r->v[k] = b->v[k];
    if (b->has[K_PHYS]) r->phys = b->phys;
    if (b->has[K_AMOUNT]) r->amount = b->amount;
}

// What the mods that are on give girl g (in the game's order, by name: a later one's setting wins); with the
// launcher's settings over them (mine), what she gets
static Body body_of(int g, BOOL mine) {
    Body r;
    body_reset(&r);
    BOOL done[MAXMODS] = {0};
    for (;;) {
        int next = -1;
        for (int i = 0; i < g_nmod; i++)
            if (g_mods[i].on && !done[i] && (next < 0 || wcscmp(g_mods[i].name, g_mods[next].name) < 0)) next = i;
        if (next < 0) break;
        done[next] = TRUE;
        body_over(&r, &g_mods[next].body[g]);
    }
    if (mine) body_over(&r, &g_body[g]);
    return r;
}

// The launcher's setting for girl g: kept only when it differs from what the mods give her (phys: for K_PHYS)
static void body_put(int g, int key, float x, int phys) {
    Body base = body_of(g, FALSE), *b = &g_body[g];
    if (key < NVAL) b->v[key] = x, b->has[key] = fabsf(x - base.v[key]) > .004f;
    else if (key == K_PHYS) b->phys = phys, b->has[K_PHYS] = phys != base.phys;
    else b->amount = x, b->has[K_AMOUNT] = fabsf(x - base.amount) > .004f;
    g_body_dirty = TRUE;
}

static void mod_scan_changes(Mod *m) {  // (romfs/<package>/character/<girl>/...)
    wchar_t base[MAX_PATH], p[MAX_PATH];
    mod_folder(m, m->was_on, base);
    swprintf(p, MAX_PATH, L"%ls\\romfs\\*", base);
    WIN32_FIND_DATAW pk, ch;
    HANDLE h = FindFirstFileW(p, &pk);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(pk.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || pk.cFileName[0] == L'.') continue;
        swprintf(p, MAX_PATH, L"%ls\\romfs\\%ls\\character\\*", base, pk.cFileName);
        HANDLE h2 = FindFirstFileW(p, &ch);
        if (h2 == INVALID_HANDLE_VALUE) continue;
        do {
            char code[16];
            if (!(ch.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || wcslen(ch.cFileName) > 8) continue;
            WideCharToMultiByte(CP_UTF8, 0, ch.cFileName, -1, code, sizeof code, NULL, NULL);
            int g = girl_index(code);
            if (g >= 0 && strlen(code) == 3) m->changes[g] = TRUE;
        } while (FindNextFileW(h2, &ch));
        FindClose(h2);
    } while (FindNextFileW(h, &pk));
    FindClose(h);
}

static int mod_cmp(const void *a, const void *b) { return _wcsicmp(((const Mod *)a)->name, ((const Mod *)b)->name); }

static void mods_load(void) {
    wchar_t p[MAX_PATH + 16];
    body_path(p);
    body_load(p, g_body);
    swprintf(p, MAX_PATH, L"%ls\\mods\\*", g_root);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' || g_nmod == MAXMODS) continue;
        Mod *m = &g_mods[g_nmod++];
        memset(m, 0, sizeof *m);
        m->on = m->was_on = fd.cFileName[0] != L'_';
        wcscpy(m->name, fd.cFileName + !m->on);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(g_mods, g_nmod, sizeof *g_mods, mod_cmp);
    for (int i = 0; i < g_nmod; i++) {
        mod_folder(&g_mods[i], g_mods[i].was_on, p);
        wcscat(p, L"\\body.ini");
        body_load(p, g_mods[i].body);
        mod_scan_changes(&g_mods[i]);
    }
}

// body.ini as lines, rewritten setting by setting: other lines (comments, other girls) stay as they are
static char *g_bl[1024];
static int g_nbl;

static BOOL bl_blank(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return !*s || *s == '#' || *s == ';';
}

static BOOL bl_head(int i, const char *code) {  // (a [section] line; code NULL: any)
    const char *s = g_bl[i];
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '[') return FALSE;
    if (!code) return TRUE;
    char t[64];
    snprintf(t, sizeof t, "%s", s + 1);
    t[strcspn(t, "]")] = 0;
    return girl_index(trim(t)) == girl_index(code);
}

static void bl_insert(int at, const char *s) {
    if (g_nbl >= 1024) return;
    memmove(g_bl + at + 1, g_bl + at, (g_nbl - at) * sizeof *g_bl);
    g_bl[at] = _strdup(s);
    g_nbl++;
}

static void bl_delete(int at) {
    free(g_bl[at]);
    memmove(g_bl + at, g_bl + at + 1, (g_nbl - at - 1) * sizeof *g_bl);
    g_nbl--;
}

static int bl_end(int head) {  // (after the section's last line that isn't blank)
    int j = head + 1;
    while (j < g_nbl && !bl_head(j, NULL)) j++;
    while (j > head + 1 && bl_blank(g_bl[j - 1])) j--;
    return j;
}

static int bl_key(int head, const char *key) {
    size_t n = strlen(key);
    for (int j = head + 1; j < g_nbl && !bl_head(j, NULL); j++) {
        const char *s = g_bl[j];
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, key, n) && (s[n] == ' ' || s[n] == '\t' || s[n] == '=')) return j;
    }
    return -1;
}

static BOOL body_save(wchar_t *err, size_t n) {  // (the launcher's body.ini)
    wchar_t p[MAX_PATH + 16];
    body_path(p);
    BOOL any = FALSE;
    for (int g = 0; g < NGIRLS; g++) any |= body_any(&g_body[g]);
    g_nbl = 0;
    FILE *f = _wfopen(p, L"rb");
    if (!f && !any) return g_body_dirty = FALSE, TRUE;
    char line[1024];
    if (f) {
        while (g_nbl < 1024 && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            g_bl[g_nbl++] = _strdup(line);
        }
        fclose(f);
    } else {
        bl_insert(0, "# Body settings from the launcher's Mods tab (docs/MODDING.md, \"Body shape\"). The game reads them "
                     "when it starts,");
        bl_insert(1, "# after every mod's body.ini, so these win. One section per girl, by her code; delete a line (or "
                     "the file) to undo.");
        bl_insert(2, "");
    }
    for (int g = 0; g < NGIRLS; g++) {
        const Body *b = &g_body[g];
        char key[NKEYS][32], val[NKEYS][32];
        for (int k = 0; k < NKEYS; k++) {
            snprintf(key[k], 32, "%s", k < NVAL ? k_vals[k].key : k == K_PHYS ? "bust_physics" : "bust_physics_amount");
            val[k][0] = 0;
            if (!b->has[k]) continue;
            if (k < NVAL) snprintf(val[k], 32, "%.2f", b->v[k]);
            else if (k == K_PHYS) snprintf(val[k], 32, "%s", b->phys >= 0 ? k_girls[b->phys].code : "none");
            else snprintf(val[k], 32, "%.2f", b->amount);
        }
        int head = -1;
        for (int j = 0; j < g_nbl && head < 0; j++)
            if (bl_head(j, k_girls[g].code)) head = j;
        if (head < 0) {
            if (!body_any(b)) continue;
            if (g_nbl && !bl_blank(g_bl[g_nbl - 1])) bl_insert(g_nbl, "");
            snprintf(line, sizeof line, "[%s]", k_girls[g].code);
            bl_insert(g_nbl, line);
            head = g_nbl - 1;
        }
        for (int k = 0; k < NKEYS; k++) {
            int j = bl_key(head, key[k]);
            if (!val[k][0]) {
                if (j >= 0) bl_delete(j);
                continue;
            }
            snprintf(line, sizeof line, "%s = %s", key[k], val[k]);
            if (j >= 0) free(g_bl[j]), g_bl[j] = _strdup(line);
            else bl_insert(bl_end(head), line);
        }
        BOOL left = FALSE;  // (a section with no settings left: its header goes too)
        for (int j = head + 1; j < g_nbl && !bl_head(j, NULL); j++) left |= !bl_blank(g_bl[j]);
        if (!left) bl_delete(head);
    }
    f = _wfopen(p, L"wb");
    BOOL ok = f != NULL;
    for (int j = 0; j < g_nbl; j++) {
        if (f) fprintf(f, "%s\n", g_bl[j]);
        free(g_bl[j]);
    }
    g_nbl = 0;
    if (f) ok = fclose(f) == 0;
    if (!ok) swprintf(err, n, L"Couldn't write %ls.", p);
    else g_body_dirty = FALSE;
    return ok;
}

// Everything the Mods tab changed: the launcher's body.ini, then the mods turned on or off (their folders renamed)
static BOOL mods_save(wchar_t *err, size_t n) {
    if (g_body_dirty && !body_save(err, n)) return FALSE;
    for (int i = 0; i < g_nmod; i++) {
        Mod *m = &g_mods[i];
        if (m->on == m->was_on) continue;
        wchar_t from[MAX_PATH], to[MAX_PATH];
        mod_folder(m, m->was_on, from);
        mod_folder(m, m->on, to);
        if (!MoveFileW(from, to)) {
            swprintf(err, n, L"Couldn't turn the mod %ls %ls: renaming its folder failed (error %lu). Is the game "
                     L"running, or a window open in that folder?", m->name, m->on ? L"on" : L"off", GetLastError());
            return FALSE;
        }
        m->was_on = m->on;
    }
    return TRUE;
}

// ---- Game tab
static void update_note(void) {
    int mode = (int)SendMessageW(g_mode, CB_GETCURSEL, 0, 0);
    EnableWindow(g_res, mode != MODE_BORDERLESS);
    wchar_t t[160];
    if (mode == MODE_BORDERLESS) swprintf(t, 160, L"Borderless uses your desktop resolution (%dx%d).", g_desk_w, g_desk_h);
    else if (mode == MODE_FULLSCREEN) swprintf(t, 160, L"Fullscreen switches the display to this resolution.");
    else swprintf(t, 160, L"The window's size; it can also be resized while playing.");
    SetWindowTextW(g_note, t);
}

static void set_fps_hint(int i) {
    if (i < 0 || i > 2 || i == g_fps_hint) return;
    g_fps_hint = i;
    SetWindowTextW(g_fpshint, k_fps_hints[i]);
    InvalidateRect(g_fpshint, NULL, FALSE);
}

// The Frame rate list, while it's open: the line under it follows the choice the pointer is over
static WNDPROC g_fps_list_proc;
static LRESULT CALLBACK fps_list_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEMOVE) {
        LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, lp);
        if (!HIWORD(r)) set_fps_hint(LOWORD(r));
    }
    return CallWindowProcW(g_fps_list_proc, h, msg, wp, lp);
}

static void load_choices(void) {
    int fs = atoi(ini_get("fullscreen")), excl = !strcmp(ini_get("fullscreen_mode"), "exclusive");
    SendMessageW(g_mode, CB_SETCURSEL, !fs ? MODE_WINDOWED : excl ? MODE_FULLSCREEN : MODE_BORDERLESS, 0);
    int w = 0, h = 0, sel = -1;
    if (sscanf(ini_get("window_size"), "%dx%d", &w, &h) == 2) sel = size_index(w, h);
    if (sel < 0) sel = fs ? size_index(g_desk_w, g_desk_h) : size_index(1280, 720);
    SendMessageW(g_res, CB_SETCURSEL, sel < 0 ? 0 : sel, 0);
    int r3d = 0;
    for (int i = 1; i < 5; i++)
        if (!strcmp(ini_get("render3d"), k_r3d_keys[i])) r3d = i;
    SendMessageW(g_r3d, CB_SETCURSEL, r3d, 0);
    int fr = atoi(ini_get("frame_rate"));  // (no line: 30; the fps60 / fps120 lines of v0.1.4 are ignored, as
                                           // the game does: everyone starts at 30 again)
    SendMessageW(g_fps, CB_SETCURSEL, fr >= 120 ? 2 : fr >= 60 ? 1 : 0, 0);
    const char *aa = ini_get("antialias");  // (the game: anything but "off" is SMAA; no line: off)
    SendMessageW(g_aa, CB_SETCURSEL, *aa && strcmp(aa, "off") ? 1 : 0, 0);
    int af = 0;
    for (int i = 1; i < 5; i++)
        if (atoi(ini_get("anisotropy")) >= k_af_vals[i]) af = i;
    SendMessageW(g_af, CB_SETCURSEL, af, 0);
    const char *ups = ini_get("upscaler");  // ("metalfx": the macOS name of the sharp one)
    SendMessageW(g_ups, CB_SETCURSEL, !strcmp(ups, "bilinear") ? 2 : !*ups || !strcmp(ups, "auto") ? 0 : 1, 0);
    SendMessageW(g_showfps, BM_SETCHECK, atoi(ini_get("show_fps")) ? BST_CHECKED : BST_UNCHECKED, 0);
    const char *bg = ini_get("background_input");  // (no line: on, the game's default on Windows)
    SendMessageW(g_bgin, BM_SETCHECK, !*bg || atoi(bg) ? BST_CHECKED : BST_UNCHECKED, 0);
    for (int i = 0; i < NSLOTS; i++) {  // (no line, or a name the game doesn't know: the game's default)
        char key[32];
        snprintf(key, sizeof key, "bind_pad_%s", k_slot_keys[i]);
        int h = host_index(ini_get(key));
        g_bind[i] = h >= 0 ? h : g_def[i];
    }
    update_note();
}

static BOOL save_choices(void) {
    int mode = (int)SendMessageW(g_mode, CB_GETCURSEL, 0, 0);
    int res = (int)SendMessageW(g_res, CB_GETCURSEL, 0, 0), r3d = (int)SendMessageW(g_r3d, CB_GETCURSEL, 0, 0);
    ini_set("fullscreen", mode == MODE_WINDOWED ? "0" : "1");
    if (mode != MODE_WINDOWED) ini_set("fullscreen_mode", mode == MODE_FULLSCREEN ? "exclusive" : "borderless");
    if (mode != MODE_BORDERLESS && res >= 0) {  // (borderless: the windowed size chosen before stays)
        char v[32];
        snprintf(v, sizeof v, "%dx%d", g_sizes[res].w, g_sizes[res].h);
        ini_set("window_size", v);
    }
    ini_set("render3d", k_r3d_keys[r3d < 0 ? 0 : r3d]);
    int fps = (int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0), af = (int)SendMessageW(g_af, CB_GETCURSEL, 0, 0);
    ini_set("frame_rate", fps == 2 ? "120" : fps == 1 ? "60" : "30");
    ini_del("fps60");  // (v0.1.4's lines: the game no longer reads them)
    ini_del("fps120");
    ini_set("antialias", SendMessageW(g_aa, CB_GETCURSEL, 0, 0) == 1 ? "smaa" : "off");
    char afv[8];
    snprintf(afv, sizeof afv, "%d", k_af_vals[af < 0 ? 0 : af]);
    ini_set("anisotropy", afv);
    int ups = (int)SendMessageW(g_ups, CB_GETCURSEL, 0, 0);
    ini_set("upscaler", k_ups_keys[ups < 0 ? 0 : ups]);
    ini_set("show_fps", SendMessageW(g_showfps, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
    ini_set("background_input", SendMessageW(g_bgin, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
    for (int i = 0; i < NSLOTS; i++) {  // (as the game saves them: a line only for a button off its default)
        char key[32];
        snprintf(key, sizeof key, "bind_pad_%s", k_slot_keys[i]);
        if (g_bind[i] != g_def[i]) ini_set(key, k_hosts[g_bind[i]].key);
        else
            while (ini_find(key) >= 0) ini_del(key);
    }
    return ini_save();
}

// As tools/run.sh: the data folder as the working directory and LARECOMP_DATA_ROOT, the MSYS2 libraries on PATH when
// they aren't next to the program, the log in doax3.log. proc: the game's process handle (NULL: let it go)
static BOOL launch(wchar_t *err, size_t n, HANDLE *proc) {
    SetEnvironmentVariableW(L"LARECOMP_DATA_ROOT", g_root);
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\SDL3.dll", g_exe_dir);
    if (!exists(p) && exists(L"C:\\msys64\\ucrt64\\bin\\SDL3.dll")) {
        static wchar_t path[32768];
        wcscpy(path, L"C:\\msys64\\ucrt64\\bin;");
        GetEnvironmentVariableW(L"PATH", path + wcslen(path), 32768 - (DWORD)wcslen(path));
        SetEnvironmentVariableW(L"PATH", path);
    }
    swprintf(p, MAX_PATH, L"%ls\\doax3.log", g_root);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE log = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si = {.cb = sizeof si};
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = si.hStdError = log;
    }
    wchar_t cmd[MAX_PATH + 32];
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" extracted", g_game);
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(g_game, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_root, &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) { swprintf(err, n, L"Couldn't start %ls (error %lu).", g_game, GetLastError()); return FALSE; }
    CloseHandle(pi.hThread);
    if (proc) *proc = pi.hProcess;
    else CloseHandle(pi.hProcess);
    return TRUE;
}

// ---- RecompLauncher: no window. --prepare runs setup\Setup.ps1 -Prepare, which builds the game and prints the
// launcher's "progress 0.4 Text" lines on our output. --play starts the game and returns its exit code; when an update of
// this project has come since the game was last built (release.txt against build\built-from.txt), it builds it again
// first, in the setup window, with the game files already extracted.
enum { EXIT_NOT_SET_UP = 20, EXIT_REBUILD_FAILED = 21 };
static wchar_t g_proj[MAX_PATH];  // (the folder with setup\: next to the launcher or above it)

static BOOL find_project(void) {
    GetModuleFileNameW(NULL, g_proj, MAX_PATH);
    *wcsrchr(g_proj, L'\\') = 0;
    for (int up = 0; up < 2; up++) {
        wchar_t p[MAX_PATH];
        swprintf(p, MAX_PATH, L"%ls\\setup\\Setup.ps1", g_proj);
        if (exists(p)) return TRUE;
        wchar_t *s = wcsrchr(g_proj, L'\\');
        if (!s) break;
        *s = 0;
    }
    return FALSE;
}

static DWORD run_setup(const wchar_t *args) {  // setup\Setup.ps1 with args, our output its output; its exit code
    static wchar_t cmd[8192];
    swprintf(cmd, 8192, L"powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File \"%ls\\setup\\Setup.ps1\" %ls",
             g_proj, args);
    STARTUPINFOW si = {.cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES};
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE std[3] = {si.hStdInput, si.hStdOutput, si.hStdError};
    for (int i = 0; i < 3; i++)
        if (std[i] && std[i] != INVALID_HANDLE_VALUE) SetHandleInformation(std[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_proj, &si, &pi)) return 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static void add_path_arg(wchar_t *out, size_t n, const wchar_t *name, const wchar_t *path) {
    wchar_t p[MAX_PATH];
    wcsncpy(p, path, MAX_PATH - 1);
    p[MAX_PATH - 1] = 0;
    size_t k = wcslen(p);
    while (k > 3 && (p[k - 1] == L'\\' || p[k - 1] == L'/')) p[--k] = 0;  // (a trailing \ would escape the quote)
    size_t used = wcslen(out);
    swprintf(out + used, n - used, L" %ls \"%ls\"", name, p);
}

static int prepare(int argc, wchar_t **argv) {
    if (!find_project()) return 1;
    wchar_t args[4096] = L"-Prepare";
    for (int i = 1; i + 1 < argc; i++) {
        if (!wcscmp(argv[i], L"--game")) add_path_arg(args, 4096, L"-Game", argv[++i]);
        else if (!wcscmp(argv[i], L"--keys")) add_path_arg(args, 4096, L"-Keys", argv[++i]);
    }
    return (int)run_setup(args);
}

static BOOL read_word(const wchar_t *path, char *out, size_t n) {  // the file's first line, trimmed
    *out = 0;
    FILE *f = _wfopen(path, L"rb");
    if (!f) return FALSE;
    if (!fgets(out, (int)n, f)) *out = 0;
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
    size_t k = strlen(out);
    while (k && (out[k - 1] == ' ' || out[k - 1] == '\t')) out[--k] = 0;
    return TRUE;
}

static BOOL needs_rebuild(void) {
    wchar_t p[MAX_PATH];
    char release[128], built[128];
    swprintf(p, MAX_PATH, L"%ls\\release.txt", g_proj);
    if (!read_word(p, release, sizeof release)) return FALSE;  // (a source checkout: built by hand)
    swprintf(p, MAX_PATH, L"%ls\\build\\built-from.txt", g_proj);
    return !read_word(p, built, sizeof built) || strcmp(release, built);
}

static int play(void) {
    if (!find_project()) return EXIT_NOT_SET_UP;
    wchar_t p[MAX_PATH], err[512];
    swprintf(p, MAX_PATH, L"%ls\\extracted\\exefs\\main", g_proj);
    if (!exists(p)) return EXIT_NOT_SET_UP;
    if (needs_rebuild() && (run_setup(L"-AutoStart -KeepExtracted -CloseWhenDone") || needs_rebuild()))
        return EXIT_REBUILD_FAILED;
    if (!find_paths(err, 512)) return EXIT_NOT_SET_UP;
    HANDLE proc;
    if (!launch(err, 512, &proc)) return EXIT_NOT_SET_UP;
    WaitForSingleObject(proc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return (int)code;
}

// ---- the look: the game's colors, a bright sky over the sea with a white and a pink line along the shore between them,
// painted once into a bitmap that the window and its controls copy from. The labels (owner-drawn statics), buttons,
// tabs and check boxes (custom draw), the sliders and the controller picture are drawn here, so their text can be
// white on the sea; the combo boxes are the system's own.
#define SKY_TOP RGB(0xF4, 0xFC, 0xFF)
#define SKY_BOTTOM RGB(0x9A, 0xDF, 0xF7)
#define SEA_TOP RGB(0x0B, 0x52, 0xB2)
#define SEA_BOTTOM RGB(0x0A, 0x86, 0xC0)
#define PINK RGB(0xFF, 0x3D, 0x9A)
#define DEEP RGB(0x0B, 0x3D, 0x91)
#define YELLOW RGB(0xFF, 0xEC, 0x8C)
#define SHADOW RGB(0x03, 0x2B, 0x60)
#define WHITE RGB(0xFF, 0xFF, 0xFF)
#define WARN RGB(0xFF, 0xB4, 0x6A)  // (the Frame rate line's warning)
#define NO_SHADOW ((COLORREF)-1)
// (a control's GWLP_USERDATA; ROLE_FPS: the Frame rate line, a note in WARN while it warns; ROLE_CENTER: a note
// centered; ROLE_VALUE: a slider's value)
enum { ROLE_LABEL, ROLE_NOTE, ROLE_HEAD, ROLE_FPS, ROLE_CENTER, ROLE_VALUE, ROLE_BUTTON = 0x100, ROLE_PRIMARY,
       ROLE_CHECK, ROLE_TAB };
enum { MAIN_W = 860, MAIN_H = 600, MAIN_HEADER = 122, TAB_Y = 138, PAD_W = 240, PAD_H = 180 };

typedef struct { HDC dc; HBITMAP bmp; HGDIOBJ old; int w, h; } Backdrop;
static Backdrop g_main_bd;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static COLORREF mix(COLORREF a, COLORREF b, float t) {
    t = clamp01(t);
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + .5f),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + .5f),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + .5f));
}

static HBITMAP make_dib(int w, int h, DWORD **bits) {  // (32-bit, top-down; premultiplied alpha where it's blended)
    BITMAPINFO bi = {.bmiHeader = {sizeof bi.bmiHeader, w, -h, 1, 32, BI_RGB}};
    *bits = NULL;
    HBITMAP b = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)bits, NULL, 0);
    return b && *bits ? b : (b ? DeleteObject(b) : 0, NULL);
}

static void blend(HDC dc, HBITMAP bmp, int x, int y, int w, int h) {
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, w, h, mem, 0, 0, w, h, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// Signed distance from a pixel's center to the edge of a w x h rectangle with corners of radius rad (< 0: inside)
static float round_dist(int i, int j, int w, int h, float rad) {
    float hw = w / 2.f, hh = h / 2.f;
    if (rad > hw) rad = hw;
    if (rad > hh) rad = hh;
    float qx = fabsf(i + .5f - hw) - (hw - rad), qy = fabsf(j + .5f - hh) - (hh - rad);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;
}

// A rounded rectangle with smooth edges: a top-to-bottom gradient, a border bw pixels wide, alpha its opacity
static void round_rect(HDC dc, int x, int y, int w, int h, int rad, COLORREF top, COLORREF bottom, COLORREF border,
                       int bw, int alpha) {
    DWORD *p;
    HBITMAP bmp = w > 0 && h > 0 ? make_dib(w, h, &p) : NULL;
    if (!bmp) return;
    for (int j = 0; j < h; j++) {
        COLORREF fill = mix(top, bottom, h > 1 ? (float)j / (h - 1) : 0);
        for (int i = 0; i < w; i++) {
            float d = round_dist(i, j, w, h, (float)rad), a = clamp01(.5f - d) * alpha / 255.f;
            COLORREF c = bw > 0 ? mix(fill, border, d + bw + .5f) : fill;
            p[j * w + i] = (DWORD)(a * 255 + .5f) << 24 | (DWORD)(GetRValue(c) * a + .5f) << 16 |
                           (DWORD)(GetGValue(c) * a + .5f) << 8 | (DWORD)(GetBValue(c) * a + .5f);
        }
    }
    blend(dc, bmp, x, y, w, h);
    DeleteObject(bmp);
}

static void text(HDC dc, const wchar_t *t, RECT r, UINT fmt, COLORREF color, COLORREF shadow) {
    SetBkMode(dc, TRANSPARENT);
    if (shadow != NO_SHADOW) {
        RECT s = r;
        OffsetRect(&s, px(1), px(1));
        SetTextColor(dc, shadow);
        DrawTextW(dc, t, -1, &s, fmt);
    }
    SetTextColor(dc, color);
    DrawTextW(dc, t, -1, &r, fmt);
}

// ---- the player's own art, read when the launcher starts (WIC: JPEG, PNG, ...), scaled with good quality to fit in
// maxw x maxh, its corners rounded by `round` pixels; premultiplied for AlphaBlend. NULL when it isn't there.
static IWICImagingFactory *g_wic;

static HBITMAP load_image(const wchar_t *path, int maxw, int maxh, int round, int *ow, int *oh) {
    if (!exists(path) || maxw <= 0 || maxh <= 0) return NULL;
    if (!g_wic && FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                          (void **)&g_wic)))
        return g_wic = NULL, NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *conv = NULL;
    IWICBitmapScaler *scaler = NULL;
    HBITMAP bmp = NULL;
    UINT sw = 0, sh = 0;
    // (premultiplied before scaling, so transparent pixels' colors don't bleed into the edges)
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(g_wic, path, NULL, GENERIC_READ,
                                                            WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) || FAILED(IWICBitmapFrameDecode_GetSize(frame, &sw, &sh)) ||
        !sw || !sh || FAILED(IWICImagingFactory_CreateFormatConverter(g_wic, &conv)) ||
        FAILED(IWICFormatConverter_Initialize(conv, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPBGRA,
                                              WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(IWICImagingFactory_CreateBitmapScaler(g_wic, &scaler)))
        goto done;
    double s = fmin((double)maxw / sw, (double)maxh / sh);
    int w = (int)(sw * s + .5), h = (int)(sh * s + .5);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    // (4: high quality cubic, Windows 10 and later; else Fant)
    if (FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)conv, w, h, (WICBitmapInterpolationMode)4)) &&
        FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)conv, w, h, WICBitmapInterpolationModeFant)))
        goto done;
    DWORD *p;
    bmp = make_dib(w, h, &p);
    if (!bmp) goto done;
    if (FAILED(IWICBitmapScaler_CopyPixels(scaler, NULL, w * 4, w * 4 * h, (BYTE *)p))) {
        DeleteObject(bmp);
        bmp = NULL;
        goto done;
    }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            BYTE *c = (BYTE *)&p[j * w + i];
            float k = round > 0 ? clamp01(.5f - round_dist(i, j, w, h, (float)round)) : 1;
            for (int n = 0; n < 4; n++) c[n] = (BYTE)((n < 3 && c[n] > c[3] ? c[3] : c[n]) * k + .5f);
        }
    *ow = w, *oh = h;
done:
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (conv) IWICFormatConverter_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    return bmp;
}

// The window's background: the sky down to `header` (the shore a long, slow wave), then the sea
static void backdrop_make(Backdrop *bd, HWND hwnd, int header) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    DWORD *p;
    memset(bd, 0, sizeof *bd);
    if (w <= 0 || h <= header || !(bd->bmp = make_dib(w, h, &p))) return;
    bd->w = w, bd->h = h;
    bd->dc = CreateCompatibleDC(NULL);
    bd->old = SelectObject(bd->dc, bd->bmp);
    float amp = (float)px(4), white = (float)px(2), pink = (float)px(4);
    const COLORREF band[3] = {WHITE, PINK, 0};
    for (int x = 0; x < w; x++) {
        float s0 = header + amp * sinf(6.2831853f * (x + .5f) / w * 1.3f + .8f), s1 = s0 + white, s2 = s1 + pink;
        for (int y = 0; y < h; y++) {
            float c = y + .5f, a0 = clamp01(c - s0 + .5f), a1 = clamp01(c - s1 + .5f), a2 = clamp01(c - s2 + .5f);
            COLORREF col[4] = {mix(SKY_TOP, SKY_BOTTOM, (float)y / header), band[0], band[1],
                               mix(SEA_TOP, SEA_BOTTOM, (float)(y - header) / (h - header))};
            float wt[4] = {1 - a0, a0 - a1, a1 - a2, a2}, r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++)
                r += wt[k] * GetRValue(col[k]), g += wt[k] * GetGValue(col[k]), b += wt[k] * GetBValue(col[k]);
            p[y * w + x] = (DWORD)(r + .5f) << 16 | (DWORD)(g + .5f) << 8 | (DWORD)(b + .5f);
        }
    }
}

static void backdrop_free(Backdrop *bd) {
    if (bd->dc) SelectObject(bd->dc, bd->old), DeleteDC(bd->dc);
    if (bd->bmp) DeleteObject(bd->bmp);
    memset(bd, 0, sizeof *bd);
}

static void backdrop_blit(HWND child, HDC dc, const RECT *r) {  // (the part of the parent's background under r)
    HWND parent = GetParent(child);
    Backdrop *bd = (Backdrop *)GetWindowLongPtrW(parent, GWLP_USERDATA);
    POINT o = {0, 0};
    MapWindowPoints(child, parent, &o, 1);
    if (bd && bd->dc)
        BitBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, bd->dc, o.x + r->left, o.y + r->top,
               SRCCOPY);
    else FillRect(dc, r, GetSysColorBrush(COLOR_WINDOW));
}

// The main window's header: the home menu cover, then the title logo, or the title as text without it
static void paint_main_header(Backdrop *bd) {
    if (!bd->dc) return;
    HDC dc = bd->dc;
    int x = px(20), right = bd->w - px(20), top = px(11), w, h;
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\extracted\\control\\icon_AmericanEnglish.dat", g_root);
    HBITMAP art = load_image(p, px(98), px(98), px(7), &w, &h);
    if (art) {
        int f = px(3);
        round_rect(dc, x - f + px(1), top - f + px(2), w + 2 * f, h + 2 * f, px(10), SHADOW, SHADOW, 0, 0, 70);
        round_rect(dc, x - f, top - f, w + 2 * f, h + 2 * f, px(10), WHITE, WHITE, 0, 0, 255);
        blend(dc, art, x, top, w, h);
        DeleteObject(art);
        x += w + px(18);
    }
    swprintf(p, MAX_PATH, L"%ls\\build\\launcher_art\\logo.png", g_root);
    if ((art = load_image(p, right - x, px(MAIN_HEADER - 16), 0, &w, &h))) {
        blend(dc, art, x + (right - x - w) / 2, (px(MAIN_HEADER) - h) / 2, w, h);
        DeleteObject(art);
        return;
    }
    static const wchar_t sub[] = L"PC launcher";
    RECT t = {x, 0, right, 0}, s = {x, 0, right, 0};
    SelectObject(dc, g_big);
    DrawTextW(dc, g_title, -1, &t, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, g_bold);
    DrawTextW(dc, sub, -1, &s, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    int th = t.bottom, sh = s.bottom, y = (px(MAIN_HEADER) - th - px(2) - sh) / 2;
    t = (RECT){x, y, right, y + th};
    s = (RECT){x, t.bottom + px(2), right, t.bottom + px(2) + sh};
    SelectObject(dc, g_big);
    text(dc, g_title, t, DT_WORDBREAK | DT_NOPREFIX, DEEP, NO_SHADOW);
    SelectObject(dc, g_bold);
    text(dc, sub, s, DT_SINGLELINE | DT_NOPREFIX, PINK, NO_SHADOW);
}

static void draw_label(const DRAWITEMSTRUCT *d) {
    int role = (int)GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA);
    backdrop_blit(d->hwndItem, d->hDC, &d->rcItem);
    wchar_t t[1024];
    GetWindowTextW(d->hwndItem, t, 1024);
    HGDIOBJ old = SelectObject(d->hDC, (HFONT)SendMessageW(d->hwndItem, WM_GETFONT, 0, 0));
    UINT fmt = DT_NOPREFIX | (role == ROLE_NOTE || role == ROLE_FPS ? DT_WORDBREAK
                              : role == ROLE_CENTER                  ? DT_WORDBREAK | DT_CENTER
                                                                     : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    COLORREF c = role == ROLE_LABEL || role == ROLE_VALUE ? WHITE : role == ROLE_FPS && g_fps_hint > 0 ? WARN : YELLOW;
    if (!IsWindowEnabled(d->hwndItem)) c = RGB(0x9C, 0xBC, 0xDC);
    text(d->hDC, t, d->rcItem, fmt, c, SHADOW);
    SelectObject(d->hDC, old);
}

static int g_tab;  // (the tab shown)

static LRESULT draw_button(const NMCUSTOMDRAW *cd) {
    if (cd->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    HWND b = cd->hdr.hwndFrom;
    HDC dc = cd->hdc;
    RECT r;
    GetClientRect(b, &r);
    int role = (int)GetWindowLongPtrW(b, GWLP_USERDATA), bw = px(1);
    UINT st = cd->uItemState;
    BOOL down = (st & CDIS_SELECTED) != 0, hot = (st & CDIS_HOT) != 0, off = (st & CDIS_DISABLED) != 0;
    BOOL focus = (st & CDIS_FOCUS) && !(SendMessageW(b, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
    wchar_t t[128];
    GetWindowTextW(b, t, 128);
    backdrop_blit(b, dc, &r);
    HFONT font = (HFONT)SendMessageW(b, WM_GETFONT, 0, 0);
    HGDIOBJ old = SelectObject(dc, font);
    if (role == ROLE_CHECK) {  // a white box, pink with a white tick when checked
        int s = px(16), y = (r.bottom - s) / 2;
        BOOL on = SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED;
        COLORREF edge = hot || down ? YELLOW : on ? WHITE : RGB(0x7F, 0xD3, 0xF2);
        round_rect(dc, px(1), y, s, s, px(4), on ? RGB(0xFF, 0x62, 0xB0) : WHITE, on ? PINK : RGB(0xE0, 0xF4, 0xFF),
                   edge, hot ? 2 * bw : bw, off ? 140 : 255);
        if (on) {
            RECT c = {px(1), y, px(1) + s, y + s};
            SelectObject(dc, g_sym);
            text(dc, L"\x2713", c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, WHITE, NO_SHADOW);
            SelectObject(dc, font);
        }
        RECT tr = {s + px(9), 0, r.right, r.bottom};
        text(dc, t, tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX, off ? RGB(0xB0, 0xC8, 0xE0) : WHITE, SHADOW);
        if (focus) {
            RECT f = tr;
            DrawTextW(dc, t, -1, &f, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            int fh = f.bottom - f.top;
            f.top = (r.bottom - fh) / 2 - px(1), f.bottom = f.top + fh + px(2), f.left -= px(2);
            f.right = f.right + px(2) < r.right ? f.right + px(2) : r.right;
            DrawFocusRect(dc, &f);
        }
    } else if (role == ROLE_TAB && GetDlgCtrlID(b) - ID_TAB0 == g_tab) {  // the tab shown: white, a pink edge
        round_rect(dc, 0, 0, r.right, r.bottom, px(8), WHITE, RGB(0xDC, 0xF2, 0xFC), PINK, 2 * bw, 255);
        text(dc, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, DEEP, NO_SHADOW);
        if (focus) {
            RECT f = r;
            InflateRect(&f, -px(4), -px(4));
            DrawFocusRect(dc, &f);
        }
    } else {  // pink (Play) or deep blue, rounded (a tab not shown too)
        COLORREF top, bottom, border, shadow;
        if (role == ROLE_PRIMARY)
            top = RGB(0xFF, 0x74, 0xB8), bottom = RGB(0xE8, 0x1E, 0x7C), border = RGB(0xFF, 0xC8, 0xE2),
            shadow = RGB(0x9C, 0x0E, 0x4E);
        else
            top = RGB(0x22, 0x66, 0xC8), bottom = RGB(0x0C, 0x34, 0x88), border = RGB(0x8C, 0xD8, 0xF6), shadow = SHADOW;
        if (hot && !down) top = mix(top, WHITE, .2f), bottom = mix(bottom, WHITE, .12f), border = WHITE;
        if (down) {
            COLORREF k = top;
            top = mix(bottom, 0, .15f), bottom = k;
        }
        if (off) top = bottom = RGB(0x80, 0xA0, 0xC4), border = RGB(0xA8, 0xC4, 0xE0);
        round_rect(dc, 0, 0, r.right, r.bottom, px(8), top, bottom, border, bw, 255);
        RECT tr = r;
        if (down) OffsetRect(&tr, 0, px(1));
        text(dc, t, tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, off ? RGB(0xE0, 0xE8, 0xF2) : WHITE,
             off ? NO_SHADOW : shadow);
        if (focus) {
            RECT f = r;
            InflateRect(&f, -px(4), -px(4));
            DrawFocusRect(dc, &f);
        }
    }
    SelectObject(dc, old);
    return CDRF_SKIPDEFAULT;
}

// A slider of its own (the system's trackbar, themed, loses what custom draw paints): a rounded track, pink up to a
// round white thumb. It takes the trackbar's TBM_SETRANGE, TBM_SETPOS, TBM_GETPOS, TBM_SETPAGESIZE and TBM_SETLINESIZE,
// and tells its parent of a move with WM_HSCROLL (lParam: the slider), as a trackbar does. The arrow keys move it by a
// line, Page Up / Page Down by a page, Home / End to the ends.
typedef struct { int lo, hi, pos, page, line; BOOL drag; } Slider;

static void slider_rects(HWND s, const Slider *d, int *x0, int *x1, int *thumb) {
    RECT r;
    GetClientRect(s, &r);
    int rad = px(9);
    *x0 = rad, *x1 = r.right - rad;
    *thumb = d->hi > d->lo ? *x0 + MulDiv(d->pos - d->lo, *x1 - *x0, d->hi - d->lo) : *x0;
}

static void slider_set(HWND s, Slider *d, int v, BOOL notify) {
    v = v < d->lo ? d->lo : v > d->hi ? d->hi : v;
    if (v == d->pos) return;
    d->pos = v;
    InvalidateRect(s, NULL, FALSE);
    if (notify) SendMessageW(GetParent(s), WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, v), (LPARAM)s);
}

static LRESULT CALLBACK sliderproc(HWND s, UINT msg, WPARAM wp, LPARAM lp) {
    Slider *d = (Slider *)GetWindowLongPtrW(s, 0);
    switch (msg) {
    case WM_NCCREATE:
        d = calloc(1, sizeof *d);
        if (!d) return FALSE;
        d->hi = 100, d->page = 10, d->line = 1;
        SetWindowLongPtrW(s, 0, (LONG_PTR)d);
        break;
    case WM_NCDESTROY:
        free(d);
        SetWindowLongPtrW(s, 0, 0);
        break;
    case TBM_SETRANGE: d->lo = LOWORD(lp), d->hi = HIWORD(lp); InvalidateRect(s, NULL, FALSE); return 0;
    case TBM_SETPAGESIZE: d->page = (int)lp; return 0;
    case TBM_SETLINESIZE: d->line = (int)lp; return 0;
    case TBM_SETPOS: slider_set(s, d, (int)lp, FALSE); return 0;
    case TBM_GETPOS: return d->pos;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_ERASEBKGND: return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(s, NULL, FALSE);
        return 0;
    case WM_KEYDOWN: {
        int v = d->pos;
        switch (wp) {
        case VK_LEFT: case VK_DOWN: v -= d->line; break;
        case VK_RIGHT: case VK_UP: v += d->line; break;
        case VK_PRIOR: v += d->page; break;
        case VK_NEXT: v -= d->page; break;
        case VK_HOME: v = d->lo; break;
        case VK_END: v = d->hi; break;
        default: return 0;
        }
        slider_set(s, d, v, TRUE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(s);
        SetCapture(s);
        d->drag = TRUE;
        // fallthrough
    case WM_MOUSEMOVE:
        if (d->drag) {
            int x0, x1, t;
            slider_rects(s, d, &x0, &x1, &t);
            int x = (short)LOWORD(lp);
            slider_set(s, d, x1 > x0 ? d->lo + MulDiv(x - x0, d->hi - d->lo, x1 - x0) : d->lo, TRUE);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (d->drag && msg == WM_LBUTTONUP) ReleaseCapture();
        d->drag = FALSE;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(s, &ps);
        RECT r;
        GetClientRect(s, &r);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        backdrop_blit(s, mem, &r);
        BOOL off = !IsWindowEnabled(s), focus = GetFocus() == s;
        int x0, x1, t, h = px(6), cy = r.bottom / 2, dd = px(18);
        slider_rects(s, d, &x0, &x1, &t);
        round_rect(mem, x0 - h / 2, cy - h / 2, x1 - x0 + h, h, h / 2, RGB(0xD6, 0xEE, 0xFB), WHITE, 0, 0,
                   off ? 110 : 230);
        if (!off) round_rect(mem, x0 - h / 2, cy - h / 2, t - x0 + h, h, h / 2, RGB(0xFF, 0x74, 0xB8), PINK, 0, 0, 255);
        round_rect(mem, t - dd / 2, cy - dd / 2, dd, dd, dd / 2, WHITE, RGB(0xE0, 0xF4, 0xFF),
                   off ? RGB(0xA8, 0xC4, 0xE0) : focus ? YELLOW : PINK, px(2), off ? 160 : 255);
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(s, &ps);
        return 0;
    }
    }
    return DefWindowProcW(s, msg, wp, lp);
}

// What the main window does to look like this; TRUE when the message is handled (*res: its result)
static BOOL theme_msg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT *res) {
    Backdrop *bd = (Backdrop *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
    case WM_PRINTCLIENT:  // (themed combo boxes ask for what's behind their rounded corners)
        if (bd && bd->dc) BitBlt((HDC)wp, 0, 0, bd->w, bd->h, bd->dc, 0, 0, SRCCOPY);
        *res = msg == WM_ERASEBKGND;
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r = ps.rcPaint;
        if (bd && bd->dc) BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, bd->dc, r.left, r.top, SRCCOPY);
        EndPaint(hwnd, &ps);
        *res = 0;
        return TRUE;
    }
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lp)->CtlType != ODT_STATIC) return FALSE;
        draw_label((DRAWITEMSTRUCT *)lp);
        *res = TRUE;
        return TRUE;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        LONG_PTR role = GetWindowLongPtrW(n->hwndFrom, GWLP_USERDATA);
        if (n->code != NM_CUSTOMDRAW || !(role & ROLE_BUTTON)) return FALSE;
        *res = draw_button((NMCUSTOMDRAW *)lp);
        return TRUE;
    }
    }
    return FALSE;
}

// The controls of each tab (shown with it); g_build_tab: the tab the controls being made belong to (-1: every tab)
static HWND g_tab_ctl[NTABS][128], g_tabs[NTABS];
static int g_ntab_ctl[NTABS], g_build_tab = -1;

static HWND add(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id,
                HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    if (g_build_tab >= 0 && g_ntab_ctl[g_build_tab] < 128) g_tab_ctl[g_build_tab][g_ntab_ctl[g_build_tab]++] = c;
    return c;
}

static HWND role(HWND c, int r) {
    SetWindowLongPtrW(c, GWLP_USERDATA, r);
    return c;
}

static HWND add_label(HWND parent, const wchar_t *text, int x, int y, int w, int h, int r, int id, HFONT font) {
    return role(add(parent, L"STATIC", text, SS_OWNERDRAW, x, y, w, h, id, font), r);
}

static HWND add_combo(HWND parent, const wchar_t *label, int x, int y, int id, int w) {  // (its label at x)
    add_label(parent, label, x, y, 132, 24, ROLE_LABEL, -1, g_font);
    return add(parent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 136, y, w, 300, id, g_font);
}

static HWND add_button(HWND parent, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, int r) {
    return role(add(parent, L"BUTTON", text, style | WS_TABSTOP, x, y, w, h, id, r == ROLE_PRIMARY ? g_semi : g_font), r);
}

static HWND add_slider(HWND parent, int x, int y, int w, int id, int lo, int hi, int page) {
    HWND s = add(parent, L"DOAX3Slider", L"", WS_TABSTOP, x, y, w, 28, id, g_font);
    SendMessageW(s, TBM_SETRANGE, FALSE, MAKELPARAM(lo, hi));
    SendMessageW(s, TBM_SETPAGESIZE, 0, page);
    SendMessageW(s, TBM_SETLINESIZE, 0, 1);
    return s;
}

static void show_tab(int t) {
    g_tab = t;
    HWND f = GetFocus();
    BOOL lost = FALSE;
    for (int k = 0; k < NTABS; k++)
        for (int i = 0; i < g_ntab_ctl[k]; i++) {
            HWND c = g_tab_ctl[k][i];
            if (k != t && (c == f || IsChild(c, f))) lost = TRUE;
            ShowWindow(c, k == t ? SW_SHOW : SW_HIDE);
        }
    for (int k = 0; k < NTABS; k++) InvalidateRect(g_tabs[k], NULL, FALSE);
    if (lost) SetFocus(g_tabs[t]);
}

// ---- Controls tab: a combo box per game button, and the controller drawn with what's on each of its buttons
static HWND g_pcombo[NSLOTS], g_pstatus, g_pic, g_padhint;
static int g_pad_hover = -1, g_focus_slot = -1;

// A list shows every controller button once it has been focused or opened, and before that only the one it's on:
// filling 16 lists of 28 at once can take a second or more on a PC with accessibility tools listening.
static void pcombo_show(int i, BOOL full) {
    HWND c = g_pcombo[i];
    int n = (int)SendMessageW(c, CB_GETCOUNT, 0, 0);
    if (full || n == NHOSTS) {
        if (n != NHOSTS) {
            SendMessageW(c, CB_RESETCONTENT, 0, 0);
            for (int h = 0; h < NHOSTS; h++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)k_hosts[h].name);
        }
        SendMessageW(c, CB_SETCURSEL, g_bind[i], 0);
    } else {
        SendMessageW(c, CB_RESETCONTENT, 0, 0);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)k_hosts[g_bind[i]].name);
        SendMessageW(c, CB_SETCURSEL, 0, 0);
    }
}

static void pad_hint(void) {  // (under the picture: the controller button the pointer is on)
    wchar_t t[256];
    if (g_pad_hover < 0) {
        wcscpy(t, L"Click a button on the controller to change it.");
    } else {
        int n = swprintf(t, 256, L"%ls: ", k_hosts[g_pad_hover].name), any = 0;
        for (int s = 0; s < NSLOTS && n > 0; s++)
            if (g_bind[s] == g_pad_hover)
                n += swprintf(t + n, 256 - n, L"%ls%ls", any++ ? L", " : L"the game's ", k_slot_names[s]);
        if (!any && n > 0) swprintf(t + n, 256 - n, L"no game button");
    }
    SetWindowTextW(g_padhint, t);
    InvalidateRect(g_padhint, NULL, FALSE);
}

static void controls_refresh(int moved, int slot) {
    for (int i = 0; i < NSLOTS; i++) pcombo_show(i, FALSE);
    wchar_t t[512];
    int n = swprintf(t, 512, L"Layout: %ls.", k_pad_names[pad_layout(g_bind)]);
    if (moved >= 0 && n > 0)
        n += swprintf(t + n, 512 - n, L" %ls was on that button: it moved to %ls's old one.", k_slot_names[moved],
                      k_slot_names[slot]);
    for (int s = 0; s < NSLOTS && n > 0; s++)  // (the ones not in the picture)
        if (g_bind[s] >= NDRAWN) n += swprintf(t + n, 512 - n, L" %ls is on %ls.", k_slot_names[s], k_hosts[g_bind[s]].name);
    SetWindowTextW(g_pstatus, t);
    InvalidateRect(g_pstatus, NULL, FALSE);
    InvalidateRect(g_pic, NULL, FALSE);
    pad_hint();
}

// The controller (96-dpi units in a PAD_W x PAD_H picture), each of k_hosts' first 16 buttons: x, y, w, h, corner
typedef struct { float x, y, w, h, r; } PadShape;
static const PadShape k_pad[NDRAWN] = {
    {167, 87, 22, 22, 11}, {187, 67, 22, 22, 11}, {147, 67, 22, 22, 11}, {167, 47, 22, 22, 11},  // face: S E W N
    {24, 20, 66, 20, 9},   {150, 20, 66, 20, 9},                                                 // LB RB
    {32, 0, 52, 26, 9},    {156, 0, 52, 26, 9},                                                  // LT RT
    {122, 56, 22, 13, 6},  {96, 56, 22, 13, 6},                                                  // Start Back
    {48, 64, 28, 28, 14},  {132, 100, 28, 28, 14},                                               // sticks
    {69, 106, 17, 16, 4},  {86, 89, 16, 17, 4},    {102, 106, 17, 16, 4}, {86, 122, 16, 17, 4},  // d-pad L U R D
};
static HBITMAP g_pad_body;

static float sd_rrect(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - (hw - r), qy = fabsf(y - cy) - (hh - r), ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

static float pad_sdf(float x, float y) {  // the body: a rounded bar and two grips, blended smoothly
    float a = sd_rrect(x, y, 120, 82, 106, 46, 36), k = 16;
    float b = fminf(hypotf(x - 56, y - 134) - 40, hypotf(x - 184, y - 134) - 40);
    float h = clamp01(.5f + .5f * (b - a) / k);
    return b + (a - b) * h - k * h * (1 - h);
}

static void pad_body_make(void) {  // (white to pale blue, a blue edge, a soft shadow; premultiplied)
    int w = pxf(PAD_W), h = pxf(PAD_H);
    DWORD *p;
    if (!(g_pad_body = make_dib(w, h, &p))) return;
    float s = 96.f / g_dpi;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            float x = (i + .5f) * s, y = (j + .5f) * s, d = pad_sdf(x, y), ds = pad_sdf(x - 1, y - 3);
            float ab = clamp01(.5f - d / s), as = clamp01(.5f - ds / 3) * .35f * (1 - ab);
            COLORREF c = mix(mix(WHITE, RGB(0xC4, 0xE6, 0xF8), y / 174), RGB(0x4E, 0x9C, 0xD6), (d + 1.8f) / .8f);
            float r = GetRValue(c) * ab + GetRValue(SHADOW) * as, g = GetGValue(c) * ab + GetGValue(SHADOW) * as,
                  b = GetBValue(c) * ab + GetBValue(SHADOW) * as;
            p[j * w + i] = (DWORD)((ab + as) * 255 + .5f) << 24 | (DWORD)(r + .5f) << 16 | (DWORD)(g + .5f) << 8 |
                           (DWORD)(b + .5f);
        }
}

static void pad_button(HDC dc, int host, BOOL label) {
    const PadShape *s = &k_pad[host];
    int x = pxf(s->x), y = pxf(s->y), w = pxf(s->x + s->w) - x, h = pxf(s->y + s->h) - y;
    int slot = -1, n = 0;
    for (int i = 0; i < NSLOTS; i++)
        if (g_bind[i] == host) slot = slot < 0 ? i : slot, n++;
    BOOL hl = host == g_pad_hover || (g_focus_slot >= 0 && g_bind[g_focus_slot] == host);
    COLORREF top = RGB(0x2A, 0x6C, 0xCC), bottom = RGB(0x0C, 0x34, 0x88), border = RGB(0x08, 0x26, 0x66);
    if (!n) top = RGB(0x9C, 0xB6, 0xD4), bottom = RGB(0x7C, 0x98, 0xBC);  // (no game button on it)
    if (hl) top = RGB(0xFF, 0x74, 0xB8), bottom = RGB(0xE8, 0x1E, 0x7C), border = WHITE;
    round_rect(dc, x, y, w, h, pxf(s->r), top, bottom, border, px(1), 255);
    if (!label || slot < 0) return;
    RECT r = {x, y, x + w, y + h};
    if (host == 6 || host == 7) r.bottom = pxf(20);       // (the triggers: the part above the shoulders)
    else if (host == 4 || host == 5) r.bottom = pxf(37);  // (the shoulders: the part above the body)
    wchar_t t[16];
    swprintf(t, 16, L"%ls%ls", k_slot_glyph[slot], n > 1 ? L"+" : L"");
    SelectObject(dc, slot >= 12 || slot == 10 || slot == 11 ? g_sym : g_semi);
    text(dc, t, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, WHITE, NO_SHADOW);
}

static void pad_draw(HDC dc) {
    for (int h = 6; h <= 7; h++) pad_button(dc, h, TRUE);  // (the triggers, then the shoulders, behind the body)
    for (int h = 4; h <= 5; h++) pad_button(dc, h, TRUE);
    if (!g_pad_body) pad_body_make();
    if (g_pad_body) blend(dc, g_pad_body, 0, 0, pxf(PAD_W), pxf(PAD_H));
    const COLORREF well0 = RGB(0xB0, 0xD8, 0xF0), well1 = RGB(0xCC, 0xE8, 0xF8);
    round_rect(dc, pxf(43), pxf(59), pxf(38), pxf(38), pxf(19), well0, well1, 0, 0, 255);   // (the sticks' wells)
    round_rect(dc, pxf(127), pxf(95), pxf(38), pxf(38), pxf(19), well0, well1, 0, 0, 255);
    round_rect(dc, pxf(67), pxf(87), pxf(54), pxf(54), pxf(27), well0, well1, 0, 0, 255);   // (the d-pad's)
    round_rect(dc, pxf(86), pxf(106), pxf(102) - pxf(86), pxf(122) - pxf(106), 0, RGB(0x2A, 0x6C, 0xCC),
               RGB(0x0C, 0x34, 0x88), 0, 0, 255);  // (its middle)
    for (int h = 0; h < NDRAWN; h++)
        if (h < 4 || h > 7) pad_button(dc, h, TRUE);
}

static int pad_hit(int x, int y) {  // (client pixels) the controller button there, -1: none
    float fx = x * 96.f / g_dpi, fy = y * 96.f / g_dpi;
    static const int order[NDRAWN] = {0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15, 4, 5, 6, 7};  // (the top one first)
    for (int k = 0; k < NDRAWN; k++) {
        const PadShape *s = &k_pad[order[k]];
        if (fx >= s->x && fx < s->x + s->w && fy >= s->y && fy < s->y + s->h) return order[k];
    }
    return -1;
}

static LRESULT CALLBACK padproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
        HGDIOBJ old = SelectObject(mem, bmp), oldf = SelectObject(mem, g_semi);
        backdrop_blit(hwnd, mem, &r);
        pad_draw(mem);
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldf);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int h = pad_hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (h != g_pad_hover) {
            g_pad_hover = h;
            InvalidateRect(hwnd, NULL, FALSE);
            pad_hint();
        }
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_pad_hover = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        pad_hint();
        return 0;
    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        SetCursor(LoadCursorW(NULL, pad_hit(p.x, p.y) >= 0 ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {  // the game button on it: its list, open
        int h = pad_hit((short)LOWORD(lp), (short)HIWORD(lp));
        for (int s = 0; s < NSLOTS && h >= 0; s++)
            if (g_bind[s] == h) {
                SetFocus(g_pcombo[s]);
                SendMessageW(g_pcombo[s], CB_SHOWDROPDOWN, TRUE, 0);
                break;
            }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void build_controls(HWND hwnd) {
    add_label(hwnd,
              L"Each of the game's buttons and the button on your controller it's on. Keyboard keys are set in the "
              L"game's menu (F1).",
              20, 182, 820, 20, ROLE_NOTE, -1, g_font);
    for (int k = 0; k < 8; k++) {
        int y = 212 + k * 31, l = k_col_left[k], r = k_col_right[k];
        add_label(hwnd, k_slot_names[l], 20, y, 92, 24, ROLE_LABEL, -1, g_font);
        g_pcombo[l] = add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 114, y, 190, 380,
                          ID_BIND0 + l, g_font);
        g_pcombo[r] = add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 556, y, 190, 380,
                          ID_BIND0 + r, g_font);
        add_label(hwnd, k_slot_names[r], 752, y, 92, 24, ROLE_LABEL, -1, g_font);
    }
    g_pic = add(hwnd, L"DOAX3Pad", L"", 0, 310, 214, PAD_W, PAD_H, ID_PADPIC, g_font);
    g_padhint = add_label(hwnd, L"", 310, 400, PAD_W, 54, ROLE_CENTER, ID_PADHINT, g_font);
    add_label(hwnd, L"Templates", 20, 472, 84, 28, ROLE_LABEL, -1, g_font);
    add_button(hwnd, k_pad_names[PAD_SWITCH], BS_PUSHBUTTON, 104, 472, 180, 28, ID_PRESET_SWITCH, ROLE_BUTTON);
    add_button(hwnd, k_pad_names[PAD_OTHER], BS_PUSHBUTTON, 292, 472, 180, 28, ID_PRESET_OTHER, ROLE_BUTTON);
    g_pstatus = add_label(hwnd, L"", 490, 466, 350, 54, ROLE_NOTE, ID_PSTATUS, g_font);
}

// ---- Mods tab: the mods, on or off; and each girl's body settings (the launcher's body.ini, over the mods')
static HWND g_mod_cb, g_mod_on, g_mod_note, g_girl_cb, g_slider[NVAL], g_val[NVAL], g_size_like, g_phys, g_amount,
    g_amount_val, g_body_note;
static int g_phys_map[NGIRLS];  // (the Bust physics list's items after "Her own": k_girls indexes)

static Mod *cur_mod(void) { return g_cur_mod >= 0 && g_cur_mod < g_nmod ? &g_mods[g_cur_mod] : NULL; }

static void val_text(int k, float x, wchar_t *t, size_t n) {  // ("0.80", "150 %")
    if (k_vals[k].pct) swprintf(t, n, L"%d %%", (int)(x * 100 + .5f));
    else swprintf(t, n, L"%.2f", x);
}

static void body_text(const Body *b, wchar_t *t, size_t n) {  // ("Bust size 0.80, Arms 1.10, moves 70 % like ...")
    int k = 0;
    for (int i = 0; i < NVAL && k >= 0 && (size_t)k < n - 40; i++)
        if (fabsf(b->v[i] - 1) > .004f) {
            wchar_t v[16];
            val_text(i, b->v[i], v, 16);
            k += swprintf(t + k, n - k, L"%ls%ls %ls", k ? L", " : L"", k_vals[i].label, v);
        }
    if (b->phys >= 0 && k >= 0)
        k += swprintf(t + k, n - k, L"%lsbreasts move %d %% like %hs's", k ? L", " : L"", (int)(b->amount * 100 + .5f),
                      k_girls[b->phys].name);
    if (!k) swprintf(t, n, L"as she is");
}

static void girl_label(int g, wchar_t *t, size_t n) {
    Body base = body_of(g, FALSE);
    swprintf(t, n, L"%hs%ls", k_girls[g].name,
             body_any(&g_body[g]) ? L"  (set)" : body_not_default(&base) ? L"  (set by a mod)" : L"");
}

static void values_text(void) {  // the numbers beside the sliders
    Body e = body_of(g_girl, TRUE);
    wchar_t t[32];
    for (int k = 0; k < NVAL; k++) {
        val_text(k, e.v[k], t, 32);
        SetWindowTextW(g_val[k], t);
        InvalidateRect(g_val[k], NULL, FALSE);
    }
    swprintf(t, 32, L"%d %%", (int)(e.amount * 100 + .5f));
    SetWindowTextW(g_amount_val, t);
    InvalidateRect(g_amount_val, NULL, FALSE);
}

static void body_note(void) {  // what she gets, and the mods that set her too
    wchar_t t[1400], b[600];
    Body e = body_of(g_girl, TRUE);
    body_text(&e, b, 600);
    int n = swprintf(t, 1400, L"In the game: %hs %ls.", k_girls[g_girl].name, b);
    for (int i = 0; i < g_nmod && n > 0 && n < 1000; i++) {
        const Mod *m = &g_mods[i];
        if (!m->on || !body_any(&m->body[g_girl])) continue;
        n += swprintf(t + n, 1400 - n, L" The mod %ls sets her too%ls.", m->name,
                      body_any(&g_body[g_girl]) ? L"; the settings here win" : L"");
    }
    if (n > 0)
        swprintf(t + n, 1400 - n, L" Jiggle: how lively the soft parts are (a bigger part swings further on its own). "
                                  L"Arms, thighs, calves, waist: thickness. Bust weight: heavier hangs lower, toward the ground. Bust contact: a bigger bust gives way to the arms and its two sides to each other (0 %: they pass through). Saved "
                                  L"in body.ini beside larecomp.ini by Save "
                                  L"or Play.");
    SetWindowTextW(g_body_note, t);
    InvalidateRect(g_body_note, NULL, FALSE);
}

static void girl_refresh(void) {  // the body controls from what g_girl gets
    wchar_t t[128];
    SendMessageW(g_girl_cb, CB_RESETCONTENT, 0, 0);
    for (int g = 0; g < NGIRLS; g++) {
        girl_label(g, t, 128);
        SendMessageW(g_girl_cb, CB_ADDSTRING, 0, (LPARAM)t);
    }
    SendMessageW(g_girl_cb, CB_SETCURSEL, g_girl, 0);
    Body e = body_of(g_girl, TRUE);
    for (int k = 0; k < NVAL; k++) {
        SendMessageW(g_slider[k], TBM_SETPOS, TRUE, (LPARAM)(e.v[k] * 100 + .5f));
        InvalidateRect(g_slider[k], NULL, FALSE);
    }
    SendMessageW(g_size_like, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_size_like, CB_ADDSTRING, 0, (LPARAM)L"Pick a girl...");
    for (int g = 0; g < NGIRLS; g++) {  // (her size on this girl's body)
        swprintf(t, 128, L"%hs: %.2f", k_girls[g].name, k_girls[g].bust / k_girls[g_girl].bust);
        SendMessageW(g_size_like, CB_ADDSTRING, 0, (LPARAM)t);
    }
    SendMessageW(g_size_like, CB_SETCURSEL, 0, 0);
    SendMessageW(g_phys, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_phys, CB_ADDSTRING, 0, (LPARAM)L"Her own");
    int n = 0, sel = 0;
    for (int g = 0; g < NGIRLS; g++) {
        if (g == g_girl) continue;
        swprintf(t, 128, L"Like %hs's%ls", k_girls[g].name, g == CALMEST ? L" (the calmest)" : L"");
        SendMessageW(g_phys, CB_ADDSTRING, 0, (LPARAM)t);
        if (e.phys == g) sel = n + 1;
        g_phys_map[n++] = g;
    }
    SendMessageW(g_phys, CB_SETCURSEL, sel, 0);
    SendMessageW(g_amount, TBM_SETPOS, TRUE, (LPARAM)(e.amount * 100 + .5f));
    EnableWindow(g_amount, e.phys >= 0);
    EnableWindow(g_amount_val, e.phys >= 0);
    InvalidateRect(g_amount, NULL, FALSE);
    values_text();
    body_note();
}

static void mod_refresh(void) {
    Mod *m = cur_mod();
    size_t cap = MAX_PATH + 512;
    wchar_t t[MAX_PATH + 512];
    SendMessageW(g_mod_cb, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_nmod; i++) {
        swprintf(t, cap, L"%ls%ls", g_mods[i].name, g_mods[i].on ? L"" : L"  (off)");
        SendMessageW(g_mod_cb, CB_ADDSTRING, 0, (LPARAM)t);
    }
    if (!g_nmod) SendMessageW(g_mod_cb, CB_ADDSTRING, 0, (LPARAM)L"(no mods in the mods folder)");
    SendMessageW(g_mod_cb, CB_SETCURSEL, m ? g_cur_mod : 0, 0);
    EnableWindow(g_mod_cb, m != NULL);
    EnableWindow(g_mod_on, m != NULL);
    SendMessageW(g_mod_on, BM_SETCHECK, m && m->on ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(g_mod_on, NULL, FALSE);
    int n = 0, any = 0;
    if (!m) n = swprintf(t, cap, L"Mods go in %ls\\mods (docs/MODDING.md).", g_root);
    else {
        for (int g = 0; g < NGIRLS && n >= 0; g++)
            if (m->changes[g]) n += swprintf(t + n, cap - n, L"%ls%hs", any++ ? L", " : L"Its files change ", k_girls[g].name);
        if (n >= 0) n += swprintf(t + n, cap - n, any ? L"." : L"It changes no girl's files.");
        for (int g = 0, k = 0; g < NGIRLS && n > 0; g++)
            if (body_any(&m->body[g]))
                n += swprintf(t + n, cap - n, L"%ls%hs", k++ ? L", " : L" Its body.ini sets ", k_girls[g].name);
        if (n > 0 && wcsstr(t, L"body.ini sets")) n += swprintf(t + n, cap - n, L".");
        if (!m->on && n > 0) swprintf(t + n, cap - n, L" Off: the game doesn't load it.");
    }
    SetWindowTextW(g_mod_note, t);
    InvalidateRect(g_mod_note, NULL, FALSE);
    girl_refresh();  // (a mod on or off changes what the girls get)
}

static void body_changed(void) {  // (a body control moved)
    wchar_t t[128];
    girl_label(g_girl, t, 128);
    SendMessageW(g_girl_cb, CB_DELETESTRING, g_girl, 0);
    SendMessageW(g_girl_cb, CB_INSERTSTRING, g_girl, (LPARAM)t);
    SendMessageW(g_girl_cb, CB_SETCURSEL, g_girl, 0);
    Body e = body_of(g_girl, TRUE);
    EnableWindow(g_amount, e.phys >= 0);
    EnableWindow(g_amount_val, e.phys >= 0);
    InvalidateRect(g_amount, NULL, FALSE);
    values_text();
    body_note();
}

static void add_value_row(HWND hwnd, int k, int x, int y) {  // (a label, a slider, its number)
    add_label(hwnd, k_vals[k].label, x, y + 2, 126, 24, ROLE_LABEL, -1, g_font);
    g_slider[k] = add_slider(hwnd, x + 126, y, 194, ID_VAL0 + k, (int)(k_vals[k].lo * 100 + .5f),
                             (int)(k_vals[k].hi * 100 + .5f), 5);
    g_val[k] = add_label(hwnd, L"", x + 326, y + 2, 74, 24, ROLE_VALUE, ID_VALTXT0 + k, g_font);
}

static void build_mods(HWND hwnd) {
    g_mod_cb = add_combo(hwnd, L"Mod", 20, 182, ID_MOD, 264);
    g_mod_on = add_button(hwnd, L"On (the game loads it)", BS_AUTOCHECKBOX, 436, 183, 180, 22, ID_MOD_ON, ROLE_CHECK);
    add_button(hwnd, L"Open folder", BS_PUSHBUTTON, 740, 180, 100, 28, ID_MOD_OPEN, ROLE_BUTTON);
    g_mod_note = add_label(hwnd, L"", 156, 210, 684, 34, ROLE_NOTE, ID_MOD_NOTE, g_font);
    add_label(hwnd, L"Body settings", 20, 252, 130, 24, ROLE_HEAD, -1, g_semi);
    g_girl_cb = add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 156, 252, 264, 300, ID_GIRL,
                    g_font);
    add_button(hwnd, L"As she is", BS_PUSHBUTTON, 436, 250, 100, 28, ID_GIRL_RESET, ROLE_BUTTON);
    add_label(hwnd, L"each girl's, whatever mods are on", 548, 254, 292, 22, ROLE_NOTE, -1, g_font);
    // (left: the bust; right: the rest)
    add_value_row(hwnd, K_BUST, 20, 290);
    add_value_row(hwnd, K_BUST_JIG, 20, 322);
    g_size_like = add_combo(hwnd, L"Size like", 20, 356, ID_SIZE_LIKE, 264);
    add_label(hwnd, L"Bust physics", 20, 390, 132, 24, ROLE_LABEL, -1, g_font);
    g_phys = add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 156, 390, 264, 300, ID_PHYS, g_font);
    add_label(hwnd, L"How much like hers", 20, 424, 126, 24, ROLE_LABEL, -1, g_font);
    g_amount = add_slider(hwnd, 146, 422, 194, ID_AMOUNT, 0, 100, 10);
    g_amount_val = add_label(hwnd, L"", 346, 424, 74, 24, ROLE_VALUE, ID_AMOUNT_VAL, g_font);
    for (int k = K_BUTT; k < NVAL; k++) add_value_row(hwnd, k, 440, 290 + (k - K_BUTT) * 28);
    g_body_note = add_label(hwnd, L"", 20, 456, 400, 88, ROLE_NOTE, ID_BODY_NOTE, g_font);
    // (first: a mod that's on, and the first girl one changes)
    g_cur_mod = g_nmod ? 0 : -1;
    for (int i = g_nmod - 1; i >= 0; i--)
        if (g_mods[i].on) g_cur_mod = i;
    for (int i = 0, found = 0; i < g_nmod && !found; i++)
        for (int g = 0; g < NGIRLS && !found; g++)
            if (g_mods[i].on && g_mods[i].changes[g]) g_girl = g, found = 1;
    mod_refresh();
}

static void build_game(HWND hwnd) {
    int y = 208, step = 34, cw = 264;
    add_label(hwnd, L"Display", 20, 180, 300, 22, ROLE_HEAD, -1, g_semi);
    add_label(hwnd, L"Frame rate and graphics", 440, 180, 300, 22, ROLE_HEAD, -1, g_semi);
    g_mode = add_combo(hwnd, L"Display mode", 20, y, ID_MODE, cw);
    for (int i = 0; i < 3; i++) SendMessageW(g_mode, CB_ADDSTRING, 0, (LPARAM)k_modes[i]);
    g_res = add_combo(hwnd, L"Resolution", 20, y + step, ID_RES, cw);
    for (int i = 0; i < g_nsizes; i++) {
        wchar_t t[48];
        swprintf(t, 48, L"%d x %d%ls", g_sizes[i].w, g_sizes[i].h,
                 g_sizes[i].w == g_desk_w && g_sizes[i].h == g_desk_h ? L" (desktop)" : L"");
        SendMessageW(g_res, CB_ADDSTRING, 0, (LPARAM)t);
    }
    g_r3d = add_combo(hwnd, L"3D resolution", 20, y + 2 * step, ID_R3D, cw);
    for (int i = 0; i < 5; i++) SendMessageW(g_r3d, CB_ADDSTRING, 0, (LPARAM)k_r3d_names[i]);
    g_ups = add_combo(hwnd, L"Upscaler", 20, y + 3 * step, ID_UPS, cw);
    for (int i = 0; i < 3; i++) SendMessageW(g_ups, CB_ADDSTRING, 0, (LPARAM)k_ups_names[i]);
    g_note = add_label(hwnd, L"", 156, y + 4 * step, cw, 40, ROLE_NOTE, ID_NOTE, g_font);
    g_fps = add_combo(hwnd, L"Frame rate", 440, y, ID_FPS, cw);
    for (int i = 0; i < 3; i++) SendMessageW(g_fps, CB_ADDSTRING, 0, (LPARAM)k_fps_names[i]);
    g_fpshint = add_label(hwnd, L"", 576, y + 27, cw, 50, ROLE_FPS, ID_FPSHINT, g_font);
    COMBOBOXINFO cbi = {.cbSize = sizeof cbi};
    if (GetComboBoxInfo(g_fps, &cbi) && cbi.hwndList)
        g_fps_list_proc = (WNDPROC)SetWindowLongPtrW(cbi.hwndList, GWLP_WNDPROC, (LONG_PTR)fps_list_proc);
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL,
                               GetModuleHandleW(NULL), NULL);
    if (tip) {  // (the whole story, when the pointer rests on the Frame rate box)
        TTTOOLINFOW ti = {.cbSize = TTTOOLINFOW_V2_SIZE, .uFlags = TTF_IDISHWND | TTF_SUBCLASS, .hwnd = hwnd,
                          .uId = (UINT_PTR)g_fps, .lpszText = (LPWSTR)k_fps_tip};
        SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, px(360));
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    }
    g_aa = add_combo(hwnd, L"Anti-aliasing", 440, y + 78, ID_AA, cw);
    for (int i = 0; i < 2; i++) SendMessageW(g_aa, CB_ADDSTRING, 0, (LPARAM)k_aa_names[i]);
    g_af = add_combo(hwnd, L"Texture filtering", 440, y + 78 + step, ID_AF, cw);
    for (int i = 0; i < 5; i++) SendMessageW(g_af, CB_ADDSTRING, 0, (LPARAM)k_af_names[i]);
    g_showfps = add_button(hwnd, L"Show FPS (the frame rate in a corner)", BS_AUTOCHECKBOX, 576, y + 78 + 2 * step + 4,
                           cw, 22, ID_SHOWFPS, ROLE_CHECK);
    g_bgin = add_button(hwnd, L"Controller works in the background", BS_AUTOCHECKBOX, 576, y + 78 + 2 * step + 30, cw,
                        22, ID_BGIN, ROLE_CHECK);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT res;
    if (theme_msg(hwnd, msg, wp, lp, &res)) return res;
    switch (msg) {
    case WM_CREATE: {
        backdrop_make(&g_main_bd, hwnd, px(MAIN_HEADER));
        paint_main_header(&g_main_bd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&g_main_bd);
        for (int t = 0; t < NTABS; t++)
            g_tabs[t] = role(add(hwnd, L"BUTTON", k_tab_names[t], BS_PUSHBUTTON | WS_TABSTOP, 20 + t * 112, TAB_Y, 104,
                                 30, ID_TAB0 + t, g_semi),
                             ROLE_TAB);
        g_build_tab = TAB_GAME;
        build_game(hwnd);
        g_build_tab = TAB_CONTROLS;
        build_controls(hwnd);
        g_build_tab = TAB_MODS;
        build_mods(hwnd);
        g_build_tab = -1;
        add_label(hwnd, L"Save keeps your choices; Play saves them too and starts the game.", 20, MAIN_H - 44, 540, 20,
                  ROLE_NOTE, ID_SAVENOTE, g_font);
        add_button(hwnd, L"Save", BS_PUSHBUTTON, MAIN_W - 20 - 88 - 8 - 88 - 8 - 88, MAIN_H - 50, 88, 32, ID_SAVE,
                   ROLE_BUTTON);
        add_button(hwnd, L"Play", BS_DEFPUSHBUTTON, MAIN_W - 20 - 88 - 8 - 88, MAIN_H - 50, 88, 32, IDOK, ROLE_PRIMARY);
        add_button(hwnd, L"Quit", BS_PUSHBUTTON, MAIN_W - 20 - 88, MAIN_H - 50, 88, 32, IDCANCEL, ROLE_BUTTON);
        load_choices();
        controls_refresh(-1, 0);
        set_fps_hint((int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0));
        show_tab(TAB_GAME);
        SetFocus(GetDlgItem(hwnd, IDOK));
        return 0;
    }
    case WM_HSCROLL: {  // (the Mods tab's sliders)
        HWND s = (HWND)lp;
        int key = s == g_amount ? K_AMOUNT : -1;
        for (int k = 0; k < NVAL; k++)
            if (s == g_slider[k]) key = k;
        if (key < 0) return 0;
        float x = (int)SendMessageW(s, TBM_GETPOS, 0, 0) / 100.f;
        Body e = body_of(g_girl, TRUE);
        if (fabsf((key == K_AMOUNT ? e.amount : e.v[key]) - x) > .001f) {
            body_put(g_girl, key, x, 0);
            body_changed();
        }
        InvalidateRect(s, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= ID_TAB0 && id < ID_TAB0 + NTABS) show_tab(id - ID_TAB0);
        else if (id == ID_MODE && code == CBN_SELCHANGE) update_note();
        else if (id == ID_FPS && (code == CBN_SELCHANGE || code == CBN_CLOSEUP))
            set_fps_hint((int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0));
        else if (id >= ID_BIND0 && id < ID_BIND0 + NSLOTS) {
            int slot = id - ID_BIND0, h = (int)SendMessageW(g_pcombo[slot], CB_GETCURSEL, 0, 0);
            if (code == CBN_SETFOCUS || code == CBN_DROPDOWN) {
                pcombo_show(slot, TRUE);
                if (g_focus_slot != slot) g_focus_slot = slot, InvalidateRect(g_pic, NULL, FALSE);
            } else if (code == CBN_KILLFOCUS) {
                if (g_focus_slot == slot) g_focus_slot = -1, InvalidateRect(g_pic, NULL, FALSE);
            } else if (code == CBN_SELCHANGE && h >= 0 && h < NHOSTS &&
                       SendMessageW(g_pcombo[slot], CB_GETCOUNT, 0, 0) == NHOSTS)
                controls_refresh(bind_assign(g_bind, slot, h), slot);
        } else if (id == ID_PRESET_SWITCH || id == ID_PRESET_OTHER) {
            memcpy(g_bind, id == ID_PRESET_SWITCH ? g_def : g_other, sizeof g_bind);
            controls_refresh(-1, 0);
        } else if (id == ID_MOD && code == CBN_SELCHANGE) {
            int i = (int)SendMessageW(g_mod_cb, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < g_nmod && i != g_cur_mod) g_cur_mod = i, mod_refresh();
        } else if (id == ID_MOD_ON && code == BN_CLICKED && cur_mod()) {
            cur_mod()->on = SendMessageW(g_mod_on, BM_GETCHECK, 0, 0) == BST_CHECKED;
            mod_refresh();
        } else if (id == ID_MOD_OPEN) {
            wchar_t p[MAX_PATH];
            Mod *m = cur_mod();
            if (m) mod_folder(m, m->was_on, p);
            else {
                swprintf(p, MAX_PATH, L"%ls\\mods", g_root);
                CreateDirectoryW(p, NULL);
            }
            ShellExecuteW(hwnd, L"open", p, NULL, NULL, SW_SHOWNORMAL);
        } else if (id == ID_GIRL && code == CBN_SELCHANGE) {
            int g = (int)SendMessageW(g_girl_cb, CB_GETCURSEL, 0, 0);
            if (g >= 0 && g < NGIRLS) g_girl = g, girl_refresh();
        } else if (id == ID_SIZE_LIKE && code == CBN_SELCHANGE) {
            int g = (int)SendMessageW(g_size_like, CB_GETCURSEL, 0, 0) - 1;
            if (g >= 0 && g < NGIRLS) {
                float v = roundf(k_girls[g].bust / k_girls[g_girl].bust * 100) / 100;
                body_put(g_girl, K_BUST, v < .3f ? .3f : v > 2.5f ? 2.5f : v, 0);
                SendMessageW(g_slider[K_BUST], TBM_SETPOS, TRUE, (LPARAM)(body_of(g_girl, TRUE).v[K_BUST] * 100 + .5f));
                InvalidateRect(g_slider[K_BUST], NULL, FALSE);
                body_changed();
            }
        } else if (id == ID_PHYS && code == CBN_SELCHANGE) {
            int i = (int)SendMessageW(g_phys, CB_GETCURSEL, 0, 0);
            body_put(g_girl, K_PHYS, 0, i > 0 && i <= NGIRLS - 1 ? g_phys_map[i - 1] : -1);
            body_changed();
        } else if (id == ID_GIRL_RESET) {  // (as the game has her: over a mod's settings too)
            for (int k = 0; k < NKEYS; k++) body_put(g_girl, k, 1, -1);
            girl_refresh();
        } else if (id == IDOK || id == ID_SAVE) {  // (Save: everything, without starting the game)
            wchar_t err[512];
            if (!save_choices()) {
                swprintf(err, 512, L"Couldn't write %ls.", g_ini);
                MessageBoxW(hwnd, err, g_title, MB_ICONERROR);
            } else if (!mods_save(err, 512)) {
                MessageBoxW(hwnd, err, g_title, MB_ICONERROR);
                mod_refresh();
            } else if (id == ID_SAVE) {
                SYSTEMTIME t;
                GetLocalTime(&t);
                swprintf(err, 512, L"Saved at %02d:%02d:%02d. Play starts the game with these.", t.wHour, t.wMinute,
                         t.wSecond);
                SetWindowTextW(GetDlgItem(hwnd, ID_SAVENOTE), err);
                InvalidateRect(GetDlgItem(hwnd, ID_SAVENOTE), NULL, FALSE);
                mod_refresh();  // (a mod turned on or off is renamed now)
            } else if (!launch(err, 512, NULL)) MessageBoxW(hwnd, err, g_title, MB_ICONERROR);
            else DestroyWindow(hwnd);
        } else if (id == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show) {
    (void)prev, (void)args;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (!wcscmp(argv[i], L"--play")) return play();
        if (!wcscmp(argv[i], L"--prepare")) return prepare(argc, argv);
    }
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);  // (WIC, for the art)
    wchar_t err[512];
    if (!find_paths(err, 512)) {
        MessageBoxW(NULL, err, L"DOAX3 Recomp launcher", MB_ICONERROR);
        return 1;
    }
    load_title();
    load_sizes();
    ini_load();
    init_binds();
    mods_load();
    g_dpi = (int)GetDpiForSystem();
    wchar_t dpi[16];  // (DOAX3_LAUNCHER_DPI=144: laid out as at 150% scaling, to check the layout without changing it)
    if (GetEnvironmentVariableW(L"DOAX3_LAUNCHER_DPI", dpi, 16) && _wtoi(dpi) >= 96 && _wtoi(dpi) <= 480)
        g_dpi = _wtoi(dpi);
    g_font = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                         0, L"Segoe UI");
    g_semi = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                         0, L"Segoe UI");
    g_bold = CreateFontW(-MulDiv(12, g_dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_big = CreateFontW(-MulDiv(17, g_dpi, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                        L"Segoe UI");
    g_sym = CreateFontW(-MulDiv(10, g_dpi, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                        L"Segoe UI Symbol");
    WNDCLASSW wc = {.lpfnWndProc = wndproc, .hInstance = inst, .lpszClassName = L"DOAX3Launcher",
                    .hCursor = LoadCursorW(NULL, IDC_ARROW), .hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1))};
    RegisterClassW(&wc);
    wc.lpfnWndProc = padproc, wc.lpszClassName = L"DOAX3Pad", wc.hIcon = NULL;
    RegisterClassW(&wc);
    wc.lpfnWndProc = sliderproc, wc.lpszClassName = L"DOAX3Slider", wc.cbWndExtra = sizeof(void *);
    RegisterClassW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = {0, 0, px(MAIN_W), px(MAIN_H)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, GetDpiForSystem());
    int w = r.right - r.left, h = r.bottom - r.top;
    wchar_t caption[0x220];
    swprintf(caption, 0x220, L"%ls - Launcher", g_title);
    HWND hwnd = CreateWindowExW(0, L"DOAX3Launcher", caption, style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                                (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {  // (Ctrl+Tab: next tab)
            show_tab((g_tab + (GetKeyState(VK_SHIFT) < 0 ? NTABS - 1 : 1)) % NTABS);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg)) TranslateMessage(&msg), DispatchMessageW(&msg);
    }
    backdrop_free(&g_main_bd);
    return 0;
}
