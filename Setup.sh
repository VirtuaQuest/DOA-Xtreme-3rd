#!/bin/sh
# Builds the game from your own game files (Linux): see tools/linux/setup.sh --help and docs/LINUX.md.
#   ./Setup.sh --game <folder with your .nsp files> [--keys <prod.keys>]
exec "$(dirname "$0")/tools/linux/setup.sh" "$@"
