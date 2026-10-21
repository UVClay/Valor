#!/usr/bin/env python3
"""A reference reimplementation of the player's movement (docs/re/movement.md, levels.md
Collision), stepped tick by tick against the original's state trace.

  move_sim.py watch                    the VALOR_WATCH list the check needs
  move_sim.py check TRACE.jsonl WATCH.log M:L [--from VBLANK] [--to VBLANK] [--ram DUMP@VBLANK]
                    [--free] [-v]
  move_sim.py floor M:L X Y Z          the floor under a point (all of the level's chunks)

`check` reads the original's per-tick state (VALOR_TRACE_STATE: the soldier's position, the look
yaw) and the soldier's fields from a VALOR_WATCH log of
  VALOR_WATCH="$(python3 tools/sim/move_sim.py watch)"
(s: the soldier from +0x1F0; l: the look/weapon block; k: the four chunk slots' rectangles;
o: +0x3BC, where entity_move left him; e: his flags), seeded by --ram DUMP@VBLANK (one RAM dump
per level, as a watch logs only changes), takes
from them the inputs the control function produced each tick (+0x200 forward, +0x218 strafe,
the jump as the vertical speed it set, the eye height +0x2D6, the look yaw) and steps the
documented movement from the original's state of the tick before (default) or from its own
state (--free). It prints per tick what differs and a summary per behaviour.

The world collision comes from the level's TSP chunks on the extracted disc (formats.md), in the
slots the watch shows loaded (streaming changes them).
"""
import json
import math
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools", "recon"))
import tspcol  # noqa: E402

NONE = -0x7FFF
FX = 1 << 16
WATCH_LIST = "s=[8009D654]+1F0:100;l=[[8009D654]+388]+0:1A0;k=80090DE8:A0;o=[8009D654]+3BC:C;e=[8009D654]+0:40"


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


# PsyQ's rcossin_tbl (exe 0x8003040C): checked equal to round(4096 sin), round(4096 cos) for
# all 4096 entries of the disc's SLUS_009.74.
SIN = [round(4096 * math.sin(2 * math.pi * i / 4096)) for i in range(4096)]
COS = [round(4096 * math.cos(2 * math.pi * i / 4096)) for i in range(4096)]


def sincos(a):
    """RotMatrixYXZ's lookup of a halfword angle."""
    a = s16(a)
    if a >= 0:
        return SIN[a & 0xFFF], COS[a & 0xFFF]
    return -SIN[(-a) & 0xFFF], COS[(-a) & 0xFFF]


def nclip(p0, p1, p2):
    """GTE NCLIP of three (sx, sy) points, as the 32-bit MAC0."""
    return s32(p0[0] * p1[1] + p1[0] * p2[1] + p2[0] * p0[1] - p0[0] * p2[1] - p1[0] * p0[1] - p2[0] * p1[1])


# ---------------------------------------------------------------- the world

class Chunk:
    def __init__(self, path):
        data = open(path, "rb").read()
        c = tspcol.parse(data)
        off = c["start"]
        self.name = os.path.basename(path)
        self.rect = c["rect"]
        n_nodes = len(c["nodes"])
        # tspcol reads three halfwords per node; the fourth is the zone id.
        self.nodes = [struct.unpack_from("<hhHH", data, off + 0x12 + 8 * i) for i in range(n_nodes)]
        self.idx = c["idx"]
        self.verts = c["verts"]
        self.norms = c["norms"]
        self.faces = c["faces"]


class World:
    """The loaded chunk slots (0x80090DE8), in slot order; None for an empty or loading slot."""

    def __init__(self, level):
        m, l = level.split(":")
        self.dir = os.path.join(ROOT, "extracted", "moh", "DATA", f"MSN{m}", f"LVL{l}", "TSP0")
        self.files = {}
        n = 1
        while os.path.exists(os.path.join(self.dir, f"{m}_{l}_C{n}.TSP")):
            ch = Chunk(os.path.join(self.dir, f"{m}_{l}_C{n}.TSP"))
            self.files[ch.rect] = ch
            n += 1
        self.slots = []

    def set_slots(self, rects):
        self.slots = [self.files.get(tuple(r)) for r in rects]

    def all_chunks(self):
        self.slots = list(self.files.values())

    def loaded(self):
        return [c for c in self.slots if c is not None]


