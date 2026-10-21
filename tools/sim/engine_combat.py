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


# ---------------------------------------------------------------- damage (combat.md, Damage)

G_STAT_HITS = 0x80039CAC
G_STAT_WOUNDED = 0x80039CB0
G_STAT_KILLS = 0x80039CB8
G_WEAPONS = 0x80093640
G_CHEATS = 0x800386A4
ATTACK_KINDS = (9, 5, 6)


def part(m, e, i):
    return m.u32(e + 0x304) + 0x48 * i


def facing(m, e, a, b):
    """0x8004C108: the entity's forward column . (b - a) > 0 (b in front of a)."""
    d = [s16(b[k] - a[k]) for k in range(3)]
    f = [m.s16(e + 0x344), m.s16(e + 0x34A), m.s16(e + 0x350)]
    return s32(f[0] * d[0] + f[1] * d[1] + f[2] * d[2]) > 0


def part_centre(m, q):
    return [s16(m.u16(q + 0x20 + 4 * k)) for k in range(3)]


def damage_from_hit(eng, victim, vpart, attacker, apart):
    """damage_from_hit (0x8004C1B4): the attacker's damage (a projectile's +0xF8, or an attack
    part's +0x10) scaled by the victim part's kind; the side it came from into the victim's +0x38;
    contacts between parts mark attack parts spent (0x8004A868); apply_damage."""
    m = eng.m
    if vpart < 0:
        return 0
    vq = part(m, victim, vpart)
    aq = None
    if apart < 0:
        dmg = m.s32(attacker + 0xF8)
    else:
        aq = part(m, attacker, apart)
        dmg = m.s32(aq + 0x10) if m.s32(aq + 8) in ATTACK_KINDS else 0
    kind = m.s32(vq + 8)
    if kind == 10:
        dmg *= 2
    elif kind == 11:
        dmg *= 4
    elif kind == 12:
        dmg = 0x7FFFFFFF
    elif kind == 2:
        dmg = (dmg >> 1) + (dmg >> 2)
    elif kind == 3:
        dmg >>= 1
    elif kind == 4:
        dmg >>= 2
    dmg = s32(dmg)
    if apart < 0:
        start = [m.s16(attacker + 0x25A + 4 * k) for k in range(3)]
        side = 0x40 if facing(m, victim, part_centre(m, vq), start) else 0x20
        m.w32(victim + 0x38, m.u32(victim + 0x38) | side)
        if dmg:
            apply_damage(eng, victim, dmg, attacker)
        return 1
    akind = m.s32(aq + 8)
    if (akind == 8 or kind == 8) and m.s32(victim + 8) != 0x1B and m.s32(attacker + 8) != 0x1B:
        return 0
    dy = s32(m.s32(victim + 0x210) - m.s32(attacker + 0x210)) >> 16
    spent = False
    front = facing(m, victim, part_centre(m, vq), part_centre(m, aq))
    if akind in ATTACK_KINDS:
        m.w32(attacker + 0x38, m.u32(attacker + 0x38) | 0x200000)
        if akind == 5:
            bits = 0x40 if front else 0x20
        else:
            bits = (0x100000 if akind == 6 else 0x80000) | (0x40 if front else 0x20)
        m.w32(victim + 0x38, m.u32(victim + 0x38) | bits)
        if m.u32(attacker + 0x10) & 0x20000:
            for i in range(m.u16(attacker + 0x36)):
                q = part(m, attacker, i)
                if m.s32(q + 8) in ATTACK_KINDS:
                    m.w32(q + 8, 8)
        spent = True
    elif kind not in ATTACK_KINDS:
        m.w32(victim + 0x38, m.u32(victim + 0x38) | (0x400 if front else 0x200))
    if dy > 0 and akind in ATTACK_KINDS:
        m.w32(victim + 0x38, m.u32(victim + 0x38) | 0x80)
    if dy < 0 and akind in ATTACK_KINDS:
        m.w32(victim + 0x38, m.u32(victim + 0x38) | 0x100)
    if dmg:
        apply_damage(eng, victim, dmg, attacker)
    return 1


