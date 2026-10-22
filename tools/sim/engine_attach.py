"""A soldier's attachments and the rest of its think (docs/re/enemy_types.md, Attachments;
enemies.md, The think): the weapon, head gear and grenade carried on bones, knocked off, falling,
landing, dropping the weapon as a pickup; the ranges; the hit reaction's puffs and drops.

The attachment list is the weapon block's +0x18 (count) and entries of 0x144 from +0x1C:
  +0 flags (1 attached, 2 falling, 4 landed, 8 detached, 0x10 turning to rest, 0x20 hidden or
     gone, 0x40 drop as a pickup), +4 fall ticks left, +8 the bone (attached) / a grenade's
     timer, +0xC bounces allowed, +0x10 bounces made, +0x14 offset from the bone (3 x s16),
     +0x1C drag, +0x20 gravity, +0x24.. the scale, +0x30 velocity, +0x3C position, +0x48 the
     previous position, +0x54 spin, +0x60 angles, +0x74 the drawn matrix (rotation and integer
     translation, 0x20 bytes), +0x94 a model instance (+4 flags, +8 the model), +0xE8 the
     holder's handle, +0xEC the holder's instance.
Per attachment index a template at 0x800980B0 (0x3C bytes): +0 fall ticks, +4 bounces, +8 drag,
+0xC gravity, +0x10 knock-off speed (-1: keep the drawn motion), +0x14 its vertical speed,
+0x1C spin, +0x20 the model shown once off, +0x24 the bounce sound (-1 none), +0x28 its random
range, +0x2C its bank (-1: the body model's), +0x30/+0x38 the resting x/z angles."""
import combat_sim as cs
import engine_gte as gt
from engine import s16, s32, G_SOLDIER
from engine_feed import FN

TEMPLATES = 0x800980B0
G_ENEMY_WEAPONS = 0x80097FF0
G_RANGE_THIRD_SET = 0x80099B2C
G_RANGE_THIRD = 0x800AB930
G_BLOOD = 0x80093A05
PICKUPS = 0x8009E8E8
PICKUP_THINK = 0x80082E88
NO_MODEL_SPECIAL = (0x37C82DE4, 0x08E34FAC, 0x25C50BD4, 0x6C8F8AA8, 0x933987E0, 0x96593A51)


def entry(m, e, k):
    return m.u32(e + 0x388) + 0x1C + 0x144 * k


def template(k):
    return TEMPLATES + 0x3C * k


# ---------------------------------------------------------------- models on attachments

def model_find(eng, h):
    from engine_objects import model_by_hash
    return model_by_hash(eng, h)


def instance_set(eng, inst, h, flags):
    """func_8006B1C4 -> model_instance_init (engine_heap.py): a model on an attachment, its block
    from the heap as the flags ask."""
    m = eng.m
    model = model_find(eng, h)
    if not model:
        m.w32(inst, 0)
        return 0
    from engine_heap import instance_init
    return instance_init(eng, inst, model, flags, 0)


def instance_release(eng, inst, delay=0):
    """func_8006B234 (no entity): its +4 bit 8 given back, the block freed now or later."""
    from engine_heap import instance_release as release
    release(eng, inst, 0, delay)


def swap_model(eng, B, h):
    if h and m_hash(eng, B) != h:
        instance_release(eng, B + 0x94)
        instance_set(eng, B + 0x94, h, 0x156)


def m_hash(eng, B):
    model = eng.m.u32(B + 0x94 + 8)
    return eng.m.u32(model) if model else 0


def attachments_init(eng, e, hashes):
    """0x80050720: one entry per hash up to the first 0 (at most 3): the model (-1: none), the
    holder, its instance and scale."""
    m = eng.m
    obj = m.u32(e + 0x388)
    m.w32(obj + 0x18, 0)
    for k, h in enumerate(hashes):
        if not h or k == 3:
            break
        m.w32(obj + 0x18, m.u32(obj + 0x18) + 1)
        B = obj + 0x1C + 0x144 * k
        if h != 0xFFFFFFFF:
            instance_set(eng, B + 0x94, h, 0x157)
        m.w32(B + 0xE8, m.u32(e))
        m.w32(B + 0xEC, m.u32(e + 0x100))
        for i in range(3):
            m.w32(B + 0x24 + 4 * i, m.u32(e + 0x270 + 4 * i))
        m.w32(B + 8, 0)
        m.w32(B, 0)


# ---------------------------------------------------------------- drawn: the matrix on the bone

