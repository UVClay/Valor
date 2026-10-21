#!/bin/bash
# tools/sim/monkey_tape.sh M L SEED STOP NAME: records a monkey run (random input from vblank
# 9700, --monkey) of level M:L (the disc's MSNm/LVLl; New Game warped there, four clips per
# weapon past a mission's first level) to build/simcheck/tapes/NAME, stopping at vblank STOP,
# on a card of its own (never the user's saves). BIN=path picks the runtime (build/cmake/valor).
# For check.py: python3 tools/sim/check.py build/simcheck/tapes/NAME
cd "$(dirname "$0")/../.."
m=$1 l=$2 seed=$3 stop=$4 name=$5
tapes=build/simcheck/tapes
mkdir -p "$tapes"
img="moh-recomp-dev:$(cat Containerfile requirements.txt | sha256sum | cut -c1-12)"
args=(--poke-on-call "80064374:80038660=$((m - 1))" --poke-on-call "80064374:80038664=$((l - 1))")
if [ "$l" -gt 1 ]; then
    ammo=(32 128 4 4 32 20 80 0 28 28 4 80 32 -1)
    row=$((0x80038758 + (m - 1) * 152 + (l - 1) * 36))
    for i in 0 2 4 6 8 10 12; do
        word=$(( (ammo[i] & 0xFFFF) | ((ammo[i + 1] & 0xFFFF) << 16) ))
        args+=(--poke-on-call "$(printf '80064374:%08X=%u' $((row + i * 2)) $word)")
    done
fi
for f in $(seq 7300 90 9000); do args+=(--press "$f:cross"); done
rm -rf "$tapes/$name"
timeout -s KILL 1800 podman run --rm --name "chk_rec_$name" --userns=keep-id --security-opt label=disable \
    -v "$PWD:/work" -w /work -e HOME=/work/build/home -e XDG_RUNTIME_DIR=/tmp/runtime -e SDL_VIDEODRIVER=dummy \
    --device /dev/dri "$img" "${BIN:-build/cmake/valor}" --bios build/openbios/openbios.bin \
    --disc "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" --hidden \
    --memcard "$tapes/card_$name" "${args[@]}" --monkey "$seed:9700" --stop-at "$stop" \
    --record "$tapes/$name" > "$tapes/$name.log" 2>&1
podman rm -f "chk_rec_$name" >/dev/null 2>&1
