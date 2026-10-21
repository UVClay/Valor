#!/usr/bin/env python3
"""A reference reimplementation of combat (docs/re/combat.md, animation.md "Hit spheres",
player.md "Weapons in play"), checked tick by tick against the original.

  combat_sim.py watch                        the VALOR_WATCH list the check needs
  combat_sim.py pose DUMP...                 poses, joints and hit spheres in RAM dumps
  combat_sim.py check TRACE.jsonl LOG --seed VBLANK:DUMP... [--from VBLANK] [--to VBLANK] [--level M:L] [-v]

tools/sim/check.py runs it on any recording. LOG and TRACE come from one replay, e.g. of the
recorded session:
  VALOR_WATCH="$(python3 tools/sim/combat_sim.py watch)" VALOR_TRACE_SHOTS=1 VALOR_TRACE_DAMAGE=1 \
  VALOR_TRACE_STATE=build/csim/full2.jsonl RP_BIN=build/valor-wave3 tools/recon/run.sh cs2 1500 \
      build/csim/full2.log --replay recordings/2026-10-19_22-22-43_m1l3 --hidden
and RAM dumps of the same replay a few vblanks into each level (1720, 20500 and 40740 there:
--seed 1720:build/csim/d1720.bin ..., with --to 61377 to stop at 2:3), which seed the watched
memory (a watch logs changes only) and hold each level's models and animations. The watch covers the five character slots (entity
fields, the animation instance, the hit parts, the pose buffers), the pad, the player's weapon
block and the hit counters.

What is stepped, each against the original: the pose a draw builds (keys, interpolation,
cross-fades, the cache), the joints, every hit part and collision sphere per frame; the trigger
and reloads per tick; where each player projectile starts and points; every projectile's
flight; which entity and part each path crosses (nearest first, lowest part), with the world's
collision triangles; the damage of every event, the head and 10% rules; the enemies' aim.
Integer work is done as the game does it (PsyQ ratan2, SquareRoot0/12, rsin, RotMatrixYXZ from
the exe's tables, the GTE's fixed-point steps), so most behaviours compare exactly. The random
draws (bullet speed, spread, the helmet roll) are taken from the outcome and checked against
their documented ranges. The summary prints per behaviour: compared, exact, within, fails.
"""
import bisect
import json
import math
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
FX = 1 << 16
SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]     # the character pool (soldiers, dogs)
POOLS = [0x800C4378 + i * 0x1308 for i in range(5)]     # their pose buffers


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ---------------------------------------------------------------- PsyQ libgte, from the exe

def _exe_tables():
    d = open(os.path.join(ROOT, "extracted", "moh", "SLUS_009.74"), "rb").read()
    base = struct.unpack_from("<I", d, 0x18)[0]
    h = lambda a, n: list(struct.unpack_from(f"<{n}h", d, 0x800 + a - base))
    return h(0x8002D4F8, 1025), h(0x8003440C, 192), h(0x800347EC, 1025), \
        [struct.unpack_from("<hh", d, 0x800 + 0x8003040C - base + 4 * i) for i in range(4096)]


RATAN, SQRT, SIN1, RCOSSIN = _exe_tables()


def ratan2(y, x):
    """ratan2 (0x8001536C): 4096 per turn."""
    nx = ny = False
    if x < 0:
        nx, x = True, -x
    if y < 0:
        ny, y = True, -y
    if x == 0 and y == 0:
        return 0
    if y < x:
        v = trunc_div(y, x >> 10) if y & 0x7FE00000 else trunc_div(y << 10, x)
        v = RATAN[v]
    else:
        v = trunc_div(x, y >> 10) if x & 0x7FE00000 else trunc_div(x << 10, y)
        v = 0x400 - RATAN[v]
    if nx:
        v = 0x800 - v
    if ny:
        v = -v
    return v


def _lzc(a):
    a &= 0xFFFFFFFF
    if a & 0x80000000:
        a = ~a & 0xFFFFFFFF
    return 32 - a.bit_length()


def _sqrt_common(a, shift_base):
    n = _lzc(a)
    if n == 32:
        return 0
    n2 = n & ~1
    t1 = (shift_base - n2) >> 1
    t4 = (a << (n2 - 24)) if n2 >= 24 else (a >> (24 - n2))
    t5 = SQRT[t4 - 0x40]
    return t5, t1


def sqrt0(a):
    """SquareRoot0 (0x80016188)."""
    r = _sqrt_common(a, 31)
    if r == 0:
        return 0
    t5, t1 = r
    return ((t5 << t1) & 0xFFFFFFFF) >> 12


def sqrt12(a):
    """SquareRoot12 (0x8001BC68): sqrt in 4.12."""
    r = _sqrt_common(a, 0x13)
    if r == 0:
        return 0
    t5, t1 = r
    return (t5 << t1) & 0xFFFFFFFF if t1 >= 0 else t5 >> -t1


def sin_1(a):
    if a <= 0x400:
        return SIN1[a]
    if a <= 0x800:
        return SIN1[0x800 - a]
    if a <= 0xC00:
        return -SIN1[a - 0x800]
    return -SIN1[0x1000 - a]


def rsin(a):
    return sin_1(a & 0xFFF) if a >= 0 else -sin_1((-a) & 0xFFF)


def _sc(a):
    """RotMatrixYXZ's lookup: (sin, cos) of a halfword angle."""
    a = s16(a)
    if a >= 0:
        s, c = RCOSSIN[a & 0xFFF]
        return s, c
    s, c = RCOSSIN[(-a) & 0xFFF]
    return -s, c


def rot_yxz(px, py, pz):
    """RotMatrixYXZ (0x8001AF98) of integer angles, as the 3x3 rows the GTE loads."""
    sx, cx = _sc(px)
    sy, cy = _sc(py)
    sz, cz = _sc(pz)
    m = [[0] * 3 for _ in range(3)]
    m[1][2] = s16(-sx)
    m[0][2] = s16((sy * cx) >> 12)
    m[2][2] = s16((cy * cx) >> 12)
    m[1][0] = s16((sz * cx) >> 12)
    m[1][1] = s16((cz * cx) >> 12)
    t8 = (sy * sx) >> 12
    m[0][0] = s16(((cy * cz) >> 12) + ((t8 * sz) >> 12))
    m[0][1] = s16(-((cy * sz) >> 12) + ((t8 * cz) >> 12))
    t8 = (cy * sx) >> 12
    m[2][1] = s16(((sy * sz) >> 12) + ((t8 * cz) >> 12))
    m[2][0] = s16(-((sy * cz) >> 12) + ((t8 * sz) >> 12))
    return m


def mvmva12(m, v):
    """GTE mvmva with sf=1, no translation: (M v) >> 12 per row (IR saturation ignored)."""
    return [(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2]) >> 12 for i in range(3)]


def quat_matrix(q):
    """The bone rotation from a quaternion {x, y, z, w} (4096 = 1), as entity_parts_update builds
    it with the GTE (0x8004D044..0x8004D158; the same as quat_matrix 0x80013E58): the transpose
    of the textbook matrix, each element its own rounding."""
    q0, q1, q2, q3 = q
    m = [[0] * 3 for _ in range(3)]
    m[0][0] = ((q0 * 2 * q0 + q3 * 2 * q3) >> 12) - 0x1000
    m[1][0] = (q1 * 2 * q0 - q2 * 2 * q3) >> 12
    m[2][0] = (q2 * 2 * q0 + q1 * 2 * q3) >> 12
    m[0][1] = (q0 * 2 * q1 + q2 * 2 * q3) >> 12
    m[1][1] = ((q1 * 2 * q1 + q3 * 2 * q3) >> 12) - 0x1000
    m[2][1] = (q2 * 2 * q1 - q0 * 2 * q3) >> 12
    m[0][2] = (-q3 * 2 * q1 + q2 * 2 * q0) >> 12
    m[1][2] = (q2 * 2 * q1 + q3 * 2 * q0) >> 12
    m[2][2] = (((q2 * q2) >> 12) + ((q3 * q3) >> 12)) * 2 - 0x1000
    return [[s16(x) for x in r] for r in m]


def angles_from_to(a, b, lift=0):
    """angles_from_to (0x80044D18): pitch, yaw (integer angle units) from a to b (16.16)."""
    dx = s32(b[0] - a[0]) >> 16
    dz = s32(b[2] - a[2]) >> 16
    yaw = ratan2(dx, dz)
    dy = s32(s32(b[1] - a[1]) - (lift >> 1)) >> 16
    pitch = -ratan2(dy, sqrt0((s16(dx) * s16(dx) + s16(dz) * s16(dz)) & 0xFFFFFFFF))
    return pitch, yaw


# ---------------------------------------------------------------- RAM images

class Ram:
    def __init__(self, path=None):
        self.m = bytearray(open(path, "rb").read()) if path else bytearray(0x200000)

    def u32(self, a):
        return struct.unpack_from("<I", self.m, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.m, a & 0x1FFFFF)[0]

    def u16(self, a):
        return struct.unpack_from("<H", self.m, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.m, a & 0x1FFFFF)[0]

    def u8(self, a):
        return self.m[a & 0x1FFFFF]

    def put32(self, a, v):
        struct.pack_into("<I", self.m, a & 0x1FFFFF, v & 0xFFFFFFFF)

    def ok(self, a):
        return (a & 0xFFE00000) == 0x80000000 and a & 0x1FFFFF


# ---------------------------------------------------------------- animation (animation.md)

def unpack3(w):
    f = lambda lo, n: (lambda v: v - (1 << n) if v >> (n - 1) else v)((w >> lo) & ((1 << n) - 1))
    return f(0, 10), f(10, 11), f(21, 10)


