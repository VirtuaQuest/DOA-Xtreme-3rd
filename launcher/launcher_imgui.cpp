// DOAX3 Recomp launcher (cross-platform: Linux build here, SDL3 + Dear ImGui): a functional port of launcher.c
// (the Windows Win32 launcher). Same three tabs, same larecomp.ini / body.ini formats and save semantics, same
// headless --prepare / --play for RecompLauncher. The Windows launcher (launcher/launcher.c) is untouched; this
// file is Linux's (and any other SDL3 platform's) launcher/doax3launcher.
//   Game      display mode, resolution, 3D resolution, upscaler, frame rate, anti-aliasing, texture filtering,
//             Show FPS, background controller input (runtime/settings.c)
//   Controls  the 16 game buttons on a drawn controller, click a button to change it; Default / Swapped / Custom
//   Mods      mods/ on or off, each girl's body settings (body.ini beside larecomp.ini, runtime/hle/body_shape.c)
// Save writes larecomp.ini and body.ini without starting the game; Play does that, then starts build/doax3recomp
// and hides the launcher until it exits (as tools/run.sh / the Windows launcher do).
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

// ============================================================================================ constants (launcher.c)
enum { MODE_WINDOWED, MODE_BORDERLESS, MODE_FULLSCREEN };
enum { TAB_GAME, TAB_CONTROLS, TAB_MODS, NTABS };
static const char *const k_tab_names[NTABS] = {"Game", "Controls", "Mods"};
static const char *const k_modes[] = {"Windowed", "Borderless fullscreen", "Fullscreen"};
static const char *const k_r3d_keys[] = {"auto", "original", "1080p", "1440p", "4k"};
static const char *const k_r3d_names[] = {"Default for your GPU", "Original (1600x900)", "1920x1080", "2560x1440",
                                           "3840x2160 (may run out of memory)"};
static const char *const k_fps_names[] = {"30 (as on the Switch)", "60 (experimental)", "120 (experimental)"};
static const char *const k_fps_hints[] = {
    "The game's own speed, as on the Switch.",
    "Experimental: the Volleyball AI may not work as intended, and some menus scroll faster.",
    "Experimental: the Volleyball AI may not work as intended, and some menus scroll faster."};
static const char k_fps_tip[] =
    "60 and 120 fps are experimental. The game is smoother, but the Beach Volleyball AI may not work as intended "
    "(the other girls miss the ball more and rallies are shorter), and holding a direction in some menus scrolls "
    "faster. 30 is the game's own frame rate, as on the Switch.";
static const char *const k_aa_names[] = {"Off", "SMAA"};
static const char *const k_af_names[] = {"As on the Switch", "2x anisotropic", "4x anisotropic", "8x anisotropic",
                                          "16x anisotropic"};
static const int k_af_vals[] = {0, 2, 4, 8, 16};
static const char *const k_ups_keys[] = {"auto", "fsr1", "bilinear"};
static const char *const k_ups_names[] = {"Default for your GPU", "AMD FSR 1 (sharper)", "Bilinear (softer)"};

enum { PAD_SWITCH, PAD_OTHER, PAD_CUSTOM };
#define NSLOTS 16
static const char *const k_slot_keys[NSLOTS] = {"a",    "b",    "x",   "y",  "lstick", "rstick", "l",     "r",
                                                 "zl",   "zr",   "plus", "minus", "left", "up",   "right", "down"};
static const char *const k_slot_names[NSLOTS] = {
    "A", "B", "X", "Y", "L stick press", "R stick press", "L", "R", "ZL", "ZR", "Plus (+)",
    "Minus (-)", "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down"};
static const char *const k_slot_glyph[NSLOTS] = {
    "A", "B", "X", "Y", "LS", "RS", "L", "R", "ZL", "ZR", "+", "-", "<", "^", ">", "v"};
static const char *const k_slot_default[NSLOTS] = {
    "east", "south", "north", "west", "lstick", "rstick", "lshoulder", "rshoulder", "ltrigger", "rtrigger", "start",
    "back", "dpad_left", "dpad_up", "dpad_right", "dpad_down"};
static const char *const k_face_other[4] = {"south", "east", "west", "north"};
static const int k_col_left[8] = {8, 6, 11, 4, 13, 12, 14, 15}, k_col_right[8] = {9, 7, 10, 2, 3, 0, 1, 5};
struct Host { const char *key, *name; };
static const Host k_hosts[] = {
    {"south", "Bottom (Xbox A, PS Cross)"},     {"east", "Right (Xbox B, PS Circle)"},
    {"west", "Left (Xbox X, PS Square)"},       {"north", "Top (Xbox Y, PS Triangle)"},
    {"lshoulder", "LB / L1 / L"},                {"rshoulder", "RB / R1 / R"},
    {"ltrigger", "LT / L2 / ZL"},                 {"rtrigger", "RT / R2 / ZR"},
    {"start", "Start / Options / +"},             {"back", "Back / Share / -"},
    {"lstick", "Left stick press"},               {"rstick", "Right stick press"},
    {"dpad_left", "D-pad Left"},                   {"dpad_up", "D-pad Up"},
    {"dpad_right", "D-pad Right"},                 {"dpad_down", "D-pad Down"},
    {"rpaddle1", "Right paddle 1 (Elite P1)"},     {"rpaddle2", "Right paddle 2 (Elite P2)"},
    {"lpaddle1", "Left paddle 1 (Elite P3)"},      {"lpaddle2", "Left paddle 2 (Elite P4)"},
    {"misc1", "Share / Mute / Capture"},           {"touchpad", "PS touchpad press"},
    {"guide", "Guide / PS / Home"},                {"misc2", "Extra button 2"},
    {"misc3", "Extra button 3"},                   {"misc4", "Extra button 4"},
    {"misc5", "Extra button 5"},                   {"misc6", "Extra button 6"},
};
#define NHOSTS ((int)(sizeof k_hosts / sizeof *k_hosts))
#define NDRAWN 16
static const char *const k_pad_names[] = {"Default (Switch layout)", "Swapped (A<->B, X<->Y)", "Custom"};

#define NGIRLS 11
struct Girl { const char *code, *name; float bust; };
static const Girl k_girls[NGIRLS] = {
    {"KAS", "Kasumi", 1}, {"MAR", "Marie Rose", .63f}, {"HON", "Honoka", 1.06f}, {"AYA", "Ayane", 1},
    {"NYO", "Nyotengu", 1}, {"KOK", "Kokoro", 1}, {"HTM", "Hitomi", 1}, {"MOM", "Momiji", 1}, {"HEL", "Helena", 1},
    {"MIS", "Misaki", 1}, {"LEI", "Leifang", 1},
};
#define CALMEST 1

enum { K_BUST, K_BUST_JIG, K_BUTT, K_BUTT_JIG, K_THIGH, K_THIGH_JIG, K_ARM, K_CALF, K_WAIST, K_WEIGHT, K_CONTACT,
       NVAL, K_PHYS = NVAL, K_AMOUNT, NKEYS };
struct ValDef { const char *key, *label; float lo, hi; bool pct; };
static const ValDef k_vals[NVAL] = {
    {"bust_size", "Bust size", .3f, 2.5f, false},     {"bust_jiggle", "Bust jiggle", 0, 2, true},
    {"butt_size", "Butt size", .5f, 2.5f, false},     {"butt_jiggle", "Butt jiggle", 0, 2, true},
    {"thigh_size", "Thighs", .7f, 2.0f, false},       {"thigh_jiggle", "Thigh jiggle", 0, 2, true},
    {"arm_size", "Arms", .7f, 2.0f, false},           {"calf_size", "Calves", .7f, 2.0f, false},
    {"waist_size", "Waist", .7f, 1.8f, false},         {"bust_weight", "Bust weight", .5f, 3, true},
    {"bust_contact", "Bust contact", 0, 1, true},
};

