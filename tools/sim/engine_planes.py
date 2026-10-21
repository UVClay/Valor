"""Objects with plane shapes (docs/re/objects.md, combat.md): the box of six planes a solid object
gets (0x8004A8F8), the segment test against it (0x8004B0F0), the bullet's touch on it
(bullet_touch, 0x80043AD4), the destructible's touch (destructible_touch, 0x8005D32C) and the
spawner of the generic objects (0x8005E7B0).

A plane record (0x1C bytes, the entity's list +0x300, +0x2EE of them): +0 flags (bits 0/1: a
surface a segment can hit), +4 the normal (3 x s16, 32767 long, in the object's frame), +0xA d
(the plane is n.p >> 15 = d), +0xC and +0x14 masks of the planes a hit point must be inside of."""
import engine_gte as gt
from engine import s16, s32

PLANES_TEMPLATE = 0x8003A238       # six records: top, bottom, -x, +x, -z, +z
DESTRUCTIBLE_TOUCH = 0x8005D32C


def register(eng):
    eng.handlers[("touch", DESTRUCTIBLE_TOUCH)] = lambda e, o: destructible_touch(eng, e, o)


def box_sphere(m, e, mn, mx):
    """0x8005D238 (and the end of 0x8004A8F8): the sphere around a box: radius (largest side +
    30) / 2 at the box's centre (+0x2D8, local)."""
    d = [abs(mx[k] - mn[k]) & 0xFFFF for k in range(3)]
    big = max(d[0], d[1])
    r = (big + 0x1E) if big >= d[2] else (d[2] + 0x1E)
    m.w16(e + 0x2E8, r >> 1)
    for k in range(3):
        m.w16(e + 0x2D8 + 2 * k, (mn[k] + (s32(d[k] << 16) >> 17)) & 0xFFFF)


def object_box(eng, e, mn, mx):
    """0x8004A8F8(e, min, max, -1): six planes from the template, their distances |min|-1 /
    |max|-1 per axis, the sphere, the box kept at +0x2F0 (min) and +0x2F8 (max)."""
    m = eng.m
    lst = m.u32(e + 0x300)
    m.w16(e + 0x2EE, 6)
    box_sphere(m, e, mn, mx)
    m.copy(lst, PLANES_TEMPLATE, 6 * 0x1C)
    for i, v in enumerate((mn[1], mx[1], mn[0], mx[0], mn[2], mx[2])):
        m.w16(lst + 0x1C * i + 0xA, abs(s16(v)) - 1)
    for k in range(3):
        m.w16(e + 0x2F0 + 2 * k, mn[k])
        m.w16(e + 0x2F8 + 2 * k, mx[k])


def to_local(m, o, p):
    """0x8004AEA0: a point into the object's frame: RotMatrixYXZ(-angles) . (p - position) (GTE,
    >> 12; the low halves kept)."""
    import brain_sim as bs
    r = bs.rot_yxz(s16(-m.s16(o + 0x22A)), s16(-m.s16(o + 0x22E)), s16(-m.s16(o + 0x232)))
    R = [r[0:3], r[3:6], r[6:9]]
    d = [s16(p[k] - m.s16(o + 0x20E + 4 * k)) for k in range(3)]
    mac, _ = gt.mul(R, d, sf=12)
    return [s16(x) for x in mac]


def segment_box(eng, a, b, o):
    """0x8004B0F0(a, b, object): the segment (integer points) against the object's planes in its
    frame. A plane is hit when the segment crosses it from its front (d0 >= 0, signs differ) with
    |d0| below the best so far, at t = |d0| << 12 / (|d0| + |d1|), the point a + (t . (b - a) >> 12)
    inside every plane its masks name (n.p >> 15 - d < 0). Returns (point, normal, flags) in the
    world (the point through the object's matrix and translation, the normal rotated) or None."""
    m = eng.m
    lst = m.u32(o + 0x300)
    n = m.u16(o + 0x2EE)
    la, lb = to_local(m, o, a), to_local(m, o, b)
    delta = [s16(lb[k] - la[k]) for k in range(3)]
    planes, ds, cross = [], [], 0
    for i in range(n):
        q = lst + 0x1C * i
        nv = [m.s16(q + 4 + 2 * k) for k in range(3)]
        d = m.s16(q + 0xA)
        d0 = (s32(sum(la[k] * nv[k] for k in range(3))) >> 15) - d
        d1 = (s32(sum(lb[k] * nv[k] for k in range(3))) >> 15) - d
        planes.append((q, nv, d))
        ds.append((d0, d1))
        if (d0 ^ d1) < 0:
            cross |= 1 << i
    best, hit = 0x7FFFFFFF, None
    for i, (q, nv, d) in enumerate(planes):
        if not m.u32(q) & 3 or not cross >> i & 1:
            continue
        d0, d1 = ds[i]
        if d0 < 0 or abs(d0) >= best:
            continue
        t = (abs(d0) << 12) // (abs(d0) + abs(d1))
        p = [s16(gt.sat16((t * delta[k]) >> 12) + la[k]) for k in range(3)]
        inside = True
        for j, (qj, nj, dj) in enumerate(planes):
            if j == i or not ((m.u32(q + 0xC) | m.u32(q + 0x14)) >> j & 1):
                continue
            if (s32(sum(p[k] * nj[k] for k in range(3))) >> 15) - dj >= 0:
                inside = False
                break
        if inside:
            best = abs(d0)
            hit = (p, nv, m.u32(q))
    if hit is None:
        return None
    M = [[m.s16(o + 0x340 + 6 * r + 2 * c) for c in range(3)] for r in range(3)]
    T = [m.s32(o + 0x354 + 4 * k) for k in range(3)]
    pw, _ = gt.mul(M, hit[0], T, 12)
    nw, _ = gt.mul(M, hit[1], (0, 0, 0), 12)
    return [s16(x) for x in pw], [s16(x) for x in nw], hit[2]


