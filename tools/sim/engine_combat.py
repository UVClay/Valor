"""Contacts and hits for the engine (docs/re/entities.md Collisions, combat.md)."""
import engine_gte as gt
from engine import s16, s32, REMOVED, G_ENTITIES

G_HIT_LIST = 0x8009D620


def call(eng, kind, addr, *args):
    """A touch (+0x37C) or hit_part (+0x374) handler by its address; 0 when there is none."""
    if not addr:
        return 0
    h = eng.handlers.get((kind, addr))
    if h is None:
        eng.note(f"{kind} {addr:08X} not implemented")
        return 0
    return h(*args) or 0


def centre(m, e):
    return [m.s16(e + 0x2E0 + 2 * k) for k in range(3)]


def spheres_touch(m, a, b):
    """spheres_touch (0x8004CB18)."""
    ra, rb = m.u16(a + 0x2E8), m.u16(b + 0x2E8)
    if not ra or not rb:
        return False
    d = [s16(x - y) for x, y in zip(centre(m, a), centre(m, b))]
    return (ra + rb) ** 2 - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) > 0


def parts_touch(m, a, b):
    """parts_touch (0x8004C990): every touching pair marked (+0 = 1, +0x314 the overlap); the first
    pair reported."""
    first = None
    pa, pb = m.u32(a + 0x304), m.u32(b + 0x304)
    for i in range(m.u16(a + 0x36)):
        qa = pa + 0x48 * i
        if m.s32(qa + 8) == 7:
            continue
        for j in range(m.u16(b + 0x36)):
            qb = pb + 0x48 * j
            if m.s32(qb + 8) == 7:
                continue
            d = [s16(m.s32(qa + 0x20 + 4 * k) - m.s32(qb + 0x20 + 4 * k)) for k in range(3)]
            r = s32(m.s32(qa + 0x34) + m.s32(qb + 0x34))
            v = s32(r * r - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]))
            if v > 0:
                m.w32(qa, 1)
                m.w32(qb, 1)
                m.w32(a + 0x314, v)
                m.w32(b + 0x314, v)
                if first is None:
                    first = (i, j)
    return first


def segment_sphere(m, e, o, u):
    """segment_sphere_from_prev (0x8004CD90 -> 0x8004CBC8): the path's line within the sphere;
    the sort key (the largest |d| + a quarter of the other two)."""
    r = m.u16(o + 0x2E8)
    if not r:
        return None
    start = [m.s16(e + 0x25A + 4 * k) for k in range(3)]
    d = [s32(c - s) for c, s in zip(centre(m, o), start)]
    _, c = gt.op([s16(x) for x in d], u, 12)
    if r * r < c[0] * c[0] + c[1] * c[1] + c[2] * c[2]:
        return None
    return gt.dist_cheap(*d)


def segment_parts(m, e, u, len2, o):
    """segment_parts_from_prev (0x8004C968 -> 0x8004C76C): every part crossed is marked; the
    lowest-numbered reported."""
    start = [m.s16(e + 0x25A + 4 * k) for k in range(3)]
    first = None
    pp = m.u32(o + 0x304)
    for j in range(m.u16(o + 0x36)):
        q = pp + 0x48 * j
        if m.s32(q + 8) == 7:
            continue
        d = [s16(m.s32(q + 0x20 + 4 * k) - start[k]) for k in range(3)]
        r2 = m.s32(q + 0x38)
        a1 = s32(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - r2)
        hit = a1 <= 0
        if not hit:
            dot = s32(d[0] * u[0] + d[1] * u[1] + d[2] * u[2])
            if dot > 0 and len2 >= a1:
                _, x = gt.op(d, u, 12)
                a1 = r2
                hit = not r2 < s32(x[0] * x[0] + x[1] * x[1] + x[2] * x[2])
        if hit:
            m.w32(q, 1)
            m.w32(o + 0x314, a1)
            if first is None:
                first = j
    return first


