#!/usr/bin/env python3
"""List entities in a RAM dump with their model/animation instance (+0x100) fields.

usage: anim_ents.py RAMDUMP [--type T] [--hex N]
"""
import struct, sys, argparse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ram')
    ap.add_argument('--type', type=lambda s: int(s, 0))
    ap.add_argument('--hex', type=lambda s: int(s, 0), default=0, help='dump N bytes of the instance')
    a = ap.parse_args()
    d = open(a.ram, 'rb').read()
    u32 = lambda p: struct.unpack_from('<I', d, p & 0x1FFFFF)[0]
    s16 = lambda p: struct.unpack_from('<h', d, p & 0x1FFFFF)[0]
    e = u32(0x8009D64C)
    n = 0
    while e and n < 2000:
        t = u32(e + 8)
        inst = u32(e + 0x100)
        if (a.type is None or t == a.type):
            line = f'{e:08X} type {t:#06x}'
            if inst & 0x80000000:
                line += (f' inst {inst:08X} model {u32(inst+8):08X} anim {s16(inst+0x5C)} frame {s16(inst+0x5E)}'
                         f' +60 {s16(inst+0x60)} blend {s16(inst+0x3E)} +74 {u32(inst+0x74):08X} +78 {u32(inst+0x78):08X}')
            print(line)
            if a.hex and inst & 0x80000000:
                b = d[inst & 0x1FFFFF:(inst & 0x1FFFFF) + a.hex]
                for i in range(0, len(b), 16):
                    print(f'   +{i:03X}: ' + ' '.join(f'{x:08X}' for x in struct.unpack_from('<4I', b, i)))
        e = u32(e + 4); n += 1

main()
