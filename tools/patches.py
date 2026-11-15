"""Hand-written replacements for single instructions of the game's main program (tools/gen.py PATCHES).

Sites are found by their instruction pattern, not by address, so the same patch applies to every version that has the
code (1.0.0 and 1.0.6). find(module) returns {guest address: function(module, address) -> one C statement}.

The render config default (the engine fills it before creating its render targets): three resolution levels, 1920x1080,
1600x900 and 1280x720, and the level the 3D scene renders at, level 1 (1600x900; the game upscales it to the 1080p
screen itself). The store of levels 0 and 1 is replaced by one that asks the runtime for level 1's size
(rt_render3d_size, runtime/hle/game_hooks.c), so the 3D resolution is a setting.
"""
import struct
import sys

RENDER_CONFIG = (
    0xB2190FE8,  # mov  x8, #0x78000000780      (orr: 1920 | 1920 << 32; its movk makes it 1920 | 1080 << 32)
    0xD280C809,  # mov  x9, #0x640              (1600)
    0xF2C08708,  # movk x8, #0x438, lsl #32     (1080)
    0xF2C07089,  # movk x9, #0x384, lsl #32     (900)
    0xA9002408,  # stp  x8, x9, [x0]            (levels 0 and 1)
)


def render_config_store(module, a):
    return ('{ extern uint64_t rt_render3d_size(uint64_t); '
            '*(uint64_t *)(uintptr_t)c->x[0] = c->x[8]; '
            '*(uint64_t *)(uintptr_t)(c->x[0] + 8) = rt_render3d_size(c->x[9]); }'
            f'  /* {a:#x}: render config levels 0 and 1 (tools/patches.py) */')


# The frame mode (the engine's frame controller; its ring of 4 frame records at 0x7000d6dec0 in 1.0.6, the mode at
# +0xc of each). A mode indexes the engine's per-mode tables: present interval {1, 2, 3}, logic steps per frame
# {1, 2, 3} (0x700038f640, read all over the game code), time step {1/60, 1/30, 1/20 s} (0x700038f690): mode 0 is
# 60 fps, mode 1 (the Switch game's) 30, mode 2 20. Each frame's record copies the last one's mode, so the mode is
# what the init (4 records) and the one setter (called with 1 at boot) store. Those stores ask the runtime
# (rt_frame_mode, runtime/hle/game_hooks.c), which turns 1 into 0 for 60 fps: every table then agrees, one logic step
# of 1/60 s a frame - the same game speed, twice the frames. (Changing only the interval doubled the game's speed: it
# still stepped twice a frame.)
FRAME_MODE_INIT = (  # (word, mask); the 4 records' mode stores are at 4, 7, 10 and 13
    (0xB26003EA, 0xFFFFFFFF),  # mov  x10, #0x100000000
    (0x320003E9, 0xFFFFFFFF),  # mov  w9, #1
    (0xF800410A, 0xFFFFFFFF),  # stur x10, [x8, #4]
    (0x9000000A, 0x9F00001F),  # adrp x10, <page>
    (0xB9000D09, 0xFFFFFFFF),  # str  w9, [x8, #0xc]      (record 0's mode)
    (0x39004109, 0xFFFFFFFF),  # strb w9, [x8, #0x10]
    (0xF801411F, 0xFFFFFFFF),  # stur xzr, [x8, #0x14]
    (0xB9001D09, 0xFFFFFFFF),  # str  w9, [x8, #0x1c]     (record 1's)
    (0x39008109, 0xFFFFFFFF),  # strb w9, [x8, #0x20]
    (0xF802411F, 0xFFFFFFFF),  # stur xzr, [x8, #0x24]
    (0xB9002D09, 0xFFFFFFFF),  # str  w9, [x8, #0x2c]     (record 2's)
    (0x3900C109, 0xFFFFFFFF),  # strb w9, [x8, #0x30]
    (0xF803411F, 0xFFFFFFFF),  # stur xzr, [x8, #0x34]
    (0xB9003D09, 0xFFFFFFFF),  # str  w9, [x8, #0x3c]     (record 3's)
)
FRAME_MODE_SET = (
    0xB9400509,  # ldr  w9, [x8, #4]          (the current record)
    0x8B091108,  # add  x8, x8, x9, lsl #4
    0xB9000D00,  # str  w0, [x8, #0xc]        (its mode)
    0xD65F03C0,  # ret
)


