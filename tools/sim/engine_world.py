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
    def sweep(self, frm, to):
        """world_sweep(from, to) with integer points: every loaded chunk whose rectangle holds
        either end or is crossed (0x8003EC78, 0x8003ECE4) is walked from `to` toward `from`
        (0x8003EACC swaps them) by chunk_sweep; the chunk with the smallest t wins (strictly).
        Returns (hit point, normal, face) or None."""
        best, out = 0x7FFF, None
        for ch in self.w.loaded():
            r = ch.rect
            if not (in_rect(r, frm[0], frm[2]) or in_rect(r, to[0], to[2]) or crosses(r, frm, to)):
                continue
            hit = chunk_sweep(ch, to, frm)
            if hit is not None and hit[0] < best:
                best, out = hit[0], hit
        if out is None:
            return None
        return out[1], out[2], out[3]


def in_rect(r, x, z):
    return r[0] <= x <= r[2] and r[1] <= z <= r[3]


def crosses(r, a, b):
    """0x8003ECE4: the segment a -> b (x, z) crossing the rectangle, as its code tests it."""
    t1, t0 = s16(a[0]), s16(a[2])
    t5, t6 = s16(b[0]), s16(b[2])
    t2 = t5 - t1
    a1 = t6 - t0
    x0, z0, x1, z1 = r
    lo = (x1 - t1) * a1
    t4 = (z1 - t0) * t2 - lo
    t7 = (z0 - t0) * t2 - lo
    if (t4 ^ t7) <= 0 and x1 < t1 and t5 < x1:
        return True
    t3 = (z1 - t0) * t2 - (x0 - t1) * a1
    if not ((t4 ^ t3) > 0 or z1 >= t0 or t6 >= z1):
        return True
    v1 = (z0 - t0) * t2 - (x0 - t1) * a1
    if (v1 ^ t3) <= 0 and not (x0 >= t1 or t5 >= x0):
        return True
    if (v1 ^ t7) <= 0 and not (z0 >= t0 or not (t6 < z0)):
        return True
    return False


def chunk_sweep(ch, start, end):
    """chunk_sweep (0x8003E3F0): the k-d tree walked along the segment (x, z), the near half
    first, up to 8 far halves deferred (LIFO); at each leaf the faces are tested (leaf_sweep,
    0x8003DC10) with the best t of the chunk so far; the first leaf with a hit ends the walk.
    Returns (t, point, normal, face) or None."""
    sx, ex, sz, ez = s16(start[0]), s16(end[0]), s16(start[2]), s16(end[2])
    x0, z0, x1, z1 = ch.rect
    stack = []
    node = 0
    best = [0x7FFF, None]
    miny, maxy = min(start[1], end[1]), max(start[1], end[1])
    while True:
        a, b, split, _ = ch.nodes[node]
        if a >= 0:
            if b >= 0:
                sp = s16(x0 + split)
                if sx < sp:
                    if sp < ex and len(stack) < 8:
                        f = _frac(sp - sx, ex - sx)
                        nz = s16(sz + (((ez - sz) * f) >> 16))
                        stack.append((b, sp, x1, z0, z1, sp, ex, nz, ez))
                        x1, ex, ez = sp - 1, sp, nz
                    else:
                        x1 = sp - 1
                    node = a
                else:
                    if ex < sp and len(stack) < 8:
                        f = _frac(sp - sx, ex - sx)
                        nz = s16(sz + (((ez - sz) * f) >> 16))
                        stack.append((a, x0, sp - 1, z0, z1, sp, ex, nz, ez))
                        x0, ex, ez = sp, sp, nz
                    else:
                        x0 = sp
                    node = b
            else:
                sp = s16(z0 + split)
                if sz < sp:
                    if sp < ez and len(stack) < 8:
                        f = _frac(sp - sz, ez - sz)
                        nx = s16(sx + (((ex - sx) * f) >> 16))
                        stack.append((~b, x0, x1, sp, z1, nx, ex, sp, ez))
                        z1, ez, ex = sp - 1, sp, nx
                    else:
                        z1 = sp - 1
                    node = a
                else:
                    if ez < sp and len(stack) < 8:
                        f = _frac(sp - sz, ez - sz)
                        nx = s16(sx + (((ex - sx) * f) >> 16))
                        stack.append((a, x0, x1, z0, sp - 1, nx, ex, sp, ez))
                        z0, ez, ex = sp, sp, nx
                    else:
                        z0 = sp
                    node = ~b
            continue
        if leaf_sweep(ch, ch.idx[b:b + ~a], start, end, miny, maxy, best):
            return best[0], best[1][0], best[1][1], best[1][2]
        if not stack:
            return None
        node, x0, x1, z0, z1, sx, ex, sz, ez = stack.pop()


def _frac(num, den):
    if den == 0:
        return 0
    q = abs(num << 16) // abs(den)
    return q if ((num << 16) >= 0) == (den >= 0) else -q


def leaf_sweep(ch, faces, a, b, miny, maxy, best):
    """leaf_sweep (0x8003DC10): faces crossed from their front (d0 >= 0) to their back, the
    crossing at t = |d0| * 4096 / (|d0| + |d1|) inside the triangle (three edge cross products
    against the normal, GTE OP sf=1); the smallest t of the chunk so far. True if one hit."""
    import engine_gte as gt
    hit = False
    delta = [s16(b[k] - a[k]) for k in range(3)]
    for fi in faces:
        f = ch.faces[fi]
        V = [ch.verts[f[k]] for k in range(3)]
        lo = min(V[0][1], V[1][1], V[2][1])
        hi = max(V[0][1], V[1][1], V[2][1])
        if hi < miny or maxy < lo:
            continue
        n = ch.norms[f[3]]
        d0 = (s32(n[0] * a[0] + n[1] * a[1] + n[2] * a[2]) >> 15) + f[4]
        d1 = (s32(n[0] * b[0] + n[1] * b[1] + n[2] * b[2]) >> 15) + f[4]
        if d0 < 0 or (d0 ^ d1) >= 0:
            continue
        t = (abs(d0) << 12) // (abs(d0) + abs(d1))
        if not t < best[0]:
            continue
        g = [gt.sat16((t * delta[k]) >> 12) for k in range(3)]
        p = [s16(g[k] + a[k]) for k in range(3)]
        ok = True
        for i in range(3):
            va, vb = V[i], V[(i + 1) % 3]
            A = [s16(p[k] - va[k]) for k in range(3)]
            B = [s16(vb[k] - va[k]) for k in range(3)]
            _, c = gt.op(A, B, 12)
            if s32(n[0] * c[0] + n[1] * c[1] + n[2] * c[2]) < 0:
                ok = False
                break
        if not ok:
            continue
        best[0] = t
        best[1] = (tuple(p), n, (ch.name, fi))
        hit = True
    return hit


