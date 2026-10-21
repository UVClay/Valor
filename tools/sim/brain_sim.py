#!/usr/bin/env python3
"""A reference reimplementation of the enemies' movement (docs/re/enemies.md "The brain",
animation.md "Root movement" / "Root turn", entities.md "Moving", levels.md Collision), stepped
one tick at a time against the original.

  brain_sim.py watch WATCH.log TRACE.jsonl DUMP[@VBLANK]... [--from V] [--to V] [--level M:L] [-v N]
                    [--ent SLOT]
  brain_sim.py watchlist [BRAIN_ADDR...]   the VALOR_WATCH list (no address: the brain finder)
  brain_sim.py pairs D_T.bin D_T1.bin ...  (or --dir build/vm: every dV.bin with a dV+1.bin)

A dump's vblank is the number ending its name unless given after @. -v N prints the first N
ticks that differ. The summary gives per goal combination the position's agreement (exact,
within 1/16, 1, 8 units, worse) and the heading's, then the fields compared, then per feature
(seek turns, slow-down, path advances, walls, root movement, floor...) the ticks where it
occurred and how many of them were exact.

For every soldier (0x1402) and dog (0x1405) of the five character slots (0x800B7B08 + i*0x2814)
it takes the state at the end of tick T and predicts the end of tick T+1:

  1. the think: the heading the scripts leave (variable 0x13, the dog's 0x0E, read at T+1: the
     think copies the heading in and out of it, the brain doesn't touch it), the animation frame
     at T+1 and, when the frame changed, its root movement (anim_root_motion) through the
     rotation matrix of T and the speed scale of T, into the push +0x1F0;
  2. entity_move: angular velocity, the frame's root turn, the local velocity, the push, the
     walls (walker_slide, with the brain's wall sorting while the walk goal is on), the floor;
  3. brain_update with the goals and parameters the scripts set (read at T+1), the turn of
     brain_seek, paths, points, nodes, flee, brain_steer; then the rotation matrix +0x340.

Script decisions are inputs (taken from T+1); the brain, the root motion and the physics are what
is checked: the position (16.16, against +0x3BC: where entity_move left it), the heading after the brain, pitch and roll, the speed scale,
the walker radius, the brain's state bits, wall slots, path point, side-waypoint flag, the
distance it writes into a script variable, the contact pulses 0x400000/0x800000/0x1000000.

The watch backend needs a log of
  VALOR_WATCH="k=80090DE8:A0;ws=8009817C:8;aI=E+0:40;vI=E+4C:A0;mI=E+1F0:1E0;nI=E+FBC+5C:48;hJ=BRAIN:124..."
(`watchlist` writes it; tools/sim/check.py finds the brains and runs it all)
(E the slot's entity, the instance at E+0xFBC, and the brains as fixed windows at the addresses
the game allocated them: a pointer watch re-bases silently, losing the brain's constant words;
the addresses are the "watch N bI at ADDR" lines of a first run with bI=[E+38C]+0:124; ws the
witness spot, a point goal's target),
VALOR_TRACE_STATE, and a RAM dump per level (seed and static data: animations, paths, nodes).
"""
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import move_sim as ms  # noqa: E402
from move_sim import NONE, s16, s32  # noqa: E402

SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]
SOLDIER, DOG = 0x1402, 0x1405
G_SOLDIER = 0x8009D654
G_WORLD = 0x8009D648
G_PATHS = 0x8009E290
G_TICK = 0x8009A5DC
RATAN_TBL = 0x8002D4F8   # ratan2's table (exe 0x8001536C reads 0x80030000 - 0x2B08)
CHUNK_SLOTS = 0x80090DE8


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ---------------------------------------------------------------- memory

class Mem:
    """RAM: a dump, with an overlay of words {address & 0x1FFFFC: u32} (a watch's state)."""

    def __init__(self, data, ov=None):
        self.d = data
        self.ov = ov or {}

    def u32(self, a):
        a &= 0x1FFFFC
        v = self.ov.get(a)
        if v is None:
            v = struct.unpack_from("<I", self.d, a)[0]
        return v

    def s32(self, a):
        return s32(self.u32(a))

    def u16(self, a):
        return (self.u32(a) >> (16 * ((a >> 1) & 1))) & 0xFFFF

    def s16(self, a):
        return s16(self.u16(a))

    def u8(self, a):
        return (self.u32(a) >> (8 * (a & 3))) & 0xFF

    def block(self, a, n):
        return bytearray(b"".join(struct.pack("<I", self.u32(a + o)) for o in range(0, n, 4)))


class Brain:
    """The brain's 0x124 bytes. Reads past them (a gait index out of range) go to RAM, writes
    there are kept aside."""

    def __init__(self, data, mem=None, addr=0):
        self.b = bytearray(data)
        self.mem, self.addr = mem, addr
        self.extra = {}

    def _r(self, o, n):
        if 0 <= o and o + n <= len(self.b):
            return bytes(self.b[o:o + n])
        return bytes((self.extra.get(o + k, self.mem.u8(self.addr + o + k)) if self.mem else 0)
                     for k in range(n))

    def _w(self, o, data):
        if 0 <= o and o + len(data) <= len(self.b):
            self.b[o:o + len(data)] = data
        else:
            for k, x in enumerate(data):
                self.extra[o + k] = x

    def s16(self, o):
        return struct.unpack("<h", self._r(o, 2))[0]

    def u16(self, o):
        return struct.unpack("<H", self._r(o, 2))[0]

    def s32(self, o):
        return struct.unpack("<i", self._r(o, 4))[0]

    def u32(self, o):
        return struct.unpack("<I", self._r(o, 4))[0]

    def w16(self, o, v):
        self._w(o, struct.pack("<H", v & 0xFFFF))

    def w32(self, o, v):
        self._w(o, struct.pack("<I", v & 0xFFFFFFFF))

    def copy(self):
        c = Brain(self.b, self.mem, self.addr)
        c.extra = dict(self.extra)
        return c

    goals = property(lambda self: self.s32(4) & 0xFFFFFFFF)
    state = property(lambda self: self.s32(8) & 0xFFFFFFFF, lambda self, v: self.w32(8, v))


# ---------------------------------------------------------------- PsyQ maths

def rot_yxz(ax, ay, az):
    """RotMatrixYXZ (exe 0x8001AF98) as nine s16, row major."""
    sx, cx = ms.sincos(ax)
    sy, cy = ms.sincos(ay)
    sz, cz = ms.sincos(az)
    m = [0] * 9
    m[5] = -sx
    m[2] = (sy * cx) >> 12
    m[8] = (cy * cx) >> 12
    m[3] = (sz * cx) >> 12
    m[4] = (cz * cx) >> 12
    t = (sy * sx) >> 12
    m[0] = ((cy * cz) >> 12) + ((t * sz) >> 12)
    m[1] = -((cy * sz) >> 12) + ((t * cz) >> 12)
    t = (cy * sx) >> 12
    m[7] = ((sy * sz) >> 12) + ((t * cz) >> 12)
    m[6] = -((sy * cz) >> 12) + ((t * sz) >> 12)
    return [s16(v) for v in m]


