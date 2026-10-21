#!/usr/bin/env python3
"""Print the player's control config and movement state from a RAM dump (docs/re/movement.md).

usage: move_cfg.py RAM.bin [--port N]
"""
import struct, sys

def main():
    ram = open(sys.argv[1], 'rb').read()
    port = int(sys.argv[sys.argv.index('--port') + 1]) if '--port' in sys.argv else 0
    def w(a): return struct.unpack_from('<i', ram, a & 0x1FFFFF)[0]
    def h(a): return struct.unpack_from('<h', ram, a & 0x1FFFFF)[0]
    def b(a): return ram[a & 0x1FFFFF]
    print('digital roles (0x800B75F0 + port*0xD0 + role*0x34):')
    for r in range(4):
        base = 0x800B75F0 + port * 0xD0 + r * 0x34
        ws = [w(base + 4 * i) for i in range(13)]
        print(f'  role {r}: ' + ' '.join(f'+{4*i:02X}={v:#x}' for i, v in enumerate(ws)))
    print('analog roles (0x800B74B0 + port*0xA0 + role*0x28):')
    for r in range(4):
        base = 0x800B74B0 + port * 0xA0 + r * 0x28
        print(f'  role {r}: axis={b(base)} lo={b(base+0xE):#x} neg={w(base+0x14)} pos={w(base+0x18)} '
              f'speed={w(base+0x20):#x} inv={w(base+0x24)} +4={w(base+4):#x} +8={w(base+8):#x}')
    print(f'look mode 0x8009E3A0[port] = {w(0x8009E3A0 + 4*port)}, 0x8009E398[port] = {w(0x8009E398 + 4*port)}, '
          f'analog 0x80093B78[port] = {b(0x80093B78 + port)}')
    print('curve 0x8009E3AC:', ' '.join(str(b(0x8009E3AC + i)) for i in range(128)))
    s = w(0x8009D654); inv = s + 0x3CC
    if s:
        print(f'soldier {s:#x}: flagsA={w(s+0xC):#x} flagsB={w(s+0x10):#x} accel={w(s+0x1FC):#x} fwd={w(s+0x200):#x} '
              f'max={w(s+0x204):#x} vel=({w(s+0x218):#x},{w(s+0x21C):#x},{w(s+0x220):#x}) speed={w(s+0x224):#x} '
              f'eye={w(s+0x2D4):#x} sphere_local=({h(s+0x2D8)},{h(s+0x2DA)},{h(s+0x2DC)}) r={h(s+0x2E8)} probe={h(s+0x2EC)}')
        print(f'  inv flags={w(inv):#x} +3C={w(inv+0x3C)} +70={w(inv+0x70):#x} B4={h(inv+0xB4)} B6={h(inv+0xB6)} '
              f'178={w(inv+0x178):#x} 184={w(inv+0x184):#x} 188={w(inv+0x188):#x} 190={h(inv+0x190)} 192={h(inv+0x192)} '
              f'1C8={w(inv+0x1C8)}')
    c = w(0x8009D638)
    if c:
        o = w(c + 0x388)
        print(f'camera {c:#x} obj {o:#x}: +94={w(o+0x94)} +98={w(o+0x98)} +4C={w(o+0x4C):#x} +1C={w(o+0x1C):#x}')

main()
