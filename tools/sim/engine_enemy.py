"""Soldiers and dogs (docs/re/enemies.md, soldier.md, enemy_types.md): spawning into the character
pool, the think around the scripts, the brain."""
import brain_sim as bs
from engine import s16, s32, CHAR_SLOTS, G_SOLDIER, G_SOLDIER_COUNT, G_SOLDIER_CAP

SOLDIER, DOG = 0x1402, 0x1405
ENEMY_THINK, DOG_THINK = 0x80055628, 0x80054AB8
POSE_POOLS = [0x800C4378 + i * 0x1308 for i in range(5)]
G_GROUP = 0x80099B30           # the next soldier's +0x35 (a running count)
G_DOG_GROUP = 0x80099B24
G_DOG_COUNT = 0x80099B20
G_LAST_DOG = 0x8009D644
G_WEAPONS = 0x80097FF0         # g_enemy_weapons, 4 x 48 bytes
G_HELMET_SAVE = 0x8009810C


def register(eng):
    eng.think[ENEMY_THINK] = lambda e: enemy_think(eng, e)
    eng.think[DOG_THINK] = lambda e: dog_think(eng, e)
    eng.handlers[("destroy", SOLDIER)] = lambda e: enemy_destroy(eng, e)
    eng.handlers[("destroy", DOG)] = lambda e: dog_destroy(eng, e)


# ---------------------------------------------------------------- the character pool

def char_alloc(eng):
    """char_alloc (0x8008EEF0): the first free slot of five (entities.md, Memory)."""
    m = eng.m
    for i, a in enumerate(CHAR_SLOTS):
        if m.u32(a + 0x2810):
            m.zero(a, 0x280C)
            m.w32(a + 0x280C, -0x43)
            m.w32(a + 0x2810, 0)
            return i, a
    return None, None


def char_release(eng, e):
    """char_release (0x8008EE60): countdown 2; char_pool_update frees the slot at 0."""
    eng.m.w32(e + 0x280C, 2)


def char_pool_update(eng):
    """char_pool_update (0x8008EAC8), at the start of every frame."""
    m = eng.m
    for a in CHAR_SLOTS:
        c = m.s32(a + 0x280C)
        if c > 0:
            c -= 1
            m.w32(a + 0x280C, c)
            if c == 0:
                m.w32(a + 0x2810, 1)


def character_create(eng, model, channels):
    """entity_create(3, 0xBF0, model, ..., channels) in a character slot: the entity, its object
    (+0x3CC), the instance (+0xFBC), the hit parts, the channels' event tables."""
    i, a = char_alloc(eng)
    if a is None:
        return None, None
    m = eng.m
    nparts = m.u16(model + 0xBE)
    e = eng.entity_create(m.s32(model + 0xFC), obj_size=0xBF0, model=0, addr=a, parts=0, channels=0)
    # the layout entity_create gives (0x8006B5B8): object, instance, parts, channel tables
    p = a + 0x3CC
    m.w32(a + 0x388, p)
    p += 0xBF0
    m.w32(a + 0x100, p)
    inst = p
    p += 0xB0
    m.w16(a + 0x36, nparts)
    m.w32(a + 0x304, p if nparts else 0)
    m.copy(p, m.u32(model + 0xC0), 0x48 * nparts)
    p += 0x48 * nparts
    for ch in (0, 1):
        m.w32(a + 0x124 + 0x74 * ch, p)
        p += 0x3C * channels
    m.w32(a + 0x1EC, channels)
    instance_init(eng, a, inst, model, POSE_POOLS[i])
    return i, a


def instance_init(eng, e, inst, model, pool):
    """model_instance_init (0x8006A96C) for a character: the fields the logic reads, and the pose
    buffers in the slot's pose pool (animation.md, The instance)."""
    m = eng.m
    nb = 0
    tbl = m.u32(model + 8)
    if tbl:
        first = m.u32(tbl + 4)
        nb = m.u8(first + 1) if first else 0
    m.w32(inst + 0x0, pool)
    m.w32(inst + 0x8, model)
    m.w16(inst + 0x3E, 0)
    m.w16(inst + 0x40, -1)
    m.w16(inst + 0x5C, -1)
    m.w16(inst + 0x5E, 0)
    m.w16(inst + 0x60, 3)
    m.w16(inst + 0x62, -1)
    m.w16(inst + 0x64, 0)
    m.w32(inst + 0x6C, 0x80)
    m.w32(inst + 0x74, tbl)
    if tbl:
        m.w16(inst + 0x70, m.u16(tbl))
        m.w32(inst + 0x78, tbl + 4)
    m.w16(inst + 0x72, nb)
    m.w32(inst + 0x7C, m.u32(model + 0x38))
    m.w32(inst + 0x80, m.u32(model + 0x84))
    m.w32(inst + 0x84, m.u32(model + 0x9C))
    m.w32(inst + 0x90, pool)
    m.w32(inst + 0x88, pool)
    m.w32(inst + 0x8C, pool + 8 * nb)
    m.w32(inst + 0x94, pool + 16 * nb)
    for k in range(3):
        m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k))
    m.w32(e + 0x2D4, m.u16(model + 0xA0) << 16)


