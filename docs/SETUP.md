# Setup in detail

`Setup.bat` does all of this for you (see the [README](../README.md)). This page is what it does, how to do it by hand,
and what to do when something goes wrong.

## 1. Your game files

You need files dumped from **your own** Switch and your own copy of the game:

| File | Required | What it is |
|---|---|---|
| Base game `.nsp` | yes | *DEAD OR ALIVE Xtreme 3 Scarlet*, title ID `01009CC00C97C000` |
| Update `.nsp` | no | version 1.0.6 (title ID `01009CC00C97C800`) |
| DLC `.nsp` files | no | add-on content (title IDs `01009CC00C97Dxxx`) |
| `prod.keys` | yes | your console's keys, dumped from the same console |

Put the `.nsp` files in one folder; subfolders are fine. Setup tells them apart by their contents, not their names,
and skips duplicates and files from other games.

Keep the keys **at least as new as your game files**. A key file dumped before an update came out can't decrypt
that update. Setup then reports those files as unreadable, and the details mention a missing `titlekek_..` or
`key_area_key_...` key.

These files, and everything made from them (`extracted/`, `generated/`, `build/`), are yours alone: never upload or
share them. `.gitignore` already keeps them out of git.

## 2. The build tools

Setup installs these with `winget`, which comes with Windows 10 and 11:

- **MSYS2** in `C:\msys64` (the path is fixed: the launcher and the build expect it), with these UCRT64 packages:
  `make`, `binutils`, `clang`, `lld`, `sdl3`, `ffmpeg`, `shaderc`, `vulkan-headers`, `vulkan-loader` and
  `vulkan-memory-allocator`. The game uses the SDL, FFmpeg and shaderc libraries from there when it runs, so keep MSYS2
  installed.
- **Python 3.12** from python.org (for the recompiler and the extractor), with a virtual environment in `.venv`.

By hand, in PowerShell:

```powershell
winget install --id MSYS2.MSYS2 -e
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"   # run twice: the first pass can stop after a core update
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm make mingw-w64-ucrt-x86_64-binutils mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-shaderc mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader mingw-w64-ucrt-x86_64-vulkan-memory-allocator"
winget install --id Python.Python.3.12 -e --scope user
py -V:3.12 -m venv .venv
.venv\Scripts\python -m pip install unicorn==2.1.4 capstone==5.0.9 lz4==4.4.5 pycryptodome==3.23.0
```

The mod tool can also export the game's BC7 textures with `.venv\Scripts\python -m pip install texture2ddecoder`
(optional).

## 3. Extract, recompile, compile (by hand)

From the project folder, in Git Bash or an MSYS2 UCRT64 shell. In Git Bash, first run
`export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"`. Give Windows paths as `C:/...`, since the venv's Python
is a Windows program.

```sh
# the game: with the update, extract the update and give the base game with --base (it writes the patched game)
.venv/Scripts/python -I tools/nsp_extract.py "C:/path/update.nsp" "C:/path/prod.keys" extracted --base "C:/path/base.nsp"
#   (without the update: .venv/Scripts/python -I tools/nsp_extract.py "C:/path/base.nsp" "C:/path/prod.keys" extracted)
# each DLC, into the same folder (they go to extracted/aoc/<id>/)
.venv/Scripts/python -I tools/nsp_extract.py "C:/path/dlc.nsp" "C:/path/prod.keys" extracted
make gen        # recompile the game's code into generated/
make -j16       # compile build/doax3recomp.exe, build/doaxgame.dll and build/DOAX3Launcher.exe
```

`make gen` must run again after the game files change, for example after adding the update. Then run
`build/DOAX3Launcher.exe` or `tools/run.sh`.

## Troubleshooting

| Problem | What to do |
|---|---|
| Windows protected your PC (SmartScreen) when opening `Setup.bat` | *More info > Run anyway*. `Setup.bat` only starts `setup\Setup.ps1` in PowerShell, which you can read first. |
| Setup says winget isn't available | Install MSYS2 from msys2.org (to `C:\msys64`) and Python 3.12 from python.org, then press *Install build tools* again for the rest. |
| Files "couldn't be read" | Usually keys older than the game files: dump `prod.keys` again from your console after updating it. Press *Show details* for the reason. A "personalized ticket" means a dump that only works on its own console. |
| "the base game's .nsp is needed too" | The update and DLC only work on top of the base game: add its `.nsp` to the folder. |
| The build fails | Press *Show details*, or read `setup.log` in the project folder. Check you have the free space, and that the project's path has no unusual characters. |
| The launcher says `doax3recomp.exe` or `extracted` is missing | The build didn't finish: run Setup again. |
| The game starts with a black window or closes at once | Update your GPU drivers (Vulkan). Then read `doax3.log`. |
| A game file is missing at start | Extract again: in Setup, untick *Keep the game files already extracted*. |

Settings are in `larecomp.ini`; deleting it restores the defaults. To start over completely, delete `extracted/`,
`generated/`, `build/` and `.venv/`. Your saves are in `save/`.

## Developing

- `tools/run.sh [seconds]`: run from Git Bash, with the log in `doax3.log`.
- `tools/route.sh <route> [seconds] [frames]`: scripted test runs with screenshots (see the script's header).
- [docs/CHANGES.md](CHANGES.md): what has changed from LAS Recomp, the debugging switches (`LARECOMP_*`) and open
  work.
