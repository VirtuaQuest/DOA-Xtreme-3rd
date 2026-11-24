#!/bin/sh
# The release archive for GitHub (and RecompLauncher): this project at the last commit, with the launcher and
# recomplauncher.json on top, and release.txt naming the version (after an update, the launcher's --play builds the
# game again when it differs from the build's). Nothing but what git tracks, plus the launcher, which is built from
# launcher/ alone: no game files and nothing made from them.
#   tools/package_release.sh v0.1.0
#     Windows (Git Bash or an MSYS2 UCRT64 shell, build tools installed: Setup.bat does that)
#                                                   ->  dist/DOAX3Recomp-v0.1.0-Windows-x64.zip
#     Linux (the build tools of docs/LINUX.md)      ->  dist/DOAX3Recomp-v0.1.0-Linux-x64.tar.gz
set -e
cd "$(dirname "$0")/.."
version=$1
case "$version" in
v[0-9]*) ;;
*) echo "usage: tools/package_release.sh vX.Y.Z"; exit 1 ;;
esac
if [ -n "$(git status --porcelain)" ]; then
    echo "Commit your changes first: the archive is made from the last commit."
    exit 1
fi
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*) os=windows; launcher=DOAX3Launcher.exe ;;
Linux) os=linux; launcher=doax3launcher ;;
*) echo "package_release: Windows or Linux"; exit 1 ;;
esac
[ $os = windows ] && export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
make "build/$launcher"
stage=dist/stage
rm -rf "$stage"
mkdir -p "$stage"
cp "build/$launcher" "$stage/"
printf '%s\n' "$version" > "$stage/release.txt"
if [ $os = windows ]; then
    out=dist/DOAX3Recomp-$version-Windows-x64.zip
    rm -f "$out"
    git archive --format=zip -o "$out" --add-file="$stage/$launcher" --add-file="$stage/release.txt" HEAD
else
    out=dist/DOAX3Recomp-$version-Linux-x64.tar.gz
    rm -f "$out"
    strip "$stage/$launcher" 2>/dev/null || true
    git archive --format=tar.gz -o "$out" --add-file="$stage/$launcher" --add-file="$stage/release.txt" HEAD
fi
rm -rf "$stage"
echo "$out ($(du -h "$out" | cut -f1))"
