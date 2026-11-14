// DOAX3 Recomp launcher (Windows): pick the display mode (windowed, borderless fullscreen, fullscreen), the
// resolution, the 3D resolution and 60 fps, then start build/doax3recomp.exe as tools/run.sh does. The choices go into
// larecomp.ini (fullscreen, fullscreen_mode, window_size, render3d, fps60, fps120, antialias, anisotropy, upscaler,
// show_fps, background_input: runtime/settings.c), so the game's own menu and
// F11 keep working with them; every other line of the file is kept as it is. The controls choice writes the face buttons'
// bind_pad_a/b/x/y lines (runtime/hle/bindings.c): "Switch controls" is the game's default and has no lines.
// Two options, for RecompLauncher (recomplauncher.json), open no window: --prepare builds the game from the player's files
// and --play starts it. Without them the launcher is as it was.
#define WIN32_LEAN_AND_MEAN  // (UNICODE: -municode)
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { ID_MODE = 100, ID_RES, ID_R3D, ID_NOTE, ID_FPS, ID_PAD, ID_AA, ID_AF, ID_UPS, ID_SHOWFPS, ID_BGIN };
enum { MODE_WINDOWED, MODE_BORDERLESS, MODE_FULLSCREEN };

static wchar_t g_exe_dir[MAX_PATH], g_root[MAX_PATH], g_game[MAX_PATH], g_ini[MAX_PATH], g_title[0x201];
static HWND g_mode, g_res, g_r3d, g_note, g_fps, g_pad, g_aa, g_af, g_ups, g_showfps, g_bgin;
static HFONT g_font, g_bold;
static int g_dpi = 96;
static struct { int w, h; } g_sizes[128];
static int g_nsizes, g_desk_w, g_desk_h;

static const wchar_t *const k_modes[] = {L"Windowed", L"Borderless fullscreen", L"Fullscreen"};
static const char *const k_r3d_keys[] = {"auto", "original", "1080p", "1440p", "4k"};  // (larecomp.ini render3d)
static const wchar_t *const k_r3d_names[] = {L"Default for your GPU", L"Original (1600x900)", L"1920x1080",
                                             L"2560x1440", L"3840x2160 (may run out of memory)"};
// Frame rate (fps60, and fps120 with it), anti-aliasing (antialias) and texture filtering (anisotropy)
static const wchar_t *const k_fps_names[] = {L"30 (as on the Switch)", L"60", L"120 (experimental)"};
static const wchar_t *const k_aa_names[] = {L"Off", L"SMAA"};
static const wchar_t *const k_af_names[] = {L"As on the Switch", L"2x anisotropic", L"4x anisotropic", L"8x anisotropic",
                                            L"16x anisotropic"};
static const int k_af_vals[] = {0, 2, 4, 8, 16};
// Upscaler: how the game's 1080p image is scaled to the window or screen (auto: the game's default for the GPU, FSR 1
// on a dedicated one, bilinear on an integrated one)
static const char *const k_ups_keys[] = {"auto", "fsr1", "bilinear"};
static const wchar_t *const k_ups_names[] = {L"Default for your GPU", L"AMD FSR 1 (sharper)", L"Bilinear (softer)"};
// The controller's face buttons, by position (south, east, west, north). Switch controls: the game's A on the right
// button, as on a Switch; Other: A and B swapped, X and Y swapped (A at the bottom, as on an Xbox pad).
enum { PAD_SWITCH, PAD_OTHER, PAD_CUSTOM };
static const char *const k_face_keys[4] = {"bind_pad_a", "bind_pad_b", "bind_pad_x", "bind_pad_y"};
static const char *const k_face_switch[4] = {"east", "south", "north", "west"};
static const char *const k_face_other[4] = {"south", "east", "west", "north"};
static const wchar_t *const k_pad_names[] = {L"Switch controls", L"Other (A and B, X and Y swapped)",
                                             L"Custom (set in the game's menu)"};
static int g_pad_found;  // (what larecomp.ini had: kept as it is while Custom stays chosen)

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