def attachment_draw(eng, e, B, bone):
    """func_8005081C (from enemy_draw for attachments with flags & 0x27 == 1): the previous
    position kept, then from the posed joints: the bone's quaternion matrix R and joint j,
    t = j + R . offset (+0x14), into the world (ApplyMatrixLV of the entity's matrix, + the
    integer translation +0x354), rotation = entity matrix x R; the position (<< 16) and the
    matrix (with t) into +0x3C and +0x74; the angles = the entity's."""
    m = eng.m
    inst = m.u32(e + 0x100)
    for k in range(3):
        m.w32(B + 0x48 + 4 * k, m.u32(B + 0x3C + 4 * k))
    if not m.u32(inst + 0x84):
        eng.note("attachment on a model without joints not implemented")
        return
    for k in range(3):
        m.w32(B + 0x60 + 4 * k, m.u32(e + 0x228 + 4 * k))
    qp = m.u32(inst + 0x88)
    if not qp:
        return
    q = tuple(m.s16(qp + 8 * bone + 2 * k) for k in range(4))
    R = cs.quat_matrix(q)
    jp = m.u32(inst + 0x94)
    j = [m.s16(jp + 8 * bone + 2 * k) for k in range(3)]
    off = [m.s16(B + 0x14 + 2 * k) for k in range(3)]
    mac, _ = gt.mul(R, off, j, 12)
    E = [[m.s16(e + 0x340 + 6 * i + 2 * k) for k in range(3)] for i in range(3)]
    w = gt.apply_matrix_lv(E, mac)
    t = [s32(w[k] + m.s32(e + 0x354 + 4 * k)) for k in range(3)]
    RR = [[s16(sum(E[i][x] * R[x][k] for x in range(3)) >> 12) for k in range(3)] for i in range(3)]
    for k in range(3):
        m.w32(B + 0x3C + 4 * k, s32(t[k] << 16))
    for i in range(3):
        for k in range(3):
            m.w16(B + 0x74 + 6 * i + 2 * k, RR[i][k])
    m.w32(B + 0x84, s32(RR[2][2]))           # MulMatrix2 stores the last element as a word
    for k in range(3):
        m.w32(B + 0x88 + 4 * k, t[k])


def angles_from_matrix(m, B):
    """func_80051408: the angles of the drawn matrix (TransposeMatrix, SquareRoot12, ratan2)."""
    M = [[m.s16(B + 0x74 + 6 * i + 2 * k) for k in range(3)] for i in range(3)]
    T = gt.transpose(M)
    v = cs.sqrt12(((T[1][1] * T[1][1]) >> 12) + ((T[0][1] * T[0][1]) >> 12))
    if v:
        a = (cs.ratan2(-T[2][1], v), cs.ratan2(T[2][0], T[2][2]), cs.ratan2(T[0][1], T[1][1]))
    else:
        a = (cs.ratan2(-T[2][1], 0), cs.ratan2(-T[0][2], T[0][0]), 0)
    for k in range(3):
        m.w32(B + 0x60 + 4 * k, s32(a[k] << 16))


# ---------------------------------------------------------------- knocked off and falling

def knock_off(eng, B, player, speed, vy, ticks, drag, gravity, bounces, spin, action):
    """attachment_knock_off (0x80051200)."""
    m = eng.m
    rd = lambda: eng.rand.draw(0x80051200)
    act = action & 0xE0000000
    if act & 0x20000000:
        v = (0, 0, 0)
        m.w32(B + 0xC, m.s32(B + 0xC) - 1)
    elif speed == -1:
        v = tuple(s32(m.s32(B + 0x3C + 4 * k) - m.s32(B + 0x48 + 4 * k)) for k in range(3))
        vy = v[1]
    else:
        sp = speed >> 16
        if act & 0x80000000:
            vx = ((s32(rd() * 2 * sp) >> 15) - sp) if sp * 2 else -sp
            vz = ((s32(rd() * 2 * sp) >> 15) - sp) if sp * 2 else -sp
            v = (s32(vx << 16), vy, s32(vz << 16))
        else:
            v = (s32(((m.s16(player + 0x344) * sp) >> 12) << 16), vy,
                 s32(((m.s16(player + 0x350) * sp) >> 12) << 16))
    m.w32(B + 0x30, v[0])
    m.w32(B + 0x34, v[1] if act & 0x20000000 or speed == -1 else vy)
    m.w32(B + 0x38, v[2])
    if not act & 0x20000000:
        for o in (0x54, 0x58, 0x5C):
            # the product is a 32-bit mult's low word: it wraps for spins of 2.0 (0x20000)
            m.w32(B + o, s32(-spin + (s32(rd() * 2 * spin) >> 15)) if spin * 2 else s32(-spin))
    m.w32(B + 4, ticks)
    m.w32(B + 0x1C, drag)
    m.w32(B + 0x20, gravity)
    m.w32(B + 0x10, 0)
    m.w32(B + 0xC, bounces)


