#!/usr/bin/env python3
"""Dump a model's skeleton and animations from a RAM dump (docs/re/animation.md).

usage: anim_dump.py RAMDUMP MODEL_INDEX [--anim N] [--frames] [--bones]
MODEL_INDEX indexes g_models (0x8009EC58, 0x100 bytes each).
"""
import struct, argparse

def sx(v, lo, n):
    v = (v >> lo) & ((1 << n) - 1)
    return v - (1 << n) if v >> (n - 1) else v

def unpack3(w):  # x: bits 0-9, y: bits 10-20, z: bits 21-30 (signed)
    return sx(w, 0, 10), sx(w, 10, 11), sx(w, 21, 10)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ram'); ap.add_argument('model', type=int)
    ap.add_argument('--anim', type=int); ap.add_argument('--frames', action='store_true')
    ap.add_argument('--bones', action='store_true')
    a = ap.parse_args()
    d = open(a.ram, 'rb').read()
    u32 = lambda p: struct.unpack_from('<I', d, p & 0x1FFFFF)[0]
    u16 = lambda p: struct.unpack_from('<H', d, p & 0x1FFFFF)[0]
    s16 = lambda p: struct.unpack_from('<h', d, p & 0x1FFFFF)[0]
    u8 = lambda p: d[p & 0x1FFFFF]
    m = u32(0x8009EC58) + a.model * 0x100
    print(f'model {m:08X} hash {u32(m):08X} type {u32(m+0xFC):#x} parts {u16(m+0xBE)}')
    root = u32(m + 0x9C)
    if a.bones and root:
        def walk(b, depth, parent):
            idx = u16(b); ox, oy, oz = s16(b+2), s16(b+4), s16(b+6)
            vt = u32(m + 0x84)
            nv = u32(u32(vt) + idx * 8 + 4) if vt else 0
            print(f'{"  "*depth}bone {idx:2d} offset ({ox},{oy},{oz}) +8 {u16(b+8):04X} {u16(b+10):04X} verts {nv}')
            if u32(b + 0xC): walk(u32(b + 0xC), depth + 1, idx)
            if u32(b + 0x10): walk(u32(b + 0x10), depth, parent)
        walk(root, 0, -1)
    at = u32(m + 8)
    if not at:
        return
    n = u16(at)
    print(f'anim table {at:08X}: {n} anims (pad {u16(at+2):04X})')
    for i in range(n):
        if a.anim is not None and i != a.anim:
            continue
        e = u32(at + 4 + 4 * i)
        if not e or e == 0xFFFFFFFF:
            continue
        nf, nb, alt = u8(e), u8(e + 1), u8(e + 2)
        fr = u32(e + 4)
        keys = []
        tot = [0, 0, 0]; rot = [0, 0, 0]
        for f in range(nf):
            p = fr + 20 * f
            mv = unpack3(u32(p)); 
            for k in range(3): tot[k] += mv[k]; rot[k] += s16(p + 8 + 2*k)
            if u32(p + 0x10): keys.append(f)
        print(f'anim {i:3d}: {nf:3d} frames, {nb} bones, mesh {alt}, keys every {sorted(set(b-a for a,b in zip(keys,keys[1:])))}, '
              f'root motion total {tot}, rotation total {rot}')
        if a.frames:
            for f in range(nf):
                p = fr + 20 * f
                il = u8(p + 0xE)
                print(f'   f{f:3d} move {unpack3(u32(p))} offset {unpack3(u32(p+4))} rot ({s16(p+8)},{s16(p+10)},{s16(p+12)}) '
                      f'next+{il>>4} prev-{il&15} nq {u8(p+0xF)} q {u32(p+0x10):08X}')

main()