def quat_decode(ram, p, n):
    """quat_decode (0x800847CC): n quaternions {x, y, z, w} from their packed words."""
    out = []
    sx = lambda v, n: v - (1 << n) if v >> (n - 1) else v
    i = 0
    while len(out) < n:
        w0 = ram.u32(p + 4 * i)
        w1 = ram.u32(p + 4 * i + 4)
        w = sx(w0 >> 20, 12)
        x = sx((w0 >> 4) & 0xFFF, 12)
        y = sx(w1 & 0xFFF, 12)
        z = sx((((w1 >> 12) & 0xF) << 8) | (((w0 >> 16) & 0xF) << 4) | (w0 & 0xF), 12)
        out.append((x * 2, y * 2, z * 2, w * 2))
        if len(out) == n:
            break
        w2 = ram.u32(p + 4 * i + 8)
        x = sx(w1 >> 20, 12)
        y = sx((w2 >> 16) & 0xFFF, 12)
        w = sx((w2 >> 4) & 0xFFF, 12)
        z = sx(((w2 >> 28) << 8) | ((w2 & 0xF) << 4) | ((w1 >> 16) & 0xF), 12)
        out.append((x * 2, y * 2, z * 2, w * 2))
        i += 3
    return out


SAME, LERP = 8, 140      # 0x8009EAB0, 0x8009EAAC


def q_interp(a, b, k):
    """One bone of anim_frame_pose / pose_build: a toward b at k quarters (1, 2, 3), literal."""
    d = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + 0x800) >> 12
    neg = d < 0
    if not (d < 0x1000 - SAME or 0x1000 + SAME < d):
        return tuple(a) if k == 1 else tuple(b)
    if neg:
        d = -d
    if 0x1000 - d < LERP:
        if k == 1:
            return tuple(s16(x + (((-y if neg else y) - x + 2) >> 2)) for x, y in zip(a, b))
        if k == 2:
            return tuple(s16(((x - y if neg else x + y) + 1) >> 1) for x, y in zip(a, b))
        if neg:
            return tuple(s16(((x + y + 2) >> 2) - y) for x, y in zip(a, b))
        return tuple(s16(y + ((x - y + 2) >> 2)) for x, y in zip(a, b))
    s = sqrt12((0x01000800 - d * d) >> 12)
    th = ratan2(s, d)
    tt = (th + (1 if k == 2 else 2)) >> (1 if k == 2 else 2)
    wa = trunc_div(rsin(th - tt) << 17, s)
    wb = trunc_div(rsin(tt) << 17, s)
    if k == 3:
        # weight sin(3/4 th) on b, sin(1/4 th) on a; the sign flip goes on a's weight.
        wa, wb = wb, wa
        if neg:
            wa = -wa
    elif neg:
        wb = -wb
    return tuple(s16((wa * x + wb * y + 0x10000) >> 17) for x, y in zip(a, b))


class Model:
    """A model as the game holds it in RAM (animation.md): skeleton, parts, animations."""

    def __init__(self, ram, m):
        self.ram, self.addr = ram, m
        self.hash = ram.u32(m)
        self.table = ram.u32(m + 8)
        self.root = ram.u32(m + 0x9C)
        self.nparts = ram.u16(m + 0xBE)
        tp = ram.u32(m + 0xC0)
        self.parts = []
        for i in range(self.nparts):
            p = tp + 0x48 * i
            self.parts.append(dict(kind=ram.s32(p + 8), a=ram.u16(p + 0x1C), b=ram.u16(p + 0x1E),
                                   r=ram.s16(p + 0x34), r2=ram.s32(p + 0x38),
                                   off=(ram.s16(p + 0x40), ram.s16(p + 0x42), ram.s16(p + 0x44)),
                                   offw=ram.u32(p + 0x40), offz=ram.s16(p + 0x44)))
        self.bones = []          # (index, offset, first child, next sibling) by record address
        self.tree = {}
        if self.root:
            self._walk(self.root)

    def _walk(self, b):
        r = self.ram
        while b:
            self.tree[b] = (r.u16(b), (r.s16(b + 2), r.s16(b + 4), r.s16(b + 6)), r.u32(b + 0xC), r.u32(b + 0x10))
            if r.u32(b + 0xC):
                self._walk(r.u32(b + 0xC))
            b = r.u32(b + 0x10)

    def entry(self, anim):
        e = self.ram.u32(self.table + 4 + 4 * anim)
        return e

    def frame(self, anim, f):
        return self.ram.u32(self.entry(anim) + 4) + 20 * f

    def frame_pose(self, anim, f):
        """anim_frame_pose (0x8008492C)."""
        r = self.ram
        fp = self.frame(anim, f)
        il, n = r.u8(fp + 0xE), r.u8(fp + 0xF)
        nxt, prv = f + (il >> 4), f - (il & 0xF)
        if r.u32(fp + 0x10):
            return quat_decode(r, r.u32(fp + 0x10), n)
        k = 2 if nxt - prv == 2 else f - prv
        if k < 0:
            k = prv - f
        a = quat_decode(r, r.u32(self.frame(anim, prv) + 0x10), n)
        b = quat_decode(r, r.u32(self.frame(anim, nxt) + 0x10), n)
        return [q_interp(x, y, k) for x, y in zip(a, b)]

    def root_offset(self, anim, f):
        return unpack3(self.ram.u32(self.frame(anim, f) + 4))

    def joints(self, quats, root_off, nbones):
        """skeleton_pose (0x80084718) / skeleton_walk (0x80013AE4): joint positions in model space.
        Positions are kept in 2^19 fixed point; a child's is its parent's plus the parent's
        rotation times the bone's offset; the joint is rounded at the end."""
        out = {}
        ident = [[0x1000, 0, 0], [0, 0x1000, 0], [0, 0, 0x1000]]

        def walk(b, prot, ppos):
            while b:
                idx, off, child, sib = self.tree[b]
                if idx > nbones:
                    return
                rot = quat_matrix(quats[idx]) if idx < len(quats) else ident
                pos = [ppos[i] + ((prot[i][0] * (off[0] * 16) + prot[i][1] * (off[1] * 16) +
                                   prot[i][2] * (off[2] * 16)) << 3) for i in range(3)]
                if child:
                    walk(child, rot, pos)
                out[idx] = tuple(s16((2 * p + 0x80000) >> 20) for p in pos)
                b = sib
        walk(self.root, ident, [root_off[0] << 19, root_off[1] << 19, root_off[2] << 19])
        return out


def part_centre_model(part, joints, quats, eye=None):
    """entity_parts_update (0x8004CEE4), one part, in model space (3 x s16)."""
    a, b = part["a"], part["b"]
    if a in (0xFFFE, 0xFFFF):
        c = list(part["off"])
        if a == 0xFFFE:
            c[1] = s16(c[1] + eye)
        return c
    rot = [0, 0, 0]
    if (part["offw"] or part["offz"]) and quats is not None:
        rot = [s16(x) for x in mvmva12(quat_matrix(quats[a]), part["off"])]
    ja, jb = joints.get(a, (0, 0, 0)), joints.get(b, (0, 0, 0))
    return [s16(rot[i] + ((ja[i] + jb[i]) >> 1)) for i in range(3)]


def to_world(m, t, c):
    """The entity's matrix and integer position (mvmva sf=1 with translation)."""
    return [((m[i][0] * c[0] + m[i][1] * c[1] + m[i][2] * c[2]) >> 12) + t[i] for i in range(3)]


def ent_matrix(ram, e):
    return [[ram.s16(e + 0x340 + 6 * i + 2 * j) for j in range(3)] for i in range(3)]


# ---------------------------------------------------------------- the pose check in RAM dumps

def instance_pose(ram, inst, n):
    p = ram.u32(inst + 0x88)
    return [tuple(ram.s16(p + 8 * i + 2 * k) for k in range(4)) for i in range(n)] if p else None


def check_slot_pose(ram, e, verbose=False):
    """For one character in a RAM image: the pose from the animation data against the pose in
    RAM, the joints from the pose against RAM's, the parts from the joints against RAM's.
    Returns a dict of (compared, equal / max error) per stage."""
    inst = ram.u32(e + 0x100)
    model = Model(ram, ram.u32(inst + 8))
    res = {}
    nb = ram.u16(inst + 0x72)
    anim, frame = ram.s16(inst + 0x62), ram.s16(inst + 0x64)
    blend = ram.s16(inst + 0x3E)
    quats = instance_pose(ram, inst, nb)
    jp = ram.u32(inst + 0x94)
    ram_joints = {i: tuple(ram.s16(jp + 8 * i + 2 * k) for k in range(3)) for i in range(nb)}
    if quats is None or anim < 0:
        return res
    nq = ram.u8(model.frame(anim, frame) + 0xF)
    if blend == 0 and ram.u32(inst + 0x88) == ram.u32(inst + 0x90):
        exp = model.frame_pose(anim, frame)
        bad = sum(1 for a, b in zip(exp, quats) if tuple(a) != tuple(b))
        res["pose from the animation"] = (1, bad == 0)
        if verbose and bad:
            print(f"  {e:08X} anim {anim} frame {frame}: {bad} bones differ")
    root = tuple(ram.s16(inst + 0x98 + 2 * k) for k in range(3))
    js = model.joints(quats, root, nq)
    drawn = ram.u32(e + 0x298)
    if drawn:
        bad = [i for i in js if js[i] != ram_joints[i]]
        res["joints from the pose"] = (1, not bad)
        if verbose and bad:
            print(f"  {e:08X} joints differ: {[(i, js[i], ram_joints[i]) for i in bad[:3]]}")
    # parts from RAM's joints (what the game used) and the matrix
    m = ent_matrix(ram, e)
    t = [ram.s32(e + 0x354 + 4 * k) for k in range(3)]
    pp = ram.u32(e + 0x304)
    worst = 0
    for i, part in enumerate(model.parts):
        c = to_world(m, t, part_centre_model(part, ram_joints, quats))
        got = [ram.s32(pp + 0x48 * i + 0x20 + 4 * k) for k in range(3)]
        worst = max(worst, max(abs(a - b) for a, b in zip(c, got)))
    res["parts from the joints"] = (1, worst == 0)
    if verbose and worst:
        print(f"  {e:08X} parts off by up to {worst}")
    # the entity's matrix from its angles
    ang = [ram.s32(e + 0x228 + 4 * k) >> 16 for k in range(3)]
    res["matrix from the angles"] = (1, rot_yxz(*ang) == m)
    return res