def wrap_angle(v):
    while v < -0x8000000:
        v += 0x10000000
    while v > 0x8000000:
        v -= 0x10000000
    return v


def fall_step(eng, B, feet):
    """0x80050E34: one tick of a falling attachment; 1 when it bounced."""
    m = eng.m
    for k in range(3):
        m.w32(B + 0x48 + 4 * k, m.u32(B + 0x3C + 4 * k))
    t = s32(m.s32(B + 4) - 1)
    m.w32(B + 4, t)
    if t <= 0:
        f = point_floor(eng, B + 0x3C)
        m.w32(B + 0x40, s32((f << 16) - 0xF0000))
        for o in (0x60, 0x64, 0x68):
            m.w32(B + o, wrap_angle(m.s32(B + o)))
        m.w32(B + 4, 0)
        m.w32(B, (m.u32(B) | 0x14) & ~2)
        return 0
    for a, s in ((0x60, 0x54), (0x68, 0x5C), (0x64, 0x58)):
        m.w32(B + a, s32(m.s32(B + a) + m.s32(B + s)))
    drag = m.s32(B + 0x1C)
    vx, vz = m.s32(B + 0x30), m.s32(B + 0x38)
    m.w32(B + 0x3C, s32(m.s32(B + 0x3C) + (vx - drag if vx > 0 else vx + drag)))
    m.w32(B + 0x40, s32(m.s32(B + 0x40) + m.s32(B + 0x34)))
    m.w32(B + 0x44, s32(m.s32(B + 0x44) + (vz - drag if vz > 0 else vz + drag)))
    if point_floor(eng, B + 0x3C) == -0x7FFF:
        for k in range(3):
            m.w32(B + 0x3C + 4 * k, m.u32(B + 0x48 + 4 * k))
        m.w32(B + 0x30, s32(-m.s32(B + 0x30)))
        m.w32(B + 0x38, s32(-m.s32(B + 0x38)))
    vy = s32(m.s32(B + 0x34) + m.s32(B + 0x20))
    m.w32(B + 0x34, min(vy, 0x4B0000))
    bounced = 0
    if feet < m.s32(B + 0x40):
        m.w32(B + 0x40, feet)
        if m.s32(B + 0x10) < m.s32(B + 0xC):
            bounced = 1
            m.w32(B + 0x20, m.s32(B + 0x20) >> 1)
            m.w32(B + 0x34, s32(-(m.s32(B + 0x34) >> 2)))
            m.w32(B + 0x30, m.s32(B + 0x30) >> 2)
            m.w32(B + 0x54, s32(-(m.s32(B + 0x54) >> 2)))
            m.w32(B + 0x38, m.s32(B + 0x38) >> 2)
            m.w32(B + 0x1C, m.s32(B + 0x1C) >> 2)
            m.w32(B + 0x58, m.s32(B + 0x58) >> 2)
            m.w32(B + 0x5C, s32(-(m.s32(B + 0x5C) >> 2)))
            m.w32(B + 0x10, m.s32(B + 0x10) + 1)
        else:
            m.w32(B + 4, 0)
        if abs(m.s32(B + 0x30)) <= 0xFFFF and abs(m.s32(B + 0x38)) <= 0xFFFF:
            m.w32(B + 4, 0)
    return bounced


def point_floor(eng, p):
    """func_8003EE30 at the 16.16 point p (its integer parts): the floor nearest it (levels.md,
    The floor nearest a point); -0x7FFF none."""
    m = eng.m
    return eng.world.point_floor([m.s16(p + 2 + 4 * k) for k in range(3)])


