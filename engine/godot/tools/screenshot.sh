#!/bin/bash
# engine/godot/tools/screenshot.sh OUT.png [user args...]: runs the Godot project on a virtual
# display (Xvfb, software OpenGL) in a container named gd_shot and saves one frame, e.g.
#   engine/godot/tools/screenshot.sh build/gd/shot.png --level=1:3 --walk=30 --frames=90
set -e
out=$1; shift
root="$(cd "$(dirname "$0")/../../.." && pwd)"
image="valor-godot-xvfb:$(sha256sum "$root/engine/godot/tools/Containerfile.xvfb" | cut -c1-12)"
podman image exists "$image" || podman build -t "$image" -f "$root/engine/godot/tools/Containerfile.xvfb" "$root/engine/godot/tools"
godot=${GODOT:-build/godot/Godot_v4.7.2-stable_linux.x86_64}
podman rm -f gd_shot >/dev/null 2>&1 || true
timeout -s KILL 300 podman run --rm --name gd_shot --userns=keep-id --security-opt label=disable \
    -v "$root:/work" -w /work -e HOME=/work/build/home "$image" \
    xvfb-run -a -s "-screen 0 1280x720x24" "$godot" --path engine/godot/project \
    --rendering-driver opengl3 ${GODOT_ARGS} -- --screenshot="/work/$out" "$@"
