"""The model-script VM for the engine: sst_vm.py's interpreter (docs/re/enemies.md, Scripts) with
the world it reads computed by the engine instead of taken from the next tick's dump, and the
effects it asks for (animation, sounds, brain goals, hit parts, messages) carried out at once."""
import sst_vm as sv
from sst_vm import s16, s32

FRAME = 0x14
import os
TRACE_VM = tuple(int(x) for x in os.environ["ENG_TRACE_VM"].split("-")) if os.environ.get("ENG_TRACE_VM") else None


class Calls(list):
    """The VM records effects as calls; the engine runs each as it is recorded, so its place in
    the think's order is the original's."""

    def __init__(self, vm):
        list.__init__(self)
        self.vm = vm

    def append(self, c):
        list.append(self, c)
        self.vm.effect(*c)


class EVM(sv.VM):
    def __init__(self, eng, e):
        sv.VM.__init__(self, eng.m, e, nxt=None, rand=None)
        self.eng = eng
        self.calls = Calls(self)

    # -- the engine's world
    def pos(self, ent, i):
        return self.m.s32(ent + 0x20C + 4 * i)

    def rand(self):
        return self.eng.rand.draw(*self.rand_fns())

    def rand_fns(self):
        from engine_feed import FN
        return FN["scripts"]

    def world(self, op, var, computed=None, cands=None):
        if computed is not None:
            return computed
        from engine_sense import world_value
        return world_value(self, op, var)

    def sensor(self, vid, m):
        if vid in (0x67, 0x71):
            from engine_sense import sensor
            return sensor(self, vid)
        return sv.VM.sensor(self, vid, m)

    # -- the effects
    def effect(self, ch, op, name, args):
        from engine_effects import run_effect
        run_effect(self, ch, op, name, args)

    # -- sop_event and script_start with the engine's addresses (sst_vm masks to 2 MB)
    def script_start(self, idx, force, ch):
        m, e = self.m, self.e
        idx = s16(idx)
        if idx < 0:
            return 0
        s = self.script_entry(idx)
        ctx = e + 0x104 + 0x74 * ch
        cls = m.u32(s)
        started = False
        if force:
            started, blend = True, 0
        elif cls & m.u32(ctx + 4):
            started, blend = True, 4
        elif (cls & m.u32(ctx)) or (not cls & m.u32(ctx + 8) and not cls & m.u32(ctx + 0xC)
                                     and m.u32(ctx + 0x10) == 0):
            started, blend = True, 0
        if not started:
            return 0
        m.w32(ctx + 0x10, s)
        m.w16(ctx + 0x1A, idx)
        m.w32(ctx + 0x14, m.u32(s + 0xC))
        m.w16(self.inst + 0x3E, blend)
        if blend:
            m.w16(self.inst + 0x40, -1)
            self.calls.append((ch, -1, "pose_blend", [idx]))
        m.w32(e + 0x1FC, 0)
        m.w32(ctx, 0)
        m.w32(ctx + 4, 0xFFFFFFFF)
        m.w32(ctx + 8, 0)
        m.w32(ctx + 0xC, 0)
        m.w32(e + 0x28C, m.u32(e + 0x288))
        count = m.u32(e + 0x1EC)
        table = m.u32(ctx + 0x20)
        for i in range(count):
            m.w32(table + 0x3C * i, 0)
        if ch == 0:
            self.calls.append((ch, -1, "hit_part_reset", []))
            n = m.u32(self.model + 0xB0)
            src = m.u32(self.model + 0xB4)
            m.copy(table, src, min(n, count) * 0x3C)
        m.w16(ctx + 0x18, -1)
        m.w16(ctx + 0x1C, 0)
        m.w16(ctx + 0x1E, 0)
        return 1

    def sop_event(self, pc, op):
        m, e, ctx = self.m, self.e, self.ctx
        w = m.u32(pc)
        cond = w & 0xFF
        vals = {}
        k = 1
        for bit in (1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80):
            if w & bit:
                vals[bit] = m.u32(pc + 4 * k)
                k += 1
        slots = {1: 8, 2: 0xC, 4: 0x10, 8: 0x14, 0x10: 0x18, 0x20: 0x1C, 0x40: 0x20, 0x80: 0x24}
        count = m.u32(e + 0x1EC)
        if m.s16(ctx + 0x1A) == m.s16(self.model + 0x94):
            table, count = m.u32(self.model + 0xB4), m.u32(self.model + 0xB0)
        elif op == 0x0E:
            table = m.u32(e + 0x124)
        else:
            table = m.u32(ctx + 0x20)
        slot = -1
        for i in range(count):
            r = table + 0x3C * i
            c = m.u32(r)
            if c == 0 and slot == -1:
                slot = i
                continue
            if c == cond and all(m.u32(r + slots[b]) == vals.get(b, 0) for b in slots):
                slot = i
                break
        stored = m.u8(pc + 4 * k + 3)
        n = m.s32(sv.STORED_LENGTH + 4 * stored)
        if slot >= 0:
            r = table + 0x3C * slot
            m.w32(r, 0 if stored == 0x0F else cond)
            for b, off in slots.items():
                m.w32(r + off, vals.get(b, 0))
            m.w32(r + 4, (w >> 8) & 0xFF)
            m.copy(r + 0x28, pc + 4 * k, 4 * n)
        return pc + 4 * (k + n)

    def run_b(self):
        # pad events (condition bits 1, 2): the player's scripts test the pad
        sv.VM.run_b(self)

    def think_scripts(self):
        """script_run_b then script_run_a, as a think calls them."""
        self.run_b()
        self.run_a()

    def op_query(self, pc, mask, w):
        """op_query (0x8005A574): the target as the original finds it, then the fact."""
        m, e = self.m, self.e
        O = lambda n, v: self.operand(n, mask, v)
        typ = O(2, m.s32(pc + 8))
        grp = O(3, (w >> 8) & 0xFF)
        tgt = O(4, m.s32(pc + 12)) & 0xFFFFFFFF
        typ8, grp8 = s32(typ << 24) >> 24, s32(grp << 24) >> 24
        if tgt == 0:
            from engine_sense import find_type_group, nearest
            if typ8 != -1:
                tgt = find_type_group(m, typ8, grp8) if grp8 != -1 else nearest(m, e, typ8)
            elif grp8 == typ8:
                tgt = e
        self._query_target = tgt
        return sv.VM.op_query(self, pc, mask, w)

    def step(self, pc, ch, ctx):
        m, e = self.m, self.e
        w = m.u32(pc)
        if TRACE_VM and TRACE_VM[0] <= self.eng.tick_no <= TRACE_VM[1]:
            print(f"  vm tick {self.eng.tick_no} {e:08X} ch{ch} pc {pc:08X} op {w >> 24:02X} w {w:08X} "
                  f"args {[m.u32(pc + 4 * k) for k in range(1, 4)]}")
        op = w >> 24
        mask, imm = (w >> 16) & 0xFF, w & 0xFFFF
        O = lambda n, v: self.operand(n, mask, v)
        W = lambda k: m.s32(pc + 4 * k)
        if op == 0x02:
            # op_sound (0x80058420): the five words go to script_operand whole (sst_vm.py keeps
            # only their low bytes); it does not yield
            self.ch, self.ctx, self.yielded = ch, ctx, 0
            self.calls.append((ch, op, "sound", [O(1, imm & 0xFF)] + [O(k + 2, W(k + 1)) for k in range(5)]
                               + [(imm >> 8) & 0xFF]))
            return pc + 0x18
        if op in (0x33, 0x37, 0x3A, 0x3E, 0x34, 0x35, 0x36, 0x46):
            from engine_world_ops import world_op
            self.ch, self.ctx, self.yielded = ch, ctx, 0
            r = world_op(self, op, pc, w, mask, imm, O, W)
            if r is not None:
                return r
        npc = sv.VM.step(self, pc, ch, ctx)
        if op == 0x00:
            # op 00 applies frame A's root movement (animation.md, Op 00)
            from engine_anim import root_motion
            root_motion(self.eng, self.e)
        return npc
