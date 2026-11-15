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
static bool g_in_logic;   // (rt_game_step: inside the logic updates, run in the 30 fps mode)
// LARECOMP_FPS_FROM=<frame> (test, with LARECOMP_FPS=60 / 120): 30 fps until that presented frame, then the
// LARECOMP_FPS rate (rt_frame_switch) - a route through the menus the same at any rate, then the scene at the rate tested.
static int g_forced_fps = -1;
static uint64_t g_fps_from;
static bool g_fps_switched;
uint32_t rt_frame_mode(uint32_t orig) {
    if (g_forced_fps < 0) {
        const char *e = getenv("LARECOMP_FPS"), *f = getenv("LARECOMP_FPS_FROM");
        g_forced_fps = e ? atoi(e) : 0;
        g_fps_from = f && g_forced_fps >= 60 ? strtoull(f, NULL, 0) : 0;
    }
    int forced = g_fps_from && !g_fps_switched ? 30 : g_forced_fps;
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

// The game updates this frame (tools/patches.py UPDATES_FN): the mode's base (1) plus the engine's catch-up after a
// slow frame. At 120 fps the engine's catch-up (in 60 Hz vsyncs) never fires, so the runtime's is used instead
// (runtime/nvn/objects.c catchup_frame: the time the frames took beyond 1/120 s).
static _Atomic int g_logic_updates = 1;  // this frame's logic updates (rt_logic_tick)
bool rt_logic30(void);
uint32_t rt_frame_updates(uint32_t base, uint32_t engine_extra) {
    if (rt_logic30()) return (uint32_t)atomic_load(&g_logic_updates);
    if (!g_frame_60) return base + engine_extra;
    if (engine_extra) {  // (the engine's own, measured in 60 Hz vsyncs against our pacing: not used at 60 / 120)
        static uint64_t n, logged = 1;
        if (++n >= logged) LOG("[game] %d fps: the engine asked for catch-up updates %llu times (ignored)\n",
                               g_frame_120 ? 120 : 60, (unsigned long long)n), logged *= 2;
    }
    extern _Atomic int g_catchup_updates;  // (set once a frame, at the present: the same for each call this frame)
    return base + (uint32_t)atomic_load(&g_catchup_updates);
}
bool rt_frame_fast(void) { return g_frame_60; }  // (60 or 120 fps: runtime/nvn/objects.c counts the catch-up)

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
    if (g_in_logic) return (float)n;
    if (!g_frame_120) return (float)n;
    steps_seen(0);
    return (float)n * 0.5f;
}

// A time in 1/60 s ticks divided by it, into frames the game then counts down one a frame: twice the frames at 120.
// (By 0: 0, as the Switch's udiv.)
uint32_t rt_steps_div(uint32_t ticks, uint32_t n) {
    if (!n) return 0;
    if (g_in_logic) return ticks / n;
    if (!g_frame_120) return ticks / n;
    steps_seen(1);
    return (uint32_t)((uint64_t)ticks * 2 / n);
}

// Added to or taken from a tick counter: at 120 fps on every other frame only, so it keeps the 60 ticks a second
// (the same for every counter within a frame).
uint32_t rt_steps_count(uint32_t n) {
    if (g_in_logic) return n;
    if (!g_frame_120) return n;
    steps_seen(2);
    return (g_frames_presented & 1) ? n : 0;
}

