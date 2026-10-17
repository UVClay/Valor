#!/usr/bin/env python3
"""Extract the ISO9660 filesystem from a PS1 BIN/CUE (MODE2/2352) image.

Files made of Form 1 sectors are written as plain 2048-byte-per-sector data.
Files containing any Form 2 sectors (XA audio / STR video) are written as raw
2352-byte sectors so no data is lost.

Usage: extract_disc.py <image.cue|image.bin> <out_dir>
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

SECTOR = 2352
USER_OFFSET = 24  # 12 sync + 4 header + 8 subheader
SUBMODE_FORM2 = 0x20

KNOWN_IMAGES = {
    "cf503f1dab8d6f9ae25d11b324b7c251651f5fff": "moh",   # Medal of Honor (USA)
    "3d4ed9b1c7fb99acd96dd9c6ce2749a40cb8a1c2": "mohu",  # Medal of Honor - Underground (USA)
}


def resolve_bin(path: Path) -> Path:
    if path.suffix.lower() != ".cue":
        return path
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.upper().startswith("FILE"):
            name = line.split('"')[1]
            return path.parent / name
    sys.exit(f"no FILE entry in {path}")


class Disc:
    def __init__(self, path: Path):
        self.f = open(path, "rb")

    def raw(self, lba: int) -> bytes:
        self.f.seek(lba * SECTOR)
        return self.f.read(SECTOR)

    def user(self, lba: int) -> bytes:
        return self.raw(lba)[USER_OFFSET:USER_OFFSET + 2048]

    def read(self, lba: int, size: int) -> bytes:
        count = (size + 2047) // 2048
        return b"".join(self.user(lba + i) for i in range(count))[:size]


def walk(disc: Disc, lba: int, size: int, path: str = ""):
    data = disc.read(lba, size)
    i = 0
    while i < len(data):
        length = data[i]
        if length == 0:
            i = (i // 2048 + 1) * 2048
            continue
        rec = data[i:i + length]
        i += length
        name = rec[33:33 + rec[32]]
        if name in (b"\0", b"\1"):
            continue
        elba = struct.unpack_from("<I", rec, 2)[0]
        esize = struct.unpack_from("<I", rec, 10)[0]
        full = f"{path}/{name.decode('latin1').split(';')[0]}"
        if rec[25] & 2:
            yield from walk(disc, elba, esize, full)
        else:
            yield full.lstrip("/"), elba, esize


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path)
    ap.add_argument("out", type=Path)
    args = ap.parse_args()

    bin_path = resolve_bin(args.image)
    sha1 = hashlib.sha1()
    with open(bin_path, "rb") as f:
        while chunk := f.read(1 << 20):
            sha1.update(chunk)
    digest = sha1.hexdigest()
    game = KNOWN_IMAGES.get(digest)
    print(f"{bin_path.name}: sha1 {digest} ({game or 'UNKNOWN IMAGE'})")
    if game is None:
        print("warning: image does not match a known dump; continuing anyway", file=sys.stderr)

    disc = Disc(bin_path)
    pvd = disc.user(16)
    if pvd[1:6] != b"CD001":
        sys.exit("no ISO9660 primary volume descriptor at LBA 16")
    root_lba, root_size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]

    manifest = []
    for rel, lba, size in walk(disc, root_lba, root_size):
        nsec = (size + 2047) // 2048
        form2 = any(disc.raw(lba + i)[18] & SUBMODE_FORM2 for i in range(nsec))
        dest = args.out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if form2:
            with open(dest, "wb") as out:
                for i in range(nsec):
                    out.write(disc.raw(lba + i))
        else:
            dest.write_bytes(disc.read(lba, size))
        manifest.append({"path": rel, "lba": lba, "size": size, "raw": form2})

    (args.out / "manifest.json").write_text(json.dumps({"image_sha1": digest, "game": game, "files": manifest}, indent=1))
    print(f"extracted {len(manifest)} files to {args.out} ({sum(m['raw'] for m in manifest)} raw/form2)")


if __name__ == "__main__":
    main()
