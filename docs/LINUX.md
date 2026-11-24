# DOAX3 Recomp on Linux

The same recompilation as on Windows, built on your PC from your own game files: the runtime's Linux host layer
(`runtime/platform/linux.c`, from LAS Recomp's Linux build), Vulkan, SDL 3 and FFmpeg from your distribution.

## What you need

- **Linux, x86-64**, from about 2023 or newer (SDL 3 has to be packaged, or built by you). Kernel 6.7 or newer is
  best: the runtime then tracks the game's GPU memory writes with `userfaultfd` + `PAGEMAP_SCAN` (older kernels work,
  a little slower).
- **A CPU from 2008 or newer** (SSE4.2); with AVX2 (Intel 4th generation / AMD Ryzen and newer) it runs faster: the
  game is built for the CPU of the PC it's set up on.
- **A GPU with a Vulkan 1.3 driver:** Mesa (RADV for AMD, ANV for Intel) or NVIDIA's own driver, 535 or newer.
- **About 20 GB of free disk space**, and your game as `.nsp` files with your console's `prod.keys`, as in the
  [README](../README.md#what-you-need).

## The build tools

Install them once with your package manager. `./Setup.sh --check` checks them and prints this command for your
distribution.

| Distribution | Command |
| --- | --- |
| Arch, CachyOS, Manjaro, EndeavourOS | `sudo pacman -S --needed base-devel clang lld sdl3 ffmpeg shaderc vulkan-headers vulkan-icd-loader python curl` |
| Debian 13, Ubuntu 25.04 and newer | `sudo apt install build-essential clang lld pkg-config libsdl3-dev libavformat-dev libavcodec-dev libswscale-dev libshaderc-dev libvulkan-dev python3 python3-venv curl` |
| Fedora 42 and newer | `sudo dnf install clang lld make pkgconf SDL3-devel ffmpeg-free-devel libshaderc-devel vulkan-loader-devel vulkan-headers python3 curl` |

The Vulkan Memory Allocator header is taken from the distribution where it has one (Debian, Fedora); elsewhere `make`
downloads that one MIT-licensed header (version 3.3.0) into `build/deps/` the first time.

**Steam Deck / immutable systems (SteamOS, Bazzite, Fedora Atomic):** build inside a container with the tools above
(Distrobox or Toolbox, e.g. an Arch or Fedora one) in the project folder; the built game then runs on the host.

## Setting up

1. **Download this project** (`git clone https://github.com/VirtuaQuest/DOA-Xtreme-3rd.git`, or the release's
   `.tar.gz`) into a folder with about 20 GB free.
2. **Build the game from your files:**

   ```sh
   ./Setup.sh --game ~/Switch/DOAX3      # the folder with your .nsp files (or the base game's .nsp)
   ```

   It finds `prod.keys` in that folder, the folders in it, the folder above it, `~/.switch` and the usual emulator key
   folders; or give it with `--keys <file>`. Setup reads each file's own headers (names don't matter): base game,
   update, DLC. It makes a Python venv in `.venv`, extracts your files into `extracted/`, recompiles the game's code into
   `generated/` and compiles the program into `build/`. Allow about an hour. Everything it does is in `setup.log`.
3. **Play:** `./Play.sh` opens the launcher (the Game, Controls and Mods tabs, as on Windows); Play starts the game.

**Updating:** `git pull` (or a new release with your `extracted/`, `save/` and `larecomp.ini` moved over), then
`./Setup.sh --keep-extracted` to build again without extracting your files again.

**By hand:** what Setup does is `python3 -m venv .venv`, `.venv/bin/pip install unicorn==2.1.4 capstone==5.0.9
lz4==4.4.5 pycryptodome==3.23.0`, `tools/nsp_extract.py` (see its `--help`), `make gen` and `make -j$(nproc)`.
`tools/run.sh` starts the game without the launcher, its log in `doax3.log`.

## Where things are

Everything stays in the project folder, as on Windows: `larecomp.ini` (settings), `body.ini` (the launcher's body
settings), `save/`, `mods/`, `doax3.log` (the last run, when started from the launcher or `tools/run.sh`).

## Troubleshooting

- **`no page write tracking` in the log:** a kernel older than 6.7, or `userfaultfd` restricted
  (`/proc/sys/vm/unprivileged_userfaultfd`). The game runs anyway.
- **The wrong GPU on a laptop:** `DRI_PRIME=1 ./Play.sh` (Mesa), or `__NV_PRIME_RENDER_OFFLOAD=1
  __GLX_VENDOR_LIBRARY_NAME=nvidia ./Play.sh` (NVIDIA).
- **Wayland:** SDL 3 uses Wayland where it can. `SDL_VIDEO_DRIVER=x11 ./Play.sh` uses X11 (XWayland) instead.
- **A crash:** the log has a backtrace; include the whole `doax3.log` in a report (never your game files).
