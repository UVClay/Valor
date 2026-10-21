"""Firing, projectiles and what a shot leaves behind, for the engine (docs/re/combat.md, objects.md):
fire_gun, bullet_spawn / projectile_create, projectile_think, the generic entity_move (the world
sweep), projectile_world_contact, the shell casing, the impact effect and the particles."""
import combat_sim as cs
import engine_gte as gt
from engine import s16, s32, REMOVED, G_WORLD
from engine_feed import FN

G_WEAPONS = 0x80093640
G_SLOT_WEAPON = 0x8009E878
G_CHEAT_FLAGS = 0x800386A4      # g_cheats: 0x10 rapid fire, 0x20 big bullets
G_STAT_SHOTS = 0x80039CA8
G_CASINGS = 0x8009EC60
G_RUMBLE_MODE = 0x80093A04
G_LAST_DECAL = 0x800AB918
G_DECAL_INDEX = 0x80093C58
G_FLOOR_LAST = 0x800B7460

PROJECTILE_THINK = 0x80044470
PROJECTILE_DRAW = 0x80044B0C
PROJECTILE_HIT_PART = 0x80043E38
PROJECTILE_WORLD_CONTACT = 0x80043284
PROJECTILE_BRAIN = 0x80044BE4
BULLET_TOUCH = 0x80043AD4
CASING_THINK = 0x80087FA0
NO_BRAIN = 0x8006A318
IMPACT_THINK = 0x800701DC
IMPACT_DRAW = 0x8006F7BC
PARTICLES_THINK = 0x80073B08
PARTICLES_DRAW = 0x800732F8

PROJECTILE_TYPES = range(0x13F0, 0x13FA)


def register(eng):
    eng.think[PROJECTILE_THINK] = lambda e: projectile_think(eng, e)
    eng.think[CASING_THINK] = lambda e: casing_think(eng, e)
    eng.think[IMPACT_THINK] = lambda e: impact_effect_think(eng, e)
    eng.think[PARTICLES_THINK] = lambda e: particles_think(eng, e)
    eng.think[0x80082E88] = lambda e: None      # the pickup's think is empty
    eng.handlers[("world_contact", PROJECTILE_WORLD_CONTACT)] = lambda e, res: projectile_world_contact(eng, e, res)
    eng.handlers[("touch", BULLET_TOUCH)] = lambda e, o: bullet_touch(eng, e, o)
    eng.handlers[("hit_part", PROJECTILE_HIT_PART)] = lambda e, p, o, op: projectile_hit_part(eng, e, p, o, op)


def weapon(m, w, off, size=2, signed=True):
    a = G_WEAPONS + 0x44 * w + off
    if size == 4:
        return m.s32(a)
    return m.s16(a) if signed else m.u16(a)


def handle_store(m, dst, ent):
    """0x8006D84C: the entity's handle, or 0."""
    m.w32(dst, m.u32(ent) if ent else 0)


def handle_same(eng, ptr, ent):
    """handle_same: the handle at ptr names this entity."""
    h = eng.m.u32(ptr)
    return bool(h) and not eng.handle_dead(h) and eng.resolve(h) == ent


# ---------------------------------------------------------------- firing

def fire_gun(eng, e, inv, fire):
    """fire_gun (0x8007D5C8), the player's: the projectile by the weapon's jump table
    (0x8003D240), aimed at the aim point in stances 0, 1, 7, 8 (0x8003D218), the rumble, the
    muzzle flash, the sound, the casing (49%), a round off the clip, the refire delay."""
    m = eng.m
    if not fire:
        return
    w = m.u8(inv + 0x55)
    bone_a, bone_b = weapon(m, w, 0xC), weapon(m, w, 0xE)
    off_a = off_b = 0
    inst = m.u32(e + 0x100)
    if inst and m.u32(inst + 0x84):
        model = m.u32(inst + 8)
        off_a, off_b = model + 0xD8, model + 0xE0
        bone_a, bone_b = m.s16(model + 0xD4), m.s16(model + 0xD6)
    slot = m.s8(inv + 0x54)
    if m.s16(inv + 0x66 + 2 * slot) <= 0:
        return
    stance = m.u32(inv + 0x1C8)
    aimed = 1 if stance in (0, 1, 7, 8) else 0
    vol = 0x17FFF
    m.w32(G_STAT_SHOTS, m.u32(G_STAT_SHOTS) + 1)
    dmg = weapon(m, w, 0x2C, 4)
    shot = None
    rumble = None
    mask = 0x08000450
    if w in (0, 8, 9, 5):
        shot = bullet_spawn(eng, e, bone_a, bone_b, 0, 0, off_a, off_b, dmg, 0, aimed, 0x13F6, 0x2000, 0x20, mask)
        rumble = 3
    elif w == 12:
        shot = bullet_spawn(eng, e, bone_a, bone_b, 0, 0, off_a, off_b, 0x28000, 0, aimed, 0x13F6, 0x2000, 0x20, mask)
        rumble = 3
    elif w in (1, 6, 11):
        shot = bullet_spawn(eng, e, bone_a, bone_b, 0, 0, off_a, off_b, dmg, 0xB, aimed, 0x13F7, 0x2000, 0x20, mask)
        r = s32(m.s32(inv + 0x50) + 0x10000)
        m.w32(inv + 0x50, min(r, 0x10000))
        rumble = 3
    elif w == 2:
        eng.note("rocket_spawn not implemented")
        rumble = 7
    elif w == 4:
        for i in range(6):
            shot = pellet_spawn(eng, e, bone_a, bone_b, 0, 0, off_a, off_b, aimed, 0x13F7, 0x2000, 0, mask)
            if shot and i < 5:
                m.w16(shot + 0x3CA, w)
        vol = 0x20000
        rumble = 7
    if rumble is not None:
        rumble_set(m, m.u8(inv + 0x152), 0, 100, rumble, 0)
    if shot:
        m.w16(shot + 0x3CA, w)
    if not m.u32(inv) & 0x200040:
        m.w16(inv + 0x1D4 + 2, 1)            # the muzzle flash (0x8006DCA0)
    snd = weapon(m, w, 0x14)
    if snd >= 0:
        bank = weapon(m, w, 0x16)
        if bank < 0:
            bank = m.s16(m.u32(m.u32(e + 0x100) + 8) + 0xC8)
        if w in (5, 11):
            vol = 0x20000
        eng.audio.play(bank, snd, e, vol, 0, 0x10000, 0x1800, 0, 0, 0, (weapon(m, w, 0x2A, signed=False) >> 3) & 2)
    if w != 7:
        if not weapon(m, w, 0x2A, signed=False) & 0x10 and (eng.rand.draw(*FN["fire_gun"]) * 100) >> 15 >= 0x33:
            casing_spawn(eng, e, [m.s32(e + 0x20C + 4 * k) for k in range(3)], 0x14000)
    a = inv + 0x66 + 2 * slot
    left = s16(m.u16(a) - 1)
    m.w16(a, left)
    if left == 0:
        m.w32(e + 0x4C + 4 * 0x10, 1)
    if m.u32(G_CHEAT_FLAGS) & 0x10:
        eng.note("refire cheat not implemented")
    d = m.s32(inv + 0x40)
    m.w32(inv + 0x44, d)
    m.w32(inv + 0x48, abs(d))


