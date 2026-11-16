# Changes

What this project changed relative to [LAS Recomp](https://github.com/peiikai/LASRecomp) (its recompiler and Switch
runtime), why, and where. It also lists the debugging switches, the test routes, the known gaps and the next steps.
Newest work is at the bottom of each section; the commits are on `main`.

## Project setup (ae4fa76)

- **Retargeted at DOAX3 Scarlet 1.0.0** (title 01009CC00C97C000, NintendoSDK 6.4.2). The Link's Awakening specific
  code is removed (warp, mirror mode, widescreen camera, pots, pets, randomizer, save editor, extra flags).
- **Game hooks:** `runtime/hle/game_hooks.c` and `runtime/ui/game_panels.mm`. Each hook point the shared runtime calls
  answers "nothing special" until a DOAX3 counterpart is written. `game_in_play()` returns 0, because the game's
  in-play state isn't located yet.
- **Three recompiled modules** (`Makefile`: `make gen`, then `make -j16`):

  | Module | What it is | Output |
  |---|---|---|
  | `main` | the game | `generated/game` |
  | `sdk` | NintendoSDK | `generated/sdk` |
  | `subsdk0` | NVIDIA glslc, base 0x7005000000 | `generated/glslc` |

  `subsdk1` (multimedia) is not loaded; `movie::` is host code instead.
- **Tools:**
  - `tools/nsp_extract.py`: extracts exefs, romfs and control from an NSP using your own `prod.keys`. It handles a
    base game only (no update/BKTR patch support yet).
  - `tools/sym.py` and `tools/addr2sym.py`: name guest addresses.
  - `tools/run.sh [secs]`: runs the game; the log goes to `doax3.log`.

## Kernel, HLE and services (ae4fa76)

- **Fibers:** `nn::os` fibers, in `runtime/hle/fiber.c`.
- **Offline network** (`runtime/services/net.c`), as on a console with no connection:
  - nifm turns every connection request down;
  - bsd fails to create sockets;
  - ssl hands out contexts that never get used.
- **Library applets** (controller strap guide, error dialogs): skipped and logged, in `runtime/hle/applets.c`.
- **`movie::MediaData::FindRect`**, in `runtime/hle/movie.c`.
- **Ids:** the program id and the add-on content base id (0x01009CC00C97D000) are DOAX3's, in
  `runtime/kernel.c` (GetInfo) and `runtime/services/misc.c` (aoc). aoc reports no DLC: the base dump is the *Basic Free Edition*.

## Audio (`runtime/services/audio.c`)

- **ae4fa76:**
  - The audren work buffer is sized as the SDK expects.
  - Splitter routing works; voices routed through splitters were silent before.
  - The BufferMixer effect is implemented.
- **3aceb94: Aux effect (type 2)**, ported from the SDK module's own code (`nn::audio::server::AuxInfo::Update`,
  `CommandGenerator::GenerateAuxCommand`, `dsp::WriteAuxBufferDsp` / `ReadAuxBufferDsp`):
  - The effect's input goes into the send ring.
  - Its output is replaced by what the game wrote into the return ring.
  - A disabled effect resets both rings and passes the input through dry.
  - Verified: the return is the send delayed by 2400 samples, correlation ~1.0, no dropouts.

  Parameter layout:

  | Offset | Field |
  |---|---|
  | 0 | in[24] |
  | 0x18 | out[24] |
  | 0x30 | channels |
  | 0x34 | rate |
  | 0x38 | ring sample count |
  | 0x40 | send info address |
  | 0x50 | return info address |

  Ring state is at info+0x40 (read offset, then write offset); int32 samples start at info+0x80.

## Graphics: NVN on Vulkan, Maxwell shader translator (ae4fa76)

- **Shader translator** (`runtime/nvn/maxwell/translate.c`):
  - separable programs;
  - shared memory (LDS/STS);
  - image load/store (SULD/SUST);
  - stencil sampling, in compute shaders only (`ShaderKey.tex_stencil_mask`).
- **Textures** (`runtime/nvn/vulkan/textures.c`):
  - storage image views (`vk_storage_view`);
  - buffer textures (target 10, made a 1D image);
  - 32-bit integer formats viewed as float;
  - a 1x1 2D texture bound in a cube slot becomes a 1x1 cube of its texel (`cube_of_1x1`); before, it was a black dummy.
- **Present pacing:** `present_pace` in `runtime/nvn/objects.c` paces to the game's present interval (2, so 30 fps).
- **Resolution:** native 1x by default. Scaled targets broke the deferred lighting compute pass, which stores at the
  game's pixel coordinates.

## Shader preparation at startup (efffec6)

- **Problem:** LAS Recomp's preparation (build every pipeline the game used before, ahead of the game) never ran.
  - It started from LA's launcher.
  - It looked for shader code where DOAX3 doesn't keep it; DOAX3's shaders are inside its packed data archive.
- **Code store:** each stage's code is kept in `cache/shader_code/<hash>.bin` the first time the game builds it.
  This is on the player's machine only and never committed.
- **Preparation:** it runs at device init from `cache/shader_code` and `cache/shader_seen.txt`.
- **Compute records** carry their stencil slots (` st=`), so the lighting shader is prepared too.
- **Result:** a second visit to the hotel prepares 40 variants in 0.24 s, with no first-visit compiles. Before, the
  first visit skipped ~1250 draws over 2 s.

## GPU waits the game can't observe (b18b47d, `runtime/nvn/vulkan/exec.c`)

- **Problem:** DOAX3's loader copies each texture out of a staging texture and calls `nvnQueueFinish` ~350 times per
  load.
- **Fix:** the copies snapshot guest data when translated. So the game's waits (QueueFinish, SyncWait) now stop at the
  latest submission with observable work: draws, dispatches or ReportCounter.
  - `obs_seq` records that submission; `note_observable()` updates it; `observable_limit()` applies it.
- **Result:** load-screen stalls dropped from 115–240 ms to 60–110 ms.
- **Also:** `runtime/nvn/slowframe.c` can log slow frames during loads.

## Opening movie (67d7a94, `runtime/hle/movie.c`)

- **Problem:** the opening started black, then crashed on its first frame. SDK 6.4.2's movie API differs from LA's.
- **Method:** the vtable order came from resolving `subsdk1`'s `_ZTVN5movie*` relocations.
- **Extractor:** slot 2 is `SetDataSource(const char *path)`, with the path in x1.
- **Decoder:**
  - The game creates its textures and buffers on the "format changed" event (event 2). So the first video frame
    signals event 2, and output is held until the game has called `GetOutputFormat`.
  - The format includes `stride`.
  - Frames are copied as packed NV12 (Y at stride = width, then UV) into the buffer the game passes to
    `GetOutputBuffer`.
  - Slot 17, `GetOutputBuffer(int, Buffer*, MediaData*)`, is implemented.
- **Debugging aids added:**
  - `LARECOMP_TRACE_CALL` (`runtime/dispatch.c`).
  - `LARECOMP_FNPROF` now writes its profile on a crash (`runtime/fnprof.c`, `runtime/platform/windows.c`).
- **When it plays:** only on a fresh save, when the first vacation starts.

## Test routes (7443143, `tools/route.sh`)

`tools/route.sh <route> [secs] [shot frames]` runs the game with scripted input; shots go to `build/frames/`.

| Route | Save | Path | Time |
|---|---|---|---|
| `menus` | existing | title, hotel room, casino | ~160 s |
| `volleyball` | fresh | opening movie, Kasumi's arrival, Niki Beach, Beach Volleyball | ~420 s |
| `relax` | fresh | as `volleyball`, then the shower scene, evening, night | ~330 s |
| `momiji` | fresh | as `relax`, with Momiji picked on the character select | ~330 s |
| `select` | fresh | the opening, then the character select screen left idle | ~190 s |
| `select-walk` | fresh | the character select, right pressed every 60 frames (who is where) | ~200 s |