def apply_rot(m, v):
    """mvmva sf=1 of the rotation matrix and a vector, as IR (saturated)."""
    out = []
    for i in range(3):
        x = (m[3 * i] * v[0] + m[3 * i + 1] * v[1] + m[3 * i + 2] * v[2]) >> 12
        out.append(max(-0x8000, min(0x7FFF, x)))
    return out


class Ratan:
    def __init__(self, mem):
        self.t = [mem.s16(RATAN_TBL + 2 * i) for i in range(1025)]

    def __call__(self, y, x):
        """ratan2 (exe 0x8001536C)."""
        nx = x < 0
        ny = y < 0
        x, y = abs(x), abs(y)
        if x == 0 and y == 0:
            return 0
        if y < x:
            v = self.t[cdiv(y, x >> 10) if y & 0x7FE00000 else cdiv(y << 10, x)]
        else:
            v = 1024 - self.t[cdiv(x, y >> 10) if x & 0x7FE00000 else cdiv(x << 10, y)]
        if nx:
            v = 2048 - v
        if ny:
            v = -v
        return v


def dist2(dx, dz):
    """The brain's cheap distance of two s16 components: max + min / 4."""
    a, b = abs(s16(dx)), abs(s16(dz))
    a, b = s16(a), s16(b)
    lo = 0
    if a < 0:  # |-32768|
        a, lo = 0, a
    if a < b:
        a, b = b, a
    return a + ((lo + b) >> 2)


# ---------------------------------------------------------------- animation

def frame_data(mem, inst, anim, frame):
    """(root movement, root offset, root turn) of a frame (animation.md, frames), or None."""
    if anim < 0 or frame < 0 or not mem.u32(inst + 0x74):
        return None
    entry = mem.u32(mem.u32(inst + 0x78) + 4 * anim)
    if not entry:
        return None
    f = mem.u32(entry + 4) + 0x14 * frame
    w0, w1 = mem.u32(f), mem.u32(f + 4)

    def unpack(w):
        return (s32(w << 22) >> 22, s32(w << 11) >> 21, s32(w << 1) >> 22)

    return unpack(w0), unpack(w1), (mem.s16(f + 8), mem.s16(f + 0xA), mem.s16(f + 0xC))


# ---------------------------------------------------------------- an entity's state

def read_ent(mem, E):
    g = lambda o: mem.s32(E + o)
    st = dict(
        addr=E, type=mem.u32(E + 8), handle=mem.u32(E), flags_a=mem.u32(E + 0xC),
        flags_b=mem.u32(E + 0x10), mask=mem.u32(E + 0x14), attach=mem.u32(E + 0x18),
        contact=mem.u32(E + 0x38), push=[g(0x1F0 + 4 * k) for k in range(3)], fwd_acc=g(0x1FC),
        fwd=g(0x200), fwd_max=g(0x204), floor=g(0x208), pos=[g(0x20C + 4 * k) for k in range(3)],
        vel=[g(0x218 + 4 * k) for k in range(3)], speed=g(0x224),
        ang=[g(0x228 + 4 * k) for k in range(3)], angvel=[g(0x234 + 4 * k) for k in range(3)],
        prev=[g(0x258 + 4 * k) for k in range(3)], root=[mem.s16(E + 0x27C + 2 * k) for k in range(3)],
        ground=g(0x284), eye=g(0x2D4), sphere=mem.u16(E + 0x2E8), probe=mem.s16(E + 0x2EC),
        rot=[mem.s16(E + 0x340 + 2 * k) for k in range(9)], old=[g(0x3BC + 4 * k) for k in range(3)],
        r3c8=mem.s16(E + 0x3C8), brain_ptr=mem.u32(E + 0x38C),
        inst=E + 0xFBC if E in SLOTS else mem.u32(E + 0x100),  # a slot's instance is in its block
        vars=[g(0x4C + 4 * k) for k in range(0x28)])
    inst = st["inst"]
    st["anim"] = mem.s16(inst + 0x5C) if inst else -1
    st["frame"] = mem.s16(inst + 0x5E) if inst else -1
    st["brain"] = Brain(mem.block(st["brain_ptr"], 0x124), mem, st["brain_ptr"]) if st["brain_ptr"] else None
    return st


def copy_st(st):
    out = {k: (list(v) if isinstance(v, list) else v) for k, v in st.items()}
    if st.get("brain"):
        out["brain"] = st["brain"].copy()
    return out


# ---------------------------------------------------------------- walls (walker_slide 0x80078F60)

def gather_leaf(ch, faces, c, r, h, delta, out, base, maxn, use_delta):
    """walls_gather_leaf (0x8007484C); use_delta 0 (the brain's walk gather) keeps the walls
    whatever the direction of the move."""
    added = 0
    for fi in faces:
        f = ch.faces[fi]
        n = ch.norms[f[3]]
        if not ms.tri_near(ch, f, c, r, h, 1):
            continue
        if use_delta and not ms.plane_dot(n, delta) < 0:
            continue
        if not ms.plane_crosses(c, r, h, n, f[4]):
            continue
        if any(e[2] == fi and e[3] == ch.name for e in out[base:]):
            continue
        out.append((n, f[4], fi, ch.name, ch, f))
        added += 1
        if len(out) - base >= maxn:
            return added
    return added


def gather_chunk(ch, c, r, h, delta, out, maxn, use_delta):
    """walls_gather_chunk (0x800787C0), move_sim's traversal with the leaf above."""
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
        k = gather_leaf(ch, ch.idx[b:b + ~a], c, r, h, delta, out, base, maxn, use_delta)
        if k > 0:
            total += k
            if total >= maxn:
                return total
        if not stack:
            return total
        node, xmin, xmax, zmin, zmax = stack.pop()


def walls_gather(world, c, r, h, delta, use_delta, maxn=32):
    out = []
    total = 0
    for ch in world.loaded():
        if ms.chunk_near(ch, c[0], c[2], r):
            total += gather_chunk(ch, c, r, h, delta, out, maxn, use_delta)
        if total >= maxn:
            break
    return out