def rumble_set(m, port, a, b, c, d):
    """0x8004D8DC: the pad's vibration (by the setting at 0x80093A04: off, halved, 3/4, full)."""
    mode = m.u32(G_RUMBLE_MODE)
    if mode == 1:
        b >>= 1
    elif mode == 2:
        b -= b >> 2
    elif mode != 3:
        return
    m.w8(0x80093BE8 + 2 * port, a)
    m.w8(0x80093BE8 + 2 * port + 1, b)
    m.w32(0x80093BFC + 4 * port, c)
    m.w32(0x80093BF4 + 4 * port, (1 << d) - 1)


def bullet_spawn(eng, owner, bone_a, bone_b, a3, a4, off_a, off_b, dmg, spread, aimed, etype, fa, fb, mask):
    """bullet_spawn (0x80045A78): speed 600 + rand % 70; touch = bullet_touch; the tracer; flags B
    0x90004; the spread (2 << n - 1 wide, three draws) for n != 0."""
    m = eng.m
    speed = (eng.rand.draw(*FN["bullet"]) % 70 + 600) << 16
    p = projectile_create(eng, owner, etype, -1, speed, bone_a, bone_b, a3, 0x20, dmg, 0xF, 0, a4, off_a, off_b,
                          aimed, fa, fb, mask)
    if p is None:
        return None
    o = m.u32(p + 0x388)
    m.w32(p + 0x37C, BULLET_TOUCH)
    m.w16(o + 0x38, 0)
    if m.u32(owner + 0xC) & 1 and m.u32(G_CHEAT_FLAGS) & 0x20:
        m.w16(o + 0x38, 0xA40)
    tracer(m, p, 0xE6, 0xB4, 0x46, 0x28, 0x14, 0xA)
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x90004)
    if spread:
        msk, half = (2 << spread) - 1, 1 << spread
        r1 = (eng.rand.draw(*FN["bullet"]) & msk) - half
        r2 = (eng.rand.draw(*FN["bullet"]) & msk) - half
        r3 = eng.rand.draw(*FN["bullet"]) & msk
        m.w32(p + 0x228, s32(m.s32(p + 0x228) + (r1 << 9)))
        m.w32(p + 0x22C, s32(m.s32(p + 0x22C) + (r2 << 9)))
        m.w32(p + 0x230, s32(m.s32(p + 0x230) + ((r3 - half) << 9)))
    return p


def pellet_spawn(eng, owner, bone_a, bone_b, a3, a4, off_a, off_b, aimed, etype, fa, fb, mask):
    """pellet_spawn (0x80045F14): speed 350 + rand*400 >> 15, damage 2.0, life 15, flags B 0x24 (swept,
    not nearest first), the tracer, then a spread of 2^13 (three draws: pitch, yaw, roll, << 9)."""
    m = eng.m
    speed = (((eng.rand.draw(*FN["pellet"]) * 0x190) >> 15) + 0x15E) << 16
    p = projectile_create(eng, owner, etype, -1, speed, bone_a, bone_b, a3, 0x20, 0x20000, 0xF, 0, a4, off_a, off_b,
                          aimed, fa, fb, mask)
    if p is None:
        return None
    o = m.u32(p + 0x388)
    m.w32(p + 0x37C, BULLET_TOUCH)
    m.w16(o + 0x38, 0)
    if m.u32(owner + 0xC) & 1 and m.u32(G_CHEAT_FLAGS) & 0x20:
        m.w16(o + 0x38, 0xA40)
    tracer(m, p, 0xE6, 0xB4, 0x46, 0x28, 0x14, 0xA)
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x24)
    r1 = (eng.rand.draw(*FN["pellet"]) & 0x3FFF) - 0x2000
    r2 = (eng.rand.draw(*FN["pellet"]) & 0x3FFF) - 0x2000
    r3 = eng.rand.draw(*FN["pellet"]) & 0x3FFF
    m.w32(p + 0x228, s32(m.s32(p + 0x228) + (r1 << 9)))
    m.w32(p + 0x22C, s32(m.s32(p + 0x22C) + (r2 << 9)))
    m.w32(p + 0x230, s32(m.s32(p + 0x230) + ((r3 - 0x2000) << 9)))
    return p


def tracer(m, p, r, g, b, r2, g2, b2):
    """0x8004572C: two gouraud lines (SetLineG2) in the object, flag 0xA0; +0x258 = the position."""
    o = m.u32(p + 0x388)
    m.w16(o + 0x38, m.u16(o + 0x38) | 0xA0)
    for prim in (o + 4, o + 0x18):
        m.w8(prim + 3, 4)
        m.w8(prim + 7, 0x50)
    for base, cols in ((8, (r, g, b)), (0x1C, (r, g, b)), (0x10, (r2, g2, b2)), (0x24, (r2, g2, b2))):
        for k in range(3):
            m.w8(o + base + k, cols[k])
    for k in range(3):
        m.w32(p + 0x258 + 4 * k, m.u32(p + 0x20C + 4 * k))


def projectile_create(eng, owner, etype, model, speed, bone_a, bone_b, a6, a7, dmg, life, extra, a11, off_a, off_b,
                      aimed, fa, fb, mask):
    """projectile_create (0x8004532C), in its order."""
    m = eng.m
    if model != -1:
        # entity_create(1, ..., the model, 0x147, 2): an instance, model_instance_init (its parts)
        p = eng.entity_create(etype, obj_size=extra + 0x90, parts=2, instance=True)
        eng.model_swap(p, model, 0x147)
    else:
        p = eng.entity_create(etype, obj_size=extra + 0x90, parts=2)
    o = m.u32(p + 0x388)
    m.w32(p + 0x36C, PROJECTILE_THINK)
    m.w32(p + 0x370, PROJECTILE_DRAW)
    m.w32(p + 0x374, PROJECTILE_HIT_PART)
    m.w32(p + 0x378, PROJECTILE_WORLD_CONTACT)
    m.w32(p + 0x384, PROJECTILE_BRAIN)
    m.w32(p + 8, etype)
    m.w32(p + 0xC, fa | 0x108)
    m.w32(p + 0x14, mask)
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x200 | fb)
    if m.u32(owner + 0xC) & 1 and m.u32(m.u32(owner + 0x388)) & 0x20000000:
        dmg *= 4
    m.w32(p + 0xF8, dmg)
    m.w32(p + 0xC, m.u32(p + 0xC) | (m.u32(owner + 0xC) & 4))
    m.w32(p + 0x30, 0)
    m.w8(p + 0x34, 0xFF)
    m.w16(o + 0x38, 0)
    handle_store(m, o + 0x54, owner)
    m.w16(o + 0x3A, life)
    m.w32(o + 0x2C, 0)
    m.w32(o + 0x30, 0)
    m.w8(o + 0x42, 0)
    m.w32(o + 0x34, 0x10000)
    for k in range(3):
        m.w32(p + 0x218 + 4 * k, 0)
    m.w32(p + 0x200, speed)
    projectile_aim(eng, owner, p, bone_a, bone_b, a6, a11, off_a, off_b, aimed)
    m.w32(p + 0x284, 0)
    m.w32(o + 0x58, 0)
    m.w32(o + 0x44, 0)
    eng.matrix(p)
    for k in range(3):
        m.w32(p + 0x258 + 4 * k, m.u32(p + 0x20C + 4 * k))
    if m.u32(owner + 0xC) & 1:
        inv = m.u32(owner + 0x388)
        w = m.u8(G_SLOT_WEAPON + m.s8(inv + 0x54))
        if weapon(m, w, 0x2A, signed=False) & 2:
            pull_back(m, owner, p)
    parts = m.u32(p + 0x304)
    if model in (0, -1) and parts:
        m.w32(parts + 0x3C, 0)
        m.w32(parts + 0x84, 0)
    parts_box(m, p)
    eng.link_end(p)
    for k in range(3):
        m.w32(o + 0x84 + 4 * k, m.u32(p + 0x20C + 4 * k))
    return p


