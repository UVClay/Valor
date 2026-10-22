"""The mounted gun, type 0x1772 (docs/re/enemy_types.md, The mounted gun and its gunner;
objects.md, firing): mounted_gun_spawn (0x8005E2AC) and mounted_gun_think (0x8005DA24). Its
scripts take events from the gunner (soldier scripts) or the player; while variable 5 is set it
fires every third tick: at the player's middle when a soldier works it, along the gun when the
player does (variable 9)."""
from engine import s16, s32, G_PLAYER, G_SOLDIER
from engine_feed import FN

MOUNTED_GUN_THINK = 0x8005DA24
MOUNTED_GUN_DRAW = 0x8005D8B0
MOUNTED_GUN_TOUCH = 0x8005E288
G_GROUP = 0x80099EC9
G_STAT_SHOTS = 0x80039CAC


def register(eng):
    eng.think[MOUNTED_GUN_THINK] = lambda e: mounted_gun_think(eng, e)
    eng.handlers[("touch", MOUNTED_GUN_TOUCH)] = lambda e, o: gun_touch(eng, e, o)


def gun_touch(eng, e, o):
    """0x8005E288: the object planes."""
    from engine_planes import sphere_vs_object_planes
    return 1 if sphere_vs_object_planes(eng, e, o)[0] else 0


def mounted_gun_spawn(eng, rec, model):
    """mounted_gun_spawn (0x8005E2AC): entity_create(3, object 0x88, the model, instance flags
    0x157, no parts, 10 channels); a brain (head bone 13, both ranges 3000, field of view 0x200,
    turn 1.0); flags B 0x40211, A 0x03038000, mask 0x411; the record's cull counter (+0x5E), group
    0x80099EC9, health (+0x5C), position (+0x10) and angles (+0x18) (also its old position);
    its yaw kept in variable 7 (+0x68); sphere 100 centred half up; the muzzle flash at vertex
    0x38 (flash_setup 0x8006DCAC)."""
    m = eng.m
    r = rec.addr
    e = eng.entity_create(m.s32(model + 0xFC), obj_size=0x88, model=model, parts=0, channels=0xA,
                          instance=True, iflags=0x157)
    if not e:
        return None
    from engine_enemy import brain_alloc
    b = brain_alloc(eng)
    m.w32(e + 0x38C, b)
    m.w32(e + 0x10, 0x40211)
    m.w32(e + 0xC, 0x03038000)
    m.w32(e + 0x14, 0x411)
    m.w32(e + 8, m.s32(model + 0xFC))
    cv = m.s16(r + 0x5E)
    m.w32(e + 0x294, cv)
    m.w32(e + 0x290, cv)
    g = m.u8(G_GROUP)
    m.w8(G_GROUP, g + 1)
    m.w8(e + 0x35, g)
    m.w32(e + 0x5C, 0)
    hp = m.u8(r + 0x5C) << 16
    m.w32(e + 0xF0, hp)
    m.w32(e + 0xF4, hp)
    for k in range(3):
        m.w32(e + 0x20C + 4 * k, s32(m.s16(r + 0x10 + 2 * k) << 16))
        m.w32(e + 0x264 + 4 * k, s32(m.s16(r + 0x18 + 2 * k) << 16))
        m.w32(e + 0x258 + 4 * k, s32(m.s16(r + 0x10 + 2 * k) << 16))
    m.w32(e + 0x36C, MOUNTED_GUN_THINK)
    m.w32(e + 0x370, MOUNTED_GUN_DRAW)
    m.w32(e + 0x384, 0x8006A37C)
    m.w32(e + 0x37C, MOUNTED_GUN_TOUCH)
    m.w16(e + 0x2EC, 0)
    m.w32(e + 0x74, 0)
    m.w32(e + 0x70, 0)
    m.w32(e + 0x374, 0)
    m.w32(e + 0x378, 0)
    m.w32(e + 0x68, m.u32(e + 0x268))
    m.w16(b + 0xAA, 0xD)
    m.w16(b + 0xB0, 0xBB8)
    m.w16(b + 0xB2, 0xBB8)
    m.w16(b + 0xB4, 0x200)
    m.w16(b + 0xB6, -1)
    m.w16(b + 0xC0, 0)
    m.w16(b + 0xC2, 0)
    m.w32(b + 0xDC, 0x10000)
    m.w16(e + 0x2E8, 0x64)
    m.w16(e + 0x3C8, 0x64)
    m.w16(e + 0x2DA, -(0x64 >> 1))
    m.w16(b + 0xC4, m.u16(e + 0x2EC))
    for o in (0xD2, 0xD0, 0xCE, 0xCC):
        m.w16(b + o, 0)
    m.w16(b + 0xC6, m.u16(e + 0x2E8))
    # flash_setup(object, entity, vertex 0x38): the flash's vertex, counter 0, its holder
    o = m.u32(e + 0x388)
    m.w16(o, 0x38)
    m.w16(o + 2, 0)
    m.w32(o + 0x18, m.u32(e))
    m.w32(o + 0x1C, 0)
    return e