# ---------------------------------------------------------------- the floor (floor_find 0x80060474)

def tri_holds(ch, f, x, z):
    """0x8005FC00: the face is up-facing (normal y <= 0) and holds (x, z)."""
    v0, v1, v2 = (ch.verts[f[0]], ch.verts[f[1]], ch.verts[f[2]])
    if ch.norms[f[3]][1] > 0:
        return False
    # x: below v0's x or beyond the larger of v1's and v2's (v0 is not checked for the max,
    # nor v1/v2 for the min); z: the true min and max.
    if x < v0[0] or max(v1[0], v2[0]) < x:
        return False
    if z < min(v0[2], v1[2], v2[2]) or max(v0[2], v1[2], v2[2]) < z:
        return False
    p = (x, z)
    a, b, c = (v0[0], v0[2]), (v1[0], v1[2]), (v2[0], v2[2])
    return nclip(a, b, p) <= 0 and nclip(b, c, p) <= 0 and nclip(c, a, p) <= 0


def plane_height(ch, f, x, z):
    """0x8005FDCC: the plane's y at (x, z); nearly vertical planes give v0's y."""
    n = ch.norms[f[3]]
    if abs(n[1]) < 0x101:
        return ch.verts[f[0]][1]
    num = s32(-(n[0] * x + n[2] * z + (f[4] << 15)))
    q = abs(num) // abs(n[1])
    if (num < 0) != (n[1] < 0):
        q = -q
    return s16(q)


def floor_pick(ch, faces, p):
    """0x8005FEA0 over a leaf's faces; p = (x, y, z) integers."""
    best, best_below, best_above, best_dist = NONE, 0, 0, 0x7FFF
    for fi in faces:
        f = ch.faces[fi]
        if not tri_holds(ch, f, p[0], p[2]):
            continue
        h = plane_height(ch, f, p[0], p[2])
        y = p[1]
        dist = abs(h - y)
        below = y < h
        above = (not below) and not (h + 32 < y)
        cand_below = below if not above else False
        if not (cand_below or above):
            continue
        if best_below:
            take = True if not cand_below else h < best
        elif best_above:
            take = dist < best_dist
        else:
            take = True
        if take:
            best_below, best_above, best_dist, best = cand_below, above, dist, h
    return best


def floor_leaf(ch, p):
    """0x80060034: the k-d descent to the leaf holding (x, z)."""
    xmin, zmin = ch.rect[0], ch.rect[1]
    n = ch.nodes[0]
    while n[0] >= 0:
        a, b, split, _ = n
        if b >= 0:
            s = s16(xmin + split)
            if p[0] >= s:
                xmin = s
                n = ch.nodes[b]
            else:
                n = ch.nodes[a]
        else:
            s = s16(zmin + split)
            if p[2] < s:
                n = ch.nodes[a]
            else:
                zmin = s
                n = ch.nodes[~b]
    count, start, _, zone = ~n[0], n[1], n[2], n[3]
    return floor_pick(ch, ch.idx[start:start + count], p), zone


def floor_find(world, pos):
    """0x800603DC -> floor_search 0x8006028C. pos = 16.16 (x, y, z). Returns (floor, zone)."""
    x, y, z = s16(pos[0] >> 16), s16(pos[1] >> 16), s16(pos[2] >> 16)
    best, zone = NONE, 0xFFFF
    for ch in world.loaded():
        r = ch.rect
        if x < r[0] or r[2] < x or z < r[1] or r[3] < z:
            continue
        h, zn = floor_leaf(ch, (x, y, z))
        take = False
        if best == NONE:
            take = True
        elif h != NONE:
            # The game compares the feet's 16.16 y with whole-unit heights here.
            a0 = s32(pos[1])
            if a0 < h:
                v1 = h - a0
                if a0 < best:
                    take = h < best
                else:
                    take = v1 < abs(best - a0)
            else:
                take = (h - a0) < abs(best - a0)
        if take:
            best, zone = h, zn
    return best, zone


# ---------------------------------------------------------------- walls (walker_slide 0x80078F60)