def projectile_planes(eng, e, o):
    """0x8004B624: the projectile's path this tick (+0x258 to +0x20C, integer) against the object's
    planes, unless it is attached (+0x18 bit 0)."""
    m = eng.m
    if not m.u16(o + 0x2EE) or m.u32(e + 0x18) & 1:
        return None
    a = [m.s16(e + 0x25A + 4 * k) for k in range(3)]
    b = [m.s16(e + 0x20E + 4 * k) for k in range(3)]
    return segment_box(eng, a, b, o)


def destructible_touch(eng, e, o):
    """destructible_touch (0x8005D32C), the object e touched by o: the player's melee part (kind 5,
    facing it, within reach: the part's damage), a projectile (not from flags-A-4 owners) or an
    explosion (type 0x31: its +0xF8): the damage, message 1 to its level script at once; at death
    the destroyed counter 0x80039D10 (once) and a drop for record +0x68; the material's sound."""
    m = eng.m
    fa = m.u32(o + 0xC)
    t = m.s32(o + 8)
    if not (fa & 0x101 or t == 0x31):
        return 0
    if fa & 4 and t != 0x31:
        return impact_material(eng, e)
    if fa & 1:
        pp = m.u32(o + 0x304)
        if m.s32(pp + 8) != 5:
            return 0
        eng.note("destructible hit by the player's melee not implemented")
        return 0
    from engine_combat import apply_damage
    apply_damage(eng, e, m.s32(o + 0xF8), o)
    ent = eng.ents.get(e)
    eng.logic.run_message(ent, 0, 1, "destructible hit")
    fe = m.u32(e + 0xC)
    if fe & 0x20000000 and not fe & 0x100000:
        m.w32(e + 0xC, fe | 0x100000)
        m.w32(0x80039D10, m.u32(0x80039D10) + 1)
        rec = m.u32(m.u32(e + 0x1C) + 4) if m.u32(e + 0x1C) else 0
        if rec and m.u32(rec + 0x68):
            eng.note("destructible drop (0x800838AC) not implemented")
    if t == 0x31:
        return 1
    return impact_material(eng, e)


MATERIAL = {0x69960D1C: 0x23, 0x4D682091: 0x23, 0x5C8D7E20: 0x23, 0xD2E7F597: 0x25, 0xE68BC914: 0x25,
            0xEBF2A379: 0x25, 0xA1EBF698: 0x25, 0x4A4B9B8A: 0x25, 0x5151A6DA: 0x27}


def impact_material(eng, e):
    """destructible_touch's sound by the object's model: wood 0x23, metal 0x25, glass 0x27."""
    m = eng.m
    inst = m.u32(e + 0x100)
    if inst:
        snd = MATERIAL.get(m.u32(m.u32(inst + 8)), -1)
        if snd != -1:
            eng.audio.play(0, snd, e, 0x20000, 0, 0x10000, 0x4000, 0, 0, 1, 0)
    return 1


def object_spawn(eng, rec, model):
    """0x8005E7B0, the generic object (types 0x1777, 0x1779, 0x177A...): no instance, six plane
    slots, the model's type, the record's health (+0x5C), position and angles (+0x264), the cull
    counter 5, a box of the record's half sizes (+0x2A..+0x2E); 0x1779 inert (a sphere only),
    the rest solid with destructible_touch."""
    m = eng.m
    r = rec.addr
    e = eng.entity_create(m.s32(model + 0xFC), list_n=6)
    g = m.u8(0x80099ECC)
    m.w8(0x80099ECC, g + 1)
    m.w8(e + 0x35, g)
    hp = m.u8(r + 0x5C) << 16
    m.w32(e + 0xF0, hp)
    m.w32(e + 0xF4, hp)
    for k in range(3):
        m.w32(e + 0x20C + 4 * k, s32(m.s16(r + 0x10 + 2 * k) << 16))
        m.w32(e + 0x264 + 4 * k, s32(m.s16(r + 0x18 + 2 * k) << 16))
        m.w32(e + 0x258 + 4 * k, s32(m.s16(r + 0x10 + 2 * k) << 16))
    m.w32(e + 0x294, 5)
    m.w32(e + 0x290, 5)
    m.w16(e + 0x2EC, 0)
    m.w16(e + 0x36, 0)
    m.w32(e + 0x36C, 0)
    m.w32(e + 0x370, 0x8005E760)
    m.w32(e + 0x374, 0)
    m.w32(e + 0x378, 0)
    m.w32(e + 0x384, 0x8006A37C)
    half = [abs(m.s16(r + 0x2A + 2 * k)) >> 1 for k in range(3)]
    mn, mx = [s16(-h) for h in half], [s16(h) for h in half]
    if m.s32(e + 8) == 0x1779:
        m.w32(e + 0x14, 0)
        m.w32(e + 0xC, 0x13028000)
        m.w32(e + 0x10, 0x40213)
        box_sphere(m, e, mn, mx)
        m.w32(e + 0x37C, 0)
        m.w16(e + 0x2EE, 0)
    else:
        m.w32(e + 0x10, 0x44011)
        m.w32(e + 0x14, 0x88214051)
        m.w32(e + 0xC, 0x13028400)
        m.w32(e + 0x37C, DESTRUCTIBLE_TOUCH)
        object_box(eng, e, mn, mx)
    return e