def brain_alloc(eng):
    return eng.alloc(0x124)


def set_var(eng, e, v, value):
    m = eng.m
    if v & 0x100:
        n = v & 0xFF
        a, bit = e + 0xEC + 4 * (n >> 5), 1 << (n & 31)
        m.w32(a, (m.u32(a) | bit) if value else (m.u32(a) & ~bit))
    else:
        m.w32(e + 0x4C + 4 * v, value)


# ---------------------------------------------------------------- spawning

def enemy_spawn(eng, rec):
    """enemy_spawn (0x800563F0) or dog_spawn (0x80054E4C) by the body model's type."""
    m = eng.m
    from engine_objects import model_by_hash
    tmpl = m.u32(rec.addr + 0x74)
    model = model_by_hash(eng, m.u32(tmpl + 4))
    if not model:
        eng.note("soldier model missing", f"r{rec.idx}")
        return None
    if m.s32(G_SOLDIER_CAP) <= 0 or m.s32(G_SOLDIER_COUNT) >= m.s32(G_SOLDIER_CAP):
        return None
    t = m.s32(model + 0xFC)
    if t == DOG:
        return dog_spawn(eng, rec, model)
    return soldier_spawn(eng, rec, model, tmpl)


def soldier_spawn(eng, rec, model, tmpl):
    m = eng.m
    r = rec.addr
    i, e = character_create(eng, model, 0x28)
    if e is None:
        return None
    set_var(eng, e, 0x101, 0)
    weapon, head, grenade = m.u32(0x8003C19C + 0), m.u32(0x8003C19C + 4), m.u32(0x8003C19C + 8)
    n = m.u32(tmpl)
    if n >= 2:
        weapon = m.u32(tmpl + 8)
        if n >= 3 and m.u32(tmpl + 0xC) != 0x4CBF0BA5:
            head = m.u32(tmpl + 0xC)
        if n >= 4:
            grenade = 0x61282777
            set_var(eng, e, 0x101, 1)
    kind = next((k for k in range(4) if m.u32(G_WEAPONS + 0x30 * k) == weapon), 4) & 0xFF
    obj = m.u32(e + 0x388)
    b = brain_alloc(eng)
    m.w32(e + 0x38C, b)
    m.w32(e + 0xC, 0x1300903C)
    m.w32(e + 0x14, 0x88204401)
    m.w32(e + 8, SOLDIER)
    m.w32(e + 0x10, 0x68000)
    cull = m.s16(r + 0x68)
    m.w32(e + 0x294, cull)
    m.w32(e + 0x290, cull)
    m.w16(e + 0x2DA, -0x78)
    m.w16(e + 0x2E8, 0x96)
    m.w16(e + 0x2EC, 0x3C)
    m.w16(e + 0x3C8, 0x3C)
    g = m.u8(G_GROUP)
    m.w8(G_GROUP, g + 1)
    m.w8(e + 0x35, g)
    hp = m.u8(r + 0x61) << 16
    m.w32(e + 0xF4, hp)
    m.w32(e + 0xF0, hp)
    m.w32(obj + 0x18, 0)
    m.w16(e + 0x2EA, 0)
    set_var(eng, e, 0xF, kind)
    set_var(eng, e, 0xD, cdiv(m.s32(e + 0xF4) * 100, m.s32(e + 0xF0)))
    set_var(eng, e, 0x22, m.s16(r + 0x6E))
    set_var(eng, e, 0x23, 0)
    set_var(eng, e, 0x10, m.u8(r + 0x5E))
    set_var(eng, e, 0x24, {0: 10, 1: 20, 2: 1000}.get(m.u8(r + 0x5C), 100))
    set_var(eng, e, 8, {0: 33, 2: 100}.get(m.u8(r + 0x5B), 67))
    for off, fn in ((0x36C, ENEMY_THINK), (0x370, 0x8005608C), (0x374, 0x80055864), (0x378, 0x80056250),
                    (0x384, 0x80056288), (0x37C, 0x8005291C)):
        m.w32(e + off, fn)
    m.w16(b + 0xAA, 0xD)
    m.w16(b + 0xB0, m.u16(r + 0x6C))
    hear = m.s16(r + 0x70)
    m.w16(b + 0xB2, hear if hear >= 10 else 0xAF0)
    hear = m.s16(b + 0xB2)
    set_var(eng, e, 5, hear)
    set_var(eng, e, 6, (hear & 0xFFFF) >> 1)
    set_var(eng, e, 7, s16(cdiv(hear, 3)))
    set_var(eng, e, 0x26, m.u8(r + 0x65))
    v = m.u8(r + 0x66)
    set_var(eng, e, 0xB, 1 if v == 0x1E else v)
    m.w16(b + 0xC8, m.u8(r + 0x63))
    m.w16(b + 0xB6, -1)
    m.w16(b + 0xBE, 1)
    m.w16(b + 0xBA, 2)
    m.w16(b + 0xC0, 0)
    m.w16(b + 0xC2, 0)
    m.w32(b + 0xD8, 0)
    m.w16(b + 0xB4, m.s16(r + 0x6A) * 11)
    m.w16(b + 0xC4, m.u16(e + 0x2EC))
    m.w16(b + 0xC6, m.u16(e + 0x2E8))
    m.w32(b + 0xDC, 0x10000)
    for o in (0xCC, 0xCE, 0xD0, 0xD2):
        m.w16(b + o, 0x64)
    m.w16(b + 0xCA, m.u16(e + 0x3C8))
    attachments_init(eng, e, (weapon, head, grenade, 0))
    # the weapon block (enemies.md, Firing) and the attachment words enemy_spawn sets
    for o in (0x10, 0x11, 0x12, 0x14):
        m.w8(obj + o, 0)
    s0 = obj + 0x18
    m.w32(s0 + 4, 0x41)
    m.w32(s0 + 0xC, 0x15)
    if kind == 0:
        m.w16(s0 + 0x18, 0xF), m.w16(s0 + 0x1A, 0), m.w16(s0 + 0x1C, 0xF)
    elif kind == 2:
        m.w16(s0 + 0x18, 0), m.w16(s0 + 0x1A, 0), m.w16(s0 + 0x1C, 0x32)
    elif kind in (1, 3):
        m.w16(s0 + 0x18, 5), m.w16(s0 + 0x1A, 0), m.w16(s0 + 0x1C, 0x19)
    m.w32(s0 + 0x150, 0xD)
    m.w16(s0 + 0x15C, -7)
    m.w16(s0 + 0x15E, 1)
    m.w32(s0 + 0x148, 1 if head != 0xFFFFFFFF else 0x20)
    m.w16(s0 + 0x160, 0x24)
    m.w32(G_HELMET_SAVE, 0xAA1714FE if head == 0x8A522920 else 0)
    m.w32(s0 + 0x294, 0x11)
    m.w32(G_SOLDIER_COUNT, m.s32(G_SOLDIER_COUNT) + 1)
    return start_scripts(eng, e)


