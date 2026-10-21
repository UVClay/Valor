#!/usr/bin/env python3
"""Spread of the player's shots (docs/re/player.md, Weapons): each VALOR_TRACE_SHOTS bullet's
angles against the direction from its start to the aim point (where an unspread shot goes).

usage: mv_spread.py LOG [FROM [TO]]     (vblank range)

Prints per shot the deviation in angle units (4096 per turn) and, at the end, the extremes
and spread of yaw and pitch deviations. The sign convention (which way yaw and pitch turn) is
the one that makes the deviations smallest over the log.
"""
import itertools
import math
import re
import statistics as st
import sys

PAT = re.compile(r"shot (\d+): yaw (-?[\d.]+) pitch (-?[\d.]+), .*?; bullet (\S+) (\S+) ([^\s,]+), "
                 r"aim (\S+) (\S+) ([^\s,]+)")


def wrap(a):
    return (a + 180) % 360 - 180


def main():
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 40
    shots = []
    for line in open(sys.argv[1], errors="replace"):
        m = PAT.search(line)
        if m and lo <= int(m[1]) <= hi:
            g = [float(x) for x in m.groups()]
            shots.append((int(g[0]), g[1], g[2], g[3:6], g[6:9]))
    if not shots:
        print("no shots")
        return

    def target(s, conv):
        sx, sz, sp, swap = conv
        d = [s[4][i] - s[3][i] for i in range(3)]
        h = math.hypot(d[0], d[2])
        yaw = math.degrees(math.atan2(sx * d[0], sz * d[2])) if not swap else math.degrees(math.atan2(sz * d[2], sx * d[0]))
        pitch = math.degrees(math.atan2(sp * d[1], h))
        return yaw, pitch

    convs = list(itertools.product((1, -1), (1, -1), (1, -1), (False, True)))
    best = min(convs, key=lambda c: sum(abs(wrap(s[1] - target(s, c)[0])) + abs(s[2] - target(s, c)[1]) for s in shots))
    unit = 4096 / 360
    dy, dp = [], []
    for s in shots:
        ty, tp = target(s, best)
        y, p = wrap(s[1] - ty) * unit, (s[2] - tp) * unit
        dist = math.dist(s[3], s[4])
        dy.append(y)
        dp.append(p)
        print(f"{s[0]:>7}  yaw {y:+7.1f}  pitch {p:+7.1f}  (aim {dist:6.0f} away)")
    print(f"{len(shots)} shots, convention {best}")
    for name, v in (("yaw", dy), ("pitch", dp)):
        sd = st.pstdev(v) if len(v) > 1 else 0
        print(f"{name}: min {min(v):+.1f} max {max(v):+.1f} mean {st.mean(v):+.1f} sd {sd:.1f} (units)")


main()