def attachments_update(eng, e):
    """0x80055198, from enemy_think: falling ones step (a bounce plays the template's sound,
    quieter each bounce); an attached one of a dying soldier (variable 0xD = -1) is detached
    (the template's model, flag 8); one turning to rest turns 0x224 a tick toward the resting
    angles and, done, with flag 0x40 drops the weapon as a pickup."""
    m = eng.m
    obj = m.u32(e + 0x388)
    for k in range(m.s32(obj + 0x18)):
        B, T = obj + 0x1C + 0x144 * k, template(k)
        f = m.u32(B)
        if f & 2:
            if fall_step(eng, B, m.s32(e + 0x210)):
                snd = m.s32(T + 0x24)
                if snd != -1:
                    att = max(m.s32(B + 0x10) - 1, 0)
                    if snd >= 0:
                        if m.s32(T + 0x28):
                            snd += (eng.rand.draw(*FN["enemy_aim"]) * m.s32(T + 0x28)) >> 15
                        bank = m.s32(T + 0x2C)
                        if bank < 0:
                            bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8)
                        eng.audio.play(s16(bank), snd, e, 0x10000 >> att, 0, 0x10000, 0x1800, 0, 0, 0, 0)
        elif f & 1 and m.s32(e + 0x4C + 4 * 0xD) == -1:
            swap_model(eng, B, m.u32(T + 0x20))
            m.w32(B + 0xE8, 0)
            m.w32(B + 0xEC, 0)
            m.w32(B, m.u32(B) | 8)
        if m.u32(B) & 0x10:
            done = True
            ax = m.s32(T + 0x30)
            x = m.s32(B + 0x60)
            if x < 0 and x < s32(0xFC000000):
                ax = s32(ax - 0x08000000)
            elif x > 0x04000000:
                ax = s32(ax + 0x08000000)
            if x != ax:
                done = False
                x = s32(x - 0x02240000) if ax < x else s32(x + 0x02240000)
                if (x < ax) if ax < m.s32(B + 0x60) else (ax < x):
                    x = ax
                m.w32(B + 0x60, x)
            z = m.s32(B + 0x68)
            az = m.s32(T + 0x38)
            if z < 0:
                az = -az
            if z != az:
                done = False
                z2 = s32(z - 0x02240000) if az < z else s32(z + 0x02240000)
                if (z2 < az) if az < z else (az < z2):
                    z2 = az
                m.w32(B + 0x68, z2)
            if done:
                fl = m.u32(B)
                m.w32(B, fl & ~0x10)
                tmpl = m.u32(m.u32(e + 0x1C) + 4) if m.u32(e + 0x1C) else 0
                if fl & 0x40 and tmpl and m.u32(tmpl + 0x7C):
                    w = m.s32(e + 0x4C + 4 * 0xF) & 0xFF
                    m.w32(B, (m.u32(B) & ~1) | 0x20)
                    drop_pickup(eng, e, B + 0x3C, B + 0x60, m.u32(G_ENEMY_WEAPONS + 0x30 * w), w)


def attachments_op(eng, e, ops, arg):
    """enemy_attachments_op (0x800523E4), variable 0x1A's bits over the attachments with an
    action: 0x10000000 set +8 to v1B; 0x08000000 hide; 0x04000000 show; 0x20000000 drop the
    grenade; 0x02000000 throw it; 0x01000000 lob it; none: knock it off from part v1B."""
    m = eng.m
    obj = m.u32(e + 0x388)
    for k in range(m.s32(obj + 0x18)):
        if not (ops >> k) & 1:
            continue
        B, T = obj + 0x1C + 0x144 * k, template(k)
        f = m.u32(B)
        if f & 6:
            continue
        if ops & 0x10000000:
            m.w32(B + 8, arg)
            continue
        if ops & 0x08000000:
            m.w32(B, (f & ~1) | 0x20)
            continue
        if ops & 0x04000000:
            m.w32(B, (f | 1) & ~0x20)
            continue
        if ops & 0x20000000 and k == 2:
            if f & 1:
                eng.note("grenade drop (grenade_spawn) not implemented")
                m.w32(B, (f | 0x20) & ~1)
            continue
        if ops & 0x03000000:
            if f & 1 and m_hash(eng, B) == 0x61282777:
                eng.note("grenade throw/lob (grenade_spawn) not implemented")
                m.w32(B, (f | 0x20) & ~1)
            continue
        if not f & 0x20:
            if m.s32(B + 8) == 0:
                q = m.u32(e + 0x304) + 0x48 * arg
                for i in range(3):
                    m.w32(B + 0x3C + 4 * i, s32(m.s32(q + 0x20 + 4 * i) << 16))
            else:
                angles_from_matrix(m, B)
                m.w32(B + 8, 0)
            swap_model(eng, B, m.u32(T + 0x20))
            knock_off(eng, B, m.u32(G_SOLDIER), m.s32(T + 0x10), m.s32(T + 0x14), m.s32(T), m.s32(T + 8),
                      m.s32(T + 0xC), m.s32(T + 4), m.s32(T + 0x1C), ops)
            m.w32(B + 0xE8, 0)
            m.w32(B + 0xEC, 0)
            m.w32(B, m.u32(B) | 3)


