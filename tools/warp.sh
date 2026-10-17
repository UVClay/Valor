#!/usr/bin/env bash
# Start MoH directly in a mission/level (1-based indices as the game counts them), for testing.
#   tools/warp.sh MISSION LEVEL [runtime args...]   e.g. tools/warp.sh 3 2 --headless --max-cycles ...
# It presses through New Game, then on the level overlay's first call overrides the mission and
# level the shell selected (exe globals at 0x800385CC+0x94/+0x98, 0-based).
set -euo pipefail
cd "$(dirname "$0")/.."
mission=$1 level=$2
shift 2
presses=()
for f in $(seq 7300 90 9000); do presses+=(--press "$f:cross"); done
exec ./build/cmake/valor --bios build/openbios/openbios.bin \
    --disc "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" \
    --poke-on-call "80064374:80038660=$((mission - 1))" \
    --poke-on-call "80064374:80038664=$((level - 1))" \
    "${presses[@]}" "$@"