def pull_back(m, owner, p):
    """projectile_create for weapons with flag 2: the start pulled back along the forward column by
    the larger horizontal distance + a quarter of the smaller, less a quarter."""
    dx = s16(m.s16(p + 0x20E) - m.s16(owner + 0x20E))
    dz = s16(m.s16(p + 0x216) - m.s16(owner + 0x216))
    a, b = abs(dx), abs(dz)
    if a < b:
        a, b = b, a
    t = a + (b >> 2)
    t -= t >> 2
    for k, o in enumerate((0x344, 0x34A, 0x350)):
        m.w32(p + 0x20C + 4 * k, s32(m.s32(p + 0x20C + 4 * k) + (((-m.s16(p + o) * t) >> 12) << 16)))


def parts_box(m, e):
    """0x8004CDB0: the box around the hit parts (+0x308..+0x312)."""
    lo, hi = [0x7FFF] * 3, [-0x7FFF] * 3
    pp = m.u32(e + 0x304)
    for i in range(m.u16(e + 0x36)):
        q = pp + 0x48 * i
        r = m.s16(q + 0x34)
        for k in range(3):
            c = m.s32(q + 0x20 + 4 * k)
            lo[k] = s16(min(lo[k], c - r))
            hi[k] = s16(max(hi[k], c + r))
    for k in range(3):
        m.w16(e + 0x308 + 2 * k, lo[k])
        m.w16(e + 0x30E + 2 * k, hi[k])


def projectile_aim(eng, owner, p, bone_a, bone_b, target, attach, off_a, off_b, aimed):
    """projectile_aim (0x800451B0): aimed at the aim point; the player's scope; otherwise from the
    gun's two points (gun_points 0x80044E0C), toward a target point if given, else along the gun
    (0x80044F10)."""
    m = eng.m
    if aimed:
        return projectile_aim_at_point(eng, owner, p)
    if m.u32(owner + 0xC) & 1 and m.u32(m.u32(owner + 0x388)) & 0x100:
        eng.note("scoped shot (copy_camera_pos) not implemented")
        return
    t = m.s32(p + 8)
    if t == 0x13F3 or (t == 0x13F1 and m.u32(owner + 0xC) & 1):
        # a grenade, the player's rocket: from the hand point with the thrower's angles
        # (copy_shooter_angles 0x80044EF0)
        pa, pb = gun_points(eng, owner, bone_a, bone_b, attach, off_a, off_b)
        for k in range(3):
            m.w32(p + 0x20C + 4 * k, s32(pb[k] << 16))
            m.w32(p + 0x228 + 4 * k, m.u32(owner + 0x228 + 4 * k))
        return
    pa, pb = gun_points(eng, owner, bone_a, bone_b, attach, off_a, off_b)
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, s32(pb[k] << 16))
    if target:
        pitch, yaw = cs.angles_from_to([m.s32(p + 0x20C + 4 * k) for k in range(3)],
                                       [s32(target[k] << 16) for k in range(3)])
        m.w32(p + 0x230, 0)
    else:
        pitch, yaw = cs.angles_from_to([s32(x << 16) for x in pa], [s32(x << 16) for x in pb])
    m.w32(p + 0x228, s32(pitch << 16))
    m.w32(p + 0x22C, s32(yaw << 16))


def gun_points(eng, owner, bone_a, bone_b, attach, off_a, off_b):
    """gun_points (0x80044E0C): with an attachment, its model's vertices bone_a, bone_b
    (0x8008E38C); otherwise points on the owner's bones (model_bone_point)."""
    if attach:
        return attachment_point(eng, attach, bone_a), attachment_point(eng, attach, bone_b)
    from engine_sense import model_bone_point
    m = eng.m
    oa = tuple(m.s16(off_a + 2 * k) for k in range(3)) if off_a else None
    ob = tuple(m.s16(off_b + 2 * k) for k in range(3)) if off_b else None
    return model_bone_point(eng, owner, bone_a, oa), model_bone_point(eng, owner, bone_b, ob)


def attachment_point(eng, B, idx):
    """0x8008E38C: vertex idx of the attachment's model (instance +0x2C, the model's +0x88)
    through its drawn matrix while attached on a bone (+8 set, flag 1), else through RotMatrixYXZ
    of its angles scaled by its scale >> 4 and moved to its integer position (0x8008D918);
    RotTrans, the low halves kept."""
    import brain_sim as bs
    m = eng.m
    vt = m.u32(B + 0x94 + 0x2C)
    v = [m.s16(vt + 8 * idx + 2 * k) for k in range(3)]
    if m.s32(B + 8) and m.u32(B) & 1:
        R = [[m.s16(B + 0x74 + 6 * i + 2 * k) for k in range(3)] for i in range(3)]
        t = [m.s32(B + 0x88 + 4 * k) for k in range(3)]
    else:
        r = bs.rot_yxz(m.s16(B + 0x62), m.s16(B + 0x66), m.s16(B + 0x6A))
        sc = [m.s32(B + 0x24 + 4 * k) >> 4 for k in range(3)]
        R = [[s16((r[3 * i + k] * sc[k]) >> 12) for k in range(3)] for i in range(3)]
        t = [m.s16(B + 0x3E + 4 * k) for k in range(3)]
    mac, _ = gt.mul(R, v, t, 12)
    return [s16(x) for x in mac]


def projectile_aim_at_point(eng, owner, p):
    """projectile_aim_at_point (0x80045084): from inv+0x198 at inv+0x1A4; the world-only mask bits
    off for distance / speed + 1 ticks."""
    m = eng.m
    inv = m.u32(owner + 0x388)
    o = m.u32(p + 0x388)
    start = [m.s32(inv + 0x198 + 4 * k) for k in range(3)]
    aim = [m.s32(inv + 0x1A4 + 4 * k) for k in range(3)]
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, start[k])
    pitch, yaw = cs.angles_from_to(start, aim)
    m.w32(p + 0x228, s32(pitch << 16))
    m.w32(p + 0x22C, s32(yaw << 16))
    dx, dz = abs(s32(aim[0] - start[0])), abs(s32(aim[2] - start[2]))
    d = dx + dz - (min(dx, dz) >> 1)
    sp = m.s32(p + 0x200)
    q = abs(d) // abs(sp) if sp else 0
    if (d < 0) != (sp < 0):
        q = -q
    m.w8(o + 0x40, (q + 1) & 0xFF)
    m.w32(o + 0x3C, m.u32(p + 0x14))
    m.w32(p + 0x14, m.u32(p + 0x14) & 0xF7FFFDFF)


# ---------------------------------------------------------------- the projectile's tick