def pose_cmd(paths, verbose):
    tot = defaultdict(lambda: [0, 0])
    for path in paths:
        ram = Ram(path)
        for e in SLOTS:
            if ram.s32(e + 8) not in (0x1402, 0x1405) or not ram.ok(ram.u32(e + 0x100)):
                continue
            for k, (n, ok) in check_slot_pose(ram, e, verbose).items():
                tot[k][0] += n
                tot[k][1] += ok
    for k, (n, ok) in tot.items():
        print(f"{k:28s} {ok}/{n} exact")




# ---------------------------------------------------------------- the recorded session

WATCH = re.compile(r"^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)")
WATCH_AT = re.compile(r"^watch (\d+) (\w+) at ([0-9A-F]+)")
SHOT = re.compile(r"^shot (\d+): yaw (\S+) pitch (\S+), view yaw (\S+) pitch (\S+), off by yaw \S+ pitch "
                  r"\S+?( scoped)?; bullet (\S+) (\S+) (\S+), aim (\S+) (\S+) (\S+), camera (\S+) (\S+) (\S+)")
DMG = re.compile(r"^damage (\d+): victim ([0-9A-F]+) type ([0-9A-F]+) part (-?\d+), attacker ([0-9A-F]+) "
                 r"type ([0-9A-F]+) weapon (-?\d+), damage (-?[\d.]+), health (-?[\d.]+) -> (-?[\d.]+)( dead)?")
MARK = re.compile(r"^replay: mark mission (\d) (\d) at vblank (\d+)")
STATS_PART = {0xCC0: 0, 0xCD0: 3, 0xCD4: 4, 0xCD8: 5, 0xCDC: 6, 0xCE0: 1, 0xCE4: 2, 0xCE8: 7, 0xCEC: 8}
STATS_PART.update({0xCF0 + 4 * k: 9 + k for k in range(8)})


def watch_string():
    w = ["pad=80093B7C:10", "iv=[8009D654]+3CC:A0", "ia=[8009D654]+55C:80", "sc=[8009D654]+3C:4",
         "sf=[8009D654]+11C:4", "sh=[8009D654]+F0:C"]
    for i, (b, p) in enumerate(zip(SLOTS, POOLS)):
        w += [f"E{i}={b:X}:20", f"H{i}={b + 0xF0:X}:C", f"M{i}={b + 0x20C:X}:30", f"V{i}={b + 0x298:X}:8",
              f"S{i}={b + 0x2D8:X}:18", f"B{i}={b + 0x304:X}:10", f"R{i}={b + 0x340:X}:20",
              f"I{i}={b + 0xFBC + 0x3C:X}:70", f"P{i}={b + 0x106C:X}:4C8", f"X{i}={p:X}:400"]
    # the action masks: player_pad_config fills them from the layout option once the level runs
    return ";".join(w + ["st=80039CA8:68", "at=800B7790:24", "ax=800B77E0:24"])


def load_log(path, to, frm=0):
    frames = defaultdict(list)
    base, shots, dmg, marks = {}, [], [], []
    for line in open(path, errors="replace"):
        c = line[:2]
        if c == "wa":
            m = WATCH.match(line)
            if m:
                v = int(m[1])
                if v > to:
                    break
                if v < frm:
                    continue
                frames[v].append((base[m[2]] + int(m[3], 16), int(m[4], 16), int(m[5], 16), m[2]))
                continue
            m = WATCH_AT.match(line)
            if m:
                base[m[2]] = int(m[3], 16)
        elif c == "sh":
            m = SHOT.match(line)
            if m and frm <= int(m[1]) <= to:
                g = m.groups()
                shots.append(dict(vb=int(g[0]), yaw=float(g[1]), pitch=float(g[2]), vyaw=float(g[3]),
                                  vpitch=float(g[4]), scoped=bool(g[5]), pos=tuple(map(float, g[6:9])),
                                  aim=tuple(map(float, g[9:12])), cam=tuple(map(float, g[12:15]))))
        elif c == "da":
            m = DMG.match(line)
            if m and frm <= int(m[1]) <= to:
                g = m.groups()
                dmg.append(dict(vb=int(g[0]), victim=int(g[1], 16), vtype=int(g[2], 16), att=int(g[4], 16),
                                atype=int(g[5], 16), weapon=int(g[6]), dmg=float(g[7]), before=float(g[8]),
                                after=float(g[9]), dead=bool(g[10])))
        elif c == "re":
            m = MARK.match(line)
            if m:
                marks.append((int(m[3]), f"{m[1]}:{m[2]}"))
    return frames, shots, dmg, marks


def load_trace(path, to, frm=0):
    out = []
    for line in open(path):
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            break
        if d["vblank"] > to:
            break
        if d["vblank"] >= frm:
            out.append(d)
    return out


class Stats:
    """Per behaviour: compared, exact, within (a documented range or rounding), fails."""

    def __init__(self):
        self.n = defaultdict(lambda: [0, 0, 0, 0])
        self.ex = defaultdict(list)
        self.spread = defaultdict(list)
        self.enemy_dmg = Counter()
        self.helmets = 0
        self.aim_err = []

    def add(self, key, kind, example=None):
        r = self.n[key]
        r[0] += 1
        r[{"exact": 1, "within": 2, "fail": 3}[kind]] += 1
        if kind == "fail" and example and len(self.ex[key]) < int(os.environ.get("CS_EX", 6)):
            self.ex[key].append(example)

    def extras(self):
        for w, xs in sorted(self.spread.items()):
            for k, name in enumerate(("pitch", "yaw", "roll")):
                v = [x[k] for x in xs]
                print(f"spread weapon {w} {name}: n {len(v)}, min {min(v):.3f} max {max(v):.3f} mean {sum(v) / len(v):.2f}")
        if self.enemy_dmg:
            print("enemy bullet damage on the player (level, value: count):",
                  ", ".join(f"{l} {v}: {c}" for (l, v), c in sorted(self.enemy_dmg.items())))
        if self.aim_err:
            ep = sorted(abs(e[0]) for e in self.aim_err)
            ey = sorted(abs(e[1]) for e in self.aim_err)
            print(f"enemy aim error (angle units): {len(ep)} bullets, |pitch| median {ep[len(ep) // 2]} max {ep[-1]}, "
                  f"|yaw| median {ey[len(ey) // 2]} max {ey[-1]}")

    def report(self):
        print(f"\n{'behaviour':58s} {'n':>6s} {'exact':>6s} {'within':>6s} {'fails':>6s}")
        for k, (n, e, w, f) in self.n.items():
            print(f"{k:58s} {n:6d} {e:6d} {w:6d} {f:6d}")
        for k, ex in self.ex.items():
            print(f"\n{k}, first fails:")
            for x in ex:
                print("   ", x)


def models_by_root(ram):
    """The level's models, by root bone (the instance's +0x84; its model pointer isn't watched)."""
    out = {}
    first = ram.u32(0x8009EC58)
    for i in range(ram.u32(0x8009EC54)):
        m = first + 0x100 * i
        if ram.u32(m + 0x9C):
            out[ram.u32(m + 0x9C)] = Model(ram, m)
    return out


class View:
    """Read-only Ram over a bytes snapshot."""

    def __init__(self, m):
        self.m = m

    u32, s32, u16, s16, u8, ok = Ram.u32, Ram.s32, Ram.u16, Ram.s16, Ram.u8, Ram.ok


def slot_quats(ram, ptr, n):
    return [tuple(ram.s16(ptr + 8 * i + 2 * k) for k in range(4)) for i in range(n)]


def slot_joints(ram, ptr, n):
    return {i: tuple(ram.s16(ptr + 8 * i + 2 * k) for k in range(3)) for i in range(n)}


def blend_root(a, t, k):
    """pose_build's root offset during a fade, k quarters toward the target."""
    if k == 0:
        return tuple(a)
    if k == 1:
        return tuple(s16(x + ((y - x) >> 2)) for x, y in zip(a, t))
    if k == 2:
        return tuple(s16(x + ((y - x) >> 1)) for x, y in zip(a, t))
    return tuple(s16(y - ((y - x) >> 2)) for x, y in zip(a, t))


def expected_pose(pre, inst, model, nb):
    """pose_build (0x80085648) as the draw of the previous frame ran it, from the instance as the
    ticks left it: (cache anim, frame, quats or None (the saved pose shown as is), root offset)."""
    anim, frame = pre.s16(inst + 0x5C), pre.s16(inst + 0x5E)
    c = pre.s16(inst + 0x3E)
    if anim < 0:
        return None
    if c == 0 and (pre.s16(inst + 0x62), pre.s16(inst + 0x64)) == (anim, frame):
        # the cache matches: nothing is rebuilt (after a fade the cache holds its target frame)
        root = tuple(pre.s16(inst + 0x98 + 2 * j) for j in range(3))
        return anim, frame, "same", root, 0
    if c > 0 and pre.s16(inst + 0x62) != -1 and pre.u32(inst + 0x88):
        tgt = pre.s16(inst + 0x40)
        if tgt < 0:
            tgt = pre.s16(inst + 0x60)
        k = 4 - c
        saved = slot_quats(pre, pre.u32(inst + 0x8C), nb)
        a0 = tuple(pre.s16(inst + 0xA0 + 2 * j) for j in range(3))
        root = blend_root(a0, model.root_offset(anim, tgt), k)
        if k == 0:
            return anim, tgt, None, root, c
        b = model.frame_pose(anim, tgt)
        n = pre.u8(model.frame(anim, tgt) + 0xF)
        return anim, tgt, [q_interp(x, y, k) for x, y in zip(saved[:n], b)], root, c
    root = tuple(pre.s16(inst + 0x98 + 2 * j) for j in range(3))
    return anim, frame, model.frame_pose(anim, frame), root, 0


