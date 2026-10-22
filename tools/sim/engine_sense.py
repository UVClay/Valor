"""What model scripts read from the world (docs/re/enemies.md Variables, op 2E query; soldier.md):
sight, hearing, angles, routes, probes, nodes. Each function follows the original routine named
in its docstring."""
import math

import combat_sim as cs
from engine import s16, s32
from sst_vm import dist_approx

G_SOLDIER = 0x8009D654
G_PLAYER = 0x8009D638
G_VOICES = 0x8009115C
G_VOICE_ENABLED = 0x800910D0


def wrap(a):
    """0x80056E40: an angle into -0x800..0x800."""
    a = s16(a)
    while a < -0x800:
        a = s16(a + 0x1000)
    while a >= 0x801:
        a = s16(a - 0x1000)
    return a


def angle_to(m, e, t):
    """angle_to (0x8003F858): from e's heading to t, -0x800..0x800."""
    h = m.s16(e + 0x22E)
    dx = s32(m.s32(t + 0x20C) - m.s32(e + 0x20C)) >> 16
    dz = s32(m.s32(t + 0x214) - m.s32(e + 0x214)) >> 16
    v = cs.ratan2(dx, dz) - h
    while v < -0x800:
        v += 0x1000
    v = s16(v)
    while v >= 0x801:
        v = s16(v - 0x1000)
    return v


def nearest(m, e, typ):
    """0x80057768: the nearest live entity of a type within 1000 vertically."""
    best, bd = 0, 0x7FFF
    for o in m.entities():
        if m.s32(o + 8) == s32(typ) and o != e and not m.u32(o + 0xC) & 0x20080000:
            d = [s16((m.s32(e + 0x20C + 4 * k) - m.s32(o + 0x20C + 4 * k)) >> 16) for k in range(3)]
            dd = dist_approx(*d) & 0xFFFF
            if dd < (bd & 0xFFFF) and abs(d[1]) < 1000:
                best, bd = o, dd
    return best


def find_type_group(m, typ, grp):
    """entity_find_type_group (0x8006D600): the first live one of that type and +0x35 group."""
    for o in m.entities():
        if m.s32(o + 8) == s32(typ) and m.s8(o + 0x35) == s32(grp << 24) >> 24 \
                and not m.u32(o + 0xC) & 0x20080000:
            return o
    return 0


def ent_matrix(m, e):
    return [[m.s16(e + 0x340 + 6 * i + 2 * j) for j in range(3)] for i in range(3)]


def model_bone_point(eng, e, bone, off=None):
    """model_bone_point (0x8008DABC): bone's joint (+ its rotation times off), into the world by
    the entity's matrix and integer position."""
    m = eng.m
    inst = m.u32(e + 0x100)
    if not inst or not m.u32(inst + 0x84):
        return [s16(m.s32(e + 0x20C + 4 * k) >> 16) for k in range(3)]
    jp = m.u32(inst + 0x94)
    j = [m.s16(jp + 8 * bone + 2 * k) for k in range(3)]
    if off is not None and any(off):
        qp = m.u32(inst + 0x88) or m.u32(inst + 0x8C)
        if qp:
            q = tuple(m.s16(qp + 8 * bone + 2 * k) for k in range(4))
            r = cs.mvmva12(cs.quat_matrix(q), off)
            j = [s16(r[k] + j[k]) for k in range(3)]
    M = ent_matrix(m, e)
    t = [m.s32(e + 0x354 + 4 * k) for k in range(3)]
    w = [s16(v) for v in cs.to_world(M, t, j)]
    if m.s32(e + 8) == 1:
        # the player's first-person model hangs from the eye: the camera's eye offset is added
        w[1] = s16(w[1] + m.u16(m.u32(m.u32(G_PLAYER) + 0x388) + 0x94))
    return w


def vector_normal_ss(v):
    """VectorNormalSS: the vector scaled to length 4096 (rounded)."""
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if n == 0:
        return [0, 0, 0]
    return [int(round(x * 4096 / n)) for x in v]