#define SKY_TOP IM_COL32(0xF4, 0xFC, 0xFF, 255)
#define SKY_BOTTOM IM_COL32(0x9A, 0xDF, 0xF7, 255)
#define SEA_TOP IM_COL32(0x0B, 0x52, 0xB2, 255)
#define SEA_BOTTOM IM_COL32(0x0A, 0x86, 0xC0, 255)
#define PINK IM_COL32(0xFF, 0x3D, 0x9A, 255)
#define DEEP IM_COL32(0x0B, 0x3D, 0x91, 255)
#define YELLOW IM_COL32(0xFF, 0xEC, 0x8C, 255)
#define WARN_COL IM_COL32(0xFF, 0xB4, 0x6A, 255)
#define MAIN_W 860
#define MAIN_H 600
#define MAIN_HEADER 122

static bool file_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}
static bool dir_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
static std::string dirname_of(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static bool starts_with_ci_token(const std::string &s, size_t n, const char *key) {
    return s.size() >= n && strncasecmp(s.c_str(), key, n) == 0;
}

// ============================================================================================ larecomp.ini: lines
struct Ini {
    std::vector<std::string> lines;
    std::string path;

    void load() {
        lines.clear();
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) return;
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            size_t n = strcspn(line, "\r\n");
            line[n] = 0;
            lines.push_back(line);
        }
        fclose(f);
    }
    int find(const char *key) const {
        size_t n = strlen(key);
        for (size_t i = 0; i < lines.size(); i++) {
            const char *s = lines[i].c_str();
            while (*s == ' ' || *s == '\t') s++;
            if (!strncmp(s, key, n) && (s[n] == ' ' || s[n] == '=')) return (int)i;
        }
        return -1;
    }
    std::string get(const char *key) const {
        int i = find(key);
        if (i < 0) return "";
        size_t eq = lines[i].find('=');
        if (eq == std::string::npos) return "";
        size_t v = eq + 1;
        while (v < lines[i].size() && lines[i][v] == ' ') v++;
        return lines[i].substr(v);
    }
    void set(const char *key, const std::string &val) {
        std::string line = std::string(key) + " = " + val;
        int i = find(key);
        if (i >= 0) lines[i] = line;
        else lines.push_back(line);
    }
    void del(const char *key) {
        for (int i = find(key); i >= 0; i = find(key)) lines.erase(lines.begin() + i);
    }
    bool save() const {
        FILE *f = fopen(path.c_str(), "wb");
        if (!f) return false;
        for (auto &l : lines) fprintf(f, "%s\n", l.c_str());
        return fclose(f) == 0;
    }
};

// ============================================================================================ body.ini
struct Body {
    float v[NVAL] = {}, amount = 1;
    int phys = -1;
    bool has[NKEYS] = {};
    void reset() {
        for (int k = 0; k < NVAL; k++) v[k] = 1;
        amount = 1, phys = -1;
        memset(has, 0, sizeof has);
    }
};
static bool body_any(const Body &b) {
    for (int k = 0; k < NKEYS; k++)
        if (b.has[k]) return true;
    return false;
}
static bool body_not_default(const Body &b) {
    for (int k = 0; k < NVAL; k++)
        if (fabsf(b.v[k] - 1) > .004f) return true;
    return b.phys >= 0;
}
static int girl_index(const std::string &s) {
    for (int i = 0; i < NGIRLS; i++) {
        size_t first = strcspn(k_girls[i].name, " ");
        if (!strcasecmp(s.c_str(), k_girls[i].code) || !strcasecmp(s.c_str(), k_girls[i].name) ||
            (s.size() == first && !strncasecmp(s.c_str(), k_girls[i].name, first)))
            return i;
    }
    return -1;
}
static void body_load(const std::string &path, Body body[NGIRLS]) {
    for (int g = 0; g < NGIRLS; g++) body[g].reset();
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;
    char line[512];
    int cur = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "#;")] = 0;
        std::string s = trim(line);
        if (!s.empty() && s[0] == '[') {
            size_t e = s.find(']');
            cur = girl_index(trim(s.substr(1, e == std::string::npos ? std::string::npos : e - 1)));
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos || cur < 0) continue;
        std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        Body *b = &body[cur];
        float x = (float)atof(v.c_str());
        int g = girl_index(v);
        for (int i = 0; i < NVAL; i++)
            if (k == k_vals[i].key && x >= k_vals[i].lo && x <= k_vals[i].hi) b->v[i] = x, b->has[i] = true;
        if (k == "bust_physics" && (g >= 0 || v.empty() || !strcasecmp(v.c_str(), "none")))
            b->phys = g != cur ? g : -1, b->has[K_PHYS] = true;
        else if (k == "bust_physics_amount" && x >= 0 && x <= 1) b->amount = x, b->has[K_AMOUNT] = true;
    }
    fclose(f);
}
static void body_over(Body *r, const Body &b) {
    for (int k = 0; k < NVAL; k++)
        if (b.has[k]) r->v[k] = b.v[k];
    if (b.has[K_PHYS]) r->phys = b.phys;
    if (b.has[K_AMOUNT]) r->amount = b.amount;
}

struct Mod {
    std::string name;
    bool on = false, was_on = false;
    Body body[NGIRLS];
    bool changes[NGIRLS] = {};
};

// body.ini as lines, rewritten setting by setting: other lines (comments, other girls) stay as they are
static bool bl_blank(const std::string &s) {
    std::string t = trim(s);
    return t.empty() || t[0] == '#' || t[0] == ';';
}
static bool bl_head(const std::vector<std::string> &bl, int i, const char *code) {
    std::string s = trim(bl[i]);
    if (s.empty() || s[0] != '[') return false;
    if (!code) return true;
    size_t e = s.find(']');
    std::string t = trim(s.substr(1, e == std::string::npos ? std::string::npos : e - 1));
    return girl_index(t) == girl_index(code);
}
static int bl_end(const std::vector<std::string> &bl, int head) {
    int j = head + 1, n = (int)bl.size();
    while (j < n && !bl_head(bl, j, nullptr)) j++;
    while (j > head + 1 && bl_blank(bl[j - 1])) j--;
    return j;
}
static int bl_key(const std::vector<std::string> &bl, int head, const std::string &key) {
    int n = (int)bl.size();
    for (int j = head + 1; j < n && !bl_head(bl, j, nullptr); j++) {
        const char *s = bl[j].c_str();
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, key.c_str(), key.size()) &&
            (s[key.size()] == ' ' || s[key.size()] == '\t' || s[key.size()] == '='))
            return j;
    }
    return -1;
}

static std::string body_ini_path(const std::string &root) { return root + "/body.ini"; }

static bool body_save(const std::string &root, Body g_body[NGIRLS], std::string *err) {
    std::string path = body_ini_path(root);
    bool any = false;
    for (int g = 0; g < NGIRLS; g++) any |= body_any(g_body[g]);
    std::vector<std::string> bl;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f && !any) return true;
    char line[1024];
    if (f) {
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            bl.push_back(line);
        }
        fclose(f);
    } else {
        bl.push_back("# Body settings from the launcher's Mods tab (docs/MODDING.md, \"Body shape\"). The game "
                      "reads them when it starts,");
        bl.push_back("# after every mod's body.ini, so these win. One section per girl, by her code; delete a "
                      "line (or the file) to undo.");
        bl.push_back("");
    }
    for (int g = 0; g < NGIRLS; g++) {
        const Body &b = g_body[g];
        std::string key[NKEYS], val[NKEYS];
        for (int k = 0; k < NKEYS; k++) {
            key[k] = k < NVAL ? k_vals[k].key : k == K_PHYS ? "bust_physics" : "bust_physics_amount";
            if (!b.has[k]) continue;
            char buf[32];
            if (k < NVAL) snprintf(buf, 32, "%.2f", b.v[k]);
            else if (k == K_PHYS) snprintf(buf, 32, "%s", b.phys >= 0 ? k_girls[b.phys].code : "none");
            else snprintf(buf, 32, "%.2f", b.amount);
            val[k] = buf;
        }
        int head = -1;
        for (int j = 0; j < (int)bl.size() && head < 0; j++)
            if (bl_head(bl, j, k_girls[g].code)) head = j;
        if (head < 0) {
            if (!body_any(b)) continue;
            if (!bl.empty() && !bl_blank(bl.back())) bl.push_back("");
            bl.push_back(std::string("[") + k_girls[g].code + "]");
            head = (int)bl.size() - 1;
        }
        for (int k = 0; k < NKEYS; k++) {
            int j = bl_key(bl, head, key[k]);
            if (val[k].empty()) {
                if (j >= 0) bl.erase(bl.begin() + j);
                continue;
            }
            std::string line2 = key[k] + " = " + val[k];
            if (j >= 0) bl[j] = line2;
            else bl.insert(bl.begin() + bl_end(bl, head), line2);
        }
        bool left = false;
        for (int j = head + 1; j < (int)bl.size() && !bl_head(bl, j, nullptr); j++) left |= !bl_blank(bl[j]);
        if (!left) bl.erase(bl.begin() + head);
    }
    f = fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "Couldn't write " + path + ".";
        return false;
    }
    for (auto &l : bl) fprintf(f, "%s\n", l.c_str());
    return fclose(f) == 0;
}

