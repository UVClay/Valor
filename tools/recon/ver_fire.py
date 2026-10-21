#!/usr/bin/env python3
"""Measure the soldiers' fire decisions and dodges in a run (docs/re/soldier.md, fire loops and
the combat loop).

Input: a log made with `ver_ai.py --watch --vars` and RAM dumps of the same run's levels (for
the script tables). Usage: ver_fire.py LOG --dump RAM [--dump RAM...] [--to V] [--script 40]

Fire loop (channel 1 in script 40 or 41, 1:3 numbering; the same words in 2:1/2:2):
- SMG: a "no" plays anim 26 (4 frames, 4 ticks) at word 60 (41: 41; the counter rests on the next word, 62/43); a "yes" sets variable 3 to
  1..4 (word 45/46) and plays anim 27 that many times. So: yes = variable 3 rising from 0 while
  the channel is in the SMG branch; no = ticks spent at the anim-26 word / 4.
- Rifle: each tick at word 66 (41: 62, the `wait 0`; counter 67/63) is one "no"; leaving it for the shot (fire
  state 2) is one "yes".
Prints per level and weapon: decisions, yeses, the rate, and the mean fire chance (v26) of the
soldiers measured.

Dodges: channel 0 entering script 135 from 25 or 28, counted against the passes of 25 (ticks
at 25:5, the `wait v0B`, divided by v0B), split by the distance to the player (< 1000, the band
whose dodge roll does not need the player's aim).
"""
import math, re, sys
from collections import defaultdict

sys.argv, _argv = sys.argv[:1] + ['--noop'], sys.argv
exec(open(__file__.replace('ver_fire', 'ver_ai')).read().split('\ndef arg(')[0])
sys.argv = _argv

SMG_NO = {40: 62, 41: 43}       # the counter after the anim-26 op (it rests on the next word)
RIFLE_NO = {40: 67, 41: 63}     # after the `wait 0`


def main():
    dumps = [a for k, a in zip(sys.argv, sys.argv[1:]) if k == '--dump']
    to = int(sys.argv[sys.argv.index('--to') + 1]) if '--to' in sys.argv else 1 << 60
    tables = script_tables(dumps)
    st = {}
    base = {}
    level = '?'
    wre = re.compile(r'^watch (\d+) (\w)(\d)\+([0-9A-F]+): ([0-9A-F]{8}) -> ([0-9A-F]{8})')
    are = re.compile(r'^watch (\d+) l(\d) at ([0-9A-F]{8})')
    # per slot: (since vblank, location) of channel 1 and channel 0
    loc1, loc0 = {}, {}
    fire = defaultdict(lambda: [0, 0.0, 0, []])   # (level, weapon) -> yes, no, rifle-no ticks, v26s
    passes = defaultdict(float)
    dodges = defaultdict(int)

    def where(i, ch):
        b = base.get(i, 0)
        pc = st.get(('c' if ch == 0 else 'd', i, 4), 0)
        r = locate(tables, b, pc)
        if r.startswith('?'):
            return None
        s, w = r.split(':')
        return int(s), int(w)

    def var(i, n):
        return s32(st.get(('v', i, 4 * n), 0))

    def dist(i):
        a = [s32(st.get(('x', i, o), 0)) / 65536 for o in (0, 4, 8)]
        b = [s32(st.get(('x', 9, o), 0)) / 65536 for o in (0, 4, 8)]
        return math.dist(a, b)

    def close1(i, t):
        if i not in loc1:
            return
        t0, l = loc1[i]
        if l and l[0] in SMG_NO and var(i, 0xF) == 0 and l[1] == SMG_NO[l[0]]:
            fire[(level, 'smg')][1] += (t - t0) / 2 / 4
        if l and l[0] in RIFLE_NO and var(i, 0xF) == 1 and l[1] == RIFLE_NO[l[0]]:
            fire[(level, 'rifle')][1] += (t - t0) / 2

    def close0(i, t):
        if i not in loc0:
            return
        t0, l, d = loc0[i]
        if l == (25, 6):       # resting after the `wait v0B` at word 5
            vb = max(var(i, 0xB), 1)
            passes[(level, d < 1000)] += (t - t0) / 2 / vb

    for line in open(sys.argv[1], errors='replace'):
        m = re.match(r'replay: mark mission (\d) (\d)', line)
        if m:
            level = f'{m[1]}:{m[2]}'
            loc1.clear(); loc0.clear()
        m = are.match(line)
        if m:
            base[int(m[2])] = int(m[3], 16)
            continue
        m = wre.match(line)
        if not m:
            continue
        t, k, i, off, old, new = int(m[1]), m[2], int(m[3]), int(m[4], 16), int(m[5], 16), int(m[6], 16)
        if t > to:
            break
        if k == 'd' and off == 4:
            close1(i, t)
            st[(k, i, off)] = new
            l = where(i, 1)
            loc1[i] = (t, l)
            if l and l[0] in RIFLE_NO and var(i, 0xF) == 1 and l[1] > RIFLE_NO[l[0]] and l[1] < RIFLE_NO[l[0]] + 28:
                fire[(level, 'rifle')][0] += 1
            continue
        if k in 'cd' and off == 0:
            if k == 'd':
                close1(i, t)
                loc1[i] = (t, None)
            else:
                close0(i, t)
                loc0[i] = (t, None, 0)
            st[(k, i, off)] = new
            continue
        if k == 'c' and off == 4:
            close0(i, t)
            prev = loc0.get(i, (0, None, 0))[1]
            st[(k, i, off)] = new
            l = where(i, 0)
            loc0[i] = (t, l, dist(i))
            if l and l[0] == 135 and prev and prev[0] in (25, 28):
                dodges[(level, dist(i) < 1000, prev[0])] += 1
            continue
        if k == 'v' and off == 0xC and s32(old) <= 0 < s32(new) <= 4:
            l = loc1.get(i, (0, None))[1]
            if l and l[0] in SMG_NO and var(i, 0xF) == 0:
                fire[(level, 'smg')][0] += 1
                fire[(level, 'smg')][3].append(var(i, 0x26))
        st[(k, i, off)] = new
    for (lv, w), (y, n, _, v26) in sorted(fire.items()):
        tot = y + n
        print(f'{lv} {w}: {tot:.0f} decisions, {y} yes ({100 * y / max(tot, 1):.1f}%), '
              f'v26 of the firers {sorted(set(v26))}')
    for (lv, near), p in sorted(passes.items()):
        d = sum(c for (l2, n2, s), c in dodges.items() if l2 == lv and n2 == near)
        print(f'{lv} {"< 1000" if near else ">= 1000"}: {p:.0f} passes of 25, {d} dodges from 25/28')


main()
