#!/usr/bin/env python3
"""The disguise levels' papers chain in a watched run (docs/re/soldier.md, Papers and suspicion).

  far_papers.py LOG DUMP [--from V] [--to V]

LOG is a run with `ver_ai.py --watch --vars` plus `ps=[8009D654]+11C:4` (the player's body
script in the high half) and `pw=[8009D654]+420:4` (slot, weapon); DUMP a RAM dump of the same
level (records, kind-3 positions and their level scripts). For each character slot it prints a
line when the slot gets a soldier (the nearest soldier record to where it appears, its role v11,
skill v8, voice v25, suspicion vA, path v22), then every change of channel 0's script with the
classifier result v4, vA, the distance to the player, and the player's weapon and body script at
that moment; and each change of the player's weapon or body script while a slot is in 227/228.
"""
import math
import re
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from lvl_records import Ram, records  # noqa: E402

SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def main():
    log, dump = sys.argv[1], sys.argv[2]
    lo = int(sys.argv[sys.argv.index('--from') + 1]) if '--from' in sys.argv else 0
    hi = int(sys.argv[sys.argv.index('--to') + 1]) if '--to' in sys.argv else 1 << 60
    ram = Ram(dump)
    recs = []
    for i, _, _, rec in records(ram):
        if ram.u32(rec + 0xC) == 3:
            recs.append((i, [ram.s16(rec + 0x10 + 2 * k) for k in range(3)], ram.s32(rec + 0x78)))
    wre = re.compile(r'^watch (\d+) (\w+?)(\d)?\+([0-9A-F]+): ([0-9A-F]{8}) -> ([0-9A-F]{8})')
    are = re.compile(r'^watch (\d+) l(\d) at ([0-9A-F]{8})')
    st, base, cur = {}, {}, {}
    pending = {}  # slot -> vblank its soldier appeared (position comes a tick later)

    def var(i, n):
        return s32(st.get(('v', i, 4 * n), 0))

    def pos(i):
        return [s32(st.get(('x', i, o), 0)) / 65536 for o in (0, 4, 8)]

    def player():
        ps, pw = st.get(('ps', None, 0), 0), st.get(('pw', None, 0), 0)
        return f'weapon {(pw >> 8) & 0xFF} body {ps >> 16}'

    for line in open(log, errors='replace'):
        m = are.match(line)
        if m:
            base[int(m[2])] = int(m[3], 16)
            continue
        m = wre.match(line)
        if not m:
            continue
        v, name, i, off, old, new = int(m[1]), m[2], m[3], int(m[4], 16), int(m[5], 16), int(m[6], 16)
        i = int(i) if i is not None else None
        st[(name, i, off)] = new
        if v > hi:
            break
        # a soldier appears: the type is set, or flags A lose 0x20000000 (dead) / 0x80000
        # (despawned) while the slot is reused for another record
        if name == 'h' and (off == 0 and new or off == 4 and old & 0x20080000 and not new & 0x20080000):
            pending[i] = v
        for k, t in list(pending.items()):
            if v > t + 2 and lo <= t:
                p = pos(k)
                n, c, own = min(recs, key=lambda r: math.dist(r[1][::2], p[::2]))
                print(f'{t:7d} slot {k} soldier: record {n} (own script {own}, {math.dist(c[::2], p[::2]):.0f} '
                      f'from it) role {var(k, 0x11)} skill {var(k, 8)} voice {var(k, 0x25)} '
                      f'vA {var(k, 0xA)} path {var(k, 0x22)}')
                del pending[k]
        if v < lo:
            continue
        if name == 'c' and off == 0 and i in base and new:
            n = (new - base[i]) // 16
            if 0 <= n < 1000 and n != cur.get(i):
                cur[i] = n
                d = math.dist(pos(i), pos(9))
                print(f'{v:7d} slot {i} -> {n:3d}  v4 {var(i, 4)} vA {var(i, 0xA):3d}  dist {d:5.0f}  '
                      f'player {player()}')
        if name == 'pw' and v >= lo and (old >> 8) & 0xFF != (new >> 8) & 0xFF:
            print(f'{v:7d}   player weapon {(old >> 8) & 0xFF} -> {(new >> 8) & 0xFF}')
        elif name == 'ps' and any(c in (227, 228) for c in cur.values()):
            print(f'{v:7d}   player {player()}')


if __name__ == '__main__':
    main()