def tri_near(ch, f, c, r, h, arg3):
    """0x800776C0: the triangle is near the walker's segment (centre c, radius r, height h)."""
    A, B, C = ch.verts[f[0]], ch.verts[f[1]], ch.verts[f[2]]
    n = ch.norms[f[3]]
    cx, cy, cz = c
    if cx + r < A[0] or max(B[0], C[0]) < cx - r:
        return False
    if cz + r < min(A[2], B[2], C[2]) or max(A[2], B[2], C[2]) < cz - r:
        return False
    if max(A[1], B[1], C[1]) < cy or cy + h < min(A[1], B[1], C[1]):
        return False
    rr = r * r
    wall = abs(n[1]) < max(abs(n[0]), abs(n[2]))
    for V in (A, B, C):
        if cy < V[1] < cy + h:
            dx, dz = s16(V[0] - cx), s16(V[2] - cz)
            if (dx * dx + dz * dz) & 0xFFFFFFFF < rr:
                if wall:
                    return True
                return (n[1] >= 0) if arg3 else True
    if wall:
        return wall_test(c, r, h, A, B, C, n)
    if arg3 and n[1] < 0:
        return False
    if n[1] < 0:
        raise NotImplementedError("up-facing plane test with arg3 = 0 (not used by the player)")
    # Down-facing (ceiling-like) triangle: the square of side 2r around (x, z) against the edges.
    pts = [(A[0], A[2]), (B[0], B[2]), (C[0], C[2])]
    for P0, P1 in ((pts[0], pts[1]), (pts[1], pts[2]), (pts[2], pts[0])):
        px = cx + r if P1[1] < P0[1] else cx - r
        pz = cz - r if P1[0] < P0[0] else cz + r
        if nclip(P0, P1, (s16(px), s16(pz))) < 0:
            return False
    return True


def wall_test(c, r, h, A, B, C, n):
    """0x8007711C: a wall-like triangle against the walker's box seen along the wall's main
    horizontal axis (x-y when |nx| < |nz|, z-y otherwise)."""
    h2 = h >> 1
    mid = c[1] + h2
    if abs(n[0]) < abs(n[2]):
        u, cu, pos_side = 0, c[0], n[2] < 0
    else:
        u, cu, pos_side = 2, c[2], n[0] > 0
    pts = [(A[u], A[1]), (B[u], B[1]), (C[u], C[1])]
    for P0, P1 in ((pts[0], pts[1]), (pts[1], pts[2]), (pts[2], pts[0])):
        if pos_side:
            pu = cu + r if P1[1] < P0[1] else cu - r
            pv = mid - h2 if P1[0] < P0[0] else mid + h2
            if nclip(P0, P1, (s16(pu), s16(pv))) < 0:
                return False
        else:
            pu = cu - r if P1[1] < P0[1] else cu + r
            pv = mid + h2 if P1[0] < P0[0] else mid - h2
            if nclip(P0, P1, (s16(pu), s16(pv))) > 0:
                return False
    return True


def plane_dot(n, p):
    """GTE MVMVA with sf=0 of a point and a normal: the 32-bit MAC1."""
    return s32(p[0] * n[0] + p[1] * n[1] + p[2] * n[2])


def plane_crosses(c, r, h, n, d):
    """0x80078350: the segment c .. c + (0, h, 0) crosses the plane, or its centre is in front
    and the point r sideways (against the normal's signs) is not."""
    t0 = (plane_dot(n, c) >> 15) + d
    v0 = t0 + ((h * n[1]) >> 15)
    if (t0 ^ v0) < 0:
        return True
    if t0 < 0:
        return False
    x = c[0] - r if n[0] > 0 else c[0] + r
    z = c[2] - r if n[2] > 0 else c[2] + r
    p = (s16(x), c[1], s16(z))
    v1 = (plane_dot(n, p) >> 15) + d
    v0 = v1 + ((h * n[1]) >> 15)
    if (v1 ^ v0) < 0:
        return True
    return (v1 ^ t0) < 0


def gather_leaf(ch, faces, c, r, h, delta, out, base, maxn):
    added = 0
    for fi in faces:
        f = ch.faces[fi]
        n = ch.norms[f[3]]
        if not tri_near(ch, f, c, r, h, 1):
            continue
        if not plane_dot(n, delta) < 0:  # only walls the move goes into
            continue
        if not plane_crosses(c, r, h, n, f[4]):
            continue
        if any(e[2] == fi for e in out[base:]):
            continue
        out.append((n, f[4], fi, ch.name))
        added += 1
        if len(out) - base >= maxn:
            return added
    return added


