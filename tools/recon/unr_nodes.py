#!/usr/bin/env python3
"""A level's AI nodes from a RAM dump (docs/re/enemies.md, Nodes; soldier.md, Cover).

  unr_nodes.py RAMDUMP [--kind HEX]

The table is [g_world (0x8009D648) + 0x388]: +0x8 the entries (0x34 bytes), +0xC the count (as
`compute`, 0x80040F48, reads them): +0xC kind, +0x10 approach side, +0x14 position (s16 x, y, z),
+0x24/+0x28 facing x/z as s16 (4096 = 1; 2:3's gun node (2046, 3548) for a gun at yaw 341), +0x30 the holder's handle.
"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
u32 = lambda a: struct.unpack_from('<I', d, a & 0x1FFFFF)[0]
s32 = lambda a: struct.unpack_from('<i', d, a & 0x1FFFFF)[0]
s16 = lambda a: struct.unpack_from('<h', d, a & 0x1FFFFF)[0]
want = int(sys.argv[sys.argv.index('--kind') + 1], 16) if '--kind' in sys.argv else None
w = u32(u32(0x8009D648) + 0x388)
n, tab = u32(w + 0xC), u32(w + 0x8)
print(f'{n} nodes at {tab:08X}')
for i in range(n):
    e = tab + 0x34 * i
    kind = u32(e + 0xC)
    if want is not None and not kind & want:
        continue
    x, y, z = (struct.unpack_from('<h', d, (e + 0x14 + 2 * k) & 0x1FFFFF)[0] for k in range(3))
    print(f'{i:3} kind {kind:#x} side {u32(e + 0x10)} at ({x}, {y}, {z}) facing ({s16(e + 0x24)}, {s16(e + 0x28)}) holder {u32(e + 0x30):08X}')
