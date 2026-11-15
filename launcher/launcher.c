// DOAX3 Recomp launcher (Windows): pick the display mode (windowed, borderless fullscreen, fullscreen), the
// resolution, the 3D resolution and 60 fps, then start build/doax3recomp.exe as tools/run.sh does. The choices go into
// larecomp.ini (fullscreen, fullscreen_mode, window_size, render3d, frame_rate, antialias, anisotropy, upscaler,
// show_fps, background_input: runtime/settings.c), so the game's own menu and
// F11 keep working with them; every other line of the file is kept as it is. The controls choice and the Controller
// buttons window write the controller's bind_pad_<button> lines (runtime/hle/bindings.c), only for buttons that differ
// from the game's default (the game reads a missing line as the default); bind_key_* lines stay as they are.
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

enum { ID_MODE = 100, ID_RES, ID_R3D, ID_NOTE, ID_FPS, ID_PAD, ID_AA, ID_AF, ID_UPS, ID_SHOWFPS, ID_BGIN, ID_PADEDIT,
       ID_FPSHINT };
enum { ID_BIND0 = 300, ID_PRESET_SWITCH = 400, ID_PRESET_OTHER, ID_PSTATUS };  // (the Controller buttons window)
enum { MODE_WINDOWED, MODE_BORDERLESS, MODE_FULLSCREEN };

static wchar_t g_exe_dir[MAX_PATH], g_root[MAX_PATH], g_game[MAX_PATH], g_ini[MAX_PATH], g_title[0x201];
static HWND g_mode, g_res, g_r3d, g_note, g_fps, g_fpshint, g_pad, g_aa, g_af, g_ups, g_showfps, g_bgin;
static HFONT g_font, g_bold, g_semi, g_big, g_sym;
static int g_dpi = 96;
static struct { int w, h; } g_sizes[128];
static int g_nsizes, g_desk_w, g_desk_h;

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
// (bindings.c k_pad_ini, here in the order the lists show them). Switch controls: the game's default, A on the right
// face button as on a Switch; Other: A and B swapped, X and Y swapped (A at the bottom, as on an Xbox pad).
enum { PAD_SWITCH, PAD_OTHER, PAD_CUSTOM };
#define NSLOTS 16
static const char *const k_slot_keys[NSLOTS] = {"a",    "b",    "x",   "y",  "lstick", "rstick", "l",     "r",
                                                "zl",   "zr",   "plus", "minus", "left", "up",   "right", "down"};
static const wchar_t *const k_slot_names[NSLOTS] = {
    L"A", L"B", L"X", L"Y", L"Left stick press", L"Right stick press", L"L", L"R", L"ZL", L"ZR", L"Plus (+)",
    L"Minus (-)", L"D-pad Left", L"D-pad Up", L"D-pad Right", L"D-pad Down"};
static const char *const k_slot_default[NSLOTS] = {
    "east", "south", "north", "west", "lstick", "rstick", "lshoulder", "rshoulder", "ltrigger", "rtrigger", "start",
    "back", "dpad_left", "dpad_up", "dpad_right", "dpad_down"};