def projectile_think(eng, e):
    """projectile_think (0x80044470)."""
    m = eng.m
    o = m.u32(e + 0x388)
    life = (m.u16(o + 0x3A) - 1) & 0xFFFF
    m.w16(o + 0x3A, life)
    if not life and m.s32(e + 8):
        eng.remove(e, "projectile life")
        return
    c = m.u8(o + 0x40)
    if c:
        c -= 1
        m.w8(o + 0x40, c)
        if not c:
            m.w32(e + 0x14, m.u32(o + 0x3C))
    parts_box(m, e)
    if m.s32(e + 8) == 0x13F3:
        from engine_grenade import grenade_soldier_bits
        grenade_soldier_bits(eng, e)
    if m.u16(o + 0x38) & 0x40:
        t = m.u8(o + 0x41)
        if t:
            t -= 1
            m.w8(o + 0x41, t)
            if not t:
                if m.u32(o + 0x44):
                    eng.note("projectile fuse callback (obj+0x44) not implemented")
                elif m.u16(o + 0x38) & 0x100:
                    # the fuse: the explosion (8, 10, 85), sparks, the sound
                    from engine_explosion import explosion_spawn, explosion_sound
                    for k in range(3):
                        m.w16(e + 0x364 + 2 * k, 0)
                    explosion_spawn(eng, e, 8, 0xA, 0x55, 0x10000, 0x1000, 0x40000, 5, 0)
                    n = ((eng.rand.draw(*FN["grenade"]) * 5) >> 15) + 0xF
                    particles_spawn(eng, [m.s32(e + 0x20C + 4 * k) for k in range(3)], 5, n, 0x28, 0x19, 0x50,
                                    0x1E, 0x3FF, 0x3F, 0x3E0, 0xFF, 1, 0x10000, 0, 0)
                    explosion_sound(eng, e)
                eng.remove(e, "fuse")
                return
    att = eng.resolve(m.u32(e + 0x30))
    if att and m.s8(e + 0x34) >= 0:
        # held on a bone (entities_update places it): no velocity, no spin, behaviour & 0xFDF6
        # (the bone point computed here is not kept)
        for k in (0x23C, 0x238, 0x234, 0x220, 0x21C, 0x218, 0x200):
            m.w32(e + k, 0)
        m.w16(o + 0x38, m.u16(o + 0x38) & 0xFDF6)
        return
    for k in range(3):
        m.w32(o + 0x68 + 4 * k, s32(m.s32(o + 0x68 + 4 * k) + m.s32(o + 0x74 + 4 * k)))
    f = m.u16(o + 0x38)
    if f & 0x400:
        rolling(eng, e, o, f)
    f = m.u16(o + 0x38)
    if f & 1:
        if not f & 2:
            m.w32(e + 0x21C, s32(m.s32(e + 0x21C) + m.s32(o + 0x34)))
        else:
            m.w16(o + 0x38, f & 0xFFFD)
    elif f & 2:
        m.w16(o + 0x38, f & 0xFFFD)
    if m.u16(o + 0x38) & 0x10:
        eng.note("projectile smoke trail (0x10) not implemented")
    m.w32(o + 0x80, m.u32(e + 0x200))


def rolling(eng, e, o, f):
    """projectile_think, a rolling grenade (0x400): on the ground, the bounce sound on a bounce
    tick, the pitch and the yaw spin halved, obj+0x70 = 0x04000000, the speed down 3.0 a tick (1.0
    at 24.99 or less); below 0 it stops (velocities, spin and rolling cleared); off the ground (or
    stopped) its rolling voice obj+0x30 is stopped."""
    m = eng.m
    if m.s32(e + 0x284):
        if f & 2:
            snd = ((eng.rand.draw(*FN["grenade"]) * 2) >> 15) + 7
            eng.audio.play(1, snd, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 0, 0)
        if m.s32(e + 0x228):
            m.w32(e + 0x228, m.s32(e + 0x228) >> 1)
        if m.s32(o + 0x78):
            m.w32(o + 0x78, m.s32(o + 0x78) >> 1)
        m.w32(o + 0x70, 0x04000000)
        sp = m.s32(e + 0x200)
        sp = s32(sp - ((3 if sp > 0x18FFFF else 1) << 16))
        m.w32(e + 0x200, sp)
        if sp >= 0:
            return
        for k in (0x220, 0x218, 0x23C, 0x238, 0x234, 0x200):
            m.w32(e + k, 0)
        for k in (0x7C, 0x78, 0x74, 0x68):
            m.w32(o + k, 0)
        m.w16(o + 0x38, m.u16(o + 0x38) & 0xFBFF)
    if m.s32(o + 0x30) > 0:
        m.w32(o + 0x30, 0)               # voice_stop: sound only


# ---------------------------------------------------------------- entity_move, the general case

def generic_move(eng, e):
    """entity_move (0x8006C83C) for anything but the player and walkers: the velocity turned by
    RotMatrixYXZ of the angles, the world sweep for flags B 0x44 (bullets), the floor."""
    m = eng.m
    pos0 = [m.s32(e + 0x20C + 4 * k) for k in range(3)]
    for k in range(3):
        m.w32(e + 0x258 + 4 * k, pos0[k])
        m.w32(e + 0x264 + 4 * k, m.u32(e + 0x228 + 4 * k))
    ground0 = m.s32(e + 0x284)
    res = 0
    for k in range(3):
        m.w32(e + 0x228 + 4 * k, s32(m.s32(e + 0x228 + 4 * k) + m.s32(e + 0x234 + 4 * k)))
    inst = m.u32(e + 0x100)
    if inst and m.s16(inst + 0x5E) >= 0 and m.s16(inst + 0x5C) >= 0:
        eng.note("generic entity_move root turn not implemented")
    if m.s32(e + 0x1FC):
        m.w32(e + 0x200, min(m.s32(e + 0x204), s32(m.s32(e + 0x200) + m.s32(e + 0x1FC))))
    m.w32(e + 0x220, m.u32(e + 0x200))
    fa = m.u32(e + 0xC)
    if not m.s32(e + 0x284) and fa & 0x20:
        vy = s32(m.s32(e + 0x21C) + 0x20000)
        m.w32(e + 0x21C, min(vy, 0x4B0000))
    sc = m.s32(e + 0x224)
    if sc == 0x10000:
        v = [s16(m.s32(e + 0x218 + 4 * k) >> 12) for k in range(3)]
    else:
        v = [s16((m.s16(e + 0x21A + 4 * k) * sc) >> 12) for k in range(3)]
    import brain_sim as bs
    rot = bs.rot_yxz(s16(m.s32(e + 0x228) >> 16), s16(m.s32(e + 0x22C) >> 16), s16(m.s32(e + 0x230) >> 16))
    M = [rot[0:3], rot[3:6], rot[6:9]]
    mac, _ = gt.mul(M, v, sf=12)
    for k in range(3):
        m.w32(e + 0x20C + 4 * k, s32(m.s32(e + 0x20C + 4 * k) + s32(mac[k] << 12) + m.s32(e + 0x1F0 + 4 * k)))
    vy16 = mac[1] >> 4
    world = m.u32(G_WORLD)
    if world and m.u32(e + 0x14) & m.u32(world + 0xC):
        fb = m.u32(e + 0x10)
        hit = None
        if fa & 8 and not fa & 1 and not fb & 1:
            if not fb & 0x44:
                eng.note("generic entity_move: walker_slide for a non-walker not implemented")
            else:
                new = [m.s32(e + 0x20C + 4 * k) >> 16 for k in range(3)]
                hit = eng.world.sweep(new, [x >> 16 for x in pos0])
                if hit is not None:
                    point, normal, face = hit
                    write_contact(m, e, point, normal, face)
                    if normal[1] >= 0x801 and vy16 < 0 and m.s32(e + 0x21C) > 0:
                        m.w32(e + 0x21C, 0)
                        m.w32(e + 0x210, m.u32(e + 0x3C0))
                    res |= 2
                    m.w32(e + 0x218, 0)
                    m.w32(e + 0x220, 0)
                    for k in range(3):
                        m.w16(e + 0x364 + 2 * k, normal[k])
                    m.w32(e + 0x38, m.u32(e + 0x38) | 0x8000)
                    if not fa & 0x80:
                        for k in range(3):
                            m.w32(e + 0x20C + 4 * k, s32(point[k] << 16))
                    else:
                        # a bouncer: back to the old x, z plus the normal << 5, rounded to units
                        for k in (0, 2):
                            m.w32(e + 0x20C + 4 * k, s32(pos0[k] + (((normal[k] << 5) + 0x8000) & 0xFFFF0000)))
        res = floor_step(eng, e, res, ground0, vy16, hit is not None)
    for k in range(3):
        m.w32(e + 0x1F0 + 4 * k, 0)
        m.w32(e + 0x3BC + 4 * k, m.u32(e + 0x20C + 4 * k))
    return res