// ============================================================================================ app state
struct App {
    std::string exe_dir, root, game, ini_path, title = "DEAD OR ALIVE Xtreme 3 Scarlet";
    Ini ini;
    // Game tab
    int mode = 0, res_sel = 0, r3d = 0, fps = 0, aa = 0, af = 0, ups = 0;
    bool show_fps = false, bg_input = true;
    struct Size { int w, h; };
    std::vector<Size> sizes;
    int desk_w = 0, desk_h = 0;
    // Controls
    int def_bind[NSLOTS], other_bind[NSLOTS], bind[NSLOTS];
    // Mods
    std::vector<Mod> mods;
    Body body[NGIRLS];
    bool body_dirty = false;
    int cur_mod = -1, girl = 0;
    // UI-only
    int tab = TAB_GAME;
    std::string save_note = "Save keeps your choices; Play saves them too and starts the game.";
    std::string mod_error;
};

static int host_index(const std::string &v) {
    size_t n = v.find_first_of(" \t");
    if (n == std::string::npos) n = v.size();
    for (int h = 0; h < NHOSTS; h++)
        if (strlen(k_hosts[h].key) == n && !strncmp(v.c_str(), k_hosts[h].key, n)) return h;
    return -1;
}

static void init_binds(App &a) {
    for (int i = 0; i < NSLOTS; i++) a.def_bind[i] = a.other_bind[i] = host_index(k_slot_default[i]);
    for (int i = 0; i < 4; i++) a.other_bind[i] = host_index(k_face_other[i]);
    memcpy(a.bind, a.def_bind, sizeof a.bind);
}
static int pad_layout(App &a) {
    if (!memcmp(a.bind, a.def_bind, sizeof a.bind)) return PAD_SWITCH;
    if (!memcmp(a.bind, a.other_bind, sizeof a.bind)) return PAD_OTHER;
    return PAD_CUSTOM;
}
static int bind_assign(App &a, int slot, int host) {
    int moved = -1;
    for (int i = 0; i < NSLOTS; i++)
        if (i != slot && a.bind[i] == host) moved = i, a.bind[i] = a.bind[slot];
    a.bind[slot] = host;
    return moved;
}

// ---- paths (launcher.c find_paths, Linux names: doax3recomp, no .exe)
static bool find_paths(App &a, std::string *err) {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) { *err = "Can't find my own path."; return false; }
    buf[n] = 0;
    a.exe_dir = dirname_of(buf);
    a.game = a.exe_dir + "/doax3recomp";
    if (!file_exists(a.game)) a.game = a.exe_dir + "/build/doax3recomp";
    if (!file_exists(a.game)) {
        *err = "doax3recomp isn't next to the launcher (" + a.exe_dir + ") or in build there. Build it first.";
        return false;
    }
    a.root = a.exe_dir;
    if (!dir_exists(a.root + "/extracted")) {
        a.root = dirname_of(a.root);
        if (!dir_exists(a.root + "/extracted")) {
            *err = "The game files (an \"extracted\" folder) aren't next to the launcher or in the folder above "
                   "it. Extract them with tools/linux/setup.sh first.";
            return false;
        }
    }
    a.ini_path = a.root + "/larecomp.ini";
    return true;
}

static void load_title(App &a) {
    std::string p = a.root + "/extracted/control/control.nacp";
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return;
    char name[0x201] = {0};
    for (int i = 0; i < 16; i++) {
        if (fseek(f, i * 0x300L, SEEK_SET)) break;
        memset(name, 0, sizeof name);
        if (fread(name, 1, 0x200, f) != 0x200) break;
        if (name[0]) break;
    }
    fclose(f);
    if (name[0]) a.title = name;
}

static int size_index(App &a, int w, int h) {
    for (size_t i = 0; i < a.sizes.size(); i++)
        if (a.sizes[i].w == w && a.sizes[i].h == h) return (int)i;
    return -1;
}

static void load_sizes(App &a) {
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *desk = SDL_GetDesktopDisplayMode(disp);
    if (desk) a.desk_w = desk->w, a.desk_h = desk->h;
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(disp, &count);
    if (modes) {
        for (int i = 0; i < count; i++) {
            int w = modes[i]->w, h = modes[i]->h;
            if (w < 800 || h < 600 || (a.desk_w && (w > a.desk_w || h > a.desk_h))) continue;
            if (size_index(a, w, h) < 0) a.sizes.push_back({w, h});
        }
        SDL_free(modes);
    }
    if (a.sizes.empty()) a.sizes.push_back({1280, 720});
    std::sort(a.sizes.begin(), a.sizes.end(), [](const App::Size &x, const App::Size &y) {
        return x.w != y.w ? x.w > y.w : x.h > y.h;
    });
}

// ---- Game tab choices (launcher.c load_choices / save_choices)
static void load_choices(App &a) {
    int fs = atoi(a.ini.get("fullscreen").c_str());
    bool excl = a.ini.get("fullscreen_mode") == "exclusive";
    a.mode = !fs ? MODE_WINDOWED : excl ? MODE_FULLSCREEN : MODE_BORDERLESS;
    int w = 0, h = 0, sel = -1;
    if (sscanf(a.ini.get("window_size").c_str(), "%dx%d", &w, &h) == 2) sel = size_index(a, w, h);
    if (sel < 0) sel = fs ? size_index(a, a.desk_w, a.desk_h) : size_index(a, 1280, 720);
    a.res_sel = sel < 0 ? 0 : sel;
    a.r3d = 0;
    for (int i = 1; i < 5; i++)
        if (a.ini.get("render3d") == k_r3d_keys[i]) a.r3d = i;
    int fr = atoi(a.ini.get("frame_rate").c_str());
    a.fps = fr >= 120 ? 2 : fr >= 60 ? 1 : 0;
    std::string aa = a.ini.get("antialias");
    a.aa = !aa.empty() && aa != "off" ? 1 : 0;
    a.af = 0;
    for (int i = 1; i < 5; i++)
        if (atoi(a.ini.get("anisotropy").c_str()) >= k_af_vals[i]) a.af = i;
    std::string ups = a.ini.get("upscaler");
    a.ups = ups == "bilinear" ? 2 : (ups.empty() || ups == "auto") ? 0 : 1;
    a.show_fps = atoi(a.ini.get("show_fps").c_str()) != 0;
    std::string bg = a.ini.get("background_input");
    a.bg_input = bg.empty() || atoi(bg.c_str()) != 0;
    for (int i = 0; i < NSLOTS; i++) {
        std::string key = std::string("bind_pad_") + k_slot_keys[i];
        int h2 = host_index(a.ini.get(key.c_str()));
        a.bind[i] = h2 >= 0 ? h2 : a.def_bind[i];
    }
}

