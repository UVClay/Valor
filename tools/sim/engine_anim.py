"""Animation playback effects on the entity (docs/re/animation.md): root movement."""
import brain_sim as bs
from engine import s16, s32


def root_motion(eng, e):
    """anim_root_motion (0x8003F1F8), for entities with flags A 0x1000: the frame's root offset to
    the instance, its movement scaled by +0x224, turned by +0x340, into the push +0x1F0."""
    m = eng.m
    if not m.u32(e + 0xC) & 0x1000:
        return
    inst = m.u32(e + 0x100)
    fd = bs.frame_data(m, inst, m.s16(inst + 0x5C), m.s16(inst + 0x5E))
    if fd is None:
        return
    mv, off, turn = fd
    for k in range(3):
        m.w16(inst + 0x98 + 2 * k, off[k])
    mv = list(mv)
    if mv[1] < 0:
        m.w32(e + 0x284, 0)
    sp = m.s32(e + 0x224)
    if sp != 0x10000:
        mv = [(v * sp) >> 16 for v in mv]
    for k in range(3):
        m.w16(e + 0x27C + 2 * k, mv[k])
    rot = [m.s16(e + 0x340 + 2 * k) for k in range(9)]
    r = bs.apply_rot(rot, [s16(v) for v in mv])
    for k in range(3):
        m.w32(e + 0x1F0 + 4 * k, s32(m.s32(e + 0x1F0 + 4 * k) + (r[k] << 16)))


def entity_bounds(eng, e):
    """entity_bounds (0x8004D43C) / entity_parts_update (0x8004CEE4): every hit part's world centre
    from the joints and pose last built, and the entity's sphere on its bone or at the local point
    +0x2D8, through the matrix +0x340 and the integer position (animation.md, Hit spheres)."""
    import combat_sim as cs
    m = eng.m
    M = [[m.s16(e + 0x340 + 6 * i + 2 * j) for j in range(3)] for i in range(3)]
    t = [s16(m.s32(e + 0x20C + 4 * k) >> 16) for k in range(3)]
    for k in range(3):
        m.w32(e + 0x354 + 4 * k, t[k])
    inst = m.u32(e + 0x100)
    n = m.u16(e + 0x36)
    joints = quats = None
    if inst:
        nb = m.u16(inst + 0x72)
        jp = m.u32(inst + 0x94)
        if jp:
            joints = cs.slot_joints(m, jp, nb)
        qp = m.u32(inst + 0x88)
        quats = cs.slot_quats(m, qp, nb) if qp else None
    pp = m.u32(e + 0x304)
    eye = m.s32(m.u32(m.u32(0x8009D638) + 0x388) + 0x94)
    for i in range(n):
        p = pp + 0x48 * i
        part = dict(a=m.u16(p + 0x1C), b=m.u16(p + 0x1E), off=(m.s16(p + 0x40), m.s16(p + 0x42), m.s16(p + 0x44)),
                    offw=m.u32(p + 0x40), offz=m.s16(p + 0x44))
        if part["a"] not in (0xFFFE, 0xFFFF) and joints is None:
            c = [0, 0, 0]
        else:
            c = cs.part_centre_model(part, joints or {}, quats, eye)
        w = cs.to_world(M, t, c)
        for k in range(3):
            m.w32(p + 0x20 + 4 * k, w[k])
    bone = m.s16(e + 0x2EA)
    if bone >= 0:
        j = (joints or {}).get(bone, (0, 0, 0))
        w = cs.to_world(M, t, list(j))
    else:
        if m.u32(e + 0xC) & 1:
            # the player's sphere turns with the yaw and roll only, scaled (0x8008D918)
            import brain_sim as bs
            r = bs.rot_yxz(0, m.s16(e + 0x22E), m.s16(e + 0x232))
            sc = [m.s32(e + 0x270 + 4 * k) >> 4 for k in range(3)]
            M = [[s16((r[3 * i + k] * sc[k]) >> 12) for k in range(3)] for i in range(3)]
        w = cs.to_world(M, t, [m.s16(e + 0x2D8 + 2 * k) for k in range(3)])
    for k in range(3):
        m.w16(e + 0x2E0 + 2 * k, w[k])