def dog_spawn(eng, rec, model):
    m = eng.m
    r = rec.addr
    i, e = character_create(eng, model, 0x14)
    if e is None:
        return None
    m.w32(G_LAST_DOG, e)
    b = brain_alloc(eng)
    m.w32(e + 0x38C, b)
    # dog_spawn places it (0x80074AD8) and builds the matrix before the rest
    from engine_objects import place
    place(eng, e, rec)
    m.w32(e + 0x1C, 0)
    eng.matrix(e)
    m.w32(e + 8, DOG)
    m.w32(e + 0x14, 0x88204401)
    m.w32(e + 0xC, 0x1300906C)
    m.w32(e + 0x10, 0x68000)
    cull = m.s16(r + 0x68)
    m.w32(e + 0x294, cull)
    m.w32(e + 0x290, cull)
    hp = m.u8(r + 0x61)
    m.w16(e + 0x2EC, 0x50)
    m.w16(e + 0x2DA, -0x32)
    m.w16(e + 0x3C8, 0x50)
    m.w32(e + 0xF4, hp << 16)
    m.w32(e + 0xF0, hp << 16)
    m.w16(e + 0x2E8, 0x7D)
    for off, fn in ((0x36C, DOG_THINK), (0x370, 0x80054B80), (0x374, 0x80054C24), (0x378, 0x80054E14),
                    (0x384, 0x80054D70), (0x37C, 0x8005291C)):
        m.w32(e + off, fn)
    m.w8(e + 0x35, m.u8(G_DOG_GROUP))
    for k in range(3):
        m.w16(e + 0x3A4 + 2 * k, m.u16(r + 0x10 + 2 * k))
    m.w32(e + 0x4C + 4 * 0x1F, m.s16(r + 0x34))
    set_var(eng, e, 0xD, cdiv(hp * 0x190000 * 4, hp << 16) if hp else 0)
    set_var(eng, e, 0x1C, m.s16(r + 0x6E))
    set_var(eng, e, 0x1D, m.u8(r + 0x5E))
    m.w16(b + 0xAA, 0xD)
    m.w16(b + 0xB0, m.u16(r + 0x6C))
    hear = m.s16(r + 0x70)
    m.w16(b + 0xB2, hear if hear >= 10 else 0xAF0)
    hear = m.s16(b + 0xB2)
    set_var(eng, e, 5, hear)
    set_var(eng, e, 6, (hear & 0xFFFF) >> 1)
    set_var(eng, e, 7, s16(cdiv(hear, 3)))
    set_var(eng, e, 0x1F, m.s16(r + 0x34))
    m.w16(b + 0xC0, 0)
    m.w16(b + 0xC2, 0)
    m.w32(b + 0xD8, 0)
    m.w16(b + 0xB4, m.s16(r + 0x6A) * 11)
    m.w16(b + 0xC4, m.u16(e + 0x2EC))
    m.w32(G_SOLDIER_COUNT, m.s32(G_SOLDIER_COUNT) + 1)
    m.w32(G_DOG_COUNT, m.s32(G_DOG_COUNT) + 1)
    m.w16(b + 0xC6, m.u16(e + 0x2E8))
    m.w32(b + 0xDC, 0x10000)
    for o in (0xCC, 0xCE, 0xD0, 0xD2):
        m.w16(b + o, 0x64)
    m.w16(b + 0xCA, m.u16(e + 0x3C8))
    return start_scripts(eng, e)


