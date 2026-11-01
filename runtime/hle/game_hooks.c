// Game hooks: the places where the shared runtime (input, renderer, file system, frame pacing) asks the game-specific
// code something. In LAS Recomp these were Link's Awakening features (warp, mirror mode, widescreen camera, pots, pets,
// save editor, extra flags); for DOAX3 they answer "nothing special" until a DOAX3 counterpart is written.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../hle.h"
#include "../settings.h"

// ---- game state
int game_in_play(void) { return 0; }  // (not located yet: the menu's save editor and LED health stay off)
bool world_on_screen(void) { return false; }
const char *game_level_name(void) { return ""; }
uint64_t game_camera_zone(void) { return 0; }
int cheats_link_health(int *max) { if (max) *max = 0; return -1; }

// ---- the 3D scene's resolution (tools/patches.py): the game's render config has levels 1920x1080, 1600x900 and
// 1280x720 and renders the 3D scene at level 1, then scales it to the 1080p screen itself. This is level 1's size
// (orig: the game's, width | height << 32): the Video setting "render3d" (settings.h R3D_*).
// LARECOMP_3D_RES=<w>x<h> (test): any size.
uint64_t rt_render3d_size(uint64_t orig) {
    static const uint64_t sizes[] = {0, 1920 | 1080ull << 32, 2560 | 1440ull << 32, 3840 | 2160ull << 32};
    const char *e = getenv("LARECOMP_3D_RES");
    unsigned w = 0, h = 0;
    uint64_t size = g_settings.render3d > R3D_ORIGINAL && g_settings.render3d <= R3D_2160 ? sizes[g_settings.render3d] : orig;
    if (e && sscanf(e, "%ux%u", &w, &h) == 2 && w >= 320 && h >= 180 && w <= 7680 && h <= 4320)
        size = w | (uint64_t)h << 32;
    LOG("[game] 3D resolution %ux%u (the game's: %ux%u)\n", (unsigned)size, (unsigned)(size >> 32), (unsigned)orig,
        (unsigned)(orig >> 32));
    return size;
}

// ---- the frame rate (tools/patches.py FRAME_MODE_*): the engine's frame mode picks its present interval, logic steps
// per frame and time step together (mode 0: 60 fps, 1 step of 1/60 s; mode 1, the Switch game's: 30 fps, 2 steps;
// mode 2: 20 fps, 3). Its stores of the mode come here: mode 1 becomes 0 for 60 fps - the same game speed, twice the
// frames. 60 fps: the Video setting "fps60"; LARECOMP_FPS=30/60 (test) overrides it.
static bool g_frame_60;  // the 60 fps mode is on (rt_frame_mode turned the game's 30 into it)
uint32_t rt_frame_mode(uint32_t orig) {
    static int forced = -1;
    if (forced < 0) { const char *e = getenv("LARECOMP_FPS"); forced = e ? atoi(e) : 0; }
    bool fps60 = forced ? forced >= 60 : g_settings.fps60;
    uint32_t mode = fps60 && orig == 1 ? 0 : orig;
    static uint32_t last_orig = ~0u, last_mode = ~0u;
    if (orig != last_orig || mode != last_mode)
        LOG("[game] frame mode %u -> %u (%s)\n", orig, mode, mode == 0 ? "60 fps" : mode == 1 ? "30 fps" : "20 fps");
    last_orig = orig, last_mode = mode;
    g_frame_60 = mode != orig;
    return mode;
}

// The vsyncs the engine adds after frames it judged late (tools/patches.py FRAME_EXTRA). In the 60 fps mode they're
// dropped: its late-time accumulator counts any frame over 97.5% of its 1/59.94 s period as partly late, and our
// paced frames (exactly 1/60 s apart) built that up until it added two vsyncs every so often - 60 and 20 fps
// alternating, ~43 fps on average, with a 230+ fps machine. The runtime's present pacing keeps the time instead
// (runtime/nvn/objects.c present_pace).
uint32_t rt_frame_extra(uint32_t orig) {
    if (!g_frame_60 || !orig) return orig;
    static uint64_t dropped;
    if ((++dropped & (dropped - 1)) == 0) LOG("[game] 60 fps: extra vsyncs dropped %llu times\n", (unsigned long long)dropped);
    return 0;
}

// ---- version, for the shader caches' tag (runtime/nvn/shader_prep.c)
const char *larecomp_version(void) { return "doax3-dev"; }

// ---- input (runtime/hle/input.c)
bool mirror_input(void) { return false; }
uint64_t pets_menu_input(uint64_t b, int32_t st[4]) { (void)st; return b; }
uint64_t tracker_map_input(uint64_t b, int32_t st[4]) { (void)st; return b; }
void input_note_rstick(int32_t ry) { (void)ry; }
int32_t g_stick_lx, g_stick_ly;
uint64_t g_stick_freeze_until;
int32_t g_test_lx, g_test_ly;
uint64_t g_test_stick_until;
uint64_t g_test_buttons, g_test_buttons_until;
char g_window_shot_request[256];

// ---- file system (runtime/services/fs.c)
void romfs_file_read_hook(const char *path) { (void)path; }
void save_editor_file_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
void flags_ext_save_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
bool flags_ext_save_written(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
    return false;
}
void save_editor_frame(uint64_t frame) { (void)frame; }

// ---- renderer (runtime/nvn/vulkan): per-draw tints and the widescreen camera
void escape_bottle_tint(int kind, const float *ui, uint32_t ui_size, float tint[8]) {
    (void)kind, (void)ui, (void)ui_size, (void)tint;
}
bool hint_sheet_icon_draw(const float *ui, uint32_t ui_size) { (void)ui, (void)ui_size; return false; }
bool mirror_draw(bool composite) { (void)composite; return false; }
bool mirror_scene_size(uint32_t sw, uint32_t sh, uint32_t w, uint32_t h) { (void)sw, (void)sh, (void)w, (void)h; return false; }
void scene_size_log(bool composite, uint32_t sw, uint32_t sh, uint32_t w, uint32_t h) {
    (void)composite, (void)sw, (void)sh, (void)w, (void)h;
}
bool pets_hide_item_cursor(void) { return false; }
bool pot_glow_active(void) { return false; }
bool pot_level_active(void) { return false; }
void pot_gold_tint(float tint[8]) { (void)tint; }
bool pot_instance_gold(const float *m) { (void)m; return false; }
bool pot_tint(const float *m, float tint[8], uint64_t mesh) { (void)m, (void)tint, (void)mesh; return false; }
bool widescreen_allowed(void) { return false; }
void widescreen_backdrop_seen(uint64_t hash) { (void)hash; }
float widescreen_effect_stretch(uint64_t fs_hash) { (void)fs_hash; return 1.0f; }
void widescreen_frame_end(uint64_t frame) { (void)frame; }
void widescreen_present_crop(uint64_t frame, double shape, float crop[4], double *vx, double *vy, double *vw, double *vh) {
    (void)frame, (void)shape, (void)vx, (void)vy, (void)vw, (void)vh;
    crop[0] = crop[1] = 0, crop[2] = crop[3] = 1;  // the whole frame, the viewport as it is
}
void widescreen_scene_ubo(const float *u, uint64_t size) { (void)u, (void)size; }
void widescreen_set_factor(float f) { (void)f; }
bool widescreen_ui_pane_partial(const float *u, uint64_t size) { (void)u, (void)size; return false; }