Fresh-save routes move `save/` to `save.route-backup/` and put it back afterwards.

Your keyboard, mouse and controllers work during a route, so you can take over or help. A press of yours shifts the
route's timing; `LARECOMP_NO_USER_INPUT=1` keeps a run strictly scripted.

## Update 1.0.6 and DLC

- **Extractor** (`tools/nsp_extract.py`):
  - `--base <game.nsp>` reads an update's BKTR RomFS. The indirect table maps the patched image onto the base RomFS
    and the update's own data. That data is AES-CTR with a per-range counter generation (the AesCtrEx table).
  - DLC (`publicdata` NCAs) go to `<outdir>/aoc/<data id>/`. Personalized tickets are refused with a message; all
    of the user's dumps have common tickets.
  - Checked against the image's own IVFC hash tree: levels 1-5 match, including every block the update rewrote.
- **What 1.0.6 changes:** only `main` (build ID 71102550...) and `main.npdm`. `sdk`, `subsdk0` and `subsdk1` are
  byte-identical to 1.0.0's, and so are all 21 of 1.0.0's RomFS files. The update adds packages
  `romfs/01`-`06` (data_01..06.lnk, ~270 MB).
- **Versions:** `runtime/game_addresses.c` has 1.0.6's build ID. The address tables are still empty for both.
- **DLC** (`runtime/services/fs.c`, `misc.c`):
  - aoc:u Count/List report each folder in `extracted/aoc` (index = data id - 0x01009CC00C97D000).
  - fsp-srv 202 `OpenDataStorageByDataId` serves that DLC's RomFS, laid out from its folder like the base image
    (`layout_pread`, shared with the base). The log counts each DLC's reads (`[aoc] <id> reads:`).
  - The game mounts all 55 at boot.
- **Makefile:** `gamelib_entry.o` now depends on `extracted/exefs/main`. Before, a new `main` kept the old build ID
  inside the library, and the runtime refused it ("built for another version").
- **Checked on 1.0.6:** the title shows Ver. 1.06; the menus route (hotel, casino, closet) plays at 30 fps with no
  errors.

## 3D resolution setting

- **Where the 1600x900 comes from:** the engine's render config default (1.0.6: 0x700061da00; 1.0.0: 0x700061d590)
  has three levels, 1920x1080, 1600x900 and 1280x720, and selects level 1 for the 3D scene. The game then scales that
  scene to its 1080p screen itself. The per-level table is at 0x7001d5a250 (0x9b8 bytes per view; w/h at +0x20/+0x24).
  It isn't GPU-driven: the game's own GPU timers read 2-5 ms per frame.
- **Patch** (`tools/patches.py`, new; `tools/gen.py` loads it for the `game` module): the store of levels 0 and 1
  asks the runtime for level 1's size (`rt_render3d_size`, `runtime/hle/game_hooks.c`). The site is found by its
  five instructions, so it applies to 1.0.0 and 1.0.6 alike. Because the game itself renders bigger, its deferred
  lighting compute pass sees consistent sizes. That pass is why the runtime's own scaled targets were off.
- **Setting:** `render3d` in larecomp.ini and Video > Rendering > "3D resolution": Original (900p), 1080p, 1440p, 4K.
  It applies at the next start, because the game makes its targets at boot. Defaults: 1440p on a dedicated GPU,
  Original on an integrated one. `LARECOMP_3D_RES=<w>x<h>` forces any size (test).
- **Measured** (RTX 3060, the game's GPU timers, title screen, median): 900p 2.2 ms, 1080p 2.6 ms, 1440p 4.0 ms,
  4K 6.6 ms. Volleyball at 4K: median 7.5 ms, p99 10.6 ms.
- **4K is not the default:** it crashes at the scene change after a volleyball match. The game's allocator
  (0x7000506380) returns null, because the 4K targets don't fit the game's own ~3 GB heap. The runtime's guest
  memory map caps the heap at 3.25 GB (`HEAP_MAX`, `runtime/rt.h`). 4K stays selectable, with that risk.
- **Checked at 1440p:** the volleyball and relax routes (fresh save) and menus all run at 30 fps with no errors and
  correct lighting.

## 60 fps

- **How the game keeps time:** the engine has frame modes. Its frame controller keeps a ring of 4 frame records at
  0x7000d6dec0 (1.0.6), with the mode at +0xc. A mode indexes per-mode tables:

  | Mode | Present interval | Logic steps per frame (0x700038f640, read all over the game) | Time step (0x700038f690) |
  |---|---|---|---|
  | 0 | 1 (60 fps) | 1 | 1/60 s |
  | 1 | 2 (30 fps) | 2 | 1/30 s |
  | 2 | 3 (20 fps) | 3 | 1/20 s |

  The Switch game stores mode 1: at init (all 4 records) and once more at boot (the setter 0x700038f750, called
  with 1).
- **Patch** (`tools/patches.py` FRAME_MODE_INIT / FRAME_MODE_SET): those mode stores go through `rt_frame_mode`,
  which turns 1 into 0. Every table then agrees: one 1/60 s step per frame. The first attempt changed only the base
  interval, and the game ran 2x fast, because it still took two steps per frame. The boot logos showed it: they
  finished in half the time, while with the mode patch they take as long as at 30 fps.
- **Extra vsyncs** (FRAME_EXTRA, `rt_frame_extra`): the engine's late-time accumulator (0x70003b9d60) counts any
  frame over 97.5% of its 1/59.94 s period as partly late. Our paced 1/60 s frames built that up until it added 2
  vsyncs, so the interval went 1, 3, 1, 3, about 43 fps. In 60 fps mode those vsyncs are dropped, and the runtime's
  present pacing keeps time. The cause turned out to be the frame pacer inherited from LAS Recomp (see
  "Frame pacing fixes" below): with it fixed, the game asks for 2 extra vsyncs in a 100 s run instead of thousands;
  the drop stays as a safety net.
- **The game's step** (GAME_STEP, `rt_game_step`): with only the frame mode changed, the game still ran 2x fast.
  - **Found by:** a 30 fps run and a 60 fps run of the relax route, compared shot by shot (`LARECOMP_SHOT_SIZE`
    series, aligned per half second of game time). The title camera and Kasumi's arrival cutscene ran at twice the
    speed.
  - **Cause:** the main update (0x7000297ec0) hands every system it updates a float step in 30 fps frames, 1.0 a frame
    (0x700087cb6c, read through 0x7000a7fdb8 by 82 functions). It isn't derived from the frame mode. A memory diff of
    the two runs found it (`LARECOMP_DUMP_MEM`), along with a steady-counter scan (`LARECOMP_COUNTER_SCAN`) and the
    function writing one of those counters (`LARECOMP_WATCH_ADDR`).
  - **Patch:** the update loads the step's address once a frame, before any system runs. That load goes through
    `rt_game_step`, which halves the step in the 60 fps mode. Any value the game stores itself is halved the next
    frame. The game never wrote it in the volleyball and relax routes (`LARECOMP_PEEK`).
  - **Result:** the comparison runs at the same speed throughout (rate 1.00), with the cutscenes, the title camera and
    the opening. The character select's backgrounds change every 10 s in both modes (it went with the step, too: the
    "UI timers" noted before were this).
