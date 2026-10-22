"""The player's death (docs/re/movement.md, Death; hud.md, Fades): player_death_start (0x80079D10),
player_think_dead (0x80079A50), the fade entity (fade_start 0x8004F748, fade_think 0x8004F488) and
the death camera (camera mode 0x800: the camera entity runs its animation scripts, a physical
walker at the soldier's place; 0x80046EB4 its look points from the animation)."""
from engine import s16, s32, G_PLAYER, G_LAST_EXPLOSION
from engine_feed import FN

PLAYER_THINK_DEAD = 0x80079A50
FADE_THINK = 0x8004F488
FADE_DRAW = 0x8004F670
CAM_TOUCH = 0x800476C4
CAM_WORLD_CONTACT = 0x800476E8
G_LEVEL_FLAGS = 0x8009C840


def register(eng):
    eng.think[PLAYER_THINK_DEAD] = lambda e: player_think_dead(eng, e)
    eng.think[FADE_THINK] = lambda e: fade_think(eng, e)
    eng.handlers[("world_contact", CAM_WORLD_CONTACT)] = lambda e, r: cam_world_contact(eng, e, r)
    eng.handlers[("touch", CAM_TOUCH)] = lambda e, o: cam_touch(eng, e, o)


def cam_world_contact(eng, e, res):
    """0x800476E8, the death camera's: landing stops it."""
    m = eng.m
    if res & 1:
        m.w32(e + 0x200, 0)
        m.w32(e + 0x38, m.u32(e + 0x38) | 2)
    return 0


def cam_touch(eng, e, o):
    """0x800476C4: pushed out of objects' planes."""
    from engine_planes import sphere_vs_object_planes
    return 1 if sphere_vs_object_planes(eng, e, o)[0] else 0


