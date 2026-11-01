# DOAX3 Recomp

A work-in-progress static recompilation of *DEAD OR ALIVE Xtreme 3 Scarlet* (Nintendo Switch, 1.0.0 and the 1.0.6 update, with DLC) into a native
Windows x86-64 program, built on [LAS Recomp](https://github.com/peiikai/LASRecomp)'s recompiler and Switch runtime.

**Status: playable, work in progress.** The game boots and runs at its own 30 fps at the game's 1920x1080: title,
menus, the island map, hotel, casino, Photo Paradise and the 3D scenes render (deferred lighting included), audio
plays (the game's own Aux effect included) and the opening movie plays (it runs when a new vacation starts on a
fresh save). Known gap: stencil sampling is only handled in compute shaders (none of the game's graphics shaders use
it).
A dump of the base application alone is the *Basic Free Edition*, as on the console; the DLC you own (characters,
swimsuits, the casino membership) are read from `extracted/aoc/`. What has been changed from LAS
Recomp, the debugging switches and the open work: [docs/CHANGES.md](docs/CHANGES.md).

No game files are included or ever committed. You need your own legally obtained copy of the game and the keys from
your own console.

## How it works

The game's ARM64 code (`exefs`) is translated to C by `tools/gen.py` and compiled into `build/doaxgame.dll`. The
runtime (`runtime/`) provides what the Switch would: a small Horizon kernel (threads, memory, sync), the OS services
(file system, HID, audio, time, ...), and NVN (the Switch's graphics API) on Vulkan, with a Maxwell shader translator.

| exefs module | What it is | Here |
|---|---|---|
| `main` | the game (Koei Tecmo engine, NintendoSDK 6.4.2) | recompiled: `generated/game` |
| `sdk` | NintendoSDK | recompiled: `generated/sdk`; its system calls and services are host code |
| `subsdk0` | NVIDIA glslc (runtime GLSL compiler) | recompiled: `generated/glslc` |
| `subsdk1` | Nintendo multimedia (movie playback) | not loaded: `movie::` is host code on FFmpeg (`runtime/hle/movie.c`) |

## Setup (Windows)

1. MSYS2 with UCRT64 clang, make, SDL3, FFmpeg, Vulkan, shaderc (see LAS Recomp's `tools/setup_windows.ps1`), and
   Python 3.12 with a venv: `py -V:3.12 -m venv .venv` and
   `.venv/Scripts/python -m pip install unicorn==2.1.4 capstone==5.0.9 lz4==4.4.5 pycryptodome==3.23.0`.
2. Extract your game: `.venv/Scripts/python -I tools/nsp_extract.py "<game>.nsp" "<your prod.keys>" extracted`
   - Update (1.0.6): `.venv/Scripts/python -I tools/nsp_extract.py "<update>.nsp" "<your prod.keys>" extracted
     --base "<game>.nsp"`. It replaces `exefs/` and `control/`; its RomFS is the base game's plus packages 01-06.
     (To save 4.4 GB, extract the update with `--no-romfs` into a scratch folder and move only `romfs/01`-`06` over.)
   - DLC: the same command for each DLC NSP, into `extracted` (they go to `extracted/aoc/<id>/`).
3. In Git Bash with `export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"`:
   `make gen` (recompile the game code into `generated/`), then `make -j16`.
4. Run: `tools/run.sh` (log: `doax3.log`).

## Credits

- **LAS Recomp** by Peiikai and contributors (MIT License, `LICENSE.LASRecomp`): the recompiler (`tools/gen.py`,
  `lifter.py`, `nso.py`, `simd_c.py`, ...) and the runtime this project started from.
- Third-party components keep their own licenses (`third_party/`): Dear ImGui (MIT), SMAA (MIT), the ported audio
  renderer and shader decoder references (MIT, `third_party/ryujinx/LICENSE.txt`), fonts (SIL OFL 1.1 / DejaVu).
- Libraries: SDL 3, FFmpeg (LGPL, dynamically linked), Vulkan Memory Allocator, shaderc; the generator uses Capstone,
  Unicorn and python-lz4.
- [Switchbrew](https://switchbrew.org) documentation (NCA/PFS0/RomFS formats for `tools/nsp_extract.py`).

*DEAD OR ALIVE Xtreme 3* is a trademark of Koei Tecmo. This project is not affiliated with Koei Tecmo or Nintendo.