- **Setting:** `fps60` in larecomp.ini, Video > Rendering > "60 fps", or the launcher's "60 fps" box. It applies at
  the next start. `LARECOMP_FPS=30/60/120` overrides it (test). Experimental and off by default since v0.1.5 (see
  "60 and 120 fps: what still differs"); the setting is `frame_rate = 60` now.
- **Result** (RTX 3060, 1440p 3D): a locked 60 fps through the menus and a 7-minute run (186 of ~200 two-second
  windows at 60.0; dips only at loads). The game uncapped runs at 230-250 fps, worst frame 8 ms.
- `LARECOMP_LOG_INTERVAL=1` now logs the guest call stack of each present-interval change.

## 120 fps

- **No such mode in the engine:** its frame modes stop at 60 fps (mode 0). The 120 fps mode runs mode 0, and every
  frame is half of one of its frames. Three values carry the step into the game, and each is halved where it's read:
  - **The step count** (1.0.6 0x700038f640: logic steps per frame, an integer, 1 in mode 0). An integer can't hold
    half, so each of its 34 call sites is patched by what it does with it (`tools/patches.py` STEPS_FN, found by the
    instruction after the call):
    - 8 convert it to a float and multiply (`ucvtf`): 0.5 (`rt_steps_float`);
    - 12 divide a time in 1/60 s ticks by it into a number of frames, then count that down by one a frame (`udiv`):
      twice the frames (`rt_steps_div`). Found in 0x7000159320: +0xe8 = ticks / steps, then -1 a frame;
    - 11 add it to or take it from a tick counter (`add` / `sub`): the step on every other frame, so 60 ticks a second
      (`rt_steps_count`, by the presented frame count);
    - 3 don't use it.
  - **The time step** (0x700038f690, seconds per frame, 1/60 in mode 0): 1/120 (`rt_time_step`, TIME_STEP).
  - **The game's float step** (GAME_STEP): 0.25 instead of 0.5 (`rt_game_step`).
- **Pacing:** `present_pace` paces at 120 Hz in this mode (a display within 0.5% of 120 Hz sets the grid, as at 60).
- **Catch-up** (`tools/patches.py` UPDATES_FN, `rt_frame_updates`; `objects.c` catchup_frame): the game updates a
  frame 1 + the catch-up its late-frame accumulator wants (the main update 0x7000297ec0 loops that many times, the
  count from 0x700038f8a0). The accumulator counts 60 Hz vsyncs, so at 120 fps it never caught up and every frame
  under 120 played in slow motion. At 120 the runtime counts it instead: the time the frames took beyond 1/120 s,
  carried from frame to frame (at most 3 periods owed, 3 extra updates a frame). The log counts them (`[nvn] 120 fps:
  N catch-up updates so far`). At 30 and 60 the engine's own catch-up stays.
- **Experimental**, and labelled so everywhere: off by default (30 fps is the default since v0.1.5).
- **Setting:** `frame_rate = 120` in larecomp.ini (since v0.1.5; v0.1.4's `fps60` / `fps120` lines are ignored),
  Video > Rendering > "Frame rate", the launcher's Frame rate, or RecompLauncher's. `LARECOMP_FPS=120` (test).
  `tools/route.sh` stretches its presses 4x at 120.
- **Checked** (RTX 3060, 1440p 3D, a 144 Hz display):
  - **Speed:** screenshot series at the same game times, matched shot by shot. The select route: 120 against 30 at
    1.02. The relax route: 120 against 60 at 1.047, the same as 60 against 30 (1.047), so that 5% is the route and
    the matching, not the mode.
  - **Frame rate:** 120.0 fps in two-second windows, dips to 100-111 fps at loads and in heavy volleyball scenes with
    SMAA.
  - **Not reached by the test routes:** the patched divisions and tick counters (0x7000155000-0x700015f000, an
    unknown system). Their handling follows the code above, but no run has used them yet; at 120 fps the log says
    when one is used for the first time (`[game] 120 fps: logic steps per frame used ...`).
  - **Menus:** the input manager's repeat is by time (0x700063b3c0: the first after 166.7 ms, then every 100 ms, by
    the real time since the last frame), but some menus count frames themselves: see below.

## 60 and 120 fps: what still differed (v0.1.5), and the fix (v0.1.6)

v0.1.4 made 60 the default. Players then found Beach Volleyball and menus behaving differently. v0.1.5 went back to
30 by default (`frame_rate`, a new key, so v0.1.4's `fps60 = 1` doesn't keep anyone at 60) and labelled 60 and 120
experimental. What differed, and why:
- **Beach Volleyball:** a match from a fresh save, the menus at 30 fps and the match from frame 7250 at the rate
  tested (`LARECOMP_FPS_FROM`, below), in the match A, B, Y, X in turn every 0.5 s: at 30 fps ~120 s with rallies,
  replays and cut-ins; at 60 fps over in 40-50 s, our side scoring 0-1 points, no replays.
- **Menus:** on the character select, a 267 ms hold of the stick moved the cursor 1 / 4 / 9 places at 30 / 60 / 120
  fps, a 2 s hold 19 / 39 / 79.
- **The cause: the fibers.** The game's scripts, the menus, the match's states and the girls' AI run as fibers
  (`Fiber::RunAll`, 1.0.6 0x700066c250, 27 slots; names from the decompilation project's notes), which wait in 30 fps
  frames (`Fiber::WaitFrames(n)`, 0x700066c5c0: n logic updates) and count their own iterations (menu key repeat:
  "after 10 frames, every 3"). The 60 fps mode made every update half a Switch one, so the fibers ran twice as fast.
  Found by bisecting the logic update's four calls at 30 Hz (`LARECOMP_SUB30`): only the fiber scheduler's mattered.
- **The fix** (`tools/patches.py` FIBER_RUN, `rt_fiber_waiting` / `rt_fiber_begin` / `rt_fiber_end`): under 60 / 120 fps
  every fiber but the camera's (0x13) is counted down and run at the Switch's 30 Hz - on one frame in 2 / 4 (more
  after a slow frame, `rt_logic_tick`), in the 30 fps frame mode with the 30 fps step. The camera fiber, the girls'
  movement, animation and physics, the ball and the effects keep the display's rate: consecutive frames at 60 fps all
  differ in the match (with the whole logic at 30 Hz, every second frame was the same). The match at 60 fps: ~92 s, at
  120: ~100 s, rallies, replays and cut-ins as at 30. The character select: 1 / 1 / 19 places at 30, 60 and 120.
- **Press edges** (`PAD_CLEAR`): a button's "just pressed" is there for one update; the fibers at 30 Hz get every edge
  since they last ran (otherwise a tap on the character select did nothing at 60 fps half the time).
- **Button histories** (`HISTORY_TICK`, `rt_history_tick`): the controls' history of button states (0x7000235fb0,
  0x70002219e0) counts frames per state, read as "pressed k frames ago": counted in 30 fps frames at every rate.
- **A 60 Hz system at 120 fps** (`TICKS60`, `rt_ticks60`): a system stepped in 60 Hz ticks once a frame (0x7000000e80)
  ran 120 ticks a second at 120 fps; one tick every other frame there now.
- **Slowdown at 60 fps:** the engine's own catch-up after a slow frame is off at 60 (its late-frame vsyncs broke the
  pacing, `FRAME_EXTRA`), so a frame over 16.7 ms played in slow motion. The runtime's catch-up (`catchup_frame`, as
  at 120) now runs at 60 too: extra logic updates for the time the frames took beyond their period.