def var(m, e, n):
    return m.s32(e + 0x4C + 4 * n)


def set_var(m, e, n, v):
    m.w32(e + 0x4C + 4 * n, s32(v))


def mounted_gun_think(eng, e):
    """mounted_gun_think (0x8005DA24): not attached while variable 4 was: contact 0x2000; the
    event matcher, the channels with variable 8 = its yaw (and back); the player working it
    (variable 9) not done; firing: while variable 5, every third tick (variable 6 counts 2
    down), a bullet (0x13F4, damage 5.0, vertices 0x94 -> 0x38) at the player's middle when a
    soldier works it (mask 0x08000401, flags A 0x800), the flash, a recoil jitter (variables
    11..13: rand ±10, ±33, ±20, the last two onto the bullet's pitch and yaw), the sound 1:0xB."""
    m = eng.m
    from engine_scripts import EVM
    if not m.u32(e + 0x18) & 1 and var(m, e, 4):
        m.w32(e + 0x38, m.u32(e + 0x38) | 0x2000)
    vm = EVM(eng, e)
    vm.run_b()
    set_var(m, e, 8, m.s32(e + 0x22C))
    vm.run_a()
    m.w32(e + 0x22C, var(m, e, 8))
    if var(m, e, 9):
        eng.note("mounted gun worked by the player not implemented")
    elif m.u32(e + 0x18) & 1:
        p = eng.resolve(m.u32(e + 0x2C))
        if p:
            m.w32(p + 0x10, m.u32(p + 0x10) | 0x4000)
    rd = lambda: eng.rand.draw(*FN["mounted_gun"])
    if var(m, e, 5):
        c = var(m, e, 6)
        if c > 0:
            set_var(m, e, 6, c - 1)
            for k in (0xB, 0xC, 0xD):
                set_var(m, e, k, 0)
        else:
            from engine_projectile import bullet_spawn
            sol = m.u32(G_SOLDIER)
            tgt = (m.s16(sol + 0x20E), s16(s32(m.s32(sol + 0x210) - (m.s32(sol + 0x2D4) >> 1)) >> 16),
                   m.s16(sol + 0x216))
            p = bullet_spawn(eng, e, 0x94, 0x38, tgt, 0, 0, 0, 0x50000, 0, 0, 0x13F4, 0, 0, 0)
            if p:
                m.w32(p + 0x14, 0x08000401)
                m.w32(p + 0xC, m.u32(p + 0xC) | 0x800)
            o = m.u32(e + 0x388)
            m.w16(o + 2, 1)                      # the muzzle flash (0x8006DCA0)
            set_var(m, e, 0xB, ((rd() * 0x14) >> 15) - 0xA)
            set_var(m, e, 0xC, ((rd() * 0x42) >> 15) - 0x21)
            set_var(m, e, 0xD, ((rd() * 0x28) >> 15) - 0x14)
            if p:
                m.w32(p + 0x228, s32(m.s32(p + 0x228) + (var(m, e, 0xC) << 16)))
                m.w32(p + 0x22C, s32(m.s32(p + 0x22C) + (var(m, e, 0xD) << 16)))
            eng.audio.play(1, 0xB, e, 0x20000, 0, 0x10000, 0x1800, 0, 0, 2, 0)
            set_var(m, e, 6, 2)
    else:
        set_var(m, e, 6, 0)
        for k in (0xB, 0xC, 0xD):
            set_var(m, e, k, 0)
    set_var(m, e, 4, m.u32(e + 0x18) & 1)
    set_var(m, e, 0xA, var(m, e, 9))