// The time step (seconds per frame, 1/60 in mode 0): halved at 120 fps.
float rt_time_step(float t) {
    if (g_in_logic) return t;
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
//
// Logic at 30 Hz (LARECOMP_LOGIC30=1, test): at 60 / 120 fps the logic updates (the main update's loop) run as on the
// Switch instead - in the 30 fps mode, with the 30 fps step, on one frame in 2 (in 4) - and only the per-frame part
// runs at the display's rate (rt_logic_end, rt_logic_step; runtime/nvn/objects.c counts the updates, rt_logic_tick).
// records: the engine's frame records (the current one's index at +4, 16 bytes each, the mode at +0xc).
static float *g_step_ptr;
static float g_step_game = 1.0f;  // the game's own step (what it stored)
static uint32_t g_step_written;   // the bits we last stored there
static bool g_step_has_written;
static uint32_t *g_logic_mode;    // the frame record whose mode the logic updates set to 30 fps, and what it was
static uint32_t g_logic_mode_was;
static int g_logic30 = -1;

bool rt_logic30(void) {
    if (g_logic30 < 0) {
        const char *e = getenv("LARECOMP_LOGIC30");
        g_logic30 = e && atoi(e) != 0;
    }
    return g_logic30 && g_frame_60;
}

static void step_store(float v) {
    *g_step_ptr = v;
    memcpy(&g_step_written, &v, 4);
    g_step_has_written = true;
}

static void step_take_game(void) {  // (a value we didn't write there is the game's own)
    uint32_t bits;
    memcpy(&bits, g_step_ptr, 4);
    if (!g_step_has_written || bits != g_step_written) {
        if (g_step_has_written) LOG("[game] the game's step is now %g\n", *g_step_ptr);
        g_step_game = *g_step_ptr;
    }
}

static uint64_t g_records;
uint64_t rt_game_step(uint64_t ptr, uint64_t records) {
    g_records = records;
    if (!g_frame_60) return ptr;
    g_step_ptr = (float *)(uintptr_t)ptr;
    step_take_game();
    if (rt_logic30()) {  // the logic updates begin: as on the Switch
        uint32_t idx = *(volatile uint32_t *)(uintptr_t)(records + 4);
        g_logic_mode = (uint32_t *)(uintptr_t)(records + (uint64_t)(idx & 3) * 16 + 0xc);
        g_logic_mode_was = *g_logic_mode;
        *g_logic_mode = 1;
        g_in_logic = true;
        step_store(g_step_game);
        static bool logged;
        if (!logged) logged = true, LOG("[game] %d fps with the logic at 30 Hz (the step %g)\n", g_frame_120 ? 120 : 60, g_step_game);
        return ptr;
    }
    float scaled = g_step_game * (g_frame_120 ? 0.25f : 0.5f);
    uint32_t bits;
    memcpy(&bits, &scaled, 4);
    if (!g_step_has_written || bits != g_step_written)
        LOG("[game] %d fps: the game's step %g -> %g\n", g_frame_120 ? 120 : 60, g_step_game, scaled);
    step_store(scaled);
    return ptr;
}

// After the logic updates (every frame, with or without them): the per-frame part's step, the display's.
float rt_logic_end(float t) {
    if (!g_step_ptr || !rt_logic30()) return t;
    if (g_in_logic) {
        g_in_logic = false;
        *g_logic_mode = g_logic_mode_was;
        step_take_game();
    }
    float scaled = g_step_game * (g_frame_120 ? 0.25f : 0.5f);
    step_store(scaled);
    return scaled;
}

// The step the per-frame part multiplies by the frame's update count: one logic update's.
float rt_logic_step(float t) { return rt_logic30() ? g_step_game : t; }

// At each present (runtime/nvn/objects.c): the next frame's logic updates, for the time since the last present -
// one per logic period (1/30 s: present periods x 2 at 60 fps, x 4 at 120), a slow frame made up for (at most 4
// periods owed). Half a present period of slack keeps the pattern steady (1, 0, 1, 0) against pacing jitter.
static void rt_frame_switch(void) {  // (LARECOMP_FPS_FROM: the switch to the tested rate, once)
    extern _Atomic uint64_t g_frames_presented;
    if (!g_fps_from || g_fps_switched || !g_records || atomic_load(&g_frames_presented) < g_fps_from) return;
    g_fps_switched = true;
    for (int i = 0; i < 4; i++) {
        uint32_t *mode = (uint32_t *)(uintptr_t)(g_records + (uint64_t)i * 16 + 0xc);
        *mode = rt_frame_mode(*mode);
    }
    LOG("[game] LARECOMP_FPS_FROM: %d fps from frame %llu\n", g_forced_fps, (unsigned long long)g_fps_from);
}

void rt_logic_tick(uint64_t now, uint64_t period) {
    rt_frame_switch();
    if (!g_frame_60) return;
    static uint64_t last;
    static int64_t owed;
    int64_t L = (int64_t)period * (g_frame_120 ? 4 : 2);
    int n = 1;
    if (last) {
        owed += (int64_t)(now - last);
        if (owed > 4 * L) owed = 4 * L;
        n = (int)((owed + (int64_t)period / 2) / L);
        owed -= (int64_t)n * L;
        if (owed < -L) owed = -L;
    } else
        owed = -(int64_t)period / 2;
    last = now;
    atomic_store(&g_logic_updates, n);
    static uint64_t frames, extra, logged = 1;
    frames++;
    if (n > 1 && (extra += (uint64_t)(n - 1)) >= logged) {
        LOG("[game] 30 Hz updates (fibers): %llu catch-up updates so far (%llu frames)\n", (unsigned long long)extra,
            (unsigned long long)frames);
        logged *= 2;
    }
}

// The controllers' button histories (tools/patches.py HISTORY_TICK): what to add to a button state's frame count this
// update (cur: the count so far, key: which history and player). The game counts 30 fps frames: at 60 / 120 fps a
// state's first update counts 1, as on the Switch, then 1 every 2nd / 4th update of that history and player (each
// update is half / a quarter of a Switch one), so "pressed k frames ago" and "held for k frames" mean the same time at
// every rate.
uint32_t rt_history_tick(uint32_t cur, uint32_t key) {
    if (!g_frame_60 || g_in_logic) return 1;  // (logic at 30 Hz: each update is a Switch one)
    static uint32_t ticks[32];
    uint32_t p = key & 31;
    static bool logged;
    if (!logged) logged = true, LOG("[game] %d fps: button history counted in 30 fps frames\n", g_frame_120 ? 120 : 60);
    uint32_t k = g_frame_120 ? 4 : 2, t = ++ticks[p];
    if (!cur) { ticks[p] = 0; return 1; }  // (a new state: its first update, then a full 30 fps frame each k)
    return t % k == 0 ? 1 : 0;
}

// A system the engine steps in 60 Hz ticks once a frame (tools/patches.py TICKS60; n: the mode's count): at 120 fps
// one tick every other frame, 60 a second as at every other rate.
uint32_t rt_ticks60(uint32_t n) {
    if (!g_frame_120) return n;
    extern _Atomic uint64_t g_frames_presented;
    static bool logged;
    if (!logged) logged = true, LOG("[game] 120 fps: the 60 Hz system ticks every other frame\n");
    return (atomic_load(&g_frames_presented) & 1) ? n : 0;
}

// ---- the game's tasks (tools/patches.py TASK_UPDATE), debug: LARECOMP_LOG_TASKS=1 logs each kind of task the first
// time it runs (its key and id fields); LARECOMP_TASK30=all | <key>,<key>,... | hash:<mask>:<value> runs those tasks at
// the Switch's 30 Hz under 60 / 120 fps (on the frames with a logic update, rt_logic_tick, in the 30 fps mode with the
// 30 fps step) while the rest run every frame - to find which task's logic changes at 60 fps.
static uint32_t task_key(uint64_t t) {
    uint32_t id0 = *(uint32_t *)(uintptr_t)t, a8 = *(uint32_t *)(uintptr_t)(t + 0xa8);
    uint16_t id1 = *(uint16_t *)(uintptr_t)(t + 6);
    return id0 * 31u + id1 * 7u + a8;
}
static uint32_t key_mix(uint32_t k) { k ^= k >> 16; k *= 0x7feb352du; k ^= k >> 15; k *= 0x846ca68bu; return k ^ (k >> 16); }

static bool task30(uint64_t t) {
    static int mode = -1;  // 0 none, 1 all, 2 list, 3 hash
    static uint32_t keys[64], nkeys, mask, value;
    if (mode < 0) {
        const char *e = getenv("LARECOMP_TASK30");
        mode = 0;
        if (e && !strcmp(e, "all")) mode = 1;
        else if (e && sscanf(e, "hash:%x:%x", &mask, &value) == 2) mode = 3;
        else if (e && *e) {
            mode = 2;
            for (const char *p = e; *p && nkeys < 64;) {
                keys[nkeys++] = (uint32_t)strtoul(p, (char **)&p, 16);
                while (*p == ',' || *p == ' ') p++;
            }
        }
        if (mode) LOG("[game] tasks at 30 Hz: %s\n", e);
    }
    if (!mode) return false;
    if (mode == 1) return true;
    uint32_t k = task_key(t);
    if (mode == 3) return (key_mix(k) & mask) == value;
    for (uint32_t i = 0; i < nkeys; i++)
        if (keys[i] == k) return true;
    return false;
}

static void task_log(uint64_t t) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("LARECOMP_LOG_TASKS"); on = e && atoi(e); }
    if (!on) return;
    static uint32_t seen[4096];
    uint32_t k = task_key(t), h = key_mix(k) & 4095;
    for (int n = 0; n < 4096; n++, h = (h + 1) & 4095) {
        if (seen[h] == k + 1) return;
        if (!seen[h]) break;
    }
    seen[h] = k + 1;
    extern _Atomic uint64_t g_frames_presented;
    LOG("[task] key %08x (hash %08x) id %08x/%04x a4 %08x a8 %08x def %llx at frame %llu\n", k, key_mix(k),
        *(uint32_t *)(uintptr_t)t, *(uint16_t *)(uintptr_t)(t + 6), *(uint32_t *)(uintptr_t)(t + 0xa4),
        *(uint32_t *)(uintptr_t)(t + 0xa8), (unsigned long long)*(uint64_t *)(uintptr_t)(t + 0x98),
        (unsigned long long)g_frames_presented);
}