- **Tools** (debug): `LARECOMP_FPS_FROM=<frame>` (with `LARECOMP_FPS=60/120`): 30 fps until that frame, then the rate
  tested - a route through the menus the same at every rate. `LARECOMP_FIBERS30=<mask>`: which fibers run at 30 Hz
  (bit = fiber id; 0: none, the v0.1.5 behaviour). `LARECOMP_SUB30=<mask>`: any of the logic update's four calls at
  30 Hz. `LARECOMP_LOGIC30=1`: the whole logic at 30 Hz (only the per-frame part at the display's rate).
  `LARECOMP_TASK30`, `LARECOMP_LOG_TASKS`: the task list's tasks. `LARECOMP_FNPROF=a:b,c:d`: several windows, one file
  each. `LARECOMP_WATCH_FROM=<frame>`: arm `LARECOMP_WATCH_ADDR` / `LARECOMP_WATCH_INPUT` from that frame. Earlier:
  `LARECOMP_COUNTER_SCAN_INT`, `LARECOMP_SCAN_WATCH`, `LARECOMP_WATCH_STATE`.

## Older CPUs: the game code built for the CPU it's set up on (v0.1.6)

- **Before:** the game code was always built for x86-64-v3 (AVX2, FMA, BMI2: Intel Haswell / 2013, AMD Excavator /
  Zen and newer); on an older CPU the game only said it needed AVX2.
- **Now** (`Makefile` GENLEVEL): the build asks the compiler what the CPU of the PC it runs on has (`-march=native`):
  x86-64-v3 where it has AVX2, FMA and BMI2, else x86-64-v2 (SSE4.2, 2008 on). The same game; on v2 the fused
  multiply-adds of the lifted code are done in software. Setup builds on the player's PC, so it fits that PC. The
  start-up check (`runtime/platform/windows.c` require_cpu, `GAME_CPU_V3`) asks for what was built. `make GENLEVEL=v2`
  picks one.
- **Checked:** a v2 build (`make B=build_v2 GENLEVEL=v2`) on this PC: the select route uncapped (no pacing) at
  ~440 fps against ~490 for v3 (10% slower), otherwise the same.

## 60 / 120 fps: smooth girls, 120 fps past the menu, texture filtering specks (after v0.1.6)

- **120 fps stopped at the main menu.** "Travel to New Zack Island" went back to the title. The vacation's mode ends
  when its page is unset (0xff), and the vacation's fiber (3) sets the first page on its first run; at 30 Hz under
  120 fps that first run could wait up to 3 updates, so the mode saw the old 0xff and ended. A fiber that was just
  started (`Fiber::Start`: slot flags bit 2 until its first `WaitFrames`) now runs on the next update whatever the
  30 Hz rhythm, as on the Switch (`rt_fiber_waiting`, which gets the slot's whole flags now). At 60 fps it only worked
  because the updates happened to line up.
- **The girls moved at 30 fps under 60 / 120 fps.** Their motion player (`SlotMotion::FiberMain`, fiber 0x11)
  advances the motions, runs their scripts, builds the bones and updates the characters' models; at 30 Hz with the
  other fibers (v0.1.6) all of that happened on every other / fourth frame. It stays at 30 Hz (its ticks raise
  one-tick flags and pulses the volleyball AI reads, count timers, record replays); on the updates in between, the
  runtime draws the girls part of a tick on (`motion_pass`, `tools/patches.py` MOTION_*):
  - it saves the motion player's data (what its functions' GOT entries point to, found when the code is generated:
    16 windows, 169 KB on 1.0.6);
  - runs `SlotMotion::UpdateSlot` for each girl the last tick advanced, with that part of a tick as the step, without
    the calls that reach past that data (script commands, starting, changing or ending a motion, the face's motion:
    made at the ticks only), moves her by her root's step (`ApplyRootMotion`), builds the bones and updates the
    characters (`FUN_7000003b60`) with the display's step;
  - then puts back every byte the motion part changed (what the characters' update changed stays: it runs every
    frame now, the tick's with the display's step too, so face and other animation layers keep their speed).
  The next tick starts from exactly what the last one left. Checked: consecutive frames of a close-up changed every
  frame instead of every other (8.9 / 0.1 / 8.8 -> 5.3 / 6.3 / 5.0, mean difference); a volleyball match at 60 fps
  1-1 after ~85 s, as at 30. `LARECOMP_MOTION_INTERP=0` turns it off.
- **Anisotropic filtering put blocky yellow-green specks** on the girls' hair and swimsuits. It was raised on every
  mipmapped filtered sampler, including two the game holds to one level (lod 0..0), through which it reads its own
  buffers. Those (and nearest-minified ones) are left alone now (`vk_sampler`). Found by bisecting the samplers by
  their min filter (`LARECOMP_ANISO_ONLY=<filter>`, test).
- `doaxmod model-import --as TARGET`: the result written as another model (a hairstyle built on another style's
  bones and physics, worn in this style's place).
- **The volleyball's ball moved at 30 Hz** under 60 / 120 fps. `Volleyball::Ball::Step` runs in the girls' fiber
  (8, at 30 Hz), and the rally camera aims at the ball every frame. So the ball, and the camera's aim, stepped on
  every other frame while the girls moved on every frame (a logged rally at 60 fps: the ball's z 5.3161, 5.3171,
  5.3171, 5.3177, 5.3177 ...; the girls and the camera's eye changed on every frame).
  - On the frames in between, the ball record is carried on by its last tick's step (it keeps the position from
    before the tick, +0xc) and its spin by its spin speed.
  - It is put back before any 30 Hz fiber runs, so the next tick starts from what the last one left (`ball_carry`,
    `tools/patches.py` BALL_STEP).
  - Not during the replay, and not across a jump (a new serve).
  - Checked: the ball's z then changed on every frame (5.3077, 5.3064, 5.3049, 5.3035 ...). The held ball stays in the
    server's hands.
  - `LARECOMP_MOTION_INTERP=0` turns it off with the girls' in-between motion.
- **Checked and left alone:** the girls' positions on the in-between frames. A first reading of the log suggested
  `ApplyRootMotion` overshot there. It didn't: the log had the position from before each tick, not after it. Read
  correctly, every frame is evenly spaced.
- **Test route note:** at 60 fps the `volleyball` route's presses don't always start the serve after a game ends;
  this happens the same way with `LARECOMP_MOTION_INTERP=0`.

## Body settings: a girl's bust size and breast physics (`runtime/hle/body_shape.c`, `runtime/mods.c`)

- **What a `body.ini` can set per girl** (a mod's, or the launcher's beside `larecomp.ini`, read last so it wins;
  the game applies them when it starts):
  - `bust_size`.
  - `bust_physics` (another girl's), with `bust_physics_amount`.
  - [MODDING.md](MODDING.md#body-shape-bust-butt-thighs-arms-calves-waist-and-jiggle) has the recipe.
- **Size:** `Character::Create` gets her files (`tools/patches.py` CHAR_FILES_COPIED / CHAR_FILES_COLLECTED: the
  resources at x21, her character id in w22). Before her character is built, `rt_body_files` multiplies her costume
  model's breast bones' inverse bind matrices (`OPT_Breast_*_base` and the 18 `OPT_ywrk_*` points per side) by a scale
  about each breast's base:
  - the full amount along the breast's direction, half across it;
  - the breast colliders on the bases shrink with them;
  - only in that model's memory, once per load (the changed matrices are remembered, so a character built again from
    the same files isn't scaled twice).
- **Checked in game:** Momiji at 0.6, in the beach close-up and the shower. The swimsuit follows the new shape, and the
  size stayed the same through the scenes that rebuild her.
- **Physics:** when the first RomFS image is laid out, `body_physics` (`runtime/mods.c`) lists the costume presets
  (`<pkg>/character/<girl>/costume/<design>/..._YWRK.ywrk`) of the girls involved in every package index (romfs and
  aoc), reads hers and the other girl's for each design, and serves hers blended as a mod file from memory:
  - the breast presets (0, 1, 6, 7) of each, every float by the amount; a field that isn't a float from the nearer;
  - a mod's own copy of either file is used in its place; a girl another takes from gives her original;
  - all 6688 of the game's are stored uncompressed, so they are read straight from the archives.
  - `doaxmod.py body` first wrote these files into the mod; it now only writes a `body.ini` to fill in and prints the
    girls' breast sizes.
  - Checked: Momiji like Marie Rose at 0.7, 586 costumes blended at start in 93 ms (22 stay hers), and a 340-second
    run through the beach and the shower with no trouble.
- **Found on the way** ([CHARACTER_MODELS.md](CHARACTER_MODELS.md#soft-parts-of-the-body)):
  - The soft body's presets are the costumes' `_YWRK.ywrk` files, per girl, not `<GIRL>_YWRK_PHYD.phyd` (that holds
    the body's collision bones).
  - Only Kasumi, Marie Rose and Honoka have costume models; the other eight wear Kasumi's.
  - The girls' character ids are 2-12 in package order (checked for Kokoro, Hitomi and Momiji by their hair models).

## Body settings: butt, thighs, arms, calves, waist, jiggle, and collision that follows the shape

- **New settings** (the launcher's Mods tab, two columns of sliders; or `body.ini`): `butt_size`, `thigh_size`,
  `arm_size`, `calf_size`, `waist_size`, and `bust_jiggle`, `butt_jiggle`, `thigh_jiggle`
  ([MODDING.md](MODDING.md#body-shape-bust-butt-thighs-arms-calves-waist-and-jiggle)).
- **Butt:** as the bust, on `OPT_Hips_*_base` and the 14 soft-body points each side (`OPT_ywrk_hip_*`). Arms, thighs,
  calves, waist: thicker or thinner across the bone (about the line from its joint to the next joint), with the
  twist and joint helper bones (`OPT_AS_*`) and the thighs' soft-body points (`OPT_ywrk_leg_*`); never longer.
- **Jiggle:** a bigger bust didn't seem to move. Scaling the skin moves no soft-body point, so it swung as far as
  before, which on a bigger bust reads as stiff. `Character::ApplySoftBodyParams` gives each point one value from the
  costume's `_YWRK.ywrk` preset (floats 4-21), and the game's own softness levels multiply exactly those
  (`FUN_7000260ec0`'s per-girl table: level 2 x0, 5 x1, 8 about x1.5; Kasumi's breasts 0.07-0.44, Marie Rose's
  0.03-0.10). `runtime/mods.c` now scales them per region when the game starts (breasts presets 0, 1, 6, 7; bottom
  2, 3, 8, 9; thighs 4, 5, 10, 11) by size times jiggle, as the levels do (a factor of 1 or more leaves values under
  0.1 alone).
- **Collision:** the arms sank into a bigger bust, and the hair too. The soft body keeps its points out of the arms
  and body (the girl's `_YWRK_PHYD.phyd`, `YWCL` shapes on `MOT08_LeftArm`, the hands ...), but the points stayed at
  the old size. They now move with the shape: bind world (`GlblMtx`) and local (`HieLay`) moved by d, inverse bind
  A T(-d) B, so the skin stays where the shape puts it and the arms meet the new surface. The hair's colliders
  (`ACSCLS` table 1 ellipsoids) follow too: a bigger bust's by its size (a smaller one's by its volume, as before),
  the arms', thighs' and waist's by their thickness.
- **Not covered:** arms crossing in front of the chest in an animation can still meet a much bigger bust (only the
  breasts give way); thicker arms don't widen the arms' own `YWCL` shapes.
- **Checked in game:** Kasumi at bust 1.40, bust jiggle 1.5, butt 1.25, thighs 1.15, arms 1.20, calves 1.10, waist
  0.85, in the opening close-up, on the pier and in the evening scenes: the shapes hold, the edges blend, no torn
  skin. 608 costumes' presets scaled at start in 60-210 ms. Movement and arm contact need watching in motion.
- `LARECOMP_BODY_INI=<file>`: a body.ini to read in place of the launcher's (test runs).

## Body settings: breast colliders, bust weight and the breasts meeting, for a bigger bust

- **Hair on a bigger bust:** many costumes' collider tables (ACSCLS table 1) have no breast colliders. A bigger bust
  now gives the costume a copy of the table with one ellipsoid collider on each breast base (kind 2, the base's index
  and name hash), its half sizes from where the soft-body points now are in the base's frame, 15 % more for the skin
  beyond them. The copy is in guest memory above the game's heap (0x7310000000, 128 MB, committed as needed: the
  chunk's table offsets are 32 bits from it) and the chunk's table 1 offset points at it. A record's bone is found by
  its index (`FUN_70005b76d0`). Seen in game (`LARECOMP_BODY_PROBE`): Kasumi at bust 1.8 has 12 colliders instead of
  10, the two new ones on the breasts, 18 cm front to back.
- **Arm spacing** (tried, then taken out): turning the upper arms out about the shoulder and moving the forearms with
  the elbow stretched and twisted the arms in poses; a bust should give way to the arms, not the arms to it.
- **Bust weight** (`bust_weight`, 0.5 to 3, the Mods tab's slider), along gravity: a first version sheared the breasts
  down in the body's frame, which hung them toward her feet whatever her pose. Now `tools/patches.py`
  SOFT_BODY_UPDATE replaces the tail call in `FUN_7000009ed0` (the character's soft body, +0x78, and its bone matrices,
  +0x20, to `FUN_7000268020`, whose last step `FUN_70002677a0` writes the points' world matrices) with the update and
  then `rt_body_soft_post`: each breast point down the world's -y by k (its reach along the breast's direction now),
  k = 0.35 (weight - 1), at most 0.7 (lighter: up, at most 0.3). A girl is known by her resources record; her points by
  the costume model's node numbers, noted after her size is put on.
  - Checked: moving every bone up 0.5 there lifts her whole body on screen, and moving only the 36 breast points lifts
    the breasts: the screen draws these matrices as left.
  - Nothing is taken back before the next update: the game writes the points' matrices afresh every frame before it
    (none was as it was left). A first version subtracted last frame's change there, which the physics took as motion
    and ran away (the breasts drifting further each frame).
- **Breasts meet** (`bust_contact`, 0 to 1, default 1; with a bigger or heavier bust): in the same place, after the
  weight. Each point's rest place is its base's matrix now times where the point sits from its base in the bind pose
  (after the size). How far a breast has moved toward the other (along the line between the two bases) is the mean of
  its points' moves from rest, weighted 0.25 (outermost) to 1 (innermost, by bind-pose distance from her middle). When
  the two add up to more than 5 mm, both are pushed back apart by the excess, half each, each point by 0.5 to 1 of
  that (the inner ones most), times `bust_contact`. Swaying the same way adds up to nothing. Seen on Kasumi at bust
  2.0: the bounce swings each breast up to 3.7 cm toward the other; at bust 1.8, weight 2, the log says they moved
  toward each other up to 4.5 cm and were pushed apart in 370 of 600 updates.
- Measured on Kasumi's body: the breast skin reaches 9.7 cm in front of its base and 9 cm to each side; the soft-body
  points 8.4 cm.

## Body settings: collision and physics that follow the size, a Save button, bigger ranges

- **What the user saw:** arms and hair going through a bigger bust, and Marie Rose's settings not showing.
- **Hair:** the live colliders (`LARECOMP_BODY_PROBE=1` logs the accessory physics' colliders every 20 s:
  `*0x7000a8c900` +0xc0, 0x7f0 bytes each, an axes-times-size matrix first) showed that many costumes have no breast
  colliders: the hair rests on the chest's ellipsoid, which a bigger bust grew through. The chest's (and the hips', for
  the butt) now reaches out by how much further the furthest soft-body point goes (`COL_PUSH`: half size added, centre
  moved half, the far side staying), where its rotation keeps it on the bone's axes. Seen in game: the chest
  collider's front-back size 0.118 -> 0.138 at bust 1.5.
- **Soft body:** `FUN_7000261320` (its step) reads a second preset block (+0x148, 0x1bc bytes, converted per level by
  `FUN_7000262b80`) with five arrays of a float per point; the one at +0x1e4 is computed and capped at 0.06 (how far a
  point may swing, it seems), the one at +0x22c holds 2-20 mm. Both now scale with the part's size, so a bigger bust
  swings and is met in proportion; jiggle now only sets how lively (it no longer also multiplies by the size).
- **Not found yet:** what makes the breasts give way to the arms; arms crossing in front of a much bigger bust can still
  go through it.
- **Marie Rose:** the character select screen doesn't build the girls with `Character::Create` (Kasumi's first build
  in a run is after it), so body settings show only from the scenes after it.
- **Launcher:** Save writes every tab (larecomp.ini, body.ini, mods on or off) without starting the game; bust and butt
  up to 2.50, thighs, arms and calves 2.00, waist 1.80.

## Launcher: Game, Controls and Mods tabs (`launcher/launcher.c`)

- **Tabs:** the window is wider (860 x 600 at 100 %) with three tabs; Ctrl+Tab and Ctrl+Shift+Tab switch. Game holds
  the display and graphics settings in two columns. Play saves every tab; Quit saves nothing.
- **Controls** (in place of the Controls choice and the Controller buttons window):
  - a controller drawn in the launcher's colors (face buttons by position, shoulders, triggers, sticks, d-pad,
    Start and Back) with the game button on each of its buttons;
  - pointing at one names it, clicking one opens that game button's list, and a focused list lights its button pink;
  - each game button's list (named for Xbox and PlayStation, as before; picking a button another game button is on
    swaps the two);
  - two templates: Default (the Switch layout) and Swapped (A and B, X and Y);
  - game buttons on buttons the picture doesn't have (Elite paddles, Share, ...) are listed under it.
  - It writes `bind_pad_*` lines as before, only for buttons off their default.
- **Mods:**
  - each folder in `mods/`, and On: Play renames a mod turned on or off (an off mod's name starts with `_`);
  - which girls its files change, what its own `body.ini` sets, and Open folder;
  - body settings per girl, whatever mods are on: Bust size (a slider, 0.30 to 1.50), "Like another girl's" (her size
    on this girl's body), Bust physics (her own, or like another girl's) and how much like hers, and "As she is";
  - the sliders show what she gets in the game (the mods' settings, then the launcher's), and a note names the mods
    that set her too.
  - Play saves them in `body.ini` beside `larecomp.ini` (`runtime/hle/body_shape.c` reads it after every mod's), only
    the settings that differ from what the mods give her ("As she is" over a mod writes `bust_physics = none`);
    comments and other lines stay.
  - The first version saved them in the selected mod's `body.ini`; it opened on the first mod, which was off, so a
    Kasumi setting never reached the game.
- **Sliders** are the launcher's own control: the system's themed trackbar lost what its custom draw painted.
- **Checked** in a scratch folder (a stand-in game exe), driven by window messages: the files written (a new
  `body.ini`, one edited with its comments kept, a mod turned on by its rename, the bindings in larecomp.ini with its
  other lines kept), and captures of each tab at 100 % and 150 % scaling.

## Launcher: the game's look, and controller buttons

- **Look:** a sky-blue header with the game's cover art and title logo, over a blue-to-cyan body, white labels, a
  pink Play button. Nothing of the game's is built into the launcher: at start it loads
  `extracted/control/icon_AmericanEnglish.dat` (the cover) and `build/launcher_art/logo.png`, which `make` cuts from
  the title screen's layout in your own game files (`tools/make_launcher_art.py`, texture `doax3_logo_ps4`). Without
  them the title is text. Loaded with WIC, drawn with AlphaBlend.
- **Controller buttons:** "Customize..." next to Controls opens a panel with each game button and the controller
  button it's on (named for Xbox / PlayStation / Switch), and two presets: Switch layout (the original) and Swapped
  (A and B, X and Y). Picking a button another input uses swaps the two. It writes `bind_pad_*` lines only for
  buttons off their default (runtime/hle/bindings.c). Keyboard keys stay in the game's menu (F1).
- **Frame rate warning:** a line under Frame rate says what the choice does, in orange for 60 and 120
  ("Experimental: the Volleyball AI may not work as intended, and some menus scroll faster"). It follows the
  pointer over the open list, and resting the pointer on the box shows the full explanation as a tooltip.

## Texture filtering (anisotropic)

- **Setting:** `anisotropy` in larecomp.ini (0: the game's own, 2, 4, 8, 16), Video > Rendering > "Texture
  filtering", the launcher and RecompLauncher. At least that much on every mipmapped, linearly filtered sampler; not
  on depth compares (shadows). Applies at the next start (samplers are made once).
- **The game makes about 10 samplers**, shared by all its textures, and asks for no anisotropy on any. 7 are
  mipmapped and get it. 16x visibly sharpens the sea and the sand at a distance. `LARECOMP_LOG_SAMPLERS=1` logs each.

## Settings that don't apply here, removed

- **Internal resolution** (LAS Recomp's render scale): any scale but 1x breaks DOAX3's deferred lighting (its compute
  pass stores at the game's pixel coordinates). The Video tab no longer shows it, and the game uses 1x whatever an
  older larecomp.ini says (`settings_resolve_defaults`). The 3D resolution is the sharpness setting.
- **Aspect ratio:** it had no effect (DOAX3 keeps 16:9: `widescreen_allowed`), so it's gone from the Video tab.
- Every PC setting that works is also in the launcher and RecompLauncher: frame rate, 3D resolution, anti-aliasing,
  texture filtering, upscaler, Show FPS, background controller input, controls.

## Worker handshake race (`tools/patches.py` WORKER_KICK, `rt_worker_state`)

- **Symptom:** the game froze now and then with a `[hang]` dump: the main thread and a worker (priority 34) each in one
  wait since the same millisecond. Twice in about 30 runs, both in light scenes (frames 3469 and 3734, during the
  opening movie and just after it), before any girl's model is loaded.
- **Cause:** the worker (1.0.6 0x70002bebd0) runs its job in state 1, sets state 2 and signals the main thread, then
  sets state 3 and waits. The main thread's kick (0x70002bec70) wakes it only in state 3, and then waits for it
  regardless. Woken in the gap, the main thread can kick too early: skipped, and both sleep for ever. On the Switch the
  worker's higher priority on a shared core closes that gap.
- **Fix:** the kick's load of the state asks the runtime, which waits out state 2 (at most 1 s). The log's
  `[game] worker handshake` line counts how often it did.

## Boot deadlock on a busy PC (`runtime/kernel.c` svcStartThread)

- **Symptom:** the game hung at boot (black window, no frames; the `[hang]` dump after 30 s) in 3 of 4 starts while
  another build kept the CPU at 90-100%. Never seen on an idle PC.
- **Cause:** a race in the game's boot code. It starts a worker thread (0x70002beb30; priority 34, above the main
  thread's 44), later kicks it (0x70002bec70) only if the worker already waits for work (its state 3), then waits for
  the job's done event unconditionally (0x70002becf0). On the Switch the worker preempts the main thread when it
  starts and is waiting long before the kick. Here it's a host thread, which a busy PC can start 5-90 ms late: the
  kick was skipped and both threads waited forever.
- **Fix:** svcStartThread of a thread with a higher priority than the starter's waits until the new thread first
  blocks, sleeps, yields or ends (`KThread.settled`, set in `kwait`, SleepThread and thread exit), for at most 1 s,
  as the Switch scheduler would run it. Starts that take over 5 ms are logged (`[kernel] StartThread ...: first wait
  after N ms`). Result at 90-100% CPU load: 8 of 8 boots (slowest start 87 ms; a 50 ms limit, tried first, still
  lost one boot).

## Frame pacing fixes (`runtime/nvn/pace.c`, `objects.c` present_pace)

- **Inflated GPU timings:** LAS Recomp's pacer holds that game in its 30 fps mode when frames don't fit 60, and while
  holding reports each frame's timing counters as at least 20 ms. DOAX3 has no such hold (its frame controller isn't
  wired to the pacer: `hold_30` does nothing), yet the boot's slow frames set the pacer's slow state in every run
  and nothing released it (it counts gameplay frames only, and `game_in_play` isn't located). So every frame's GPU
  time read as 20 ms or more, and in the 60 fps mode the game judged every frame late. The counters are now only
  changed while the hold is actually applied.
- **Display refresh:** the window's display refresh rate is read once a second (device.c `display_rate_poll`, logged
  as `[vk] display WxH at N Hz`). A display within 0.5% of 60 Hz (59.94 Hz) sets the present grid, so mailbox
  presentation doesn't drop a frame every ~17 s; the game then runs 0.1% slower. 60.000 Hz displays are unchanged.

## Shadows checked (no change needed)

The sun shadows looked missing on the beach, so the shadow path was traced: the game draws a 3104x6144 depth atlas
(two 3072x3072 cascades) holding the characters only, and the deferred lighting shader (compute, `sampler2DShadow`
t7, less-or-equal) clamps the reference to 1 for ground beyond the cascade's far plane, so the ground is shadowed
exactly where a character is in the map. A split-screen test (`LARECOMP_SHADOW_VIS=1`) showed the test and the
normal image agree: character shadows on the sand, self-shadowing on swimsuits, shadows in the hotel room. With a
high sun they fall mostly under and behind the girls in the volleyball serve view. Umbrellas, trees and furniture
cast none, by the game's design (they aren't in the map).

## Names

The window title, message boxes, crash explanations and menu texts said "LAS Recomp"; they say "DOAX3 Recomp" now,
and the wrong-version explanation names 1.0.0 and 1.0.6.

## Launcher, display modes, window title and icon

- **Window title:** the game's own name from `extracted/control/control.nacp` (`rt_game_title`, `runtime/paths.c`),
  "DEAD OR ALIVE Xtreme 3 Scarlet".
- **Icon:** the game's own Switch home menu icon, `extracted/control/icon_AmericanEnglish.dat` (a 256x256 JPEG).
  - `tools/make_icon.py` turns it into `build/icon/icon.ico` (16 to 256 px).
  - Below 32 px the whole picture is a blur, so those sizes show a crop of the cover girl's face (Misaki).
  - The Makefile builds it into both exes. SDL gives the game's window the exe's first icon.
  - It comes from your game files, so it is never committed. Without `extracted/control`, the exes have no icon.
- **Display modes** (`rt_window_apply_mode`, `runtime/paths.c`): `fullscreen_mode = borderless | exclusive` in
  larecomp.ini. Borderless is SDL's desktop fullscreen, as before. Exclusive switches the display to the closest
  mode to `window_size` at the desktop's refresh rate. `window_size = WxH | auto` is also the window's size when
  windowed (auto: 1280x720). F11 and the menu's Fullscreen toggle use the chosen mode.
- **Launcher** (`launcher/launcher.c`, `build/DOAX3Launcher.exe`, Win32, no extra libraries): display mode,
  resolution (the primary display's modes) and 3D resolution, written to larecomp.ini (other lines kept), then
  starts the game as `tools/run.sh` does: the data folder (the one with `extracted/`, next to the launcher or above
  it) as working directory and `LARECOMP_DATA_ROOT`, MSYS2's `ucrt64/bin` on PATH when SDL3.dll isn't next to the
  program, and the log in `doax3.log`.

## Mods (`runtime/mods.c`, `tools/mods/`, [MODDING.md](MODDING.md))

- **Loader:** `mods/<mod>/romfs/<path>` replaces the game file `<path>`, named as the packages' indexes name it.
  - `romfs_layout` asks `mods_source` for each file; a replaced file is served from pieces (`RomfsSource`: memory, a
    host file range, or zeros) by `layout_pread` (`romfs_source_read`).
  - **Loose files** (sound banks, movies) are served from the mod in their place.
  - **Packed files:** the package's `package_data.bin` and `data_*.lnk` are served as changed copies. The replacement
    is appended past the archive's end (at its 0x800 alignment), and the archive entry and index record are pointed at
    it, uncompressed, with the new size.
  - The base game and the DLC images both go through it.
  - Folders starting with `_` are off; `LARECOMP_NO_MODS=1` turns all mods off.
- **Formats** (`tools/mods/`):
  - `ktpkg.py`: packages (`pkginfo` / `btif` index; `b0xx` archives with 16-byte-aligned zlib chunks).
  - `ktss.py`: voice banks (G1L of KTSS streams: Opus in Nintendo's packet framing, 48 kHz mono, 240-byte CBR
    frames, restricted-low-delay mode). Encoded with libopus via ctypes.
  - `kwb.py`: short-sound banks (XWSFILE of KWBN banks: DSP ADPCM, 24 kHz, variants). Our own DSP encoder:
    least-squares predictors clustered into 8.
  - All 16039 index records match their archive entries, and the G1L / XWS files rebuild byte-identical.
- **Checked in game:**
  - Kasumi with Ayane's face and hair, from four packed files: the arrival scene shows them.
  - Her short sounds (all three banks) replaced by a tone: it plays during the volleyball match. A first version
    appended the new data and kept the old (banks 1.8x and 2.1x their size): the game then hung on the loading
    screen before the match. `kwb.Bank.build` now lays the data out again, and the tool warns about banks bigger
    than the game's. A dialogue bank 1.6x its size (4 s per line) played in full: those are streamed.
  - Kasumi's 391 voice lines replaced by a 1.5 kHz tone: a capture with `LARECOMP_AUDIO_WAV` has the tone exactly
    where she speaks.
- **Models** (`tools/mods/tmc.py`; `model-export` / `model-import`):
  - TMC + TMCL go to glTF, with every submesh as a mesh `<object>#<n>`, the node tree as a skin, 12 weights and
    textures as PNG. The way back rebuilds the vertex / index buffers, the counts, ranges and layouts, and the bounds
    of the replaced submeshes.
  - Round trip: the TMC comes back byte-identical, the vertices to half-float rounding, and it looks the same in game
    (hair and face).
  - Unrigged or foreign-rigged meshes copy their weights from the nearest original vertex; `--rigid` puts every
    vertex on the main bone instead.
  - `--only-given` hides the other submeshes with one zero-area triangle each.
  - Checked in game: a generated custom mesh with its own texture replaced Kasumi's hair.
  - **Found on the way:**
    - The GeoDecl stride is at +0x44.
    - An empty submesh crashes the game.
    - Triangles are counter-clockwise, as in glTF.
    - Textures are linear G1T: BC1, or BC7 (encoded here in mode 6).
  - **Corrected:** the first version read node names from +0x60, not +0x50, which cut long names short;
    the "hair reuses bone names" note above came from that. It also missed two things:
    - each object's bone palette: a vertex's bone numbers index it, not the node list;
    - each object's node space: the face's eyes are stored relative to the head, the hair's depth layer in the face
      model's space.
    Export and import now handle all three (the round trip keeps every vertex's bones and weights exactly). Exports
    made with the old version must be redone.
  - `--hide` (named submeshes), `--material-colour` (a material's `MtrCol` row: a hair's highlight),
    `--hold-from-colour` (vertex colour red = held to the head). Weight copy blends the 4 nearest original vertices by
    distance and uses a k-d tree (a 45k-vertex hair model: 10 min -> 24 s).
  - The BC7 encoder also tries mode 5 for blocks whose alpha varies, which keeps alpha's own endpoints and indices,
    and keeps whichever is closer. Checked bit-exact against `texture2ddecoder`.
  - `tmc.move_bones`: moves bones in HieLay, GlblMtx, BnOfsMtx and the `ACSCLS` physics chains (start, node offsets,
    spring rest lengths; the format is in its docstring). `tmc.chain_swing`: a chain's per-node swing values. `model-import` takes a `parts_hook` for scripts that change
    the parts and skeleton before the rebuild.
  - `--copy-k N`: how many original vertices a copied weight blends (default 4).
  - `--alpha-coverage CUT`: smaller mips keep the full-size image's coverage above an alpha cutoff.
    `--material-colour` takes `*` to keep a value.
  - `ktss.py` also looks for libopus in MSYS2: a static `ffmpeg` first on the PATH has no DLLs beside it, which stopped
    `voice-unpack` and `voice-pack`.
  - `WGT_zdmodel` (the hair's depth layer): fewer, double, hidden and fully replaced versions (up to 9,267 vertices)
    all load through the relax and menus routes. The hang blamed on replacing it was the worker handshake race ("Worker
    handshake race" below).
- **UI textures** (`tools/mods/kslt.py`; `portraits-*`, `kslt-*`): KSLT packs, linear BC3 / RGBA8, with a BC3 encoder.
  Each girl's character select picture (`chara_<girl>`), profile card (`status_<girl>`), icons and name plates are
  replaced in every language's pack. Checked in game on the character select.
- **Not done:** subtitles, body skin (`TEXCHL`), new materials and submeshes.

## Debugging switches (environment variables)

Added in this project:

| Variable | Effect |
|---|---|
| `LARECOMP_FENCE_ALL=1` | full GPU waits (turns off the observable-work shortcut) |
| `LARECOMP_SLOW_FRAME_ALL=1` | log slow frames during loads too |
| `LARECOMP_TRACE_CALL=<hex>` | log a guest function's arguments x0–x3 and result (first 200 calls) |
| `LARECOMP_AUX_DRY=1` | leave the Aux effect out |
| `LARECOMP_AUX_LOG=1` | Aux send/return levels every 1000 frames |
| `LARECOMP_AUX_CAPTURE=<file>` | save Aux channel 0's input and output |
| `LARECOMP_AUDIO_LEVEL=1` | output peak and RMS about once a second |
| `LARECOMP_LOG_STAGES=<n>` | the first n shaders' headers and their stages |
| `LARECOMP_SHADER_SURVEY=1` | log every unknown instruction in a shader |
| `LARECOMP_PRESENT_PACE=0` | uncapped frame rate |
| `LARECOMP_3D_RES=<w>x<h>` | the 3D scene at that size (overrides the setting) |
| `LARECOMP_FPS=30` / `60` | the game's 30 or 60 fps mode (overrides the setting); `tools/route.sh` doubles its press timings at 60 |
| `LARECOMP_SHADOW_VIS=1` | (with `LARECOMP_SHADER_PREP=0`) left half of the lit image shows the shadow test: red shadowed, magenta beyond the far plane, green / blue / grey lit by how far in front |
| `LARECOMP_NO_SHADOW=1` / `2` / `3` | shadow compares always pass / never pass / inverted |
| `LARECOMP_COPY_TRACE=<frame>` | now also saves the shadow map's depth (`build/shadow_<frame>_t<slot>.pgm`) and logs its size and scales |
| `LARECOMP_SHOT_SIZE=<w>x<h>` | game-image screenshots drawn at that size (with `LARECOMP_SCREENSHOT_EVERY` / `_FROM`: long series that stay small) |
| `LARECOMP_DUMP_MEM=<frame>:<lo>:<hi>:<file>` | guest memory [lo, hi) (hex) saved at that frame |
| `LARECOMP_COUNTER_SCAN=<frame>` | logs the floats that change by the same step every frame from that frame on (`[scan]`), in the game's globals and heap |
| `LARECOMP_PEEK=<addr>[,...]` | logs those floats whenever they change (`[peek]`) |
| `LARECOMP_NO_MODS=1` | no mods (nor body settings) |
| `LARECOMP_BODY_INI=<file>` | that body.ini in place of the launcher's (test runs) |
| `LARECOMP_BODY_PROBE=1` | log the accessory physics' colliders (hair) every 20 s |

Inherited from LAS Recomp and used here:

| Variable | Effect |
|---|---|
| `LARECOMP_INPUT="frame:BUTTON:frames,..."` | scripted input |
| `LARECOMP_SHOT_AT=<frames>` | screenshots at those frames, to `build/frames/` |
| `LARECOMP_COPY_TRACE=<frame>` | per-draw and per-copy trace of one frame |
| `LARECOMP_SAVE_MSL=1` | keep translated shaders in `build/glsl/` |
| `LARECOMP_FNPROF=<from>:<to>` | guest function-entry profile |
| `LARECOMP_SHADER_PREP=0` | no shader preparation at startup |
| `LARECOMP_SYNC_SHADERS=1` | compile on first use, blocking, instead of skipping draws for a few frames |

## Status (2026-10-31)

- **Plays at 30 fps at 1920x1080.** The game renders 3D at 1600x900 and the UI at 1080p.
- **Scenes checked correct:**
  - title and menus;
  - island map, hotel, casino, closet, Owner Shop;
  - beach cutscenes, relax/shower, sunset, night;
  - a full Beach Volleyball match;
  - the opening movie.
- **Performance** (RTX 3060):
  - GPU ~7 ms per frame;
  - render thread ~2 ms per frame;
  - about half a CPU core.
- **Stability:** a 10.7-minute random-input run had no crash.
- **First-visit shader pop-in** is by design (`LARECOMP_SYNC_SHADERS=1` stalls instead).

## Known gaps and next steps

- **Load stalls:** about 60 ms per scene change remains (on its loading screen). Measured at 60 fps: ~345 texture
  uploads (24 MB) in ~345 GPU submissions, because the game waits for the GPU after each staging copy. Render-thread
  CPU is 22-29 ms of that and GPU waits are 15-25 ms. Deswizzling is only ~2 ms of it; it now copies one GOB at a time
  (`runtime/nvn/tegra.c`, 1.6x; `LARECOMP_TEGRA_SLOW=1`: the old path). Batching those submissions is the next step.
- **`game_in_play` and level hooks** are not located, so slow-frame classification treats every frame as a load.
- **Stencil sampling in graphics stages:** not handled. None of the game's graphics shaders use it so far, and the
  translator logs it if one does.
- **Textures:** 8- and 16-bit integer formats; swkbd (software keyboard) offsets.
- **Untested:** the volleyball serve with scripted input; other activities (Beach Flags, pool).
- **DLC in play:** check the DLC characters (Honoka, Marie Rose) and swimsuits in play; so far only mounting and the
  game's boot-time reads are confirmed.
