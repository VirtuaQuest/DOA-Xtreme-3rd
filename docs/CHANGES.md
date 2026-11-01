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

Fresh-save routes move `save/` to `save.route-backup/` and put it back afterwards.

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
- **Setting:** `fps60` in larecomp.ini and Video > Rendering > "60 fps". It applies at the next start. Off by default.
  `LARECOMP_FPS=30/60` overrides it (test).
- **Result** (RTX 3060, 1440p 3D): a locked 60 fps through the menus and a 7-minute run (186 of ~200 two-second
  windows at 60.0; dips only at loads). The game uncapped runs at 230-250 fps, worst frame 8 ms.
- **Known difference:** some UI timers count frames, not game time. The character select's rotating backgrounds
  change about twice as often. That's likely the PS4 game's own 60 fps pace. Engine-timed content (the boot logos)
  takes the same time as at 30 fps.
- `LARECOMP_LOG_INTERVAL=1` now logs the guest call stack of each present-interval change.

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
