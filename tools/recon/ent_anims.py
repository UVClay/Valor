#!/usr/bin/env python3
"""Survey models and their animations across RAM dumps of several levels (docs/re/animation.md).

usage: ent_anims.py survey DUMP...        models using the second animation format (+0x98),
                                          mesh swaps (+0xEC.. hashes, entry mesh bytes), and
                                          per entity type: models, bones, hit parts, animations
       ent_anims.py table TYPE DUMP...    every animation index of that entity type's models:
                                          frame count, levels that load it, root movement
                                          (forward z, sideways x, total) and root turn (yaw)
TYPE as hex, e.g. 0x1402 (soldier), 0x1405, 1 (first-person weapons).
"""
import struct, sys, os
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sst  # noqa: E402  (the script disassembler: instruction lengths)


def sx(v, lo, n):
    v = (v >> lo) & ((1 << n) - 1)
    return v - (1 << n) if v >> (n - 1) else v


class Ram:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        self.name = os.path.basename(path).replace('.bin', '')

    def u32(self, a): return struct.unpack_from('<I', self.d, a & 0x1FFFFF)[0]
    def u16(self, a): return struct.unpack_from('<H', self.d, a & 0x1FFFFF)[0]
    def s16(self, a): return struct.unpack_from('<h', self.d, a & 0x1FFFFF)[0]
    def u8(self, a): return self.d[a & 0x1FFFFF]

    def models(self):
        base, n = self.u32(0x8009EC58), self.u32(0x8009EC54)
        for i in range(min(n, 200)):
            yield i, base + i * 0x100

    def anims(self, m):
        """{index: (frames, bones, mesh, frame pointer)} of the model's table."""
        t = self.u32(m + 0x08)
        out = {}
        if not t & 0x80000000:
            return out
        for i in range(self.u16(t)):
            e = self.u32(t + 4 + 4 * i)
            if e & 0x80000000:
                out[i] = (self.u8(e), self.u8(e + 1), self.u8(e + 2), self.u32(e + 4))
        return out

    def motion(self, frames, ptr):
        x = z = yaw = 0
        for f in range(frames):
            w = self.u32(ptr + 20 * f)
            x += sx(w, 0, 10)
            z += sx(w, 21, 10)
            yaw += self.s16(ptr + 20 * f + 0xA)
        return x, z, yaw

    def bones(self, m):
        root, n, stack = self.u32(m + 0x9C), 0, []
        if root & 0x80000000:
            stack.append(root)
        while stack and n < 200:
            b = stack.pop()
            n += 1
            for p in (self.u32(b + 0xC), self.u32(b + 0x10)):
                if p & 0x80000000:
                    stack.append(p)
        return n


def survey(paths):
    types = defaultdict(set)
    for p in paths:
        r = Ram(p)
        for i, m in r.models():
            h, t = r.u32(m), r.u32(m + 0xFC)
            an = r.anims(m)
            if r.u32(m + 0x98):
                print(f'{r.name}: model {i} {h:08X} type {t:#x}: +0x98 = {r.u32(m + 0x98):08X}')
            swaps = [r.u32(m + 0xEC + 4 * k) for k in range(4)]
            if any(s != 0xFFFFFFFF for s in swaps):
                print(f'{r.name}: model {i} {h:08X} type {t:#x}: +0xEC {[hex(s) for s in swaps]}')
            mesh = {k: v[2] for k, v in an.items() if v[2]}
            if mesh:
                print(f'{r.name}: model {i} {h:08X} type {t:#x}: animations with a mesh byte {mesh}')
            if an:
                types[t].add((h, r.bones(m), r.u16(m + 0xBE), r.u16(r.u32(m + 8)), len(an)))
    for t in sorted(types):
        print(f'type {t:#x}:', ', '.join(f'{h:08X} ({b} bones, {pa} parts, {n} slots, {k} loaded)'
                                         for h, b, pa, n, k in sorted(types[t])))


def script_uses(r, typ):
    """{anim: [script numbers]} over the scripts of the first model of that type (op 00)."""
    uses = defaultdict(list)
    for i, m in r.models():
        if r.u32(m + 0xFC) != typ:
            continue
        lst, count = r.u32(m + 0xB8), r.u16(m + 0xBC)
        for k in range(count):
            s = lst + 16 * k
            n, code = r.u16(s + 0xA), r.u32(s + 0xC)
            words = [r.u32(code + 4 * j) for j in range(n)]
            j = 0
            while j < n:
                op = words[j] >> 24
                if op == 0 and j + 1 < n and (words[j] & 0xFFF) not in uses or op == 0 and k not in uses[words[j] & 0xFFF]:
                    uses[words[j] & 0xFFF].append(k)
                j += max(sst.length(op, words, j), 1)
        break
    return uses


def table(typ, paths):
    rows = defaultdict(lambda: {'frames': set(), 'levels': [], 'mot': set()})
    for p in paths:
        r = Ram(p)
        seen = set()
        for i, m in r.models():
            if r.u32(m + 0xFC) != typ:
                continue
            for k, (fr, bo, me, ptr) in r.anims(m).items():
                if (k, ptr) in seen:
                    continue
                seen.add((k, ptr))
                row = rows[k]
                row['frames'].add(fr)
                if r.name not in row['levels']:
                    row['levels'].append(r.name)
                row['mot'].add(r.motion(fr, ptr))
    ref = [Ram(p) for p in paths if os.path.basename(p) in ('1_3.bin', '2_1.bin')]
    uses = [script_uses(r, typ) for r in ref]
    print('| Anim | Frames | Levels | Root movement x, z (total) | Root turn (yaw) | Scripts ' +
          ' / '.join(r.name for r in ref) + ' |')
    print('|---|---|---|---|---|---|')
    for k in sorted(rows):
        row = rows[k]
        mots = sorted(row['mot'])
        mot = '; '.join(f'{x}, {z}' for x, z, _ in mots[:2])
        yaw = '; '.join(str(y) for _, _, y in mots[:2])
        lv = len(row['levels'])
        sc = ' / '.join(' '.join(map(str, sorted(u.get(k, [])))) or '-' for u in uses)
        print(f"| {k} | {'/'.join(map(str, sorted(row['frames'])))} | {lv} | {mot} | {yaw} | {sc} |")


def main():
    if sys.argv[1] == 'survey':
        survey(sys.argv[2:])
    elif sys.argv[1] == 'table':
        table(int(sys.argv[2], 0), sys.argv[3:])


main()
