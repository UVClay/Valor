#!/usr/bin/env python3
"""The front end's screen scripts (global\\script\\m*.sst in DATA/GLOBAL.RSC): screens, their
buttons and the screen each button leads to.

  shell_sst.py [RSC] [--sprites]     one line per screen and button (RSC: extracted/moh/DATA/GLOBAL.RSC)

RSC archive: name[0x40], u32 count, u32 ?, then count entries of 0x50 bytes:
name[0x40], u32 index, u32 size, u32 offset (from the file's start), u32 0.
.sst: a stream of records, each starting with a u32 tag; the sizes (tag included) are the
loader's (SHELL 0x80049E90): 1 screen 0x20 (name[0x1C]), 2 button 0x40 (name[0x1C], the
screen it leads to [0x1C], u32), 3 sprite 0x48 (TIM name[0x1C], ...), 4 0x4C, 5 and 12: 4,
6 0x50, 7 0x28, 8 0x118, 10 0x74, 11 0x124, any other tag 0xC + 4 * the u32 at +4.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def entries(rsc):
    n = struct.unpack_from("<I", rsc, 0x40)[0]
    for i in range(n):
        o = 0x48 + i * 0x50
        name = rsc[o:o + 0x40].split(b"\0")[0].decode()
        _, size, off, _ = struct.unpack_from("<4I", rsc, o + 0x40)
        yield name, rsc[off:off + size]


def cstr(b):
    return b.split(b"\0")[0].decode("latin-1")


SIZES = {1: 0x20, 2: 0x40, 3: 0x48, 4: 0x4C, 5: 4, 6: 0x50, 7: 0x28, 8: 0x118, 10: 0x74, 11: 0x124, 12: 4}


def parse(sst, sprites=False):
    o = 0
    out = []
    while o + 4 <= len(sst):
        tag = struct.unpack_from("<I", sst, o)[0]
        size = SIZES.get(tag) or 0xC + 4 * struct.unpack_from("<I", sst, o + 4)[0]
        rec = sst[o:o + size]
        if tag == 1:
            out.append(f"screen {cstr(rec[4:0x20])}")
        elif tag == 2:
            out.append(f"  button {cstr(rec[4:0x20])} -> {cstr(rec[0x20:0x3C])}")
        elif tag in (3, 4, 6) and sprites:
            out.append(f"    {tag} {cstr(rec[4:0x20])}")
        elif tag not in (3, 4, 6, 5, 12) and (sprites or tag in (8, 10, 11)):
            name = cstr(rec[4:0x20]) if 0x20 <= size and rec[4:5].isalpha() else ""
            out.append(f"    tag {tag} (0x{size:X} bytes) {name}")
        o += size
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = Path(args[0]) if args else ROOT / "extracted/moh/DATA/GLOBAL.RSC"
    for name, data in entries(path.read_bytes()):
        if name.endswith(".sst"):
            print(f"# {name}")
            print("\n".join(parse(data, "--sprites" in sys.argv)))


if __name__ == "__main__":
    main()
