#!/bin/sh
# Scripted test routes (LARECOMP_INPUT button presses by frame), for checking the game's scenes and screenshots.
# Usage: tools/route.sh <route> [seconds] [screenshot frames, comma separated]
#   menus       existing save: title -> hotel room -> casino (the locked Roulette game)        ~160 s
#   volleyball  fresh save: opening movie, Kasumi's arrival, Niki Beach -> Beach Volleyball     ~420 s
#   relax       fresh save: as volleyball but Relax (the shower scene), evening, night           ~330 s
# Fresh-save routes move save/ aside first and put it back afterwards (the game autosaves).
# Frames are presented frames (30 fps); load times shift them a little between runs.
cd "$(dirname "$0")/.." || exit 1
route=$1 secs=$2 shots=$3
seq_presses() {  # <button> <from> <step> <to>
    for f in $(seq "$2" "$3" "$4"); do printf ',%s:%s:8' "$f" "$1"; done
}
fresh=0
case "$route" in
menus)
    in="400:A:10,1000:A:10,1300:A:10,1700:A:10,2100:A:10,2500:A:10,2900:A:10,3300:A:10,3700:B:10,4100:B:10,4500:B:10,4900:A:10"
    : "${secs:=160}" ;;
volleyball)
    fresh=1
    in="400:A:10,1100:A:10,1600:A:10,1800:A:10$(seq_presses A 2300 120 5900)"
    for f in $(seq 6020 120 6740); do in="$in,$((f - 50)):DOWN:8,$f:A:8"; done  # (the location menu: Beach Volleyball)
    in="$in,6900:B:8,7100:B:8$(seq_presses A 7300 45 12000)"                     # (the tutorial's Start Game, then play)
    : "${secs:=420}" ;;
relax)
    fresh=1
    in="400:A:10,1100:A:10,1600:A:10,1800:A:10$(seq_presses A 2300 120 9500)"
    : "${secs:=330}" ;;
*)
    sed -n '2,9p' "$0"; exit 1 ;;
esac
if [ "$fresh" = 1 ]; then
    rm -rf save.route-backup
    [ -d save ] && mv save save.route-backup
fi
[ -n "$shots" ] && export LARECOMP_SHOT_AT="$shots"
LARECOMP_INPUT="$in" tools/run.sh "$secs"
if [ "$fresh" = 1 ]; then
    rm -rf save
    [ -d save.route-backup ] && mv save.route-backup save
fi