# ---------------------------------------------------------------- the weapon as a pickup

def drop_pickup(eng, e, pos, ang, model_hash, w):
    """func_80083728: a pickup of the soldier's record template's kind (+0x7C) at the weapon's
    place and angles (health and item kinds at the soldier, default model), linked at the front.
    The record it reads is built on the stack: fields not written there (+0x30) read as 0."""
    m = eng.m
    tmpl = m.u32(m.u32(e + 0x1C) + 4)
    kind_hash = m.u32(tmpl + 0x7C)
    rec = dict(flags58=1, cull=0x96, count=m.u8(tmpl + 0x62), w=w, flags30=0)
    if kind_hash in NO_MODEL_SPECIAL:
        p = [m.s16(e + 0x20E), m.s16(e + 0x212), m.s16(e + 0x216)]
        a = [0, 0, 0]
        model_hash = 0xFFFFFFFF
    else:
        p = [m.s16(pos + 4 * k + 2) for k in range(3)]
        a = [m.s16(ang + 4 * k + 2) for k in range(3)]
    rec["pos"], rec["ang"] = p, a
    pk = pickup_spawn(eng, rec, kind_hash, model_hash, e)
    if pk:
        eng.link_front(pk)
        m.w32(pk + 0x1C, 0)
    return pk


def pickup_spawn(eng, rec, kind_hash, model_hash, owner):
    """pickup_spawn (0x800833A0): the kind's table entry (0x8009E8E8, 0x14 bytes: hash, model,
    object size, init, touch), type 0x65, flags A 0x47028200, flags B 0x40211, mask 1, the
    record's position and angles, y snapped to the floor within 55, the sphere from the model's
    box, the cull counter for record +0x58 bit 0, the object {kind, model, 900, count}."""
    m = eng.m
    kind = None
    for i in range(0x14):
        if m.u32(PICKUPS + 0x14 * i) == kind_hash:
            kind = i
            break
    if kind is None:
        eng.note("pickup kind unknown", f"{kind_hash:08X}")
        return None
    t = PICKUPS + 0x14 * kind
    if model_hash == 0xFFFFFFFF:
        model_hash = m.u32(t + 4)
    from engine_objects import model_by_hash
    model = model_by_hash(eng, model_hash)
    if not model:
        eng.note("pickup model missing", f"{model_hash:08X}")
        return None
    p = eng.entity_create(0x65, obj_size=m.u16(t + 8), model=model, iflags=0x157)
    if not p:
        return None
    m.w32(p + 8, 0x65)
    m.w32(p + 0x36C, PICKUP_THINK)
    m.w32(p + 0xC, 0x47028200)
    m.w32(p + 0x10, 0x40211)
    m.w32(p + 0x374, 0)
    m.w32(p + 0x370, 0x8006A5B4)
    m.w32(p + 0x378, 0)
    m.w32(p + 0x384, 0x8006A37C)
    m.w32(p + 0x14, 1)
    m.w32(p + 0x37C, m.u32(t + 0x10))
    for k in range(3):
        m.w32(p + 0x20C + 4 * k, s32(rec["pos"][k] << 16))
        m.w32(p + 0x228 + 4 * k, s32(rec["ang"][k] << 16))
    f, _ = eng.world.floor([m.s32(p + 0x20C + 4 * k) for k in range(3)])
    if f is not None and f != -0x7FFF:
        y = rec["pos"][1]
        if abs(y - f) < 0x37:
            rec["pos"][1] = f
            if rec.get("addr"):
                m.w16(rec["addr"] + 0x12, f)     # the record's own y is changed
            m.w32(p + 0x210, s32(f << 16))
    m.w16(p + 0x2EC, 0)
    m.w16(p + 0x36, 0)
    lo = [m.s16(model + 0x44 + 2 * k) for k in range(3)]
    hi = [m.s16(model + 0x7C + 2 * k) for k in range(3)]
    d = [abs(hi[k] - lo[k]) & 0xFFFF for k in range(3)]
    big = max(d[0], d[1])
    r = (d[2] + 0x1E) if big < d[2] else (big + 0x1E)
    m.w16(p + 0x2E8, r >> 1)
    for k in range(3):
        m.w16(p + 0x2D8 + 2 * k, (lo[k] + (s32(d[k] << 16) >> 17)) & 0xFFFF)
    if rec["flags58"] & 1:
        m.w32(p + 0xC, m.u32(p + 0xC) | 0x8000)
        m.w32(p + 0x294, rec["cull"])
        m.w32(p + 0x290, rec["cull"])
    o = m.u32(p + 0x388)
    m.w32(o, kind)
    m.w32(o + 4, model_hash)
    m.w32(o + 8, 0x384)
    m.w32(o + 0xC, rec.get("w", -1) if rec["flags30"] & 8 else -1)

    init = m.u32(t + 0xC)
    if init == 0x80083058:
        m.w32(o + 0x14, {0: 0xD0000, 1: 0x200000, 2: 0x400000}.get(kind, m.u32(o + 0x14)))
    elif init == 0x800830B0:
        m.w32(o + 0x14, rec["count"])
    return p


