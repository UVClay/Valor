#!/bin/bash
# tools/recon/run.sh NAME SECONDS LOG [valor args...]: runs the game in the container (no display:
# replays hold, see the game.cpp near-polygon hook), named rp_NAME, killed after SECONDS.
# SDL_VIDEODRIVER=offscreen gives an OpenGL renderer instead (slower); RP_BIN=path another binary.
name=$1; secs=$2; logf=$3; shift 3
# --warp M:L as ./play does it (without ./play's build step): the next New Game loads that level,
# with four clips per weapon when it isn't a mission's first level.
args=()
while [ $# -gt 0 ]; do
    if [ "$1" = --warp ] && [ $# -ge 2 ]; then
        m=${2%:*} l=${2#*:}
        args+=(--poke-on-call "80064374:80038660=$((m - 1))" --poke-on-call "80064374:80038664=$((l - 1))")
        if [ "$l" -gt 1 ]; then
            ammo=(32 128 4 4 32 20 80 0 28 28 4 80 32 -1)
            row=$((0x80038758 + (m - 1) * 152 + (l - 1) * 36))
            for i in 0 2 4 6 8 10 12; do
                word=$(( (ammo[i] & 0xFFFF) | ((ammo[i + 1] & 0xFFFF) << 16) ))
                args+=(--poke-on-call "$(printf '80064374:%08X=%u' $((row + i * 2)) $word)")
            done
        fi
        shift 2
    else
        args+=("$1"); shift
    fi
done
set -- "${args[@]}"
cd "$(dirname "$0")/../.."
image="moh-recomp-dev:$(cat Containerfile requirements.txt | sha256sum | cut -c1-12)"
env=(-e XDG_RUNTIME_DIR=/tmp/runtime -e SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}")
for var in $(compgen -e | grep '^VALOR_' || true); do env+=(-e "$var"); done
podman rm -f "rp_$name" >/dev/null 2>&1
timeout -s KILL "$secs" podman run --rm --name "rp_$name" --userns=keep-id --security-opt label=disable \
    -v "$PWD:/work" -w /work -e HOME=/work/build/home "${env[@]}" --device /dev/dri \
    -v /etc/localtime:/etc/localtime:ro "$image" "${RP_BIN:-./build/cmake/valor}" \
    --bios build/openbios/openbios.bin --disc "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" "$@" > "$logf" 2>&1
podman kill "rp_$name" >/dev/null 2>&1; podman rm -f "rp_$name" >/dev/null 2>&1
exit 0
