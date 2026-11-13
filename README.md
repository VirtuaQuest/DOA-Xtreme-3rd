# DOAX3 Recomp

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A static recompilation of *DEAD OR ALIVE Xtreme 3 Scarlet* (Nintendo Switch: version 1.0.0 or the 1.0.6 update, with
DLC) into a native Windows x86-64 program. It's built on [LAS Recomp](https://github.com/peiikai/LASRecomp)'s recompiler
and Switch runtime.

> **No game included.** This project contains no game files, code, art, audio or keys. To use it you need your own
> copy of the game, dumped from your own Nintendo Switch, and the keys from that console. The game is built from those
> files on your PC, and nothing is uploaded anywhere. Never share the game files or anything built from them (see
> [Disclaimers](#disclaimers)).

**Status: playable, work in progress.** The title, menus, island, hotel, casino, Photo Paradise and the 3D scenes run,
with sound, the opening movie, an optional 60 fps mode and a selectable 3D resolution. See
[docs/CHANGES.md](docs/CHANGES.md) for what has changed from LAS Recomp and what's still open.

## What you need

- **A Windows PC:** Windows 10 or 11, 64-bit; a CPU with AVX2 (Intel 4th generation / AMD Ryzen or newer); a GPU with
  Vulkan drivers; about 20 GB of free disk space; an internet connection for the one-time tool download.
- **Your game, as `.nsp` files dumped from your own console:**
  - **Base game:** required. Alone, it's the *Basic Free Edition*, as on the console.
  - **The 1.0.6 update:** optional, recommended.
  - **DLC you own:** optional. It unlocks characters, swimsuits and the casino membership.
- **Your console's keys:** a `prod.keys` file dumped from the same console, at least as new as your game files.

Use homebrew dumping tools on your own console for both. This project doesn't provide them, and doesn't help with
getting game files any other way.

## Install with RecompLauncher

[RecompLauncher](https://recomplauncher.com) can install and update this project for you. Its setup screen asks for two
things: your game (the folder with its `.nsp` files: base game, and the update and DLC if you have them; or the base
game's `.nsp`, with the others beside it), and your console's `prod.keys`. Pick `prod.keys`, not `title.keys`. Then it
builds the game for you without the setup window, the same way `Setup.bat` does. The first time, it installs the build tools (MSYS2 and Python 3.12, Windows may ask for permission).
It takes about an hour and about 20 GB. Then Play starts the game, and the window mode, window size, 3D resolution,
frame rate and FPS counter can be changed on the game's page. When a new version comes out, the first Play builds the
game again, in the setup window, without extracting your files again.

## Install by hand

1. **Download this project.**
   - On GitHub: **Code > Download ZIP**, then right-click the ZIP and pick *Extract All*.
   - Or with Git: `git clone https://github.com/VirtuaQuest/DOA-Xtreme-3rd.git`
   - Put it in a short path without unusual characters, for example `C:\Games\DOAX3Recomp`. Avoid OneDrive folders.
2. **Put your game files together.** Copy your `.nsp` files into one folder (subfolders are fine), and keep your
   `prod.keys` anywhere you like.
3. **Run `Setup.bat`.** Double-click it. If Windows SmartScreen warns you, pick *More info > Run anyway*: it only
   opens `setup\Setup.ps1`, which you can read first. A window opens with three steps:
   1. **Build tools:** press *Install build tools* if it asks. This installs the free MSYS2 (compilers and libraries,
      into `C:\msys64`) and Python 3.12 through `winget`, a few GB in all. Windows may ask for permission. Keep MSYS2
      installed afterwards: the game uses its libraries.
   2. **Your game:** choose the folder with your `.nsp` files and your `prod.keys`. Setup reads each file's own
      headers, so names don't matter, and shows what it found: base game, update, DLC, duplicates.
   3. **Build:** press *Build the game*. It extracts your game files into `extracted/`, recompiles the game's code into
      `generated/`, and compiles the program into `build/`. Allow about an hour (54 minutes on a 10-core PC with all DLC).
      It runs in the background, and *Show details* shows what it's doing.
4. **Play.** Press *Play* in Setup, or double-click `Play.bat` any time later. The launcher lets you choose windowed,
   borderless or fullscreen, the resolution, the 3D resolution and 60 fps, then starts the game.

**Shortcut:** right-click `Play.bat` > *Show more options* > *Send to* > *Desktop (create shortcut)*.

**Updating this project:** with Git, run `git pull`. With a ZIP, extract the new one to a new folder and move your
`extracted/`, `save/` and `larecomp.ini` into it. Then run `Setup.bat` again, tick *Keep the game files already
extracted*, and press *Build the game*.

**Uninstalling:** delete the project folder. That removes the game files made from your copy, your saves and the
build. MSYS2 and Python 3.12 can be removed in *Settings > Apps* if nothing else uses them.

To do all of this by hand, and for troubleshooting, see [docs/SETUP.md](docs/SETUP.md).

## Playing

- **Controls:** keyboard or any controller SDL knows. **F1**, or **Back + Start** / the **Guide** button on a
  controller, opens the recomp's menu (controls, display, audio). **F11** switches fullscreen.
- **Settings** are saved in `larecomp.ini`, and your game saves in `save/`, both in the project folder.
- **The log** is `doax3.log` in the project folder. Include it when you report a problem, but never your game files.

## Mods

Mods replace the game's own files (models, textures, voice lines) from `mods/<mod>/romfs/`, without touching your
extracted files. The mod tool (`tools/mods/doaxmod.py`) unpacks game files, rebuilds models from glTF, and replaces
portraits and voice lines. See [docs/MODDING.md](docs/MODDING.md). No mods are included. Only share mods made of your
own work: a mod built from someone else's characters, models or game files carries their copyright.

## Making a release (for maintainers)

1. Commit everything, then run `tools/package_release.sh v0.1.0` (the new version) from Git Bash. It writes
   `dist/DOAX3Recomp-v0.1.0-Windows-x64.zip`: the project at the last commit, plus `DOAX3Launcher.exe`,
   `recomplauncher.json` and `release.txt` on top. It contains nothing made from the game.
2. On GitHub, *Releases > Draft a new release*: tag `v0.1.0`, a title, a line on what changed, and attach that zip.
   Mark test builds as pre-releases.

RecompLauncher installs the latest full release, runs `DOAX3Launcher.exe --prepare --game <folder>` to build the game
on the player's PC, and `DOAX3Launcher.exe --play` to start it (`recomplauncher.json`). Both work without RecompLauncher
too. Keep `recomplauncher.json` in step with `larecomp.ini` when settings are added or renamed.

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

## License

This project's own code is released under the **MIT License** ([`LICENSE`](LICENSE)), copyright (c) 2026 VirtuaQuest.
The parts that came from LAS Recomp are MIT too, copyright (c) 2026 Peiikai ([`LICENSE.LASRecomp`](LICENSE.LASRecomp)).

**What this lets you do:** use, copy, change, merge and share the code, in free or paid projects, as long as you:

- **keep both copyright notices and license texts** (`LICENSE` and `LICENSE.LASRecomp`) with any copy, or any
  substantial part, of the code;
- **keep the third-party licenses** listed below with the parts they cover;
- **accept that it comes with no warranty:** "as is", and the authors aren't liable for anything that comes of using it.

**To use the license in a project of your own:** copy `LICENSE` into it, keep the notices above, and add your own
copyright line for your changes, for example `Copyright (c) 2026 Your Name`.

**What the license doesn't cover:** the game. Its code, art, audio and text belong to their owners. The MIT License
applies only to this project's own source code: not to your game files, nor to anything made from them
(`extracted/`, `generated/`, `build/doaxgame.dll`).

## Third-party notices

Included in this repository (each keeps its own license, next to it in `third_party/`):

| Component | License | Used for |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | the in-game menu. Its bundled examples and fonts carry their own notices (GLFW: zlib; Roboto, Droid Sans, Cousine: Apache 2.0; Karla: OFL 1.1; Proggy: MIT-style) |
| [SMAA](https://github.com/iryoku/smaa) | MIT | antialiasing |
| [Ryujinx](https://github.com/Ryujinx/Ryujinx) (references for the audio renderer and shader decoder) | MIT | `third_party/ryujinx/LICENSE.txt` |
| BIZ UDPGothic, Baloo 2 fonts | SIL Open Font License 1.1 | text when the system font is missing |
| DejaVu Sans | Bitstream Vera / DejaVu license | a fallback font |

Installed by Setup from MSYS2 and linked when you build (not included in this repository):

| Component | License |
|---|---|
| [FFmpeg](https://ffmpeg.org) (MSYS2's build) | **GPL-3.0-or-later**, because MSYS2 builds it with GPL components (`--enable-gpl --enable-version3`) |
| [SDL 3](https://libsdl.org) | zlib |
| [shaderc](https://github.com/google/shaderc), Vulkan loader | Apache 2.0 |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | MIT |

Python packages used only while recompiling (not part of the game program): [Unicorn](https://www.unicorn-engine.org)
(GPL-2.0 engine), [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
[python-lz4](https://github.com/python-lz4/python-lz4) (BSD-3-Clause) and
[PyCryptodome](https://www.pycryptodome.org) (BSD-2-Clause / public domain).

**If you share a compiled build of the program itself** (not recommended, see below): it links GPL-3.0 FFmpeg, so the
build as a whole must be offered under GPL-3.0 terms, with its source. The MIT-licensed code here allows that.
Sharing the source code, as this repository does, has no such requirement.

Format documentation from [Switchbrew](https://switchbrew.org) (NCA, PFS0 and RomFS, for `tools/nsp_extract.py`). The
icon and the setup window are this project's own designs (`assets/make_icon.py` draws the icon).

## Disclaimers

- **Trademarks:** *DEAD OR ALIVE Xtreme 3* is a trademark of Koei Tecmo. Nintendo Switch is a trademark of Nintendo.
  Other names belong to their owners and are used only to say what this project works with.
- **Not affiliated:** this project isn't affiliated with, endorsed by or supported by Koei Tecmo, Nintendo, NVIDIA or
  the makers of any game or console mentioned. Please don't contact them about it.
- **Your own copy only:** you need a copy of the game you own, dumped from your own console with your own keys. Getting
  game files or keys any other way may be illegal where you live, and this project doesn't help with it.
- **Never share game-derived files:** `extracted/` (the game's files), `generated/` (the game's code, translated) and
  `build/doaxgame.dll` (that code, compiled) are made from the game and are its owners' copyright. Don't upload or share
  them, and don't ask for them. They're already in `.gitignore`.
- **No warranty:** as the license says, this software comes as is. Use it at your own risk; keep backups of your saves.
