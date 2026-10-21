#!/usr/bin/env python3
"""Summarise BSD files the way the level loader reads them (formats.md, BSD).

  fmt_bsd.py FILE.BSD...          one line per file, then a total of failed checks
  fmt_bsd.py -v FILE.BSD          also the header, the lights, the models, streaming and kind-5 records

Checks: the 0x800-byte size table (ascending, one entry per model after the first, the last
covering the body), model k's data ending before size[k-1], the shared animation tables'
offsets (+0x544..+0x588) staying inside their sections.
"""
import struct
import sys

BODY = 0x800
SECTIONS = [  # (name, table, count, entry size, field holding an offset, base table, base count, base entry size)
    ("anim table", 0x544, 0x548, 8, 4, 0x54C, 0x550, 0x14),
    ("anim frames", 0x54C, 0x550, 0x14, 0x10, 0x554, 0x558, 8),
    ("face tables", 0x564, 0x568, 8, 0, 0x56C, 0x570, 0x1C),
    ("vertex table index", 0x574, 0x578, 8, 0, 0x57C, 0x580, 8),
    ("vertex tables", 0x57C, 0x580, 8, 0, 0x584, 0x588, 8),
]
MODEL_PTRS = (0x04, 0x0C, 0x24, 0x2C, 0x30, 0x34, 0x88, 0x90, 0xB4, 0xC0, 0xC4)


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def s16(d, o):
    return struct.unpack_from("<h", d, o)[0]


def records(d):
    et = BODY + u32(d, BODY + 0x53C)
    n = u32(d, et)
    base = et + 16 + n * 8
    for i in range(n):
        r = base + u32(d, et + 16 + i * 8 + 4)
        yield i, r, u32(d, r), u32(d, r + 4), u32(d, r + 0xC)


def check(d):
    errs = []
    h = BODY
    n = u32(d, 0)
    sizes = struct.unpack_from(f"<{n}I", d, 4)
    models = u32(d, h + 0x5A4)
    body = len(d) - BODY
    if list(sizes) != sorted(sizes) or models != n + 1 or not 0 <= sizes[-1] - body < 0x800:
        errs.append("size table")
    for k in range(1, models):
        m = h + 0x5A8 + k * 0x100
        ptrs = [u32(d, m + o) for o in MODEL_PTRS]
        ptrs = [p for p in ptrs if p not in (0, 0xFFFFFFFF)]
        if ptrs and max(ptrs) >= sizes[k - 1]:
            errs.append(f"model {k} past size[{k - 1}]")
    for name, tab, cnt, esz, fld, btab, bcnt, besz in SECTIONS:
        t, c, b, bc = u32(d, h + tab), u32(d, h + cnt), u32(d, h + btab), u32(d, h + bcnt)
        for i in range(c):
            off = u32(d, h + t + i * esz + fld)
            if off != 0xFFFFFFFF and off >= bc * besz:
                errs.append(f"{name} {i}")
                break
    return errs


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv
    bad = 0
    for f in args:
        d = open(f, "rb").read()
        h = BODY
        errs = check(d)
        bad += bool(errs)
        tsp = d[h:h + 0x80].split(b"\0")[0].decode()
        print(f"{f.split('/')[-1]:16} models {u32(d, h + 0x5A4):3} tsp '{tsp}' files {u32(d, h + 0x80)} slots "
              f"{u32(d, h + 0x84)} first {u32(d, h + 0x88)} lights {u32(d, h + 0xD8)} fog {s16(d, h + 0xCC)} "
              f"view {u32(d, h + 0x590)} {'; '.join(errs) or 'ok'}")
        if not verbose:
            continue
        print(f"  prim pool {u32(d, h + 0x98):#x}  fog colour {d[h + 0xCE:h + 0xD1].hex()}  +0xD4 {s16(d, h + 0xD4)}"
              f"  sky {d[h + 0x58C:h + 0x58F].hex()} stars {d[h + 0x58F]}  moon y/z {s16(d, h + 0x594)} {s16(d, h + 0x596)}")
        for i in range(u32(d, h + 0xD8)):
            e = h + 0xDC + i * 0x14
            cnt, off = u32(d, e), u32(d, e + 4)
            cols = [u32(d, h + off + 4 * k) for k in range(cnt)]
            print(f"  light {i:2}: {cnt:3} steps " + " ".join(f"{c & 0xFFFFFF:06x}/{c >> 24}" for c in cols[:6])
                  + (" ..." if cnt > 6 else ""))
        for k in range(u32(d, h + 0x5A4)):
            m = h + 0x5A8 + k * 0x100
            set_ = [f"{o:#x}" for o in MODEL_PTRS if u32(d, m + o) not in (0, 0xFFFFFFFF)]
            print(f"  model {k:2} {u32(d, m):08x} type {u32(d, m + 0xFC):#x} base {u32(d, m + 0xE8):08x} "
                  f"verts {struct.unpack_from('<H', d, m + 0x8C)[0]} offsets {' '.join(set_)}")
        for i, r, hsh, size, kind in records(d):
            if hsh == 0x4D078F23:
                print(f"  record {i}: stream chunks {struct.unpack_from('<4h', d, r + 0x30)}")
            if kind == 5:
                lists = [f"+{0x38 + 4 * j:X}" for j in range(7) if u32(d, r + 0x38 + 4 * j)]
                print(f"  record {i}: kind 5, shape {struct.unpack_from('<4h', d, r + 0x28)} lists {lists}")
    print(f"{len(args)} files, {bad} with failed checks")


if __name__ == "__main__":
    main()
