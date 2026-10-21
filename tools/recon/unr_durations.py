#!/usr/bin/env python3
"""How long a slot's channel stays in each script, from ver_ai.py's output on stdin
(docs/re/enemy_types.md, Level differences: the watched timings).

  ver_ai.py LOG --dump RAM --slot N | unr_durations.py [c0|c1]

Per script: the number of stays, min / max / mean length in ticks (vblanks / 2), and the
scripts that followed it.
"""
import collections, re, sys

ch = sys.argv[1] if len(sys.argv) > 1 else 'c1'
rows = []
for line in sys.stdin:
    m = re.match(r'\s*(\d+) slot \d+ \w+ (c[01]) ->\s+(-?\d+)', line)
    if m and m[2] == ch:
        rows.append((int(m[1]), int(m[3])))
d, nxt = collections.defaultdict(list), collections.defaultdict(collections.Counter)
for (v, s), (v2, s2) in zip(rows, rows[1:]):
    d[s].append((v2 - v) / 2)
    nxt[s][s2] += 1
for s, l in sorted(d.items()):
    print(f'{s:4} x{len(l):3}  {min(l):6.1f} .. {max(l):6.1f}  mean {sum(l) / len(l):6.1f}  then {dict(nxt[s])}')