def sight_test(eng, e, t, rng, blockers=True):
    """sight_test (0x8005A0C8): from the head bone (brain +0xAA) and its facing (the bone turned
    (-100, 0, 15)), the target's point (its eye height above its feet, halved when its flags B
    0x800) within the range and the field of view (brain +0xB4), no blocking entity (flags B
    0x4000) nearer on the line, and a clear world sweep."""
    m = eng.m
    b = m.u32(e + 0x38C)
    eye = m.s32(t + 0x2D4)
    if m.u32(t + 0x10) & 0x800:
        eye >>= 1
    if not b:
        eng.note("sight without a brain (0x80056D08) not implemented")
        return 0
    bone = m.s16(b + 0xAA)
    p0 = model_bone_point(eng, e, bone)
    p1 = model_bone_point(eng, e, bone, (-100, 0, 15))
    fwd = (s16(p1[0] - p0[0]), s16(p1[1] - p0[1]), s16(p1[2] - p0[2]))
    a0 = cs.ratan2(fwd[0], fwd[2])
    d = [s16(m.s16(t + 0x20E) - p0[0]), s16((s32(m.s32(t + 0x210) - eye) >> 16) - p0[1]),
         s16(m.s16(t + 0x216) - p0[2])]
    ang = wrap(s16(cs.ratan2(d[0], d[2]) - a0))
    ang = abs(ang)
    dist = dist_approx(*d)
    if not (dist < rng and ang < m.s16(b + 0xB4)):
        return 0
    if blockers:
        u = vector_normal_ss(d)
        for o in m.entities():
            if not (m.u32(o + 0x10) & 0x4000) or m.u32(o + 0xC) & 0x20080000:
                continue
            v = [s16(m.u16(o + 0x2E0 + 2 * k) - p0[k]) for k in range(3)]
            dv = dist_approx(*v)
            if rng < dv or not dv < dist:
                continue
            if s32(v[0] * u[0] + v[1] * u[1] + v[2] * u[2]) <= 0:
                continue
            c = cross12(v, u)
            r = m.u16(o + 0x2E8)
            if not r * r < c[0] * c[0] + c[1] * c[1] + c[2] * c[2]:
                return 0
    a = tuple(p0)
    tb = (m.s32(t + 0x20C) >> 16, s32(m.s32(t + 0x210) - eye) >> 16, m.s32(t + 0x214) >> 16)
    hit = eng.world.sweep(a, tb)
    return 1 if hit is None else 0


def cross12(r, ir):
    """GTE op sf=1: R (diagonal r) x IR, each component >> 12, saturated to s16."""
    sat = lambda v: max(-0x8000, min(0x7FFF, v))
    return [sat((r[1] * ir[2] - r[2] * ir[1]) >> 12), sat((r[2] * ir[0] - r[0] * ir[2]) >> 12),
            sat((r[0] * ir[1] - r[1] * ir[0]) >> 12)]


def sees(eng, w, v, long=False):
    m = eng.m
    b = m.u32(w + 0x38C)
    if not b:
        return False
    return bool(sight_test(eng, w, v, m.s16(b + (0xB2 if long else 0xB0))))


def hearing(eng, e):
    """Query 0x11 (op_query): a voice 1..23 enabled, one-shot, younger than 60 frames, positional
    and heard (flags & 3 == 0), not the entity's own, within the sight range (3000 without a
    brain) of the entity, by dist_approx from the voice's position."""
    m = eng.m
    b = m.u32(e + 0x38C)
    rng = m.u16(b + 0xB0) if b else 0xBB8
    p = [m.s16(e + 0x20E), m.s16(e + 0x212), m.s16(e + 0x216)]
    for v in range(1, 24):
        a = G_VOICES + 0x34 * v
        if m.u8(a + 0xE) != 0 or not m.u8(G_VOICE_ENABLED + v) & 1 or m.u16(a + 0x28) >= 0x3C:
            continue
        if m.u32(a) & 3:
            continue
        h = m.u32(a + 4)
        if not eng.handle_dead(h) and h == m.u32(e):
            continue
        d = [s16(p[k] - m.u16(a + 0x2C + 2 * k)) for k in range(3)]
        if (dist_approx(*d) & 0xFFFF) < s16(rng):
            return 1
    return 0