def frame_mode_store(reg, off):
    def emit(module, a):
        return ('{ extern uint32_t rt_frame_mode(uint32_t); '
                f'*(uint32_t *)(uintptr_t)(c->x[8] + {off:#x}) = rt_frame_mode((uint32_t)c->x[{reg}]); }}'
                f'  /* {a:#x}: the frame mode (tools/patches.py) */')
    return emit


# The vsyncs the engine adds to the base after frames it judged late (its late-time accumulator, 1.0.6 0x70003b9d60):
# replaced by a load that lets the runtime drop them in 60 fps mode (rt_frame_extra), where our own pacing keeps time.
FRAME_EXTRA = (
    0xB9401A88,  # ldr  w8, [x20, #0x18]     (the extra vsyncs)
    0x2A0003F3,  # mov  w19, w0              (the base)
    0x0B130114,  # add  w20, w8, w19         (the present interval)
    0x2A1403E0,  # mov  w0, w20
)


def frame_extra_load(module, a):
    return ('{ extern uint32_t rt_frame_extra(uint32_t); '
            'c->x[8] = rt_frame_extra(*(const uint32_t *)(uintptr_t)(c->x[20] + 0x18)); }'
            f'  /* {a:#x}: the frame\'s extra vsyncs (tools/patches.py) */')


# The game's step: a float the main update (1.0.6 0x7000297ec0) hands every system it updates, in 30 fps frames - 1.0
# per frame, whatever the frame mode. It isn't derived from the mode, so in the 60 fps mode animation, cameras,
# cutscenes and gameplay ran twice as fast. The update loads its address once per frame, before any system runs: that
# load asks the runtime (rt_game_step, runtime/hle/game_hooks.c), which halves the step in the 60 fps mode.
GAME_STEP = (
    (0x7100041F, 0xFFFFFFFF),  # cmp  w0, #1               (the frame's update count)
    (0x5400000B, 0xFF00001F),  # b.lt <none>
    (0x90000014, 0x9F00001F),  # adrp x20, <page>
    (0x90000015, 0x9F00001F),  # adrp x21, <page>
    (0xF9400294, 0xFFC003FF),  # ldr  x20, [x20, #<pause flag pointer>]
    (0xF94002B5, 0xFFC003FF),  # ldr  x21, [x21, #<step pointer>]
    (0x1E2703E8, 0xFFFFFFFF),  # fmov s8, wzr
    (0x2A1F03F3, 0xFFFFFFFF),  # mov  w19, wzr
)