def walker_slide(world, st, delta, log):
    """Returns (walls of the final gather, result: 0 none, 1 still in a wall, 2 free)."""
    r = st["probe"]
    eye = s16(st["eye"] >> 16)
    fp = eye - 16
    pos, old = st["pos"], st["old"]
    c = (s16(pos[0] >> 16), s16((pos[1] >> 16) - fp), s16(pos[2] >> 16))
    b = st["brain"]
    walk = False
    if b is not None:
        b.state = b.state & ~0x1C
        walk = bool(b.goals & 4)
    if walk:
        g = b.s16(0xC0)
        gait = b.s16(0xCC + 2 * g)
        rot = st["rot"]
        fwd = (s16(-rot[2]), 0, s16(-rot[8])) if b.s32(0xD8) else (rot[2], 0, rot[8])
        b.w16(0xCC + 2 * g, b.u16(0xD0 + 2 * g))
        planes = walls_gather(world, c, max(gait, r), fp - 4, delta, False)
        log["gather_r"] = max(gait, r)
    else:
        planes = walls_gather(world, c, r, eye - 20, delta, True)
    if not planes:
        return 0, 0
    count = len(planes)
    if walk:
        best = [0x7FFFFFFF] * 3
        keep = []
        for p in planes:
            n, d = p[0], p[1]
            if abs(n[1]) < 0x5A80 and (s32(fwd[0] * n[0] + fwd[2] * n[2]) >> 12) < 0x2EE0:
                cross = s32(n[0] * fwd[2] - n[2] * fwd[0]) >> 12
                m = s32(c[0] * n[0] + c[1] * n[1] + c[2] * n[2]) + d
                a0 = s32(m if m >= 0 else -m)
                slot = None
                if abs(cross) < 0x5001:
                    slot, off, bit = 0, 0xF4, 4
                elif cross >= 0x5001:
                    slot, off, bit = 1, 0x104, 8
                elif cross < -0x5000:
                    slot, off, bit = 2, 0x114, 0x10
                if slot is not None and a0 < best[slot]:
                    best[slot] = a0
                    b.w16(0xEC, fwd[0])
                    b.w16(0xEE, 0)
                    b.w16(0xF0, fwd[2])
                    for k in range(3):
                        b.w16(off + 2 * k, n[k])
                    b.w16(off + 6, d)
                    b.w32(off + 8, a0)
                    b.w32(off + 12, cross)
                    b.state = b.state | bit
                    log.setdefault("walls", []).append((bit, p[2], a0, cross))
            ok = (ms.tri_near(p[4], p[5], c, r, fp - 4, 1) and ms.plane_dot(n, delta) < 0
                  and ms.plane_crosses(c, r, fp - 4, n, d))
            if ok:
                keep.append(p)
        planes = keep
    left, pushed = count, False
    while True:
        i = ms.first_hit([p[:4] for p in planes], old, pos, r, fp) if planes else -1
        if i < 0:
            break
        pushed = True
        left -= 1
        log.setdefault("push", []).append(planes[i][2])
        ms.wall_push(planes[i][0], planes[i][1], pos, fp, r + 4)
        if left < 0:
            break
    if not pushed:
        return 0, 2
    delta2 = tuple(s16((pos[k] >> 16) - (old[k] >> 16)) for k in range(3))
    c2 = (s16(pos[0] >> 16), s16(pos[1] >> 16), s16(pos[2] >> 16))
    planes2 = walls_gather(world, c2, r, fp - 4, delta2, True)
    if not planes2:
        return 0, 2
    if ms.first_hit([p[:4] for p in planes2], old, pos, r, fp) < 0:
        return len(planes2), 2
    return len(planes2), 1


# ---------------------------------------------------------------- entity_move (0x8006C83C)

def entity_move(world, st, turn, log):
    """An enemy's entity_move. turn: the current frame's root turn (or None)."""
    st["prev"] = list(st["pos"])
    on_ground0 = st["ground"]
    ang = st["ang"]
    for k in range(3):
        ang[k] = s32(ang[k] + st["angvel"][k])
    if turn is not None:
        for k in range(3):
            ang[k] = s32(ang[k] + (turn[k] << 16)) & 0x0FFF0000
    if st["fwd_acc"]:
        st["fwd"] = min(s32(st["fwd"] + st["fwd_acc"]), st["fwd_max"])
    vel = st["vel"]
    vel[2] = st["fwd"]
    if not st["ground"] and st["flags_a"] & 0x20:
        vel[1] = min(s32(vel[1] + 0x20000), 0x4B0000)
    sp = st["speed"]
    if sp == 0x10000:
        v = [s16((vel[k] & 0xFFFFFFFF) >> 12) for k in range(3)]
    else:
        v = [s16((s16(vel[k] >> 16) * sp) >> 12) for k in range(3)]
    m = rot_yxz(s16(ang[0] >> 16), s16(ang[1] >> 16), s16(ang[2] >> 16))
    mv = [(m[3 * i] * v[0] + m[3 * i + 1] * v[1] + m[3 * i + 2] * v[2]) >> 12 for i in range(3)]
    pos = st["pos"]
    p0 = list(pos)
    for k in range(3):
        pos[k] = s32(pos[k] + (mv[k] << 12) + st["push"][k])
    dy4 = mv[1] >> 4
    old = st["old"]
    delta = tuple(s16((pos[k] >> 16) - (old[k] >> 16)) for k in range(3))
    walker = (st["flags_a"] & 8) and not (st["flags_a"] & 1) and not (st["flags_b"] & 0x45)
    if walker:
        n, res = walker_slide(world, st, delta, log)
        log["slide"] = (n, res)
        if res == 2:
            st["contact"] |= 0x8000
        if n > 0:
            if delta[1] >= 0x801 and dy4 < 0 and vel[1] > 0:
                vel[1] = 0
                pos[1] = old[1]
            vel[0] = vel[2] = 0
            st["contact"] |= 0x8000
            if st["flags_a"] & 0x80:
                pos[0] = s32(p0[0] + (((delta[0] << 5) + 0x8000) & 0xFFFF0000))
                pos[2] = s32(p0[2] + (((delta[2] << 5) + 0x8000) & 0xFFFF0000))
    elif st["flags_b"] & 0x44:
        log["unsupported"] = "world_sweep mover"
    if not (st["attach"] & 1) and not (st["flags_b"] & 0x10) and (st["ground"] or st["flags_a"] & 8):
        f, zone = ms.floor_find(world, pos)
        log["floor"] = f
        if f == NONE:
            if st["flags_a"] & 7 and not (st["flags_b"] & 0x10000):
                pos[:] = list(old)
                if not on_ground0:
                    pos[1] = st["floor"]
                st["ground"] = 1
                st["contact"] |= 0x80008000
                log["event"] = "no floor"
        else:
            st["floor"] = s32(f << 16)
            d = s16(pos[1] >> 16) - f
            if st["ground"]:
                if d >= -32:
                    pos[1] = s32(f << 16)
                elif st["flags_b"] & 0x200000 and d < -57:
                    vel[:] = [0, 0, 0]
                    st["fwd"] = 0
                    pos[:] = list(old)
                    st["ground"] = on_ground0
                    st["contact"] |= 0x80000000
                    log["event"] = "edge held"
                else:
                    st["ground"] = 0
                    st["contact"] |= 1
                    log["event"] = "off an edge"
            elif st["flags_a"] & 0x20:
                lim = dy4 + 16 if dy4 < 48 else 64
                if d >= 0 or lim >= abs(d):
                    if st["flags_a"] & 0x80 and abs(dy4) >= 8:
                        st["ground"] = 0
                        pos[1] = s32(f << 16)
                        vel[1] = -abs(vel[1])
                        log["event"] = "bounce"
                    else:
                        st["ground"] = 1
                        pos[1] = s32(f << 16)
                        st["push"][1] = 0
                        vel[1] = 0
                        st["contact"] &= ~1
                        log["event"] = "landing"
    st["push"] = [0, 0, 0]
    st["old"] = list(pos)


# ---------------------------------------------------------------- the brain (0x80040DF4)

