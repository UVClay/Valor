#!/usr/bin/env python3
"""The collision surfaces in a vertical line through a point of a level (docs/re/movement.md,
Ceilings): every TSP collision triangle above or below (x, z), its height and normal.

  mv_column.py TSPDIR X Z                 the surfaces through (X, Z), top first
  mv_column.py TSPDIR X Z --near R FEET   points within R of (X, Z), on a 25-unit grid, whose
                                          lowest surface above FEET - 300 is a ceiling (normal
                                          pointing down) between 300 and 650 above the feet:
                                          places where a standing jump reaches the ceiling

y points down: a ceiling's normal has y > 0 (faces down), a floor's y < 0.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tspcol import parse  # noqa: E402


def load(d):
    tris = []
    for f in sorted(Path(d).glob("*.TSP")):
        c = parse(f.read_bytes())
        for v0, v1, v2, n, _d in c["faces"]:
            tris.append((c["verts"][v0], c["verts"][v1], c["verts"][v2], c["norms"][n], f.name))
    return tris


def column(tris, x, z):
    out = []
    for a, b, c, n, name in tris:
        # barycentric in x/z
        det = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2])
        if det == 0:
            continue
        u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / det
        v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / det
        if u < 0 or v < 0 or u + v > 1:
            continue
        y = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])
        out.append((y, n, name))
    return sorted(out)


def main():
    tris = load(sys.argv[1])
    x, z = int(sys.argv[2]), int(sys.argv[3])
    if "--near" in sys.argv:
        i = sys.argv.index("--near")
        r, feet = int(sys.argv[i + 1]), int(sys.argv[i + 2])
        for dx in range(-r, r + 1, 25):
            for dz in range(-r, r + 1, 25):
                col = column(tris, x + dx, z + dz)
                floors = [s for s in col if s[1][1] < 0 and abs(s[0] - feet) <= 32]
                ceils = [s for s in col if s[1][1] > 0 and feet - 650 <= s[0] <= feet - 300]
                if floors and ceils:
                    c = max(ceils)
                    print(f"{x + dx} {z + dz}: floor {floors[0][0]:.0f}, ceiling {c[0]:.0f} "
                          f"({feet - c[0]:.0f} above the feet), normal {c[1]}")
        return
    for y, n, name in column(tris, x, z):
        kind = "ceiling" if n[1] > 0 else "floor" if n[1] < 0 else "wall"
        print(f"y {y:8.1f}  {kind:7}  normal {n}  {name}")


main()
