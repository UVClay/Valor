#!/usr/bin/env python3
"""Decode a Medal of Honor save: the g_game block (0x780 bytes) of a memory card file.

  shell_save.py FILE [--raw]

FILE: a raw card image (.mcd, 128 KB: every MoH save on it), a single save (.mcs: 0x80-byte
directory frame + the 8 KB block) or a RAM dump (2 MB: g_game at 0x800385CC).
The layout is docs/re/shell.md's "The save".
"""
import struct
import sys
from pathlib import Path

SLOTS = [(0, 3), (1, 4), (2, 4), (3, 3), (4, 4), (6, 3), (7, 3), (8, 3)]  # g_campaign: mission, levels
CHEATS = ["DENNISMODE", "SPRECHEN", "BRADALARM", "TRACERON", "ICOSIDODEC", "GOBLUE", "BADCOPSHOW", "MOSTMEDALS"]


def games(data):
    if len(data) == 0x200000:
        yield "RAM", data[0x385CC:0x385CC + 0x780]
    elif data[:2] == b"MC" and len(data) == 0x20000:
        for i in range(1, 16):
            f = data[i * 128:(i + 1) * 128]
            if f[0] == 0x51 and f[0xA:0x16] == b"BASLUS-00974":
                blk = data[i * 0x2000:(i + 1) * 0x2000]
                yield f[0xA:0x1E].split(b"\0")[0].decode(), blk[0x200:0x980]
    else:
        yield data[0xA:0x1E].split(b"\0")[0].decode(), data[0x80 + 0x200:0x80 + 0x980]


def show(name, g):
    w = lambda o: struct.unpack_from("<i", g, o)[0]
    h = lambda o: struct.unpack_from("<h", g, o)[0]
    print(f"== {name}: magic {g[0:4]!r} {g[0x77C:0x780]!r}, codename {g[4:0xF].split(b'\0')[0].decode()!r}")
    print(f"mission {w(0x94)} level {w(0x98)} lives {w(0x9C)} slot {w(0xA8)} slot level {w(0xAC)}")
    print(f"career +10..+3C: {[w(o) for o in range(0x10, 0x40, 4)]}")
    print(f"kills per weapon +40..+70: {[w(o) for o in range(0x40, 0x74, 4)]}")
    print(f"+74 {w(0x74)}  MP characters +78 {w(0x78):#x}  MP arenas +7C {w(0x7C):#x}")
    print(f"audio: sfx {w(0x88)} music {w(0x80)} mono {w(0x84)} ambience-only {w(0x8C)}  difficulty {g[0x110]}")
    print(f"medals +D0 {w(0xD0):#x}  cheats unlocked +D4 {w(0xD4):#x}  on +D8 {w(0xD8):#x}")
    print("controls p1 " + " ".join(f"{w(o)}" for o in range(0xDC, 0xF0, 4)) +
          "  p2 " + " ".join(f"{w(o)}" for o in range(0xF0, 0x104, 4)) +
          f"  player {w(0x104)} configs {w(0x108)} {w(0x10C)}")
    for s, (m, n) in enumerate(SLOTS):
        b = 0x18C + m * 0x98
        rows = []
        for lv in range(4):
            r = b + lv * 0x24
            rows.append(f"L{lv + 1}: open {w(r + 0x20)} best {h(r + 0x1A)} hp {w(r + 0x1C) / 65536:g}")
        print(f"slot {s} MSN{m + 1}: gallery {w(b + 0x90)} done {w(b + 0x94)} | " + " | ".join(rows[:n if m != 8 else 4]))


def main():
    data = Path(sys.argv[1]).read_bytes()
    for name, g in games(data):
        show(name, g)


if __name__ == "__main__":
    main()