static uint32_t g_task_step_bits, g_task_mode_was;
static uint32_t *g_task_mode;
int rt_task_begin(uint64_t t) {
    task_log(t);
    if (!g_frame_60 || g_in_logic || !g_step_ptr || !g_records || !task30(t)) return 0;
    extern _Atomic uint64_t g_frames_presented;
    static struct { uint64_t task, frame; } last[4096];
    uint64_t frame = atomic_load(&g_frames_presented) + 1;
    uint32_t h = (uint32_t)((t >> 4) * 0x9E3779B1u) & 4095;
    for (int n = 0; n < 4096 && last[h].task && last[h].task != t; n++) h = (h + 1) & 4095;
    if (last[h].task == t && last[h].frame == frame) return -1;  // (its updates for this frame are done)
    last[h].task = t, last[h].frame = frame;
    int n = atomic_load(&g_logic_updates);
    if (n <= 0) return -1;
    memcpy(&g_task_step_bits, g_step_ptr, 4);
    *g_step_ptr = g_step_game;
    uint32_t idx = *(volatile uint32_t *)(uintptr_t)(g_records + 4);
    g_task_mode = (uint32_t *)(uintptr_t)(g_records + (uint64_t)(idx & 3) * 16 + 0xc);
    g_task_mode_was = *g_task_mode;
    *g_task_mode = 1;
    g_in_logic = true;
    return n;
}