def slot_parts(ram, e, inst, model, angles, pos16, joints=None, quats=None):
    """The hit parts' world centres for a slot, from the pose in ram and the given angles and
    position (16.16): entity_bounds as each tick runs it."""
    nb = ram.u16(inst + 0x72)
    if joints is None:
        joints = slot_joints(ram, ram.u32(inst + 0x94), nb)
    if quats is None:
        qp = ram.u32(inst + 0x88)
        quats = slot_quats(ram, qp, nb) if qp else None
    m = rot_yxz(*[a >> 16 for a in angles])
    t = [p >> 16 for p in pos16]
    return [to_world(m, t, part_centre_model(p, joints, quats)) for p in model.parts], m, t, joints




# ---------------------------------------------------------------- the player's trigger

def player_rows(trace):
    """Per tick: (vblank, slot, weapon, clip, reserve)."""
    return [(t["vblank"], t["player"][2], t["player"][3], t["player"][4], t["player"][5]) if "player" in t else None
            for t in trace]


def check_trigger(st, trace, fstate):
    """player_update's trigger (player.md, The trigger), stepped from the original's refire
    state at the end of the previous frame, with the frame's pad: when shots fire and when the
    empty clip reloads."""
    rows = player_rows(trace)
    prev_pad = 0
    last_vb = None
    cd = lat = 0
    for n in range(1, len(trace)):
        r, r0 = rows[n], rows[n - 1]
        if not r or not r0:
            continue
        _, slot, w, clip, res = r
        vb = trace[n]["frame"]
        fs = fstate.get(vb)
        pfs = fstate.get(trace[n - 1]["frame"])
        if not fs or not pfs:
            continue
        # The game polls the pad for every tick (player.md, The trigger); the watch shows the
        # frame's last two polls (0x80093B7C now, 0x80093B84 before): a frame of k ticks reads
        # the earlier one for its first k-1 ticks, the last one for its last tick.
        poll = fs["pad"] if trace[n]["last"] or trace[n]["nframe"] == 1 else fs["pad_prev"]
        if vb != last_vb:
            iv = fs["pre_iv"]
            cd, lat = iv[0x48 // 4], iv[0x44 // 4]
            cur, prev = poll, prev_pad
            prev_pad = fs["pad"]
            last_vb = vb
        else:
            prev, cur = cur, poll
        if fs["level"] != pfs["level"] or slot != r0[1] or w != r0[2] or w in (3, 7, 10):
            continue
        # a dead player has no trigger or reload (player_think_dead instead of player_update,
        # contact 0x4000 stops player_weapon_update; movement.md, Death)
        if any(e[1] == 1 and e[9] <= 0 for k in (n - 1, n) for e in trace[k]["ents"]):
            continue
        m, x = fs["mask"]
        held = (cur & m) == m and (x == 0 or (cur & x) != x)
        pressed = held and (prev & m) != m
        trig = False
        if cd > 0:
            cd -= 1
        else:
            trig = held if lat > 0 else pressed
        delay, flags, _, size = fs["weapons"][w]
        busy = (fs["pre_script"] if trace[n]["first"] else fs["script"]) in (2, 9, 10, 13, 16)
        # Use (action 5) comes first in player_update: a reload it starts this tick makes the
        # weapon script 9 before player_weapon_update looks, so the trigger's shot is lost
        um, ux = fs["use"]
        use = (cur & um) == um and (prev & um) != um and (ux == 0 or (cur & ux) != ux)
        use_reload = use and r0[4] != 0 and 0 < r0[3] < size and not flags & 1
        # so does the weapon switch (action 2, weapon_holster: script 10), first of all
        hm, hx = fs["holster"]
        holster = (cur & hm) == hm and (prev & hm) != hm and (hx == 0 or (cur & hx) != hx)
        fire = trig and r0[3] > 0 and not flags & 1 and not busy and not use_reload and not holster
        shot = clip == r0[3] - 1
        before = (lat, cd)
        if fire:
            lat, cd = delay, abs(delay)
        key = "trigger: a shot exactly when refire 0 and pressed/held"
        if fire == shot:
            st.add(key, "exact")
        elif not trace[n]["first"] and fs["pre_script"] != fs["script"]:
            # the weapon script (reload, raise) ended inside a frame of several ticks: the watch
            # doesn't show in which
            st.add(key, "within")
            lat, cd = (delay, abs(delay)) if shot else before
        else:
            st.add(key, "fail", f"tick {trace[n]['tick']} vblank {vb} weapon {w} clip {r0[3]}->{clip}: "
                                f"predicted {'shot' if fire else 'none'}, cd {cd} lat {lat} held {held} pressed {pressed} script {fs['pre_script']}->{fs['script']}")
        # the automatic reload: clip 0, countdown run out, rounds in reserve
        reload = clip > r0[3] and res < r0[4]
        want = (clip if fire else r0[3]) == 0 and cd <= 0 and r0[4] != 0 and not flags & 1
        if use and not want and (reload or r0[3] > 0):
            want = use and r0[4] != 0 and r0[3] < size and not flags & 1
            key = "reload: on a Use press (clip not full, rounds in reserve, whatever weapon script runs)"
            if reload == want:
                st.add(key, "exact")
            elif not trace[n]["first"] and fs["pre_script"] != fs["script"]:
                # a weapon script started or ended inside a frame of several ticks: the watch
                # doesn't show in which
                st.add(key, "within")
            else:
                st.add(key, "fail", f"tick {trace[n]['tick']} vblank {vb} weapon {w} "
                       f"clip {r0[3]}->{clip} reserve {r0[4]}->{res}: predicted {want} script {fs['pre_script']}")
        elif reload or (want and r0[3] == 0):
            st.add("reload: starts the tick the countdown runs out on an empty clip",
                   "exact" if reload == want else "fail", f"tick {trace[n]['tick']} vblank {vb} weapon {w} "
                   f"clip {r0[3]}->{clip} reserve {r0[4]}->{res}: predicted {want}")
            if reload:
                size = clip - r0[3] + 0
                st.add("reload: rounds move at once (clip = min(size, clip + reserve))",
                       "exact" if res == r0[4] - (clip - r0[3]) else "fail", f"vblank {vb}")


# ---------------------------------------------------------------- where shots start and point

def shot_ticks(trace):
    rows = player_rows(trace)
    out = {}
    for n in range(1, len(trace)):
        r, r0 = rows[n], rows[n - 1]
        if r and r0 and r[1] == r0[1] and r[2] == r0[2] and r[3] == r0[3] - 1:
            out[n] = r[2]
    return out


def new_projectiles(trace, n, types):
    before = {e[0] for e in trace[n - 1]["ents"]} if n else set()
    return [e for e in trace[n]["ents"] if e[1] in types and e[0] not in before]


def check_aim(st, trace, shots, fstate, targets, dmg):
    """fire_gun / projectile_aim: the bullet's start and angles from the stance, the weapon
    block's start point (+0x198) and aim point (+0x1A4), plus the spread. A projectile that is
    missing from the firing tick's state was removed in that tick: its zero-length path started
    inside a hit part."""
    fired = shot_ticks(trace)
    dmg_ticks = Counter((d["vb"], d["atype"]) for d in dmg)
    for n, w in sorted(fired.items()):
        t = trace[n]
        fs = fstate.get(t["frame"])
        if not fs or w in (2, 3, 10):
            continue
        ia = fs["ia"]
        stance = ia[(0x1C8 - 0x190) // 4]
        start = ia[2:5]
        aim = ia[5:8]
        flags = fs["iv"][0]
        new = new_projectiles(trace, n, (0x13F6, 0x13F7))
        want = 6 if w == 4 else 1
        btype = 0x13F7 if w in (1, 4, 6, 11) else 0x13F6
        if len(new) < want:
            # removed in the firing tick: the start inside a part (the path has no length)
            lo, hi = tick_window(trace, n)
            got = sum(dmg_ticks[(v, btype)] for v in range(lo + 1 if lo < hi else hi, hi + 1))
            if t["last"] and stance in (0, 1, 7, 8) and not flags & 0x100 and w != 4:
                pitch, yaw = angles_from_to(start, aim)
                m = rot_yxz(pitch, yaw, 0)
                tgs = [tg for tg in targets.get(n, []) if tg["slot"] != "player"]
                hit = collide(start, start, m, tgs, 0x08000450)
                moved = set()
                for sp in range(600, 670):
                    cur, _ = bullet_step(start, [pitch << 16, yaw << 16, 0], sp)
                    moved.add(tuple(collide(start, cur, m, tgs, 0x08000450)[:1]))
                walls = set()
                if not got:
                    # nothing hit: it moved in its firing tick (an entity followed the soldier)
                    # and stopped at a wall
                    for sp in range(600, 670):
                        cur, _ = bullet_step(start, [pitch << 16, yaw << 16, 0], sp)
                        walls.add(world_cut(fs["level"], start, cur)[1])
                if hit and got:
                    st.add("shot: gone in its firing tick: started inside a hit part", "exact")
                elif not got and walls == {True}:
                    st.add("shot: gone in its firing tick: moved into a wall (an entity followed the soldier)", "exact")
                elif got and all(moved):
                    st.add("shot: gone in its firing tick: moved and hit (an entity followed the soldier)",
                           "exact" if len(moved) == 1 else "within")
                else:
                    st.add("shot: gone in its firing tick: moved and hit (an entity followed the soldier)", "fail",
                           f"vblank {t['vblank']} weapon {w}: predicted still {hit[:1]}, moved {moved}, damage events {got}")
            else:
                st.add("shot: gone in its firing tick (not predicted: pellets, gun bones or a later tick)", "within")
        else:
            st.add("shot: projectiles per shot (6 pellets, else 1)", "exact" if len(new) == want else "fail",
                   f"vblank {t['vblank']} weapon {w}: {len(new)}")
        ty = [e[1] for e in t["ents"]]
        follows = ty.index(1) + 1 < len(ty) and any(e[0] not in {b[0] for b in new} for e in t["ents"][ty.index(1) + 1:])
        for b in new:
            moved = list(b[3:6]) != list(start) if t["last"] and not flags & 0x100 and stance in (0, 1, 7, 8) else None
            if moved is not None:
                st.add("shot: still at its start after the firing tick (nothing followed the soldier)",
                       "exact" if not moved and not follows else "fail" if moved == (not follows) else "within",
                       f"vblank {t['vblank']} weapon {w}: moved {moved}, entities after the soldier {follows}")
        if not t["last"]:
            # the watch shows the weapon block after a later tick of the same frame
            st.add("start and angles: not compared (watched after a later tick)", "within")
            continue
        cam = [e for e in t["ents"] if e[1] == 2]
        for b in new:
            pos, ang = b[3:6], b[6:9]
            if flags & 0x100:
                ok = cam and list(pos) == list(cam[0][3:6])
                st.add("start: scoped, the camera's position", "exact" if ok else "fail", f"vblank {t['vblank']}")
                continue
            if stance not in (0, 1, 7, 8):
                st.add(f"start: stance {stance} (gun bones), not predicted", "within")
                continue
            if list(pos) == list(start):
                st.add("start: the weapon block's +0x198", "exact")
            else:
                lo, hi = (350, 749) if w == 4 else (600, 669)
                sp = speed_of(start, list(pos), ang, lo, hi)
                st.add("start: the weapon block's +0x198, one step on (moved in the firing tick)",
                       "within" if sp else "fail",
                       f"vblank {t['vblank']} weapon {w}: {[p / FX for p in pos]} vs {[p / FX for p in start]}")
            pitch, yaw = angles_from_to(start, aim)
            dp, dy, dr = ang[0] - (pitch << 16), ang[1] - (yaw << 16), ang[2]
            if w == 4:
                lim, key = 64, "angles: toward the aim point + pellet spread (+-64)"
            elif w in (1, 6, 11):
                lim, key = 16, "angles: toward the aim point + spread (+-16)"
            else:
                lim, key = 0, "angles: toward the aim point (angles_from_to)"
            ok = all(abs(d) <= lim * FX and d % 512 == 0 for d in (dp, dy, dr))
            kind = "exact" if ok and lim == 0 else "within" if ok else "fail"
            st.add(key, kind, f"vblank {t['vblank']} weapon {w} stance {stance}: bullet {ang[0] / FX:.3f} "
                              f"{ang[1] / FX:.3f}, predicted {pitch} {yaw}")
            if lim:
                st.spread[w].append((dp / FX, dy / FX, dr / FX))


# ---------------------------------------------------------------- the world (levels.md, Collision)

class WorldMesh:
    """Every collision triangle of a level's TSP chunks, for the projectiles' world sweep. The
    game's own sweep (world_sweep, 0x8003EF5C) walks the chunk's k-d tree; here a plain
    segment-triangle test in floating point finds the same first face to within a unit."""

    def __init__(self, level):
        import numpy as np
        sys.path.insert(0, os.path.join(ROOT, "tools", "recon"))
        import tspcol
        m, l = level.split(":")
        d = os.path.join(ROOT, "extracted", "moh", "DATA", f"MSN{m}", f"LVL{l}", "TSP0")
        tris = []
        k = 1
        while os.path.exists(os.path.join(d, f"{m}_{l}_C{k}.TSP")):
            c = tspcol.parse(open(os.path.join(d, f"{m}_{l}_C{k}.TSP"), "rb").read())
            v = c["verts"]
            tris += [(v[f[0]], v[f[1]], v[f[2]]) for f in c["faces"]]
            k += 1
        self.np = np
        t = np.array(tris, dtype=np.float64)
        self.a, self.e1, self.e2 = t[:, 0], t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]
        self.lo, self.hi = t.min(axis=1), t.max(axis=1)

    def first_hit(self, p, q):
        """The fraction along p -> q (world units) of the first face crossed, or None."""
        np = self.np
        p, q = np.array(p, float), np.array(q, float)
        lo, hi = np.minimum(p, q), np.maximum(p, q)
        sel = np.all(self.hi >= lo - 1, axis=1) & np.all(self.lo <= hi + 1, axis=1)
        if not sel.any():
            return None
        a, e1, e2 = self.a[sel], self.e1[sel], self.e2[sel]
        dvec = q - p
        h = np.cross(dvec, e2)
        det = np.einsum("ij,ij->i", e1, h)
        ok = np.abs(det) > 1e-9
        inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0)
        sv = p - a
        u = np.einsum("ij,ij->i", sv, h) * inv
        qv = np.cross(sv, e1)
        v = np.einsum("j,ij->i", dvec, qv) * inv
        t = np.einsum("ij,ij->i", e2, qv) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t >= 0) & (t <= 1)
        return float(t[hit].min()) if hit.any() else None