class World2:
    """What the brain reads besides its entity: other entities, nodes, paths, variables."""

    def __init__(self, mem, targets, ratan):
        self.mem = mem            # RAM at T+1 for the static data
        self.targets = targets    # pointer -> (flags_a, x_int, z_int) as the brain sees them
        self.ratan = ratan


def set_var(st, var, value, log):
    log.setdefault("set_var", []).append((var, s32(value)))
    if 0 <= var < 0x28:
        st["vars"][var] = s32(value)


def log_tag(st, tag):
    if st.get("_log") is not None:
        st["_log"].setdefault("tags", set()).add(tag)


def brain_steer(st, w):
    b = st["brain"]
    s = b.state
    if (s & 0x18) == 0x18:
        s &= ~8 if b.s32(0x11C) < b.s32(0x10C) else ~0x10
    if s & 0x1C:
        log_tag(st, "brain_steer along a wall")
    if s & 4:
        st["angvel"][1] = 0
        t = w.ratan(b.s16(0xF4), b.s16(0xF8))
        if s & 0x10:
            if not s & 2:
                st["ang"][1] = s32((t + 0x400) << 16)
            b.state = b.state & ~1
        elif s & 8:
            if not s & 1:
                st["ang"][1] = s32((t - 0x400) << 16)
            b.state = b.state & ~2
        else:
            if b.s32(0x100) > 0:
                st["ang"][1] = s32((t - 0x400) << 16)
                b.state = b.state & ~2
            else:
                st["ang"][1] = s32((t + 0x400) << 16)
                b.state = b.state & ~1
        return 0
    if s & 8:
        if not s & 1:
            st["ang"][1] = s32((w.ratan(b.s16(0x104), b.s16(0x108)) - 0x400) << 16)
        b.state = b.state & ~2
        return 0
    if s & 0x10:
        if not s & 2:
            st["ang"][1] = s32((w.ratan(b.s16(0x114), b.s16(0x118)) + 0x400) << 16)
        b.state = b.state & ~1
        return 0
    return 1


def brain_seek(st, w, fwd, d, rate, dist, log):
    b = st["brain"]
    cross = s32(d[0] * fwd[2] - d[2] * fwd[0]) >> 12
    slow = b.s16(0xC2)
    g = b.s16(0xC0)
    if slow != 0:
        if not slow < dist:
            sp = s32(cdiv(s32(dist << 16), slow))
            st["speed"] = sp
            b.w32(0x18, sp)
            st["probe"] = s16((b.s16(0xC4) * sp) >> 16)
            st["r3c8"] = s16((b.s16(0xCA) * sp) >> 16)
            b.w16(0xCC + 2 * g, (b.s16(0xD0 + 2 * g) * sp) >> 16)
        else:
            st["speed"] = 0x10000
            b.w32(0x18, b.s32(0xDC))
            st["probe"] = b.s16(0xC4)
            st["sphere"] = b.u16(0xC6)
            b.w16(0xCC + 2 * g, b.u16(0xD0 + 2 * g))
    c = cross >> 2 if dist >= 2001 else cross >> 1
    s1 = rate
    if c < -0x20:
        b.state = b.state | 2
        s1 = (c & 0xFFFF) if c + s16(rate) >= 0 else -rate
    elif c > 0x20:
        b.state = b.state | 1
        s1 = (c & 0xFFFF) if c - s16(rate) <= 0 else rate
    else:
        dot = s32(d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2])
        if dot < 0:
            if c >= 0:
                b.state = b.state | 1
            else:
                s1 = -rate
                b.state = b.state | 2
        else:
            s1 = 0
    log.setdefault("seek", []).append((c, dist, s1))
    tags = log.setdefault("tags", set())
    if -0x20 <= c <= 0x20 and s1:
        tags.add("seek: turn round (behind)")
    elif s1 and (s1 & 0xFFFF) not in (rate & 0xFFFF, -rate & 0xFFFF):
        tags.add("seek: turn by the cross")
    elif s1:
        tags.add("seek: turn at full rate")
    if slow and not slow < dist:
        tags.add("seek: arrive slow-down")
    if b.goals & 4:
        brain_steer(st, w)
    if b.state & 3:
        h = s32(st["ang"][1] + (s1 << 16))
        while h < -0x8000000:
            h += 0x10000000
        while h > 0x8000000:
            h -= 0x10000000
        st["ang"][1] = h


def brain_move_to(st, w, flee, log):
    b = st["brain"]
    t = w.targets.get(b.u32(0x10))
    if b.u32(0x10) == 0 or (t is not None and t[0] & 0x180000):
        st["contact"] |= 0x10
        return
    if t is None:
        log["unsupported"] = "target %08X" % b.u32(0x10)
        return
    rot = st["rot"]
    fwd = (s16(-rot[2]), 0, s16(-rot[8])) if flee else (rot[2], 0, rot[8])
    dx = s16(t[1] - s16(st["pos"][0] >> 16))
    dz = s16(t[2] - s16(st["pos"][2] >> 16))
    brain_seek(st, w, fwd, (dx, 0, dz), b.s16(0xD4), dist2(dx, dz), log)


def brain_goto_point(st, w, log):
    b = st["brain"]
    p = b.u32(0x40)
    dx = s16(w.mem.u16(p) - s16(st["pos"][0] >> 16))
    dz = s16(w.mem.u16(p + 4) - s16(st["pos"][2] >> 16))
    dist = dist2(dx, dz)
    log["point"] = (hex(p), w.mem.s16(p), w.mem.s16(p + 4))
    var = b.s32(0x80)
    set_var(st, var, dist, log)
    if dist < b.s16(0xA8):
        set_var(st, var, 0, log)
        st["contact"] |= 0x400000
        log.setdefault("tags", set()).add("node/point: arrived")
    rot = st["rot"]
    brain_seek(st, w, (rot[2], 0, rot[8]), (dx, 0, dz), 0x100, dist, log)


def brain_goto_node(st, w, log):
    b = st["brain"]
    node = b.s32(0x3C)
    nodes = w.mem.u32(w.mem.u32(w.mem.u32(G_WORLD) + 0x388) + 8)
    nd = nodes + node * 0x34
    via = b.s32(0x8C)
    tx, tz = (b.u16(0xAC), b.u16(0xAE)) if via else (b.u16(0xA0), b.u16(0xA4))
    dx = s16(tx - s16(st["pos"][0] >> 16))
    dz = s16(tz - s16(st["pos"][2] >> 16))
    dist = dist2(dx, dz)
    var = b.s32(0x7C)
    set_var(st, var, dist, log)
    radius = b.s16(0xA8)
    if via:
        dot = s32(w.mem.s16(nd + 0x24) * -dx + w.mem.s16(nd + 0x28) * -dz)
        if dist < radius or dot < 0:
            b.w32(0x8C, 0)
            log["via_off"] = True
            log.setdefault("tags", set()).add("node: side waypoint passed")
    elif dist < radius:
        set_var(st, var, 0, log)
        st["contact"] |= 0x400000
        log.setdefault("tags", set()).add("node/point: arrived")
    rot = st["rot"]
    brain_seek(st, w, (rot[2], 0, rot[8]), (dx, 0, dz), 0x100, dist, log)


