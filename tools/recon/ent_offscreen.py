#!/usr/bin/env python3
"""Count enemy fights that happen while the enemy is not drawn (frozen hit spheres).

Reads a replay log made with VALOR_TRACE_DAMAGE=1, VALOR_WATCH_HALF=1 and the watch list
`ent_offscreen.py --watch` prints (the five character pool slots at 0x800B7B08 + i*0x2814,
the camera and the player's view). Every event (a hit on a soldier or dog, a shot by one) is
classed by the slot's draw state at that moment:

  drawn    +0x29C set: posed and drawn last frame (spheres one tick behind)
  frozen   +0x298 set, +0x29C clear: seen before, now off screen (spheres frozen)
  never    +0x298 clear: never drawn (every joint 0: spheres at the feet)

and by an estimate of whether the original 4:3 view (65 x 51 degrees, docs/re/animation.md)
would have shown its bounding sphere: the replay runs with the recording's own wider view.

usage: ent_offscreen.py --watch            print the VALOR_WATCH string
       ent_offscreen.py LOG [--until N]    analyse (N: stop at this log line, e.g. a desync)
"""
import math, re, sys
from collections import Counter, defaultdict

SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]


def watch_string():
    w = []
    for i, b in enumerate(SLOTS):
        w += [f't{i}={b:X}+8:4', f'v{i}={b:X}+298:8', f'p{i}={b:X}+354:C', f'a{i}=[{b:X}+100]+5C:4',
              f'c{i}={b:X}+11C:4', f'd{i}={b:X}+190:4', f'w{i}=[{b:X}+388]+10:8']
    w += ['cam=[8009D638]+354:C', 'view=[8009D654]+544:10', 'fp=[[8009D654]+100]+5C:4',
          'inv=[8009D654]+3CC:8', 'inw=[8009D654]+41C:58']
    return ';'.join(w)


def orig_visible(pos, cam, yaw, pitch):
    """Would the sphere (pelvis, 155 above the feet, radius 150) touch the 4:3 view?"""
    if cam is None or yaw is None:
        return None
    dx, dy, dz = pos[0] - cam[0], pos[1] - 155 - cam[1], pos[2] - cam[2]
    a = yaw * 2 * math.pi / 4096
    fwd = dx * math.sin(a) + dz * math.cos(a)
    side = dx * math.cos(a) - dz * math.sin(a)
    p = -pitch * 2 * math.pi / 4096          # looking down: pitch < 0, y grows downward
    z = fwd * math.cos(p) + dy * math.sin(p)
    up = dy * math.cos(p) - fwd * math.sin(p)
    r = 150 * 1.42                             # the test uses the cube around the sphere
    if z + r <= 0:
        return False
    zz = max(z, 1.0)
    return (side - r) / zz < 284 / 400 and (side + r) / zz > -256 / 400 and \
        abs(up) - r < 0.48 * zz


def main():
    if sys.argv[1] == '--watch':
        print(watch_string())
        return
    until = None
    if '--until' in sys.argv:
        until = int(sys.argv[sys.argv.index('--until') + 1])
    st = defaultdict(lambda: defaultdict(int))     # watch name -> offset -> value
    level = '?'
    ev = Counter()
    rows = []
    wre = re.compile(r'^watch (\d+) (\w+)\+([0-9A-F]+): (-?\d+) -> (-?\d+)')
    for n, line in enumerate(open(sys.argv[1], errors='replace'), 1):
        if until and n >= until:
            break
        m = re.match(r'replay: mark mission (\d) (\d)', line)
        if m:
            level = f'{m[1]}:{m[2]}'
            continue
        m = wre.match(line)
        if m:
            name, off, old, new = m[2], int(m[3], 16), int(m[4]), int(m[5])
            prev = st[name][off]
            st[name][off] = new
            if name[0] == 'w' and name[1:].isdigit() and off == 2:
                left_old, left_new = (old >> 8) & 0xFF, (new >> 8) & 0xFF
                if 0 < left_old - left_new <= 2:
                    event(rows, ev, level, 'shot', int(name[1]), st, int(m[1]), left_old - left_new)
            if name[0] == 'a' and name[1:].isdigit() and off == 0 and new == 21 and old == 20:
                event(rows, ev, level, 'shot', int(name[1]), st, int(m[1]), 1)
            continue
        m = re.match(r'^damage (\d+): victim ([0-9A-F]+) type (\w+).*attacker \w+ type (\w+)', line)
        if m:
            v = int(m[2], 16)
            if v in SLOTS:
                event(rows, ev, level, 'hit by ' + m[4], SLOTS.index(v), st, int(m[1]), 1)
    tot = Counter()
    for (lv, kind, state, orig), k in sorted(ev.items()):
        print(f'{lv} {kind:12} {state:7} original view {orig!s:5}: {k}')
        tot[(kind.split()[0], state)] += k
    print()
    for k, v in sorted(tot.items()):
        print(*k, v)


def event(rows, ev, level, kind, i, st, frame, count):
    t = st[f't{i}'][0]
    if t not in (0x1402, 0x1405):
        return
    seen, drawn = st[f'v{i}'][0], st[f'v{i}'][4]
    state = 'drawn' if drawn else 'frozen' if seen else 'never'
    pos = [st[f'p{i}'][o] for o in (0, 4, 8)]
    cam = [st['cam'][o] for o in (0, 4, 8)] if st['cam'] else None
    vis = orig_visible(pos, cam, st['view'][0xE], st['view'][2])
    ev[(level, kind, state, vis)] += count


main()