static void update_note(void) {
    int mode = (int)SendMessageW(g_mode, CB_GETCURSEL, 0, 0);
    EnableWindow(g_res, mode != MODE_BORDERLESS);
    wchar_t t[160];
    if (mode == MODE_BORDERLESS) swprintf(t, 160, L"Borderless uses your desktop resolution (%dx%d).", g_desk_w, g_desk_h);
    else if (mode == MODE_FULLSCREEN) swprintf(t, 160, L"Fullscreen switches the display to this resolution.");
    else swprintf(t, 160, L"The window's size; it can also be resized while playing.");
    SetWindowTextW(g_note, t);
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
    const char *f60 = ini_get("fps60");  // (no line: 60, the game's default)
    SendMessageW(g_fps, CB_SETCURSEL, *f60 && !atoi(f60) ? 0 : atoi(ini_get("fps120")) ? 2 : 1, 0);
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
    int sw = 1, ot = 1;  // (no line: the game's default)
    for (int i = 0; i < 4; i++) {
        const char *v = ini_get(k_face_keys[i]);
        sw &= !*v || !strcmp(v, k_face_switch[i]);
        ot &= !strcmp(v, k_face_other[i]);
    }
    g_pad_found = sw ? PAD_SWITCH : ot ? PAD_OTHER : PAD_CUSTOM;
    if (g_pad_found == PAD_CUSTOM) SendMessageW(g_pad, CB_ADDSTRING, 0, (LPARAM)k_pad_names[PAD_CUSTOM]);
    SendMessageW(g_pad, CB_SETCURSEL, g_pad_found, 0);
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
    ini_set("fps60", fps >= 1 ? "1" : "0");
    ini_set("fps120", fps == 2 ? "1" : "0");
    ini_set("antialias", SendMessageW(g_aa, CB_GETCURSEL, 0, 0) == 1 ? "smaa" : "off");
    char afv[8];
    snprintf(afv, sizeof afv, "%d", k_af_vals[af < 0 ? 0 : af]);
    ini_set("anisotropy", afv);
    int ups = (int)SendMessageW(g_ups, CB_GETCURSEL, 0, 0);
    ini_set("upscaler", k_ups_keys[ups < 0 ? 0 : ups]);
    ini_set("show_fps", SendMessageW(g_showfps, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
    ini_set("background_input", SendMessageW(g_bgin, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
    int pad = (int)SendMessageW(g_pad, CB_GETCURSEL, 0, 0);
    if (pad == PAD_SWITCH || pad == PAD_OTHER) {
        // A control the game's menu moved onto a face button goes back to its own button, so no two share one.
        static const char *const others[] = {"bind_pad_lstick", "bind_pad_rstick", "bind_pad_l", "bind_pad_r",
                                             "bind_pad_zl", "bind_pad_zr", "bind_pad_plus", "bind_pad_minus",
                                             "bind_pad_left", "bind_pad_up", "bind_pad_right", "bind_pad_down"};
        for (int i = 0; i < 12; i++)
            for (int k = 0; k < 4; k++)
                if (!strcmp(ini_get(others[i]), k_face_other[k])) { ini_del(others[i]); break; }
        for (int i = 0; i < 4; i++)
            if (pad == PAD_OTHER) ini_set(k_face_keys[i], k_face_other[i]);
            else ini_del(k_face_keys[i]);
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

static HWND add(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id,
                HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static HWND add_combo(HWND parent, const wchar_t *label, int y, int id) {
    add(parent, L"STATIC", label, SS_LEFT, 20, y + 4, 120, 20, -1, g_font);
    return add(parent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 150, y, 280, 300, id, g_font);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        add(hwnd, L"STATIC", g_title, SS_LEFT | SS_ENDELLIPSIS, 20, 16, 410, 26, -1, g_bold);
        g_mode = add_combo(hwnd, L"Display mode", 56, ID_MODE);
        for (int i = 0; i < 3; i++) SendMessageW(g_mode, CB_ADDSTRING, 0, (LPARAM)k_modes[i]);
        g_res = add_combo(hwnd, L"Resolution", 92, ID_RES);
        for (int i = 0; i < g_nsizes; i++) {
            wchar_t t[48];
            swprintf(t, 48, L"%d x %d%ls", g_sizes[i].w, g_sizes[i].h,
                     g_sizes[i].w == g_desk_w && g_sizes[i].h == g_desk_h ? L" (desktop)" : L"");
            SendMessageW(g_res, CB_ADDSTRING, 0, (LPARAM)t);
        }
        g_r3d = add_combo(hwnd, L"3D resolution", 128, ID_R3D);
        for (int i = 0; i < 5; i++) SendMessageW(g_r3d, CB_ADDSTRING, 0, (LPARAM)k_r3d_names[i]);
        g_pad = add_combo(hwnd, L"Controls", 164, ID_PAD);
        for (int i = 0; i < 2; i++) SendMessageW(g_pad, CB_ADDSTRING, 0, (LPARAM)k_pad_names[i]);
        g_fps = add_combo(hwnd, L"Frame rate", 200, ID_FPS);
        for (int i = 0; i < 3; i++) SendMessageW(g_fps, CB_ADDSTRING, 0, (LPARAM)k_fps_names[i]);
        g_aa = add_combo(hwnd, L"Anti-aliasing", 236, ID_AA);
        for (int i = 0; i < 2; i++) SendMessageW(g_aa, CB_ADDSTRING, 0, (LPARAM)k_aa_names[i]);
        g_af = add_combo(hwnd, L"Texture filtering", 272, ID_AF);
        for (int i = 0; i < 5; i++) SendMessageW(g_af, CB_ADDSTRING, 0, (LPARAM)k_af_names[i]);
        g_ups = add_combo(hwnd, L"Upscaler", 308, ID_UPS);
        for (int i = 0; i < 3; i++) SendMessageW(g_ups, CB_ADDSTRING, 0, (LPARAM)k_ups_names[i]);
        g_showfps = add(hwnd, L"BUTTON", L"Show FPS (the frame rate in a corner)", BS_AUTOCHECKBOX | WS_TABSTOP, 150, 344,
                        280, 22, ID_SHOWFPS, g_font);
        g_bgin = add(hwnd, L"BUTTON", L"Controller works in the background", BS_AUTOCHECKBOX | WS_TABSTOP, 150, 370, 280,
                     22, ID_BGIN, g_font);
        g_note = add(hwnd, L"STATIC", L"", SS_LEFT, 20, 404, 410, 20, ID_NOTE, g_font);
        add(hwnd, L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 240, 436, 92, 30, IDOK, g_font);
        add(hwnd, L"BUTTON", L"Quit", BS_PUSHBUTTON | WS_TABSTOP, 338, 436, 92, 30, IDCANCEL, g_font);
        load_choices();
        SetFocus(GetDlgItem(hwnd, IDOK));
        return 0;
    }
    case WM_CTLCOLORSTATIC:  // (labels on the window's own background)
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_COMMAND:
        if (LOWORD(wp) == ID_MODE && HIWORD(wp) == CBN_SELCHANGE) update_note();
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
    wchar_t err[512];
    if (!find_paths(err, 512)) {
        MessageBoxW(NULL, err, L"DOAX3 Recomp launcher", MB_ICONERROR);
        return 1;
    }
    load_title();
    load_sizes();
    ini_load();
    g_dpi = (int)GetDpiForSystem();
    g_font = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                         0, L"Segoe UI");
    g_bold = CreateFontW(-MulDiv(12, g_dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW wc = {.lpfnWndProc = wndproc, .hInstance = inst, .lpszClassName = L"DOAX3Launcher",
                    .hCursor = LoadCursorW(NULL, IDC_ARROW), .hbrBackground = GetSysColorBrush(COLOR_WINDOW),
                    .hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1))};
    RegisterClassW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, px(450), px(484)};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, g_dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    wchar_t caption[0x220];
    swprintf(caption, 0x220, L"%ls - Launcher", g_title);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, caption, style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                                (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
        if (!IsDialogMessageW(hwnd, &msg)) TranslateMessage(&msg), DispatchMessageW(&msg);
    return 0;
}