_WORLDS = {}


def world(level):
    if level not in _WORLDS:
        _WORLDS[level] = WorldMesh(level)
    return _WORLDS[level]


def world_cut(level, prev, cur):
    """The projectile's move cut at the first wall (16.16 in and out), and whether it was."""
    if level is None:
        return cur, False
    p = [x / FX for x in prev]
    q = [x / FX for x in cur]
    f = world(level).first_hit(p, q)
    if f is None:
        return cur, False
    return [s32(int(prev[k] + (cur[k] - prev[k]) * f)) for k in range(3)], True

# ---------------------------------------------------------------- bullets against hit spheres

def normal_ss(d):
    """VectorNormalSS: the direction as a 4096-long s16 vector (rounded here; the GTE's table
    method can differ by one unit, which only matters on a sphere's very edge)."""
    n = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    return [int(x * 4096 / n) for x in d] if n else [0, 0, 0]


def sat16(v):
    return max(-0x8000, min(0x7FFF, v))


def cross12(d, u):
    """GTE OP (sf=1) of the diagonal (dx, dy, dz) with IR = u, saturated to IR's 16 bits."""
    return [sat16((d[1] * u[2] - d[2] * u[1]) >> 12), sat16((d[2] * u[0] - d[0] * u[2]) >> 12),
            sat16((d[0] * u[1] - d[1] * u[0]) >> 12)]


def segment_sphere(start, u, centre, r):
    """segment_sphere (0x8004CBC8): the line (not the segment) within the radius; the sort key."""
    if not r:
        return None
    d = [centre[k] - start[k] for k in range(3)]
    c = cross12([s16(x) for x in d], u)
    if r * r < c[0] * c[0] + c[1] * c[1] + c[2] * c[2]:
        return None
    a = sorted(abs(x) for x in d)
    return a[2] + ((a[1] + a[0]) >> 2)


def segment_parts(start, u, len2, parts):
    """segment_parts (0x8004C76C): the lowest-numbered part the path crosses (or None)."""
    for j, (c, r2, kind) in enumerate(parts):
        if kind == 7:
            continue
        d = [s16(c[k] - start[k]) for k in range(3)]
        a1 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - r2
        if a1 > 0:
            if d[0] * u[0] + d[1] * u[1] + d[2] * u[2] <= 0 or len2 < a1:
                continue
            x = cross12(d, u)
            if r2 < x[0] * x[0] + x[1] * x[1] + x[2] * x[2]:
                continue
        return j
    return None


def bullet_step(prev, angles, n):
    """entity_move for a projectile: the speed (+0x200, integer n) as the local z velocity,
    turned by RotMatrixYXZ of the integer angles; each axis moves in 1/16 units."""
    m = rot_yxz(angles[0] >> 16, angles[1] >> 16, 0)
    vz = s16(((n << 16) & 0xFFFFFFFF) >> 12)
    return [s32(prev[i] + (((m[i][2] * vz) >> 12) << 12)) for i in range(3)], m


def collide(prev, cur, m, targets, mask, nearest=True):
    """entities_collide for a swept, nearest-first projectile: [(slot, part)] of the first target
    whose parts the path prev -> cur crosses, nearest (by the sphere's sort key) first."""
    d = [s16(s32(cur[k] - prev[k]) >> 16) for k in range(3)]
    len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2]
    u = normal_ss(d)
    if u == [0, 0, 0]:
        u = [m[0][2], m[1][2], m[2][2]]
    start = [p >> 16 for p in prev]
    hits = []
    for tg in targets:
        if not (mask & tg["flags_a"]) or tg["flags_a"] & 0x80000 or tg["flags_b"] & 6:
            continue
        key = segment_sphere(start, u, tg["sphere"], tg["radius"])
        if key is None:
            continue
        part = segment_parts(start, u, len2, tg["parts"])
        if part is not None:
            hits.append((key, tg["slot"], part))
    if nearest:
        hits.sort(key=lambda h: h[0])
    return [(h[1], h[2]) for h in hits]