def gather_chunk(ch, c, r, h, delta, out, maxn):
    """0x800787C0: every leaf whose cell comes within r of (x, z), a stack of at most 8 halves."""
    base = len(out)
    x, z = c[0], c[2]
    stack = []
    xmin, xmax, zmin, zmax = ch.rect[0], ch.rect[2], ch.rect[1], ch.rect[3]
    node = 0
    total = 0
    while True:
        a, b, split, _ = ch.nodes[node]
        if a >= 0:
            if b >= 0:
                s = s16(xmin + split)
                if x - r < s:
                    if s < x + r and len(stack) < 8:
                        stack.append((b, s, xmax, zmin, zmax))
                    xmax = s - 1
                    node = a
                else:
                    xmin = s
                    node = b
            else:
                s = s16(zmin + split)
                if z - r < s:
                    if s < z + r and len(stack) < 8:
                        stack.append((~b, xmin, xmax, s, zmax))
                    zmax = s - 1
                    node = a
                else:
                    zmin = s
                    node = ~b
            continue
        k = gather_leaf(ch, ch.idx[b:b + ~a], c, r, h, delta, out, base, maxn)
        if k > 0:
            total += k
            if total >= maxn:
                return total
        if not stack:
            return total
        node, xmin, xmax, zmin, zmax = stack.pop()


def chunk_near(ch, x, z, r):
    """0x80078AA4: the rectangle holds (x, z), or its nearest edge point is within r. For a
    coordinate inside the rectangle's range, the nearer edge is used, not the coordinate."""
    x0, z0, x1, z1 = ch.rect
    if x0 <= x <= x1 and z0 <= z <= z1:
        return True
    if x0 < x:
        ex = x1 if (x < x1 and (x1 - x) < (x - x0)) or x >= x1 else x0
    else:
        ex = x0
    if z0 < z:
        ez = z1 if (z < z1 and (z1 - z) < (z - z0)) or z >= z1 else z0
    else:
        ez = z0
    return (z - ez) ** 2 + (x - ex) ** 2 < r * r


def walls_gather(world, c, r, h, delta, maxn=32):
    out = []
    total = 0
    for ch in world.loaded():
        if chunk_near(ch, c[0], c[2], r):
            total += gather_chunk(ch, c, r, h, delta, out, maxn)
        if total >= maxn:
            break
    return out


def capsule_dist(n, d, p, h, off):
    """0x80078BCC: the signed distance (16.16) of the feet (up-facing plane) or of the point h
    above them (others), less off."""
    t = s32(n[0] * (p[0] >> 15) + n[2] * (p[2] >> 15))
    yy = (p[1] >> 15) if n[1] < 0 else s32((p[1] >> 15) - 2 * h)
    return s32(t + n[1] * yy + (d << 16) - (off << 16))


def first_hit(planes, old, pos, r, fp):
    """0x80078D1C: of the planes the walker now penetrates, the one nearest at the old position."""
    best, idx = 0x7FFFFFFF, -1
    for i, (n, d, _, _) in enumerate(planes):
        if capsule_dist(n, d, pos, fp, r) < 0:
            d0 = capsule_dist(n, d, old, fp, 0)
            if d0 < best:
                best, idx = d0, i
    return idx


def wall_push(n, d, pos, fp, off):
    """0x80078C4C: out along the normal, horizontally, to off (radius + 4) in front."""
    v = capsule_dist(n, d, pos, fp, off) >> 8
    pos[0] = s32(pos[0] - (n[0] >> 7) * v)
    pos[2] = s32(pos[2] - (n[2] >> 7) * v)


def walker_slide(world, e, delta, old, log=None):
    """Returns (planes gathered at the end, result: 0 none, 1 still in a wall, 2 free)."""
    r, eye = e["radius"], e["eye"]
    fp = eye - 16
    pos = e["pos"]
    c = (s16(pos[0] >> 16), s16((pos[1] >> 16) - fp), s16(pos[2] >> 16))
    planes = walls_gather(world, c, r, eye - 20, delta)
    if not planes:
        return 0, 0
    left, pushed = len(planes), False
    while True:
        i = first_hit(planes, old, pos, r, fp)
        if i < 0:
            break
        pushed = True
        left -= 1
        if log is not None:
            log.append(("push", planes[i][2], planes[i][3]))
        wall_push(planes[i][0], planes[i][1], pos, fp, r + 4)
        if left < 0:
            break
    delta2 = tuple(s16((pos[k] >> 16) - (old[k] >> 16)) for k in range(3))
    if not pushed:
        return 0, 2
    # The re-check is gathered at the feet, not at the centre: the segment runs from the feet
    # down to eye - 20 below them.
    c2 = (s16(pos[0] >> 16), s16(pos[1] >> 16), s16(pos[2] >> 16))
    planes2 = walls_gather(world, c2, r, fp - 4, delta2)
    if not planes2:
        return 0, 2
    if first_hit(planes2, old, pos, r, fp) < 0:
        return len(planes2), 2
    return len(planes2), 1