void rt_task_end(int k) {
    if (k <= 0) return;
    *g_task_mode = g_task_mode_was;
    memcpy(g_step_ptr, &g_task_step_bits, 4);
    g_in_logic = false;
}

// The logic update's four calls (tools/patches.py LOGIC_CALLS), debug: LARECOMP_SUB30=<mask> runs call n (bit n) at
// the Switch's 30 Hz under 60 / 120 fps - on the frame's first logic update, as many times as rt_logic_tick counts,
// in the 30 fps mode with the 30 fps step (call 3, the task list, also gets it in its step argument, x0) - and skips it
// on the other updates; the rest run every update. Calls: 0 the fiber scheduler (0x700066c250), 1 and 2 two systems
// without arguments, 3 the task list. (Bisecting with it found the fibers, rt_fiber_waiting below.)
static uint32_t g_sub_step_bits, g_sub_arg_bits, g_sub_mode_was;
static uint32_t *g_sub_mode;
int rt_sub_begin(int n, uint64_t x0) {
    static int mask = -1;
    if (mask < 0) {
        const char *e = getenv("LARECOMP_SUB30");
        mask = e ? (int)strtol(e, NULL, 0) : 0;
        if (mask) LOG("[game] logic update calls at 30 Hz: mask %#x\n", mask);
    }
    if (!(mask & (1 << n)) || !g_frame_60 || g_in_logic || !g_step_ptr || !g_records) return 0;
    extern _Atomic uint64_t g_frames_presented;
    static uint64_t last[8];
    uint64_t frame = atomic_load(&g_frames_presented) + 1;
    if (last[n] == frame) return -1;  // (this frame's updates of it are done)
    last[n] = frame;
    int k = atomic_load(&g_logic_updates);
    if (k <= 0) return -1;
    memcpy(&g_sub_step_bits, g_step_ptr, 4);
    *g_step_ptr = g_step_game;
    if (n == 3) memcpy(&g_sub_arg_bits, (void *)(uintptr_t)x0, 4), memcpy((void *)(uintptr_t)x0, &g_step_game, 4);
    uint32_t idx = *(volatile uint32_t *)(uintptr_t)(g_records + 4);
    g_sub_mode = (uint32_t *)(uintptr_t)(g_records + (uint64_t)(idx & 3) * 16 + 0xc);
    g_sub_mode_was = *g_sub_mode;
    *g_sub_mode = 1;
    g_in_logic = true;
    return k;
}

void rt_sub_end(int n, int k, uint64_t x0) {
    if (k <= 0) return;
    *g_sub_mode = g_sub_mode_was;
    memcpy(g_step_ptr, &g_sub_step_bits, 4);
    if (n == 3) memcpy((void *)(uintptr_t)x0, &g_sub_arg_bits, 4);
    g_in_logic = false;
}

