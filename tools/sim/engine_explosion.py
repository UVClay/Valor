"""Explosions (docs/re/objects.md, Explosions): explosion_spawn (0x800719C0), its think
(0x800715D4) with the fireballs' and smoke puffs' particle systems (0x800712A0), the first
think's burst of sparks (0x80070DEC -> 0x800708F0, an entity of type 0x23), the camera shake,
the light, and explosion_sound (0x80042DF8). The damage is the contact pass's: the explosion's
one hit part (kind 5) against everything its sphere reaches (engine_combat)."""
import move_sim as ms
from engine import s16, s32, G_SOLDIER, G_LAST_EXPLOSION
from engine_feed import FN

EXPLOSION_THINK = 0x800715D4
EXPLOSION_DRAW = 0x80070F24
EXPLOSION_DESTROY = 0x8007192C
DAMAGE_FROM_HIT = 0x8004C1B4
BURST_THINK = 0x800702A4
BURST_DRAW = 0x8006FBF0
NO_BRAIN = 0x8006A318
G_LIGHT = 0x800AA188                 # r, g, b, +4 radius, +8 radius^2
G_LIGHT_STEP = 0x8009E278
G_LIGHT_ON = 0x800AA160
G_CAMERA = 0x8009D638


def register(eng):
    eng.think[EXPLOSION_THINK] = lambda e: explosion_think(eng, e)
    eng.think[BURST_THINK] = lambda e: burst_think(eng, e)
    from engine_combat import damage_from_hit
    eng.handlers[("hit_part", DAMAGE_FROM_HIT)] = lambda e, p, o, op: damage_from_hit(eng, e, p, o, op)


def rd(eng):
    return eng.rand.draw(*FN["explosion"])


def rsin(a):
    return ms.sincos(s16(a) if -0x8000 <= a < 0x8000 else a)[0]


def rcos(a):
    return ms.sincos(s16(a) if -0x8000 <= a < 0x8000 else a)[1]


