#!/usr/bin/env bash
# DOAX3 Recomp setup on Linux: what setup/Setup.ps1 does on Windows, in a terminal.
#   tools/linux/setup.sh --game <folder with your .nsp files | the base game's .nsp> [--keys <prod.keys>]
#   tools/linux/setup.sh --keep-extracted          build again from the files already in extracted/
#   tools/linux/setup.sh --check                   only check the build tools (and say how to install them)
# Steps: the build tools (the distribution's own packages: this script only checks and tells you the command), a
# Python venv in .venv with the pinned packages, your .nsp files read by their own headers (base game, update, DLC),
# extracted into extracted/, the game's code recompiled into generated/ and the program compiled into build/.
# Lines "progress <0..1> <text>" on stdout are for RecompLauncher; everything is also in setup.log.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT" || exit 1
LOG="$ROOT/setup.log"
GAME_ID_PREFIX=01009cc00c97
PY_PINS=(unicorn==2.1.4 capstone==5.0.9 lz4==4.4.5 pycryptodome==3.23.0)
FREE_GB_EXTRACT=20   # (about 11 GB of game files with all DLC, plus the build)
FREE_GB_BUILD=3

GAME=''; KEYS=''; KEEP=0; CHECK_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --game) GAME=${2:-}; shift 2 ;;
        --keys) KEYS=${2:-}; shift 2 ;;
        --keep-extracted) KEEP=1; shift ;;
        --check) CHECK_ONLY=1; shift ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)"; exit 2 ;;
    esac
done

: > "$LOG.tmp" 2>/dev/null && mv -f "$LOG.tmp" "$LOG"
say()      { printf '%s\n' "$*"; printf '%s\n' "$*" >> "$LOG"; }
progress() { printf 'progress %s %s\n' "$1" "$2"; printf '[%s] %s\n' "$1" "$2" >> "$LOG"; }
die()      { printf 'ERROR: %s\n' "$*" >&2; printf 'ERROR: %s\n' "$*" >> "$LOG"; exit 1; }
# a command's output into the log only (and its exit code kept)
logged()   { "$@" >> "$LOG" 2>&1; }

# ---- the build tools
check_tools() {
    local missing=()
    for c in clang clang++ ld.lld make pkg-config curl; do command -v "$c" > /dev/null || missing+=("$c"); done
    for p in sdl3 libavformat libavcodec libavutil libswscale vulkan; do
        pkg-config --exists "$p" 2> /dev/null || missing+=("$p")
    done
    pkg-config --exists shaderc 2> /dev/null || [ -e /usr/lib/x86_64-linux-gnu/libshaderc.so ] ||
        [ -e /usr/lib/libshaderc_shared.so ] || [ -e /usr/lib64/libshaderc_shared.so ] || missing+=(shaderc)
    [ -e /usr/include/vulkan/vulkan.h ] || missing+=(vulkan-headers)
    command -v python3 > /dev/null || missing+=(python3)
    if command -v python3 > /dev/null && ! python3 -c 'import sys, venv, ensurepip; sys.exit(sys.version_info < (3, 10))' 2> /dev/null; then
        missing+=('python3 (3.10 or newer, with venv)')
    fi
    if [ ${#missing[@]} -eq 0 ]; then return 0; fi
    say "Missing build tools: ${missing[*]}"
    local id='' like=''
    [ -r /etc/os-release ] && . /etc/os-release && id=${ID:-} && like=${ID_LIKE:-}
    say 'Install them with your package manager, for example:'
    case " $id $like " in
        *" arch "*)
            say '  sudo pacman -S --needed base-devel clang lld sdl3 ffmpeg shaderc vulkan-headers vulkan-icd-loader python curl' ;;
        *" debian "*|*" ubuntu "*)
            say '  sudo apt install build-essential clang lld pkg-config libsdl3-dev libavformat-dev libavcodec-dev \'
            say '      libswscale-dev libshaderc-dev libvulkan-dev python3 python3-venv curl'
            say '  (SDL 3 is in Debian 13 / Ubuntu 25.04 and newer; older releases need it built from source)' ;;
        *" fedora "*|*" rhel "*)
            say '  sudo dnf install clang lld make pkgconf SDL3-devel ffmpeg-free-devel libshaderc-devel \'
            say '      vulkan-loader-devel vulkan-headers python3 curl' ;;
        *" suse "*|*" opensuse "*)
            say '  sudo zypper install clang lld make pkg-config SDL3-devel ffmpeg-7-libavformat-devel shaderc-devel \'
            say '      vulkan-devel python3 curl' ;;
        *) say '  clang, lld, make, pkg-config, SDL 3, FFmpeg, shaderc, the Vulkan loader and headers, Python 3, curl' ;;
    esac
    say 'Your GPU also needs a Vulkan driver (Mesa RADV / ANV, or NVIDIA 535 or newer).'
    return 1
}

progress 0.01 'Checking the build tools'
check_tools || exit 1
say 'Build tools: all there.'
[ $CHECK_ONLY = 1 ] && exit 0