static bool save_choices(App &a) {
    a.ini.set("fullscreen", a.mode == MODE_WINDOWED ? "0" : "1");
    if (a.mode != MODE_WINDOWED) a.ini.set("fullscreen_mode", a.mode == MODE_FULLSCREEN ? "exclusive" : "borderless");
    if (a.mode != MODE_BORDERLESS && a.res_sel >= 0 && a.res_sel < (int)a.sizes.size()) {
        char v[32];
        snprintf(v, sizeof v, "%dx%d", a.sizes[a.res_sel].w, a.sizes[a.res_sel].h);
        a.ini.set("window_size", v);
    }
    a.ini.set("render3d", k_r3d_keys[a.r3d < 0 ? 0 : a.r3d]);
    a.ini.set("frame_rate", a.fps == 2 ? "120" : a.fps == 1 ? "60" : "30");
    a.ini.del("fps60");
    a.ini.del("fps120");
    a.ini.set("antialias", a.aa == 1 ? "smaa" : "off");
    char afv[8];
    snprintf(afv, sizeof afv, "%d", k_af_vals[a.af < 0 ? 0 : a.af]);
    a.ini.set("anisotropy", afv);
    a.ini.set("upscaler", k_ups_keys[a.ups < 0 ? 0 : a.ups]);
    a.ini.set("show_fps", a.show_fps ? "1" : "0");
    a.ini.set("background_input", a.bg_input ? "1" : "0");
    for (int i = 0; i < NSLOTS; i++) {
        std::string key = std::string("bind_pad_") + k_slot_keys[i];
        if (a.bind[i] != a.def_bind[i]) a.ini.set(key.c_str(), k_hosts[a.bind[i]].key);
        else a.ini.del(key.c_str());
    }
    return a.ini.save();
}

// ---- mods (launcher.c mods_load / mods_save, mod_scan_changes)
static void mod_folder(const App &a, const Mod &m, bool on, std::string *out) {
    *out = a.root + "/mods/" + (on ? "" : "_") + m.name;
}
static void mod_scan_changes(const App &a, Mod *m) {
    std::string base;
    mod_folder(a, *m, m->was_on, &base);
    std::string romfs = base + "/romfs";
    DIR *d = opendir(romfs.c_str());
    if (!d) return;
    struct dirent *pk;
    while ((pk = readdir(d))) {
        if (pk->d_name[0] == '.') continue;
        std::string pkpath = romfs + "/" + pk->d_name;
        if (!dir_exists(pkpath)) continue;
        std::string chdir_ = pkpath + "/character";
        DIR *d2 = opendir(chdir_.c_str());
        if (!d2) continue;
        struct dirent *ch;
        while ((ch = readdir(d2))) {
            if (ch->d_name[0] == '.' || strlen(ch->d_name) > 8) continue;
            if (!dir_exists(chdir_ + "/" + ch->d_name)) continue;
            int g = girl_index(ch->d_name);
            if (g >= 0 && strlen(ch->d_name) == 3) m->changes[g] = true;
        }
        closedir(d2);
    }
    closedir(d);
}
static void mods_load(App &a) {
    a.mods.clear();
    body_load(body_ini_path(a.root), a.body);
    std::string moddir = a.root + "/mods";
    DIR *d = opendir(moddir.c_str());
    if (d) {
        struct dirent *fd;
        while ((fd = readdir(d))) {
            if (fd->d_name[0] == '.' || !dir_exists(moddir + "/" + fd->d_name)) continue;
            Mod m;
            m.on = m.was_on = fd->d_name[0] != '_';
            m.name = fd->d_name + (m.on ? 0 : 1);
            a.mods.push_back(m);
        }
        closedir(d);
    }
    std::sort(a.mods.begin(), a.mods.end(),
              [](const Mod &x, const Mod &y) { return strcasecmp(x.name.c_str(), y.name.c_str()) < 0; });
    for (auto &m : a.mods) {
        std::string p;
        mod_folder(a, m, m.was_on, &p);
        body_load(p + "/body.ini", m.body);
        mod_scan_changes(a, &m);
    }
}
static Body body_of(App &a, int g, bool mine) {
    Body r;
    r.reset();
    std::vector<int> order;
    for (size_t i = 0; i < a.mods.size(); i++)
        if (a.mods[i].on) order.push_back((int)i);
    std::sort(order.begin(), order.end(),
              [&](int x, int y) { return strcasecmp(a.mods[x].name.c_str(), a.mods[y].name.c_str()) < 0; });
    for (int i : order) body_over(&r, a.mods[i].body[g]);
    if (mine) body_over(&r, a.body[g]);
    return r;
}
static void body_put(App &a, int g, int key, float x, int phys) {
    Body base = body_of(a, g, false);
    Body *b = &a.body[g];
    if (key < NVAL) b->v[key] = x, b->has[key] = fabsf(x - base.v[key]) > .004f;
    else if (key == K_PHYS) b->phys = phys, b->has[K_PHYS] = phys != base.phys;
    else b->amount = x, b->has[K_AMOUNT] = fabsf(x - base.amount) > .004f;
    a.body_dirty = true;
}
static bool mods_save(App &a, std::string *err) {
    if (a.body_dirty && !body_save(a.root, a.body, err)) return false;
    a.body_dirty = false;
    for (auto &m : a.mods) {
        if (m.on == m.was_on) continue;
        std::string from, to;
        mod_folder(a, m, m.was_on, &from);
        mod_folder(a, m, m.on, &to);
        if (rename(from.c_str(), to.c_str()) != 0) {
            *err = "Couldn't turn the mod " + m.name + " " + (m.on ? "on" : "off") +
                   ": renaming its folder failed. Is the game running, or a window open in that folder?";
            return false;
        }
        m.was_on = m.on;
    }
    return true;
}

// ============================================================================================ launching the game
// As tools/run.sh: the data folder as the working directory and LARECOMP_DATA_ROOT, the log in doax3.log.
static bool launch(App &a, std::string *err, pid_t *out_pid) {
    std::string logpath = a.root + "/doax3.log";
    int logfd = open(logpath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    pid_t pid = fork();
    if (pid < 0) {
        *err = "Couldn't start " + a.game + " (fork failed).";
        if (logfd >= 0) close(logfd);
        return false;
    }
    if (pid == 0) {  // child
        if (chdir(a.root.c_str()) != 0) _exit(127);
        setenv("LARECOMP_DATA_ROOT", a.root.c_str(), 1);
        if (logfd >= 0) dup2(logfd, 1), dup2(logfd, 2);
        char *argv[] = {(char *)a.game.c_str(), (char *)"extracted", nullptr};
        execv(a.game.c_str(), argv);
        _exit(127);
    }
    if (logfd >= 0) close(logfd);
    if (out_pid) *out_pid = pid;
    else {
        int status;
        waitpid(pid, &status, 0);
    }
    return true;
}

// ============================================================================================ headless RecompLauncher
enum { EXIT_NOT_SET_UP = 20, EXIT_REBUILD_FAILED = 21 };

static bool find_project(std::string *proj) {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return false;
    buf[n] = 0;
    std::string p = dirname_of(buf);
    for (int up = 0; up < 3; up++) {
        if (file_exists(p + "/tools/linux/setup.sh")) { *proj = p; return true; }
        size_t s = p.find_last_of('/');
        if (s == std::string::npos) break;
        p = p.substr(0, s);
    }
    return false;
}

// Runs a command, inheriting our stdout/stderr, cwd = proj; its exit code (1: couldn't start)
static int run_inherited(const std::vector<std::string> &args, const std::string &cwd) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        std::vector<char *> argv;
        for (auto &s : args) argv.push_back((char *)s.c_str());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int prepare(int argc, char **argv) {
    std::string proj;
    if (!find_project(&proj)) return 1;
    std::vector<std::string> args = {proj + "/tools/linux/setup.sh"};
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--game")) args.push_back("--game"), args.push_back(argv[++i]);
        else if (!strcmp(argv[i], "--keys")) args.push_back("--keys"), args.push_back(argv[++i]);
    }
    return run_inherited(args, proj);
}

static bool read_word(const std::string &path, std::string *out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char line[256] = {0};
    if (!fgets(line, sizeof line, f)) line[0] = 0;
    fclose(f);
    *out = trim(line);
    return true;
}

static bool needs_rebuild(const std::string &proj) {
    std::string release, built;
    if (!read_word(proj + "/release.txt", &release)) return false;  // (a source checkout: built by hand)
    return !read_word(proj + "/build/built-from.txt", &built) || release != built;
}

