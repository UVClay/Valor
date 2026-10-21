"""Spawning from records and the world objects (docs/re/levels.md Spawning, objects.md)."""
import move_sim as ms
from engine import s16, s32, CHAR_SLOTS, G_SOLDIER_COUNT, G_SOLDIER_CAP

NONE = -0x7FFF


def register(eng):
    pass


def place(eng, e, rec):
    """0x80074AD8: the record's position and angles."""
    m = eng.m
    for k in range(3):
        m.w32(e + 0x20C + 4 * k, rec.pos[k] << 16)
        m.w32(e + 0x3BC + 4 * k, rec.pos[k] << 16)
        m.w32(e + 0x258 + 4 * k, rec.pos[k] << 16)
        m.w32(e + 0x228 + 4 * k, rec.ang[k] << 16)


def model_by_hash(eng, h):
    for i in range(eng.m.u32(0x8009EC54)):
        a = eng.m.u32(0x8009EC58) + 0x100 * i
        if eng.m.u32(a) == h:
            return a
    return 0


def record_spawn(eng, rec):
    """record_spawn (0x8007505C) by kind; returns the entity, or None when refused (queued)."""
    m = eng.m
    if rec.kind in (2, 6):
        model = model_by_hash(eng, rec.model)
        if not model:
            eng.note("record model missing", f"r{rec.idx}")
            return None
        t = m.s32(model + 0xFC)
        e = spawn_by_type(eng, t, model, rec)
        if e is not None:
            from engine_enemy import start_scripts
            start_scripts(eng, e)
    elif rec.kind == 3:
        from engine_enemy import enemy_spawn
        e = enemy_spawn(eng, rec)       # starts its scripts itself (spawn_from_model)
    elif rec.kind == 4:
        e = pickup_spawn(eng, rec)
    else:
        return None
    return e


def spawn_by_type(eng, t, model, rec):
    m = eng.m
    if t in (0x1770, 0x1778):
        # destructible_spawn (0x8005D678); the record's own y is snapped to the floor when within
        # 55 units (the record is changed: the placement after it uses it)
        e = eng.entity_create(t, model=model, list_n=6)
        r = rec.addr
        cull_v = m.s16(r + 0x5E)
        m.w32(e + 0x294, cull_v)
        m.w32(e + 0x290, cull_v)
        g = m.u8(0x80099EC8)
        m.w8(0x80099EC8, g + 1)
        m.w8(e + 0x35, g)
        hp = m.u8(r + 0x5C) << 16
        m.w32(e + 0xF0, hp)
        m.w32(e + 0xF4, hp)
        for k in range(3):
            m.w32(e + 0x20C + 4 * k, m.s16(r + 0x10 + 2 * k) << 16)
        f, zone = eng.world.floor(tuple(m.s32(e + 0x20C + 4 * k) for k in range(3)))
        y = m.s16(r + 0x12)
        if (0 <= y - f < 0x37) or (y - f < 0 and f - y < 0x37):
            m.w16(r + 0x12, f)
            rec.pos = (rec.pos[0], f, rec.pos[2])
        for k in range(3):
            m.w32(e + 0x264 + 4 * k, m.s16(r + 0x18 + 2 * k) << 16)
            m.w32(e + 0x258 + 4 * k, m.s16(r + 0x10 + 2 * k) << 16)
        m.w32(e + 0x370, 0x8006A5B4)
        m.w32(e + 0x384, 0x8006A37C)
        m.w16(e + 0x2EC, 0)
        m.w16(e + 0x36, 0)
        if t == 0x1778:
            m.w32(e + 0x14, 0)
            m.w32(e + 0xC, 0x13028000)
            m.w32(e + 0x10, 0x40213)
            m.w16(e + 0x2EE, 0)
            from engine_planes import box_sphere
            box_sphere(m, e, [m.s16(model + 0x44 + 2 * k) for k in range(3)],
                       [m.s16(model + 0x7C + 2 * k) for k in range(3)])
        else:
            m.w32(e + 0x10, 0x44011)
            m.w32(e + 0xC, 0x13028400)
            m.w32(e + 0x14, 0x88214051)
            m.w32(e + 0x37C, 0x8005D32C)
            from engine_planes import object_box
            object_box(eng, e, [m.s16(model + 0x44 + 2 * k) for k in range(3)],
                       [m.s16(model + 0x7C + 2 * k) for k in range(3)])
        return e
    if t in (0x1777, 0x1779, 0x177A) or t not in KNOWN_SPAWNERS:
        from engine_planes import object_spawn
        return object_spawn(eng, rec, model)
    eng.note(f"spawn of type {t:X} not implemented", f"r{rec.idx}")
    e = eng.entity_create(t, model=model)
    place(eng, e, rec)
    return e