def brain_follow_path(st, w, log):
    b = st["brain"]
    mem = w.mem
    P = mem.u32(G_PATHS) + 8 * b.s16(0xB8)
    pts = mem.u32(P + 4)
    count = mem.s16(P + 2)
    pos = st["pos"]
    x, z = s16(pos[0] >> 16), s16(pos[2] >> 16)
    moved = dist2(x - s16(st["prev"][0] >> 16), z - s16(st["prev"][2] >> 16))
    last = count - 1
    for _ in range(4 * count + 8):
        i = b.s16(0xBC)
        pt = pts + i * 0x14
        dx = s16(mem.u16(pt) - x)
        dz = s16(mem.u16(pt + 4) - z)
        dist = dist2(dx, dz)
        j = i - b.s16(0xBE)
        if j < 0:
            j = 1
        elif j >= count:
            j = count - 2
        pj = pts + j * 0x14
        px = s16(mem.u16(pt) - mem.u16(pj))
        pz = s16(mem.u16(pt + 4) - mem.u16(pj + 4))
        dot = s32(dx * px + dz * pz)
        if moved < dist and dist >= 0x4C and dot > 0:
            rot = st["rot"]
            brain_seek(st, w, (rot[2], 0, rot[8]), (dx, 0, dz), 0xC0, dist, log)
            return
        mode = b.s16(0xBA)
        log.setdefault("advance", []).append(i)
        log.setdefault("tags", set()).add(f"path: advance (mode {mode})")
        if mode == 0:
            if i >= last:
                st["contact"] |= 0x1000000
                b.w16(0xBC, 0)
            else:
                st["contact"] |= 0x400000
                b.w16(0xBC, i + 1)
        elif mode == 1:
            if i <= 0:
                st["contact"] |= 0x800000
                b.w16(0xBC, count - 1)
            else:
                st["contact"] |= 0x400000
                b.w16(0xBC, i - 1)
        elif mode == 2:
            if i <= 0:
                st["contact"] |= 0x800000
                b.w16(0xBC, 1)
                b.w16(0xBE, 1)
            elif i >= last:
                st["contact"] |= 0x1000000
                b.w16(0xBC, count - 2)
                b.w16(0xBE, -1)
            else:
                st["contact"] |= 0x400000
                b.w16(0xBC, i + b.s16(0xBE))
    log["unsupported"] = "path loop hangs"


def brain_update(st, w, log):
    b = st["brain"]
    for k in range(3):
        b.w32(0x94 + 4 * k, st["pos"][k])
    goals = b.goals
    b.state = b.state & ~3
    if not goals & 4:
        b.state = b.state & ~0x1C
    if goals & 1:
        brain_move_to(st, w, 0, log)
    if goals & 0x4000:
        brain_move_to(st, w, 1, log)
    if goals & 0x1000:
        brain_goto_point(st, w, log)
    if goals & 0x800:
        brain_goto_node(st, w, log)
    if goals & 4 and not goals & 0x5C03:
        brain_steer(st, w)
    if goals & 2:
        brain_follow_path(st, w, log)
    b.state = b.state & ~0x20


# ---------------------------------------------------------------- one tick

# Brain words the scripts set (op_behaviour, brain_node_target), taken from T+1; the rest (state,
# walk speed +0x18, path point and step, side-waypoint flag, gait speeds, the wall slots) carry
# over from T and are computed.
SCRIPT_WORDS = (0x00, 0x04, 0x10, 0x3C, 0x40, 0x50, 0x7C, 0x80, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0,
                0xB4, 0xB8, 0xC0, 0xC4, 0xC8, 0xD0, 0xD4, 0xD8, 0xDC)


def merge_brain(bT, bT1, reissued):
    b = bT.copy()
    for o in SCRIPT_WORDS:
        b.b[o:o + 4] = bT1.b[o:o + 4]
    for o in reissued:
        if isinstance(o, int):
            b.b[o:o + 4] = bT1.b[o:o + 4]
    return b


def path_nearest_point(mem, st, path):
    """path_nearest_point (0x80057C8C): the point nearest the entity (3D cheap distance)."""
    P = mem.u32(G_PATHS) + 8 * path
    pts, n = mem.u32(P + 4), mem.s16(P + 2)
    best, bi = 0x7FFF, -1
    for i in range(n):
        p = pts + 0x14 * i
        d = ms_dist3(s16(st["pos"][0] >> 16) - mem.u16(p), s16(st["pos"][1] >> 16) - mem.u16(p + 2),
                     s16(st["pos"][2] >> 16) - mem.u16(p + 4))
        if (d & 0xFFFF) < (best & 0xFFFF):
            best, bi = d, i
    return bi


def ms_dist3(dx, dy, dz):
    a = sorted((abs(s16(dx)), abs(s16(dy)), abs(s16(dz))))
    return a[2] + ((a[0] + a[1]) >> 2)


def reset_gait(st):
    """brain_reset_gait (0x8005ABA0), which every movement goal of op_behaviour runs."""
    b = st["brain"]
    st["probe"] = b.s16(0xC4)
    st["sphere"] = b.u16(0xC6)
    st["r3c8"] = b.s16(0xCA)
    b.w16(0xC0, 0)
    b.w16(0xC2, 0)
    st["speed"] = 0x10000
    b.w32(0xD8, 0)
    b.w16(0xCC, b.u16(0xD0))


def heading_var(st):
    return 0x13 if st["type"] == SOLDIER else 0x0E


def step(world, w, A, B, scale_reset=False, root_applied=None, reissue=()):
    """Predict B (the state at T+1) from A (T). Returns (predicted state, log)."""
    log = {}
    st = copy_st(A)
    st["_log"] = log
    st["brain"] = merge_brain(A["brain"], B["brain"], reissue)
    # The think: the heading the scripts leave, the frame, the root movement.
    if "path" in reissue:
        # The scripts gave the path goal again: op_behaviour starts at the nearest point.
        b = st["brain"]
        b.w16(0xBC, path_nearest_point(w.mem, st, b.s16(0xB8)))
        mode = b.s16(0xBA)
        if mode in (0, 2):
            b.w16(0xBE, 1)
        elif mode == 1:
            b.w16(0xBE, -1)
    st["ang"][1] = B["vars"][heading_var(A)]
    if B["angvel"] != A["angvel"] and any(B["angvel"]):
        st["angvel"] = list(B["angvel"])
    st["fwd_acc"], st["fwd_max"] = B["fwd_acc"], B["fwd_max"]
    if B["fwd"] != A["fwd"] and not B["fwd_acc"]:
        st["fwd"] = B["fwd"]
    st["anim"], st["frame"] = B["anim"], B["frame"]
    st["flags_a"], st["flags_b"] = B["flags_a"], B["flags_b"]
    st["contact"] &= ~PULSES   # one-tick pulses, cleared by the think
    if scale_reset:
        reset_gait(st)
    changed = (A["anim"], A["frame"]) != (B["anim"], B["frame"])
    apply = changed if root_applied is None else root_applied
    fd = frame_data(w.mem, A["inst"], B["anim"], B["frame"]) if A["inst"] else None
    log["root_applied"] = bool(apply and fd and st["flags_a"] & 0x1000)
    if log["root_applied"]:
        mv = list(fd[0])
        if mv[1] < 0:
            st["ground"] = 0
        sp = st["speed"]
        if sp != 0x10000:
            mv = [(v * sp) >> 16 for v in mv]
        st["root"] = [s16(v) for v in mv]
        r = apply_rot(st["rot"], st["root"])
        for k in range(3):
            st["push"][k] = s32(st["push"][k] + (r[k] << 16))
    # The move.
    entity_move(world, st, fd[2] if fd else None, log)
    # The brain, then the matrix.
    if st["brain"] is not None and not st["flags_a"] & 0x80000:
        brain_update(st, w, log)
    st["rot"] = rot_yxz(s16(st["ang"][0] >> 16), s16(st["ang"][1] >> 16), s16(st["ang"][2] >> 16))
    return st, log