static int play_headless() {
    std::string proj;
    if (!find_project(&proj)) return EXIT_NOT_SET_UP;
    if (!file_exists(proj + "/extracted/exefs/main")) return EXIT_NOT_SET_UP;
    if (needs_rebuild(proj)) {
        int rc = run_inherited({proj + "/tools/linux/setup.sh", "--keep-extracted"}, proj);
        if (rc != 0 || needs_rebuild(proj)) return EXIT_REBUILD_FAILED;
    }
    App a;
    std::string err;
    if (!find_paths(a, &err)) return EXIT_NOT_SET_UP;
    pid_t pid;
    if (!launch(a, &err, &pid)) return EXIT_NOT_SET_UP;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// ---- --selftest <mock root>: exercises load_choices/save_choices and the Mods tab's save logic without opening a
// window (CI / this port's own verification). Loads from root, sets a few known choices and body/mod changes, saves,
// prints "OK" and the files it wrote.
static int selftest(const std::string &root) {
    App a;
    a.exe_dir = a.root = root;
    a.game = root + "/build/doax3recomp";
    a.ini_path = root + "/larecomp.ini";
    a.ini.path = a.ini_path;
    a.ini.load();
    init_binds(a);
    mods_load(a);
    load_choices(a);
    // Game tab: turn on fullscreen (exclusive) at a size, 60 fps, SMAA, 8x anisotropic, Show FPS
    a.mode = MODE_FULLSCREEN;
    a.sizes.push_back({1920, 1080});
    a.res_sel = (int)a.sizes.size() - 1;
    a.fps = 1;
    a.aa = 1;
    a.af = 3;
    a.show_fps = true;
    // Controls: A off its default (east -> south)
    bind_assign(a, 0, host_index("south"));
    // Mods: turn ModOne off (if present), and set Kasumi's bust_jiggle
    for (auto &m : a.mods)
        if (m.name == "ModOne") m.on = false;
    body_put(a, 0 /* Kasumi */, K_BUST_JIG, 1.4f, 0);
    std::string err;
    bool ok = save_choices(a) && mods_save(a, &err);
    printf(ok ? "OK\n" : ("FAIL: " + err + "\n").c_str());
    return ok ? 0 : 1;
}


// JPEG (icon_AmericanEnglish.dat, no extension) via FFmpeg's jpeg_pipe demuxer + swscale to RGBA. PNG via SDL_LoadPNG.
static SDL_Surface *decode_jpeg(const std::string &path) {
    AVFormatContext *fmt = nullptr;
    const AVInputFormat *informat = av_find_input_format("jpeg_pipe");
    if (avformat_open_input(&fmt, path.c_str(), informat, nullptr) < 0) return nullptr;
    SDL_Surface *surf = nullptr;
    AVCodecContext *ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    SwsContext *sws = nullptr;
    if (avformat_find_stream_info(fmt, nullptr) < 0 || fmt->nb_streams < 1) goto done;
    {
        AVCodecParameters *par = fmt->streams[0]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (!codec) goto done;
        ctx = avcodec_alloc_context3(codec);
        if (!ctx || avcodec_parameters_to_context(ctx, par) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) goto done;
        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        bool got = false;
        while (av_read_frame(fmt, pkt) >= 0 && !got) {
            if (avcodec_send_packet(ctx, pkt) >= 0 && avcodec_receive_frame(ctx, frame) >= 0) got = true;
            av_packet_unref(pkt);
        }
        if (!got || frame->width <= 0 || frame->height <= 0) goto done;
        sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height,
                              AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) goto done;
        surf = SDL_CreateSurface(frame->width, frame->height, SDL_PIXELFORMAT_RGBA32);
        if (!surf) goto done;
        uint8_t *dst[1] = {(uint8_t *)surf->pixels};
        int dststride[1] = {surf->pitch};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dststride);
    }
done:
    if (sws) sws_freeContext(sws);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (ctx) avcodec_free_context(&ctx);
    if (fmt) avformat_close_input(&fmt);
    return surf;
}

static SDL_Texture *load_texture_jpeg(SDL_Renderer *r, const std::string &path) {
    SDL_Surface *s = decode_jpeg(path);
    if (!s) return nullptr;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    return t;
}
static SDL_Texture *load_texture_png(SDL_Renderer *r, const std::string &path) {
    if (!file_exists(path)) return nullptr;
    SDL_Surface *s = SDL_LoadPNG(path.c_str());
    if (!s) return nullptr;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    return t;
}

// ============================================================================================ GUI
// The controller picture (96-dpi units in a 240x180 box): each of k_hosts' first 16 buttons' x, y, w, h, corner
// radius (approximate positions, not pixel-identical to the Windows drawing).
struct PadShape { float x, y, w, h, r; };
static const PadShape k_pad[NDRAWN] = {
    {167, 87, 22, 22, 11}, {187, 67, 22, 22, 11}, {147, 67, 22, 22, 11}, {167, 47, 22, 22, 11},
    {24, 20, 66, 20, 9},   {150, 20, 66, 20, 9},
    {32, 0, 52, 26, 9},    {156, 0, 52, 26, 9},
    {122, 56, 22, 13, 6},  {96, 56, 22, 13, 6},
    {48, 64, 28, 28, 14},  {132, 100, 28, 28, 14},
    {69, 106, 17, 16, 4},  {86, 89, 16, 17, 4}, {102, 106, 17, 16, 4}, {86, 122, 16, 17, 4},
};

static void draw_sky_sea_background(ImDrawList *dl, ImVec2 origin, float w, float h, float header) {
    // Sky down to `header`, then the sea; a thin white and pink line between them (a simplified shoreline, not
    // the wavy one the Windows launcher paints pixel by pixel).
    int bands = 48;
    for (int i = 0; i < bands; i++) {
        float y0 = header * i / bands, y1 = header * (i + 1) / bands;
        float t = (i + .5f) / bands;
        ImVec4 top4 = ImColor(0xF4, 0xFC, 0xFF).Value, bot4 = ImColor(0x9A, 0xDF, 0xF7).Value;
        ImU32 top = ImGui::GetColorU32(ImVec4(top4.x + (bot4.x - top4.x) * t, top4.y + (bot4.y - top4.y) * t,
                                               top4.z + (bot4.z - top4.z) * t, 1));
        dl->AddRectFilled(ImVec2(origin.x, origin.y + y0), ImVec2(origin.x + w, origin.y + y1), top);
    }
    dl->AddRectFilled(ImVec2(origin.x, origin.y + header), ImVec2(origin.x + w, origin.y + header + 3), IM_COL32_WHITE);
    dl->AddRectFilled(ImVec2(origin.x, origin.y + header + 3), ImVec2(origin.x + w, origin.y + header + 6), PINK);
    bands = 64;
    for (int i = 0; i < bands; i++) {
        float y0 = header + 6 + (h - header - 6) * i / bands, y1 = header + 6 + (h - header - 6) * (i + 1) / bands;
        float t = (i + .5f) / bands;
        ImVec4 top5 = ImColor(0x0B, 0x52, 0xB2).Value, bot5 = ImColor(0x0A, 0x86, 0xC0).Value;
        ImU32 c = ImGui::GetColorU32(ImVec4(top5.x + (bot5.x - top5.x) * t, top5.y + (bot5.y - top5.y) * t,
                                             top5.z + (bot5.z - top5.z) * t, 1));
        dl->AddRectFilled(ImVec2(origin.x, origin.y + y0), ImVec2(origin.x + w, origin.y + y1), c);
    }
}