# ---------------------------------------------------------------- the rest of the think

def ranges_update(eng, e):
    """enemy_ranges_update (0x80055528): v5, v6, v7 from the brain's +0xB2 (the third of the
    first soldier's, kept once in 0x800AB930); 3/4, 3/8, 1/4 while the player crouches."""
    m = eng.m
    b = m.u32(e + 0x38C)
    if not m.u32(G_RANGE_THIRD_SET):
        m.w32(G_RANGE_THIRD_SET, 1)
        h = m.s16(b + 0xB2)
        q = abs(h) // 3
        m.w16(G_RANGE_THIRD, q if h >= 0 else -q)
    a2 = m.u16(b + 0xB2)
    v = s32(a2 << 16)
    if m.u32(m.u32(m.u32(G_SOLDIER) + 0x388)) & 2:
        a0 = v >> 18
        a2 = a2 - a0
        s0 = (v >> 17) - (v >> 19)
        s1 = a0
    else:
        s0 = v >> 17
        s1 = m.u16(G_RANGE_THIRD)
    for var, x in ((5, a2), (6, s0), (7, s1)):
        m.w32(e + 0x4C + 4 * var, s16(x))


def hit_reaction(eng, e, var, part):
    """0x80052084 (variable 0x1C, the hit-part reaction): by the blood setting (0x80093A05; 0:
    reactions 1, 4, 8 become 2) a burst of drops (1), a puff (2), or drops in a spray (4, 8),
    from the part's centre; the variable back to 0 for 1 and 2."""
    m = eng.m
    obj = m.u32(e + 0x388)
    q = m.u32(e + 0x304) + 0x48 * part
    pos = [s32(m.s32(q + 0x20 + 4 * k) << 16) for k in range(3)]
    blood = m.u8(G_BLOOD)
    r = m.s32(e + 0x4C + 4 * var)
    if blood == 0 and r in (1, 4, 8):
        r = 2
    elif blood == 1 and r in (4, 8):
        r = 1
    m.w32(e + 0x4C + 4 * var, r)
    rd = lambda: eng.rand.draw(*FN["hit_reaction"])
    if r == 1:
        if blood == 1:
            n, life, spread = 0xB, 0x19, ((rd() * 0xA) >> 15) + 0xF
        else:
            n, life, spread = 0xF, 0x23, ((rd() * 0xF) >> 15) + 0x14
        drops_spawn(eng, obj + 0x49C, pos, n, life, spread, 0x10000, 4, 0, 0)
        m.w32(e + 0x4C + 4 * var, 0)
    elif r == 2:
        puff_spawn(eng, obj + 0x3E8, pos)
        m.w32(e + 0x4C + 4 * var, 0)
    elif r in (4, 8):
        eng.note("blood spray reactions (4, 8) not implemented")


def puff_spawn(eng, base, pos):
    """0x80072244: the first free of 5 puffs: 60 ticks, a random angle and spin 8..32."""
    m = eng.m
    for i in range(5):
        p = base + 0x24 * i
        if m.u32(p) & 1:
            continue
        m.w32(p, m.u32(p) | 1)
        m.w32(p + 4, 0x3C)
        m.w32(p + 8, (eng.rand.draw(*FN["hit_reaction"]) << 12) >> 15)
        for k in range(3):
            m.w32(p + 0x10 + 4 * k, pos[k])
        m.w32(p + 0x1C, 0x20000)
        for k in range(3):
            m.w8(p + 0x20 + k, 0x3C)
        m.w32(p + 0xC, ((eng.rand.draw(*FN["hit_reaction"]) * 0x19) >> 15) + 8)
        return