# ---------------------------------------------------------------- comparing

PULSES = 0x400000 | 0x800000 | 0x1000000


def compare(st, B, log):
    """Per field: True when exact."""
    out = {}
    out["pos"] = max(abs(st["pos"][k] - B["old"][k]) for k in range(3))
    out["heading"] = st["ang"][1] == B["ang"][1]
    out["pitch/roll"] = st["ang"][0] == B["ang"][0] and st["ang"][2] == B["ang"][2]
    out["speed"] = (st["speed"], st["probe"], st["r3c8"]) == (B["speed"], B["probe"], B["r3c8"])
    out["matrix"] = st["rot"] == B["rot"]
    if log.get("root_applied"):
        out["root"] = st["root"] == B["root"]
    out["pulses"] = (st["contact"] & PULSES) == (B["contact"] & PULSES)
    b, bb = st["brain"], B["brain"]
    if b is not None and bb is not None and not B["flags_a"] & 0x80000:
        out["state"] = (b.state & 0x1F) == (bb.state & 0x1F)
        ws = []
        for bit, off in ((4, 0xF4), (8, 0x104), (0x10, 0x114)):
            if b.state & bit:
                ws.append(b.b[off:off + 16] == bb.b[off:off + 16])
        if ws:
            out["walls"] = all(ws)
        out["path"] = b.b[0xBC:0xC0] == bb.b[0xBC:0xC0]
        out["via"] = b.s32(0x8C) == bb.s32(0x8C)
        out["gait"] = b.b[0xCC:0xD0] == bb.b[0xCC:0xD0] and b.b[0x18:0x1C] == bb.b[0x18:0x1C]
        for var, val in log.get("set_var", [])[-1:]:
            if 0 <= var < 0x28:
                out["var"] = st["vars"][var] == B["vars"][var]
    return out


def classify(A, B, st, log):
    if A["flags_a"] & 0x80000 or B["flags_a"] & 0x80000:
        return "dead"
    b = st["brain"]
    g = b.goals if b else 0
    kinds = []
    for bit, name in ((1, "seek"), (0x4000, "flee"), (0x1000, "point"), (0x800, "node"), (2, "path"),
                      (4, "walk")):
        if g & bit:
            kinds.append(name)
    k = "+".join(kinds) or "no goal"
    if not A["ground"] or not st["ground"]:
        k += " air"
    if log.get("push") or log.get("walls"):
        k += " wall"
    return k


class Stats:
    def __init__(self):
        self.s = defaultdict(Counter)
        self.worst = defaultdict(int)
        self.fields = defaultdict(Counter)
        self.tags = defaultdict(Counter)

    def add(self, kind, res, inferred):
        s = self.s[kind]
        s["ticks"] += 1
        err = res["pos"]
        if err == 0:
            s["exact"] += 1
        elif err <= 1 << 12:
            s["<=1/16"] += 1
        elif err <= 1 << 16:
            s["<=1"] += 1
        elif err <= 8 << 16:
            s["<=8"] += 1
        else:
            s["fail"] += 1
        self.worst[kind] = max(self.worst[kind], err)
        s["heading"] += res["heading"]
        if inferred:
            s["inferred"] += 1
        for f, ok in res.items():
            if f == "pos":
                ok = ok == 0
            self.fields[f]["n"] += 1
            self.fields[f]["ok"] += bool(ok)

    def tag(self, t, res):
        c = self.tags[t]
        c["ticks"] += 1
        c["pos"] += res["pos"] == 0
        c["near"] += 0 < res["pos"] <= 1 << 16
        c["heading"] += bool(res["heading"])
        c["all"] += all_ok(res)

    def show(self):
        print(f"{'behaviour':<26} {'ticks':>6} {'exact':>6} {'<=1/16':>7} {'<=1':>5} {'<=8':>5} {'fail':>5} "
              f"{'heading':>7} {'infer':>5}  worst")
        for k in sorted(self.s):
            s = self.s[k]
            print(f"{k:<26} {s['ticks']:>6} {s['exact']:>6} {s['<=1/16']:>7} {s['<=1']:>5} {s['<=8']:>5} "
                  f"{s['fail']:>5} {s['heading']:>7} {s['inferred']:>5}  {self.worst[k] / 65536:.4f}")
        print("fields exact:", ", ".join(f"{f} {c['ok']}/{c['n']}" for f, c in sorted(self.fields.items())))
        print(f"\n{'feature':<34} {'ticks':>6} {'pos':>6} {'<=1':>5} {'head':>6} {'all':>6}")
        for t in sorted(self.tags):
            c = self.tags[t]
            print(f"{t:<34} {c['ticks']:>6} {c['pos']:>6} {c['near']:>5} {c['heading']:>6} {c['all']:>6}")


def all_ok(res):
    return res["pos"] == 0 and all(v for k, v in res.items() if k != "pos")


