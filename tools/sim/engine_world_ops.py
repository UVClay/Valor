"""Model-script ops that read or change the world, done by the engine (the handlers named)."""
from engine import s16, s32
import engine_sense as es


def world_op(vm, op, pc, w, mask, imm, O, W):
    """Returns the next pc, or None to let the interpreter run the op."""
    eng, m, e = vm.eng, vm.m, vm.e
    imm_lo, imm_hi = imm & 0xFF, imm >> 8
    if op == 0x33:      # 0x8005B23C: compute (node search 0x80040F48), then the variable
        r = es.compute_node(eng, e, O(2, W(2)), O(3, W(3)), O(4, W(4)))
        vm.set_var(O(1, W(1)), r)
        return pc + 0x18
    if op == 0x35:      # 0x8005B7F0: node_write: kind 1 word 0, 6 the holder handle
        wo = m.u32(m.u32(0x8009D648) + 0x388)
        node, val = O(1, W(1)) & 0xFFFFFFFF, O(2, W(2))
        if node < m.u32(wo + 0xC):
            nd = m.u32(wo + 8) + 0x34 * node
            if imm_lo == 1:
                m.w32(nd, val)
            elif imm_lo == 6:
                m.w32(nd + 0x30, m.u32(val) if val else 0)
        return pc + 0xC
    if op == 0x37:      # 0x8005BAC4: route toward an entity
        t1, t2 = s32(O(1, W(1)) << 24) >> 24, s32(O(2, W(2)) << 24) >> 24
        if t1 != -1:
            tgt = es.find_type_group(m, t1, t2 if t2 != -1 else 0)
        else:
            tgt = O(3, W(3)) & 0xFFFFFFFF
        v1, v2 = O(4, W(4)), O(5, W(5))
        if tgt:
            k, a = es.route_turn_kind(m, e, m.s16(tgt + 0x20E), m.s16(tgt + 0x216))
        else:
            k, a = 0, 0
        vm.set_var(v1, k)
        vm.set_var(v2, a)
        return pc + 0x18
    if op == 0x3A:      # 0x8005BDC4: route toward a node's standing point or side waypoint
        node = O(1, W(1))
        v1, v2 = O(2, W(2)), O(3, W(3))
        got = {}
        via = es.brain_node_target(m, e, lambda f, v: got.__setitem__(f, v), s32(node))
        if via:
            nodes = m.u32(m.u32(m.u32(0x8009D648) + 0x388) + 8)
            px, pz = got["side"]
        else:
            px, pz = got["node_point"][0], got["node_point"][2]
        k, a = es.route_turn_kind(m, e, px, pz)
        vm.set_var(v1, k)
        vm.set_var(v2, a)
        return pc + 0x10
    if op == 0x3E:      # 0x8005C000: probe
        r = es.probe(eng, e, O(2, imm_hi), s16(O(3, W(1))))
        vm.set_var(s16(O(1, imm_lo)), r)
        return pc + 8
    if op == 0x46:      # 0x8005C398: follow
        return follow(vm, pc, O, W, imm)
    if op == 0x34:      # 0x8005B32C: node_eval
        return node_eval(vm, pc, w, O, W)
    if op == 0x36:      # 0x8005B8EC: node_take
        return node_take(vm, pc, O, W)
    return None


def node_take(vm, pc, O, W):
    """Op 36 (0x8005B8EC): take a node. Operands: the node, a variable, how. For a node in the
    world's table (0x34-byte records, the world object's +8, count +0xC): a node with +4 set
    halves the brain's radii into +0x2E8 and +0x3C8; how 0 or 1: x and z on the brain's node
    point (+0xA0, +0xA4); how 0 or 2: the heading from the node's +0x1E, else how 4: ratan2 of
    its +0x24, +0x28, into +0x22C and the variable; flags B |= 0x80."""
    import combat_sim as cs
    m, e = vm.m, vm.e
    obj = m.u32(m.u32(0x8009D648) + 0x388)
    node, var, how = O(1, W(1)) & 0xFFFFFFFF, O(2, W(2)), O(3, W(3)) & 0xFFFFFFFF
    if node < m.u32(obj + 0xC):
        n = m.u32(obj + 8) + 0x34 * node
        br = m.u32(e + 0x38C)
        if m.s32(n + 4):
            m.w16(e + 0x2E8, s16(s32(m.u16(br + 0xC6) << 16) >> 17))
            m.w16(e + 0x3C8, s16(s32(m.u16(br + 0xCA) << 16) >> 17))
        if how == 0 or how & 1:
            m.w32(e + 0x20C, s32(m.s16(br + 0xA0) << 16))
            m.w32(e + 0x214, s32(m.s16(br + 0xA4) << 16))
        a = None
        if how == 0 or how & 2:
            a = s32(m.s16(n + 0x1E) << 16)
        elif how & 4:
            a = s32(cs.ratan2(m.s16(n + 0x24), m.s16(n + 0x28)) << 16)
        if a is not None:
            m.w32(e + 0x22C, a)
            vm.set_var(var, a)
        m.w32(e + 0x10, m.u32(e + 0x10) | 0x80)
    return pc + 0x10


