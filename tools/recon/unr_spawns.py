#!/usr/bin/env python3
"""Where each soldier of a level comes from and which behaviour its record starts it in
(docs/re/enemy_types.md, Level differences; levels.md for records and triggers).

  unr_spawns.py RAMDUMP [--bits HEX]     one line per kind-3 record

Per soldier/dog record: index, position, the record's own script (+0x78), the values its msg-0
handler passes to set_flags (the `s40` behaviour bits the soldier starts with), the other
set_flags values in that script with their messages, its tags, and the kind-1 trigger boxes
whose enter list sends 3 and which share a tag with it (index and position), plus the distance
from the player's position in the dump. --bits keeps only records whose script sets any of
those bits.
"""
import math
import re
import subprocess
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from lvl_records import Ram, records, lists, script_ids  # noqa: E402


def script_flags(path):
    """{script id: [(msg, set_flags value)]} from lvs.py's folded listing."""
    out = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'lvs.py'), path],
                         capture_output=True, text=True).stdout
    res, sid, msg = {}, None, None
    for line in out.splitlines():
        m = re.match(r'script (\d+) at', line)
        if m:
            sid = int(m.group(1))
            res[sid] = []
            continue
        m = re.match(r'on msg ([0-9A-F]+):', line)
        if m:
            msg = int(m.group(1), 16)
            continue
        m = re.search(r'set_flags\((\d+)\)', line)
        if m and sid is not None:
            res[sid].append((msg, int(m.group(1))))
    return res


def main():
    path = sys.argv[1]
    bits = int(sys.argv[sys.argv.index('--bits') + 1], 16) if '--bits' in sys.argv else 0
    ram = Ram(path)
    flags = script_flags(path)
    pl = ram.u32(0x8009D654)
    px, py, pz = (ram.s32(pl + 0x20C + 4 * k) >> 16 for k in range(3))
    recs = []
    for i, pair, state, rec in records(ram):
        kind, sid, own = script_ids(ram, rec)
        pos = (ram.s16(rec + 0x10), ram.s16(rec + 0x12), ram.s16(rec + 0x14))
        recs.append((i, state, kind, sid, own, pos, lists(ram, rec)))
    trig = [(i, pos, set(ls.get('tags', []))) for i, _, kind, _, _, pos, ls in recs
            if kind == 1 and 3 in ls.get('enter', [])]
    print(f'player at ({px}, {py}, {pz})')
    for i, state, kind, sid, own, pos, ls in recs:
        if kind != 3:
            continue
        fl = flags.get(own, [])
        start = [v for m, v in fl if m == 0]
        other = [(m, v) for m, v in fl if m != 0]
        if bits and not any((v & bits) for _, v in fl):
            continue
        tags = set(ls.get('tags', []))
        ts = [(j, p) for j, p, t in trig if t & tags]
        d = math.dist((px, pz), (pos[0], pos[2]))
        text = f'{i:3} at {pos} d{d:6.0f} script {sid}/{own} start {" ".join(hex(v) for v in start) or "-"}'
        if other:
            text += ' other ' + ' '.join(f'{m:X}:{v:#x}' for m, v in other)
        text += ' tags ' + ' '.join(f'{t:X}' for t in sorted(tags))
        text += ' trig ' + ' '.join(f'{j}{p}' for j, p in ts)
        print(text)


if __name__ == '__main__':
    main()