def run_entity(world, w, A, B, stats, verbose, tag, shown, level="?"):
    """Step one entity, with the script-side candidates when the plain prediction misses."""
    st, log = step(world, w, A, B)
    res = compare(st, B, log)
    inferred = False
    if not all_ok(res):
        changed = (A["anim"], A["frame"]) != (B["anim"], B["frame"])
        reissue_opts = [(), (0x8C,), (0xBC,), (0x8C, 0xBC), (0x18,), (0x18, 0xCC), ("path",)]
        best = (sum(not v for k, v in res.items() if k != "pos") + (res["pos"] != 0), res, st, log)
        for sr in (False, True):
            for ra in (None, not changed):
                for ri in reissue_opts:
                    if (sr, ra, ri) == (False, None, ()):
                        continue
                    st2, log2 = step(world, w, A, B, sr, ra, ri)
                    r2 = compare(st2, B, log2)
                    sc = sum(not v for k, v in r2.items() if k != "pos") + (r2["pos"] != 0)
                    if sc < best[0]:
                        best = (sc, r2, st2, log2)
                        log2["cand"] = ("gait reset in the think " if sr else "") + \
                            ("root motion " + ("applied" if ra else "not applied") + " " if ra is not None else "") + \
                            ("goal given again " + str(ri) if ri else "")
        if best[1] is not res:
            inferred = True
            _, res, st, log = best
    kind = classify(A, B, st, log)
    stats.add(kind, res, inferred)
    tags = set(log.get("tags", ()))
    fd = frame_data(w.mem, A["inst"], B["anim"], B["frame"]) if A["inst"] else None
    if log.get("root_applied") and any(st["root"]):
        tags.add("move: root motion")
    if fd and any(fd[2]):
        tags.add("move: root turn")
    if any(st["angvel"]):
        tags.add("move: angular velocity")
    if log.get("push"):
        tags.add("walls: pushed out")
    if log.get("walls"):
        tags.add("walls: sorted for the brain")
    if log.get("event"):
        tags.add("floor: " + log["event"])
    if A["ground"] and st["ground"] and (A["pos"][1] >> 16) != (st["pos"][1] >> 16) and not log.get("event"):
        tags.add("floor: step or slope")
    if not A["ground"] and not st["ground"]:
        tags.add("air")
    if B["flags_a"] & 0x80000:
        tags = {"dead (brain off)"} | {t for t in tags if t.startswith(("floor", "air", "move"))}
    if log.get("unsupported"):
        tags.add("unsupported: " + log["unsupported"].split()[0])
    if inferred:
        tags.add("(script input inferred: " + log.get("cand", "").strip() + ")")
    for t in tags:
        stats.tag(t, res)
    alive = "dead" if B["flags_a"] & 0x80000 else "alive"
    stats.tag(f"[{level} {'dog' if A['type'] == DOG else 'soldier'} {alive}]", res)
    if verbose and not all_ok(res) and shown[0] < verbose:
        shown[0] += 1
        bad = [k for k, v in res.items() if (v if k == "pos" else not v)]
        print(f"{tag} {hex(A['type'])} [{kind}] bad {bad} dpos "
              f"{[(st['pos'][k] - B['old'][k]) / 65536 for k in range(3)]} head sim {st['ang'][1] / 65536:.3f} "
              f"game {B['ang'][1] / 65536:.3f} (think {B['vars'][heading_var(A)] / 65536:.3f}) "
              f"state sim {st['brain'].state & 0x1F if st['brain'] else '-'} game "
              f"{B['brain'].state & 0x1F if B['brain'] else '-'} anim {A['anim']}:{A['frame']}->{B['anim']}:{B['frame']} "
              f"root sim {st['root']} game {B['root']} pushT {[x/65536 for x in A['push']]} posT {[x/65536 for x in A['pos']]} posT1 {[x/65536 for x in B['pos']]} oldT1 {[x/65536 for x in B['old']]} groundT {A['ground']} {B['ground']} vel {A['vel']} {A['fwd']} -> {B['vel']} {B['fwd']} acc {B['fwd_acc']} "
              f"goals {st['brain'].goals if st['brain'] else 0:#x} gait sim {st['brain'].b[0xCC:0xD0].hex()} {st['brain'].b[0x18:0x1C].hex()} game {B['brain'].b[0xCC:0xD0].hex()} {B['brain'].b[0x18:0x1C].hex()} T {A['brain'].b[0xCC:0xD0].hex()} {A['brain'].b[0x18:0x1C].hex()} speed {st['speed']:#x} {B['speed']:#x} probe {A['probe']} {st['probe']} {B['probe']} r3c8 {A['r3c8']} {st['r3c8']} {B['r3c8']} sphere {A['sphere']} {B['sphere']} contact {st['contact']:#x} {B['contact']:#x} {log}")
    return res


def target_view(memA, memB, list_b, ent):
    """{pointer: (flags_a, x, z)} for the slot entities and the player, as ent's brain sees
    them: from T+1 for those before ent in the list, else from T."""
    out = {}
    idx = list_b.index(ent) if ent in list_b else len(list_b)
    sol = memB.u32(G_SOLDIER)
    for p in SLOTS + [sol]:
        if not p:
            continue
        m = memB if (p in list_b and list_b.index(p) < idx) else memA
        out[p] = (m.u32(p + 0xC), m.s16(p + 0x20E), m.s16(p + 0x216))
    return out


def mission_level(mem):
    """"M:L" of the level being played (the campaign's 0-based mission and level, 0x80038660)."""
    return f"{mem.s32(0x80038660) + 1}:{mem.s32(0x80038664) + 1}"


def level_of(mem):
    p = mem.u32(0x800939F4)
    return mem.u8(p + 0x8) if p else 0


# ---------------------------------------------------------------- dump pairs

def entity_list(mem):
    out, e = [], mem.u32(0x8009D64C)
    while e and len(out) < 1024:
        out.append(e)
        e = mem.u32(e + 4)
    return out


def chunk_rects(mem):
    return [tuple(mem.s16(CHUNK_SLOTS + 0x28 * i + o) for o in (0, 2, 4, 6)) for i in range(4)]


def pairs_cmd(args):
    verbose = int(args[args.index("-v") + 1]) if "-v" in args else 0
    level = args[args.index("--level") + 1] if "--level" in args else None
    paths = []
    if "--dir" in args:
        d = args[args.index("--dir") + 1]
        vs = sorted(int(m[1]) for m in (re.match(r"d(\d+)\.bin$", f) for f in os.listdir(d)) if m)
        paths = [(os.path.join(d, f"d{v}.bin"), os.path.join(d, f"d{v + 1}.bin")) for v in vs if v + 1 in vs]
    else:
        files = [a for a in args if a.endswith(".bin")]
        paths = list(zip(files[::2], files[1::2]))
    stats = Stats()
    worlds = {}
    shown = [0]
    for pa, pb in paths:
        memA = Mem(open(pa, "rb").read())
        memB = Mem(open(pb, "rb").read())
        if memB.u32(G_TICK) != memA.u32(G_TICK) + 1:
            continue
        v = int(re.search(r"(\d+)\.bin$", pa)[1])
        lv = level or mission_level(memA)
        if lv not in worlds:
            worlds[lv] = ms.World(lv)
        world = worlds[lv]
        world.set_slots(chunk_rects(memB))
        w = World2(memB, {}, Ratan(memA))
        lst = entity_list(memB)
        for E in SLOTS:
            A, B = read_ent(memA, E), read_ent(memB, E)
            if A["type"] not in (SOLDIER, DOG) or B["type"] != A["type"] or not A["brain"] or not B["brain"]:
                continue
            if A["brain_ptr"] != B["brain_ptr"] or A["handle"] != B["handle"]:
                continue
            w.targets = target_view(memA, memB, lst, E)
            run_entity(world, w, A, B, stats, verbose, f"{os.path.basename(pa)} {E:08X}", shown, lv)
    stats.show()


# ---------------------------------------------------------------- the watch log

WATCH = re.compile(r"^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)")
WATCH_AT = re.compile(r"^watch (\d+) (\w+) at ([0-9A-F]+)")