# ---------------------------------------------------------------- flights and hits

def tracks(trace, types):
    """Projectiles in the trace: per handle, contiguous runs of (tick index, pos, angles)."""
    live, out = {}, []
    for n, t in enumerate(trace):
        seen = set()
        for e in t["ents"]:
            if e[1] in types:
                k = (e[0], e[1])
                seen.add(k)
                if k not in live:
                    live[k] = dict(handle=e[0], type=e[1], n0=n, pos=[], ang=e[6:9])
                live[k]["pos"].append(list(e[3:6]))
        for k in [k for k in live if k not in seen]:
            out.append(live.pop(k))
    return out


def speed_of(prev, cur, ang, lo, hi):
    for n in range(lo, hi + 1):
        if bullet_step(prev, ang, n)[0] == cur:
            return n
    return None


def tick_window(trace, n):
    """The vblanks whose damage events belong to tick n: a tick's events are logged with the
    vblank of the moment, its state with a later one when the tick straddles a vblank."""
    lo = trace[n - 1]["vblank"] if n else -1
    return lo, trace[n]["vblank"]


def victims_at(dmg_by_vb, window, btype):
    """The damage events a projectile type caused in one tick, by victim."""
    out = Counter()
    lo, hi = window
    for vb in range(lo + 1 if lo < hi else hi, hi + 1):
        for d in dmg_by_vb.get(vb, []):
            if d["atype"] == btype:
                out[d["victim"]] += 1
    return out


def check_bullets(st, trace, targets, shots, dmg, fstate, verbose, levels=None):
    levels = levels or {}
    fired = shot_ticks(trace)
    dmg_by_vb = defaultdict(list)
    for d in dmg:
        dmg_by_vb[d["vb"]].append(d)
    vanish = []
    for tr in tracks(trace, (0x13F4, 0x13F6, 0x13F7)):
        n0, pos, ang = tr["n0"], tr["pos"], tr["ang"]
        player = tr["type"] != 0x13F4
        pellet = player and fired.get(n0) == 4
        lo, hi = (350, 749) if pellet else (600, 669)
        name = "pellet" if pellet else "player bullet" if player else "enemy bullet"
        # flight: still on the first tick, then a fixed random speed along the angles
        n = None
        for k in range(1, len(pos)):
            sp = speed_of(pos[k - 1], pos[k], ang, lo, hi)
            if n is None and sp is not None:
                n = sp
                st.add(f"flight: {name} speed in {lo}..{hi}, along the angles, 1/16 steps", "within")
            ok = sp is not None and sp == n
            if not ok:
                st.add(f"flight: {name} speed in {lo}..{hi}, along the angles, 1/16 steps", "fail",
                       f"tick {trace[n0 + k]['tick']}: no speed fits")
            elif k > 1:
                st.add(f"flight: {name} keeps its speed", "exact")
        if len(pos) == 15 and player:
            st.add("flight: life 15 ticks", "exact")
        if not player and n0 > 0:
            # enemies.md: aimed at the player's position raised by half the eye height (the
            # player as the previous tick left him: enemies think before the player moves),
            # plus a random error in +-E on pitch and yaw. An enemy's bullet moves in its
            # firing tick (the list goes on past the enemy), so the start is one step back.
            pl = [e for e in trace[n0 - 1]["ents"] if e[1] == 1]
            if pl:
                start = bullet_step(pos[0], [ang[0], ang[1], 0], -(n or 635))[0]
                tgt = [pl[0][3], pl[0][4] - (120 << 16), pl[0][5]]
                pp, yy = angles_from_to(start, tgt)
                ep = ((ang[0] >> 16) - pp + 2048) % 4096 - 2048
                ey = ((ang[1] >> 16) - yy + 2048) % 4096 - 2048
                if max(abs(ep), abs(ey)) <= 100:
                    st.aim_err.append((ep, ey, n is not None))
                    st.add("enemy aim: bullet angles within +-100 of the line to the player's mid-height", "within")
                else:
                    # not aimed: suppressing fire along the facing (state 3), or the death spray
                    # (anim 53, fire 3 along the falling gun, soldier.md)
                    sh = sorted((sum(((e[3 + j] - start[j]) >> 16) ** 2 for j in range(3)), e) for e in trace[n0 - 1]["ents"]
                                if e[1] == 0x1402)
                    e = sh[0][1] if sh else None
                    off = abs(((ang[1] >> 16) - (e[7] >> 16) + 2048) % 4096 - 2048) if e else 9999
                    if e is not None and e[10] == 53:
                        st.add("enemy aim: not aimed: the death spray (anim 53, along the falling gun)", "within")
                    else:
                        st.add("enemy aim: not aimed: along the shooter's facing (suppressing fire), +-100",
                               "within" if off <= 100 else "fail",
                               f"tick {trace[n0]['tick']}: error pitch {ep} yaw {ey}, off the nearest soldier's facing {off}")
        # the hit test, tick by tick: the firing tick (zero length), then each move
        owner = None
        if not player:
            tg0 = targets.get(n0, [])
            near = [(sum((tg["sphere"][j] - (pos[0][j] >> 16)) ** 2 for j in range(3)), tg["slot"]) for tg in tg0
                    if tg["slot"] != "player"]
            owner = min(near)[1] if near else None
        mask = 0xFFFFFFFF if not player else 0x08000450
        end = n0 + len(pos)            # the tick it vanished in
        for k in range(0, len(pos) + 1):
            tn = n0 + k
            if tn >= len(trace) or tn not in targets:
                break
            tgs = [tg for tg in targets[tn] if tg["slot"] != owner and (not player or tg["slot"] != "player")]
            if k == 0:
                outs = {tuple(collide(pos[0], pos[0], rot_yxz(ang[0] >> 16, ang[1] >> 16, 0), tgs, mask, not pellet)[:1])}
            elif k < len(pos):
                outs = {tuple(collide(pos[k - 1], pos[k], None, tgs, mask, not pellet)[:1])}
            else:
                speeds = [n] if n else range(lo, hi + 1)
                outs = set()
                outs_nc = set()
                for sp in speeds:
                    cur, m = bullet_step(pos[k - 1], ang, sp)
                    outs_nc.add(tuple(collide(pos[k - 1], cur, m, tgs, mask, not pellet)[:1]))
                    cur, wall = world_cut(levels.get(tn), pos[k - 1], cur)
                    outs.add(tuple(collide(pos[k - 1], cur, m, tgs, mask, not pellet)[:1]))
            if k < len(pos):
                # it flew on: nothing may have been hit
                hit = [o for o in outs if o]
                vb = trace[tn]["vblank"]
                st.add(f"hits: {name} flies on when its path misses every hit part",
                       "fail" if hit else "exact", f"tick {trace[tn]['tick']} vblank {vb}: predicted {hit}")
                continue
            if len(pos) == 15:
                break
            vb = trace[tn]["vblank"]
            pred = {o[0] if o else None for o in outs}
            addr_of = lambda ps: {(SLOTS[p[0][0]] if isinstance(p[0][0], int) else "player") if p else None for p in ps}
            pred_addr = addr_of(outs)
            pred_addr_nc = addr_of(outs_nc)
            vanish.append(dict(vb=vb, type=tr["type"], name=name, pred=pred, pred_addr=pred_addr, tn=tn, speed=n,
                               pred_addr_nc=pred_addr_nc, pred_nc={o[0] if o else None for o in outs_nc},
                               player=player))
            break

    # match the vanished projectiles of each frame to that frame's damage events by victim
    groups = defaultdict(list)
    pellet_parts = defaultdict(lambda: (set(), set()))
    for r in vanish:
        groups[(r["tn"], r["type"])].append(r)
    for (tn0, btype), rs in groups.items():
        vb = trace[tn0]["vblank"]
        left = Counter({(v if v in SLOTS else "player"): c
                        for v, c in victims_at(dmg_by_vb, tick_window(trace, rs[0]["tn"]), btype).items()})
        rs.sort(key=lambda r: (len(r["pred_addr"]), None in r["pred_addr"]))
        for r in rs:
            r["act"] = None
            for a in sorted(r["pred_addr"] - {None}, key=str):
                if left[a] > 0:
                    left[a] -= 1
                    r["act"] = a
                    break
        for r in rs:
            if r["act"] is None and None not in r["pred_addr"]:
                for a in left:
                    if left[a] > 0:
                        left[a] -= 1
                        r["act"] = a
                        break
        for r in rs:
            if r["act"] is None and None in r["pred_addr"] and any(left.values()):
                a = next(a for a in left if left[a] > 0)
                left[a] -= 1
                r["act"] = a
        for r in rs:
            act, pa, tn = r["act"], r["pred_addr"], r["tn"]
            kind = ("exact" if len(pa) == 1 else "within") if act in pa else "fail"
            if kind == "fail" and act in r["pred_addr_nc"]:
                kind = "within"
                st.add(f"hits: {r['name']}: our float world test stopped it short", "within")
            st.add(f"hits: {r['name']}'s target (nearest crossed, or none: a wall)", kind,
                   f"tick {trace[tn]['tick']} vblank {vb}: predicted {sorted(map(str, r['pred']))}, actual "
                   f"{act if not isinstance(act, int) else act} (speed {r['speed']})")
            fs = fstate.get(trace[tn]["frame"])
            if r["player"] and act not in (None, "player") and fs:
                stp = [STATS_PART[x] for x, c in fs["stats"].items() if x in STATS_PART and c > 0]
                tg = [x for x in targets[tn] if x["addr"] == act]
                if tg and tg[0]["type"] == 0x1402 and stp:
                    pp = {p[1] for p in r["pred"] | r["pred_nc"] if p and p[0] == SLOTS.index(act)}
                    if r["name"] == "pellet":
                        pellet_parts[trace[tn]["frame"]][0].update(pp)
                        pellet_parts[trace[tn]["frame"]][1].update(stp)
                        continue
                    kind = "exact" if pp == set(stp) and len(pp) == 1 else "within" if pp & set(stp) else "fail"
                    st.add(f"hits: {r['name']}'s part (the lowest-numbered part crossed)", kind,
                           f"tick {trace[tn]['tick']} vblank {vb}: predicted parts {sorted(pp)}, counted {stp}")
    for fr, (pp, stp) in pellet_parts.items():
        # pellets hit in list order; only hits that lower health are counted (a dead body isn't)
        kind = "exact" if pp == stp else "within" if stp <= pp else "fail"
        st.add("hits: pellets' parts (each the lowest-numbered crossed; counted ones among them)", kind,
               f"frame {fr}: predicted parts {sorted(pp)}, counted {sorted(stp)}")