def explosion_spawn(eng, src, fireballs, puffs, spread, scale, var0, damage, reach, rec):
    """explosion_spawn(source, fireballs, puffs, spread, scale, var0, damage, reach, record)
    (0x800719C0): a type 0x31 entity at the source (or the record) with its object of two particle
    systems (0x64 bytes of header, 0x3C a particle), one hit part of kind 5 with the damage and a
    radius of spread x reach, its sphere spread x 5; linked at the front; the light; the latest
    explosion (0x8009D650)."""
    m = eng.m
    e = eng.entity_create(0x31, obj_size=(fireballs + puffs + 1) * 0x3C + 0x64, parts=1)
    if not e:
        return 0
    m.w32(e + 8, 0x31)
    m.w32(e + 0x294, 0xB4)
    m.w32(e + 0x290, 0xB4)
    m.w32(e + 0x36C, EXPLOSION_THINK)
    m.w32(e + 0x370, EXPLOSION_DRAW)
    m.w32(e + 0x374, DAMAGE_FROM_HIT)
    m.w32(e + 0x14, 0x88004451)
    o = m.u32(e + 0x388)
    m.w32(e + 0x37C, 0)
    m.w32(e + 0xC, 0x01228000)
    m.w32(e + 0x378, 0)
    m.w32(e + 0x384, EXPLOSION_DESTROY)
    m.w32(e + 0x284, 0)
    m.w32(e + 0x10, m.u32(e + 0x10) | 0x201)
    if src:
        for k in range(3):
            m.w32(e + 0x20C + 4 * k, m.u32(src + 0x20C + 4 * k))
        m.w16(e + 0x3CA, m.u16(src + 0x3CA))
        m.w32(e + 0x10, m.u32(e + 0x10) | (m.u32(src + 0x10) & 0x100000))
        if m.s32(src + 0x364) == 0 and m.s16(src + 0x368) == 0:
            normal_from_angle(m, e, m.s16(src + 0x22E) + 0x800)
        else:
            for k in range(4):
                m.w16(e + 0x364 + 2 * k, m.u16(src + 0x364 + 2 * k))
    elif rec:
        for k in range(3):
            m.w32(e + 0x20C + 4 * k, s32(m.s16(rec + 0x10 + 2 * k) << 16))
        m.w16(e + 0x3CA, 0xFFFF)
        normal_from_angle(m, e, m.s16(rec + 0x1A) + 0x800)
    else:
        eng.remove(e, "explosion without a place")
        return None
    for k in range(3):
        m.w32(e + 0x258 + 4 * k, m.u32(e + 0x20C + 4 * k))
    m.w32(e + 0x4C, var0)
    m.w32(o, 0)
    m.w32(o + 4, 0)
    recs = o + 0x64
    # the fireballs (system at obj+0xC)
    sys_header(m, o + 0xC, fireballs, recs, scale, 0xF0000, -0x200,
               (0x3E0, 0x20, 0x3E0, 0xFF, 0x3F, 0x3F, 0x10, 2))
    m.w32(o + 0x14, 1)                   # the first think's countdown (the burst)
    delay = 0
    for i in range(fireballs):
        r = recs + 0x3C * i
        m.w8(r, 3)
        m.w32(r + 0x20, 0x50000)
        place(eng, e, r, spread)
        m.w16(r + 8, ((rd(eng) * 0x32) >> 15) - 0x19)
        m.w16(r + 0xA, rd(eng) >> 3)
        m.w32(r + 0x10, 0x140000)
        m.w32(r + 0x18, 0x400000)
        m.w32(r + 0x14, 0x400000)
        m.w8(r + 2, 5)
        m.w8(r + 1, 5)
        m.w32(r + 0x1C, s32((((rd(eng) * 2) >> 15) + 3) << 16))
        m.w16(r + 0xC, (((rd(eng) * 0x64) >> 15) + 0x23) * 2)
        m.w16(r + 0xE, (((rd(eng) * 0x64) >> 15) + 0x23) * 2)
        m.w16(r + 6, delay)
        delay += rd(eng) >> 15
        m.w8(r + 3, 0)
        m.w16(r + 4, 1)
    recs2 = recs + 0x3C * fireballs
    # the smoke puffs (system at obj+0x38)
    sys_header(m, o + 0x38, puffs, recs2, s32(scale * 2), 0x1E0000, -0x2000,
               (0x3E8, 0, 0x3E0, 0xFA, 0x1F, 0x1F, 8, 2))
    for i in range(puffs):
        r = recs2 + 0x3C * i
        m.w8(r, 3)
        m.w32(r + 0x20, 0)
        place(eng, e, r, spread)
        m.w16(r + 8, ((rd(eng) * 3) & 0xFFFFFFFF) >> 13)
        m.w16(r + 0xA, rd(eng) >> 3)
        m.w32(r + 0x10, 0x50000)
        big = s32((((rd(eng) * 0xA) >> 15) + 0x14) << 16)
        m.w8(r + 2, 0x2D)
        m.w8(r + 1, 0x2D)
        m.w32(r + 0x18, big)
        m.w32(r + 0x14, big)
        m.w32(r + 0x1C, 0x50000)
        m.w16(r + 0xC, 0x78)
        m.w16(r + 0xE, 0x78)
        m.w16(r + 6, ((rd(eng) * 0x14) >> 15) + 8)
        m.w8(r + 3, 0)
        m.w16(r + 4, 2)
    m.w16(e + 0x2E8, spread * 5)
    m.w16(e + 0x36, 1)
    q = m.u32(e + 0x304)
    m.w32(q, 0)
    m.w32(q + 0x18, 0)
    m.w32(q + 8, 5)
    m.w32(q + 0xC, 5)
    m.w32(q + 0x10, damage)
    m.w32(e + 0xF8, damage)
    for k in range(3):
        m.w32(q + 0x20 + 4 * k, m.s16(e + 0x20E + 4 * k))
    rr = s32(spread * reach)
    m.w32(q + 0x34, rr)
    m.w32(q + 0x30, rr)
    m.w32(q + 0x38, s32(rr * rr))
    m.w32(q + 0x3C, 0)                   # the fireball model's instance: drawing only
    from engine_projectile import parts_box
    parts_box(m, e)
    eng.link_front(e)
    m.w8(G_LIGHT, 0x50)
    m.w8(G_LIGHT + 1, 0x28)
    m.w8(G_LIGHT + 2, 0xA)
    m.w16(G_LIGHT + 4, 0x96)
    m.w32(G_LIGHT + 8, 0x57E4)
    m.w32(G_LAST_EXPLOSION, e)
    m.w8(G_LIGHT_STEP, 0)
    m.w32(G_LIGHT_ON, 1)
    return e


