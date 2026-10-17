#!/usr/bin/env bash
# Crash sweep: warp into every single-player level on the disc and play it with random input.
#   tools/sweep.sh [MINUTES_OF_CONSOLE_TIME] [SEED]
# Results and logs go to build/sweep/.
set -uo pipefail
cd "$(dirname "$0")/.."
minutes=${1:-20}
seed=${2:-1}
cycles=$((minutes * 60 * 33868800 / 2))
out=build/sweep
mkdir -p "$out"
levels=()
for dir in extracted/moh/DATA/MSN*/LVL*; do
    m=${dir#*MSN}; m=${m%%/*}; l=${dir##*LVL}
    [ "$m" = 12 ] && continue  # multiplayer arenas
    levels+=("$m:$l")
done
run() {
    local m=${1%:*} l=${1#*:}
    tools/warp.sh "$m" "$l" --headless --memcard "$out/card_${m}_${l}.mcd" --max-cycles "$cycles" \
        --monkey "$seed:9700" > "$out/m${m}_l${l}.log" 2>&1
    echo "mission $m level $l: $(grep -m1 '^halt:' "$out/m${m}_l${l}.log")"
}
export -f run
export out cycles seed
printf '%s\n' "${levels[@]}" | xargs -P "$(nproc)" -I{} bash -c 'run {}' | sort -V