def apply_damage(eng, victim, dmg, attacker):
    """apply_damage (0x8004AC58): nothing while +0x48 runs; health down; at 0 dead (flags A
    0x20000000, +0x38 0x4000), a kill credited for flags B 0x8000; the player's wound feedback;
    the hit statistic once per projectile."""
    m = eng.m
    if m.s32(victim + 0x48) > 0:
        return
    if m.u32(attacker + 0xC) & 1 and m.u32(m.u32(attacker + 0x388)) & 0x20000000:
        dmg *= 4
    h = s32(m.s32(victim + 0xF4) - dmg)
    m.w32(victim + 0xF4, h)
    if h <= 0:
        fa = m.u32(victim + 0xC)
        if fa & 0x20000001 != 0x20000000:
            if not fa & 1 and m.u32(victim + 0x10) & 0x8000:
                stat_kill(eng, attacker, 1)
            m.w32(victim + 0x38, m.u32(victim + 0x38) | 0x4000)
            m.w32(victim + 0xC, m.u32(victim + 0xC) | 0x20000000)
        m.w32(victim + 0xF4, 0)
    fa = m.u32(victim + 0xC)
    if fa & 1:
        eng.note("hit_indicator (the player's wound wedge) not implemented")
        inv = m.u32(victim + 0x388)
        m.w32(G_STAT_WOUNDED, m.u32(G_STAT_WOUNDED) + 1)
        m.w32(inv + 0x144, m.u32(inv + 0x144) + 1)
        return
    if fa & 0x4440 and not m.u32(attacker + 0xC) & 1 and not m.u32(attacker + 0x10) & 0x100000 \
            and m.s16(attacker + 0x3CA) != -1:
        stat_hit(eng)
        m.w32(attacker + 0x10, m.u32(attacker + 0x10) | 0x100000)


def stat_hit(eng):
    m = eng.m
    from engine import G_SOLDIER
    inv = m.u32(m.u32(G_SOLDIER) + 0x388)
    m.w32(G_STAT_HITS, m.u32(G_STAT_HITS) + 1)
    m.w32(inv + 0x148, m.u32(inv + 0x148) + 1)


def stat_kill(eng, attacker, n):
    """stat_kill (0x80080240): the kill counters, and the weapon's (+0x30)."""
    m = eng.m
    from engine import G_SOLDIER
    inv = m.u32(m.u32(G_SOLDIER) + 0x388)
    if not n:
        return
    v = (m.u16(inv + 0x142) + n) & 0xFFFF
    m.w16(inv + 0x142, v)
    m.w32(G_STAT_KILLS, m.u32(G_STAT_KILLS) + n)
    if v & 0x8000:
        m.w32(G_STAT_KILLS, 0)
        m.w16(inv + 0x142, 0)
    w = m.s16(attacker + 0x3CA)
    if w != -1:
        if w < 0xD:
            a = G_WEAPONS + 0x44 * w + 0x30
            m.w16(a, m.u16(a) + 1)
    elif m.u32(attacker + 0xC) & 1:
        a = G_WEAPONS + 0x44 * m.u8(inv + 0x55) + 0x30
        m.w16(a, m.u16(a) + 1)


def get_var(m, e, v):
    return m.s32(e + 0x4C + 4 * v)


def set_var(m, e, v, x):
    m.w32(e + 0x4C + 4 * v, s32(x))


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def dog_hit_part(eng, e, p, o, op):
    """dog_hit_part (0x80054C24)."""
    m = eng.m
    akind = m.s32(part(m, o, op) + 8) if op >= 0 else None
    damage_from_hit(eng, e, p, o, op)
    if get_var(m, e, 0xD) != -1:
        set_var(m, e, 0xD, cdiv(m.s32(e + 0xF4) * 100, m.s32(e + 0xF0)))
    set_var(m, e, 0x18, m.s32(o + 8))
    set_var(m, e, 0xC, p)
    if m.s32(o + 8) == 1 and akind == 5:
        m.w32(part(m, o, op) + 8, 1)
        eng.audio.play(0, 0x22, e, 0x20000, 0, 0x10000, 0x3333, 0, 0, 0, 0)
    return 1


HEAD_STATS = {3: (0x80039CC4, 0x80039CD0), 4: (0x80039CC4, 0x80039CD4), 5: (0x80039CC8, 0x80039CD8),
              6: (0x80039CC8, 0x80039CDC), 8: (0x80039CC4, 0x80039CEC), 9: (0x80039CCC, 0x80039CF0),
              10: (0x80039CCC, 0x80039CF4), 11: (0x80039CCC, 0x80039CF8), 12: (0x80039CCC, 0x80039CFC),
              13: (0x80039CCC, 0x80039D00), 14: (0x80039CCC, 0x80039D04), 15: (0x80039CCC, 0x80039D08),
              16: (0x80039CCC, 0x80039D0C)}


def bump(m, *addrs):
    for a in addrs:
        m.w32(a, m.u32(a) + 1)