def write_contact(m, e, point, normal, face):
    """world_sweep's face record into +0x318: the point, the normal (the vertices are not kept)."""
    for k in range(3):
        m.w16(e + 0x318 + 2 * k, point[k])
        m.w16(e + 0x320 + 2 * k, normal[k])


def floor_step(eng, e, res, ground0, vy16, walled=False):
    """entity_move's floor part for the general case (0x8006C83C): with the floor under it
    (floor_find, its normal): no floor: a moving object (flags A & 7) goes back (to its old y
    on the floor if it was in the air), on the ground, 0x80008000 in +0x38. On the ground: within
    32 above to 256 below it stays on the floor; further below it steps off (+0x38 | 1), or for
    flags B 0x200000 more than 57 down stops dead. Falling (flags A 0x20): it reaches the floor
    when not more than this tick's fall + 16 (64 at most) above it: the player lands moving down;
    a bouncer (0x80) falling 8 or more a tick bounces (vy = -|vy|, the floor's normal as the
    contact unless a wall was hit, not on the ground); anything else lands (+0x38 & ~1)."""
    m = eng.m
    if m.u32(e + 0x18) & 1 or m.u32(e + 0x10) & 0x10:
        return res
    fa = m.u32(e + 0xC)
    if not (m.s32(e + 0x284) or fa & 8):
        return res
    f, zone, nrm = eng.world.floor_normal([m.s32(e + 0x20C + 4 * k) for k in range(3)])
    m.w32(e + 0x390, zone)
    m.w16(G_FLOOR_LAST, f)
    if f == -0x7FFF:
        if fa & 7 and not m.u32(e + 0x10) & 0x10000:
            for k in range(3):
                m.w32(e + 0x20C + 4 * k, m.u32(e + 0x3BC + 4 * k))
            if not ground0:
                m.w32(e + 0x210, m.u32(e + 0x208))
            res |= 2
            m.w32(e + 0x284, 1)
            m.w32(e + 0x38, m.u32(e + 0x38) | 0x80008000)
        return res
    m.w32(e + 0x208, s32(f << 16))
    d = m.s16(e + 0x212) - f
    if m.s32(e + 0x284):
        if 0 <= d + 0x20 < 0x121 or d >= 0:
            m.w32(e + 0x210, s32(f << 16))
        elif m.u32(e + 0x10) & 0x200000 and d < -0x39:
            res |= 0x80
            for k in (0x220, 0x21C, 0x218, 0x200):
                m.w32(e + k, 0)
            for k in range(3):
                m.w32(e + 0x20C + 4 * k, m.u32(e + 0x3BC + 4 * k))
            m.w32(e + 0x284, ground0)
            m.w32(e + 0x38, m.u32(e + 0x38) | 0x80000000)
        else:
            m.w32(e + 0x284, 0)
            m.w32(e + 0x38, m.u32(e + 0x38) | 1)
        return res
    if not fa & 0x20:
        return res
    if d < 0:
        lim = vy16 + 0x10 if vy16 < 0x30 else 0x40
        if lim < abs(d):
            return res
    if fa & 1:
        if vy16 < 0:
            return res
    elif fa & 0x80 and abs(vy16) >= 8:
        if not walled and nrm is not None:
            for k in range(4):
                m.w16(e + 0x364 + 2 * k, nrm[k])
        res |= 1
        m.w32(e + 0x284, 0)
        m.w32(e + 0x210, s32(f << 16))
        m.w32(e + 0x21C, s32(-abs(m.s32(e + 0x21C))))
        return res
    res |= 1
    m.w32(e + 0x284, 1)
    m.w32(e + 0x210, s32(f << 16))
    m.w32(e + 0x1F4, 0)
    m.w32(e + 0x21C, 0)
    m.w32(e + 0x38, m.u32(e + 0x38) & ~1)
    return res


# ---------------------------------------------------------------- hitting the world

def projectile_world_contact(eng, e, res):
    """projectile_world_contact (0x80043284): for swept projectiles the first call only marks it
    (flags B 0x8, the result kept); entities_collide calls it again after the entity tests."""
    m = eng.m
    fb = m.u32(e + 0x10)
    o = m.u32(e + 0x388)
    if fb & 4:
        if not fb & 8:
            m.w32(e + 0x10, fb | 8)
            m.w32(o + 0x5C, res)
            return 1
        m.w32(e + 0x10, fb & ~8)
        res = m.s32(o + 0x5C)
        if fb & 0x20:
            if decal(eng, e + 0x318):
                for k in range(3):
                    m.w32(e + 0x20C + 4 * k, s32(m.s16(e + 0x318 + 2 * k) << 16))
            if not m.u16(o + 0x38) & 0x200:
                n = ((eng.rand.draw(*FN["grenade"]) * 3) >> 15) + 2
                smoke = ((eng.rand.draw(*FN["grenade"]) * 4) >> 15) + 6
                particles_spawn(eng, [m.s32(e + 0x20C + 4 * k) for k in range(3)], 1, n, 0x1E, 0x19, 0xF, 0x64,
                                0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke)
    f = m.u16(o + 0x38)
    if res & 3 or f & 0x200:
        m.w16(o + 0x38, f | 2)
        f |= 2
        if f & 4 and res & 1:
            m.w32(e + 0xC, m.u32(e + 0xC) & ~0x80)
        elif f & 0x208 and not f & 0x400:
            bounce(eng, e, o, res)
        f = m.u16(o + 0x38)
        if f & 0x10 and res & 1:
            eng.note("projectile smoke trail (0x10) on the ground not implemented")
        if f & 0x200:
            bounce_turn(eng, e, o, res)
    f = m.u16(o + 0x38)
    if not f & 0x40:
        if f & 0x100:
            # an explosive round or a rocket hitting the world: the explosion (10, 12, 130)
            from engine_explosion import explosion_spawn, explosion_sound
            explosion_spawn(eng, e, 0xA, 0xC, 0x82, 0x10000, 0x1000, 0x40000, 5, 0)
            n = ((eng.rand.draw(*FN["grenade"]) * 5) >> 15) + 0xF
            particles_spawn(eng, [m.s32(e + 0x20C + 4 * k) for k in range(3)], 5, n, 0x28, 0x19, 0x50, 0x1E,
                            0x3FF, 0x3F, 0x3E0, 0xFF, 1, 0x17FFF, 0, 0)
            explosion_sound(eng, e)
        else:
            impact_sound(eng, e)
        y = s32(m.s32(e + 0x22C) + ((((eng.rand.draw(*FN["grenade"]) & 0x3FF) - 0x200) << 16) + 0x08000000))
        if y > 0x10000000:
            y -= 0x10000000
        m.w32(e + 0x22C, y)
        eng.remove(e, "projectile hit the world")
    return 1