# ---------------------------------------------------------------- entity_move (0x8006C83C)

def entity_move(world, e, log=None):
    """The player's entity_move: e has pos (16.16 list), vy, fwd, strafe, yaw (16.16), ground,
    floor (+0x208), old (+0x3BC), radius, eye, flags_b."""
    pos = e["pos"]
    on_ground0 = e["ground"]
    e["events"] = []
    ev = e["events"].append
    if not e["ground"] and e.get("gravity", True):
        e["vy"] = min(e["vy"] + 0x20000, 0x4B0000)
    vx = s16((e["strafe"] & 0xFFFFFFFF) >> 12)
    vy = s16((e["vy"] & 0xFFFFFFFF) >> 12)
    vz = s16((e["fwd"] & 0xFFFFFFFF) >> 12)
    sn, cs = sincos(e["yaw"] >> 16)
    mx = (cs * vx + sn * vz) >> 12
    my = (4096 * vy) >> 12
    mz = (-sn * vx + cs * vz) >> 12
    pos[0] = s32(pos[0] + (mx << 12) + e.get("push", (0, 0, 0))[0])
    pos[1] = s32(pos[1] + (my << 12) + e.get("push", (0, 0, 0))[1])
    pos[2] = s32(pos[2] + (mz << 12) + e.get("push", (0, 0, 0))[2])
    dy = my >> 4
    old = e["old"]
    delta = tuple(s16((pos[k] >> 16) - (old[k] >> 16)) for k in range(3))
    e["contact"] = 0
    if e.get("walls", True):
        n, res = walker_slide(world, e, delta, old, log)
        if res == 1:
            ev("still in a wall: move undone")
            pos[:] = list(old)
            e["ground"] = on_ground0
            e["contact"] |= 4
        elif res == 2 and n:
            e["contact"] |= 2
        if n > 0:
            e["contact"] |= 2
            e["strafe_cleared"] = True
    if e.get("flags_b", 0) & 0x10:
        pass
    else:
        f, zone = floor_find(world, pos)
        e["zone"] = zone
        e["floor_found"] = f
        if f == NONE:
            ev("no floor")
            if not (e.get("flags_b", 0) & 0x10000):
                pos[:] = list(old)
                if not on_ground0:
                    pos[1] = e["floor"]
                e["ground"] = 1
        else:
            e["floor"] = s32(f << 16)
            d = s16(pos[1] >> 16) - f
            if e["ground"]:
                if -32 <= d:
                    if d:
                        ev("step down" if d < 0 else "step up / floor above")
                    pos[1] = s32(f << 16)
                else:
                    ev("walked off an edge")
                    e["ground"] = 0
            elif e.get("gravity", True):
                land = d >= 0 or abs(d) <= (dy + 16 if dy < 48 else 64)
                if land and dy >= 0:
                    ev("landing")
                    e["ground"] = 1
                    pos[1] = s32(f << 16)
                    e["vy"] = 0
                    e["landed"] = True
    e["old"] = list(pos)


# ---------------------------------------------------------------- the original's traces

WATCH = re.compile(r"^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)")
WATCH_AT = re.compile(r"^watch (\d+) (\w+) at ([0-9A-F]+)")


def ram_seed(path):
    """The watched words as a RAM dump holds them (a watch only logs changes)."""
    m = open(path, "rb").read()
    r32 = lambda a: struct.unpack_from("<I", m, a & 0x1FFFFF)[0]
    sol = r32(0x8009D654)
    look = r32(sol + 0x388)
    seed = {}
    for name, base, n in (("s", sol + 0x1F0, 0x100), ("l", look, 0x1A0), ("k", 0x80090DE8, 0xA0),
                          ("o", sol + 0x3BC, 0xC), ("e", sol, 0x40)):
        for o in range(0, n, 4):
            seed[(name, o)] = r32(base + o)
    return seed