def enemy_hit_part(eng, e, p, o, op):
    """enemy_hit_part (0x80055864): the soldier's hit_part (combat.md, Damage; Head hits)."""
    m = eng.m
    from engine_feed import FN
    hp0 = m.s32(e + 0xF4)
    ot = m.s32(o + 8)
    proj = 0x13F0 <= ot < 0x13FA
    if proj:
        from engine_projectile import handle_same
        if handle_same(eng, m.u32(o + 0x388) + 0x54, e):
            return 0
    if ot == m.s32(e + 8) or (proj and m.u32(o + 0xC) & 4):
        damage_from_hit(eng, e, p, o, op)
        set_var(m, e, 0x18, ot)
        set_var(m, e, 0xC, p)
        if not m.u32(e + 0xC) & 0x20000000 and m.s32(e + 0xF4) >= 0:
            return 1
        set_var(m, e, 0xD, 0)
        m.w32(e + 0xF4, 0)
        return 1
    akind = m.s32(part(m, o, op) + 8) if op >= 0 else None
    damage_from_hit(eng, e, p, o, op)
    set_var(m, e, 0x18, ot)
    set_var(m, e, 0xC, p)
    if hp0 != m.s32(e + 0xF4):
        if not m.u32(o + 0x10) & 0x100000 and not m.u32(o + 0xC) & 1 and m.s16(o + 0x3CA) != -1:
            m.w32(G_STAT_HITS, m.u32(G_STAT_HITS) + 1)
            m.w32(o + 0x10, m.u32(o + 0x10) | 0x100000)
        if 0 <= p < 0x11:
            if p == 0:
                obj = m.u32(e + 0x388) + 0x18
                hf = m.s32(obj + 0x144 + 4)
                if hf & 1 and not hf & 0x1E and ((eng.rand.draw(*FN["enemy_hit"]) * 100) >> 15) < 0x32:
                    eng.note("helmet knocked off (attachment_knock_off) not implemented")
                    apply_damage(eng, e, m.s32(e + 0xF4), o)
                    if ot != 0x31:
                        bump(m, 0x80039CC0)
                    else:
                        bump(m, 0x80039CC4, 0x80039CE8)
                else:
                    apply_damage(eng, e, m.s32(e + 0xF4), o)
                    if ot != 0x31:
                        bump(m, 0x80039CC0)
                    else:
                        bump(m, 0x80039CC4, 0x80039CE8)
            elif p == 7 or (p in (1, 2) and ot == 0x31):
                bump(m, 0x80039CC4, 0x80039CE8)
            elif p == 1:
                bump(m, 0x80039CC8, 0x80039CE0)
            elif p == 2:
                bump(m, 0x80039CC8, 0x80039CE4)
            elif p in HEAD_STATS:
                bump(m, *HEAD_STATS[p])
        if ot == 1 and akind == 5:
            m.w32(part(m, o, op) + 8, 1)
            eng.audio.play(0, 0x22, e, 0x20000, 0, 0x10000, 0x3333, 0, 0, 0, 0)
    if get_var(m, e, 0xD) != -1:
        if not m.u32(e + 0xC) & 0x20000000 and m.s32(e + 0xF4) > 0:
            set_var(m, e, 0xD, cdiv(m.s32(e + 0xF4) * 100, m.s32(e + 0xF0)))
            if not m.u32(e + 0xC) & 0x20000000 and get_var(m, e, 0xD) < 0xA:
                apply_damage(eng, e, m.s32(e + 0xF4), o)
        else:
            set_var(m, e, 0xD, 0)
            m.w32(e + 0xF4, 0)
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x20000000)
    if proj and not m.u32(o + 0xC) & 4:
        ent = eng.ents.get(e)
        eng.logic.run_message(ent, 0, 1, "hit by a projectile")
    return 1