def bounce(eng, e, o, res):
    """projectile_world_contact, a bouncing projectile (0x208) not rolling: unless 0x800, the speed
    less a third (C division) and, on a ground contact when the next tick would reach the floor
    (floor < y + vy + extra gravity + push), it starts rolling (0x400 for 0x9: on the ground, vy
    and pitch 0, flags A 0x28 instead of 0x80, no pitch spin, at least 30.0 forward); the bounce
    sound; unless 0x800, the extra gravity 10.0 and a quarter of the vertical velocity kept."""
    m = eng.m
    f = m.u16(o + 0x38)
    if not f & 0x800:
        sp = m.s32(e + 0x200)
        sp = s32(sp - int(sp / 3))
        m.w32(e + 0x200, max(sp, 0))
        if res & 1 and m.s32(e + 0x208) < s32(m.s32(e + 0x210) + m.s32(e + 0x21C) + m.s32(o + 0x34)
                                                + m.s32(e + 0x1F4)):
            m.w16(o + 0x38, (m.u16(o + 0x38) & 0xFFF6) | 0x400)
            m.w32(e + 0x284, 1)
            m.w32(e + 0x21C, 0)
            m.w32(e + 0x228, 0)
            m.w32(e + 0xC, (m.u32(e + 0xC) & ~0x80) | 0x28)
            m.w32(o + 0x74, 0)
            if m.s32(e + 0x200) <= 0x1DFFFF:
                m.w32(o + 0x80, 0x1E0000)
                m.w32(e + 0x200, 0x1E0000)
    if m.s32(e + 8) != 0x13F3:
        snd = (eng.rand.draw(*FN["grenade"]) * 7) >> 15
        eng.audio.play(0, snd, e, 0x30000, 0, 0x10000, 0x7FFF, 0, 0, 3, 0)
    else:
        snd = ((eng.rand.draw(*FN["grenade"]) * 2) >> 15) + 7
        eng.audio.play(1, snd, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 0, 0)
    if not m.u16(o + 0x38) & 0x800:
        m.w32(o + 0x34, 0xA0000)
        m.w32(e + 0x21C, m.s32(e + 0x21C) >> 2)


def bounce_turn(eng, e, o, res):
    """projectile_world_contact, 0x200: the speed back to last tick's unless flags A 0x80; unless
    0x800: rolling, halved (and the first time, 0x4000, the soldiers warned), else on a wall a
    third off; the spin halved. Then the heading reflected about the contact normal +0x364: d =
    (forward x, 0, forward z), d += (-(d . n >> 17) . n) >> 12 (GTE), yaw = ratan2(dx, dz), pitch 0
    (0x800431EC with dy 0), roll 0; the spin's signs follow the new angles; no spin: the drawn
    angles take the new ones; a positive pitch makes vy positive."""
    m = eng.m
    import combat_sim as cs
    if not m.u32(e + 0xC) & 0x80:
        m.w32(e + 0x200, m.u32(o + 0x80))
    f = m.u16(o + 0x38)
    if not f & 0x800:
        if f & 0x400:
            sp = m.s32(e + 0x200)
            sp = s32(sp - (sp >> 1))
            m.w32(e + 0x200, max(sp, 0))
            if f & 0x4000:
                warn_soldiers(eng, e)
                m.w16(o + 0x38, m.u16(o + 0x38) & 0xBFFF)
        elif res & 2:
            sp = m.s32(e + 0x200)
            sp = s32(sp - int(sp / 3))
            m.w32(e + 0x200, max(sp, 0))
        m.w32(o + 0x74, m.s32(o + 0x74) >> 1)
        m.w32(o + 0x78, m.s32(o + 0x78) >> 1)
    d = [m.s16(e + 0x344), 0, m.s16(e + 0x350)]
    n = [m.s16(e + 0x364 + 2 * k) for k in range(3)]
    dot = s32(d[0] * n[0] + d[2] * n[2])
    k0 = -(dot >> 17)
    t = [gt.sat16((k0 * n[k]) >> 12) for k in range(3)]
    d = [s16(d[0] + t[0]), 0, s16(d[2] + t[2])]
    m.w32(e + 0x22C, s32(cs.ratan2(d[0], d[2]) << 16))
    m.w32(e + 0x228, 0)
    m.w32(e + 0x230, 0)
    if (m.s32(e + 0x228) ^ m.s32(o + 0x68)) >= 0:
        m.w32(o + 0x74, s32(-m.s32(o + 0x74)))
    if (m.s32(e + 0x22C) ^ m.s32(o + 0x6C)) >= 0:
        m.w32(o + 0x78, s32(-m.s32(o + 0x78)))
    if m.s32(o + 0x74) == 0 and m.s32(o + 0x78) == 0:
        for k in range(3):
            m.w32(o + 0x68 + 4 * k, m.u32(e + 0x228 + 4 * k))
    if m.s32(e + 0x228) > 0:
        m.w32(e + 0x21C, abs(m.s32(e + 0x21C)))


def warn_soldiers(eng, e):
    """A rolling grenade's first contact: every living soldier and dog within 1200
    (dist2d_approx) gets +0x3C 0x40; the nearest (the first of equals) 0x80 if a dog or within
    1000, else 0x20 if three or more were within 1200; then the grenade's hit part kind 7
    (0x8004C64C)."""
    m = eng.m
    from engine_objects import dist2d_approx
    near, best, n = None, 0x7FFFFFFF, 0
    for o in list(eng.entities()):
        if m.s32(o + 8) not in (0x1402, 0x1405) or m.u32(o + 0xC) & 0x20080000:
            continue
        d = dist2d_approx(m, e, o) & 0xFFFFFFFF
        if d < 0x4B0:
            n += 1
            m.w32(o + 0x3C, m.u32(o + 0x3C) | 0x40)
            if near is None or d < best:
                near, best = o, d
    if near is None:
        return
    if m.s32(near + 8) == 0x1405 or ((n & 0xFF) < 3 and best < 0x3E8):
        m.w32(near + 0x3C, m.u32(near + 0x3C) | 0x80)
    elif (n & 0xFF) >= 3:
        m.w32(near + 0x3C, m.u32(near + 0x3C) | 0x20)
    else:
        return
    pp = m.u32(e + 0x304)
    for i in range(m.u16(e + 0x36)):
        m.w32(pp + 0x48 * i + 8, 7)


def decal(eng, rec):
    """0x8004E614: a bullet hole unless within 16 units of the last one, or the normal is zero;
    the hole's polygon (0x8004E354) is drawing only: its draw is taken, its shape not built."""
    m = eng.m
    last = [m.s16(G_LAST_DECAL + 2 * k) for k in range(3)]
    p = [m.s16(rec + 2 * k) for k in range(3)]
    if all(last[k] - 0x10 < p[k] < last[k] + 0x10 for k in range(3)):
        return 0
    if not (m.s16(rec + 8) or m.s16(rec + 0xA) or m.s16(rec + 0xC)):
        return 0
    i = m.s32(G_DECAL_INDEX) + 1
    if i >= 0x64:
        i = 0
    eng.rand.draw(0x8004E354)
    a = 0x80093C5C + (i << 7)
    for off, v in ((0x26, 0xC0), (0x28, 7), (0x2A, 0x1F), (0x2E, 0xFC), (0x24, 0x3E0), (0x2C, 0x3E0)):
        m.w16(a + off, v)
    m.w32(G_DECAL_INDEX, i)
    m.w32(a, m.u32(a) | 7)
    for k in range(4):
        m.w16(G_LAST_DECAL + 2 * k, m.u16(rec + 2 * k))
    return 1