def collide(eng):
    """entities_collide (0x8006C08C), after every entity's update: pairs of entities further down
    the list whose spheres and parts touch; swept projectiles (flags B 0x4) against everything on
    their path this tick, nearest first for flags B 0x80000; then the deferred world contact."""
    m = eng.m
    head = m.u32(G_ENTITIES)
    e = head
    while e:
        nxt = m.u32(e + 4)
        fb = m.u32(e + 0x10)
        if m.u32(e + 0xC) & REMOVED or fb & 2:
            e = nxt
            continue
        if not fb & 4:
            o = nxt
            while o:
                onext = m.u32(o + 4)
                if (not m.u32(o + 0xC) & REMOVED and not m.u32(o + 0x10) & 6 and m.u32(e + 0x14) & m.u32(o + 0xC)
                        and spheres_touch(m, e, o)):
                    call(eng, "touch", m.u32(e + 0x37C), e, o)
                    call(eng, "touch", m.u32(o + 0x37C), o, e)
                    pt = parts_touch(m, e, o)
                    if pt is not None:
                        call(eng, "hit_part", m.u32(e + 0x374), e, pt[0], o, pt[1])
                        call(eng, "hit_part", m.u32(o + 0x374), o, pt[1], e, pt[0])
                        if m.u32(e + 0xC) & REMOVED:
                            break
                o = onext
        else:
            swept(eng, e, head)
        e = nxt


def swept(eng, e, head):
    m = eng.m
    fb = m.u32(e + 0x10)
    nearest = bool(fb & 0x80000)
    path = [s16(s32(m.s32(e + 0x20C + 4 * k) - m.s32(e + 0x258 + 4 * k)) >> 16) for k in range(3)]
    sq, _ = gt.sqr(path, 0)
    len2 = s32(sq[0] + sq[1] + sq[2])
    u = [s16(x) for x in gt.vector_normal(path)] if any(path) else [0, 0, 0]
    if u == [0, 0, 0]:
        u = [m.s16(e + 0x344), m.s16(e + 0x34A), m.s16(e + 0x350)]
    hits = []                   # g_hit_list: [entity, key, part, done]
    best = None
    o = head
    while o:
        onext = m.u32(o + 4)
        if (not m.u32(o + 0xC) & REMOVED and not m.u32(o + 0x10) & 6 and m.u32(e + 0x14) & m.u32(o + 0xC)
                and o != e):
            key = segment_sphere(m, e, o, u)
            if key is not None:
                if not nearest:
                    call(eng, "touch", m.u32(e + 0x37C), e, o)
                    part = segment_parts(m, e, u, len2, o)
                    if part is not None:
                        call(eng, "hit_part", m.u32(e + 0x374), e, -1, o, part)
                        call(eng, "hit_part", m.u32(o + 0x374), o, part, e, -1)
                        if m.u32(e + 0xC) & REMOVED:
                            break
                elif len(hits) < 0x9F:
                    if m.u16(o + 0x36):
                        part = segment_parts(m, e, u, len2, o)
                        if part is not None:
                            if best is None or (key & 0xFFFF) < hits[best][1]:
                                best = len(hits)
                            hits.append([o, key & 0xFFFF, part, 0])
                    else:
                        if best is None or (key & 0xFFFF) < hits[best][1]:
                            best = len(hits)
                        hits.append([o, key & 0xFFFF, -1, 0])
        o = onext
    while best is not None:
        tgt, key, part = hits[best][0], hits[best][1], hits[best][2]
        hits[best][3] = 1
        r0 = call(eng, "touch", m.u32(e + 0x37C), e, tgt)
        r1 = r2 = 0
        if m.u16(tgt + 0x36) and part != -1:
            r1 = call(eng, "hit_part", m.u32(e + 0x374), e, -1, tgt, part)
            r2 = call(eng, "hit_part", m.u32(tgt + 0x374), tgt, part, e, -1)
        if r0 or r1 or r2:
            break
        nb, nkey = None, 0x3B9AC9FF
        for i, h in enumerate(hits):
            k = s16(h[1])
            if not h[3] and k >= s16(key) and k < nkey and h[0] != tgt:
                nb, nkey = i, k
        best = nb
    if not m.u32(e + 0xC) & REMOVED and m.u32(e + 0x10) & 8:
        h = eng.handlers.get(("world_contact", m.u32(e + 0x378)))
        if h:
            h(e, 0)
        else:
            eng.note(f"world_contact {m.u32(e + 0x378):08X} not implemented")


def damage_from_hit(eng, victim, part, attacker, apart):
    eng.note("damage_from_hit not implemented")


def enemy_weapon(eng, e):
    """enemy_weapon (0x8005163C): not yet."""
    m = eng.m
    if m.u8(m.u32(e + 0x388) + 0x11):
        eng.note("enemy_weapon firing not implemented")


def enemy_aim_hook(eng, e):
    pass


def attachments_hook(eng, e):
    pass


def fire_gun(eng, e, inv, fire):
    from engine_projectile import fire_gun as fg
    fg(eng, e, inv, fire)