KNOWN_SPAWNERS = (0x1402, 0x1405, 0x1770, 0x1778, 0x1771, 0x1772, 0x1774, 0x1775, 0x1776)


def cull(eng, e, rec):
    """The out-of-sight counter (entities.md, Despawning): records whose +0x58 has bit 0 (property
    19) get flags A 0x8000 and +0x290/+0x294 = the record's +0x5E (property 20)."""
    m = eng.m
    if m.u32(rec.addr + 0x58) & 1:
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x8000)
        v = m.s16(rec.addr + 0x5E)
        m.w32(e + 0x290, v)
        m.w32(e + 0x294, v)


def pickup_spawn(eng, rec):
    """record_spawn_pickup (0x80074CE8): pickup_spawn (0x800833A0) of the kind hash at record
    +0x60 with its default model; then placed (0x80074AD8: the record's angles and position,
    the y as pickup_spawn may have snapped it in the record), the matrix and the model's
    behaviour script (+0x94) started and run once. The caller links it and sends message 0."""
    m = eng.m
    import engine_attach as at
    r = rec.addr
    rd = dict(pos=[m.s16(r + 0x10 + 2 * k) for k in range(3)], ang=[m.s16(r + 0x18 + 2 * k) for k in range(3)],
              flags58=m.u32(r + 0x58), cull=m.s16(r + 0x5E), count=m.u8(r + 0x5C), flags30=m.u32(r + 0x30),
              w=m.s16(r + 0x36), addr=r)
    e = at.pickup_spawn(eng, rd, m.u32(r + 0x60), 0xFFFFFFFF, 0)
    if e is None:
        return None
    for k in range(3):
        m.w32(e + 0x20C + 4 * k, m.s16(r + 0x10 + 2 * k) << 16)
        m.w32(e + 0x3BC + 4 * k, m.s16(r + 0x10 + 2 * k) << 16)
        m.w32(e + 0x258 + 4 * k, m.s16(r + 0x10 + 2 * k) << 16)
        m.w32(e + 0x228 + 4 * k, m.s16(r + 0x18 + 2 * k) << 16)
    from engine_enemy import start_scripts
    start_scripts(eng, e)
    return e


def level_explosion(eng, sub, sub_obj, kind):
    """Built-in 6, lvl_explosion (0x80052F14), at the entity or its record's position (kept in
    0x800AB920): 1 and 6 sparks with smoke (1: and half the time a sound 0..3), 3 a burst of
    sparks; the explosions proper (0, 2, 4, 5, 7, 8) not yet."""
    m = eng.m
    from engine_projectile import particles_spawn
    e = sub.addr if sub is not None and getattr(sub, "addr", 0) else 0
    if e:
        pos = [m.s32(e + 0x20C + 4 * k) for k in range(3)]
    else:
        r = m.u32(sub_obj + 4)
        pos = [s32(m.s16(r + 0x10 + 2 * k) << 16) for k in range(3)]
        for k in range(3):
            m.w32(0x800AB920 + 4 * k, pos[k])
    rd = lambda: eng.rand.draw(0x80052F14)
    if kind == 3:
        particles_spawn(eng, pos, 5, ((rd() * 5) >> 15) + 0xF, 0x28, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF,
                        0x17FFF, 0, 0)
    elif kind in (1, 6):
        n = ((rd() * 3) >> 15) + 2
        smoke = ((rd() * 4) >> 15) + 6
        particles_spawn(eng, pos, 1, n, 0x1E, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke)
        if kind == 1 and ((rd() * 100) >> 15) < 0x32:
            eng.audio.play(0, rd() % 4, e, 0xCCCC, 0, 0x8000, 0x2000, 0, 0, 0, 0)
    else:
        eng.note(f"level explosion({kind}) not implemented")


def set_home(eng, tgt, src):
    eng.note("set_home not implemented")
