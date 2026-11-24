#!/bin/sh
# Starts the game's launcher, once setup.sh has built it (Linux).
cd "$(dirname "$0")" || exit 1
if [ -x build/doax3launcher ]; then
    exec build/doax3launcher "$@"
else
    echo "The game isn't built yet. Run ./Setup.sh --game <folder with your .nsp files> first."
    exit 1
fi