def read_watch(path, seeds=()):
    """Per vblank, the full watched words (name, offset) -> value as of the end of that frame.
    seeds: (vblank, words) from RAM dumps, which replace the state after that vblank."""
    cur, frames = {}, {}
    names = {m[2] for m in map(WATCH_AT.match, open(path, errors="replace")) if m}
    seeds = sorted(((v, {k: x for k, x in sd.items() if k[0] in names}) for v, sd in seeds),
                   key=lambda s: s[0])
    for line in open(path, errors="replace"):
        m = WATCH.match(line)
        if m:
            f, name, off, new = int(m[1]), m[2], int(m[3], 16), int(m[5], 16)
            while seeds and f > seeds[0][0]:
                cur.update(seeds.pop(0)[1])
            if (name, off) not in cur:
                cur[(name, off)] = int(m[4], 16)
            cur[(name, off)] = new
            frames[f] = dict(cur)
    return frames


def read_trace(path):
    out = []
    for line in open(path):
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            break
        sol = [e for e in d["ents"] if e[1] == 1]
        if not sol or "player" not in d:
            continue
        out.append(dict(tick=d["tick"], vblank=d["vblank"], pos=sol[0][3:6], yaw=d["player"][0],
                        pitch=d["player"][1], flags_b=d["player"][6], health=sol[0][9]))
    return out


def ticks(trace_path, watch_path, lo, hi, seeds=()):
    """The ticks with a full state: trace line + watched fields at the end of the tick."""
    tr = read_trace(trace_path)
    w = read_watch(watch_path, seeds)
    per_vbl = Counter(t["vblank"] for t in tr)
    keys = sorted(w)
    out = []
    import bisect
    for n, t in enumerate(tr):
        if not (lo <= t["vblank"] <= hi):
            continue
        # The watch logs at the drawing pass, after all the ticks of a frame: its state is this
        # tick's only if the frame is drawn before the next tick runs.
        i = bisect.bisect_left(keys, t["vblank"])
        if i >= len(keys):
            continue
        nxt = tr[n + 1]["vblank"] if n + 1 < len(tr) else 1 << 40
        if keys[i] >= nxt:
            t["single"] = False
        st = w[keys[i]]
        g = lambda o: s32(st.get(("s", o - 0x1F0), 0))
        t.update(fwd=g(0x200), strafe=g(0x218), vy=g(0x21C), ground=g(0x284), floor=g(0x208),
                 eye=s16(g(0x2D4) >> 16), radius=s16(g(0x2EC)), look_flags=s32(st.get(("l", 0), 0)),
                 single=t.get("single", True) and per_vbl[t["vblank"]] == 1, local_fwd=g(0x220),
                 end_push=[g(0x1F0), g(0x1F4), g(0x1F8)],
                 flinch=st.get(("l", 0x154), 0) & 0xFFFF,
                 knock=[s32(st.get(("l", 0x80 + 4 * i), 0)) for i in range(3)],
                 pin=[s32(st.get(("l", 0x90 + 4 * i), 0)) for i in range(3)],
                 lean=s32(st.get(("l", 0x9C), 0)),
                 ladder=s32(st.get(("l", 0x74), 0)),
                 old=[s32(st[("o", 4 * i)]) for i in range(3)] if ("o", 0) in st else None,
                 parent=st.get(("e", 0x2C), 0),
                 slots=[tuple(s16(st.get(("k", 0x28 * i + o), 0) >> sh) for o, sh in ((0, 0), (0, 16), (4, 0), (4, 16)))
                        for i in range(4)] if ("k", 0) in st else None)
        out.append(t)
    return out


