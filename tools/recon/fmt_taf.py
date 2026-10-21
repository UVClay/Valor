#!/usr/bin/env python3
"""List a TAF file: the TIM images (uploaded to VRAM in order) and the VAB sound banks after them.

  fmt_taf.py FILE.TAF [FILE2.TAF]   (with two files: which entries differ)

Layout as read by the level loader (0x800869E0, 0x80086A94; formats.md, TAF).
"""
import struct
import sys


def tims(d):
    o, out = 0, []
    while o + 8 <= len(d) and struct.unpack_from("<I", d, o)[0] == 0x10:
        start, flags = o, struct.unpack_from("<I", d, o + 4)[0]
        o += 8
        clut = None
        if flags & 8:
            n, x, y, w, h = struct.unpack_from("<I4H", d, o)
            clut = (x, y, w, h)
            o += n
        n, x, y, w, h = struct.unpack_from("<I4H", d, o)
        o += n
        out.append((start, o - start, flags & 7, (x, y, w, h), clut))
    return out, o


def vabs(d, o):
    count = struct.unpack_from("<I", d, o)[0]
    out = []
    for i in range(count):
        off = struct.unpack_from("<I", d, o + 4 + 4 * i)[0]
        magic, ver, vid, size = struct.unpack_from("<4sIII", d, o + off)
        progs, tones, vags = struct.unpack_from("<HHH", d, o + off + 0x12)
        out.append((o + off, size, progs, tones, vags, magic))
    return count, out


def describe(path):
    d = open(path, "rb").read()
    t, end = tims(d)
    count, v = vabs(d, end)
    return d, t, end, count, v


def main():
    d, t, end, count, v = describe(sys.argv[1])
    bpp = {0: "4-bit", 1: "8-bit", 2: "15-bit", 3: "24-bit"}
    print(f"{len(t)} TIMs, 0x{end:X} bytes; VAB table at 0x{end:X}: {count} banks; file 0x{len(d):X}")
    for i, (o, n, mode, (x, y, w, h), clut) in enumerate(t):
        px_w = w * {0: 4, 1: 2, 2: 1, 3: 1}[mode] if mode != 3 else w * 2 // 3
        c = f" clut {clut[0]},{clut[1]} {clut[2]}x{clut[3]}" if clut else ""
        print(f"  tim {i:3} @0x{o:06X} {bpp.get(mode, mode):6} vram {x:4},{y:3} {w:3}x{h:3} words ({px_w}px wide){c}")
    for i, (o, size, progs, tones, vags, magic) in enumerate(v):
        print(f"  vab {i} @0x{o:06X} {magic!r} size 0x{size:X} programs {progs} tones {tones} samples {vags}")
    if len(sys.argv) > 2:
        d2, t2, end2, _, v2 = describe(sys.argv[2])
        dt = [i for i, (a, b) in enumerate(zip(t, t2)) if d[a[0]:a[0] + a[1]] != d2[b[0]:b[0] + b[1]]]
        dv = [i for i, (a, b) in enumerate(zip(v, v2)) if d[a[0]:a[0] + a[1]] != d2[b[0]:b[0] + b[1]] or a[1] != b[1]]
        print(f"differ: tims {dt} ({len(t)} vs {len(t2)}), vabs {dv} ({len(v)} vs {len(v2)})")


if __name__ == "__main__":
    main()