def normal_from_angle(m, e, a):
    """explosion_spawn without a contact normal: the source's yaw turned round, rsin and rcos
    (16.16, rounded toward zero to 4096ths) scaled to (x 40/65536, 0, z 5/8192)."""
    sn, cs = rsin(a), rcos(a)
    v = s32(sn << 16) + (0xFFF if sn & 0x8000 else 0)
    w = s32(cs << 16) + (0xFFF if cs & 0x8000 else 0)
    m.w16(e + 0x364, s16(((v >> 12) * 0x28) >> 16))
    m.w16(e + 0x366, 0)
    m.w16(e + 0x368, s16(((w >> 12) * 5) >> 13))


def sys_header(m, s, n, recs, scale, drag, grav, cols):
    m.w32(s, 1)
    m.w16(s + 4, n)
    m.w16(s + 6, n)
    m.w32(s + 0x28, recs)
    m.w32(s + 0xC, scale)
    m.w32(s + 0x10, drag)
    m.w32(s + 0x14, grav)
    for k, v in enumerate(cols):
        m.w16(s + 0x18 + 2 * k, v)


def place(eng, e, r, spread):
    """A particle at a random point of the cube +-spread around the explosion (three draws;
    none with a spread of 0); no velocity."""
    m = eng.m
    for k in range(3):
        c = m.s32(e + 0x20C + 4 * k)
        if spread * 2:
            c = s32(c + ((-spread + ((rd(eng) * spread * 2) >> 15)) << 16))
        else:
            c = s32(c - spread * 0x10000)
        m.w32(r + 0x24 + 4 * k, c)
        m.w32(r + 0x30 + 4 * k, 0)


