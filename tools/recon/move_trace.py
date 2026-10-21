#!/usr/bin/env python3
"""Turn a VALOR_WATCH log into a per-tick table of the player's movement (docs/re/movement.md).

usage: move_trace.py LOG [FROM [TO]] [--fields name+off,...]

Each watched word is shown as 16.16 (value / 65536), or as an integer with a ':i' suffix. Default fields: position, its change
since the last row (dx, dy, dz, horizontal speed), the local velocity, on-ground, eye offset.
The log comes from a run with VALOR_WATCH naming pos/vel/fwd/gnd/eye/cam/look/... (see the doc).
"""
import math, re, sys

LINE = re.compile(r'^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)')

def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    fields = None
    if '--fields' in sys.argv:
        fields = sys.argv[sys.argv.index('--fields') + 1].split(',')
        args.remove(sys.argv[sys.argv.index('--fields') + 1]) if sys.argv[sys.argv.index('--fields') + 1] in args else None
    log, lo, hi = args[0], int(args[1]) if len(args) > 1 else 0, int(args[2]) if len(args) > 2 else 1 << 30
    state, frames = {}, {}
    for line in open(log, errors='replace'):
        m = LINE.match(line)
        if not m:
            continue
        f, name, off, old, new = int(m[1]), m[2], int(m[3], 16), int(m[4], 16), int(m[5], 16)
        key = f'{name}+{off:X}'
        state.setdefault(key, s32(old))
        frames.setdefault(f, {})[key] = s32(new)
    cur = dict(state)
    fields = fields or ['vel+0', 'vel+8', 'vel+4', 'gnd+0:i', 'eye+0', 'cam+0:i']
    print(f"{'frame':>6} {'dx':>8} {'dy':>8} {'dz':>8} {'hspeed':>8} " + ' '.join(f'{k:>10}' for k in fields))
    last = None
    for f in sorted(frames):
        cur.update(frames[f])
        if not (lo <= f <= hi):
            last = (cur.get('pos+0', 0), cur.get('pos+4', 0), cur.get('pos+8', 0))
            continue
        p = (cur.get('pos+0', 0), cur.get('pos+4', 0), cur.get('pos+8', 0))
        d = [(a - b) / 65536 for a, b in zip(p, last)] if last else [0, 0, 0]
        last = p
        cells = []
        for k in fields:
            v = cur.get(k.split(':')[0], 0)
            cells.append(f'{v:10d}' if k.endswith(':i') else f'{v / 65536:10.3f}')
        print(f'{f:>6} {d[0]:8.2f} {d[1]:8.2f} {d[2]:8.2f} {math.hypot(d[0], d[2]):8.2f} ' + ' '.join(cells))

main()