def start_scripts(eng, e):
    """spawn_from_model (0x8006D6F0): the matrix, the model's start script forced, the channels
    run once, all before record_spawn_model places the entity (still at the origin)."""
    m = eng.m
    inst = m.u32(e + 0x100)
    if inst and m.u32(inst + 8):
        eng.matrix(e)
        s = m.s16(m.u32(inst + 8) + 0x94)
        if s >= 0:
            from engine_scripts import EVM
            vm = EVM(eng, e)
            vm.script_start(s, 1, 0)
            vm.run_a()
    return e


def attachments_init(eng, e, hashes):
    """0x80050720: the attachment list at weapon block +0x18 (count, entries of 0x144)."""
    m = eng.m
    obj = m.u32(e + 0x388)
    m.w32(obj + 0x18, 0)
    for k, h in enumerate(hashes):
        if not h or k == 3:
            break
        m.w32(obj + 0x18, m.u32(obj + 0x18) + 1)
        base = obj + 0x1C + 0x144 * k
        m.w32(base, 0)
        m.w32(base + 0x8, 0)
        m.w32(base + 0x8 + 0xE4, m.u32(e + 0x100))
        m.w32(base + 0x8 + 0x1C, m.u32(e + 0x270))
        m.w32(base + 0x8 + 0x20, m.u32(e + 0x274))
        m.w32(base + 0x8 + 0x24, m.u32(e + 0x278))
        if h != 0xFFFFFFFF:
            m.w32(base + 0x94, h)


def cdiv(a, b):
    if b == 0:
        return 0
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ---------------------------------------------------------------- the think