def read_trace(path):
    out = []
    for line in open(path):
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            break
        out.append((d["tick"], d["vblank"], [e[0] for e in d["ents"]],
                    {e[0]: (e[1], e[2], e[3], e[4], e[5]) for e in d["ents"]}))
    return out


def watch_cmd(args):
    log_path, trace_path = args[0], args[1]
    dumps = []
    for a in args[2:]:
        if a.endswith(".bin") or ".bin@" in a:
            p, _, v = a.partition("@")
            dumps.append((int(v or re.search(r"(\d+)\.bin$", p)[1]), p))
    dumps.sort()
    lo = int(args[args.index("--from") + 1]) if "--from" in args else 0
    hi = int(args[args.index("--to") + 1]) if "--to" in args else 1 << 62
    verbose = int(args[args.index("-v") + 1]) if "-v" in args else 0
    only = int(args[args.index("--ent") + 1]) if "--ent" in args else None
    trace = read_trace(trace_path)
    # A watch frame holds a tick's state only if no other tick ran in that frame.
    # The watch logs at the drawing, after all the ticks of a frame: a tick's state is there only
    # if a frame was drawn after it and before the next tick (a watch line between them).
    import bisect
    keys = sorted({int(m[1]) for m in map(re.compile(r"^watch (\d+) ").match, open(log_path, errors="replace")) if m})
    single = set()
    for i in range(len(trace) - 1):
        v, nxt = trace[i][1], trace[i + 1][1]
        j = bisect.bisect_left(keys, v)
        if j < len(keys) and keys[j] < nxt:
            single.add(i)
    want = {}
    for i in range(len(trace) - 2):
        (t0, v0, _, e0), (t1, v1, l1, e1) = trace[i], trace[i + 1]
        if t1 == t0 + 1 and i in single and i + 1 in single and lo <= v0 and v1 <= hi:
            want.setdefault(v0, None)
            want[v1] = (l1, e0, e1)
    want_sorted = sorted(want)
    sizes, addrs = {}, {}
    cur = {}
    stats = Stats()
    worlds = {}
    shown = [0]
    # The levels: the replay's "mark mission M L at vblank V" lines (a recording's level starts).
    bounds = [(int(m[3]), f"{m[1]}:{m[2]}") for m in
              map(re.compile(r"^replay: mark mission (\d+) (\d+) at vblank (\d+)").match, open(log_path, errors="replace")) if m]
    if "--level" in args:
        # --level M:L: the one level played in [--from, --to] (a replay that parted from its
        # recording plays other levels than its marks say)
        bounds = [(lo, args[args.index("--level") + 1]), (hi + 1, None)]

    def level_at(v):
        lv = None
        for b, name in bounds:
            if v >= b:
                lv = name
        return lv

    ctx = dict(static=None, static_v=None, ratan=None, prev=None)

    def flush(g):
        snap = dict(cur)
        prev = ctx["prev"]
        ctx["prev"] = (g, snap)
        if ctx["static"] is None or prev is None or want[g] is None:
            return
        if level_at(prev[0]) != level_at(g) or level_at(g) is None:
            return
        memA = Mem(ctx["static"], prev[1])
        memB = Mem(ctx["static"], snap)
        lv = level_at(g)
        if lv not in worlds:
            worlds[lv] = ms.World(lv)
        world = worlds[lv]
        world.set_slots(chunk_rects(memB))
        w = World2(memB, {}, ctx["ratan"])
        hlist, entsA, entsB = want[g]
        order = [E for h in hlist for E in SLOTS if memB.u32(E) == h]
        # The player's soldier (not watched: its position from the trace) moves after the enemies.
        player = None
        for h, e in entsA.items():
            if e[0] == 1:
                player = (e[1], s16(e[2] >> 16), s16(e[4] >> 16))
        for i, E in enumerate(SLOTS):
            if only is not None and i != only:
                continue
            A, B = read_ent(memA, E), read_ent(memB, E)
            if A["type"] not in (SOLDIER, DOG) or B["type"] != A["type"] or not A["brain"] or not B["brain"]:
                continue
            if A["brain_ptr"] != B["brain_ptr"] or A["handle"] != B["handle"]:
                continue
            w.targets = target_view(memA, memB, order, E)
            if player is not None:
                w.targets[memB.u32(G_SOLDIER)] = player
            run_entity(world, w, A, B, stats, verbose, f"vbl {g} slot {i}", shown, lv)

    di = 0
    wi = 0
    last_f = -1
    for line in open(log_path, errors="replace"):
        m = WATCH.match(line)
        if m:
            f = int(m[1])
            if f != last_f:
                # Every wanted frame before f is complete.
                while wi < len(want_sorted) and want_sorted[wi] < f:
                    g = want_sorted[wi]
                    wi += 1
                    # a dump taken at or before g seeds the state first
                    while di < len(dumps) and dumps[di][0] <= g:
                        seed(dumps[di], cur, sizes, addrs, ctx)
                        di += 1
                    flush(g)
                while di < len(dumps) and dumps[di][0] < f:
                    seed(dumps[di], cur, sizes, addrs, ctx)
                    di += 1
                last_f = f
            a = addrs.get(m[2])
            if a is not None:
                cur[(a + int(m[3], 16)) & 0x1FFFFC] = int(m[5], 16)
            continue
        m = WATCH_AT.match(line)
        if m:
            addrs[m[2]] = int(m[3], 16)
            continue
        m = re.match(r"^watch: (\w+) = .*, 0x([0-9A-F]+) bytes", line)
        if m:
            sizes[m[1]] = int(m[2], 16)
    stats.show()


def seed(dump, cur, sizes, addrs, ctx):
    v, p = dump
    data = open(p, "rb").read()
    mem = Mem(data)
    for name, a in addrs.items():
        for o in range(0, sizes.get(name, 0), 4):
            cur[(a + o) & 0x1FFFFC] = mem.u32(a + o)
    ctx.update(static=data, static_v=v, ratan=Ratan(mem), prev=None)


def watch_list(brains=()):
    """The VALOR_WATCH list: with no brains, the pointer watches whose "at" lines give their
    addresses; else the slots and fixed windows over the brains (overlapping ones merged)."""
    if not brains:
        return ";".join(f"b{i}=[{E:X}+38C]+0:124" for i, E in enumerate(SLOTS))
    items = ["k=80090DE8:A0", "ws=8009817C:8"]
    for i, E in enumerate(SLOTS):
        items += [f"a{i}={E:X}+0:40", f"v{i}={E:X}+4C:A0", f"m{i}={E:X}+1F0:1E0", f"n{i}={E + 0xFBC + 0x5C:X}:48"]
    spans = []
    for a in sorted(set(brains)):
        if spans and a <= spans[-1][1]:
            spans[-1][1] = max(spans[-1][1], a + 0x124)
        else:
            spans.append([a, a + 0x124])
    items += [f"h{j}={a:X}:{b - a:X}" for j, (a, b) in enumerate(spans)]
    return ";".join(items)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "watchlist":
        print(watch_list([int(a, 16) for a in args]))
        return
    {"pairs": pairs_cmd, "watch": watch_cmd}[cmd](args)


if __name__ == "__main__":
    main()
