#!/usr/bin/env bash
# Build OpenBIOS (third_party/nugget) into build/openbios/openbios.elf.
# The nugget Makefiles build in-tree, so we build a copy to keep the submodule clean.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/third_party/nugget"
work="$root/build/openbios/nugget"
out="$root/build/openbios"

if [ ! -f "$src/openbios/Makefile" ]; then
    echo "third_party/nugget is missing: run 'git submodule update --init'" >&2
    exit 1
fi

mkdir -p "$out"
rm -rf "$work"
cp -a "$src" "$work"

# Fedora's mips64-linux-gnu toolchain handles mips1/o32/little-endian with nugget's
# flags; only the BFD output name differs from mipsel-none-elf.
make -C "$work/openbios" -j"$(nproc)" \
    PREFIX=mips64-linux-gnu FORMAT=elf32-tradlittlemips BUILD=Release BOOT_MODE=fast

cp "$work/openbios/openbios.elf" "$work/openbios/openbios.bin" "$out/"
echo "built $out/openbios.elf"
