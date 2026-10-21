#!/usr/bin/env python3
"""Watch how the character pool's entities are removed in a replay (docs/re/entities.md).

Log made with the watch list `ent_despawn.py --watch` (whole words, no VALOR_WATCH_HALF): per
character slot its flags A (+0xC), out-of-sight counter (+0x290), health (+0xF4) and the
drawn flags (+0x298, +0x29C). A removal is flags A gaining 0x80000; it is classed as a despawn
when the counter last watched was at or below 1 (the watch runs before the draw pass, which
decrements it and removes at 0): 'despawn' after a count down, 'despawn-cap' when it jumped to
1 (the make-room rule); else a script/other removal. Also by the health then.

usage: ent_despawn.py --watch | ent_despawn.py LOG [--until LINE]
"""
import re, sys
from collections import defaultdict, Counter

SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def main():
    if sys.argv[1] == '--watch':
        w = []
        for i, b in enumerate(SLOTS):
            w += [f'f{i}={b:X}+C:4', f'n{i}={b:X}+290:4', f'h{i}={b:X}+F4:4', f'q{i}={b:X}+298:8']
        w += ['cam=[8009D638]+354:C', 'cnt=80097FE8:4', 'queue=8009E288:4']
        print(';'.join(w))
        return
    until = int(sys.argv[sys.argv.index('--until') + 1]) if '--until' in sys.argv else None
    st = defaultdict(int)
    level = '?'
    out = Counter()
    rows = []
    wre = re.compile(r'^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]{8}) -> ([0-9A-F]{8})')
    for n, line in enumerate(open(sys.argv[1], errors='replace'), 1):
        if until and n >= until:
            break
        m = re.match(r'replay: mark mission (\d) (\d)', line)
        if m:
            level = f'{m[1]}:{m[2]}'
        m = wre.match(line)
        if not m:
            continue
        name, off, old, new = m[2], int(m[3], 16), int(m[4], 16), int(m[5], 16)
        if name[0] == 'f' and not old & 0x80000 and new & 0x80000 and old:
            i = name[1]
            cnt, seen = s32(st[('n' + i, 0)]), st[('q' + i, 0)]
            hp = s32(st[('h' + i, 0)]) / 65536
            jumped = st[('j' + i, 0)]
            why = ('despawn-cap' if jumped else 'despawn') if cnt <= 1 and seen else 'other'
            state = 'dead' if old & 0x20000000 or hp <= 0 else 'alive'
            out[(level, why, state)] += 1
            rows.append((m[1], level, i, why, state, cnt, seen, hp))
        if name[0] == 'n':
            st[('j' + name[1], 0)] = int(new == 1 and s32(old) > 2)
        st[(name, off)] = new
    for k, v in sorted(out.items()):
        print(*k, v)
    for r in rows:
        if r[3] != 'other' or r[4] == 'alive':
            print('frame %s %s slot %s %s %s counter %d seen %d health %.1f' % r)


main()
