"""aim_update (0x8007E034; docs/re/combat.md, Aiming): the crosshair ray from the camera, swept in
three pieces against the world, shortened to the nearest entity it passes (aim friction past
1200 and 2750 units on characters), the aim point inv+0x1A4, where shots start (inv+0x198) and
the soldier's angular velocity steering the gun toward the crosshair. Translated from the
routine's code, step by step."""
import combat_sim as cs
import engine_gte as gt
from engine_camera import view_matrix
from engine_gte import s16, s32
from engine_sense import model_bone_point

G_PLAYER = 0x8009D638


def cheap(a, b, c):
    a, b, c = abs(a), abs(b), abs(c)
    if a < b:
        a, b = b, a
    if a < c:
        a, c = c, a
    return a + ((b + c) >> 2)


def aim_update(eng, e, inv):
    m = eng.m
    flags = m.u32(inv)
    if m.u32(inv + 0xA4) >= 0x191 and not flags & 0x200000:
        m.w32(e + 0x234, 0)
        m.w32(e + 0x238, 0)
        return
    stance = m.u32(inv + 0x1C8)
    if flags & 0x800000:
        if stance < 2:
            m.w32(e + 0x234, s32(m.s32(inv + 0x178) - 4 * m.s32(inv + 0x17C) + m.s32(inv + 0x180) - m.s32(e + 0x228)))
            yb = s32(m.s32(inv + 0x184) + 8 * m.s32(inv + 0x188))
        else:
            m.w32(e + 0x234, s32(m.s32(inv + 0x178) + m.s32(inv + 0x180) - m.s32(e + 0x228)))
            yb = m.s32(inv + 0x184)
        m.w32(e + 0x238, s32(yb + m.s32(inv + 0x18C) - m.s32(e + 0x22C)))
        return
    cam = m.u32(G_PLAYER)
    co = m.u32(cam + 0x388)
    M, t = view_matrix(m, co)
    T = gt.transpose(M)
    for i in range(3):
        v = T[i][1] << 6
        q = abs(v) // 25
        T[i][1] = s16(q if v >= 0 else -q)
    v = [s16((((m.s16(inv + 0x190) - 0x100) << 6) - t[0]) >> 2),
         s16((((m.s16(inv + 0x192) - (m.s16(inv + 0x4E) + 0x78)) << 6) - t[1]) >> 2),
         s16(((m.u32(inv + 0xA4) << 6) - t[2]) >> 2)]
    P, _ = gt.mul(T, v, sf=12)
    C = [m.s32(cam + 0x20C + 4 * k) for k in range(3)]
    if any(abs(x) >= 0x4001 for x in P):
        end = [s32(C[k] + (s32(s32((P[k] >> 2) << 16) - (C[k] >> 2)) << 1)) for k in range(3)]
    else:
        end = [s32(C[k] + (s32(s32(P[k] << 16) - C[k]) >> 1)) for k in range(3)]
    B0 = [s32(C[k] + (s32(end[k] - C[k]) >> 3)) for k in range(3)]
    C0 = [s32(C[k] + (s32(end[k] - C[k]) >> 1)) for k in range(3)]
    hit = None
    for a, b in ((B0, C), (C0, B0), (end, C0)):
        hit = eng.world.sweep(tuple(x >> 16 for x in a), tuple(x >> 16 for x in b))
        if hit is not None:
            break
    aim = list(hit[0]) if hit is not None else [s16(x >> 16) for x in end]
    flags &= ~0x100000 & 0x7FFFFFFF
    Ci = [s16(x >> 16) for x in C]
    d = [s32(aim[k] - Ci[k]) for k in range(3)]
    ds = [s16(x) for x in d]
    length = cheap(*d)
    sq, _ = gt.sqr(ds, 0)
    len2 = s32(sq[0] + sq[1] + sq[2])
    u = gt.vector_normal(ds)
    best = length
    for o in eng.entities():
        if o == e or m.u32(o + 0xC) & 0x20000000 or m.u32(o + 0x10) & 0x206:
            continue
        centre = [m.s16(o + 0x2E0 + 2 * k) for k in range(3)]
        if cs.segment_sphere(Ci, u, centre, m.u16(o + 0x2E8)) is None:
            continue
        ok = True
        if m.u32(o + 0xC) & 0x10400:
            if m.u16(o + 0x2EE):
                from engine_planes import segment_box
                ok = segment_box(eng, Ci, aim, o) is not None
            else:
                pp = m.u32(o + 0x304)
                parts = [([m.s32(pp + 0x48 * i + 0x20 + 4 * k) for k in range(3)], m.s32(pp + 0x48 * i + 0x38),
                          m.s32(pp + 0x48 * i + 8)) for i in range(m.u16(o + 0x36))]
                ok = cs.segment_parts(Ci, u, len2, parts) is not None
        if not ok:
            continue
        dd = cheap(s32(C[0] - m.s32(o + 0x20C)) >> 16, s32(C[1] - m.s32(o + 0x210)) >> 16,
                   s32(C[2] - m.s32(o + 0x214)) >> 16)
        if dd < best:
            best = dd
            if dd >= 0x4B0 and m.u32(o + 0xC) & 0x50:
                flags |= 0x100000
                if dd >= 0xABE:
                    flags |= 0x80000000
    if best != length:
        den = cheap(*d) + 1
        aim = [s16(Ci[k] + _cdiv(d[k] * best, den)) for k in range(3)]
    m.w32(inv, flags)
    for k in range(3):
        m.w32(inv + 0x1A4 + 4 * k, s32(aim[k] << 16))
    # the gun's two points (gun_points 0x80044E0C): near (bone +0xD4, offset +0xD8), far (+0xD6, +0xE0)
    model = m.u32(m.u32(e + 0x100) + 8)
    near = model_bone_point(eng, e, m.s16(model + 0xD4), tuple(m.s16(model + 0xD8 + 2 * k) for k in range(3)))
    far = model_bone_point(eng, e, m.s16(model + 0xD6), tuple(m.s16(model + 0xE0 + 2 * k) for k in range(3)))
    far32 = [s32(x << 16) for x in far]
    ax = abs(s32(C[0] - m.s32(inv + 0x1A4)) >> 2)
    az = abs(s32(C[2] - m.s32(inv + 0x1AC)) >> 2)
    t5 = ax + az - (min(ax, az) >> 1)
    fx = abs(s32(C[0] - far32[0]) >> 2)
    fz = abs(s32(C[2] - far32[2]) >> 2)
    a3 = fx + fz - (min(fx, fz) >> 1)
    s1 = cheap(s32(C[0] - m.s32(inv + 0x1A4)), s32(C[1] - m.s32(inv + 0x1A8)), s32(C[2] - m.s32(inv + 0x1AC)))
    if t5 < a3 + 0x140000:
        for k in range(3):
            m.w32(inv + 0x198 + 4 * k, C[k])
        m.w32(inv, m.u32(inv) | 0x20000)
        sx, sy = gt.rtps(M, t, m.u32(inv + 0xA4) & 0xFFFF, 256 << 16, 120 << 16, far)
        s0 = m.s16(inv + 0x190) - sx
        m.w32(e + 0x238, s32(s0 << 16) if abs(s0) >= 9 else s32(s0 << 16) >> 1)
        s0 = sy - m.s16(inv + 0x192)
        if abs(s0) >= 9:
            av = s32((s0 + (s0 >> 2)) << 16)
        elif abs(s0) >= 5:
            av = s32(s0 << 16)
        else:
            av = s32(s0 << 16) >> 1
        m.w32(e + 0x234, av)
    else:
        start = C if m.u32(inv) & 0x200000 else far32
        for k in range(3):
            m.w32(inv + 0x198 + 4 * k, start[k])
        m.w32(inv, m.u32(inv) & 0xFFFDFFFF)
        g = gt.vector_normal([far[k] - near[k] for k in range(3)])
        a = gt.vector_normal([aim[k] - near[k] for k in range(3)])
        s0 = _wrap(cs.ratan2(g[2], g[0]) - cs.ratan2(a[2], a[0]))
        s0 = _scale(s0, s1)
        m.w32(e + 0x238, s32(s0 << 16))
        hg = cs.sqrt0(s32(g[0] * g[0] + g[2] * g[2]) & 0xFFFFFFFF)
        ha = cs.sqrt0(s32(a[0] * a[0] + a[2] * a[2]) & 0xFFFFFFFF)
        s0 = _wrap(cs.ratan2(g[1], hg) - cs.ratan2(a[1], ha))
        s0 = _scale(s0, s1)
        bp = m.s32(e + 0x228)
        v1 = s32(bp + (s0 << 16))
        if 0x2AA0000 < v1:
            v1 = 0x2AA0000
        if not s32(0xFD560000) < v1:
            v1 = s32(0xFD560000)
        m.w32(e + 0x234, s32((s32(s32(v1 * 8) - v1 + bp) >> 3) - bp))
    bp, by = m.s32(e + 0x228), m.s32(e + 0x22C)
    ap, ay = m.s32(e + 0x234), m.s32(e + 0x238)
    if stance < 2:
        m.w32(inv + 0x180, s32(bp + ap - (m.s32(inv + 0x178) - 4 * m.s32(inv + 0x17C))))
        m.w32(inv + 0x18C, s32(by + ay - (m.s32(inv + 0x184) + 8 * m.s32(inv + 0x188))))
    else:
        m.w32(inv + 0x180, s32(bp + ap - m.s32(inv + 0x178)))
        m.w32(inv + 0x18C, s32(by + ay - m.s32(inv + 0x184)))


def _wrap(v):
    v &= 0xFFF
    return v - 0x1000 if v >= 0x801 else v


def _scale(s0, s1):
    if s1 <= 0x257FFFF:
        s0 >>= 2
    elif s1 <= 0x63FFFFF:
        s0 >>= 1
    elif s1 <= 0xC7FFFFF:
        s0 -= s0 >> 2
    if abs(s0) < 8:
        s0 >>= 2
    return s0


def _cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q