def particles_update(eng, s):
    """0x800712A0: one system's tick. Each particle: a delay counts down first; then velocity +=
    the system's gravity, position += velocity, the horizontal velocities eased toward 0 by the
    drag, the angle += spin; by state: 3 grows to its size, 4 holds (at its end a fireball, flag
    1, leaves the system's live count and its colours move by -10..9), 5 shrinks (fireballs rise
    0.5 faster) to 6, done. The system's state: 1 while anything is under way, else 2."""
    m = eng.m
    m.w32(s, 2)
    recs = m.u32(s + 0x28)
    for i in range(m.s16(s + 4)):
        r = recs + 0x3C * i
        if m.s16(r + 6):
            m.w16(r + 6, m.u16(r + 6) - 1)
            m.w32(s, 1)
            continue
        g = m.s32(s + 0x14)
        if g:
            m.w32(r + 0x34, s32(m.s32(r + 0x34) + g))
        m.w32(r + 0x24, s32(m.s32(r + 0x24) + m.s32(r + 0x30)))
        m.w32(r + 0x2C, s32(m.s32(r + 0x2C) + m.s32(r + 0x38)))
        m.w32(r + 0x28, s32(m.s32(r + 0x28) + m.s32(r + 0x34)))
        drag = m.s32(s + 0x10)
        for o in (0x38, 0x30):
            v = m.s32(r + o)
            m.w32(r + o, (v - drag if v > 0 else v + drag) if drag < abs(v) else 0)
        m.w16(r + 0xA, m.u16(r + 0xA) + m.u16(r + 8))
        st = m.u8(r)
        fire = m.u16(r + 4) & 1
        if st == 3:
            size = s32(m.s32(r + 0x20) + m.s32(r + 0x10))
            m.w32(r + 0x20, size)
            if size >= m.s32(r + 0x18):
                m.w8(r, 4)
                m.w32(r + 0x20, m.u32(r + 0x18))
            m.w32(s, 1)
        elif st == 4:
            h = (m.u8(r + 2) - 1) & 0xFF
            m.w8(r + 2, h)
            if not h:
                m.w8(r, 5)
                if fire:
                    m.w16(s + 6, m.u16(s + 6) - 1)
                    m.w16(r + 0xC, m.u16(r + 0xC) + ((eng.rand.draw(0x800712A0) * 0x14) >> 15) - 0xA)
                    m.w16(r + 0xE, m.u16(r + 0xE) + ((eng.rand.draw(0x800712A0) * 0x14) >> 15) - 0xA)
            m.w32(s, 1)
        elif st == 5:
            size = s32(m.s32(r + 0x20) - m.s32(r + 0x1C))
            m.w32(r + 0x20, size)
            if size <= 0:
                m.w8(r, 6)
                m.w32(r + 0x20, 0)
            if fire:
                m.w32(r + 0x34, s32(m.s32(r + 0x34) - 0x8000))
            m.w32(s, 1)
        elif fire and st not in (5, 6):
            eng.note("explosion particle in an unknown state", st)
    return 1


def explosion_think(eng, e):
    """explosion_think (0x800715D4): the first think's burst, camera shake and rumble; the part
    and sphere gone once no fireball holds (flags B & ~2); both systems' ticks; removed when both
    are done; the light (the latest explosion's) grows for 8 thinks, then fades."""
    m = eng.m
    o = m.u32(e + 0x388)
    s1 = o + 0xC
    c = m.s32(s1 + 8)
    if c:
        c -= 1
        m.w32(s1 + 8, c)
        if c <= 0:
            burst(eng, e)
            m.w32(s1 + 8, 0)
            sol = m.u32(G_SOLDIER)
            d = [abs(s16((m.s32(sol + 0x20C) - m.s32(e + 0x20C)) >> 16)), 0,
                 abs(s16((m.s32(sol + 0x214) - m.s32(e + 0x214)) >> 16))]
            d.sort()
            t = d[2] + ((d[0] + d[1]) >> 2)
            if t < 0xBB8:
                amp = (0xBB8 - t) >> 4
                cam = m.u32(G_CAMERA)
                co = m.u32(cam + 0x388)
                m.w16(co + 0xCC, amp)        # camera_shake (0x80046C50)
                m.w16(co + 0xCE, 0xA)
    if m.s16(s1 + 6) <= 0:
        m.w16(e + 0x2E8, 0)
        m.w16(e + 0x36, 0)
        m.w32(e + 0x10, m.u32(e + 0x10) & ~2)
    if m.s32(o + 0xC) != 2:
        particles_update(eng, s1)
        if m.s32(o + 0xC) == 2:
            m.w32(o, 1)
    if m.s32(o + 0x38) == 2:
        m.w32(o + 4, 1)
    else:
        particles_update(eng, o + 0x38)
    if m.s32(o + 4) and m.s32(o):
        eng.remove(e, "explosion done")
    if m.u32(G_LAST_EXPLOSION) == e:
        if m.u8(G_LIGHT_STEP) < 8:
            r = (m.u16(G_LIGHT + 4) + 0xAF) & 0xFFFF
            m.w8(G_LIGHT_STEP, m.u8(G_LIGHT_STEP) + 1)
            m.w8(G_LIGHT, (m.u8(G_LIGHT) + 8) & 0xFF)
            m.w16(G_LIGHT + 4, r)
            m.w8(G_LIGHT + 1, (m.u8(G_LIGHT + 1) + 3) & 0xFF)
            m.w8(G_LIGHT + 2, (m.u8(G_LIGHT + 2) + 1) & 0xFF)
            m.w32(G_LIGHT + 8, s32(s16(r) * s16(r)))
            return
        m.w8(G_LIGHT, (m.u8(G_LIGHT) - 4) & 0xFF)
        m.w8(G_LIGHT + 1, (m.u8(G_LIGHT + 1) - 3) & 0xFF)
        if m.u8(G_LIGHT + 2):
            m.w8(G_LIGHT + 2, m.u8(G_LIGHT + 2) - 1)
        r = (m.u16(G_LIGHT + 4) - 0x4B) & 0xFFFF
        m.w16(G_LIGHT + 4, r)
        m.w32(G_LIGHT + 8, s32(s16(r) * s16(r)))
        if m.s32(o):
            m.w32(G_LAST_EXPLOSION, 0)
            m.w8(G_LIGHT_STEP, 0)