static void apply_theme() {
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 8;
    st.FrameRounding = 6;
    st.GrabRounding = 10;
    st.TabRounding = 8;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_WindowBg] = ImColor(0x0B, 0x52, 0xB2, 0).Value;  // (the drawn gradient shows through)
    c[ImGuiCol_Text] = ImColor(255, 255, 255).Value;
    c[ImGuiCol_FrameBg] = ImColor(0x0C, 0x34, 0x88, 200).Value;
    c[ImGuiCol_FrameBgHovered] = ImColor(0x22, 0x66, 0xC8, 220).Value;
    c[ImGuiCol_FrameBgActive] = ImColor(0x22, 0x66, 0xC8, 255).Value;
    c[ImGuiCol_Button] = ImColor(0x22, 0x66, 0xC8, 255).Value;
    c[ImGuiCol_ButtonHovered] = ImColor(0x3A, 0x80, 0xE0, 255).Value;
    c[ImGuiCol_ButtonActive] = ImColor(0xFF, 0x3D, 0x9A, 255).Value;
    c[ImGuiCol_Header] = ImColor(0xFF, 0x3D, 0x9A, 220).Value;
    c[ImGuiCol_HeaderHovered] = ImColor(0xFF, 0x74, 0xB8, 220).Value;
    c[ImGuiCol_HeaderActive] = ImColor(0xE8, 0x1E, 0x7C, 255).Value;
    c[ImGuiCol_Tab] = ImColor(0x0C, 0x34, 0x88, 230).Value;
    c[ImGuiCol_TabSelected] = ImColor(0xFF, 0x3D, 0x9A, 255).Value;
    c[ImGuiCol_TabHovered] = ImColor(0xFF, 0x74, 0xB8, 230).Value;
    c[ImGuiCol_CheckMark] = ImColor(255, 255, 255).Value;
    c[ImGuiCol_SliderGrab] = ImColor(255, 255, 255).Value;
    c[ImGuiCol_SliderGrabActive] = ImColor(0xFF, 0xEC, 0x8C).Value;
}

struct Ui {
    SDL_Texture *cover = nullptr, *logo = nullptr;
    int pad_hover = -1;
};

static void build_game_tab(App &a) {
    ImGui::TextColored(ImColor(0xFF, 0xEC, 0x8C).Value, "Display");
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Display mode", &a.mode, k_modes, 3);
    bool borderless = a.mode == MODE_BORDERLESS;
    ImGui::BeginDisabled(borderless);
    std::vector<std::string> resl;
    std::vector<const char *> resp;
    for (auto &s : a.sizes) {
        char buf[48];
        snprintf(buf, sizeof buf, "%d x %d%s", s.w, s.h,
                 s.w == a.desk_w && s.h == a.desk_h ? " (desktop)" : "");
        resl.push_back(buf);
    }
    for (auto &s : resl) resp.push_back(s.c_str());
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Resolution", &a.res_sel, resp.data(), (int)resp.size());
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("3D resolution", &a.r3d, k_r3d_names, 5);
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Upscaler", &a.ups, k_ups_names, 3);
    const char *note = a.mode == MODE_BORDERLESS ? "Borderless uses your desktop resolution."
                        : a.mode == MODE_FULLSCREEN ? "Fullscreen switches the display to this resolution."
                                                     : "The window's size; it can also be resized while playing.";
    ImGui::TextWrapped("%s", note);
    ImGui::Spacing();
    ImGui::TextColored(ImColor(0xFF, 0xEC, 0x8C).Value, "Frame rate and graphics");
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Frame rate", &a.fps, k_fps_names, 3);
    if (a.fps > 0) ImGui::TextColored(ImColor(0xFF, 0xB4, 0x6A).Value, "%s", k_fps_hints[a.fps]);
    else ImGui::TextWrapped("%s", k_fps_hints[0]);
    if (ImGui::IsItemHovered()) {}
    if (ImGui::BeginItemTooltip()) { ImGui::TextWrapped("%s", k_fps_tip); ImGui::EndTooltip(); }
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Anti-aliasing", &a.aa, k_aa_names, 2);
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Texture filtering", &a.af, k_af_names, 5);
    ImGui::Checkbox("Show FPS (the frame rate in a corner)", &a.show_fps);
    ImGui::Checkbox("Controller works in the background", &a.bg_input);
}

// One column of the Controls tab: each game button's name and the controller button it's on
static void bind_column(App &a, const int *slots, int id_base) {
    ImGui::BeginGroup();
    for (int k = 0; k < 8; k++) {
        int l = slots[k];
        ImGui::PushID(id_base + l);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", k_slot_names[l]);
        ImGui::SameLine(95);  // (inside the group: relative to its left edge)
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("##bind", k_hosts[a.bind[l]].name)) {
            for (int h = 0; h < NHOSTS; h++)
                if (ImGui::Selectable(k_hosts[h].name, a.bind[l] == h)) bind_assign(a, l, h);
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
}

static void build_controls_tab(App &a, Ui &ui) {
    ImGui::TextWrapped("Each of the game's buttons and the button on your controller it's on. Keyboard keys are "
                        "set in the game's menu (F1).");
    ImGui::Spacing();
    float col_y = ImGui::GetCursorPosY();
    bind_column(a, k_col_left, 0);
    ImGui::SameLine(0, 12);
    ImVec2 pad_origin = ImGui::GetCursorScreenPos();
    float scale = 1.2f;
    ImVec2 pad_size(240 * scale, 180 * scale);
    ImGui::Dummy(pad_size);
    bool pad_hovered = ImGui::IsItemHovered();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pad_origin, ImVec2(pad_origin.x + pad_size.x, pad_origin.y + pad_size.y),
                       IM_COL32(0x0A, 0x30, 0x70, 255), 10);
    ImVec2 mouse = ImGui::GetMousePos();
    int hover_host = -1;
    for (int order = 0; order < NDRAWN; order++) {
        const PadShape &s = k_pad[order];
        ImVec2 p0(pad_origin.x + s.x * scale, pad_origin.y + s.y * scale);
        ImVec2 p1(pad_origin.x + (s.x + s.w) * scale, pad_origin.y + (s.y + s.h) * scale);
        if (pad_hovered && mouse.x >= p0.x && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y) hover_host = order;
    }
    ui.pad_hover = hover_host;
    for (int order = 0; order < NDRAWN; order++) {
        const PadShape &s = k_pad[order];
        ImVec2 p0(pad_origin.x + s.x * scale, pad_origin.y + s.y * scale);
        ImVec2 p1(pad_origin.x + (s.x + s.w) * scale, pad_origin.y + (s.y + s.h) * scale);
        int slot = -1, n = 0;
        for (int i = 0; i < NSLOTS; i++)
            if (a.bind[i] == order) slot = slot < 0 ? i : slot, n++;
        bool hl = order == hover_host;
        ImU32 top = n ? IM_COL32(0x2A, 0x6C, 0xCC, 255) : IM_COL32(0x9C, 0xB6, 0xD4, 255);
        if (hl) top = PINK;
        dl->AddRectFilled(p0, p1, top, s.r * scale * .5f);
        dl->AddRect(p0, p1, IM_COL32(0x08, 0x26, 0x66, 255), s.r * scale * .5f);
        if (slot >= 0) {
            char buf[16];
            snprintf(buf, sizeof buf, "%s%s", k_slot_glyph[slot], n > 1 ? "+" : "");
            ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2((p0.x + p1.x) / 2 - ts.x / 2, (p0.y + p1.y) / 2 - ts.y / 2), IM_COL32_WHITE, buf);
        }
        if (hover_host == order && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && slot >= 0) {
            ImGui::OpenPopup("pad_assign");
            ImGui::SetNextWindowPos(mouse);
        }
    }
    if (ImGui::BeginPopup("pad_assign")) {
        for (int i = 0; i < NSLOTS; i++)
            if (a.bind[i] == hover_host || (hover_host < 0 && false)) {
                ImGui::TextDisabled("%s ->", k_slot_names[i]);
                for (int h = 0; h < NHOSTS; h++)
                    if (ImGui::Selectable(k_hosts[h].name, a.bind[i] == h)) bind_assign(a, i, h);
            }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(pad_origin.x, pad_origin.y + pad_size.y + 4));
    if (hover_host >= 0) {
        std::string t = std::string(k_hosts[hover_host].name) + ": ";
        int any = 0;
        for (int s = 0; s < NSLOTS; s++)
            if (a.bind[s] == hover_host) t += (any++ ? ", " : "the game's ") + std::string(k_slot_names[s]);
        if (!any) t += "no game button";
        ImGui::TextWrapped("%s", t.c_str());
    } else {
        ImGui::TextWrapped("Click a button on the controller to change it.");
    }
    // the right column, beside the picture
    ImVec2 after = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x + 245 + 12 + pad_size.x + 12, col_y));
    bind_column(a, k_col_right, 100);
    ImGui::SetCursorPos(after);
    ImGui::Spacing();
    ImGui::Text("Layout: %s", k_pad_names[pad_layout(a)]);
    if (ImGui::Button("Default (Switch layout)")) memcpy(a.bind, a.def_bind, sizeof a.bind);
    ImGui::SameLine();
    if (ImGui::Button("Swapped (A<->B, X<->Y)")) memcpy(a.bind, a.other_bind, sizeof a.bind);
}