def puffs_update(m, base):
    """enemy_puffs_update (0x80072704): each live puff rises 1 a tick, turns, fades by 1/8."""
    any_ = 0
    for i in range(5):
        p = base + 0x24 * i
        if not m.u32(p) & 1:
            continue
        any_ = 1
        t = s32(m.s32(p + 4) - 1)
        m.w32(p + 4, t)
        if t <= 0:
            m.w32(p, 0)
            m.w32(p + 4, 0)
            continue
        m.w32(p + 0x14, s32(m.s32(p + 0x14) - 0x10000))
        m.w32(p + 8, s32(m.s32(p + 8) + m.s32(p + 0xC)))
        for k in range(3):
            c = m.u8(p + 0x20 + k)
            m.w8(p + 0x20 + k, c - (c >> 3))
    return any_


def drops_spawn(eng, base, pos, n, life, spread, vy, a6, a7, a8):
    """0x800727B4: up to n of 15 drops, each with a random angle and velocity."""
    m = eng.m
    rd = lambda: eng.rand.draw(*FN["hit_reaction"])
    for i in range(0xF):
        p = base + 0x74 * i
        if m.u32(p) & 1:
            continue
        m.w32(p, m.u32(p) | 0x31 | a8)
        m.w32(p + 4, life)
        m.w32(p + 8, (rd() << 12) >> 15)
        for c in (0x28, 0x34, 0x40):
            for k in range(3):
                m.w32(p + c + 4 * k, pos[k])
        m.w32(p + 0x10, 0x10000)
        m.w8(p + 0x4C, 0x3C)
        m.w8(p + 0x4E, 0)
        m.w8(p + 0x4D, 0)
        m.w32(p + 0xC, 0)
        sxz, sy = spread * 2, vy + 5
        m.w32(p + 0x1C, s32((-spread + ((rd() * sxz) >> 15)) << 16) if sxz else s32(-spread << 16))
        m.w32(p + 0x20, s32((-vy + ((rd() * sy) >> 15)) << 16) if sy else s32(-vy << 16))
        m.w32(p + 0x24, s32((-spread + ((rd() * sxz) >> 15)) << 16) if sxz else s32(-spread << 16))
        m.w32(p + 0x14, 0x10000)
        m.w8(p + 0x70, a6 + 0xF)
        m.w8(p + 0x71, a6 + 0xF)
        m.w32(p + 0x18, a7)
        n -= 1
        if n <= 0:
            return


def drops_update(eng, e, base):
    """enemy_drops_update (0x800730D4): drops fall to the soldier's floor (+0x208) and lie there
    (0x80072EEC: one draw; the splat's shape is drawing only)."""
    m = eng.m
    any_ = 0
    for i in range(0xF):
        p = base + 0x74 * i
        if not m.u32(p) & 1:
            continue
        any_ = 1
        t = s32(m.s32(p + 4) - 1)
        m.w32(p + 4, t)
        if t <= 0:
            m.w32(p, 0)
            m.w32(p + 4, 0)
            continue
        for k in range(3):
            m.w32(p + 0x40 + 4 * k, m.u32(p + 0x34 + 4 * k))
        y = s32(m.s32(p + 0x38) + m.s32(p + 0x20))
        m.w32(p + 0x38, y)
        if m.s32(e + 0x208) < y:
            if not m.u32(p) & 8:
                m.w32(p + 0x38, m.u32(e + 0x208))
                for o in (0x24, 0x20, 0x1C):
                    m.w32(p + o, 0)
                eng.rand.draw(*FN["hit_reaction"])
                m.w32(p, (m.u32(p) & ~6) | 8)
        elif not m.u32(p) & 8:
            m.w32(p + 0x34, s32(m.s32(p + 0x34) + m.s32(p + 0x1C)))
            m.w32(p + 0x3C, s32(m.s32(p + 0x3C) + m.s32(p + 0x24)))
            drag = m.s32(p + 0x14)
            for o in (0x1C, 0x24):
                v = m.s32(p + o)
                if v > 0:
                    m.w32(p + o, max(s32(v - drag), 0))
                elif v < 0:
                    m.w32(p + o, min(s32(v + drag), 0))
            g = m.s32(p + 0x18)
            m.w32(p + 0x20, min(s32(m.s32(p + 0x20) + g), g * 2))
            m.w32(p + 8, s32(m.s32(p + 8) + m.s32(p + 0xC)))
    return any_


# ---------------------------------------------------------------- taking pickups

G_SLOT_COUNT = 0x8009E874
G_SLOT_WEAPON = 0x8009E878
G_WEAPONS = 0x80093640


