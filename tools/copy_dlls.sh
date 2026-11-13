#!/bin/sh
# Puts the MSYS2 libraries a program needs (SDL3, FFmpeg, shaderc, the C++ runtime, and theirs) next to it, so it
# starts without MSYS2 on PATH, and keeps working after MSYS2 updates its packages (FFmpeg's names change with its
# version). Copied on your own PC from your own MSYS2, never shared.
#   tools/copy_dlls.sh build/doax3recomp.exe      (the Makefile runs it after each build)
# Exit 2: a library it was linked with isn't in MSYS2 any more (updated since): link the program again, then run this.
# DLL_SOURCE=<folder> takes the libraries from another folder (for testing).
set -e
exe=$1
dir=$(dirname "$exe")
src=${DLL_SOURCE:-/ucrt64/bin}
[ -d "$src" ] || src=/c/msys64/ucrt64/bin   # (from Git Bash)
sys=$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32
[ -f "$exe" ] || { echo "copy_dlls: $exe isn't there"; exit 1; }
[ -d "$src" ] || { echo "copy_dlls: no MSYS2 libraries in $src"; exit 1; }

# The DLLs some files import, by name. One objdump for a whole level: starting programs is slow in MSYS2.
imports() { objdump -p "$@" | sed -n 's/^[[:space:]]*DLL Name: //p' | tr -d '\r'; }
# The names not seen yet (case doesn't matter on Windows), each once.
unseen() { printf '%s\n' "$@" | awk -v seen="$seen" 'BEGIN { n = split(tolower(seen), a, " "); for (i = 1; i <= n; i++) s[a[i]] = 1 }
    { k = tolower($0) } k != "" && !(k in s) { s[k] = 1; print }'; }

seen=''
copied=0
checked=0
missing=''
names=$(imports "$exe")
while [ -n "$names" ]; do
    new=$(unseen $names)
    seen="$seen $new"
    scan=''
    for name in $new; do
        case "$name" in api-ms-win-*|API-MS-WIN-*|ext-ms-win-*|EXT-MS-WIN-*) continue ;; esac   # (Windows' own)
        checked=$((checked + 1))
        if [ -f "$src/$name" ]; then
            # (copied when it's new or MSYS2's is newer; cp -p keeps its date for the next time)
            if [ ! -f "$dir/$name" ] || [ "$src/$name" -nt "$dir/$name" ]; then
                cp -pf "$src/$name" "$dir/$name"
                copied=$((copied + 1))
            fi
            scan="$scan $src/$name"
        elif [ -f "$sys/$name" ]; then
            :   # (Windows' own: KERNEL32.dll, USER32.dll...)
        elif [ -f "$dir/$name" ]; then
            :   # (copied before, from an older MSYS2: it still matches the program)
        else
            missing="$missing $name"
        fi
    done
    names=''
    [ -z "$scan" ] || names=$(imports $scan)
done
echo "copy_dlls: $checked libraries checked, $copied copied next to $(basename "$exe")"
if [ -n "$missing" ]; then
    echo "copy_dlls: MSYS2 no longer has$missing"
    exit 2
fi
