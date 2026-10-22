"""Grenades (docs/re/objects.md, Grenades): the player's throw (throw_weapon), the grenade itself
(grenade_spawn), what soldiers do to it (grenade_soldier_bits) and its direct hit
(grenade_hit_part). Its flight, bounces and rolling are the projectile's think, entity_move and
world contact (engine_projectile.py); its explosion is engine_explosion.py."""
import engine_gte as gt
from engine import s16, s32, G_SOLDIER
from engine_feed import FN

GRENADE_HIT_PART = 0x80044014
G_STAT_SHOTS = 0x80039CA8
G_CHEATS = 0x800386A4
G_WEAPONS = 0x80093640
GRENADE_MODEL, STICK_GRENADE_MODEL = 0x55A51DAB, 0x61282777


def register(eng):
    eng.handlers[("hit_part", GRENADE_HIT_PART)] = lambda e, p, o, op: grenade_hit_part(eng, e, p, o, op)


def throw_weapon(eng, e, bone_a, bone_b, fire, charge):
    """throw_weapon (0x8007CF4C), weapons 3 and 10. A fire with rounds in the clip and no throw
    under way starts one: variable 0x10 = 1, script 0xC (charge < 12) or 0xE, the wind-up
    inv+0x120 = 8 (stick grenade 18 or 15), inv+0x124 = 1, the refire delay. While a throw is
    under way inv+0x124 counts up; at the wind-up the grenade leaves the hand (grenade_spawn,
    fuse 90, speed 55 + 2 x charge), made to start no further than the thrower can reach (the
    world sweep back toward him, an entity on that line), one off the clip, the sound."""
    m = eng.m
    inv = m.u32(e + 0x388)
    from engine_player import set_var
    from engine_scripts import EVM
    slot = m.s8(inv + 0x54)
    if fire & 0xFF and m.s16(inv + 0x66 + 2 * slot) != 0 and m.s32(inv + 0x124) == 0:
        set_var(m, e, 0x10, 1)
        stick = m.u8(inv + 0x55) == 0xA
        if charge < 0xC:
            EVM(eng, e).script_start(0xC, 1, 0)
            wind = 0x12 if stick else 8
        else:
            EVM(eng, e).script_start(0xE, 1, 0)
            wind = 0xF if stick else 8
        m.w32(inv + 0x120, wind)
        d = m.s32(inv + 0x40)
        m.w32(inv + 0x124, 1)
        m.w32(inv + 0x44, d)
        m.w32(inv + 0x48, abs(d))
        from engine_texts import ammo_hud_update
        ammo_hud_update(eng, e)
    if m.s32(inv + 0x124) == 0:
        return
    c = m.s32(inv + 0x124)
    m.w32(inv + 0x124, c + 1)
    if c < m.s32(inv + 0x120):              # the count before this tick's step
        return
    model = STICK_GRENADE_MODEL if m.u8(inv + 0x55) == 0xA else GRENADE_MODEL
    off_a = off_b = 0
    inst = m.u32(e + 0x100)
    if m.u32(inst + 0x84):
        mdl = m.u32(inst + 8)
        off_a, off_b = mdl + 0xD8, mdl + 0xE0
        bone_a, bone_b = m.s16(mdl + 0xD4), m.s16(mdl + 0xD6)
    m.w32(G_STAT_SHOTS, m.u32(G_STAT_SHOTS) + 1)
    g = grenade_spawn(eng, e, model, bone_a, bone_b, 0x5A, charge * 2 + 0x37, 0, off_a, off_b)
    if g:
        start_within_reach(eng, e, g, inv)
    if not m.u32(G_CHEATS) & 0x40:
        a = inv + 0x66 + 2 * slot
        m.w16(a, (m.u16(a) - 1) & 0xFFFF)
    w = G_WEAPONS + 0x44 * m.u8(inv + 0x55)
    snd = m.s16(w + 0x14)
    if snd >= 0:
        bank = m.s16(w + 0x16)
        if bank < 0:
            bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8)
        eng.audio.play(s16(bank), snd, e, 0x20000, 0, 0x10000, 0x1800, 0, 0, 0, 0)
    m.w32(inv + 0x124, 0)