static void val_text(int k, float x, char *t, size_t n) {
    if (k_vals[k].pct) snprintf(t, n, "%d %%", (int)(x * 100 + .5f));
    else snprintf(t, n, "%.2f", x);
}

static void build_mods_tab(App &a) {
    ImGui::TextColored(ImColor(0xFF, 0xEC, 0x8C).Value, "Mods");
    if (a.mods.empty()) {
        ImGui::TextWrapped("Mods go in %s/mods (docs/MODDING.md).", a.root.c_str());
    } else {
        std::vector<std::string> names;
        std::vector<const char *> namep;
        for (auto &m : a.mods) names.push_back(m.name + (m.on ? "" : "  (off)"));
        for (auto &s : names) namep.push_back(s.c_str());
        if (a.cur_mod < 0) a.cur_mod = 0;
        ImGui::SetNextItemWidth(260);
        ImGui::Combo("Mod", &a.cur_mod, namep.data(), (int)namep.size());
        ImGui::SameLine();
        Mod &m = a.mods[a.cur_mod];
        bool on = m.on;
        if (ImGui::Checkbox("On (the game loads it)", &on)) m.on = on;
        ImGui::SameLine();
        if (ImGui::Button("Open folder")) {
            std::string p;
            mod_folder(a, m, m.was_on, &p);
            SDL_OpenURL(("file://" + p).c_str());
        }
        std::string note;
        int any = 0;
        for (int g = 0; g < NGIRLS; g++)
            if (m.changes[g]) note += (any++ ? ", " : "Its files change ") + std::string(k_girls[g].name);
        note += any ? "." : "It changes no girl's files.";
        int any2 = 0;
        for (int g = 0; g < NGIRLS; g++)
            if (body_any(m.body[g])) {
                note += (any2++ ? ", " : " Its body.ini sets ") + std::string(k_girls[g].name);
            }
        if (any2) note += ".";
        if (!m.on) note += " Off: the game doesn't load it.";
        ImGui::TextWrapped("%s", note.c_str());
    }
    ImGui::Separator();
    ImGui::TextColored(ImColor(0xFF, 0xEC, 0x8C).Value, "Body settings");
    std::vector<std::string> girls;
    std::vector<const char *> girlp;
    for (int g = 0; g < NGIRLS; g++) {
        Body base = body_of(a, g, false);
        std::string t = k_girls[g].name;
        if (body_any(a.body[g])) t += "  (set)";
        else if (body_not_default(base)) t += "  (set by a mod)";
        girls.push_back(t);
    }
    for (auto &s : girls) girlp.push_back(s.c_str());
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("Girl", &a.girl, girlp.data(), (int)girlp.size());
    ImGui::SameLine();
    if (ImGui::Button("As she is")) {
        for (int k = 0; k < NKEYS; k++) body_put(a, a.girl, k, 1, -1);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("each girl's, whatever mods are on");  // (what "As she is" means: the game's own, mods or not)

    Body e = body_of(a, a.girl, true);
    bool changed = false;
    for (int k = 0; k < NVAL; k++) {
        ImGui::PushID(k);
        // (percent settings: the slider runs over the percentage, as the Windows launcher shows "150 %")
        float m = k_vals[k].pct ? 100.f : 1.f, v = e.v[k] * m;
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderFloat(k_vals[k].label, &v, k_vals[k].lo * m, k_vals[k].hi * m, k_vals[k].pct ? "%.0f %%" : "%.2f")) {
            body_put(a, a.girl, k, v / m, 0);
            changed = true;
        }
        ImGui::PopID();
    }
    int sel_like = 0;
    std::vector<std::string> likes = {"Pick a girl..."};
    for (int g = 0; g < NGIRLS; g++) {
        char buf[64];
        snprintf(buf, sizeof buf, "%s: %.2f", k_girls[g].name, k_girls[g].bust / k_girls[a.girl].bust);
        likes.push_back(buf);
    }
    std::vector<const char *> likep;
    for (auto &s : likes) likep.push_back(s.c_str());
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Size like", &sel_like, likep.data(), (int)likep.size()) && sel_like > 0) {
        int g = sel_like - 1;
        float v = roundf(k_girls[g].bust / k_girls[a.girl].bust * 100) / 100;
        v = v < .3f ? .3f : v > 2.5f ? 2.5f : v;
        body_put(a, a.girl, K_BUST, v, 0);
        changed = true;
    }
    std::vector<std::string> physn = {"Her own"};
    std::vector<int> physmap;
    int physsel = 0;
    for (int g = 0; g < NGIRLS; g++) {
        if (g == a.girl) continue;
        std::string t = std::string("Like ") + k_girls[g].name + "'s" + (g == CALMEST ? " (the calmest)" : "");
        physn.push_back(t);
        if (e.phys == g) physsel = (int)physmap.size() + 1;
        physmap.push_back(g);
    }
    std::vector<const char *> physp;
    for (auto &s : physn) physp.push_back(s.c_str());
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Bust physics", &physsel, physp.data(), (int)physp.size())) {
        body_put(a, a.girl, K_PHYS, 0, physsel > 0 ? physmap[physsel - 1] : -1);
        changed = true;
    }
    ImGui::BeginDisabled(e.phys < 0);
    float amt = e.amount * 100;
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderFloat("How much like hers", &amt, 0, 100, "%.0f %%")) {
        body_put(a, a.girl, K_AMOUNT, amt / 100, 0);
        changed = true;
    }
    ImGui::EndDisabled();

    e = body_of(a, a.girl, true);
    std::string b;
    int kcount = 0;
    for (int i = 0; i < NVAL; i++)
        if (fabsf(e.v[i] - 1) > .004f) {
            char vb[16];
            val_text(i, e.v[i], vb, 16);
            b += (kcount++ ? ", " : "") + std::string(k_vals[i].label) + " " + vb;
        }
    if (e.phys >= 0)
        b += (kcount++ ? ", " : "") + ("breasts move " + std::to_string((int)(e.amount * 100 + .5f)) + "% like " +
                                        k_girls[e.phys].name + "'s");
    if (!kcount) b = "as she is";
    std::string body_note_text = std::string("In the game: ") + k_girls[a.girl].name + " " + b + ".";
    for (auto &m : a.mods)
        if (m.on && body_any(m.body[a.girl]))
            body_note_text += " The mod " + m.name + " sets her too" + (body_any(a.body[a.girl]) ? "; the settings here win" : "") + ".";
    ImGui::TextWrapped("%s", body_note_text.c_str());
    (void)changed;
}

// ============================================================================================ main
static ImFont *g_title_font;  // (Baloo 2, the title when there's no logo; NULL: the body font)

