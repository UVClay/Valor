#!/usr/bin/env python3
"""Analyses VALOR_TRACE_SHOTS output (runtime/src/watch.cpp): how far each bullet passes from the aim point, and where the aim point is relative to the view."""
import itertools
import math
import re
import statistics as st
import sys

PAT = re.compile(r"shot (\d+): yaw (-?[\d.]+) pitch (-?[\d.]+), view yaw (-?[\d.]+) pitch (-?[\d.]+), "
                 r"off by yaw (-?[\d.]+) pitch (-?[\d.]+)( scoped)?; bullet (\S+) (\S+) (\S+), "
                 r"aim (\S+) (\S+) (\S+), camera (\S+) (\S+) (\S+)")

shots = []
for line in open(sys.argv[1]):
    m = PAT.search(line)
    if m:
        g = m.groups()
        shots.append(dict(vb=int(g[0]), yaw=float(g[1]), pitch=float(g[2]), scoped=bool(g[7]),
                          b=tuple(map(float, g[8:11])), aim=tuple(map(float, g[11:14])),
                          cam=tuple(map(float, g[14:17]))))


def direction(s, sx, sy, sz):
    y, p = math.radians(s["yaw"]), math.radians(s["pitch"])
    return (sx * math.sin(y) * math.cos(p), sy * math.sin(p), sz * math.cos(y) * math.cos(p))


def miss(s, d):
    """Perpendicular distance from the aim point to the bullet's line, and how far along it is."""
    v = [s["aim"][i] - s["b"][i] for i in range(3)]
    along = sum(v[i] * d[i] for i in range(3))
    perp = math.sqrt(max(0.0, sum(x * x for x in v) - along * along))
    return perp, along


scoped = [s for s in shots if s["scoped"]]
best = min(itertools.product((1, -1), repeat=3),
           key=lambda c: sum(miss(s, direction(s, *c))[0] for s in (scoped or shots)))
print(f"{len(shots)} shots, {len(scoped)} scoped; direction signs {best}")
for name, group in (("scoped", scoped), ("unscoped", [s for s in shots if not s["scoped"]])):
    if not group:
        continue
    res = [miss(s, direction(s, *best)) for s in group]
    perp = [r[0] for r in res]
    dist = [math.dist(s["cam"], s["aim"]) for s in group]
    ang = [math.degrees(math.atan2(p, max(d, 1))) for p, d in zip(perp, dist)]
    q = lambda xs, f: sorted(xs)[int(f * (len(xs) - 1))]
    print(f"{name}: {len(group)} shots; miss distance median {st.median(perp):.1f} p75 {q(perp, .75):.1f} "
          f"p90 {q(perp, .9):.1f} units; as seen from the camera median {st.median(ang):.2f} p90 "
          f"{q(ang, .9):.2f} deg; aim point distance median {st.median(dist):.0f}")
    if name == "unscoped":
        worst = sorted(zip(perp, ang, dist, group), key=lambda x: -x[1])[:12]
        for p, a, d, s in worst:
            print(f"  vblank {s['vb']}: misses by {p:.0f} units ({a:.1f} deg) at {d:.0f}")

# Where the aim point is relative to the view's centre (the runtime's crosshair), as seen from the camera.
print("aim point relative to the view direction (unscoped):")
dy, dp = [], []
PAT2 = re.compile(r"view yaw (-?[\d.]+) pitch (-?[\d.]+)")
views = [tuple(map(float, PAT2.search(l).groups())) for l in open(sys.argv[1]) if PAT.search(l)]
for s, (vyaw, vpitch) in zip(shots, views):
    if s["scoped"]:
        continue
    v = [s["aim"][i] - s["cam"][i] for i in range(3)]
    yaw = math.degrees(math.atan2(v[0], v[2]))
    pitch = math.degrees(math.atan2(-v[1], math.hypot(v[0], v[2])))
    dy.append((yaw - vyaw + 180) % 360 - 180)
    dp.append(pitch - vpitch)
q = lambda xs, f: sorted(xs)[int(f * (len(xs) - 1))]
print(f"  yaw: median {st.median(dy):.2f}, p10 {q(dy, .1):.2f}, p90 {q(dy, .9):.2f} deg")
print(f"  pitch: median {st.median(dp):.2f}, p10 {q(dp, .1):.2f}, p90 {q(dp, .9):.2f} deg")
