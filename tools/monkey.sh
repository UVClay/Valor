#!/usr/bin/env bash
# Crash hunting: run several seeded random-input sessions in parallel, headless and unthrottled.
#   tools/monkey.sh [SEEDS] [MINUTES_OF_CONSOLE_TIME]
# Each run navigates to Mission 1 with scripted presses, then plays randomly. Logs and the
# halt report of any crash land in build/monkey/.
set -uo pipefail
cd "$(dirname "$0")/.."
seeds=${1:-8}
minutes=${2:-60}
cycles=$((minutes * 60 * 33868800 / 2))
out=build/monkey
mkdir -p "$out"
presses=()
for f in $(seq 7300 90 9000); do presses+=(--press "$f:cross"); done
run() {
    local seed=$1
    ./build/cmake/valor --bios build/openbios/openbios.bin \
        --disc "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" --headless \
        --memcard "$out/card$seed.mcd" --max-cycles "$cycles" \
        "${presses[@]}" --monkey "$seed:9700" > "$out/run$seed.log" 2>&1
    local verdict
    verdict=$(grep -m1 "^halt:" "$out/run$seed.log")
    echo "seed $seed: ${verdict:-no halt line}"
}
for s in $(seq 1 "$seeds"); do run "$s" & done
wait
