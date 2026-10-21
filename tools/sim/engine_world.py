"""The world's collision for the engine: the level's TSP chunks (levels.md, Collision) in the slots
the CD had loaded at each frame (the original's timing, build/engine/timing.log), the floor and
walker rules of move_sim.py, and the segment sweep (`world_sweep`)."""
import os
import re
import struct

import move_sim as ms

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
TIMING = os.path.join(ROOT, "build", "engine", "timing.log")
WATCH = re.compile(r"^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)")


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


class Timeline:
    """The chunk slot table (0x80090DE8, 4 x 0x28) and the slot being loaded (0x800AA184) per
    frame, from a watch of the original: the CD's timing, an input to the engine."""
    _cache = None

    @classmethod
    def get(cls):
        if cls._cache is None:
            cls._cache = cls(TIMING)
        return cls._cache

    def __init__(self, path):
        self.events = []        # (vblank, name, offset, new)
        for line in open(path, errors="replace"):
            if not line.startswith("watch "):
                continue
            m = WATCH.match(line)
            if m and m[2] in ("k", "ld", "tmr", "qi"):
                self.events.append((int(m[1]), m[2], int(m[3], 16), int(m[5], 16)))


class Collision:
    def __init__(self, eng):
        self.eng = eng
        self.w = ms.World(eng.level)
        m = eng.m
        self.words = {o: m.u32(0x80090DE8 + o) for o in range(0, 0xA0, 4)}
        self.loading = m.s32(0x800AA184)
        self.tl = Timeline.get()
        self.i = 0
        seed_v = None
        self._apply()

    def at(self, vb):
        """The table as the original had it while running the frame at vblank vb (the watch logs
        each frame's end: changes logged before vb apply)."""
        ev = self.tl.events
        changed = False
        while self.i < len(ev) and ev[self.i][0] < vb:
            v, name, off, new = ev[self.i]
            if name == "k":
                self.words[off] = new
                changed = True
            elif name == "ld":
                self.loading = s32(new)
                changed = True
            self.i += 1
        if changed:
            self._apply()

    def skip_to(self, vb):
        ev = self.tl.events
        while self.i < len(ev) and ev[self.i][0] <= vb:
            self.i += 1

    def _apply(self):
        rects = []
        for slot in range(4):
            a, b = self.words[0x28 * slot], self.words[0x28 * slot + 4]
            rects.append((s16(a), s16(a >> 16), s16(b), s16(b >> 16)))
        self.w.set_slots(rects)
        if 0 <= self.loading < 4:
            self.w.slots[self.loading] = None
        m = self.eng.m
        for o, v in self.words.items():
            m.w32(0x80090DE8 + o, v)
        m.w32(0x800AA184, self.loading)

    # -- the floor (move_sim's floor_find)
    def floor(self, pos):
        return ms.floor_find(self.w, pos)

    # -- sweeping a segment (world_sweep, 0x8003EF5C; levels.md, Collision)
    def sweep(self, a, b):
        """The nearest hit of the segment a -> b (integer points) against every loaded chunk whose
        rectangle holds either end or is crossed: (point, normal, face) or None."""
        best = None
        for ch in self.w.loaded():
            if not seg_rect(ch.rect, a, b):
                continue
            hit = sweep_chunk(ch, a, b)
            if hit and (best is None or hit[0] < best[0]):
                best = hit
        if best is None:
            return None
        t, p, n, f = best
        return p, n, f


def seg_rect(r, a, b):
    x0, z0, x1, z1 = r
    for p in (a, b):
        if x0 <= p[0] <= x1 and z0 <= p[2] <= z1:
            return True
    # the segment crossing the rectangle (Liang-Barsky on x/z)
    t0, t1 = 0.0, 1.0
    dx, dz = b[0] - a[0], b[2] - a[2]
    for p, q in ((-dx, a[0] - x0), (dx, x1 - a[0]), (-dz, a[2] - z0), (dz, z1 - a[2])):
        if p == 0:
            if q < 0:
                return False
        else:
            t = q / p
            if p < 0:
                t0 = max(t0, t)
            else:
                t1 = min(t1, t)
    return t0 <= t1


def sweep_chunk(ch, a, b):
    """Every face of the leaves the segment passes (the tree walk is replaced by testing the
    leaves whose cells the segment's box touches, which visits a superset): a face is hit where
    its plane's distance changes sign between the ends, t = |d0| / (|d0| + |d1|) in 1/4096, and
    the point is inside the triangle (edge cross products against the normal)."""
    best = None
    xmin, xmax = min(a[0], b[0]), max(a[0], b[0])
    zmin, zmax = min(a[2], b[2]), max(a[2], b[2])
    seen = set()
    for start, count in leaves_in(ch, xmin, zmin, xmax, zmax):
        for fi in ch.idx[start:start + count]:
            if fi in seen:
                continue
            seen.add(fi)
            f = ch.faces[fi]
            n = ch.norms[f[3]]
            d0 = ((n[0] * a[0] + n[1] * a[1] + n[2] * a[2]) >> 15) + f[4]
            d1 = ((n[0] * b[0] + n[1] * b[1] + n[2] * b[2]) >> 15) + f[4]
            if (d0 < 0) == (d1 < 0) or d0 == d1:
                continue
            t = (abs(d0) << 12) // (abs(d0) + abs(d1))
            p = tuple(a[k] + (((b[k] - a[k]) * t) >> 12) for k in range(3))
            if not inside(ch, f, n, p):
                continue
            if best is None or t < best[0]:
                best = (t, p, n, (ch.name, fi))
    return best


def inside(ch, f, n, p):
    v = [ch.verts[f[k]] for k in range(3)]
    sign = 0
    for k in range(3):
        e = [v[(k + 1) % 3][i] - v[k][i] for i in range(3)]
        q = [p[i] - v[k][i] for i in range(3)]
        c = (e[1] * q[2] - e[2] * q[1], e[2] * q[0] - e[0] * q[2], e[0] * q[1] - e[1] * q[0])
        d = c[0] * n[0] + c[1] * n[1] + c[2] * n[2]
        if d == 0:
            continue
        s = 1 if d > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def leaves_in(ch, xmin, zmin, xmax, zmax):
    """The k-d leaves whose cells meet the box: (face index start, count)."""
    out = []
    stack = [(0, ch.rect[0], ch.rect[2], ch.rect[1], ch.rect[3])]
    while stack:
        node, x0, x1, z0, z1 = stack.pop()
        a, b, split, _ = ch.nodes[node]
        if a < 0:
            out.append((b, ~a))
            continue
        if b >= 0:
            s = s16(x0 + split)
            if xmin < s:
                stack.append((a, x0, s - 1, z0, z1))
            if xmax >= s:
                stack.append((b, s, x1, z0, z1))
        else:
            s = s16(z0 + split)
            if zmin < s:
                stack.append((a, x0, x1, z0, s - 1))
            if zmax >= s:
                stack.append((~b, x0, x1, s, z1))
    return out