def fade_start(eng, mode, delay, length, hold, c0, c1):
    """fade_start (0x8004F748): a type 0x32 entity (object 0x74: +0 mode, +4 delay, +8 the ramp's
    ticks, +0xC hold, +0x10.. the per-tick steps, +0x18.. the start colour; its prims drawing
    only), drawn from the start when there is no delay; linked at the front."""
    m = eng.m
    e = eng.entity_create(0x32, obj_size=0x74)
    if not e:
        return 0
    m.w32(e + 8, 0x32)
    m.w32(e + 0xC, 0x20000)
    m.w32(e + 0x10, 0x203)
    m.w32(e + 0x14, 0)
    m.w32(e + 0x36C, FADE_THINK)
    m.w32(e + 0x370, 0 if delay else (FADE_DRAW if mode & 1 else 0))
    m.w32(e + 0x374, 0)
    m.w32(e + 0x378, 0)
    m.w32(e + 0x384, 0x8006A318)
    o = m.u32(e + 0x388)
    m.w32(o, mode)
    m.w32(o + 4, delay)
    m.w32(o + 8, length)
    m.w32(o + 0xC, hold)
    for k in range(3):
        m.w8(o + 0x18 + k, c0[k])
        m.w8(o + 0x24 + k, c1[k])
        m.w16(o + 0x10 + 2 * k, int((c1[k] - c0[k]) / length) if length else 0)
    if mode & 4:
        m.w32(o + 0x6C, 0x300000)
        m.w32(o + 0x70, 0x300000 // length if length else 0)
    eng.link_front(e)
    return e


def fade_think(eng, e):
    """fade_think (0x8004F488): the delay, then the ramp, then the hold count down; then removed."""
    m = eng.m
    o = m.u32(e + 0x388)
    for off in (4, 8):
        if m.s32(o + off) > 0:
            m.w32(o + off, m.s32(o + off) - 1)
            return
    if m.s32(o + 0xC) > 0:
        m.w32(o + 0xC, m.s32(o + 0xC) - 1)
        return
    eng.remove(e, "fade done")


def player_death_start(eng, e, killer):
    """player_death_start (0x80079D10): the soldier's think becomes player_think_dead, its hit,
    touch and contact handlers cleared. With a camera animation: the soldier's death script (0xF),
    the camera in mode 0x800 (easing shift 3), a physical walker at the soldier's place with the
    death camera's touch and contact, its variable 2 the death animation (30 crouched, 20 killed
    by an explosion, else 50% 10..12, 50% 0..2); the soldier stops moving itself. Without one:
    the fade (delay 50, 15 ticks, hold 120). Then aim, scope and zoom flags cleared."""
    m = eng.m
    inv = m.u32(e + 0x388)
    cam = m.u32(G_PLAYER)
    co = m.u32(cam + 0x388)
    m.w32(e + 0x36C, PLAYER_THINK_DEAD)
    m.w32(e + 0x374, 0)
    m.w32(e + 0x378, 0)
    m.w32(e + 0x37C, 0)
    cinst = m.u32(cam + 0x100)
    if cinst and m.u32(cinst + 0x74):
        m.w32(e + 0x38, m.u32(e + 0x38) & ~0x4000)
        from engine_scripts import EVM
        EVM(eng, e).script_start(0xF, 0, 0)
        m.w32(co + 0x9C, 0x800)
        m.w32(co + 0x10, 3)
        m.w32(e + 0x228, 0)
        m.w32(e + 0x230, 0)
        m.w32(cam + 0xC, 0x102A)
        m.w32(cam + 0x10, 0x200)
        m.w32(cam + 0x37C, CAM_TOUCH)
        m.w32(cam + 0x14, 0x08000400)
        m.w32(cam + 0x378, CAM_WORLD_CONTACT)
        m.w32(cam + 0x284, m.u32(e + 0x284))
        for o in (0x20C, 0x210, 0x214, 0x228, 0x22C, 0x230, 0x258, 0x25C, 0x260, 0x264, 0x268, 0x26C,
                  0x270, 0x274, 0x278):
            m.w32(cam + o, m.u32(e + o))
        m.copy(cam + 0x2D8, e + 0x2D8, 0x10)
        m.w16(cam + 0x2E8, m.u16(e + 0x2E8))
        m.w16(cam + 0x2EC, m.u16(e + 0x2EC))
        m.w32(cam + 0x2D4, m.u32(e + 0x2D4))
        m.w16(cam + 0x3C8, m.u16(e + 0x3C8))
        m.w32(cam + 0x208, m.u32(e + 0x208))
        rd = lambda: eng.rand.draw(*FN["player_death"])
        if m.u32(inv) & 2:
            v = 0x1E
        elif killer == 0x31:
            v = 0x14
            m.w32(co + 0x10, 3)
        elif ((rd() * 0x64) >> 15) < 0x32:
            v = ((rd() * 3) >> 15) + 0xA
        else:
            v = (rd() * 3) >> 15
        m.w32(cam + 0x4C + 4 * 2, v)
        m.w32(e + 0xC, (m.u32(e + 0xC) & ~0x28) | 0x20000)
        m.w32(e + 0x10, m.u32(e + 0x10) | 0x13)
    else:
        fade_start(eng, 1, 0x32, 0xF, 0x78, (0, 0, 0), (0xFF, 0xFF, 0xFF))
    m.w32(inv, m.u32(inv) & ~0x20 & ~0x40 & ~0x100 & 0xFFDFFFFF & ~0x4000)
    m.w32(e + 0xC, m.u32(e + 0xC) | 0x20)
    m.w32(e + 0x10, m.u32(e + 0x10) & ~0x10)


def player_think_dead(eng, e):
    """player_think_dead (0x80079A50): with the death camera the soldier copies its position and
    angles; when the camera's script has ended (its variable 2 is -1) the fade (delay 50, 15
    ticks); when that is gone the level is lost (0x8009C840 | 2, contact 0x4000). Then the
    soldier's own scripts."""
    m = eng.m
    inv = m.u32(e + 0x388)
    cam = m.u32(G_PLAYER)
    cinst = m.u32(cam + 0x100)
    co = m.u32(cam + 0x388)
    if cinst and m.u32(cinst + 0x74) and m.u32(co + 0x9C) & 0x800:
        m.w32(e + 0x284, m.u32(cam + 0x284))
        for o in (0x20C, 0x210, 0x214, 0x228, 0x22C, 0x230, 0x258, 0x25C, 0x260, 0x264, 0x268, 0x26C):
            m.w32(e + o, m.u32(cam + o))
        if m.s16(e + 0x11C) == -1:
            m.w32(inv, m.u32(inv) | 0x8000)
        if m.u32(inv) & 0x10000:
            from engine_sense import find_type_group
            if not find_type_group(m, 0x32, m.s8(inv + 0x152)):
                m.w32(G_LEVEL_FLAGS, m.u32(G_LEVEL_FLAGS) | 2)
                m.w32(e + 0x38, m.u32(e + 0x38) | 0x4000)
        elif m.s32(cam + 0x54) == -1:
            f = fade_start(eng, 1, 0x32, 0xF, 0, (0, 0, 0), (0xFF, 0xFF, 0xFF))
            if f:
                m.w32(inv, m.u32(inv) | 0x10000)
                m.w8(f + 0x35, m.u8(inv + 0x152))
            else:
                m.w32(G_LEVEL_FLAGS, m.u32(G_LEVEL_FLAGS) | 2)
                m.w32(e + 0x38, m.u32(e + 0x38) | 0x4000)
    elif m.u32(e + 0xC) & 0x100000:
        m.w32(G_LAST_EXPLOSION, 0)
        m.w32(G_LEVEL_FLAGS, m.u32(G_LEVEL_FLAGS) | 0x202)
        eng.note("player death without a death camera (white fades)")
    from engine_scripts import EVM
    vm = EVM(eng, e)
    vm.run_b()
    if m.s32(e + 0x288) < 0:
        m.w32(e + 0x288, 1)
        m.w32(e + 0x28C, 1)
        m.w32(e + 0x22C, s32(m.s32(e + 0x22C) + 0x08000000))
    vm.run_a()


def cam_local(m, e, x, y, z):
    """The camera entity's matrix (+0x340) and translation (+0x354) applied to a short vector."""
    import engine_gte as gt
    R = [[m.s16(e + 0x340 + 6 * i + 2 * j) for j in range(3)] for i in range(3)]
    t = [m.s32(e + 0x354 + 4 * k) for k in range(3)]
    mac, _ = gt.mul(R, [s16(x), s16(y), s16(z)], t, 12)
    return [s16(v) for v in mac]


def death_points(eng, e, co):
    """0x80046EB4: the death camera's targets from its animation: the joint of bone model+0xD4
    (co+0x54..), and that bone's rotation (the pose's quaternion) applied to the model's offsets
    +0xD8 and +0xE0, plus the joint: the look point (co+0x24..) and the side point (co+0x84..)."""
    import combat_sim as cs
    import engine_gte as gt
    m = eng.m
    inst = m.u32(e + 0x100)
    model = m.u32(inst + 8)
    bone = m.s16(model + 0xD4)
    jp = m.u32(inst + 0x94) + 8 * bone
    j = [m.s16(jp + 2 * k) for k in range(3)]
    for k in range(3):
        m.w32(co + 0x54 + 4 * k, s32(j[k] << 16))
    qb = m.u32(inst + 0x88) or m.u32(inst + 0x8C) or m.u32(inst + 0x90)
    q = qb + 8 * bone
    R = cs.quat_matrix((m.s16(q), m.s16(q + 2), m.s16(q + 4), m.s16(q + 6)))
    for off, dst in ((0xD8, 0x24), (0xE0, 0x84)):
        v = [m.s16(model + off + 2 * k) for k in range(3)]
        mac, _ = gt.mul(R, v, j, 12)
        for k in range(3):
            m.w32(co + dst + 4 * k, s32(s16(mac[k]) << 16))
