#!/usr/bin/env python3
"""Follow the character pool's soldiers and dogs script by script in a run (docs/re/enemy_types.md).

Watch list (`ver_ai.py --watch [--vars]`): per character slot (0x800B7B08 + i*0x2814) the type
(+0x8), flags A (+0xC), health (+0xF4), both script channels' current script (+0x104+0x10 and
+0x178+0x10) and program counter, the script list's address (logged as 'at' when it changes) and,
with --vars, the word variables (+0x4C, 0x28 words). Run with VALOR_WATCH set to it (whole words)
and optionally VALOR_TRACE_STATE=FILE for positions and animations.

ver_ai.py LOG [--state FILE] [--slot N] [--from V] [--to V] [--vars 16,22,...] [--anim]
  prints, per slot, each change of script (channel 0 'c0', channel 1 'c1') with the vblank,
  health, the distance to the player and, with --anim, the slot's animation changes; --vars
  adds changes of those variable ids (decimal or 0x). --dump RAM (one or more, a RAM dump
  taken in the same run's levels) resolves the program counters: --pc prints every change of
  either channel's position as script:word (word = offset in 32-bit words), the called
  script included (a call leaves +0x10 alone and moves the counter into the callee).
"""
import json, math, re, sys

SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def watch_string(with_vars):
    w = []
    for i, b in enumerate(SLOTS):
        w += [f'n{i}={b:X}+0:4', f'h{i}={b:X}+8:8', f'p{i}={b:X}+F4:4', f'c{i}={b:X}+114:8', f'd{i}={b:X}+188:8',
              f'l{i}=[[[{b:X}+100]+8]+B8]:4']
        w.append(f'x{i}={b:X}+20C:C')
        if with_vars:
            w.append(f'v{i}={b:X}+4C:A0')
    w += ['x9=[8009D654]+20C:C', 'p9=[8009D654]+F4:4', 'f9=[8009D654]+10:4']
    return ';'.join(w)


def script_tables(paths):
    """{list base: [(code start, length in words, index)]} for every model in the dumps."""
    import struct
    tables = {}
    for p in paths:
        d = open(p, 'rb').read()
        w = lambda a: struct.unpack_from('<I', d, a - 0x80000000)[0]
        h = lambda a: struct.unpack_from('<H', d, a - 0x80000000)[0]
        for off in range(0, len(d) - 0xC0, 4):
            base = struct.unpack_from('<I', d, off + 0xB8)[0]
            cnt = struct.unpack_from('<I', d, off + 0xBC)[0] & 0xFFFF
            if not (0x80000000 <= base < 0x80200000 and 20 <= cnt <= 400) or base in tables:
                continue
            try:
                rows = [(w(base + 16 * k + 0xC), h(base + 16 * k + 0xA), k) for k in range(cnt)]
            except struct.error:
                continue
            if all(0x80000000 <= c < 0x80200000 and 0 <= n < 4000 for c, n, _ in rows):
                tables[base] = rows
    return tables


def locate(tables, base, pc):
    for c, n, k in tables.get(base, ()):
        if c <= pc < c + 4 * n:
            return f'{k}:{(pc - c) // 4}'
    return f'?{pc:08X}'


def arg(name, conv, default):
    return conv(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def main():
    if sys.argv[1] == '--watch':
        print(watch_string('--vars' in sys.argv))
        return
    slot_only = arg('--slot', int, None)
    lo, hi = arg('--from', int, 0), arg('--to', int, 1 << 60)
    vars_ = [int(x, 0) for x in arg('--vars', str, '').split(',') if x]
    pos = {}
    anims = {}
    if '--state' in sys.argv:
        for line in open(arg('--state', str, None)):
            try:
                j = json.loads(line)
            except ValueError:
                continue
            v = j['vblank']
            if not lo <= v <= hi:
                continue
            pos[v] = {e[0]: e for e in j['ents']}
            pos[v]['player'] = j['ents'][1] if len(j['ents']) > 1 else None
    st = {}
    base = {}
    tables = script_tables([a for k, a in zip(sys.argv, sys.argv[1:]) if k == '--dump'])
    show_pc = '--pc' in sys.argv
    wre = re.compile(r'^watch (\d+) (\w)(\d)\+([0-9A-F]+): ([0-9A-F]{8}) -> ([0-9A-F]{8})')
    are = re.compile(r'^watch (\d+) l(\d) at ([0-9A-F]{8})')
    level = '?'
    for line in open(sys.argv[1], errors='replace'):
        m = re.match(r'replay: mark mission (\d) (\d)', line)
        if m:
            level = f'{m[1]}:{m[2]}'
            print(f'-- level {level}')
        m = are.match(line)
        if m:
            base[int(m[2])] = int(m[3], 16)
            continue
        m = wre.match(line)
        if not m:
            continue
        v, kind, i, off, old, new = int(m[1]), m[2], int(m[3]), int(m[4], 16), int(m[5], 16), int(m[6], 16)
        st[(kind, i, off)] = new
        if slot_only is not None and i != slot_only or not lo <= v <= hi:
            continue
        hp = s32(st.get(('p', i, 0), 0)) / 65536
        typ = st.get(('h', i, 0), 0)
        b = base.get(i, 0)
        if kind in 'cd' and off == 0:
            ch = 0 if kind == 'c' else 1
            n = (new - b) // 16 if new and b else -1
            extra = ''
            extra = dist_st(st, i)
            print(f'{v:7d} slot {i} {typ:04X} c{ch} -> {n:4d}  hp {hp:5.2f}{extra}')
        elif kind in 'cd' and off == 4 and show_pc and new:
            print(f'{v:7d} slot {i} {typ:04X} {kind == "c" and "c0" or "c1"} @ {locate(tables, base.get(i, 0), new)}')
        elif kind == 'h' and off == 4 and (old ^ new) & 0x20080000:
            print(f'{v:7d} slot {i} {typ:04X} flags A {old:08X} -> {new:08X}')
        elif kind == 'h' and off == 0 and new != old:
            print(f'{v:7d} slot {i} type {old:X} -> {new:X}')
        elif kind == 'v' and off // 4 in vars_:
            print(f'{v:7d} slot {i} {typ:04X} v{off // 4:02X} {s32(old)} -> {s32(new)}')


def dist_st(st, i):
    a = [s32(st.get(('x', i, o), 0)) / 65536 for o in (0, 4, 8)]
    b = [s32(st.get(('x', 9, o), 0)) / 65536 for o in (0, 4, 8)]
    return f'  dist {math.dist(a, b):6.0f} xz {math.dist(a[::2], b[::2]):6.0f} dy {a[1] - b[1]:5.0f}'


def dist_note(ents, handle):
    me, pl = ents.get(handle), ents.get('player')
    if not me or not pl:
        return ''
    d = math.dist([c / 65536 for c in me[3:6]], [c / 65536 for c in pl[3:6]])
    return f'  dist {d:6.0f}  anim {me[10]}'


main()