def burst(eng, e):
    """explosion_burst (0x80070DEC) -> 0x800708F0(pos, the contact normal scaled by variable 0,
    colours (64, 64, 64) and (32, 32, 32), 5, life 3, 2): an entity of type 0x23 with 5 pairs of
    sparks (three draws each: a jitter of +-8 per axis, one unused, the angles of their streak),
    linked at the front; the streaks are drawing only."""
    m = eng.m
    p = eng.entity_create(0x23, obj_size=5 * 0x48 + 0x30)
    if not p:
        return 0
    o = m.u32(p + 0x388)
    m.w32(o + 4, 3)
    m.w32(o, 10)
    m.w32(o + 8, 2)
    m.w32(o + 0x2C, o + 0x30)
    m.w32(p + 8, 0x23)
    m.w32(p + 0x10, 0x203)
    m.w32(p + 0x36C, BURST_THINK)
    m.w32(p + 0x370, BURST_DRAW)
    for k in (0xC, 0x374, 0x378, 0x228, 0x22C, 0x230, 0x284):
        m.w32(p + k, 0)
    m.w32(p + 0x384, NO_BRAIN)
    pos = [m.s32(e + 0x20C + 4 * k) for k in range(3)]
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, pos[k])
        m.w32(p + 0x270 + 4 * k, 0x10000)
    for i in range(5):
        for base, st in ((o + 0x30 + 0x24 * i, 1), (o + 0x30 + 0x24 * (i + 5), 2)):
            r = eng.rand.draw(0x800708F0)
            for k in range(3):
                m.w32(base + 4 + 4 * k, s32(pos[k] + ((((r >> (4 * k)) & 0xF) - 8) << 16)))
            eng.rand.draw(0x800708F0)
            eng.rand.draw(0x800708F0)
            m.w32(base, st)
    eng.link_front(p)
    return p


def burst_think(eng, e):
    """0x800702A4: the life (obj+4) down; removed at 0."""
    m = eng.m
    o = m.u32(e + 0x388)
    c = m.s32(o + 4) - 1
    m.w32(o + 4, c)
    if c <= 0:
        eng.remove(e, "burst done")


def explosion_sound(eng, e):
    """explosion_sound (0x80042DF8): two or three layers of sounds 9/10 (draws by this function)."""
    m = eng.m
    t = m.s32(e + 8)
    r = lambda: ((eng.rand.draw(0x80042DF8) * 2) >> 15) + 9
    if t in (0x13F1, 0x13F3):
        eng.audio.play(0, r(), e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 3, 0)
    a = r()
    eng.audio.play(0, a, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 3, 0)
    eng.audio.play(0, a, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 3, 0)
