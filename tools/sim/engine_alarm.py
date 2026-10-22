"""The alarm (docs/re/audio.md, Pickups, interface, alarm; enemies.md op 49): raised by the
soldiers' op 49 (op_alarm, 0x8005C710) or the level's level_set(4, id) (alarm_from_level,
0x8005F068); an entity (alarm_spawn, 0x8005EA54) that re-triggers one sound, timed by the vblank
counter (VSync(-1)), until 1801 thinks or until its node's record is switched off."""
from engine import s16, s32

G_ALARM = 0x80097FE0                 # the alarm node's index, -1 none
G_ALARM_ENT = 0x80097FE4
G_PARAMS = 0x800939F4                # +0 mission, +4 level
G_CHEATS = 0x800386A4
ALARM_THINK = 0x8005EDF8
ALARM_DESTROY = 0x8005F008


def register(eng):
    eng.think[ALARM_THINK] = lambda e: alarm_think(eng, e)
    eng.handlers[("destroy", 0x20)] = lambda e: alarm_destroy(eng, e)


def node_rec(m, node):
    """The node's link (+0x2C) into the world object's table +0x14 (8 bytes: +0 flags, +4 the
    record)."""
    wo = m.u32(m.u32(0x8009D648) + 0x388)
    n = m.u32(wo + 8) + 0x34 * node
    return n, m.u32(wo + 0x14) + 8 * m.s32(n + 0x2C)


def alarm_spawn(eng, sound, bank, delay, rnd, volume, vspread, pitch, pspread, node):
    """alarm_spawn: an entity of type 0x20 with a 0x2C-byte object (+0 node, +4 thinks, +8 the
    next vblank, +0xC delay, +0x10 random delay, +0x14 volume, +0x18 its spread, +0x1C pitch,
    +0x20 its spread, +0x24 sound, +0x26 voice (-1), +0x27 bank, +0x28 bit 0x80 waiting), not
    positional (flags A 0x40000000), flags B | 0x200; the pitch by mission: 1.0 in 4, 8, 9 and 3
    (0.5 in 3:1), 0.5 in 2 and 5, else the caller's; the start chime 0:0x13. (Cheat 4: a random
    sound of 25, not done.)"""
    m = eng.m
    e = eng.entity_create(0x20, obj_size=0x2C)
    m.w32(e + 8, 0x20)
    m.w32(e + 0x36C, ALARM_THINK)
    m.w32(e + 0x384, ALARM_DESTROY)
    for k in (0x14, 0x370, 0x374, 0x378):
        m.w32(e + k, 0)
    m.w32(e + 0x10, m.u32(e + 0x10) | 0x200)
    o = m.u32(e + 0x388)
    if m.u32(G_CHEATS) & 4:
        eng.note("alarm cheat 4 (random sounds) not implemented")
    mission, level = m.s32(G_PARAMS), m.s32(G_PARAMS + 4)
    if mission in (4, 8, 9):
        pitch = 0x10000
    elif mission == 3:
        pitch = 0x8000 if level == 1 else 0x10000
    elif mission in (2, 5):
        pitch = 0x8000
    m.w8(o + 0x27, bank)
    m.w16(o + 0x24, sound)
    m.w32(o + 8, 0)
    m.w32(o + 0xC, delay)
    m.w32(o + 0x10, rnd)
    m.w32(o + 0x14, volume)
    m.w32(o + 0x1C, pitch)
    m.w32(o + 0x18, vspread)
    m.w8(o + 0x28, 0)
    m.w32(o + 0x20, pspread)
    m.w8(o + 0x26, 0xFF)
    m.w32(o + 4, 0)
    m.w32(o, node)
    m.w32(e + 0xC, m.u32(e + 0xC) | 0x40000000)
    eng.audio.play(0, 0x13, e, 0x10000, 0, 0x10000, 0x3333, 0, 0, 0, 0)
    return e


def raised(eng, a, node, src):
    """After a spawn: g_alarm, the entity at the front, every living soldier (but the raiser)
    gets +0x3C 0x100, and every entity of the node's record sends message 0x2B."""
    m = eng.m
    m.w32(G_ALARM, node)
    eng.link_front(a)
    m.w32(G_ALARM_ENT, a)
    for o in list(eng.entities()):
        if m.s32(o + 8) == 0x1402 and not m.u32(o + 0xC) & 0x20080000 and o != src:
            m.w32(o + 0x3C, m.u32(o + 0x3C) | 0x100)
    _, rec = node_rec(m, node)
    for o in list(eng.entities()):
        if m.u32(o + 0x1C) == rec:
            eng.logic.send(0x2B, eng.ents.get(o), 0, None, src="alarm")


