#!/bin/bash
# engine/godot/tools/package.sh: runnable packages of the Godot project in build/dist/, for Linux
# (valor-linux-x86_64/) and Windows (valor-windows-x86_64/), with no disc data in them: the player
# points the game at their own disc image on the first run (the front end's YOUR DISC, or
# --disc-image=PATH).
#
#   1. The official Godot 4.7.2 export templates (build/godot/, downloaded from the release and
#      checked against its SHA512-SUMS.txt) are installed for the export.
#   2. Both GDExtensions are built as template_release: Linux in the dev container, Windows with
#      MinGW-w64 (Containerfile.win, mingw64.cmake; only KERNEL32 and msvcrt imported).
#   3. The two presets of export_presets.cfg are exported, and each pack is listed (exported again
#      as a .zip) to check that only the project's own files went in.
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
ver=4.7.2
godot=build/godot/Godot_v${ver}-stable_linux.x86_64
tpz=Godot_v${ver}-stable_export_templates.tpz
xvfb="valor-godot-xvfb:$(sha256sum engine/godot/tools/Containerfile.xvfb | cut -c1-12)"
mingw="valor-mingw:$(sha256sum engine/godot/tools/Containerfile.win | cut -c1-12)"
run() {  # run IMAGE CMD: in a container with the repository at /work
    local img=$1; shift
    podman run --rm --userns=keep-id --security-opt label=disable -v "$root:/work" -w /work \
        -e HOME=/work/build/home "$img" "$@"
}

echo "== the export templates"
if [ ! -f "build/godot/$tpz" ]; then
    curl -sSL -o "build/godot/$tpz" "https://github.com/godotengine/godot/releases/download/${ver}-stable/$tpz"
fi
curl -sSL -o build/godot/SHA512-SUMS.release.txt "https://github.com/godotengine/godot/releases/download/${ver}-stable/SHA512-SUMS.txt"
(cd build/godot && grep " $tpz\$" SHA512-SUMS.release.txt | sha512sum -c -)
tdir=build/home/.local/share/godot/export_templates/${ver}.stable
mkdir -p "$tdir"
unzip -o -j -q "build/godot/$tpz" templates/version.txt templates/linux_release.x86_64 \
    templates/windows_release_x86_64.exe templates/windows_release_x86_64_console.exe -d "$tdir"

echo "== the extensions, release builds"
run "moh-recomp-dev:$(cat Containerfile requirements.txt | sha256sum | cut -c1-12)" sh -c '
    cmake -S engine -B build/rel/engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release >/dev/null &&
    cmake --build build/rel/engine -j8 --target valor_godot | tail -1 &&
    cmake -S engine/godot/present -B build/rel/gdp -G Ninja -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release >/dev/null &&
    cmake --build build/rel/gdp -j8 | tail -1'
podman image exists "$mingw" || podman build -t "$mingw" -f engine/godot/tools/Containerfile.win engine/godot/tools
run "$mingw" sh -c '
    set -e
    for p in "engine build/win/engine valor_godot" "engine/godot/present build/win/gdp valor_present"; do
        set -- $p
        cmake -S $1 -B $2 -G Ninja -DCMAKE_TOOLCHAIN_FILE=/work/engine/godot/tools/mingw64.cmake \
            -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_release -DGODOTCPP_USE_STATIC_CPP=ON >/dev/null
        cmake --build $2 -j8 --target $3 | tail -1
    done
    cp build/win/engine/godot/libvalor.windows.template_release.x86_64.dll \
       build/win/gdp/libvalor_present.windows.template_release.x86_64.dll engine/godot/project/bin/
    for d in engine/godot/project/bin/*.dll; do echo "$d imports:" $(x86_64-w64-mingw32-objdump -p $d | grep "DLL Name" | cut -d: -f2); done'

echo "== the exports"
mkdir -p build/dist/valor-linux-x86_64 build/dist/valor-windows-x86_64 build/dist/check
run "$xvfb" sh -c "
    $godot --headless --path engine/godot/project --import >/dev/null 2>&1
    $godot --headless --path engine/godot/project --export-release Linux /work/build/dist/valor-linux-x86_64/valor.x86_64 2>&1 | grep -iE 'error|export' | grep -v ALSA || true
    $godot --headless --path engine/godot/project --export-release Windows /work/build/dist/valor-windows-x86_64/valor.exe 2>&1 | grep -iE 'error|export' | grep -v ALSA || true
    $godot --headless --path engine/godot/project --export-pack Linux /work/build/dist/check/linux.zip >/dev/null 2>&1
    $godot --headless --path engine/godot/project --export-pack Windows /work/build/dist/check/windows.zip >/dev/null 2>&1"
for d in valor-linux-x86_64 valor-windows-x86_64; do
    cp engine/godot/tools/dist_README.txt "build/dist/$d/README.txt"
done

echo "== no disc data"
python3 - <<'EOF'
import os, zipfile
bad = []
# the disc's own file types (formats.md), none of which the project holds
disc_ext = {".bsd", ".tsp", ".taf", ".vb", ".rsc", ".str", ".tim", ".bin", ".74", ".sst", ".xa"}
for name in ("linux", "windows"):
    z = zipfile.ZipFile(f"build/dist/check/{name}.zip")
    names = z.namelist()
    for n in names:
        own = n.startswith(".godot/") and os.path.basename(n) in ("uid_cache.bin", "global_script_class_cache.cfg", "extension_list.cfg")
        if (os.path.splitext(n.lower())[1] in disc_ext and not own) or "extracted" in n.lower():
            bad.append(f"{name}: {n}")
    print(f"{name}: the pack holds {len(names)} files, {sum(i.file_size for i in z.infolist())} bytes, all the project's own")
for d in ("valor-linux-x86_64", "valor-windows-x86_64"):
    for f in sorted(os.listdir(f"build/dist/{d}")):
        print(f"  {d}/{f}: {os.path.getsize(f'build/dist/{d}/{f}')} bytes")
if bad:
    raise SystemExit("disc data in a pack: " + ", ".join(bad))
EOF