static void render_header(App &a, Ui &ui, ImDrawList *dl, ImVec2 origin, float w) {
    float x = 20, right = w - 20, top = 11;
    if (ui.cover) {
        float cw, ch;
        SDL_GetTextureSize(ui.cover, &cw, &ch);
        float s = std::min(98.f / cw, 98.f / ch);
        float dw = cw * s, dh = ch * s;
        dl->AddImage((ImTextureID)(intptr_t)ui.cover, ImVec2(origin.x + x, origin.y + top),
                     ImVec2(origin.x + x + dw, origin.y + top + dh));
        x += dw + 18;
    }
    if (ui.logo) {
        float lw, lh;
        SDL_GetTextureSize(ui.logo, &lw, &lh);
        float maxw = right - x, maxh = MAIN_HEADER - 16;
        float s = std::min(maxw / lw, maxh / lh);
        float dw = lw * s, dh = lh * s;
        dl->AddImage((ImTextureID)(intptr_t)ui.logo, ImVec2(origin.x + x + (maxw - dw) / 2, origin.y + (MAIN_HEADER - dh) / 2),
                     ImVec2(origin.x + x + (maxw - dw) / 2 + dw, origin.y + (MAIN_HEADER - dh) / 2 + dh));
        return;
    }
    ImFont *f = g_title_font ? g_title_font : ImGui::GetFont();
    float size = g_title_font ? 40 : ImGui::GetFontSize();
    dl->AddText(f, size, ImVec2(origin.x + x, origin.y + 22), DEEP, a.title.c_str());
    dl->AddText(f, size * .55f, ImVec2(origin.x + x + 2, origin.y + 22 + size + 2), PINK, "PC launcher");
}

static void save_png_screenshot(SDL_Renderer *renderer, const char *path) {
    SDL_Surface *s = SDL_RenderReadPixels(renderer, nullptr);
    if (s) {
        SDL_SavePNG(s, path);
        SDL_DestroySurface(s);
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--play")) return play_headless();
        if (!strcmp(argv[i], "--prepare")) return prepare(argc, argv);
        if (!strcmp(argv[i], "--selftest") && i + 1 < argc) return selftest(argv[i + 1]);
    }
    std::string screenshot_path;
    int screenshot_tab = 0;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--screenshot=", 13)) screenshot_path = argv[i] + 13;
        if (!strncmp(argv[i], "--tab=", 6)) screenshot_tab = atoi(argv[i] + 6);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    App a;
    std::string err;
    if (!find_paths(a, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        SDL_Quit();
        return 1;
    }
    load_title(a);
    load_sizes(a);
    a.ini.path = a.ini_path;
    a.ini.load();
    init_binds(a);
    mods_load(a);
    load_choices(a);

    std::string caption = a.title + " - Launcher";
    SDL_Window *window = SDL_CreateWindow(caption.c_str(), MAIN_W, MAIN_H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN |
                                                                                    SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) { fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); return 1; }

    // Window icon: the project's own (assets/logo.png), not the game's art.
    {
        std::string proj_assets = a.exe_dir + "/../assets/logo.png";
        if (!file_exists(proj_assets)) proj_assets = a.exe_dir + "/assets/logo.png";
        SDL_Surface *icon = file_exists(proj_assets) ? SDL_LoadPNG(proj_assets.c_str()) : nullptr;
        if (icon) { SDL_SetWindowIcon(window, icon); SDL_DestroySurface(icon); }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // (no imgui.ini: the launcher keeps its own state in larecomp.ini / body.ini)
    // HiDPI (a 4K screen at 150 %): the window has more pixels than its logical size; the renderer draws in logical
    // units scaled up (imgui_impl_sdlrenderer3 takes the render scale), the fonts are rasterized at the real size
    float scale = SDL_GetWindowPixelDensity(window);
    if (scale < 1) scale = 1;
    SDL_SetRenderScale(renderer, scale, scale);
    // The font, resolved relative to the project root (dev checkout: two levels up from build/doax3launcher;
    // a release folder ships its own copy next to the launcher); the default ImGui font without it.
    {
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        std::string exe = n > 0 ? std::string(buf, n) : "";
        // the project's folders a font can be under: the dev checkout (build/..), a release (beside it), the data root
        std::vector<std::string> dirs;
        if (!exe.empty()) {
            std::string exedir = dirname_of(exe);
            dirs.push_back(dirname_of(exedir));
            dirs.push_back(exedir);
        }
        dirs.push_back(a.root);
        auto find_font = [&](const char *rel) {
            for (auto &d : dirs)
                if (file_exists(d + "/" + rel)) return d + "/" + rel;
            return std::string();
        };
        std::string body = find_font("third_party/fonts/DejaVu/DejaVuSans.ttf");
        std::string head = find_font("third_party/fonts/Baloo2/Baloo2-SemiBold.ttf");
        if (!body.empty()) io.Fonts->AddFontFromFileTTF(body.c_str(), 16 * scale);
        else io.Fonts->AddFontDefault();
        if (!head.empty()) g_title_font = io.Fonts->AddFontFromFileTTF(head.c_str(), 40 * scale);
    }
    io.FontGlobalScale = 1.0f / scale;  // (the font's loaded at scale px; ImGui itself isn't told the display is bigger)
    apply_theme();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Ui ui;
    ui.cover = load_texture_jpeg(renderer, a.root + "/extracted/control/icon_AmericanEnglish.dat");
    ui.logo = load_texture_png(renderer, a.root + "/build/launcher_art/logo.png");

    bool headless_screenshot = !screenshot_path.empty();
    a.tab = headless_screenshot ? screenshot_tab : TAB_GAME;
    int force_tab = headless_screenshot ? screenshot_tab : -1;
    if (!headless_screenshot) SDL_ShowWindow(window);

    bool running = true, quit_and_play = false;
    int frame_count = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) running = false;
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)ww, (float)wh));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
        ImGui::Begin("##main", nullptr,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        draw_sky_sea_background(dl, ImVec2(0, 0), (float)ww, (float)wh, MAIN_HEADER);
        render_header(a, ui, dl, ImVec2(0, 0), (float)ww);
        ImGui::SetCursorPosY(MAIN_HEADER + 4);
        float footer_h = 70;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("tabcontent", ImVec2(0, (float)wh - ImGui::GetCursorPosY() - footer_h), false);
        if (ImGui::BeginTabBar("tabs")) {
            ImGuiTabItemFlags f0 = force_tab == TAB_GAME ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags f1 = force_tab == TAB_CONTROLS ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags f2 = force_tab == TAB_MODS ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(k_tab_names[TAB_GAME], nullptr, f0)) { a.tab = TAB_GAME; build_game_tab(a); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(k_tab_names[TAB_CONTROLS], nullptr, f1)) { a.tab = TAB_CONTROLS; build_controls_tab(a, ui); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(k_tab_names[TAB_MODS], nullptr, f2)) { a.tab = TAB_MODS; build_mods_tab(a); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SetCursorPosY((float)wh - 68);
        ImGui::TextWrapped("%s", a.save_note.c_str());
        if (!a.mod_error.empty()) ImGui::TextColored(ImColor(0xFF, 0x60, 0x60).Value, "%s", a.mod_error.c_str());
        ImGui::SetCursorPosY((float)wh - 50);
        ImGui::SetCursorPosX((float)ww - 20 - 88 * 3 - 16);
        if (ImGui::Button("Save", ImVec2(88, 32))) {
            a.mod_error.clear();
            if (!save_choices(a)) a.mod_error = "Couldn't write " + a.ini_path + ".";
            else if (!mods_save(a, &a.mod_error)) {}
            else a.save_note = "Saved. Play starts the game with these.";
        }
        ImGui::SameLine(0, 8);
        if (ImGui::Button("Play", ImVec2(88, 32))) {
            a.mod_error.clear();
            if (!save_choices(a)) a.mod_error = "Couldn't write " + a.ini_path + ".";
            else if (!mods_save(a, &a.mod_error)) {}
            else {
                std::string lerr;
                if (!launch(a, &lerr, nullptr)) a.mod_error = lerr;
                else quit_and_play = true;
            }
        }
        ImGui::SameLine(0, 8);
        if (ImGui::Button("Quit", ImVec2(88, 32))) running = false;
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (headless_screenshot) {
            frame_count++;
            if (frame_count < 3) continue;  // (let the tab bar apply the forced selection before capturing)
            save_png_screenshot(renderer, screenshot_path.c_str());
            running = false;
        }
        if (quit_and_play) running = false;
    }

    if (ui.cover) SDL_DestroyTexture(ui.cover);
    if (ui.logo) SDL_DestroyTexture(ui.logo);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