def follow(vm, pc, O, W, imm):
    """Op 46 (0x8005C398): place the entity at a distance from another. Operands: the target
    (W1 & 0xFFFF), the distance (imm), a sideways offset (W2 & 0xFFFF), a variable (W2 >> 16 &
    0xFFF), a mode (W2 >> 28). Mode 0: the direction from the entity to the target (VectorNormalSS
    of dx, 0, dz), the entity faces it (yaw into the variable and +0x22C); mode 1: the target's
    forward column and yaw. Then the position x, z = (with an offset: the target's matrix and
    translation applied to (offset, 0, 0); else the target's position) - direction * distance."""
    import engine_gte as gt
    import combat_sim as cs
    m, e = vm.m, vm.e
    dist = O(2, imm)
    tgt = O(1, W(1) & 0xFFFF) & 0xFFFFFFFF
    off = O(3, W(2) & 0xFFFF)
    var = O(4, (W(2) >> 16) & 0xFFF)
    mode = O(5, (W(2) >> 28) & 0xF)
    if mode == 0:
        u = gt.vector_normal([s16(m.s16(tgt + 0x20E) - m.s16(e + 0x20E)), 0,
                              s16(m.s16(tgt + 0x216) - m.s16(e + 0x216))])
        yaw = s32(cs.ratan2(u[0], u[2]) << 16)
    elif mode & 1:
        u = [m.s16(tgt + 0x344), 0, m.s16(tgt + 0x350)]
        yaw = m.s32(tgt + 0x22C)
    else:
        return pc + 0xC
    vm.set_var(var, yaw)
    m.w32(e + 0x22C, yaw)
    if s16(off):
        M = [[m.s16(tgt + 0x340 + 6 * i + 2 * k) for k in range(3)] for i in range(3)]
        T = [m.s32(tgt + 0x354 + 4 * k) for k in range(3)]
        mac, _ = gt.mul(M, [s16(off), 0, 0], T, 12)
        p = [s16(x) for x in mac]
        m.w32(e + 0x20C, s32((((-s16(u[0]) * dist) >> 12) + p[0]) << 16))
        m.w32(e + 0x214, s32((((-s16(u[2]) * dist) >> 12) + p[2]) << 16))
    else:
        m.w32(e + 0x20C, s32(m.s32(tgt + 0x20C) + (((-s16(u[0]) * dist) >> 12) << 16)))
        m.w32(e + 0x214, s32(m.s32(tgt + 0x214) + (((-s16(u[2]) * dist) >> 12) << 16)))
    return pc + 0xC


def node_eval(vm, pc, w, O, W):
    """Op 34 (0x8005B32C): a fact about node N (operand 1) into a variable (operand 2), the fact
    the instruction's low byte: 1..5 the node's words +0..+0x10, 6 its holder (the entity of the
    handle +0x30), 7 its cover score: 0x7FFF if the node has +4 and its link +0x2C names an entry
    of the world object's table +0x14 (8 bytes) with bit 2; else, from the soldier to the node's
    point (the brain's node point +0xA0 when it holds this node, else the node's +0x14), 0x7FFE
    when the node's +4 is 500 or more and the soldier is 201 or more off in height or in front
    of it (offset . direction +0x24 >> 12 at least 101), or when the player is within 600 of
    the point; otherwise the angle between the node's direction and the player (|wrap|) if the
    player is within 3500 (7000 for kind 0x10), farther than the soldier and under 0x400 off,
    else 0x7FFD. -1 for N past the node count."""
    import combat_sim as cs
    eng, m, e = vm.eng, vm.m, vm.e
    wo = m.u32(m.u32(0x8009D648) + 0x388)
    base = m.u32(wo + 8)
    n = O(1, W(1)) & 0xFFFFFFFF
    var = O(2, W(2))
    fact = w & 0xFF
    if n >= m.u32(wo + 0xC):
        vm.set_var(var, -1)
        return pc + 0xC
    q = base + 0x34 * n
    v = 0
    if 1 <= fact <= 5:
        v = m.s32(q + 4 * (fact - 1))
    elif fact == 6:
        h = m.u32(q + 0x30)
        v = eng.resolve(h) if h else 0
    elif fact == 7:
        link = m.s32(q + 0x2C)
        if m.s32(q + 4) and link >= 0 and m.u32(m.u32(wo + 0x14) + 8 * link) & 2:
            v = 0x7FFF
        else:
            b = m.u32(e + 0x38C)
            src = b + 0xA0 if m.s16(b + 0xB6) == s16(n) else q + 0x14
            pt = [m.s16(src + 2 * k) for k in range(3)]
            d = [s16(m.s16(e + 0x20E + 4 * k) - pt[k]) for k in range(3)]
            near = False
            if m.s32(q + 4) >= 0x1F4:
                if abs(d[1]) >= 0xC9:
                    near = True
                else:
                    dot = s32(sum(d[k] * m.s16(q + 0x24 + 2 * k) for k in range(3))) >> 12
                    near = dot >= 0x65
            if near:
                v = 0x7FFE
            else:
                de = es.dist_approx(*d) & 0xFFFF
                p = m.u32(0x8009D654)
                dp = [s16(m.s16(p + 0x20E + 4 * k) - pt[k]) for k in range(3)]
                dpd = es.dist_approx(*dp) & 0xFFFF
                if dpd < 0x258:
                    v = 0x7FFE
                else:
                    a = es.wrap(s16(cs.ratan2(dp[0], dp[2]) - cs.ratan2(m.s16(q + 0x24), m.s16(q + 0x28))))
                    a = abs(a)
                    lim = 0x1B58 if m.s32(q + 0xC) == 0x10 else 0xDAC
                    v = a if (dpd < lim and de < dpd and a < 0x400) else 0x7FFD
    vm.set_var(var, v)
    return pc + 0xC