def start_within_reach(eng, e, g, inv):
    """throw_weapon after grenade_spawn: type 0x13F3, mask 0x08000451, the weapon, flags A bit
    31; under the thrower's floor: lifted 32 above it, at most 55 down. The segment from the
    grenade back along its forward column as long as their horizontal distance (max + min / 4)
    is swept against the world: a hit puts the grenade there, 20 units (the column x 5 >> 10)
    back toward the thrower, again no lower than 32 above his floor. +0x258 = that segment's
    end; if the path from there to the grenade crosses an entity's sphere (not dead or
    removed, not flags B 6, in the mask, not the grenade or the thrower, not flags A
    0x08000000), the grenade starts at the end instead (its matrix and bounds rebuilt)."""
    m = eng.m
    m.w32(g + 8, 0x13F3)
    m.w32(g + 0x14, 0x08000451)
    m.w16(g + 0x3CA, m.u8(inv + 0x55))
    m.w32(g + 0xC, m.u32(g + 0xC) | 0x80000000)
    floor = m.s32(e + 0x208)
    if floor < m.s32(g + 0x210):
        m.w32(g + 0x210, s32(floor - 0x200000))
        if m.s32(g + 0x21C) > 0x370000:
            m.w32(g + 0x21C, 0x370000)
    fwd = [m.s16(g + o) for o in (0x344, 0x34A, 0x350)]
    dx = abs(s16(m.s16(g + 0x20E) - m.s16(e + 0x20E)))
    dz = abs(s16(m.s16(g + 0x216) - m.s16(e + 0x216)))
    a, b = (dz, dx) if dx < dz else (dx, dz)
    t = a + (b >> 2)
    end = [s32(m.s32(g + 0x20C + 4 * k) + (((-fwd[k] * t) >> 12) << 16)) for k in range(3)]
    hit = eng.world.sweep([m.s32(g + 0x20C + 4 * k) >> 16 for k in range(3)], [x >> 16 for x in end])
    if hit is not None:
        point = hit[0]
        for k in range(3):
            m.w32(g + 0x20C + 4 * k, s32((point[k] + ((fwd[k] * -5) >> 10)) << 16))
        if floor < m.s32(g + 0x210):
            m.w32(g + 0x210, s32(floor - 0x200000))
    for k in range(3):
        m.w32(g + 0x258 + 4 * k, end[k])
    u = gt.vector_normal([s16((m.s32(g + 0x20C + 4 * k) - end[k]) >> 16) for k in range(3)])
    from engine_combat import segment_sphere
    best, near = 0x3B9AC9FF, None
    for o in list(eng.entities()):
        fa = m.u32(o + 0xC)
        if fa & 0x20080000 or m.u32(o + 0x10) & 6 or not m.u32(g + 0x14) & fa or o in (g, e) \
                or fa & 0x08000000:
            continue
        d = segment_sphere(m, g, o, u)
        if d is not None and d < best:
            best, near = d, o
    if near is not None:
        for k in range(3):
            m.w32(g + 0x20C + 4 * k, end[k])
        eng.matrix(g)
        from engine_anim import entity_bounds
        entity_bounds(eng, g)


