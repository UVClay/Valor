#!/usr/bin/env python3
"""The character slots and the player in a RAM dump (docs/re/enemy_types.md): per slot the type,
flags A, health, position, both channels' script:word (from the model's script list), and the
player's position, flags B and distance to each.

  unr_slots.py RAMDUMP
"""
import math, struct, sys

d = open(sys.argv[1], 'rb').read()
u32 = lambda a: struct.unpack_from('<I', d, a & 0x1FFFFF)[0]
s32 = lambda a: struct.unpack_from('<i', d, a & 0x1FFFFF)[0]
pl = u32(0x8009D654)
pp = [s32(pl + 0x20C + 4 * k) / 65536 for k in range(3)]
print(f'player {pl:08X} at ({pp[0]:.0f}, {pp[1]:.0f}, {pp[2]:.0f}) flags B {u32(pl + 0x10):08X} hp {s32(pl + 0xF4) / 65536:.2f}')
for i in range(5):
    b = 0x800B7B08 + i * 0x2814
    t = u32(b + 8)
    if not t:
        continue
    lst = u32(u32(u32(b + 0x100) + 8) + 0xB8)
    def where(ch):
        rec, pc = u32(b + ch + 0x10), u32(b + ch + 0x14)
        if not rec or not lst:
            return '-'
        n = (rec - lst) // 16
        code = u32(rec + 0xC)
        return f'{n}:{(pc - code) // 4}'
    p = [s32(b + 0x20C + 4 * k) / 65536 for k in range(3)]
    print(f'slot {i} type {t:X} flags A {u32(b + 0xC):08X} hp {s32(b + 0xF4) / 65536:.2f} at ({p[0]:.0f}, {p[1]:.0f}, {p[2]:.0f}) '
          f'd {math.dist(p, pp):.0f} c0 {where(0x104)} c1 {where(0x178)}')