# ---------------------------------------------------------------- damage

def fx3(v):
    return round(v * 1000)


def check_damage(st, dmg, fstate, trace):
    """damage_from_hit / apply_damage / enemy_hit_part on every logged event (combat.md, Damage):
    flat damage per projectile, health clamped at 0, then on a soldier a second event for the
    rest of the health when the hit was on the head (unless a helmet saved it) or left less than
    10% of the maximum."""
    renders = sorted(fstate)
    frame_of = lambda vb: renders[min(bisect.bisect_left(renders, vb), len(renders) - 1)]
    heads, followed_head, unfollowed = {}, Counter(), Counter()
    i = 0
    while i < len(dmg):
        d = dmg[i]
        fs = fstate.get(frame_of(d["vb"]))
        nxt = dmg[i + 1] if i + 1 < len(dmg) else None
        follow = nxt if nxt and nxt["att"] == d["att"] and nxt["victim"] == d["victim"] and nxt["vb"] == d["vb"] else None
        ok_math = fx3(d["after"]) == fx3(max(0.0, d["before"] - d["dmg"]))
        st.add("damage: health -= damage, clamped at 0", "exact" if ok_math else "fail",
               f"vblank {d['vb']}: {d}")
        if d["atype"] in (0x13F6, 0x13F7) and fs:
            w = d["weapon"]
            want = 2.0 if w == 4 else fs["weapons"][w][2] / FX
            ok = fx3(d["dmg"]) == fx3(want)
            st.add(f"damage: player projectile = the weapon's ({d['vtype']:X} victims)", "exact" if ok else "fail",
                   f"vblank {d['vb']} weapon {w}: {d['dmg']} vs {want}")
            if d["vtype"] == 0x1402 and d["before"] > 0:
                maxhp = fs["maxhp"].get(d["victim"], 0)
                after16 = round(d["after"] * FX) if d["dead"] or d["after"] > 0 else 1
                pct = trunc_div(after16 * 100, maxhp) if maxhp else 100
                low = not d["dead"] and pct < 10
                fr = frame_of(d["vb"])
                heads[fr] = fs["stats"].get(0xCC0, 0)
                if follow:
                    ok2 = fx3(follow["dmg"]) == fx3(follow["before"]) and fx3(follow["before"]) == fx3(d["after"])
                    if low:
                        st.add("damage: below 10% of maximum health -> the rest (second event)",
                               "exact" if ok2 else "fail", f"vblank {d['vb']}")
                    else:
                        followed_head[fr] += 1
                        st.add("damage: head hit -> the rest (second event)", "exact" if ok2 else "fail",
                               f"vblank {d['vb']}")
                    i += 1
                else:
                    if low:
                        st.add("damage: below 10% of maximum health -> the rest (second event)", "fail",
                               f"vblank {d['vb']}: no second event, {pct}% left")
                    elif not d["dead"]:
                        unfollowed[fr] += 1
        elif d["atype"] == 0x13F4:
            st.add("damage: enemy bullet on the player (the soldier record's value)", "within")
            st.enemy_dmg[(fs["level"] if fs else "?", d["dmg"])] += 1
        elif d["atype"] == 0x31 and d["vtype"] == 1 and fx3(d["dmg"]) == 1000000:
            # player_hit_part: an explosion within 300 (dist2d_approx: |dx| + |dz| - min/2) sets
            # its part's damage to 1000 (combat.md, The player being hit)
            near = False
            for t in trace:
                if t["vblank"] != d["vb"]:
                    continue
                pl = [e for e in t["ents"] if e[1] == 1]
                for e in (e for e in t["ents"] if e[1] == 0x31):
                    if pl:
                        dx, dz = abs((pl[0][3] >> 16) - (e[3] >> 16)), abs((pl[0][5] >> 16) - (e[5] >> 16))
                        near |= dx + dz - (min(dx, dz) >> 1) < 300
            st.add("damage: explosion on the player within 300, 1000", "exact" if near else "fail", f"{d}")
        elif d["atype"] == 0x31:
            st.add("damage: explosion, 4.0 per tick", "exact" if fx3(d["dmg"]) == 4000 else "fail", f"{d}")
        elif d["atype"] == 0x1405:
            st.add("damage: dog bite, 2.0", "exact" if fx3(d["dmg"]) == 2000 else "fail", f"{d}")
        i += 1
    for fr, h in heads.items():
        # the head counter against the head kills: the difference are heads that survived (a
        # saving helmet knocked off, 50% while it's on); a kill needs no counter beyond the hits
        surv = h - followed_head[fr]
        if surv == 0:
            st.add("damage: every counted head hit kills (second event), unless a helmet saves", "exact")
        elif 0 < surv <= unfollowed[fr]:
            st.add("damage: every counted head hit kills (second event), unless a helmet saves", "within")
            st.helmets += surv
        else:
            st.add("damage: every counted head hit kills (second event), unless a helmet saves", "fail",
                   f"frame {fr}: head counter {h}, head kills {followed_head[fr]}, surviving hits {unfollowed[fr]}")

def check(trace_path, log_path, seeds, to, frm, verbose, level=None):
    st = Stats()
    frames, shots, dmg, marks = load_log(log_path, to, frm)
    if level:
        # one level played from --from on, whatever the recording's marks say
        marks = [(frm, level)]
    trace = load_trace(trace_path, to, frm)
    print(f"{len(trace)} ticks, {len(frames)} watched frames, {len(shots)} player bullets logged, "
          f"{len(dmg)} damage events")
    # A frame's ticks run before its drawing pass, where the watch logs; a tick's vblank (when its
    # state was written) can be earlier than the drawing pass's when a frame straddles a vblank.
    renders = sorted(frames)
    ticks_at = defaultdict(list)
    for n, t in enumerate(trace):
        t["n"] = n
        i = bisect.bisect_left(renders, t["vblank"])
        t["frame"] = renders[i] if i < len(renders) else t["vblank"]
        ticks_at[t["frame"]].append(t)
    for ts in ticks_at.values():
        for k, t in enumerate(ts):
            t["last"] = k == len(ts) - 1
            t["first"] = k == 0
            t["nframe"] = len(ts)
    seeds = sorted(seeds)
    ram = Ram()
    seeded = False
    models = {}
    last_out = {}
    ctx = dict(st=st, verbose=verbose, level="?")
    targets = {}            # tick index -> hit-test targets (characters and the player)
    fstate = {}             # vblank -> the watched player state after the frame
    level_of = {}
    for vb in sorted(set(frames) | set(ticks_at)):
        # a dump at V is taken before frame V's ticks: it seeds before V's changes
        while seeds and vb >= seeds[0][0]:
            ram = Ram(seeds[0][1])
            models = models_by_root(ram)
            ctx["player_parts"] = player_templates(ram)
            seeded = True
            seeds.pop(0)
            last_out = {}
        for mv, lv in marks:
            if mv <= vb:
                ctx["level"] = lv
        if not seeded:
            continue
        pre = View(bytes(ram.m))
        stats_d = Counter()
        for addr, old, new, name in frames.get(vb, ()):
            if ram.u32(addr) != old:
                near = any(0 <= vb - mv < 12 for mv, _ in marks)
                st.add("watch replay onto the seed dumps (old values)", "within" if near else "fail",
                       f"vblank {vb} {name} {addr:08X}: {ram.u32(addr):08X} vs {old:08X}")
            else:
                st.add("watch replay onto the seed dumps (old values)", "exact")
            if name == "st":
                stats_d[addr & 0xFFF] += s32(new - old)
            ram.put32(addr, new)
        ctx.update(ram=ram, pre=pre, models=models, vb=vb, ticked=bool(ticks_at.get(vb)))
        for i, e in enumerate(SLOTS):
            check_slot(ctx, i, e, last_out)
        sol = ram.u32(0x8009D654)
        fstate[vb] = dict(pre_script=pre.s16(sol + 0x11E), script=ram.s16(sol + 0x11E), pre_iv=[pre.s32(sol + 0x3CC + o) for o in range(0, 0xA0, 4)],
                          iv=[ram.s32(sol + 0x3CC + o) for o in range(0, 0xA0, 4)],
                          ia=[ram.s32(sol + 0x55C + o) for o in range(0, 0x80, 4)],
                          pad=ram.u32(0x80093B7C), pad_prev=ram.u32(0x80093B84), mask=(ram.u32(0x800B7790), ram.u32(0x800B77E0)),
                          use=(ram.u32(0x800B7790 + 20), ram.u32(0x800B77E0 + 20)),
                          holster=(ram.u32(0x800B7790 + 8), ram.u32(0x800B77E0 + 8)),
                          stats=stats_d, level=ctx["level"],
                          weapons={w: (s16(ram.u32(0x80093640 + 0x44 * w + 4)), ram.u16(0x80093640 + 0x44 * w + 0x2A),
                                       ram.s32(0x80093640 + 0x44 * w + 0x2C), ram.s16(0x80093640 + 0x44 * w + 0xA))
                                   for w in range(13)},
                          maxhp={e: ram.s32(e + 0xF0) for e in SLOTS})
        for t in ticks_at.get(vb, []):
            targets[t["n"]] = tick_targets(ctx, t)
            level_of[t["n"]] = ctx["level"]
    check_trigger(st, trace, fstate)
    check_aim(st, trace, shots, fstate, targets, dmg)
    check_bullets(st, trace, targets, shots, dmg, fstate, verbose, level_of)
    check_damage(st, dmg, fstate, trace)
    st.report()
    print()
    st.extras()


