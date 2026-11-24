#!/bin/sh
# Run the recompiled game from the repo root's extracted/ (log: doax3.log). Extra env vars pass through
# (LARECOMP_* debugging switches, see the runtime). Usage: tools/run.sh [seconds to run, default: until closed]
# Windows (Git Bash / MSYS2) or Linux.
cd "$(dirname "$0")/.." || exit 1
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*)
    export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
    exe=build/doax3recomp.exe ;;
*)
    exe=build/doax3recomp ;;
esac
export LARECOMP_DATA_ROOT="$(pwd -W 2>/dev/null || pwd)"
if [ -n "$1" ]; then
    "$exe" extracted > doax3.log 2>&1 &
    pid=$!
    sleep "$1"
    kill "$pid" 2>/dev/null
    case "$exe" in
    *.exe) taskkill //F //IM doax3recomp.exe > /dev/null 2>&1 ;;
    *) sleep 2; kill -9 "$pid" 2>/dev/null ;;  # (a game stuck on shutdown)
    esac
    wait "$pid" 2>/dev/null
else
    "$exe" extracted 2>&1 | tee doax3.log
fi