def check(args):
    trace, watch, level = args[0], args[1], args[2]
    lo = int(args[args.index("--from") + 1]) if "--from" in args else 0
    hi = int(args[args.index("--to") + 1]) if "--to" in args else 1 << 40
    free = "--free" in args
    verbose = "-v" in args
    world = World(level)
    slots = None
    if "--slots" in args:  # chunk numbers in slot order, when the watch has no slot table
        m, l = level.split(":")
        names = [f"{m}_{l}_C{n}.TSP" for n in args[args.index("--slots") + 1].split(",")]
        slots = [next(c for c in world.files.values() if c.name == nm) for nm in names]
    # --ram DUMP@VBLANK (repeatable): the watched words' values at that vblank (a watch logs
    # only changes; fields that never change, like the radius, need a dump of each level).
    seeds = []
    for i, a in enumerate(args):
        if a == "--ram":
            path, _, vbl = args[i + 1].partition("@")
            seeds.append((int(vbl or re.search(r"(\d+)\.bin$", path)[1]), ram_seed(path)))
    T = ticks(trace, watch, lo, hi, seeds)
    stats = defaultdict(Counter)
    worst = defaultdict(int)
    e = None
    shown = 0
    last_strafe = last_fwd = 0
    for k in range(1, len(T)):
        p, t = T[k - 1], T[k]
        if t["tick"] != p["tick"] + 1 or not (t["single"] and p["single"]):
            e = None
            continue
        if t["slots"]:
            world.set_slots(t["slots"])
        elif slots:
            world.slots = slots
        else:
            world.all_chunks()
        if e is None or not free:
            e = dict(pos=list(p["pos"]), vy=p["vy"], ground=p["ground"], floor=p["floor"], old=list(p["pos"]))
        # +0x3BC, the position entity_move ended with; a touch handler (objects.md, Blocking)
        # can move the player after that.
        if not free:
            e["old"] = list(p["old"]) if p["old"] else list(e["pos"])
        moved_after = p["old"] is not None and p["old"] != p["pos"]
        if free and moved_after:
            # An object's touch handler moved the soldier after the move (not simulated): take
            # the position it left, keep the rest of the simulated state.
            e["pos"] = list(p["pos"])
        # The inputs of tick t as the control function produced them.
        e.update(fwd=t["fwd"], strafe=t["strafe"], yaw=t["yaw"], eye=t["eye"], radius=t["radius"],
                 flags_b=t["flags_b"])
        e.pop("landed", None)
        e.pop("strafe_cleared", None)
        jump = p["ground"] and t["vy"] == -0x180000
        if jump:
            e["ground"] = 0
            e["vy"] = -0x1A0000
        e["gravity"] = True
        if t["ladder"]:
            # On a ladder (inv+0x70 set by the ladder's think this tick, latched in inv+0x74):
            # ladder_climb set the vertical speed (half the forward role up, down to the floor at
            # most) and turned gravity off; entity_move leaves it as it is, so the watch holds it.
            e["gravity"] = False
            e["vy"] = t["vy"]
            if t["vy"] < 0:
                e["ground"] = 0
        elif p["ladder"]:
            e["vy"] = 0  # the latch: leaving a ladder zeroes the vertical speed
        aiming = bool(t["look_flags"] & 0x20)
        push = [0, 0, 0]
        if aiming:
            # player_controls in aim mode: pinned at inv+0x90 (y set, x/z by the push), no
            # forward speed, the strafe is the lean inv+0x9C, no gravity, no floor check.
            push = [s32(t["pin"][0] - e["pos"][0]), 0, s32(t["pin"][2] - e["pos"][2])]
            e["pos"][1] = t["pin"][1]
            e["fwd"] = 0
            e["strafe"] = t["lean"]
        elif p["look_flags"] & 0x20:
            e["pos"] = list(t["pin"])  # leaving aim mode: back to the pinned spot
        if p["flinch"] and not (t["look_flags"] & 2):
            # The hit flinch (player_update): the knockback inv+0x80, less a quarter of the
            # velocity's local components taken as world ones (+0x220 still last tick's).
            vel = (e["strafe"], e["vy"], p["local_fwd"])
            push = [s32(push[i] + p["knock"][i] - (vel[i] >> 2)) for i in range(3)]
        # A push left at the end of the tick before (entity_move clears it, so it came after the
        # move: the player's touch handler against a soldier or dog, 0x8007C030) moves this tick.
        push = [s32(push[i] + p["end_push"][i]) for i in range(3)]
        e["push"] = push
        tried = []
        # +0x218 reads 0 after a wall contact or a landing (both clear it): try the last strafe.
        # +0x218 reads 0 at the end of a tick that gathered walls or landed (entity_move and
        # player_world_contact clear it): then try the strafe of the tick before and the values a
        # keyboard or full stick gives (34.863, or 8.716 crouched).
        keys = (0, 0x22DD00, -0x22DD00, 0x8B740, -0x8B740)
        cands = [(e["fwd"], e["strafe"])]
        if not aiming and t["strafe"] == 0:
            cands += [(e["fwd"], v) for v in (last_strafe,) + keys]
        if not aiming:
            # The player's touch handler against a soldier (0x8007C030) clears +0x200 too, even
            # when its push rounds to 0.
            cands += [(f, v) for f in (last_fwd,) + keys for v in (last_strafe,) + keys]
        best = None
        for fv, sv in cands:
            trial = json.loads(json.dumps(e))
            trial["fwd"], trial["strafe"] = fv, sv
            lg = []
            entity_move(world, trial, lg)
            # Compared with +0x3BC, where entity_move left the soldier (an object's touch handler
            # can move him after that: objects.md, Blocking), else with the traced position.
            target = t["old"] or t["pos"]
            err = max(abs(trial["pos"][i] - target[i]) for i in range(3))
            tried.append((err, (fv, sv), trial, lg))
            if err == 0:
                break
        err, (fv, sv), trial, lg = min(tried, key=lambda x: x[0])
        if (fv, sv) != (e["fwd"], e["strafe"]):
            stats["(input inferred)"]["ticks"] += 1
        e = trial
        last_fwd, last_strafe = fv, sv
        if err == 0:
            for x in set(e.get("events", [])) | ({"wall push"} if lg else set()):
                stats["(exact: " + x + ")"]["ticks"] += 1
        kind = classify(p, t, e, jump, lg, aiming)
        if moved_after:
            kind = "touched:" + kind
        if p["parent"] or t["parent"]:
            kind = "riding"
        stats[kind]["ticks"] += 1
        if err == 0:
            stats[kind]["exact"] += 1
        else:
            worst[kind] = max(worst[kind], err)
            for lim in (1 << 12, 1 << 16, 8 << 16):
                if err <= lim:
                    stats[kind][f"<={lim / 65536:g}"] += 1
                    break
            else:
                stats[kind]["fail"] += 1
            if verbose and shown < 60:
                shown += 1
                print(f"vbl {t['vblank']} tick {t['tick']} [{kind}] sim {[x / 65536 for x in e['pos']]} "
                      f"game {[x / 65536 for x in (t['old'] or t['pos'])]} dpos {[(e['pos'][i] - (t['old'] or t['pos'])[i]) / 65536 for i in range(3)]} "
                      f"ground sim {e['ground']} game {t['ground']} vy sim {e['vy'] / 65536} game {t['vy'] / 65536} "
                      f"fwd {fv / 65536:.3f} str {sv / 65536:.3f} eye {t['eye']} floor {e.get('floor_found')} {lg}")
    print(f"{'behaviour':<14} {'ticks':>6} {'exact':>6} {'<=1/16':>7} {'<=1':>5} {'<=8':>5} {'fail':>5}  worst")
    for kind in sorted(stats):
        s = stats[kind]
        print(f"{kind:<14} {s['ticks']:>6} {s['exact']:>6} {s['<=0.0625']:>7} {s['<=1']:>5} {s['<=8']:>5} "
              f"{s['fail']:>5}  {worst[kind] / 65536:.4f}")


def classify(p, t, e, jump, lg, aiming):
    if p["health"] <= 0:
        return "dead"
    if aiming:
        return "aim"
    if p["flinch"]:
        return "flinch"
    if t["ladder"] or p["ladder"]:
        return "ladder"
    if jump:
        return "jump"
    if lg:
        return "wall"
    if not p["ground"] or not t["ground"]:
        return "air"
    if t["eye"] < 200:
        return "crouch" if t["eye"] == 80 else "eye188"
    dy = t["pos"][1] - p["pos"][1]
    if dy:
        return "step/slope"
    if t["fwd"] or t["strafe"]:
        return "run"
    return "still"


def floor_cmd(args):
    world = World(args[0])
    world.all_chunks()
    x, y, z = (int(float(v) * 65536) for v in args[1:4])
    print(floor_find(world, (x, y, z)))


def main():
    if not sys.argv[1:]:
        print(__doc__)
        return
    cmd, args = sys.argv[1], sys.argv[2:]
    {"check": check, "floor": floor_cmd, "watch": lambda _: print(WATCH_LIST)}[cmd](args)


if __name__ == "__main__":
    main()