def player_hit_part(eng, e, p, o, op):
    """player_hit_part (0x8007C1D0): the mounted gun's use; a character's attack part (kind 5,
    touching) as the attacking part; damage unless god mode; the wound feedback."""
    m = eng.m
    from engine_feed import FN
    inv = m.u32(e + 0x388)
    ot = m.s32(o + 8)
    if ot == 0x1772:
        eng.note("player_hit_part: mounted gun use not implemented")
        m.w32(inv + 0x1C0, ot)
        m.w32(m.u32(e + 0x304) + 0x50, 7)
        return 1
    if ot in (0x1405, 0x1402):
        for i in range(m.u16(o + 0x36)):
            q = part(m, o, i)
            if m.s32(q + 8) == 5 and m.s32(q):
                op = i
                break
    if 0x13F0 <= ot < 0x13FA:
        from engine_projectile import handle_same
        if handle_same(eng, m.u32(o + 0x388) + 0x54, e):
            m.w32(inv + 0x1C0, ot)
            m.w32(m.u32(e + 0x304) + 0x50, 7)
            return 0
    hp0 = m.s32(e + 0xF4)
    if ot == 0x31:
        eng.note("player hit by an explosion not implemented")
    elif not m.u32(G_CHEATS) & 0x80:
        damage_from_hit(eng, e, p, o, op)
    if hp0 != m.s32(e + 0xF4):
        m.w16(e + 0x2E8, 0xC8)
        m.w32(inv + 0x1C0, ot)
        f = [m.s16(o + 0x344), m.s16(o + 0x34A), m.s16(o + 0x350)]
        m.w32(inv + 0x80, s32((s32(f[0] << 16) >> 0xB) * 3))
        m.w32(inv + 0x84, s32(f[1] * 0x60))
        m.w16(inv + 0x154, 8)
        m.w32(inv + 0x88, s32(f[2] * 0x60))
        eng.note("camera_shake not implemented")
        if ((eng.rand.draw(*FN["player_hit"]) * 100) >> 15) < 0x1E:
            snd = 0x1B if ((eng.rand.draw(*FN["player_hit"]) * 100) >> 15) >= 0x33 else 0x1C
            eng.audio.play(0, snd, e, 0x20000, 0, 0x8000, 0x1000, 0, 0, 0, 0)
        if ot == 0x1402:
            snd, pitch = 0x22, 0x3333
            m.w32(part(m, o, op) + 8, 1)
        else:
            snd = 0xC if ((eng.rand.draw(*FN["player_hit"]) * 100) >> 15) < 0x32 else 0xD
            pitch = 0x7FFF
        eng.audio.play(0, snd, e, 0x20000, 0, pitch, 0, 0, 0, 0, 0)
        from engine_projectile import rumble_set
        rumble_set(m, m.u8(inv + 0x152), 1, 0xFF, 2, 0)
    return 1


# ---------------------------------------------------------------- touching

def object_planes(eng, e, o):
    """sphere_vs_object_planes (0x8004B6DC): only objects with plane shapes (+0x2EE) answer."""
    if eng.m.u16(o + 0x2EE):
        eng.note("sphere_vs_object_planes not implemented", f"type {eng.m.s32(o + 8):X}")
    return 0


def enemy_touch(eng, e, o):
    """enemy_touch (0x8005291C): against an object's planes (stop, or slide along it)."""
    r = object_planes(eng, e, o)
    return 1 if r else 0


def player_touch(eng, e, o):
    """The player's touch (0x8007C030): characters standing (instance +0x6C 0x80) push the player
    out of the sum of the two probe radii (up to 65 a tick); objects' planes."""
    m = eng.m
    ot = m.s32(o + 8)
    if ot in (0x1402, 0x1405) and m.s32(m.u32(o + 0x100) + 0x6C) == 0x80:
        dx = s16(m.s16(o + 0x20E) - m.s16(e + 0x20E))
        dz = s16(m.s16(o + 0x216) - m.s16(e + 0x216))
        a, b = abs(dx), abs(dz)
        if a < b:
            a, b = b, a
        dist = a + (b >> 2)
        over = m.s16(o + 0x2EC) + m.s16(e + 0x2EC) - dist
        if over > 0:
            n = gt.vector_normal([dx, 0, dz])
            over = min(over, 0x41)
            m.w32(e + 0x1F0, s32(m.s32(e + 0x1F0) + (((-n[0] * over) >> 12) << 16)))
            m.w32(e + 0x200, 0)
            m.w32(e + 0x218, 0)
            m.w32(e + 0x220, 0)
            m.w32(e + 0x1F8, s32(m.s32(e + 0x1F8) + (((-n[2] * over) >> 12) << 16)))
            return 1
        return 0
    if object_planes(eng, e, o):
        eng.matrix(e)
        return 1
    return 0


def register(eng):
    eng.handlers[("hit_part", 0x80054C24)] = lambda e, p, o, op: dog_hit_part(eng, e, p, o, op)
    eng.handlers[("hit_part", 0x80055864)] = lambda e, p, o, op: enemy_hit_part(eng, e, p, o, op)
    eng.handlers[("hit_part", 0x8007C1D0)] = lambda e, p, o, op: player_hit_part(eng, e, p, o, op)
    eng.handlers[("touch", 0x8005291C)] = lambda e, o: enemy_touch(eng, e, o)
    eng.handlers[("touch", 0x8007C030)] = lambda e, o: player_touch(eng, e, o)


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
