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


def game_step_load(module, a):
    off = ((module.words[(a - module.lo) // 4] >> 10) & 0xFFF) * 8
    return ('{ extern uint64_t rt_game_step(uint64_t); '
            f'c->x[21] = rt_game_step(*(const uint64_t *)(uintptr_t)(c->x[21] + {off:#x})); }}'
            f'  /* {a:#x}: the game\'s step (tools/patches.py) */')


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
    i = find_one(module.words, GAME_STEP, 'game step')
    patches[module.lo + (i + 5) * 4] = game_step_load
    i = find_one(module.words, WORKER_KICK, 'worker kick')
    patches[module.lo + i * 4] = worker_state_load
    i = find_one(module.words, STEPS_FN, 'logic steps per frame')
    uses, kinds = steps_uses(module, module.lo + i * 4)
    patches.update(uses)
    print('game: logic steps per frame used as ' + ', '.join(f'{v} {k}' for k, v in kinds.items()), file=sys.stderr)
    i = find_one(module.words, TIME_STEP, 'time step')
    patches[module.lo + (i + 5) * 4] = time_step_load
    return patches