def route_turn_kind(m, e, px, pz):
    """route_turn_kind (0x80056B00): 0 ahead, 1/2 the sides, 3 behind; and the signed angle (16.16)
    from the heading to the point."""
    fx, fz = m.s16(e + 0x344), m.s16(e + 0x350)
    dx, dz = s16(px - m.s16(e + 0x20E)), s16(pz - m.s16(e + 0x216))
    a2 = cs.ratan2(dx, dz)
    h = m.s32(e + 0x22C)
    while h < -0x8000000:
        h += 0x10000000
    while h > 0x8000000:
        h -= 0x10000000
    a0 = (h >> 16) - a2
    v1 = 0x1000 - a0
    if a0 < v1:
        v1 = a0
    out = s32(v1 << 16)
    a = abs(v1)
    ir2 = max(-0x8000, min(0x7FFF, (fz * dx - fx * dz) >> 12))
    if 0 <= a - 0x641 < 0x2BB:
        return 3, out
    if 0 <= a - 0x259 < 0x383:
        return (1 if ir2 < 0 else 2), out
    return 0, out


def brain_node_target(m, e, write, node):
    """brain_node_target (0x80057474): the point to stand at and the side waypoint for a node;
    `write(field, value)` stores into the brain given (op_route_node passes a scratch one).
    Reads the node table at the given index whatever it is (-1 included), as the game does."""
    nodes = m.u32(m.u32(m.u32(0x8009D648) + 0x388) + 8)
    nd = (nodes + 0x34 * node) & 0xFFFFFFFF
    fx, fz = m.s16(nd + 0x24), m.s16(nd + 0x28)
    side = m.s32(nd + 0x10)
    kind = m.s32(nd + 0xC)
    px, py, pz = m.s16(nd + 0x14), m.s16(nd + 0x16), m.s16(nd + 0x18)
    d = [s16(px - m.s16(e + 0x20E)), s16(py - m.s16(e + 0x212)), s16(pz - m.s16(e + 0x216))]
    via, sx, sz, ox, oz = 1, 0, 0, 0, 0
    if side != 4:
        if abs(fz) < abs(fx):
            if fx < 0:
                if side == 1:
                    sz, oz = 0x64, -0x28
                elif side == 2:
                    sz, oz = -0x64, 0x28
                else:
                    via = 0
            elif fx > 0:
                if side == 1:
                    sz, oz = -0x64, 0x28
                elif side == 2:
                    sz, oz = 0x64, -0x28
                else:
                    via = 0
            else:
                via = 0
        else:
            if fz < 0:
                if side == 1:
                    sx, ox = -0x64, 0x28
                elif side == 2:
                    sx, ox = 0x64, -0x28
                else:
                    via = 0
            elif fz > 0:
                if side == 1:
                    sx, ox = 0x64, -0x28
                elif side == 2:
                    sx, ox = -0x64, 0x28
                else:
                    via = 0
            else:
                via = 0
    else:
        via = 0
    if dist_approx(*d) < 0xFA:
        via = 0
    if kind != 8:
        write("node_point", (s16(px + ox - ((fx * 5) >> 10)), py, s16(pz + oz - ((fz * 5) >> 10))))
        write("side", (s16(sx + px + ox - ((fx * 0x19) >> 12)), s16(sz + pz + oz - ((fz * 0x19) >> 12))))
    else:
        write("node_point", (px, py, pz))
        write("side", (sx, sz))
    write("via", via)
    write("claimed", node)
    return via