// The fibers (tools/patches.py FIBER_RUN): the game's scripts, the match's states and the girls' AI wait in 30 fps
// frames (Fiber::WaitFrames(n): n updates) and some count their own iterations, so at 60 / 120 fps they ran 2 / 4
// times as fast: Beach Volleyball's AI reacted too soon and the girls missed (a match from a fresh save: 2-7 in ~45 s at
// 60 fps against ~120 s at 30). Under 60 / 120 fps a fiber at 30 Hz is counted down and run only on the updates of a
// 30 fps frame (rt_logic_tick: one frame in 2 / 4, more after a slow frame), in the 30 fps mode with the 30 fps step -
// as on the Switch. All of them but the camera's (fiber 0x13: it updates the view every frame, smoothly at any rate).
// LARECOMP_FIBERS30=<mask> (test): which fibers (bit = id), 0 for none.
static bool fiber30(uint32_t slot) {
    static int64_t mask = -1;
    if (mask < 0) {
        const char *e = getenv("LARECOMP_FIBERS30");
        mask = e ? (int64_t)strtoll(e, NULL, 0) : (int64_t)(((1ull << 27) - 1) & ~(1ull << 0x13));
        LOG("[game] fibers at 30 Hz under 60 / 120 fps: %#llx\n", (unsigned long long)mask);
    }
    return slot < 27 && (mask >> slot & 1);
}

// The controller's press edges (pad: the pad globals, from tools/patches.py PAD_CLEAR: just pressed at +0x48, just
// released +0x4c, auto-repeated +0x50) are there for one update. The fibers at 30 Hz would miss those of the frames
// they don't run on (a tap on the character select did nothing at 60 fps half the time): each frame's edges are
// gathered, and while a fiber at 30 Hz runs the globals hold all of them since the fibers last ran.
static uint64_t g_pad_got, g_pad;
static uint32_t g_edges[3], g_edges_real[3];
static bool g_edges_used;  // (the fibers at 30 Hz got them: the next frame starts over)

static void edges_gather(uint64_t frame) {
    static uint64_t last;
    if (frame == last) return;
    last = frame;
    if (!g_pad && g_pad_got) g_pad = *(volatile uint64_t *)(uintptr_t)g_pad_got;
    if (!g_pad) return;
    if (g_edges_used) g_edges_used = false, memset(g_edges, 0, sizeof g_edges);
    for (int i = 0; i < 3; i++) g_edges[i] |= *(volatile uint32_t *)(uintptr_t)(g_pad + 0x48 + 4 * i);
}

uint32_t rt_fiber_waiting(uint32_t slot, uint32_t state, uint64_t pad_got) {
    if (state != 1 || !g_frame_60 || g_in_logic) return state;
    extern _Atomic uint64_t g_frames_presented;
    uint64_t frame = atomic_load(&g_frames_presented) + 1;
    g_pad_got = pad_got;
    edges_gather(frame);
    if (!fiber30(slot)) return state;
    static uint64_t frame_of[27];
    static int used[27];
    if (frame_of[slot] != frame) frame_of[slot] = frame, used[slot] = 0;
    if (used[slot] >= atomic_load(&g_logic_updates)) return 0;  // (not this update: no count down, no run)
    used[slot]++;
    return 1;
}

static uint32_t g_fiber_step_bits, g_fiber_mode_was;
static uint32_t *g_fiber_mode;
int rt_fiber_begin(uint32_t slot) {
    if (!g_frame_60 || g_in_logic || !g_step_ptr || !g_records || !fiber30(slot)) return 0;
    memcpy(&g_fiber_step_bits, g_step_ptr, 4);
    *g_step_ptr = g_step_game;
    uint32_t idx = *(volatile uint32_t *)(uintptr_t)(g_records + 4);
    g_fiber_mode = (uint32_t *)(uintptr_t)(g_records + (uint64_t)(idx & 3) * 16 + 0xc);
    g_fiber_mode_was = *g_fiber_mode;
    *g_fiber_mode = 1;
    g_in_logic = true;
    if (g_pad)  // (every press edge since the fibers last ran)
        for (int i = 0; i < 3; i++) {
            volatile uint32_t *e = (volatile uint32_t *)(uintptr_t)(g_pad + 0x48 + 4 * i);
            g_edges_real[i] = *e, *e = g_edges[i];
        }
    g_edges_used = true;
    return 1;
}

void rt_fiber_end(int k) {
    if (!k) return;
    *g_fiber_mode = g_fiber_mode_was;
    memcpy(g_step_ptr, &g_fiber_step_bits, 4);
    if (g_pad)
        for (int i = 0; i < 3; i++) *(volatile uint32_t *)(uintptr_t)(g_pad + 0x48 + 4 * i) = g_edges_real[i];
    g_in_logic = false;
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