static const char *const k_face_other[4] = {"south", "east", "west", "north"};  // (A, B, X, Y in Other)
static const int k_panel_order[NSLOTS] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 4, 5, 12, 13, 14, 15};  // (two columns)
static const struct { const char *key; const wchar_t *name; } k_hosts[] = {
    {"south", L"Bottom face (Xbox A / PS Cross / Switch B)"},
    {"east", L"Right face (Xbox B / PS Circle / Switch A)"},
    {"west", L"Left face (Xbox X / PS Square / Switch Y)"},
    {"north", L"Top face (Xbox Y / PS Triangle / Switch X)"},
    {"lshoulder", L"LB / L1 / L"},
    {"rshoulder", L"RB / R1 / R"},
    {"ltrigger", L"LT / L2 / ZL"},
    {"rtrigger", L"RT / R2 / ZR"},
    {"start", L"Start / Options / +"},
    {"back", L"Back / Share / -"},
    {"lstick", L"Left stick press (LS / L3)"},
    {"rstick", L"Right stick press (RS / R3)"},
    {"dpad_left", L"D-pad Left"},
    {"dpad_up", L"D-pad Up"},
    {"dpad_right", L"D-pad Right"},
    {"dpad_down", L"D-pad Down"},
    {"rpaddle1", L"Right paddle 1 (Xbox Elite P1)"},
    {"rpaddle2", L"Right paddle 2 (Xbox Elite P2)"},
    {"lpaddle1", L"Left paddle 1 (Xbox Elite P3)"},
    {"lpaddle2", L"Left paddle 2 (Xbox Elite P4)"},
    {"misc1", L"Xbox Share / PS5 Mute / Switch Capture"},
    {"touchpad", L"PS touchpad press"},
    {"guide", L"Guide (Xbox / PS / Home button)"},
    {"misc2", L"Extra button 2"},
    {"misc3", L"Extra button 3"},
    {"misc4", L"Extra button 4"},
    {"misc5", L"Extra button 5"},
    {"misc6", L"Extra button 6"},
};
#define NHOSTS ((int)(sizeof k_hosts / sizeof *k_hosts))
static const wchar_t *const k_pad_names[] = {L"Switch controls", L"Other (A and B, X and Y swapped)", L"Custom"};
// Bindings as k_hosts indexes: the default, Other, the launcher's (saved with Play), the last custom ones (the main
// window's Custom choice brings them back) and the Controller buttons window's own until OK
static int g_def[NSLOTS], g_other[NSLOTS], g_bind[NSLOTS], g_custom[NSLOTS], g_pbind[NSLOTS];

static int px(int v) { return MulDiv(v, g_dpi, 96); }

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
    memcpy(g_custom, g_def, sizeof g_custom);
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

static void sync_pad_combo(void) {  // the main window's Controls choice from g_bind (Custom listed when it's custom)
    int layout = pad_layout(g_bind);
    if (layout == PAD_CUSTOM) {
        memcpy(g_custom, g_bind, sizeof g_custom);
        if (SendMessageW(g_pad, CB_GETCOUNT, 0, 0) < 3)
            SendMessageW(g_pad, CB_ADDSTRING, 0, (LPARAM)k_pad_names[PAD_CUSTOM]);
    }
    SendMessageW(g_pad, CB_SETCURSEL, layout, 0);
}

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
    sync_pad_combo();
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
// painted once per window into a bitmap that the window, its labels and its buttons copy from. The labels (owner-drawn
// statics), buttons and check boxes (custom draw) are drawn here, so their text can be white on the sea; the combo
// boxes are the system's own.
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
// (a control's GWLP_USERDATA; ROLE_FPS: the Frame rate line, a note in WARN while it warns)
enum { ROLE_LABEL, ROLE_NOTE, ROLE_HEAD, ROLE_FPS, ROLE_BUTTON = 0x100, ROLE_PRIMARY, ROLE_CHECK };
enum { MAIN_W = 490, MAIN_H = 592, MAIN_HEADER = 122, PANEL_W = 822, PANEL_H = 458, PANEL_HEADER = 64 };

typedef struct { HDC dc; HBITMAP bmp; HGDIOBJ old; int w, h; } Backdrop;
static Backdrop g_main_bd, g_panel_bd;

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
    wchar_t t[512];
    GetWindowTextW(d->hwndItem, t, 512);
    HGDIOBJ old = SelectObject(d->hDC, (HFONT)SendMessageW(d->hwndItem, WM_GETFONT, 0, 0));
    UINT fmt = DT_NOPREFIX | (role == ROLE_NOTE || role == ROLE_FPS ? DT_WORDBREAK
                                                                     : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    COLORREF c = role == ROLE_LABEL ? WHITE : role == ROLE_FPS && g_fps_hint > 0 ? WARN : YELLOW;
    text(d->hDC, t, d->rcItem, fmt, c, SHADOW);
    SelectObject(d->hDC, old);
}

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
    } else {  // pink (Play, OK) or deep blue, rounded
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

// What both windows do to look like this; TRUE when the message is handled (*res: its result)
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
        if (n->code != NM_CUSTOMDRAW || !(GetWindowLongPtrW(n->hwndFrom, GWLP_USERDATA) & ROLE_BUTTON)) return FALSE;
        *res = draw_button((NMCUSTOMDRAW *)lp);
        return TRUE;
    }
    }
    return FALSE;
}