def player_templates(ram):
    p = ram.u32(0x8009D654)
    pp = ram.u32(p + 0x304)
    out = []
    for i in range(ram.u16(p + 0x36)):
        q = pp + 0x48 * i
        out.append(dict(kind=ram.s32(q + 8), a=ram.u16(q + 0x1C), b=ram.u16(q + 0x1E), r2=ram.s32(q + 0x38),
                        off=(ram.s16(q + 0x40), ram.s16(q + 0x42), ram.s16(q + 0x44)),
                        offw=ram.u32(q + 0x40), offz=ram.s16(q + 0x44)))
    return dict(parts=out, local=(ram.s16(p + 0x2D8), ram.s16(p + 0x2DA), ram.s16(p + 0x2DC)),
                radius=ram.u16(p + 0x2E8))


def tick_targets(ctx, t):
    """Every character's hit parts and sphere in tick t, as the collision pass of that tick sees
    them: the pose drawn last (the frame's watched joints), the tick's angles and position."""
    ram, models = ctx["ram"], ctx["models"]
    ents = {e[0]: e for e in t["ents"]}
    order = {e[0]: k for k, e in enumerate(t["ents"])}
    out = []
    for i, e in enumerate(SLOTS):
        if ram.s32(e + 8) not in (0x1402, 0x1405):
            continue
        h = ram.u32(e)
        te = ents.get(h)
        inst = e + 0xFBC
        if te is None or ram.u32(inst + 0x84) not in models:
            continue
        model = models[ram.u32(inst + 0x84)]
        parts, m, tt, joints = slot_parts(ram, e, inst, model, te[6:9], te[3:6])
        bone = ram.s16(e + 0x2EA)
        local = joints.get(bone, (0, 0, 0)) if bone >= 0 else [ram.s16(e + 0x2D8 + 2 * k) for k in range(3)]
        sph = [s16(x) for x in to_world(m, tt, local)]
        out.append(dict(slot=i, addr=e, handle=h, order=order[h], flags_a=te[2] & 0xFFFFFFFF,
                        flags_b=ram.u32(e + 0x10), sphere=sph, radius=ram.u16(e + 0x2E8),
                        parts=[(c, p["r2"], p["kind"]) for c, p in zip(parts, model.parts)],
                        health=te[9], type=te[1]))
    pl = [e for e in t["ents"] if e[1] == 1]
    cam = [e for e in t["ents"] if e[1] == 2]
    # a dead player (player_death_start: flags A 0x20000, the hit and touch handlers cleared;
    # movement.md, Death) stops nothing
    if pl and "player_parts" in ctx and not pl[0][2] & 0x20000:
        e = pl[0]
        pt = ctx["player_parts"]
        m = rot_yxz(*[a >> 16 for a in e[6:9]])
        tt = [p >> 16 for p in e[3:6]]
        eye = (cam[0][4] >> 16) - tt[1] if cam else -240
        parts = [(to_world(m, tt, part_centre_model(p, {}, None, eye)), p["r2"], p["kind"]) for p in pt["parts"]]
        my = rot_yxz(0, e[7] >> 16, e[8] >> 16)
        sph = [s16(x) for x in to_world(my, tt, pt["local"])]
        out.append(dict(slot="player", addr=None, handle=e[0], order=order[e[0]], flags_a=e[2] & 0xFFFFFFFF,
                        flags_b=0, sphere=sph, radius=pt["radius"], parts=parts, health=e[9], type=1))
    out.sort(key=lambda x: x["order"])
    return out


def check_slot(ctx, i, e, last_out):
    ram, pre, st, models = ctx["ram"], ctx["pre"], ctx["st"], ctx["models"]
    if ram.s32(e + 8) not in (0x1402, 0x1405) or ram.u32(e + 0xC) & 0x80000:
        return
    inst = e + 0xFBC
    if ram.u32(inst + 0x84) not in models:
        return
    model = models[ram.u32(inst + 0x84)]
    nb = ram.u16(inst + 0x72)
    qp, jp = ram.u32(inst + 0x88), ram.u32(inst + 0x94)
    if not (ram.ok(qp) and ram.ok(jp)):
        return
    quats = slot_quats(ram, qp, nb)
    joints = slot_joints(ram, jp, nb)
    cache = (ram.s16(inst + 0x62), ram.s16(inst + 0x64))
    out = (tuple(quats), tuple(sorted(joints.items())), cache, qp)
    key = (i, ctx["level"])
    changed = last_out.get(key) is not None and last_out[key] != out
    last_out[key] = out
    vb = ctx["vb"]
    if changed and pre.s16(inst + 0x5C) >= 0 and cache[0] >= 0:
        exp = expected_pose(pre, inst, model, nb)
        if exp:
            anim, frame, q, root, c = exp
            tag = f"vblank {vb} slot {i} anim {anim} frame {frame} blend {c}"
            st.add("pose cache = the animation and frame at the draw", "exact" if cache == (anim, frame) else "fail", tag + f": cache {cache}")
            if q == "same":
                ok = quats == slot_quats(pre, pre.u32(inst + 0x88), nb)
                st.add("pose kept when the cache matches", "exact" if ok else "fail", tag)
            elif q is None:
                ok = qp == pre.u32(inst + 0x8C)
                st.add("pose: fade start shows the saved pose", "exact" if ok else "fail", tag)
            else:
                bad = [j for j in range(len(q)) if tuple(q[j]) != quats[j]]
                key2 = "pose from the animation (fading)" if c else "pose from the animation (frame, key interpolation)"
                st.add(key2, "exact" if not bad else "fail",
                       tag + f": {len(bad)} bones differ, e.g. {[(j, q[j], quats[j]) for j in bad[:2]]}")
            if ram.u32(e + 0x29C) or ram.u32(e + 0x298):
                nq = pre.u8(model.frame(anim, frame) + 0xF)
                js = model.joints(quats, root, nq)
                bad = [j for j in js if j < nb and js[j] != joints[j]]
                st.add("joints from the pose (skeleton walk)", "exact" if not bad else "fail",
                       tag + f": {len(bad)} joints differ, e.g. {[(j, js[j], joints[j]) for j in bad[:2]]}")
    # the hit parts after the frame's last tick; a frame without one (paused, or a menu over
    # the level) draws, rebuilding the joints, but places no parts: they keep the last tick's
    if not ctx.get("ticked", True):
        return
    angles = [ram.s32(e + 0x228 + 4 * k) for k in range(3)]
    pos = [ram.s32(e + 0x20C + 4 * k) for k in range(3)]
    parts, m, t, _ = slot_parts(ram, e, inst, model, angles, pos, joints, quats)
    rm = ent_matrix(ram, e)
    st.add("entity matrix = RotMatrixYXZ(angles)", "exact" if rm == m else "fail", f"vblank {vb} slot {i}")
    pp = e + 0x106C
    worst, wj = 0, None
    for j, c in enumerate(parts):
        got = [ram.s32(pp + 0x48 * j + 0x20 + 4 * k) for k in range(3)]
        d = max(abs(a - b) for a, b in zip(c, got))
        if d > worst:
            worst, wj = d, (j, c, got)
    st.add("hit part centres from joints + offset + matrix", "exact" if worst == 0 else "fail",
           f"vblank {vb} slot {i} type {ram.s32(e + 8):X} flags {ram.u32(e + 0xC):08X} drawn "
           f"{ram.u32(e + 0x298)}/{ram.u32(e + 0x29C)}: off by {worst}: part {wj}")
    bone = ram.s16(e + 0x2EA)
    local = joints.get(bone, (0, 0, 0)) if bone >= 0 else [ram.s16(e + 0x2D8 + 2 * k) for k in range(3)]
    sc = to_world(m, t, local)
    got = [ram.s16(e + 0x2E0 + 2 * k) for k in range(3)]
    if sc is not None:
        st.add("collision sphere centre on its bone (or fixed point)", "exact" if [s16(x) for x in sc] == got else "fail",
               f"vblank {vb} slot {i}: {sc} vs {got}")




def main():
    a = sys.argv[1:]
    v = "-v" in a
    a = [x for x in a if x != "-v"]
    to, frm, seeds, level = 1 << 62, 0, [], None
    while "--to" in a or "--from" in a or "--seed" in a or "--level" in a:
        i = next(k for k, x in enumerate(a) if x in ("--to", "--from", "--seed", "--level"))
        if a[i] == "--level":
            level = a[i + 1]
        elif a[i] == "--to":
            to = int(a[i + 1])
        elif a[i] == "--from":
            frm = int(a[i + 1])
        else:
            v, _, p = a[i + 1].partition(":")
            seeds.append((int(v), p))
        del a[i:i + 2]
    if not a:
        print(__doc__)
    elif a[0] == "pose":
        pose_cmd(a[1:], v)
    elif a[0] == "watch":
        print(watch_string())
    elif a[0] == "check":
        check(a[1], a[2], seeds, to, frm, v, level)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