# ---- Python
PY="$ROOT/.venv/bin/python"
venv_ok() { [ -x "$PY" ] && "$PY" -c 'import unicorn, capstone, lz4, Crypto' 2> /dev/null; }
if ! venv_ok; then
    progress 0.02 'Setting up Python (.venv)'
    [ -x "$PY" ] || logged python3 -m venv "$ROOT/.venv" || die 'Creating .venv failed. See setup.log.'
    logged "$PY" -m pip install --disable-pip-version-check "${PY_PINS[@]}" || die 'pip failed. See setup.log.'
    venv_ok || die "Python's packages don't load. See setup.log."
fi

free_gb() { df -Pk "$ROOT" | awk 'NR == 2 { print int($4 / 1048576) }'; }
need_space() {
    local have; have=$(free_gb)
    [ "${have:-0}" -ge "$1" ] || die "This needs about $1 GB free here ($ROOT), and there's ${have:-0} GB."
}

has_header_key() { [ -f "$1" ] && grep -qE '^\s*header_key\s*=' "$1"; }

# ---- prod.keys: the one given, else the usual places (the game's folder, two deep, the folder above it, ~/.switch,
# and the key folders of the Switch emulators)
keys_places() {
    local folder=$1
    [ -f "$folder" ] && folder=$(dirname "$folder")
    if [ -d "$folder" ]; then
        echo "$folder/prod.keys"
        find "$folder" -mindepth 2 -maxdepth 3 -name prod.keys 2> /dev/null
        echo "$(dirname "$folder")/prod.keys"
    fi
    echo "$HOME/.switch/prod.keys"
    local data=${XDG_DATA_HOME:-$HOME/.local/share} conf=${XDG_CONFIG_HOME:-$HOME/.config}
    for emu in Ryujinx/system yuzu/keys suyu/keys sudachi/keys citron/keys eden/keys; do
        echo "$data/$emu/prod.keys"; echo "$conf/$emu/prod.keys"
    done
    echo "$HOME/.var/app/org.ryujinx.Ryujinx/config/Ryujinx/system/prod.keys"
}

find_keys() {
    if [ -n "$KEYS" ]; then
        [ -f "$KEYS" ] || die "Keys file not found: $KEYS"
        if has_header_key "$KEYS"; then echo "$KEYS"; return; fi
        # title.keys has no header_key, and is easy to pick by mistake: the prod.keys beside it, or a usual place
        local p
        while read -r p; do
            if has_header_key "$p"; then
                say "$(basename "$KEYS") isn't a prod.keys (it has no header_key): using $p instead." >&2
                echo "$p"; return
            fi
        done < <(echo "$(dirname "$KEYS")/prod.keys"; keys_places "$GAME")
        die "That keys file ($(basename "$KEYS")) isn't a prod.keys: it has no header_key line. Choose the prod.keys dumped from your console (title.keys is a different file)."
    fi
    local p
    while read -r p; do
        if has_header_key "$p"; then echo "$p"; return; fi
    done < <(keys_places "$GAME")
    die "prod.keys wasn't found. Put your console's prod.keys in the folder with your game files, or give it with --keys <file>."
}

