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
    if op in (0x36, 0x46):
        eng.note(f"script op {op:X} not implemented")
        return None
    return None
