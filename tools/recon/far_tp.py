#!/usr/bin/env python3
"""--poke-on-call arguments for teleports in warped runs (docs/re/enemy_types.md, Warped runs).

  far_tp.py DUMP [--god] [--stream REC] [--at CALL:X,Y,Z]... [--fire V]

DUMP: a RAM dump of the level (the player's address is [0x8009D654]). --god: cheats 0x80 on the
first player_update. --stream REC: on the first player_update, make streaming record REC (its
address, from far_stream.py) the current one, so `chunk_stream` loads its four chunks one by
one (about 400 vblanks each) without the player going there. --at CALL:X,Y,Z: on the first call
of CALL (8007A6B0 the first player_update, 80044470 the first projectile think after a shot)
write the position into +0x20C, +0x258, +0x3BC (16.16) and +0x354; a teleport into a chunk not
yet loaded hangs there and lands once it is. --fire V: press cross at vblank V (the shot that
fires the 80044470 pokes), with the shot's voices flagged unheard.
"""
import struct
import sys


def main():
    a = sys.argv[1:]
    d = open(a[0], 'rb').read()
    pl = struct.unpack_from('<I', d, 0x9D654)[0]
    out = []
    i = 1
    while i < len(a):
        if a[i] == '--god':
            out.append('--poke-on-call 8007A6B0:800386A4=128')
        elif a[i] == '--stream':
            i += 1
            out.append(f'--poke-on-call 8007A6B0:80099070={int(a[i], 16)}')
        elif a[i] == '--at':
            i += 1
            call, xyz = a[i].split(':')
            for k, v in enumerate(int(c) for c in xyz.split(',')):
                for o in (0x20C, 0x258, 0x3BC):
                    out.append(f'--poke-on-call {call}:{pl + o + 4 * k:08X}={(v << 16) & 0xFFFFFFFF}')
                out.append(f'--poke-on-call {call}:{pl + 0x354 + 4 * k:08X}={v & 0xFFFFFFFF}')
        elif a[i] == '--fire':
            i += 1
            out.append(f'--press {a[i]}:cross:2')
            for v in range(1, 5):
                out.append(f'--poke-on-call 80044470:{0x8009115C + 0x34 * v:08X}=2')
        i += 1
    print(' '.join(out))


if __name__ == '__main__':
    main()