def grenade_spawn(eng, owner, model_hash, bone_a, bone_b, fuse, speed, target, off_a, off_b):
    """grenade_spawn (0x80045808): projectile_create(0x13F3, the model, speed << 16, damage 10.0,
    life 150); sphere radii 76 and 40, eye 40, bullet_touch, grenade_hit_part, flags A |=
    0x040000A0, flags B |= 0x40; behaviour 0x4349, the fuse, extra gravity 3.0, a spin (three
    draws), obj+0x42 = 120; then the speed split by the pitch (RotMatrixX of +0x22A applied to
    (0, 0, speed)): vertical +0x21C, forward +0x200, the pitch 0."""
    from engine_projectile import projectile_create, BULLET_TOUCH
    from engine_objects import model_by_hash
    m = eng.m
    model = model_by_hash(eng, model_hash)
    p = projectile_create(eng, owner, 0x13F3, model or -1, s32(speed << 16), bone_a, bone_b, target, 0x20, 0xA0000,
                          0x96, 0, 0, off_a, off_b, 0, 0, 0, 0)
    if p is None:
        return None
    rd = lambda: eng.rand.draw(*FN["grenade"])
    m.w16(p + 0x2EC, 0x28)
    m.w32(p + 0x2D4, 0x280000)
    m.w32(p + 0x37C, BULLET_TOUCH)
    m.w32(p + 0x374, GRENADE_HIT_PART)
    o = m.u32(p + 0x388)
    m.w32(p + 0xC, m.u32(p + 0xC) | 0x040000A0)
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x40)
    m.w16(o + 0x38, 0x4349)
    m.w8(o + 0x41, fuse)
    m.w32(o + 0x34, 0x30000)
    m.w32(o + 0x68, s32((s32(rd() << 12) >> 15) << 16))
    m.w32(o + 0x6C, 0)
    m.w32(o + 0x70, 0)
    m.w32(o + 0x74, s32((((rd() * 0x28A) >> 15) + 0xC8) << 16))
    m.w32(o + 0x78, s32((((rd() * 0x1E) >> 15) + 0xA) << 16))
    m.w32(o + 0x7C, 0)
    m.w8(o + 0x42, 0x78)
    m.w32(p + 0x230, 0)
    import brain_sim as bs
    r = bs.rot_yxz(m.s16(p + 0x22A), 0, 0)          # RotMatrixX: the yaw and roll parts 0
    v = [0, 0, m.s16(p + 0x202)]
    mac, _ = gt.mul([r[0:3], r[3:6], r[6:9]], v, sf=12)
    m.w32(p + 0x21C, s32(mac[1] << 16))
    m.w32(p + 0x228, 0)
    m.w16(p + 0x2E8, 0x4C)
    m.w32(p + 0x200, s32(mac[2] << 16))
    return p


