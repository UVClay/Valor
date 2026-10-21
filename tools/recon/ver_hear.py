#!/usr/bin/env python3
"""What made each soldier or dog detect the player, in a run (docs/re/soldier.md, Hearing).

Watch list (`ver_hear.py --watch`): ver_ai.py's (the character slots with their variables) plus
the player's handle and the effect voices 1..23 of g_voices (0x8009115C, 0x34 bytes each: flags,
source handle, sound id, bank, age, source position). Run with VALOR_WATCH set to it (whole
words).

ver_hear.py LOG [--to V]: for each detection (variable 4 going 0 -> 1 while channel 0 is in a
waiting script), the distance to the player and every sound a soldier could hear then (enabled
in the table, one-shot, positional, not flagged unheard, not its own, younger than 60 frames)
with its distance from the soldier, its source (player, slot N, other handle) and id.
"""
import math, re, subprocess, sys

VOICES = 0x8009115C


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    return v - (1 << 16) if v & 0x8000 else v


def watch_string():
    ai = subprocess.run([sys.executable, __file__.replace('ver_hear', 'ver_ai'), '--watch', '--vars'],
                        capture_output=True, text=True).stdout.strip()
    w = [ai, 'n9=[8009D654]+0:4', 'e0=800910D0:18']
    for v in range(1, 24):
        a = VOICES + v * 0x34
        w += [f'o{v}={a:X}:10', f'a{v}={a + 0x28:X}:C']
    return ';'.join(w)


def main():
    if sys.argv[1] == '--watch':
        print(watch_string())
        return
    to = int(sys.argv[sys.argv.index('--to') + 1]) if '--to' in sys.argv else 1 << 60
    st = {}
    wre = re.compile(r'^watch (\d+) ([a-z])(\d+)\+([0-9A-F]+): ([0-9A-F]{8}) -> ([0-9A-F]{8})')
    born = {}       # voice -> vblank of its last start (age dropping)
    level = '?'
    for line in open(sys.argv[1], errors='replace'):
        m = re.match(r'replay: mark mission (\d) (\d)', line)
        if m:
            level = f'{m[1]}:{m[2]}'
        m = wre.match(line)
        if not m:
            continue
        t, k, i, off, old, new = int(m[1]), m[2], int(m[3]), int(m[4], 16), int(m[5], 16), int(m[6], 16)
        if t > to:
            break
        st[(k, i, off)] = new
        if k == 'a' and off == 0 and (new & 0xFFFF) < (old & 0xFFFF):
            born[i] = t
        if k == 'v' and off == 0x10 and old == 0 and new == 1:
            report(st, born, level, t, i)


def pos(st, k, i):
    return [s32(st.get((k, i, o), 0)) / 65536 for o in (0, 4, 8)]


def report(st, born, level, t, i):
    me = pos(st, 'x', i)
    pl = pos(st, 'x', 9)
    handles = {st.get(('n', j, 0)): f'slot {j}' for j in range(5)}
    handles[st.get(('n', 9, 0))] = 'player'
    myh = st.get(('n', i, 0))
    typ = st.get(('h', i, 0), 0)
    out = []
    for v in range(1, 24):
        flags, src, sid = st.get(('o', v, 0), 0), st.get(('o', v, 4), 0), st.get(('o', v, 8), 0)
        bank, loop = st.get(('o', v, 0xC), 0) & 0xFFFF, (st.get(('o', v, 0xC), 0) >> 16) & 0xFF
        age = st.get(('a', v, 0), 0) & 0xFFFF
        word = st.get(('e', 0, v & ~3), 0)
        enabled = (word >> (8 * (v & 3))) & 0xFF
        if not enabled or flags & 3 or loop or src == myh or age >= 60:
            continue
        w0, w1 = st.get(('a', v, 4), 0), st.get(('a', v, 8), 0)
        sp = [s16(w0 & 0xFFFF), s16(w0 >> 16), s16(w1 & 0xFFFF)]
        d = math.dist(me, sp)
        out.append(f'v{v} {handles.get(src, f"{src:08X}")} bank {bank} id {sid} age {age} at {d:.0f}')
    print(f'{level} {t} slot {i} {typ:04X} detects: player at {math.dist(me, pl):.0f}; sounds: '
          + ('; '.join(out) or 'none'))


main()