def probe(eng, e, how, r):
    """0x800579F4 (op 3E): a point r away in the entity's frame (1: -x, 2: +x, 4: +z, 8: -z), at
    half its eye height (full with bit 0x10), swept back to the entity: hit -> 0; with flags B
    0x200000 a drop of more than 400 below there -> 0; else r + 1."""
    m = eng.m
    k = how & 0xF
    v = {1: (-r, 0, 0), 2: (r, 0, 0), 4: (0, 0, r), 8: (0, 0, -r)}.get(k)
    if v is None:
        return 0
    M = ent_matrix(m, e)
    t = [m.s32(e + 0x354 + 4 * j) for j in range(3)]
    p = cs.to_world(M, t, [s16(x) for x in v])
    eye = m.s32(e + 0x2D4) if how & 0x10 else m.s32(e + 0x2D4) >> 1
    y = s32(m.s32(e + 0x210) - eye)
    a = (s16(p[0]), y >> 16, s16(p[2]))
    b = (m.s32(e + 0x20C) >> 16, y >> 16, m.s32(e + 0x214) >> 16)
    if eng.world.sweep(a, b) is not None:
        return 0
    if m.u32(e + 0x10) & 0x200000:
        f, z = eng.world.floor((a[0] << 16, a[1] << 16, a[2] << 16))
        if s16(m.s16(e + 0x212) - f) < -0x190:
            return 0
    return s16(r) + 1


def compute_node(eng, e, rng, exclude, mask):
    """0x80040F48 (op 33 compute 0): over the world's nodes (0x34 bytes: +0xC kind bits, +0x14
    position, +0x24 direction, +0x30 holder), the nearest (max + (mid + min) / 4 from the soldier,
    within |rng|) that is not `exclude`, free (its holder's handle dead; with exclude -1 any),
    of a kind in mask (mask -1: any), with the soldier nearer to it than the player and the player
    within 10000 of it, and the player in front of it ((player - node) . direction, y left out,
    >> 12, at least 101); mask 8 waives the last two tests but the 10000. -1 if none."""
    m = eng.m
    wo = m.u32(m.u32(0x8009D648) + 0x388)
    n, base = m.s32(wo + 0xC), m.u32(wo + 8)
    best, rng = -1, abs(s32(rng))
    p = m.u32(G_SOLDIER)

    def cheap(a, q):
        d = sorted(abs(s16(m.s16(a + 0x20E + 4 * k) - m.u16(q + 0x14 + 2 * k))) for k in range(3))
        return d[2] + ((d[0] + d[1]) >> 2)
    for i in range(max(n, 0)):
        q = base + 0x34 * i
        if i == s32(exclude):
            continue
        if not eng.handle_dead(m.u32(q + 0x30)) and s32(exclude) != -1:
            continue
        t = cheap(e, q)
        if rng < t:
            continue
        if not m.u32(q + 0xC) & mask and s32(mask) != -1:
            continue
        tp = cheap(p, q)
        if not (t < tp or s32(mask) == 8):
            continue
        if not tp < 0x2710:
            continue
        dx = s16(m.s16(p + 0x20E) - m.u16(q + 0x14))
        dz = s16(m.s16(p + 0x216) - m.u16(q + 0x18))
        dot = s32(dx * m.s16(q + 0x24) + dz * m.s16(q + 0x28)) >> 12
        if dot < 0x65 and s32(mask) != 8:
            continue
        best, rng = i, t
    return best


# ---------------------------------------------------------------- the VM's hooks

import os
TRACE = os.environ.get("ENG_TRACE_SENSE")


def world_value(vm, op, var):
    v = _world_value(vm, op, var)
    if TRACE:
        lo, hi = (int(x) for x in TRACE.split("-"))
        if lo <= vm.eng.tick_no <= hi:
            print(f"  sense tick {vm.eng.tick_no} {vm.e:08X} op {op:X} var {var:X} -> {v}")
    return v