def grenade_soldier_bits(eng, e):
    """grenade_soldier_bits (0x80044120): what soldiers' scripts set on the grenade's +0x3C
    (objects.md, What soldiers do with them)."""
    m = eng.m
    b = m.u32(e + 0x3C)
    o = m.u32(e + 0x388)
    rd = lambda: eng.rand.draw(*FN["grenade"])
    from engine_projectile import handle_store
    if b & 1:
        # kicked: by the nearest soldier within reach, along his facing
        s = near_soldier(eng, e)
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0)
        if s is None:
            m.w16(o + 0x38, 0x348)
            return
        for k in range(3):
            m.w32(e + 0x228 + 4 * k, m.u32(s + 0x228 + 4 * k))
        m.w32(e + 0x200, 0x190000)
        m.w32(e + 0x21C, s32(0xFFDF0000))
        m.w32(e + 0x284, 0)
        m.w32(o + 0x68, s32((s32(rd() << 12) >> 15) << 16))
        m.w32(o + 0x6C, 0)
        m.w32(o + 0x70, 0)
        m.w32(o + 0x74, s32((((rd() * 0x28A) >> 15) + 0xC8) << 16))
        y = ((rd() * 0x1E) >> 15) + 0xA
        m.w16(o + 0x38, 0x348)
        m.w32(o + 0x78, s32(y << 16))
        m.w32(o + 0x7C, 0)
        m.w16(o + 0x3A, m.u16(o + 0x3A) + 8)
        m.w8(o + 0x41, (m.u8(o + 0x41) + 8) & 0xFF)
        return
    if b & 8:
        # dropped (the holder was hit)
        handle_store(m, e + 0x30, 0)
        m.w32(e + 0x284, 0)
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0)
        m.w16(o + 0x38, 0x348)
        return
    if b & 2:
        # picked up: held on the nearest soldier's bone 17, life 120, the fuse back to 90
        s = near_soldier(eng, e)
        if s is None:
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0)
            m.w16(o + 0x38, 0x348)
            return
        handle_store(m, e + 0x30, s)
        m.w8(e + 0x34, 0x11)
        m.w16(o + 0x3A, 0x78)
        m.w8(o + 0x41, 0x5A)
        for k in (0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C):
            m.w32(o + k, 0)
        m.w16(o + 0x38, m.u16(o + 0x38) & 0xFDF6)
        return
    if b & 4:
        # thrown back along the soldier's facing, harder the further the player is
        s = near_soldier(eng, e)
        handle_store(m, e + 0x30, 0)
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0)
        if s is None:
            m.w16(o + 0x38, 0x348)
            return
        for k in range(3):
            m.w32(e + 0x228 + 4 * k, m.u32(s + 0x228 + 4 * k))
        from engine_objects import dist2d_approx
        d = max(dist2d_approx(m, e, m.u32(G_SOLDIER)), 0)
        a1 = d >> 6
        m.w32(e + 0x21C, s32(0xFFE70000))
        sp = a1 + 0x1E
        if d >= 0x7D1:
            sp = a1 + 0x23
            m.w32(e + 0x21C, s32(0xFFE20000))
        elif d >= 0x515:
            sp = a1 + 0x23
        m.w32(e + 0x200, s32(min(sp, 0x41) << 16))
        m.w32(e + 0x284, 0)
        m.w32(o + 0x74, s32((((rd() * 0x28A) >> 15) + 0xC8) << 16))
        y = s32((((rd() * 0x1E) >> 15) + 0xA) << 16)
        m.w16(o + 0x3A, 0x78)
        m.w8(o + 0x41, 0x5A)
        m.w32(o + 0x78, y)
        m.w16(o + 0x38, 0x348)


def near_soldier(eng, e):
    """0x8005788C(e, 0x50, -1): the nearest entity with flags A & 0x50 (characters), alive and
    not removed, by dist_approx (max + (mid + min) / 4, 16-bit), less than 1000 apart in y."""
    m = eng.m
    best, near = 0x7FFF, None
    for o in eng.entities():
        if not m.u32(o + 0xC) & 0x50 or o == e or m.u32(o + 0xC) & 0x20080000:
            continue
        d = [abs(s16((m.s32(e + 0x20C + 4 * k) - m.s32(o + 0x20C + 4 * k)) >> 16)) for k in range(3)]
        dy = s16((m.s32(e + 0x210) - m.s32(o + 0x210)) >> 16)
        a = sorted(d)
        t = (a[2] + ((a[0] + a[1]) >> 2)) & 0xFFFF
        if t < (best & 0xFFFF) and abs(dy) < 0x3E8:
            near, best = o, t
    return near


def grenade_hit_part(eng, e, part, other, opart):
    """grenade_hit_part (0x80044014): not the thrower, not a projectile (0x13F0..0x13F9), not a
    0x1777: the damage (damage_from_hit) and, with bit 0x100, the small explosion (6, 6, 75) at
    once, the grenade removed. 1 if it hit."""
    m = eng.m
    o = m.u32(e + 0x388)
    from engine_projectile import handle_same
    if handle_same(eng, o + 0x54, other):
        return 0
    t = m.s32(other + 8)
    if 0x13F0 <= t < 0x13FA or t == 0x1777:
        return 0
    from engine_combat import damage_from_hit
    damage_from_hit(eng, e, part, other, opart)
    if m.u16(o + 0x38) & 0x100:
        for k in range(3):
            m.w16(e + 0x364 + 2 * k, 0)
        from engine_explosion import explosion_spawn, explosion_sound
        explosion_spawn(eng, e, 6, 6, 0x4B, 0x10000, 0x1000, 0x40000, 5, 0)
        explosion_sound(eng, e)
        eng.remove(e, "grenade hit")
    return 1