def impact_sound(eng, e):
    """impact_sound (0x80043140): a ricochet (sound 0..6, class 3) and the sparks."""
    m = eng.m
    snd = (eng.rand.draw(0x80043140) * 7) >> 15
    eng.audio.play(0, snd, e, 0x30000, 0, 0x10000, 0x7FFF, 0, 0, 3, 0)
    normal = [m.s16(e + 0x364 + 2 * k) for k in range(3)]
    u = gt.vector_normal(normal)
    vec = [gt.sat16((0xC00 * c) >> 12) for c in u]
    p = impact_effect_spawn(eng, [m.s32(e + 0x20C + 4 * k) for k in range(3)], vec, (0xF0, 0x48, 0), (0x40, 0x10, 0),
                            8, 4)
    if p:
        eng.link_front(p)


def impact_effect_spawn(eng, pos, vec, c0, c1, n, life):
    """impact_effect_spawn (0x800702E0): n sparks around the point (type 0x23). The sparks' end
    points (RotMatrix of random angles applied to vec) are drawing only and not computed."""
    m = eng.m
    p = eng.entity_create(0x23, obj_size=0x130)
    o = m.u32(p + 0x388)
    m.w8(o + 1, life)
    m.w8(o, n)
    m.w32(p + 8, 0x23)
    m.w32(p + 0x10, 0x203)
    m.w32(p + 0x36C, IMPACT_THINK)
    m.w32(p + 0x370, IMPACT_DRAW)
    for off in (0x14, 0xC, 0x374, 0x378, 0x228, 0x22C, 0x230, 0x284):
        m.w32(p + off, 0)
    m.w32(p + 0x384, NO_BRAIN)
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, pos[k])
        m.w32(p + 0x270 + 4 * k, 0x10000)
    for off, v in ((6, 0x3FF), (2, 1), (4, 1), (0xA, 0x3E0), (8, 0), (0xC, 0xFD), (0xE, 4)):
        m.w16(o + off, v)
    for i in range(n):
        q = o + 0x10 + 0x24 * i
        r = eng.rand.draw(0x800702E0)
        for k in range(3):
            m.w32(q + 4 + 4 * k, s32(pos[k] + ((((r >> (4 * k)) & 0xF) - 8) << 16)))
        eng.rand.draw(0x800702E0)
        eng.rand.draw(0x800702E0)
        for k in range(3):
            m.w8(q + 0x1C + k, c0[k])
            m.w8(q + 0x20 + k, c0[k])
    return p


def impact_effect_think(eng, e):
    """impact_effect_think (0x800701DC): the life down; the colours halved in the last 3 ticks."""
    m = eng.m
    o = m.u32(e + 0x388)
    t = m.u8(o + 1)
    if not t:
        eng.remove(e, "impact effect done")
        return
    t -= 1
    m.w8(o + 1, t)
    if t < 3:
        for i in range(m.u8(o)):
            q = o + 0x10 + 0x24 * i
            for off in (0x1C, 0x1D, 0x1E, 0x20, 0x21, 0x22):
                m.w8(q + off, m.u8(q + off) >> 1)


# ---------------------------------------------------------------- particles

def particles_spawn(eng, pos, spread, n, vel_xz, vel_y, col, col_rand, a7, a8, a9, a10, a11, a12, smoke_on, smoke_n):
    """particles_spawn (0x80073BA8): a type 0x31 entity with a system of n sparks (and one of smoke
    puffs), linked at the front. Each particle: +0 state, +1 delay, +2 life, +4 position, +0x10
    velocity, +0x1C rotation and +0x24 its speed, +0x2C colour."""
    m = eng.m
    size = n * 0x34 + 0x64 + (smoke_n * 0x34 if smoke_on else 0)
    p = eng.entity_create(0x31, obj_size=size)
    o = m.u32(p + 0x388)
    rd = lambda: eng.rand.draw(0x80073BA8)
    m.w32(p + 8, 0x31)
    m.w32(p + 0x294, 0xB4)
    m.w32(p + 0x290, 0xB4)
    m.w32(p + 0x36C, PARTICLES_THINK)
    m.w32(p + 0x370, PARTICLES_DRAW)
    m.w32(p + 0x38, 0)
    m.w32(p + 0xC, 0x01028000)
    m.w32(p + 0x374, 0)
    m.w32(p + 0x378, 0)
    m.w32(p + 0x384, NO_BRAIN)
    m.w32(p + 0x284, 0)
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x203)
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, pos[k])
        m.w32(p + 0x258 + 4 * k, pos[k])
    arr = o + 0x64
    sa = o + 0xC
    m.w32(o, 0)
    m.w32(o + 4, 0)
    m.w32(sa + 0x28, arr)
    m.w32(sa + 4, n)
    m.w32(sa, 1)
    m.w32(sa + 0xC, 0x20000)
    m.w32(sa + 0x10, 0x20000)
    m.w32(sa + 0x14, 0x4B0000)
    m.w32(sa + 8, a12)
    for off, v in ((0x18, a7), (0x1A, a8), (0x1C, a9), (0x1E, a10), (0x20, a11), (0x22, a11), (0x24, 8), (0x26, 0)):
        m.w16(sa + off, v)
    s5, s4 = spread * 2, vel_y * 2
    for i in range(n):
        q = arr + 0x34 * i
        m.w8(q, 1)
        m.w8(q + 1, 0)
        m.w8(q + 2, ((rd() * 0xA) >> 15) + 0x19)
        for k in range(3):
            c = m.s32(p + 0x20C + 4 * k)
            m.w32(q + 4 + 4 * k, s32(c + (-spread << 16)) if s5 == 0 else s32(c + ((-spread + ((rd() * s5) >> 15)) << 16)))
        for k in range(3):
            m.w32(q + 0x10 + 4 * k, s32(-vel_xz << 16) if s4 == 0 else s32((-vel_xz + ((rd() * s4) >> 15)) << 16))
        for k in range(3):
            m.w16(q + 0x1C + 2 * k, rd() >> 3)
        m.w16(q + 0x24, 0x96)
        m.w16(q + 0x26, 0x64)
        m.w16(q + 0x28, 0xC8)
        c = col
        if col_rand:
            c += (rd() * col_rand) >> 15
        for k in range(3):
            m.w8(q + 0x2C + k, c)
        m.w8(q + 3, 1 if ((rd() * 100) >> 15) >= 0x33 else 0)
        m.w16(q + 0x30, ((rd() * 7) >> 15) + 5)
        m.w16(q + 0x32, ((rd() * 7) >> 15) + 5)
    arr2 = arr + 0x34 * n
    sb = o + 0x38
    if smoke_on:
        m.w32(sb + 4, smoke_n)
        m.w32(sb + 0x28, arr2)
        m.w32(sb, 1)
        m.w32(sb + 8, a12)
        m.w32(sb + 0x10, s32(-4))
        m.w32(sb + 0x14, 0x960000)
        for off, v in ((0x18, 0x3E8), (0x1C, 0x3E0), (0x1E, 0xFA), (0x20, 0x1F), (0x22, 0x1F), (0x1A, 0), (0x24, 8),
                       (0x26, 2)):
            m.w16(sb + off, v)
        m.w32(sb + 0xC, 2)
        for i in range(smoke_n):
            q = arr2 + 0x34 * i
            m.w8(q, 1)
            m.w8(q + 1, i)
            m.w8(q + 2, ((rd() * 2) >> 15) + 0x14)
            for k in range(3):
                m.w32(q + 4 + 4 * k, m.u32(p + 0x20C + 4 * k))
            m.w32(q + 0x10, 0)
            m.w32(q + 0x14, s32(((-(rd() * 2) >> 15) - 2) << 16))
            m.w16(q + 0x30, 5)
            m.w32(q + 0x18, 0)
            for off in (0x1C, 0x1E, 0x20, 0x24, 0x26, 0x28):
                m.w16(q + off, 0)
            m.w16(q + 0x32, 6)
    else:
        m.w32(sb + 4, 0)
        m.w32(sb + 0x28, 0)
        m.w32(sb, 2)
    eng.link_front(p)
    return p


