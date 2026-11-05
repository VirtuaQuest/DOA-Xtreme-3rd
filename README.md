# DOAX3 Recomp

A static recompilation of *DEAD OR ALIVE Xtreme 3 Scarlet* (Nintendo Switch: version 1.0.0 or the 1.0.6 update, with
DLC) into a native Windows x86-64 program. It's built on [LAS Recomp](https://github.com/peiikai/LASRecomp)'s recompiler
and Switch runtime.

**This project contains no game files.** You need your own copy of the game, dumped from your own Switch, and the keys
from that console. Everything the game needs is made from those on your PC, and nothing is uploaded anywhere.

**Status: playable, work in progress.** The title, menus, island, hotel, casino, Photo Paradise and the 3D scenes run,
with sound, the opening movie, an optional 60 fps mode and a selectable 3D resolution. See
[docs/CHANGES.md](docs/CHANGES.md) for what has changed from LAS Recomp and what's still open.

## Quick start (Windows)

1. Get this project: **Code > Download ZIP** on GitHub and unzip it, or `git clone` it. A short folder path, such as
   `C:\Games\DOAX3Recomp`, avoids trouble.
2. Double-click **`Setup.bat`**. A window opens:
   1. **Build tools:** press *Install build tools* if it asks. This installs MSYS2 (compilers and libraries) and
      Python 3.12, which are free and a few GB in all. Windows may ask for permission.
   2. **Your game:** choose the folder with your game's `.nsp` files and your `prod.keys`. Setup reads the files
      themselves, so names don't matter, and tells you what it found: base game, update and DLC.
   3. **Build:** press *Build the game*. It extracts your game files, recompiles the game's code, and compiles it.
      Allow about 20 GB of free space and about an hour (54 minutes on a 10-core PC, with all DLC). It runs in the
      background.
3. Press **Play** in Setup, or double-click **`Play.bat`** later. Choose windowed, borderless or fullscreen, the
   resolution, the 3D resolution and 60 fps, then *Play*.

For the manual route, details and troubleshooting, see [docs/SETUP.md](docs/SETUP.md).

## What you need

- **PC:** Windows 10 or 11, 64-bit; a CPU with AVX2 (Intel 4th generation / AMD Ryzen or newer); a GPU with Vulkan
  drivers; about 20 GB of free disk space.
- **Your game, as `.nsp` files dumped from your own console:** the base game, which is required. The 1.0.6 update and
  the DLC are optional. The base game alone is the *Basic Free Edition*, as on the console, and DLC you own unlocks
  characters, swimsuits and the casino membership.
- **Your console's keys:** a `prod.keys` dumped from the same console. Its keys must be at least as new as your game
  files.

## Playing

- **Controls:** keyboard or any controller SDL knows. **F1**, or **Back + Start** / the **Guide** button on a
  controller, opens the recomp's menu (controls, display, audio). **F11** switches fullscreen.
- **Settings** are saved in `larecomp.ini`, and your game saves in `save/`, both in the project folder.
- **The log** is `doax3.log` in the project folder. Include it when you report a problem.

## Mods

Mods replace the game's own files (models, textures, voice lines) from `mods/<mod>/romfs/`, without touching your
extracted files. The mod tool (`tools/mods/doaxmod.py`) unpacks game files, rebuilds models from glTF, and replaces
portraits and voice lines. See [docs/MODDING.md](docs/MODDING.md). No mods are included.

## How it works

The game's ARM64 code (`exefs`) is translated to C by `tools/gen.py` and compiled into `build/doaxgame.dll`. The runtime
(`runtime/`) provides what the Switch would: a small Horizon kernel (threads, memory, sync), the OS services (file
system, input, audio, time, ...), and NVN, the Switch's graphics API, on Vulkan with a Maxwell shader translator.

| exefs module | What it is | Here |
|---|---|---|
| `main` | the game (Koei Tecmo engine, NintendoSDK 6.4.2) | recompiled: `generated/game` |
| `sdk` | NintendoSDK | recompiled: `generated/sdk`; its system calls and services are host code |
| `subsdk0` | NVIDIA glslc (runtime GLSL compiler) | recompiled: `generated/glslc` |
| `subsdk1` | Nintendo multimedia (movie playback) | not loaded: `movie::` is host code on FFmpeg (`runtime/hle/movie.c`) |

`extracted/`, `generated/` and `build/` are made from your game on your PC. They're listed in `.gitignore` and must never
be shared.

## Credits

- **LAS Recomp** by Peiikai and contributors (MIT License, `LICENSE.LASRecomp`): the recompiler (`tools/gen.py`,
  `lifter.py`, `nso.py`, `simd_c.py`, ...) and the runtime this project started from.
- Third-party components keep their own licenses (`third_party/`): Dear ImGui (MIT), SMAA (MIT), the ported audio
  renderer and shader decoder references (MIT, `third_party/ryujinx/LICENSE.txt`), and fonts (SIL OFL 1.1 / DejaVu).
- Libraries: SDL 3, FFmpeg (LGPL, dynamically linked), Vulkan Memory Allocator and shaderc. The generator uses Capstone,
  Unicorn and python-lz4.
- [Switchbrew](https://switchbrew.org) documentation (NCA/PFS0/RomFS formats, for `tools/nsp_extract.py`).
- The icon and the setup window are this project's own designs (`assets/make_icon.py` draws the icon).

*DEAD OR ALIVE Xtreme 3* is a trademark of Koei Tecmo. Nintendo Switch is a trademark of Nintendo. This project is not
affiliated with or endorsed by either of them.