static HWND add(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id,
                HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static HWND role(HWND c, int r) {
    SetWindowLongPtrW(c, GWLP_USERDATA, r);
    return c;
}

static HWND add_label(HWND parent, const wchar_t *text, int x, int y, int w, int h, int r, int id, HFONT font) {
    return role(add(parent, L"STATIC", text, SS_OWNERDRAW, x, y, w, h, id, font), r);
}

static HWND add_combo(HWND parent, const wchar_t *label, int y, int id, int w) {
    add_label(parent, label, 20, y, 136, 24, ROLE_LABEL, -1, g_font);
    return add(parent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 160, y, w, 300, id, g_font);
}

static HWND add_button(HWND parent, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, int r) {
    return role(add(parent, L"BUTTON", text, style | WS_TABSTOP, x, y, w, h, id, r == ROLE_PRIMARY ? g_semi : g_font), r);
}

// ---- the Controller buttons window: each of the game's buttons and the controller button it's on. Modal: the main
// window waits; OK gives the launcher these bindings (saved with Play), Cancel forgets them.
static HWND g_panel, g_pcombo[NSLOTS], g_pstatus;

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
        SendMessageW(c, CB_SETCURSEL, g_pbind[i], 0);
    } else {
        SendMessageW(c, CB_RESETCONTENT, 0, 0);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)k_hosts[g_pbind[i]].name);
        SendMessageW(c, CB_SETCURSEL, 0, 0);
    }
}

static void panel_refresh(int moved, int slot) {
    for (int i = 0; i < NSLOTS; i++) pcombo_show(i, FALSE);
    wchar_t t[256];
    int n = swprintf(t, 256, L"Layout: %ls", k_pad_names[pad_layout(g_pbind)]);
    if (moved >= 0 && n > 0)
        swprintf(t + n, 256 - n, L"\n%ls was on that button: it moved to %ls's old one.", k_slot_names[moved],
                 k_slot_names[slot]);
    SetWindowTextW(g_pstatus, t);
}

