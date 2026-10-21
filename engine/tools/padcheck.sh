#!/bin/bash
# engine/tools/padcheck.sh [-j N] [--stick-only] TAPE...: the core's player from the recorded pad against the
# original, for every level start and restart (after a death) of each recording: capture.sh twice per recording
# (once to find the starts, once with a RAM dump 7 vblanks into each), export_pad.py, coretest (with
# aim_update against the trace; the recordings' projection is 4:3 and the original's FOV).
# Captures go to build/gd/cap/NAME; a summary line per segment to build/gd/cap/padcheck.txt.
# A recording made with mouse and keyboard takes the analog roles from the trace (the runtime adds
# them); with --stick-only (recordings without: monkey runs, gamepads) the core computes them from
# the sticks.
cd "$(dirname "$0")/../.."
jobs=4
if [ "$1" = -j ]; then jobs=$2; shift 2; fi
export STICK=""
if [ "$1" = --stick-only ]; then STICK=--stick-only; shift; fi
one() {
    tape=$1; name=$(basename "$tape"); out=build/gd/cap/$name
    engine/tools/capture.sh "$tape" "$out" 200000
    starts=$(python3 -c "
import json, sys
last, hp = 0, 1
for line in open('$out/state.jsonl'):
    try: d = json.loads(line)
    except json.JSONDecodeError: break
    sol = [e for e in d['ents'] if e[1] == 1]
    h = sol[0][9] if sol else 0
    # a level's start, or a restart in place after a death (the player alive again; the tick
    # counter goes on)
    if d['tick'] < last or d['tick'] == 1 or (hp <= 0 < h): print(d['vblank'] + 7)
    last, hp = d['tick'], h")
    [ -z "$starts" ] && { echo "$name: no level start" >> build/gd/cap/padcheck.txt; return; }
    engine/tools/capture.sh "$tape" "$out" 200000 $starts
    for v in $starts; do
        [ -f "$out/d$v.bin" ] || continue
        python3 engine/tools/export_pad.py "$out" "$v" > "$out/pad_$v.txt" 2> "$out/pad_$v.err" || continue
        build/gd/cmake/coretest "$out/pad_$v.txt" --ram "$out/d$v.bin" --stream-log "$out/run.log" $STICK -v 20 \
            --aim-log "$out/run.log" --projection 4:3,0 \
            > "$out/coretest_$v.txt"
        echo "$name @$v: $(grep "from the pad" "$out/coretest_$v.txt" | tail -1)" >> build/gd/cap/padcheck.txt
        echo "$name @$v: $(grep "^aim_update" "$out/coretest_$v.txt" | tail -1)" >> build/gd/cap/padcheck.txt
    done
}
export -f one
printf '%s\n' "$@" | xargs -P "$jobs" -I{} bash -c 'one {}'
