#!/usr/bin/env python3
"""Counts rand() calls per game tick from a VALOR_WATCH log (docs/re/objects.md, Randomness).

  obj_rng.py LOG [FROM TO]

The log must watch the seed and the tick counter:
  VALOR_WATCH="seed=800CA2A4:4;tick=8009A5DC:4"
rand (0x8001552C) is the PsyQ LCG seed = seed * 0x41C64E6D + 12345, so the number of calls between
two logged seeds is how many steps take one to the other (searched up to 200000). Prints a
histogram of calls per logged change and the total, for vblanks FROM..TO.
"""
import collections
import sys

A, C, M = 0x41C64E6D, 12345, 1 << 32
LIMIT = 200000


def steps(a, b):
    s = a
    for n in range(1, LIMIT + 1):
        s = (s * A + C) % M
        if s == b:
            return n
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 62
    seeds = []
    for line in open(sys.argv[1], errors="replace"):
        p = line.split()
        if len(p) >= 6 and p[0] == "watch" and p[2] == "seed+0:":
            v = int(p[1])
            if lo <= v <= hi:
                seeds.append((v, int(p[3], 16), int(p[5], 16)))
    hist = collections.Counter()
    total = 0
    unknown = 0
    per_vblank = []
    for v, a, b in seeds:
        n = steps(a, b)
        if n is None:
            unknown += 1
            continue
        hist[n] += 1
        total += n
        per_vblank.append((v, n))
    print(f"{len(seeds)} changes, {total} calls, {unknown} not reached in {LIMIT} steps")
    if per_vblank:
        span = per_vblank[-1][0] - per_vblank[0][0]
        if span > 0:
            print(f"{total / (span / 2):.1f} calls per tick (30 Hz) over vblanks "
                  f"{per_vblank[0][0]}..{per_vblank[-1][0]}")
    for n, c in sorted(hist.items())[:40]:
        print(f"{n:6d} calls: {c} times")


if __name__ == "__main__":
    main()