def health_touch(eng, p, e):
    """health_pickup_touch (0x80082E90): not for the dead or removed (flags A 0x20180000) nor at
    full health (128.0): the message (kind 0..2: 0x8009E864.., engine_texts), health += the
    pickup's +0x14 up to 128.0, sound 0x1F
    (kind 0) or 0x1E, removed."""
    m = eng.m
    if m.u32(e + 0xC) & 0x20180000 or m.s32(e + 0xF4) == 0x800000:
        return 0
    o = m.u32(p + 0x388)
    from engine_texts import pickup_message
    kind = m.s32(o)
    pickup_message(eng, e, m.u32(0x8009E864 + 4 * kind) if kind in (0, 1, 2) else 0)
    m.w32(e + 0xF4, min(s32(m.s32(e + 0xF4) + m.s32(o + 0x14)), 0x800000))
    snd = {0: 0x1F, 1: 0x1E, 2: 0x1E}.get(m.s32(o), -1)
    if snd != -1:
        eng.audio.play(0, snd, e, 0x20000, 0, 0x10000, 0x3333, 0, 0, 0, 0)
    eng.remove(p, "health taken")
    return 1


def ammo_touch(eng, p, e):
    """ammo_pickup_touch (0x800830BC): the slot holding the kind's weapon (table +0xA), if its
    reserve (inv +0x56) is below its maximum (+0x5E): the text, sound 0x20, ammo_add, removed.
    Returns 1 once the weapon is carried (taken or not)."""
    m = eng.m
    if m.u32(e + 0xC) & 0x20180000:
        return 0
    o = m.u32(p + 0x388)
    w = m.u16(PICKUPS + 0x14 * m.s32(o) + 0xA)
    inv = m.u32(e + 0x388)
    for slot in range(m.s32(G_SLOT_COUNT)):
        if m.u8(G_SLOT_WEAPON + slot) == w:
            if m.s16(inv + 0x56 + 2 * slot) < m.s16(inv + 0x5E + 2 * slot):
                from engine_texts import pickup_message
                pickup_message(eng, e, m.u32(G_WEAPONS + 0x44 * w + 0x3C))
                eng.audio.play(0, 0x20, e, 0x10000, 0, 0x10000, 0x3333, 0, 0, 0, 0)
                ammo_add(eng, e, slot, m.s32(o + 0x14))
                eng.remove(p, "ammo taken")
            return 1
    return 0


def ammo_add(eng, e, slot, n):
    """ammo_add (0x8004301C): the reserve += n up to the maximum (a negative reserve is set to
    n); the slot weapon's reload sound (+0x20, bank +0x22 or its model's), pitch 0.5."""
    m = eng.m
    inv = m.u32(e + 0x388)
    a = inv + 0x56 + 2 * slot
    n = s16(n)
    if m.s16(a) < 0:
        m.w16(a, n)
    else:
        m.w16(a, min(s16(n + m.s16(a)), m.s16(inv + 0x5E + 2 * slot)))
    w = G_WEAPONS + 0x44 * m.u8(G_SLOT_WEAPON + slot)
    snd = m.s16(w + 0x20)
    if snd >= 0:
        bank = m.s16(w + 0x22)
        if bank < 0:
            bank = m.s16(m.u32(w + 0x40) + 0xC8)
        flags = (m.u16(G_WEAPONS + 0x44 * m.u8(inv + 0x55) + 0x2A) >> 3) & 2
        eng.audio.play(bank, snd, e, 0x10000, 0, 0x7FFF, 0, 0, 0, 0, flags)
    from engine_texts import ammo_hud_update
    ammo_hud_update(eng, e)


def item_touch(eng, p, e):
    """item_pickup_touch (0x800832C0): kinds 15..19 send level messages 0x2F, 0x30, 0x31, 0x69,
    0x6A with the pickup as sender; removed."""
    m = eng.m
    msg = {15: 0x2F, 16: 0x30, 17: 0x31, 18: 0x69, 19: 0x6A}.get(m.s32(m.u32(p + 0x388)))
    if msg is not None:
        eng.logic.send(msg, eng.ents.get(p), 0, None, src="item pickup")
    eng.remove(p, "item taken")
    return 1


def register(eng):
    eng.handlers[("touch", 0x80082E90)] = lambda p, e: health_touch(eng, p, e)
    eng.handlers[("touch", 0x800830BC)] = lambda p, e: ammo_touch(eng, p, e)
    eng.handlers[("touch", 0x800832C0)] = lambda p, e: item_touch(eng, p, e)
