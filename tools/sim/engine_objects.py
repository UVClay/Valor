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
            eng.note("scenery shape (0x8005D238) not built")
        else:
            m.w32(e + 0x10, 0x44011)
            m.w32(e + 0xC, 0x13028400)
            m.w32(e + 0x14, 0x88214051)
            m.w32(e + 0x37C, 0x8005D32C)
            eng.note("destructible shape (0x8004A8F8) not built")
        return e
    eng.note(f"spawn of type {t:X} not implemented", f"r{rec.idx}")
    e = eng.entity_create(t, model=model)
    place(eng, e, rec)
    return e


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
    eng.note("pickup spawn not implemented", f"r{rec.idx}")
    return None


def level_explosion(eng, sub, sub_obj, kind):
    eng.note(f"level explosion({kind}) not implemented")


def set_home(eng, tgt, src):
    eng.note("set_home not implemented")
