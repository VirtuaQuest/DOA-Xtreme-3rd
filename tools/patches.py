"""Hand-written replacements for single instructions of the game's main program (tools/gen.py PATCHES).

Sites are found by their instruction pattern, not by address, so the same patch applies to every version that has the
code (1.0.0 and 1.0.6). find(module) returns {guest address: function(module, address) -> one C statement}.

The render config default (the engine fills it before creating its render targets): three resolution levels, 1920x1080,
1600x900 and 1280x720, and the level the 3D scene renders at, level 1 (1600x900; the game upscales it to the 1080p
screen itself). The store of levels 0 and 1 is replaced by one that asks the runtime for level 1's size
(rt_render3d_size, runtime/hle/game_hooks.c), so the 3D resolution is a setting.
"""
import struct

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
    return patches
