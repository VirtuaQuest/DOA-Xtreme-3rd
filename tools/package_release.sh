#!/bin/sh
# The release zip for GitHub (and RecompLauncher): this project at the last commit, with DOAX3Launcher.exe and
# recomplauncher.json on top, and release.txt naming the version (after an update, DOAX3Launcher.exe --play builds the
# game again when it differs from the build's). Nothing but what git tracks, plus the launcher, which is built from
# launcher/ alone: no game files and nothing made from them.
#   tools/package_release.sh v0.1.0      ->  dist/DOAX3Recomp-v0.1.0-Windows-x64.zip
# From Git Bash or an MSYS2 UCRT64 shell, with the build tools installed (Setup.bat does that).
set -e
cd "$(dirname "$0")/.."
version=$1
case "$version" in
v[0-9]*) ;;
*) echo "usage: tools/package_release.sh vX.Y.Z"; exit 1 ;;
esac
if [ -n "$(git status --porcelain)" ]; then
    echo "Commit your changes first: the zip is made from the last commit."
    exit 1
fi
export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
make build/DOAX3Launcher.exe
stage=dist/stage
rm -rf "$stage"
mkdir -p "$stage"
cp build/DOAX3Launcher.exe "$stage/"
printf '%s\n' "$version" > "$stage/release.txt"
out=dist/DOAX3Recomp-$version-Windows-x64.zip
rm -f "$out"
git archive --format=zip -o "$out" --add-file="$stage/DOAX3Launcher.exe" --add-file="$stage/release.txt" HEAD
rm -rf "$stage"
echo "$out ($(du -h "$out" | cut -f1))"
