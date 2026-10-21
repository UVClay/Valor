#!/usr/bin/env python3
"""Find double frame advances (both script channels stepping the shared frame in one tick).

Reads a replay log made with VALOR_WATCH_HALF=1 and the watch list `ent_double.py --watch`
prints: g_tick, and per character pool slot the instance's animation and frame and both
channels' frame target (context +0x18) and wait (+0x1C). For every displayed frame it compares
the frame's advance with the ticks run; an advance larger than the ticks with no animation
change is a double advance.

usage: ent_double.py --watch | ent_double.py LOG [--until LINE]
"""
import re, sys
from collections import defaultdict, Counter

SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]


def main():
    if sys.argv[1] == '--watch':
        w = ['tick=8009A5DC:4']
        for i, b in enumerate(SLOTS):
            w += [f'a{i}=[{b:X}+100]+5C:4', f'c{i}={b:X}+11C:8', f'd{i}={b:X}+190:8',
                  f's{i}={b:X}+114:4', f'r{i}={b:X}+188:4']
        w += ['fp=[[8009D654]+100]+5C:4', 'fc=[8009D654]+11C:8', 'fd=[8009D654]+190:8']
        print(';'.join(w))
        return
    until = int(sys.argv[sys.argv.index('--until') + 1]) if '--until' in sys.argv else None
    st = defaultdict(int)
    prev = None
    frame = None
    found = Counter()
    both = Counter()
    ex = []
    wre = re.compile(r'^watch (\d+) (\w+)\+([0-9A-F]+): (-?\d+) -> (-?\d+)')

    def snapshot():
        return dict(st)

    def compare(a, b):
        ticks = (b.get(('tick', 0), 0) - a.get(('tick', 0), 0)) & 0xFFFF
        if ticks <= 0 or ticks > 4:
            return
        for k in [str(i) for i in range(5)] + ['f']:
            an = ('a' + k, 0) if k != 'f' else ('fp', 0)
            fr = ('a' + k, 2) if k != 'f' else ('fp', 2)
            c0 = ('c' + k, 0) if k != 'f' else ('fc', 0)
            c1 = ('d' + k, 0) if k != 'f' else ('fd', 0)
            if a.get(an, 0) != b.get(an, 0) or a.get(an, 0) < 0:
                continue
            f0, f1 = a.get(fr, 0), b.get(fr, 0)
            t0, t1 = a.get(c0, 0), a.get(c1, 0)
            if t0 > f0 and t1 > f0:
                both[k] += 1
            if f1 - f0 > ticks:
                found[k] += 1
                if len(ex) < 12:
                    ex.append((frame, k, a.get(an), f0, f1, ticks, t0, t1, b.get(c0, 0), b.get(c1, 0)))

    for n, line in enumerate(open(sys.argv[1], errors='replace'), 1):
        if until and n >= until:
            break
        m = wre.match(line)
        if not m:
            continue
        f = int(m[1])
        if f != frame:
            if frame is not None:
                cur = snapshot()
                if prev is not None:
                    compare(prev, cur)
                prev = cur
            frame = f
        st[(m[2], int(m[3], 16))] = int(m[5])
    print('advances larger than the ticks run, per slot (f: the player):', dict(found))
    print('frames where both channels had a target ahead of the frame:', dict(both))
    for e in ex:
        print('frame %s slot %s anim %s: frame %d -> %d in %d ticks; targets %d/%d -> %d/%d' % e)


main()
