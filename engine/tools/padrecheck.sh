#!/bin/bash
# engine/tools/padrecheck.sh [--stick-only] CAPTURE_DIR...: padcheck.sh's export and coretest again on
# captures it already made (every dV.bin), one summary line per segment.
cd "$(dirname "$0")/../.."
stick=""
if [ "$1" = --stick-only ]; then stick=--stick-only; shift; fi
for out in "$@"; do
    for d in "$out"/d*.bin; do
        v=$(basename "$d" .bin); v=${v#d}
        python3 engine/tools/export_pad.py "$out" "$v" > "$out/pad_$v.txt" 2> "$out/pad_$v.err" || continue
        build/gd/cmake/coretest "$out/pad_$v.txt" --ram "$d" --stream-log "$out/run.log" $stick -v 20 > "$out/coretest_$v.txt"
        echo "$(basename "$out") @$v: $(grep 'from the pad' "$out/coretest_$v.txt")"
    done
done