def enemy_think(eng, e):
    """enemy_think (0x80055628; enemies.md, The think)."""
    m = eng.m
    obj = m.u32(e + 0x388)
    # the fire state goes to the weapon block, enemy_weapon, and back
    m.w8(obj + 0x11, m.s32(e + 0x4C + 4 * 0x16))
    from engine_combat import enemy_weapon
    enemy_weapon(eng, e)
    m.w32(e + 0x4C + 4 * 0x16, m.u8(obj + 0x11))
    from engine_combat import enemy_aim_hook, attachments_hook
    enemy_aim_hook(eng, e)
    attachments_hook(eng, e)
    witness_report(eng, e)
    scripts(eng, e, 0x13)


def dog_think(eng, e):
    """dog_think (0x80054AB8): the witness report, the scripts around the heading (v0E)."""
    witness_report(eng, e)
    scripts(eng, e, 0x0E)


def scripts(eng, e, hv):
    m = eng.m
    from engine_scripts import EVM
    vm = EVM(eng, e)
    vm.run_b()
    m.w32(e + 0x4C + 4 * hv, m.s32(e + 0x22C))
    vm.run_a()
    m.w32(e + 0x22C, m.s32(e + 0x4C + 4 * hv))


WITNESS_VICTIM, WITNESS_SPOT, WITNESSES, WITNESS_COUNT = 0x80098178, 0x8009817C, 0x80098164, 0x80098184


def witness_report(eng, e):
    """0x80052AF4: hit this tick (+0x38 0x20/0x40): the victim, its position and the first five
    other living soldiers and dogs in the list as witnesses (enemy_types.md, Witnesses)."""
    m = eng.m
    if not m.u32(e + 0x38) & 0x60:
        return
    m.w32(WITNESS_VICTIM, e)
    for k in range(3):
        m.w16(WITNESS_SPOT + 2 * k, s16(m.s32(e + 0x20C + 4 * k) >> 16))
    n = 0
    for o in eng.entities():
        if n >= 5:
            break
        if o != e and m.s32(o + 8) in (SOLDIER, DOG) and not m.u32(o + 0xC) & 0x20080000:
            m.w32(WITNESSES + 4 * n, o)
            n += 1
    m.w32(WITNESS_COUNT, n)


def witness_check(eng):
    """0x80052C38, once per entities_update: the last witness tests whether it sees the victim
    with its long range; if so +0x3C 0x10000."""
    m = eng.m
    n = m.s32(WITNESS_COUNT)
    if n <= 0:
        return
    n -= 1
    m.w32(WITNESS_COUNT, n)
    w, v = m.u32(WITNESSES + 4 * n), m.u32(WITNESS_VICTIM)
    if not w or not v:
        return
    from engine_sense import sees
    if sees(eng, w, v, long=True):
        m.w32(w + 0x3C, m.u32(w + 0x3C) | 0x10000)


def enemy_destroy(eng, e):
    """The soldier's destructor (0x80056288 -> 0x80056290): the count, its brain, the slot."""
    m = eng.m
    m.w32(G_SOLDIER_COUNT, m.s32(G_SOLDIER_COUNT) - 1)
    b = m.u32(e + 0x38C)
    if b:
        eng.free(b)
    char_release(eng, e)


def dog_destroy(eng, e):
    m = eng.m
    m.w32(G_SOLDIER_COUNT, m.s32(G_SOLDIER_COUNT) - 1)
    m.w32(G_DOG_COUNT, m.s32(G_DOG_COUNT) - 1)
    b = m.u32(e + 0x38C)
    if b:
        eng.free(b)
    char_release(eng, e)


# ---------------------------------------------------------------- the brain

def brain_update(eng, e):
    """brain_update (0x80040DF4), brain_sim.py's rules on the engine's state."""
    from engine_move import read_st, write_st
    m = eng.m
    st = read_st(eng, e)
    targets = BrainTargets(eng)
    w = bs.World2(m, targets, RATAN)
    log = {}
    bs.brain_update(st, w, log)
    write_st(eng, e, st)
    if log.get("unsupported"):
        eng.note("brain: " + log["unsupported"])


class BrainTargets:
    """The brain's view of a target entity: (flags A, integer x, integer z), as it is now."""

    def __init__(self, eng):
        self.eng = eng

    def get(self, ptr):
        m = self.eng.m
        if not ptr:
            return None
        return (m.u32(ptr + 0xC), s16(m.s32(ptr + 0x20C) >> 16), s16(m.s32(ptr + 0x214) >> 16))


class _Ratan:
    def __call__(self, y, x):
        from combat_sim import ratan2
        return ratan2(y, x)


RATAN = _Ratan()
