#!/bin/bash
# engine/tools/capture.sh TAPE OUT STOP [VBLANK...]: replays a recording through the original and
# keeps what coretest compares the core with, in OUT/: the state after every tick (state.jsonl),
# the player's input per tick and every control-role call (VALOR_TRACE_INPUT, in run.log), the
# watched player fields (WATCH below, in run.log), and a RAM dump at each VBLANK (dV.bin: one a few
# vblanks into each level seeds what a watch only logs as changes). Container gd_cap_NAME; the
# binary is $RP_BIN (build/gd/valor-gd: the runtime with the stick bytes in the input trace).
set -e
tape=$1; out=$2; stop=$3; shift 3
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
mkdir -p "$out"
# s: the soldier from +0x1F0; l: the look/weapon block (inv); k: the chunk slots' rectangles;
# o: +0x3BC; e: the soldier's first 0x40 bytes (flags A/B, contact); c: the camera's eye offset;
# cs/cl: the streaming slots' chunks and the slot being read; ck: the camera's shake (amplitude, count).
WATCH="s=[8009D654]+1F0:100;l=[[8009D654]+388]+0:1D0;k=80090DE8:A0;o=[8009D654]+3BC:C;e=[8009D654]+0:40"
WATCH+=";c=[[8009D638]+388]+94:4;cs=80098970:10;cl=800AA184:4;ck=[[8009D638]+388]+CC:4"
args=()
for v in "$@"; do args+=(--dump-ram "$v:$out/d$v.bin"); done
name="gd_cap_$(basename "$out")"
image="moh-recomp-dev:$(cat Containerfile requirements.txt | sha256sum | cut -c1-12)"
podman rm -f "$name" >/dev/null 2>&1 || true
timeout -s KILL 2400 podman run --rm --name "$name" --userns=keep-id --security-opt label=disable \
    -v "$root:/work" -w /work -e HOME=/work/build/home -e XDG_RUNTIME_DIR=/tmp -e SDL_VIDEODRIVER=dummy \
    -e VALOR_TRACE_STATE="$out/state.jsonl" -e VALOR_TRACE_INPUT=1 -e VALOR_TRACE_AIM=1 -e VALOR_WATCH="$WATCH" \
    "$image" "${RP_BIN:-build/gd/valor-gd}" --bios build/openbios/openbios.bin \
    --disc "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" --replay "$tape" --hidden \
    --stop-at "$stop" "${args[@]}" > "$out/run.log" 2>&1 || true
podman rm -f "$name" >/dev/null 2>&1 || true