def game_step_load(records):
    def emit(module, a):
        off = ((module.words[(a - module.lo) // 4] >> 10) & 0xFFF) * 8
        return ('{ extern uint64_t rt_game_step(uint64_t, uint64_t); '
                f'c->x[21] = rt_game_step(*(const uint64_t *)(uintptr_t)(c->x[21] + {off:#x}), {records:#x}ull); }}'
                f'  /* {a:#x}: the game\'s step, the logic updates begin (tools/patches.py) */')
    return emit


# Logic at the Switch's 30 Hz under 60 / 120 fps (rt_game_step, rt_logic_end; runtime/hle/game_hooks.c). The main
# update's loop (the game's logic, run the frame's update count times) is followed by the per-frame part: systems handed
# the step once a frame (s8 / s9, loaded here after the loop) and one handed the step times the update count. The loop
# runs in the 30 fps mode with the 30 fps step on the frames that have a logic update; the per-frame part in the
# display's.
LOGIC_END = (
    (0x90000014, 0x9F00001F),  # adrp x20, <page>
    (0xF9400294, 0xFFC003FF),  # ldr  x20, [x20, #<pause flag pointer>]
    (0x90000015, 0x9F00001F),  # adrp x21, <page>
    0x39400288,                # ldrb w8, [x20]
    (0xF94002B5, 0xFFC003FF),  # ldr  x21, [x21, #<step pointer>]
    0xBD4002A9,                # ldr  s9, [x21]             (the step, for the per-frame systems)
    0x1E2703E0,                # fmov s0, wzr
    0x7100011F,                # cmp  w8, #0
    0x1E200D28,                # fcsel s8, s9, s0, eq
)
LOGIC_COUNT_STEP = (
    0xBD4002A9,                # ldr  s9, [x21]             (the step...)
    (0x94000000, 0xFC000000),  # bl   <updates per frame>
    0x1E220000,                # scvtf s0, w0
    0x39400280,                # ldrb w0, [x20]
    0x1E200920,                # fmul s0, s9, s0            (...times the frame's updates)
)


def logic_end_load(module, a):
    return ('{ extern float rt_logic_end(float); float t; memcpy(&t, (const void *)(uintptr_t)c->x[21], 4); '
            'FP_SETS(9, rt_logic_end(t)); }'
            f'  /* {a:#x}: the logic updates end, the per-frame step (tools/patches.py) */')


def logic_count_step_load(module, a):
    return ('{ extern float rt_logic_step(float); float t; memcpy(&t, (const void *)(uintptr_t)c->x[21], 4); '
            'FP_SETS(9, rt_logic_step(t)); }'
            f'  /* {a:#x}: the step of each logic update (tools/patches.py) */')


# The game's tasks (1.0.6 0x7000163050: the list the logic update runs through; each one updated by 0x7000160c20(task,
# &step) unless its flag at +0x40 says it's done). The call goes through the runtime (rt_task_begin / rt_task_end,
# runtime/hle/game_hooks.c), which can run a task at the Switch's 30 Hz under 60 / 120 fps (debug: LARECOMP_TASK30).
TASK_UPDATE = (
    0xF94002A0,                # ldr  x0, [x21]             (the task)
    (0xB4000000, 0xFF00001F),  # cbz  x0, <end>
    (0x94000000, 0xFC000000),  # bl   <its done flag>
    (0x37000000, 0xFFF8001F),  # tbnz w0, #0, <next>
    0xF94002A0,                # ldr  x0, [x21]
    0xAA1403E1,                # mov  x1, x20               (&step)
    (0x94000000, 0xFC000000),  # bl   <update the task>
)


def task_update_call(module, a):
    w = module.words[(a - module.lo) // 4]
    off = (w & 0x3FFFFFF) - (1 << 26 if w & (1 << 25) else 0)
    fn = a + off * 4
    return ('{ extern int rt_task_begin(uint64_t); extern void rt_task_end(int); '
            f'extern void f_{fn:x}(Cpu *); int k_ = rt_task_begin(c->x[0]); uint64_t t_ = c->x[0], s_ = c->x[1]; '
            f'for (int i_ = 0; i_ < (k_ < 0 ? 0 : k_ > 0 ? k_ : 1); i_++) {{ c->x[0] = t_, c->x[1] = s_; '
            f'c->x[30] = {a + 4:#x}ull; f_{fn:x}(c); }} rt_task_end(k_); }}'
            f'  /* {a:#x}: a task\'s update (tools/patches.py) */')


# The controllers' button histories: the game's (1.0.6 0x7000235fb0, once a logic update; per player 16 entries of
# {frames, buttons, flags} at 0x7000c22820) and the menus' (0x70002219e0, players 1 and 2, one after the other): a new
# entry when the buttons change, and the current one's frame count goes up by 1 an update (at most 999). The match's
# controls (and those of the girls the game plays) and the menus read them as "what was pressed k frames ago" and "held
# for k frames", k in 30 fps frames; at 60 / 120 fps the counts went up 2 / 4 times as fast, so every press read as 2 /
# 4 times as long, the timing windows were 1/2 / 1/4 as long in real time (Beach Volleyball's misses at 60 fps) and a
# held direction repeated sooner. Each increment asks the runtime (rt_history_tick, runtime/hle/game_hooks.c): a 30 fps
# frame's worth. Its key: the site (in address order) x 4 + x19 & 3 - the player in the game's history, 0 in the
# menus' (x19 holds the history's address there, a multiple of 8).
HISTORY_TICK = (
    0x710F993F,                # cmp  w9, #0x3e6            (the current entry's frame count)
    (0x5400000C, 0xFF00001F),  # b.gt <done>
    0x11000529,                # add  w9, w9, #1
)


def history_tick_add(site):
    def emit(module, a):
        return ('{ extern uint32_t rt_history_tick(uint32_t, uint32_t); '
                f'c->x[9] = (uint32_t)((uint32_t)c->x[9] + rt_history_tick((uint32_t)c->x[9], {site * 4} + (uint32_t)(c->x[19] & 3))); }}'
                f'  /* {a:#x}: a button state\'s frame count (tools/patches.py) */')
    return emit


def find_all(words, pattern):
    pat = [p if isinstance(p, tuple) else (p, 0xFFFFFFFF) for p in pattern]
    return [i for i in range(len(words) - len(pat)) if all((words[i + k] & m) == w for k, (w, m) in enumerate(pat))]


# A system stepped in 60 Hz ticks once a frame (1.0.6 0x7000000e80, after the logic updates: 0x70005b66b0(obj, 1)
# ticks times, ticks by the frame mode: 1 at 60 fps, 2 at 30, 3 at 20). At 120 fps it ran 120 ticks a second, twice its
# speed; the count asks the runtime (rt_ticks60), which gives it one tick every other frame there.
TICKS60 = (
    (0x94000000, 0xFC000000),  # bl   <the frame mode>
    0x7100081F,                # cmp  w0, #2
    0x320007E8,                # mov  w8, #3
    0x1A9F0508,                # csinc w8, w8, wzr, eq
    0x7100041F,                # cmp  w0, #1
    0x321F03E9,                # mov  w9, #2
    0x2A1F03F4,                # mov  w20, wzr
    0x1A880135,                # csel w21, w9, w8, eq      (the ticks)
)


def ticks60_count(module, a):
    return ('{ extern uint32_t rt_ticks60(uint32_t); uint32_t m_ = (uint32_t)c->x[0]; '
            'c->x[21] = rt_ticks60(m_ == 1 ? 2 : m_ == 2 ? 3 : 1); }'
            f'  /* {a:#x}: a 60 Hz system\'s ticks this frame (tools/patches.py) */')


# The logic update's four calls (in the main update's loop, after GAME_STEP: three systems without arguments, then the
# task list with &step): through the runtime (rt_sub_begin / rt_sub_end, runtime/hle/game_hooks.c), which can run any of
# them at the Switch's 30 Hz under 60 / 120 fps (debug: LARECOMP_SUB30=<mask>, bit n for call n) - to find which
# system's logic changes at 60 fps.
LOGIC_CALLS = (12, 13, 14, 21)  # (instruction offsets from GAME_STEP's first)


def sub_call(n):
    def emit(module, a):
        w = module.words[(a - module.lo) // 4]
        off = (w & 0x3FFFFFF) - (1 << 26 if w & (1 << 25) else 0)
        fn = a + off * 4
        return ('{ extern int rt_sub_begin(int, uint64_t); extern void rt_sub_end(int, int, uint64_t); '
                f'extern void f_{fn:x}(Cpu *); uint64_t x0_ = c->x[0], x1_ = c->x[1]; int k_ = rt_sub_begin({n}, x0_); '
                f'for (int i_ = 0; i_ < (k_ < 0 ? 0 : k_ > 0 ? k_ : 1); i_++) {{ c->x[0] = x0_, c->x[1] = x1_; '
                f'c->x[30] = {a + 4:#x}ull; f_{fn:x}(c); }} rt_sub_end({n}, k_, x0_); }}'
                f'  /* {a:#x}: logic update call {n} (tools/patches.py) */')
    return emit


# The fibers (1.0.6 Fiber::RunAll 0x700066c250, the first of the logic update's calls; 27 slots of 0x88 bytes, the slot
# index is the fiber's id): each update it counts every waiting fiber's wait down (Fiber::WaitFrames(n) at
# 0x700066c5c0 sets it: n updates) and switches to those at zero until they yield. The game's scripts, the match's
# states and the girls' AI are fibers that wait in 30 fps frames, and some count their own iterations, so at 60 / 120
# fps every wait was 1/2 / 1/4 as long: Beach Volleyball's AI reacted too soon and the girls missed, the match was
# over in a third of the time. The runtime runs those fibers at the Switch's 30 Hz instead (runtime/hle/game_hooks.c
# rt_fiber_waiting, rt_fiber_begin / rt_fiber_end): counted down and run on the updates of a 30 fps frame only, in the
# 30 fps mode with the 30 fps step. The camera's fiber keeps the display's rate.
FIBER_RUN = (
    0x9B365517,                # smaddl x23, w8, w22, x21  (slot w8)
    0xB84C8EE9,                # ldr  w9, [x23, #0xc8]!    (its flags)
    0x12000529,                # and  w9, w9, #3           (1: waiting)
    0x7100053F,                # cmp  w9, #1
    (0x54000001, 0xFF00001F),  # b.ne <next>
    0x93407D18,                # sxtw x24, w8
    0x9B165708,                # madd x8, x24, x22, x21
    0xB940CD09,                # ldr  w9, [x8, #0xcc]      (frames left to wait)
    0x5100052A,                # sub  w10, w9, #1
    0x7100013F,                # cmp  w9, #0
    0x1A9FC149,                # csel w9, w10, wzr, gt
    0xB900CD09,                # str  w9, [x8, #0xcc]
    (0x35000009, 0xFF00001F),  # cbnz w9, <next>
    0x9B165708,                # madd x8, x24, x22, x21
    0x9101C113,                # add  x19, x8, #0x70
    0xAA1303E0,                # mov  x0, x19
    (0x94000000, 0xFC000000),  # bl   <switch to the fiber>
)


# The controller's press edges: Input::ClearPressed and Input::ClearRepeated (1.0.6 0x70003b9330, 0x70003b9340: the
# pad globals' address from a GOT entry; just pressed at +0x48, just released +0x4c, auto-repeated +0x50). A press is
# there for one update; the fibers at 30 Hz get every edge since their last run instead (rt_fiber_waiting).
PAD_CLEAR = (
    (0x90000008, 0x9F00001F),  # adrp x8, <page>
    (0xF9400108, 0xFFC003FF),  # ldr  x8, [x8, #<the pad globals' GOT entry>]
    0xF900251F,                # str  xzr, [x8, #0x48]     (pressed and released)
    0xD65F03C0,                # ret
    (0x90000008, 0x9F00001F),  # adrp x8, <page>
    (0xF9400108, 0xFFC003FF),  # ldr  x8, [x8, #<the same>]
    0xB900511F,                # str  wzr, [x8, #0x50]     (auto-repeated)
    0xD65F03C0,                # ret
)


def got_entry(module, i):
    """The GOT entry an adrp at words[i] and the ldr x, [x, #imm] after it load."""
    w, a = module.words[i], module.lo + i * 4
    imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)
    if imm & (1 << 20):
        imm -= 1 << 21
    return (a & ~0xFFF) + (imm << 12) + ((module.words[i + 1] >> 10) & 0xFFF) * 8


def fiber_waiting(pad_got):
    def emit(module, a):
        return ('{ extern uint32_t rt_fiber_waiting(uint32_t, uint32_t, uint64_t); '
                f'c->x[9] = rt_fiber_waiting((uint32_t)c->x[8], (uint32_t)c->x[9] & 3, {pad_got:#x}ull); }}'
                f'  /* {a:#x}: does a fiber wait this update (tools/patches.py) */')
    return emit


def fiber_switch(module, a):
    w = module.words[(a - module.lo) // 4]
    off = (w & 0x3FFFFFF) - (1 << 26 if w & (1 << 25) else 0)
    fn = a + off * 4
    return ('{ extern int rt_fiber_begin(uint32_t); extern void rt_fiber_end(int); '
            f'extern void f_{fn:x}(Cpu *); int k_ = rt_fiber_begin((uint32_t)c->x[24]); '
            f'c->x[30] = {a + 4:#x}ull; f_{fn:x}(c); rt_fiber_end(k_); }}'
            f'  /* {a:#x}: switch to a fiber (tools/patches.py) */')


def adrp_add(module, i):
    """The address an adrp at words[i] and the add after it make."""
    w, a = module.words[i], module.lo + i * 4
    imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)
    if imm & (1 << 20):
        imm -= 1 << 21
    return (a & ~0xFFF) + (imm << 12) + ((module.words[i + 1] >> 10) & 0xFFF)


# 120 fps. The engine has no 120 fps frame mode, so it runs in its 60 fps mode (mode 0: one logic step of 1/60 s a
# frame) and the runtime makes each frame half of that. Three things carry the mode's step into the game:
# - the step count (1.0.6 0x700038f640: logic steps per frame, an integer: 1 at 60 fps, 2 at 30). Its 34 callers use it
#   in three ways, each patched where it's used (an integer can't hold half a step):
#   - converted to a float and multiplied (ucvtf s, w0): 0.5 at 120 fps (rt_steps_float);
#   - dividing a time in 1/60 s ticks into frames (udiv w, w, w0), then counted down by 1 a frame: twice the frames
#     (rt_steps_div);
#   - added to or taken from a tick counter (add / sub w, w, w0): 1 every other frame (rt_steps_count).
#   (A few callers don't use the value.)
# - the time step (1.0.6 0x700038f690: seconds per frame, 1/60 in mode 0): 1/120 (rt_time_step);
# - the game's float step (GAME_STEP above): a quarter of the 30 fps one (rt_game_step).
# At 30 and 60 fps all of these return what the game had.
STEPS_FN = (
    (0x90000008, 0x9F00001F),  # adrp x8, <page>
    (0x91000108, 0xFFC003FF),  # add  x8, x8, #<frame records>
    0xB9400509,                # ldr  w9, [x8, #4]          (the current record)
    0x8B091108,                # add  x8, x8, x9, lsl #4
    0x321E07E9,                # mov  w9, #0xc              (12 bytes a mode)
    0xB9400D08,                # ldr  w8, [x8, #0xc]        (its mode)
    (0x9000000A, 0x9F00001F),  # adrp x10, <page>
    (0x9100014A, 0xFFC003FF),  # add  x10, x10, #<mode table>
    0x9B092908,                # madd x8, x8, x9, x10
    0xB9400900,                # ldr  w0, [x8, #8]          (the mode's logic steps per frame)
    0xD65F03C0,                # ret
)
TIME_STEP = (
    0xB9800D08,                # ldrsw x8, [x8, #0xc]       (the mode)
    0x7100091F,                # cmp  w8, #2
    (0x54000008, 0xFF00001F),  # b.hi <a default>
    (0x90000009, 0x9F00001F),  # adrp x9, <page>
    (0x91000129, 0xFFC003FF),  # add  x9, x9, #<time steps>
    0xBC687920,                # ldr  s0, [x9, x8, lsl #2]  (the mode's time step)
)


# The game updates per frame (1.0.6 0x700038f8a0): the mode's base (1) plus the catch-up updates the engine's
# late-frame accumulator wants after a slow frame (the main update 0x7000297ec0 loops that many times). The engine
# measures lateness in 60 Hz vsyncs, so at 120 fps it never catches up and a slow frame plays in slow motion; there
# the runtime counts the catch-up itself from the real frame times (rt_frame_updates).
UPDATES_FN = (
    0xB9401509,                # ldr  w9, [x8, #0x14]       (the engine's catch-up updates)
    0xB9400D08,                # ldr  w8, [x8, #0xc]        (the mode)
    0x321E07EA,                # mov  w10, #0xc
    (0x9000000B, 0x9F00001F),  # adrp x11, <page>
    (0x9100016B, 0xFFC003FF),  # add  x11, x11, #<mode table>
    0x9B0A2D08,                # madd x8, x8, x10, x11
    0xB9400508,                # ldr  w8, [x8, #4]          (the mode's base)
    0x0B090100,                # add  w0, w8, w9
    0xD65F03C0,                # ret
)


def updates_add(module, a):
    return ('{ extern uint32_t rt_frame_updates(uint32_t, uint32_t); '
            'c->x[0] = rt_frame_updates((uint32_t)c->x[8], (uint32_t)c->x[9]); }'
            f'  /* {a:#x}: the game updates this frame (tools/patches.py) */')


def steps_float(d):
    def emit(module, a):
        return ('{ extern float rt_steps_float(uint32_t); '
                f'FP_SETS({d}, rt_steps_float((uint32_t)c->x[0])); }}'
                f'  /* {a:#x}: logic steps per frame, as a float (tools/patches.py) */')
    return emit


def steps_div(d, n):
    def emit(module, a):
        return ('{ extern uint32_t rt_steps_div(uint32_t, uint32_t); '
                f'c->x[{d}] = rt_steps_div((uint32_t)c->x[{n}], (uint32_t)c->x[0]); }}'
                f'  /* {a:#x}: ticks into frames (tools/patches.py) */')
    return emit


def steps_count(d, n, op):
    def emit(module, a):
        return ('{ extern uint32_t rt_steps_count(uint32_t); '
                f'c->x[{d}] = (uint32_t)((uint32_t)c->x[{n}] {op} rt_steps_count((uint32_t)c->x[0])); }}'
                f'  /* {a:#x}: a tick counter (tools/patches.py) */')
    return emit


def time_step_load(module, a):
    return ('{ extern float rt_time_step(float); float t; '
            'memcpy(&t, (const void *)(uintptr_t)(c->x[9] + (c->x[8] << 2)), 4); FP_SETS(0, rt_time_step(t)); }'
            f'  /* {a:#x}: the time step (tools/patches.py) */')


def steps_uses(module, fn):
    """{address: emit} for the uses of the step count that follow each call to fn (see STEPS_FN), and the counts."""
    out, kinds = {}, {'float': 0, 'div': 0, 'count': 0, 'unused': 0}
    for i, w in enumerate(module.words):
        if (w & 0xFC000000) != 0x94000000 or module.lo + i * 4 + (((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) * 4 != fn:
            continue
        kind = 'unused'
        for k in range(1, 5):  # (the use comes within 4 instructions: after a load of the counter, at most)
            if i + k >= len(module.words):
                break
            u, a = module.words[i + k], module.lo + (i + k) * 4
            d, n, m = u & 31, (u >> 5) & 31, (u >> 16) & 31
            if (u & 0xFFFFFFE0) in (0x1E230000, 0x1E220000):        # ucvtf / scvtf s<d>, w0
                out[a], kind = steps_float(d), 'float'
            elif (u & 0xFFE0FC00) == 0x1AC00800 and m == 0:          # udiv w<d>, w<n>, w0
                out[a], kind = steps_div(d, n), 'div'
            elif (u & 0xFFE0FC00) in (0x0B000000, 0x4B000000) and m == 0 and n != 0:  # add / sub w<d>, w<n>, w0
                out[a], kind = steps_count(d, n, '+' if u & 0x40000000 == 0 else '-'), 'count'
            elif (u & 0x7C000000) == 0x14000000 or (u & 0x7E000000) == 0x34000000 or u == 0xD65F03C0:
                break  # (a branch, call or return first: the value isn't used here)
            else:
                continue
            break
        kinds[kind] += 1
    return out, kinds


# A worker's state in a handshake with the main thread (1.0.6 0x70002bec70, the "kick"; the worker 0x70002bebd0): the
# worker runs its job (state 1), sets state 2 and signals the main thread, then sets state 3 and waits for the next kick.
# The kick wakes it only in state 3, and the main thread then waits for it regardless. Woken in the gap between the
# worker's signal and its state 3, the main thread can kick too early: skipped, and both sleep for ever. On the Switch the
# worker's higher priority on a shared core closes the gap; on a PC, in a light scene (the opening movie), it hung about
# one run in ten. The kick's load of the state asks the runtime (rt_worker_state, runtime/hle/game_hooks.c), which waits
# out state 2.
WORKER_KICK = (
    0xB940050B,  # ldr  w11, [x8, #4]       (the worker's state)
    0x7100055F,  # cmp  w10, #1             (a kick is wanted)
    0x7A430960,  # ccmp w11, #3, #0, eq
    0x54000040,  # b.eq <wake it>
    0xD65F03C0,  # ret
)


def worker_state_load(module, a):
    return ('{ extern uint32_t rt_worker_state(uint64_t, uint32_t); '
            'c->x[11] = rt_worker_state(c->x[8] + 4, (uint32_t)c->x[10]); }'
            f'  /* {a:#x}: the worker\'s state (tools/patches.py) */')


def find_one(words, pattern, what):
    """Index of the one place in words matching pattern ((word, mask) pairs, or plain words)."""
    pat = [p if isinstance(p, tuple) else (p, 0xFFFFFFFF) for p in pattern]
    n = len(pat)
    sites = [i for i in range(len(words) - n)
             if all((words[i + k] & m) == w for k, (w, m) in enumerate(pat))]
    if len(sites) != 1:
        raise ValueError(f'{what}: {len(sites)} matches for its instruction pattern, expected 1')
    return sites[0]


def find(module):
    patches = {}
    i = find_one(module.words, RENDER_CONFIG, 'render config')
    patches[module.lo + (i + len(RENDER_CONFIG) - 1) * 4] = render_config_store
    i = find_one(module.words, FRAME_MODE_INIT, 'frame mode init')
    for k, off in ((4, 0xc), (7, 0x1c), (10, 0x2c), (13, 0x3c)):
        patches[module.lo + (i + k) * 4] = frame_mode_store(9, off)
    i = find_one(module.words, FRAME_MODE_SET, 'frame mode setter')
    patches[module.lo + (i + 2) * 4] = frame_mode_store(0, 0xc)
    i = find_one(module.words, FRAME_EXTRA, 'frame extra vsyncs')
    patches[module.lo + i * 4] = frame_extra_load
    steps = find_one(module.words, STEPS_FN, 'logic steps per frame')
    i = find_one(module.words, GAME_STEP, 'game step')
    patches[module.lo + (i + 5) * 4] = game_step_load(adrp_add(module, steps))
    for n, k in enumerate(LOGIC_CALLS):
        if (module.words[i + k] & 0xFC000000) != 0x94000000:
            raise ValueError(f'logic update call {n}: not a bl')
        patches[module.lo + (i + k) * 4] = sub_call(n)
    i = find_one(module.words, LOGIC_END, 'logic end')
    patches[module.lo + (i + 5) * 4] = logic_end_load
    i = find_one(module.words, LOGIC_COUNT_STEP, 'logic count step')
    patches[module.lo + i * 4] = logic_count_step_load
    i = find_one(module.words, FIBER_RUN, 'fiber scheduler')
    pad = find_one(module.words, PAD_CLEAR, 'pad clear')
    patches[module.lo + (i + 2) * 4] = fiber_waiting(got_entry(module, pad))
    patches[module.lo + (i + 16) * 4] = fiber_switch
    i = find_one(module.words, TICKS60, '60 Hz ticks')
    patches[module.lo + (i + 7) * 4] = ticks60_count
    sites = find_all(module.words, HISTORY_TICK)
    if not 1 <= len(sites) <= 8:
        raise ValueError(f'button history tick: {len(sites)} matches for its instruction pattern, expected 1-8')
    for k, i in enumerate(sites):
        patches[module.lo + (i + 2) * 4] = history_tick_add(k)
    print(f'game: {len(sites)} button history counters', file=sys.stderr)
    i = find_one(module.words, TASK_UPDATE, 'task update')
    patches[module.lo + (i + 6) * 4] = task_update_call
    i = find_one(module.words, WORKER_KICK, 'worker kick')
    patches[module.lo + i * 4] = worker_state_load
    i = find_one(module.words, STEPS_FN, 'logic steps per frame')
    uses, kinds = steps_uses(module, module.lo + i * 4)
    patches.update(uses)
    print('game: logic steps per frame used as ' + ', '.join(f'{v} {k}' for k, v in kinds.items()), file=sys.stderr)
    i = find_one(module.words, TIME_STEP, 'time step')
    patches[module.lo + (i + 5) * 4] = time_step_load
    i = find_one(module.words, UPDATES_FN, 'updates per frame')
    patches[module.lo + (i + 7) * 4] = updates_add
    return patches
