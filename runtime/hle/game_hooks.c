// Game hooks: the places where the shared runtime (input, renderer, file system, frame pacing) asks the game-specific
// code something. In LAS Recomp these were Link's Awakening features (warp, mirror mode, widescreen camera, pots, pets,
// save editor, extra flags); for DOAX3 they answer "nothing special" until a DOAX3 counterpart is written.
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// frames. 60 fps: the Video setting "fps60"; LARECOMP_FPS=30/60/120 (test) overrides it.
// 120 fps ("fps120" as well): the engine has no such mode, so it runs in mode 0 and every frame is half of one of its
// 60 fps frames: its step count, time step and float step are halved where the game reads them (rt_steps_*,
// rt_time_step, rt_game_step; tools/patches.py STEPS_FN) and frames are paced at 120 Hz (runtime/nvn/objects.c).
static bool g_frame_60;   // the 60 fps mode is on (rt_frame_mode turned the game's 30 into it)
static bool g_frame_120;  // and each of its frames is half a 60 fps frame
uint32_t rt_frame_mode(uint32_t orig) {
    static int forced = -1;
    if (forced < 0) { const char *e = getenv("LARECOMP_FPS"); forced = e ? atoi(e) : 0; }
    bool fps60 = forced ? forced >= 60 : g_settings.fps60;
    bool fps120 = forced ? forced >= 120 : g_settings.fps60 && g_settings.fps120;
    uint32_t mode = fps60 && orig == 1 ? 0 : orig;
    g_frame_60 = mode != orig;
    g_frame_120 = g_frame_60 && fps120;
    static uint32_t last_orig = ~0u, last_mode = ~0u;
    if (orig != last_orig || mode != last_mode)
        LOG("[game] frame mode %u -> %u (%s)\n", orig, mode,
            g_frame_120 ? "120 fps" : mode == 0 ? "60 fps" : mode == 1 ? "30 fps" : "20 fps");
    last_orig = orig, last_mode = mode;
    return mode;
}

// The frame rate frames are paced at (runtime/nvn/objects.c present_pace): 120 in the 120 fps mode, else the game's
// own (its present interval: 60 Hz vsyncs).
int rt_frame_rate(void) { return g_frame_120 ? 120 : 60; }

// Frames presented so far (runtime/nvn/objects.c): which half of a 60 fps frame this is.
extern _Atomic uint64_t g_frames_presented;

// The engine's logic steps per frame where the game uses them (tools/patches.py STEPS_FN); n is the mode's own (1 in
// mode 0). As a float: half of it at 120 fps.
static void steps_seen(int kind) {  // (the log says which uses ran at 120 fps, once each)
    static const char *const names[] = {"as a float", "dividing ticks into frames", "on a tick counter"};
    static bool seen[3];
    if (!seen[kind]) seen[kind] = true, LOG("[game] 120 fps: logic steps per frame used %s\n", names[kind]);
}
float rt_steps_float(uint32_t n) {
    if (!g_frame_120) return (float)n;
    steps_seen(0);
    return (float)n * 0.5f;
}

// A time in 1/60 s ticks divided by it, into frames the game then counts down one a frame: twice the frames at 120.
// (By 0: 0, as the Switch's udiv.)
uint32_t rt_steps_div(uint32_t ticks, uint32_t n) {
    if (!n) return 0;
    if (!g_frame_120) return ticks / n;
    steps_seen(1);
    return (uint32_t)((uint64_t)ticks * 2 / n);
}

// Added to or taken from a tick counter: at 120 fps on every other frame only, so it keeps the 60 ticks a second
// (the same for every counter within a frame).
uint32_t rt_steps_count(uint32_t n) {
    if (!g_frame_120) return n;
    steps_seen(2);
    return (g_frames_presented & 1) ? n : 0;
}

// The time step (seconds per frame, 1/60 in mode 0): halved at 120 fps.
float rt_time_step(float t) {
    if (!g_frame_120) return t;
    static bool logged;
    if (!logged) logged = true, LOG("[game] 120 fps: the time step %g -> %g\n", t, t * 0.5f);
    return t * 0.5f;
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

// A worker's state at the main thread's kick (tools/patches.py WORKER_KICK): a kick that finds the worker in state 2 (its
// job just done, not yet back at its wait) would be skipped and both threads would sleep for ever. It waits the state out;
// the worker is only a few instructions from state 3, so a limit of 1 s is never reached unless something else is wrong.
uint32_t rt_worker_state(uint64_t addr, uint32_t wanted) {
    volatile uint32_t *state = (volatile uint32_t *)(uintptr_t)addr;
    uint32_t v = *state;
    if (v != 2 || wanted != 1) return v;
    static uint64_t waits;
    if ((++waits & (waits - 1)) == 0) LOG("[game] worker handshake: waited out its state 2, %llu times\n", (unsigned long long)waits);
    uint64_t end = plat_now_ns() + 1000000000ull;
    while ((v = *state) == 2 && plat_now_ns() < end) plat_sleep_until_ns(plat_now_ns() + 50000);
    return v;
}

// The game's step (tools/patches.py GAME_STEP): the float its main update hands every system, in 30 fps frames (1.0 a
// frame), at `ptr`; the update reads its address here once a frame, before any system runs. In the 60 fps mode it is
// halved (a quarter at 120 fps), so animation, cameras, cutscenes and gameplay keep their speed. A value the game
// stores itself (it's 1.0 for the whole game so far; a slow motion would change it) is scaled the next frame: what's
// there and isn't what we stored is the game's.
uint64_t rt_game_step(uint64_t ptr) {
    static uint32_t stored;  // (the bits we last wrote; 0: none)
    float *step = (float *)(uintptr_t)ptr;
    uint32_t bits;
    memcpy(&bits, step, 4);
    if (!g_frame_60 || bits == stored) return ptr;
    float scaled = *step * (g_frame_120 ? 0.25f : 0.5f);
    memcpy(&stored, &scaled, 4);
    if (!stored) stored = 1;  // (0.0 stays 0.0, but must not read as "nothing stored")
    LOG("[game] %d fps: the game's step %g -> %g\n", g_frame_120 ? 120 : 60, *step, scaled);
    *step = scaled;
    return ptr;
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