def op_alarm(vm, node, sound, pitch, pspread, volume, vspread, bank):
    """Op 49 (0x8005C710): when no alarm is up: bank -1 is the model's (+0xC8); delay 10."""
    eng, m, e = vm.eng, vm.m, vm.e
    if m.s32(G_ALARM) >= 0:
        return
    if s32(bank) == -1:
        mdl = m.u32(m.u32(e + 0x100) + 8)
        bank = m.u16(mdl + 0xC8) if mdl else 0
    a = alarm_spawn(eng, sound & 0xFFFF, bank & 0xFF, 0xA, 0, volume, vspread, pitch, pspread, node)
    if a:
        raised(eng, a, node, e)


def alarm_from_level(eng, nid):
    """level_set(4, id): the node whose +4 is id (the last such); if its record isn't switched
    off (record +0x34 == node +4 with the table flag 2): with an alarm up, it is ended (its
    thinks set to 1801); else 0:0x2C, delay 10, volume 0x20000, pitch 0.5 (by mission)."""
    m = eng.m
    wo = m.u32(m.u32(0x8009D648) + 0x388)
    node = -1
    for i in range(m.s32(wo + 0xC)):
        if m.s32(m.u32(wo + 8) + 0x34 * i + 4) == nid:
            node = i
    if node < 0:
        return
    n, ent = node_rec(m, node)
    if m.s16(m.u32(ent + 4) + 0x34) == m.s16(n + 4) and m.u32(ent) & 2:
        return
    if m.s32(G_ALARM) != -1:
        a = m.u32(G_ALARM_ENT)
        if a:
            m.w32(m.u32(a + 0x388) + 4, 0x709)
        return
    a = alarm_spawn(eng, 0x2C, 0, 0xA, 0, 0x20000, 0, 0x8000, 0, node)
    if a:
        raised(eng, a, node, 0)


def alarm_think(eng, e):
    """alarm_think: thinks + 1; at 1801, or with its node's record switched off, removed. Not
    waiting: when its voice is free (none yet, or its status not 1) the next play is due now (a
    first sound) or after the delay (+ rand % random); then, waiting, when the vblank counter
    reaches it, the sound plays again."""
    m = eng.m
    o = m.u32(e + 0x388)
    vb = eng.vblank
    t = m.s32(o + 4) + 1
    m.w32(o + 4, t)
    n, ent = node_rec(m, m.s32(o))
    if t >= 0x709 or (m.s16(m.u32(ent + 4) + 0x34) == m.s16(n + 4) and m.u32(ent) & 2):
        eng.remove(e, "alarm over")
        return
    f = m.u8(o + 0x28)
    if not f & 0x80:
        v = m.s8(o + 0x26)
        if v == -1 or m.u8(0x800910D0 + v) != 1:
            m.w32(o + 8, vb)
            m.w8(o + 0x28, f | 0x80)
            if v != -1:
                m.w32(o + 8, vb + m.s32(o + 0xC))
                if m.s32(o + 0x10) > 0:
                    m.w32(o + 8, m.s32(o + 8) + eng.rand.draw(ALARM_THINK) % m.s32(o + 0x10))
    if m.u8(o + 0x28) & 0x80 and vb >= m.s32(o + 8):
        v = eng.audio.play(m.u8(o + 0x27), m.u16(o + 0x24), e, m.s32(o + 0x14), m.s32(o + 0x18),
                           m.s32(o + 0x1C), m.s32(o + 0x20), 0, 0, 0, 0)
        m.w8(o + 0x26, v & 0xFF)
        m.w8(o + 0x28, m.u8(o + 0x28) ^ 0x80)


def alarm_destroy(eng, e):
    """0x8005F008: its voice stopped, no alarm up."""
    m = eng.m
    v = m.s8(m.u32(e + 0x388) + 0x26)
    if v > 0:
        eng.audio.stop(v)
    m.w32(G_ALARM, -1)
    m.w32(G_ALARM_ENT, 0)