def particles_think(eng, e):
    """0x80073B08: both systems stepped (0x80073960); removed once both are done."""
    m = eng.m
    o = m.u32(e + 0x388)
    for flag, sys_ in ((o, o + 0xC), (o + 4, o + 0x38)):
        if m.s32(sys_) == 2:
            m.w32(flag, 1)
        else:
            particles_step(m, sys_)
    if m.s32(o + 4) and m.s32(o):
        eng.remove(e, "particles done")


def particles_step(m, sys_):
    """0x80073960: each live particle's life down (then dead), and after its delay it moves:
    gravity up to the limit, drag on x and z, the rotation."""
    arr = m.u32(sys_ + 0x28)
    m.w32(sys_, 2)
    for i in range(m.s32(sys_ + 4)):
        q = arr + 0x34 * i
        if m.u8(q) & 2:
            continue
        life = m.u8(q + 2)
        if life:
            m.w8(q + 2, life - 1)
        else:
            m.w8(q, 2)
        d = m.u8(q + 1)
        if d:
            m.w8(q + 1, d - 1)
        else:
            vy = m.s32(q + 0x14)
            g = m.s32(sys_ + 0x10)
            if abs(vy) < abs(m.s32(sys_ + 0x14)) and g:
                m.w32(q + 0x14, s32(vy + g))
            vz = m.s32(q + 0x18)
            m.w32(q + 4, s32(m.s32(q + 4) + m.s32(q + 0x10)))
            m.w32(q + 0xC, s32(m.s32(q + 0xC) + vz))
            m.w32(q + 8, s32(m.s32(q + 8) + m.s32(q + 0x14)))
            drag = m.s32(sys_ + 0xC)
            for off, v in ((0x18, vz), (0x10, m.s32(q + 0x10))):
                if drag < abs(v):
                    m.w32(q + off, s32(v - drag) if v > 0 else s32(v + drag))
                else:
                    m.w32(q + off, 0)
            for k in range(3):
                m.w16(q + 0x1C + 2 * k, (m.u16(q + 0x1C + 2 * k) + m.u16(q + 0x24 + 2 * k)) & 0xFFFF)
        m.w32(sys_, 1)


# ---------------------------------------------------------------- the shell casing

def casing_spawn(eng, owner, pos, pitch):
    """casing_spawn (0x80088040): at most 5 pending; lands (a sound) 8..23 ticks later."""
    m = eng.m
    if m.u32(G_CASINGS) >= 5:
        return None
    p = eng.entity_create(0x66, obj_size=0xC, instance=True)
    o = m.u32(p + 0x388)
    m.w32(p + 8, 0x66)
    m.w32(p + 0xC, 0x07020000)
    m.w32(p + 0x10, 0x213)
    m.w32(p + 0x36C, CASING_THINK)
    for off in (0x374, 0x370, 0x378, 0x37C, 0x14):
        m.w32(p + off, 0)
    m.w32(p + 0x384, NO_BRAIN)
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, pos[k])
    m.w32(o, (eng.rand.draw(*FN["casing"]) & 0xF) + 8)
    handle_store(m, o + 4, owner)
    m.w32(o + 8, pitch)
    m.w32(G_CASINGS, m.u32(G_CASINGS) + 1)
    eng.link_front(p)
    return p


def casing_think(eng, e):
    m = eng.m
    o = m.u32(e + 0x388)
    t = s32(m.s32(o) - 1)
    m.w32(o, t)
    if t == 0:
        snd = (eng.rand.draw(*FN["casing"]) & 1) | 0x10
        eng.audio.play(0, snd, e, 0xCCCC, 0, m.s32(o + 8), 0x199B, 0, 0, 0, 0)
        m.w32(G_CASINGS, m.u32(G_CASINGS) - 1)
        eng.remove(e, "casing landed")


# ---------------------------------------------------------------- hits on entities

def bullet_touch(eng, e, other):
    """bullet_touch (0x80043AD4): not its shooter nor another projectile; with flags B 4 the path
    against the object's planes (0x8004B624; characters have none: 0). A hit on anything but type
    0x1777: the projectile moved to the hit point, +0x364 = the reversed normal; explosive rounds
    explode; on a target with flags B 0x4000 the impact sound and sparks; a soldier's ricochet
    sound; removed. Then (flags B 4) the target's own touch. 0x1777 lets it fly on."""
    m = eng.m
    o = m.u32(e + 0x388)
    if handle_same(eng, o + 0x54, other) or m.s32(other + 8) in PROJECTILE_TYPES:
        return 0
    if not m.u32(e + 0x10) & 4:
        eng.note("bullet_touch without a sweep (sphere_vs_object_planes) not implemented")
        return 0
    from engine_planes import projectile_planes
    hit = projectile_planes(eng, e, other)
    if hit is None:
        return 0
    res = 0
    if m.s32(other + 8) != 0x1777:
        res = 1
        point, normal, _ = hit
        for k in range(3):
            m.w32(e + 0x20C + 4 * k, s32(point[k] << 16))
            m.w16(e + 0x364 + 2 * k, s16(-normal[k]))
        if m.u16(o + 0x38) & 0x100:
            eng.note("explosive round's hit on an object not implemented")
            eng.remove(e, "explosive round")
            return 1
        if m.u32(other + 0x10) & 0x4000:
            impact_sound(eng, e)
            n = ((eng.rand.draw(*FN["hit"]) * 3) >> 15) + 2
            smoke = ((eng.rand.draw(*FN["hit"]) * 4) >> 15) + 6
            particles_spawn(eng, [m.s32(e + 0x20C + 4 * k) for k in range(3)], 1, n, 0x1E, 0x19, 0xF, 0x64,
                            0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke)
        if m.s32(other + 8) == 0x1402:
            snd = (eng.rand.draw(*FN["hit"]) >> 15) + 0xC
            eng.audio.play(0, snd, e, 0x20000, 0, 0x10000, 0x4CCD, 0, 0, 0, 0)
        eng.remove(e, "bullet hit an object")
    from engine_combat import call
    call(eng, "touch", m.u32(other + 0x37C), other, e)
    return res


def projectile_hit_part(eng, e, part, other, opart):
    """The projectile's hit_part (0x80043E38): not its shooter, another projectile or type 0x25:
    damage_from_hit, then removed (1)."""
    m = eng.m
    o = m.u32(e + 0x388)
    if handle_same(eng, o + 0x54, other):
        return 0
    t = m.s32(other + 8)
    if t in PROJECTILE_TYPES or t == 0x25:
        return 0
    from engine_combat import damage_from_hit
    damage_from_hit(eng, e, part, other, opart)
    if m.u16(o + 0x38) & 0x100:
        eng.note("explosive round's hit not implemented")
    if t == 0x1402:
        snd = (eng.rand.draw(*FN["hit"]) >> 15) + 0xC
        eng.audio.play(0, snd, e, 0x20000, 0, 0x10000, 0x4CCD, 0, 0, 0, 0)
    eng.remove(e, "bullet hit")
    return 1