def _world_value(vm, op, var):
    """A world read of the VM (sst_vm.VM.world): op is the opcode, 0x2E00 | fact for queries."""
    eng, m, e = vm.eng, vm.m, vm.e
    if op == 0x2EC:
        op = 0x2E0C
    if op & 0xFF00 == 0x2E00:
        fact = op & 0xFF
        tgt = vm._query_target
        b = m.u32(e + 0x38C)
        crouch = m.u32(m.u32(G_SOLDIER) + 0x388) & 2
        if fact in (0xB, 0x10, 0x12):
            if not tgt:
                return 0
            if not b:
                eng.note("sight without a brain not implemented")
                return 0
            rng = m.s16(b + (0xB0 if fact == 0xB else 0xB2))
            if crouch and fact != 0x12:
                rng = (rng & 0xFFFF) - (s32((rng & 0xFFFF) << 16) >> 18)
            return sight_test(eng, e, tgt, s16(rng))
        if fact == 0x11:
            return hearing(eng, e)
        if fact == 0xC:
            return abs(angle_to(m, e, tgt)) if tgt else 0
        if fact == 0xF:
            return aimed_at(eng, e)
        eng.note(f"query fact {fact:X} not implemented")
        return 0
    eng.note(f"script world read op {op:X} not implemented")
    return 0


def aimed_at(eng, e):
    """Query 0xF (0x80056F38 -> 0x80056F60): the player's gun points at the entity: the line
    through the player's sphere centre along the gun (bone +0xD6 of the body model: the offset
    +0xE0 turned by the bone's quaternion, from its joint, and 100 further along z, both through
    the soldier's matrix and integer translation, + the eye offset; without joints the weapon
    table's two bones) passes within the entity's radius (+0x2E8, else the brain's +0xC6) of its
    sphere centre (GTE outer product, sf=1)."""
    import engine_gte as gt
    m = eng.m
    pl = m.u32(G_SOLDIER)
    r = m.u16(e + 0x2E8)
    if not r and m.u32(e + 0x38C):
        r = m.u16(m.u32(e + 0x38C) + 0xC6)
    inst = m.u32(pl + 0x100)
    if m.u32(inst + 0x84):
        model = m.u32(inst + 8)
        bone = m.s16(model + 0xD6)
        q = tuple(m.s16(m.u32(inst + 0x88) + 8 * bone + 2 * k) for k in range(4))
        R = cs.quat_matrix(q)
        j = [m.s16(m.u32(inst + 0x94) + 8 * bone + 2 * k) for k in range(3)]
        off = [m.s16(model + 0xE0 + 2 * k) for k in range(3)]
        mac, _ = gt.mul(R, off, j, 12)
        p1 = [s16(x) for x in mac]
        p2 = [p1[0], p1[1], s16(p1[2] + 100)]
        E = ent_matrix(m, pl)
        T = [m.s32(pl + 0x354 + 4 * k) for k in range(3)]
        eye = m.u16(m.u32(m.u32(G_PLAYER) + 0x388) + 0x94)
        a, _ = gt.mul(E, p1, T, 12)
        b, _ = gt.mul(E, p2, T, 12)
        a = [s16(x) for x in a]
        b = [s16(x) for x in b]
        a[1] = s16(a[1] + eye)
        b[1] = s16(b[1] + eye)
    else:
        w = m.u8(m.u32(pl + 0x388) + 0x55)
        a = model_bone_point(eng, pl, m.s16(0x80093640 + 0x44 * w + 0xC))
        b = model_bone_point(eng, pl, m.s16(0x80093640 + 0x44 * w + 0xE))
    u = gt.vector_normal([s16(b[k] - a[k]) for k in range(3)])
    d = [s16(m.s16(e + 0x2E0 + 2 * k) - m.s16(pl + 0x2E0 + 2 * k)) for k in range(3)]
    _, c = gt.op(d, u, 12)       # the offset on the diagonal, the direction in IR: d x u, floored
    return int(r * r >= c[0] * c[0] + c[1] * c[1] + c[2] * c[2])


def sensor(vm, vid):
    eng, m = vm.eng, vm.m
    if vid == 0x67:
        return int(eng.action_held(0))
    if vid == 0x71:
        eng.note("sensor 0x71 (the player looks at it) not implemented")
        return 0
    return 0