# ---- the game files, by their own headers
BASE=''; UPDATE=''; UPDATE_VER=-1; DLC=(); DUPES=0
scan_files() {
    local files=() f
    if [ -f "$GAME" ]; then
        # a .nsp given: the folder it's in (the update and DLC are usually beside it)
        GAME=$(dirname "$GAME")
    fi
    [ -d "$GAME" ] || die "Not a folder or a file: $GAME"
    while IFS= read -r -d '' f; do files+=("$f"); done < <(find "$GAME" -type f -iname '*.nsp' -print0 | sort -z)
    [ ${#files[@]} -gt 0 ] || die "No .nsp files in $GAME."
    local scan; scan=$(mktemp -d "${TMPDIR:-/tmp}/doax3-scan.XXXXXX")
    local i=0 seen=' '
    for f in "${files[@]}"; do
        i=$((i + 1))
        progress 0.03 "Reading your game files ($i of ${#files[@]})"
        local out
        if ! out=$("$PY" -I -u tools/nsp_extract.py "$f" "$KEYS" "$scan" --list 2>&1); then
            say "Can't read $(basename "$f"): $(printf '%s\n' "$out" | grep -E 'Error|personalized' | tail -1)"
            continue
        fi
        printf '%s\n' "$out" >> "$LOG"
        local m kind id
        m=$(printf '%s\n' "$out" | grep -oE '(program|publicdata)\s+id\s+[0-9a-f]{16}' | head -1)
        [ -n "$m" ] || { say "Not part of the game: $(basename "$f")"; continue; }
        kind=${m%% *}; id=${m##* }
        [[ $id == "$GAME_ID_PREFIX"* ]] || { say "Another game's file: $(basename "$f")"; continue; }
        if [ "$kind" = publicdata ]; then
            if [[ $seen == *" $id "* ]]; then DUPES=$((DUPES + 1)); else seen+="$id "; DLC+=("$f"); fi
        elif printf '%s\n' "$out" | grep -q 'romfs(patch)'; then
            local v; v=$(basename "$f" | grep -oE '\[v[0-9]+\]' | tr -dc 0-9); v=${v:-0}
            [ -n "$UPDATE" ] && DUPES=$((DUPES + 1))
            if [ "$v" -gt "$UPDATE_VER" ]; then UPDATE=$f; UPDATE_VER=$v; fi
        else
            if [ -n "$BASE" ]; then DUPES=$((DUPES + 1)); else BASE=$f; fi
        fi
    done
    rm -rf "$scan"
    [ -n "$BASE" ] || die 'The base game is needed: its .nsp is not in that folder.'
    say "Base game: $(basename "$BASE")"
    [ -n "$UPDATE" ] && say "Update: $(basename "$UPDATE")" || say 'Update: none (the 1.0.6 update is recommended)'
    say "DLC: ${#DLC[@]}$([ $DUPES -gt 0 ] && echo ", $DUPES duplicates skipped")"
}

files_stamp() {
    local f
    for f in "$BASE" "$UPDATE" "${DLC[@]}"; do
        [ -n "$f" ] && printf '%s|%s\n' "$(basename "$f")" "$(stat -c %s "$f")"
    done | LC_ALL=C sort
}

extract() {
    local out="$ROOT/extracted" stamp="$ROOT/extracted/extracted-from.txt"
    rm -f "$stamp"
    progress 0.05 'Extracting the game'
    if [ -n "$UPDATE" ]; then
        logged "$PY" -I -u tools/nsp_extract.py "$UPDATE" "$KEYS" "$out" --base "$BASE" || die 'Extracting the game failed. See setup.log.'
    else
        logged "$PY" -I -u tools/nsp_extract.py "$BASE" "$KEYS" "$out" || die 'Extracting the game failed. See setup.log.'
    fi
    local i=0 d
    for d in "${DLC[@]}"; do
        i=$((i + 1))
        progress 0.15 "Extracting DLC $i of ${#DLC[@]}"
        logged "$PY" -I -u tools/nsp_extract.py "$d" "$KEYS" "$out" || die "Extracting $(basename "$d") failed. See setup.log."
    done
    [ -f "$out/exefs/main" ] || die "Extraction finished without the game's program."
    files_stamp > "$stamp"
}

build() {
    local jobs; jobs=$(nproc 2> /dev/null || echo 4)
    progress 0.25 'Recompiling the game code'
    logged make gen || die 'Recompiling failed. See setup.log.'
    progress 0.40 "Compiling (about an hour; $jobs threads)"
    # (make's lines into the log; a rough progress from the objects built so far)
    local total; total=$(find generated -name '*.c' | wc -l)
    ( make -j"$jobs" >> "$LOG" 2>&1; echo $? > "$ROOT/build/.make-exit" ) &
    local pid=$!
    while kill -0 $pid 2> /dev/null; do
        sleep 10
        local done_; done_=$(find build/obj -name '*.o' 2> /dev/null | wc -l)
        [ "$total" -gt 0 ] && progress "$(awk -v d="$done_" -v t="$total" 'BEGIN { printf "%.2f", 0.40 + 0.58 * (d > t ? t : d) / t }')" \
            "Compiling ($done_ of $total files)"
    done
    wait $pid
    local code; code=$(cat "$ROOT/build/.make-exit" 2> /dev/null || echo 1); rm -f "$ROOT/build/.make-exit"
    [ "$code" = 0 ] || die 'Compiling failed. See setup.log.'
    for f in doax3recomp doaxgame.so doax3launcher; do
        [ -f "build/$f" ] || die "The build finished without build/$f."
    done
    # (which release it was built from: the launcher's --play builds again after an update)
    local from=source
    [ -f release.txt ] && from=$(head -1 release.txt | tr -d '\r' | xargs)
    echo "$from" > build/built-from.txt
    progress 1 'The build is finished'
}

if [ $KEEP = 1 ]; then
    [ -f extracted/exefs/main ] || die 'There are no extracted game files to keep (run with --game first).'
    need_space $FREE_GB_BUILD
    mkdir -p build
    build
else
    [ -n "$GAME" ] || die 'Give your game files: --game <folder with your .nsp files> (see --help).'
    KEYS=$(find_keys) || exit 1
    say "Keys: $KEYS"
    scan_files
    if [ -f extracted/exefs/main ] && [ -f extracted/extracted-from.txt ] &&
       [ "$(cat extracted/extracted-from.txt)" = "$(files_stamp)" ]; then
        say 'These files are extracted already: keeping them.'
        need_space $FREE_GB_BUILD
    else
        need_space $FREE_GB_EXTRACT
        extract
    fi
    mkdir -p build
    build
fi
say "Done. Start the game with ./Play.sh (or build/doax3launcher)."