static void panel_close(HWND hwnd) {
    EnableWindow(GetWindow(hwnd, GW_OWNER), TRUE);  // (first, so the main window gets the focus back)
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK panelproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT res;
    if (theme_msg(hwnd, msg, wp, lp, &res)) return res;
    switch (msg) {
    case WM_CREATE: {
        backdrop_make(&g_panel_bd, hwnd, px(PANEL_HEADER));
        if (g_panel_bd.dc) {
            SelectObject(g_panel_bd.dc, g_bold);
            text(g_panel_bd.dc, L"Controller buttons", (RECT){px(20), px(6), px(PANEL_W - 20), px(32)},
                 DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX, DEEP, NO_SHADOW);
            SelectObject(g_panel_bd.dc, g_font);
            text(g_panel_bd.dc,
                 L"Pick the button on your controller for each of the game's buttons. Keyboard keys are set in the "
                 L"game's menu (F1).",
                 (RECT){px(20), px(32), px(PANEL_W - 20), px(54)}, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX, DEEP,
                 NO_SHADOW);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&g_panel_bd);
        for (int c = 0; c < 2; c++) {
            add_label(hwnd, L"Game button", 20 + c * 400, 74, 118, 20, ROLE_HEAD, -1, g_semi);
            add_label(hwnd, L"On your controller", 140 + c * 400, 74, 262, 20, ROLE_HEAD, -1, g_semi);
        }
        for (int k = 0; k < NSLOTS; k++) {
            int i = k_panel_order[k], x = 20 + k / 8 * 400, y = 98 + k % 8 * 32;
            add_label(hwnd, k_slot_names[i], x, y, 118, 24, ROLE_LABEL, -1, g_font);
            g_pcombo[i] = add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 120, y, 262, 380,
                              ID_BIND0 + i, g_font);
        }
        add_label(hwnd, L"Presets", 20, 364, 60, 28, ROLE_LABEL, -1, g_font);
        add_button(hwnd, L"Switch layout (original)", BS_PUSHBUTTON, 82, 364, 180, 28, ID_PRESET_SWITCH, ROLE_BUTTON);
        add_button(hwnd, L"Swapped layout (A\x2194" L"B, X\x2194" L"Y)", BS_PUSHBUTTON, 270, 364, 206, 28,
                   ID_PRESET_OTHER, ROLE_BUTTON);
        g_pstatus = add_label(hwnd, L"", 494, 358, 308, 40, ROLE_NOTE, ID_PSTATUS, g_font);
        add_label(hwnd, L"OK keeps these buttons; the launcher saves them when you press Play.", 20, 416, 560, 20,
                  ROLE_NOTE, -1, g_font);
        add_button(hwnd, L"OK", BS_DEFPUSHBUTTON, 614, 410, 92, 32, IDOK, ROLE_PRIMARY);
        add_button(hwnd, L"Cancel", BS_PUSHBUTTON, 712, 410, 90, 32, IDCANCEL, ROLE_BUTTON);
        panel_refresh(-1, 0);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= ID_BIND0 && id < ID_BIND0 + NSLOTS) {
            int slot = id - ID_BIND0, h = (int)SendMessageW(g_pcombo[slot], CB_GETCURSEL, 0, 0);
            if (HIWORD(wp) == CBN_SETFOCUS || HIWORD(wp) == CBN_DROPDOWN) pcombo_show(slot, TRUE);
            else if (HIWORD(wp) == CBN_SELCHANGE && h >= 0 && h < NHOSTS &&
                     SendMessageW(g_pcombo[slot], CB_GETCOUNT, 0, 0) == NHOSTS)
                panel_refresh(bind_assign(g_pbind, slot, h), slot);
        } else if (id == ID_PRESET_SWITCH || id == ID_PRESET_OTHER) {
            memcpy(g_pbind, id == ID_PRESET_SWITCH ? g_def : g_other, sizeof g_pbind);
            panel_refresh(-1, 0);
        } else if (id == IDOK) {
            memcpy(g_bind, g_pbind, sizeof g_bind);
            sync_pad_combo();
            panel_close(hwnd);
        } else if (id == IDCANCEL) panel_close(hwnd);
        return 0;
    }
    case WM_CLOSE:
        panel_close(hwnd);
        return 0;
    case WM_DESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        backdrop_free(&g_panel_bd);
        g_panel = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void open_panel(HWND owner) {
    if (g_panel) {  // (already open: a command that reached the main window while it waits)
        SetForegroundWindow(g_panel);
        return;
    }
    memcpy(g_pbind, g_bind, sizeof g_pbind);
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, ex = WS_EX_DLGMODALFRAME;
    RECT r = {0, 0, px(PANEL_W), px(PANEL_H)}, o;
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, GetDpiForSystem());
    int w = r.right - r.left, h = r.bottom - r.top;
    GetWindowRect(owner, &o);
    MONITORINFO mi = {.cbSize = sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    int x = (o.left + o.right - w) / 2, y = (o.top + o.bottom - h) / 2;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    wchar_t caption[0x220];
    swprintf(caption, 0x220, L"Controller buttons - %ls", g_title);
    g_panel = CreateWindowExW(ex, L"DOAX3Buttons", caption, style, x, y, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_panel) return;
    EnableWindow(owner, FALSE);
    ShowWindow(g_panel, SW_SHOW);
    SetFocus(g_pcombo[0]);
    MSG msg;
    while (g_panel) {
        int got = GetMessageW(&msg, NULL, 0, 0);
        if (got <= 0) {  // (WM_QUIT: for the main loop)
            if (!got) PostQuitMessage((int)msg.wParam);
            panel_close(g_panel);
            break;
        }
        if (!IsDialogMessageW(g_panel, &msg)) TranslateMessage(&msg), DispatchMessageW(&msg);
    }
    SetFocus(GetDlgItem(owner, ID_PADEDIT));
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT res;
    if (theme_msg(hwnd, msg, wp, lp, &res)) return res;
    switch (msg) {
    case WM_CREATE: {
        backdrop_make(&g_main_bd, hwnd, px(MAIN_HEADER));
        paint_main_header(&g_main_bd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&g_main_bd);
        int y = MAIN_HEADER + 24, step = 34, cw = MAIN_W - 180;
        g_mode = add_combo(hwnd, L"Display mode", y, ID_MODE, cw);
        for (int i = 0; i < 3; i++) SendMessageW(g_mode, CB_ADDSTRING, 0, (LPARAM)k_modes[i]);
        g_res = add_combo(hwnd, L"Resolution", y += step, ID_RES, cw);
        for (int i = 0; i < g_nsizes; i++) {
            wchar_t t[48];
            swprintf(t, 48, L"%d x %d%ls", g_sizes[i].w, g_sizes[i].h,
                     g_sizes[i].w == g_desk_w && g_sizes[i].h == g_desk_h ? L" (desktop)" : L"");
            SendMessageW(g_res, CB_ADDSTRING, 0, (LPARAM)t);
        }
        g_r3d = add_combo(hwnd, L"3D resolution", y += step, ID_R3D, cw);
        for (int i = 0; i < 5; i++) SendMessageW(g_r3d, CB_ADDSTRING, 0, (LPARAM)k_r3d_names[i]);
        g_pad = add_combo(hwnd, L"Controls", y += step, ID_PAD, cw - 94);
        for (int i = 0; i < 2; i++) SendMessageW(g_pad, CB_ADDSTRING, 0, (LPARAM)k_pad_names[i]);
        add_button(hwnd, L"Customize...", BS_PUSHBUTTON, MAIN_W - 20 - 88, y, 88, 24, ID_PADEDIT, ROLE_BUTTON);
        g_fps = add_combo(hwnd, L"Frame rate", y += step, ID_FPS, cw);
        for (int i = 0; i < 3; i++) SendMessageW(g_fps, CB_ADDSTRING, 0, (LPARAM)k_fps_names[i]);
        g_fpshint = add_label(hwnd, L"", 160, y + 27, cw, 34, ROLE_FPS, ID_FPSHINT, g_font);
        y += 36;
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
        g_aa = add_combo(hwnd, L"Anti-aliasing", y += step, ID_AA, cw);
        for (int i = 0; i < 2; i++) SendMessageW(g_aa, CB_ADDSTRING, 0, (LPARAM)k_aa_names[i]);
        g_af = add_combo(hwnd, L"Texture filtering", y += step, ID_AF, cw);
        for (int i = 0; i < 5; i++) SendMessageW(g_af, CB_ADDSTRING, 0, (LPARAM)k_af_names[i]);
        g_ups = add_combo(hwnd, L"Upscaler", y += step, ID_UPS, cw);
        for (int i = 0; i < 3; i++) SendMessageW(g_ups, CB_ADDSTRING, 0, (LPARAM)k_ups_names[i]);
        y += step + 2;
        g_showfps = add_button(hwnd, L"Show FPS (the frame rate in a corner)", BS_AUTOCHECKBOX, 160, y, cw, 22,
                               ID_SHOWFPS, ROLE_CHECK);
        g_bgin = add_button(hwnd, L"Controller works in the background", BS_AUTOCHECKBOX, 160, y + 26, cw, 22, ID_BGIN,
                            ROLE_CHECK);
        g_note = add_label(hwnd, L"", 20, MAIN_H - 80, MAIN_W - 40, 20, ROLE_NOTE, ID_NOTE, g_font);
        add_button(hwnd, L"Play", BS_DEFPUSHBUTTON, MAIN_W - 20 - 88 - 8 - 88, MAIN_H - 50, 88, 32, IDOK, ROLE_PRIMARY);
        add_button(hwnd, L"Quit", BS_PUSHBUTTON, MAIN_W - 20 - 88, MAIN_H - 50, 88, 32, IDCANCEL, ROLE_BUTTON);
        load_choices();
        set_fps_hint((int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0));
        SetFocus(GetDlgItem(hwnd, IDOK));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_MODE && HIWORD(wp) == CBN_SELCHANGE) update_note();
        else if (LOWORD(wp) == ID_FPS && (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_CLOSEUP))
            set_fps_hint((int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0));
        else if (LOWORD(wp) == ID_PAD && HIWORD(wp) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_pad, CB_GETCURSEL, 0, 0);
            memcpy(g_bind, sel == PAD_SWITCH ? g_def : sel == PAD_OTHER ? g_other : g_custom, sizeof g_bind);
        } else if (LOWORD(wp) == ID_PADEDIT) open_panel(hwnd);
        else if (LOWORD(wp) == IDOK) {
            wchar_t err[512];
            if (!save_choices()) {
                swprintf(err, 512, L"Couldn't write %ls.", g_ini);
                MessageBoxW(hwnd, err, g_title, MB_ICONERROR);
            } else if (!launch(err, 512, NULL)) MessageBoxW(hwnd, err, g_title, MB_ICONERROR);
            else DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
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
    wc.lpfnWndProc = panelproc, wc.lpszClassName = L"DOAX3Buttons";
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
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
        if (!IsDialogMessageW(hwnd, &msg)) TranslateMessage(&msg), DispatchMessageW(&msg);
    return 0;
}
