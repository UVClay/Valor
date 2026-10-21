#!/usr/bin/env python3
"""Reference interpreter of the model-script VM (docs/re/enemies.md, Scripts; soldier.md).

Steps one entity's two script channels for one tick, the way `enemy_think` / `dog_think` run
them: the event matcher (`script_run_b`) then the channel stepper (`script_run_a`), on a copy of
a RAM dump. Effects outside the VM (animations' poses, sounds, brain goals, hit parts, messages)
are recorded as calls with their operands, not simulated. Values that read the world (sight and
hearing tests, probes, node searches, the turn toward a target, rand) come from an oracle: the
next tick's dump, read as the variable the op writes (see Oracle).

  sst_vm.py DUMP_T DUMP_T1 [--ent ADDR] [-v]   step every soldier/dog of DUMP_T, compare to DUMP_T1
  sst_vm.py --pairs V... [--dir build/vm]       the same for the dump pairs dV.bin/d(V+1).bin,
                                                with agreement per opcode

A dump pair is two RAM dumps one game tick apart (g_tick 0x8009A5DC differs by 1).
"""
import argparse
import os
import struct
import sys
from collections import defaultdict

G_TICK = 0x8009A5DC
G_ENTITIES = 0x8009D64C
G_SOLDIER = 0x8009D654
G_WORLD = 0x8009D648
G_ALARM = 0x80097FE0
G_HANDLES = 0x8009D678
G_PARAMS = 0x800939F4
G_WITNESS_SPOT = 0x8009817C
STORED_LENGTH = 0x80090EA0   # words of an instruction stored by an event, by opcode
RAND_SEED = 0x800CA2A4

SOLDIER, DOG = 0x1402, 0x1405
THINKS = (0x80055628, 0x80054AB8)   # enemy_think, dog_think

NAMES = {
    0x00: "anim", 0x01: "mask", 0x02: "sound", 0x03: "gravity", 0x04: "fall", 0x05: "impulse",
    0x06: "nudge", 0x07: "goto", 0x08: "restart", 0x09: "hang", 0x0A: "turn_half", 0x0B: "hang",
    0x0C: "hit_part", 0x0D: "event", 0x0E: "event0", 0x0F: "nop", 0x10: "flags_set",
    0x11: "flags_clear", 0x12: "leave_ground", 0x13: "set", 0x14: "weight", 0x15: "spawn",
    0x16: "wait", 0x17: "remove_self", 0x18: "vec_a", 0x19: "vec_b", 0x1A: "bytes", 0x1B: "op1b",
    0x1C: "model_variant", 0x1D: "jump_script", 0x1E: "random", 0x1F: "property",
    0x20: "clear_input", 0x21: "random_timer", 0x22: "spin", 0x23: "spin_add", 0x24: "angles",
    0x25: "angles_add", 0x26: "goto", 0x27: "if", 0x28: "xor", 0x29: "or", 0x2A: "and",
    0x2B: "nop3", 0x2C: "add", 0x2D: "sub", 0x2E: "query", 0x2F: "behaviour", 0x30: "mul",
    0x31: "end", 0x32: "nearest", 0x33: "find_node", 0x34: "node_eval", 0x35: "node_write",
    0x36: "node_take", 0x37: "route", 0x38: "call", 0x39: "return", 0x3A: "route_node",
    0x3B: "start_script", 0x3C: "clear_script", 0x3D: "current_script", 0x3E: "probe",
    0x3F: "or_3c", 0x40: "or_38", 0x41: "instance_6c", 0x42: "event", 0x43: "send_event",
    0x44: "to_spawn", 0x45: "has_anim", 0x46: "follow", 0x47: "clear_events",
    0x48: "run_message", 0x49: "alarm",
}


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def cdiv(a, b):
    """C division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def cmod(a, b):
    return a - b * cdiv(a, b)


class Mem:
    def __init__(self, data):
        self.d = bytearray(data)

    def u8(self, a):
        return self.d[a & 0x1FFFFF]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x1FFFFF)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFF)[0]

    def w8(self, a, v):
        self.d[a & 0x1FFFFF] = v & 0xFF

    def w16(self, a, v):
        struct.pack_into("<H", self.d, a & 0x1FFFFF, v & 0xFFFF)

    def w32(self, a, v):
        struct.pack_into("<I", self.d, a & 0x1FFFFF, v & 0xFFFFFFFF)

    def entities(self):
        out, e = [], self.u32(G_ENTITIES)
        while e and len(out) < 1024:
            out.append(e)
            e = self.u32(e + 4)
        return out


def dist_approx(dx, dy, dz):
    """`dist_approx` (0x80056EB4): max + (the other two) / 4, of s16 components."""
    a = sorted((abs(s16(dx)), abs(s16(dy)), abs(s16(dz))))
    return a[2] + ((a[0] + a[1]) >> 2)


class Hang(Exception):
    pass


class VM:
    """One entity's think, script part. `mem` is modified in place."""

    def __init__(self, mem, e, nxt=None, rand=None, moved=(), choices=None):
        self.m, self.e = mem, e
        self.nxt = nxt                    # the next tick's dump (the oracle), or None
        self.rand_stream = rand           # list of rand() values to draw from, or None
        self.rand_used = 0
        self.moved = set(moved)           # entities that already moved this tick (before e)
        self.calls = []                   # [(channel, op, name, operands)]
        self.trace = []                   # [(channel, script, word, op)]
        self.oracle = []                  # [(op, var, value, computed)]
        self.unknown = []                 # world reads with no oracle
        self.writes = defaultdict(int)    # variable -> times written this tick
        self.write_log = []               # variables in the order written
        self.choices = choices or {}      # world call index -> value to use instead
        m = mem
        self.inst = m.u32(e + 0x100)
        self.pose_cache = m.s16(self.inst + 0x62)   # the pose last built (animation.md)
        self.model = m.u32(self.inst + 0x8)
        self.type = m.s32(e + 0x8)
        self.ch = 0
        self.ctx = e + 0x104
        self.yielded = 0

    # --- the world -----------------------------------------------------------------------------
    def pos(self, ent, i):
        """Position word i (0 x, 1 y, 2 z, 16.16) of an entity at the time of e's think."""
        if ent in self.moved and self.nxt is not None:
            return self.nxt.s32(ent + 0x20C + 4 * i)
        return self.m.s32(ent + 0x20C + 4 * i)

    def rand(self):
        if self.rand_stream is None:
            self.rand_used += 1
            return None
        if self.rand_used >= len(self.rand_stream):
            raise Hang("rand stream exhausted")
        v = self.rand_stream[self.rand_used]
        self.rand_used += 1
        return v

    def world(self, op, var, computed=None, cands=None):
        """A world-dependent result about to be written to variable `var`.

        Oracle: a value computed here (a distance) if there is one, else the variable's value in
        the next tick's dump, which is right when nothing writes the variable again this tick;
        `choices` (from the search in run_one) overrides either."""
        i = len(self.oracle)
        nxt = self.get_var(var, self.nxt) if self.nxt is not None else None
        if i in self.choices:
            v, src = self.choices[i], "searched"
        elif computed is not None:
            v, src = computed, "computed"
        elif nxt is not None:
            v, src = nxt, "next"
        else:
            self.unknown.append((op, var))
            v, src = 0, "unknown"
        if cands is None and computed is None:
            cands = [x for x in dict.fromkeys([nxt, -1, 0, 1]) if x is not None]
        self.oracle.append(dict(op=op, var=var, value=v, computed=computed, next=nxt, src=src,
                                cands=cands, at=len(self.write_log)))
        return v

    def last_write(self, o):
        """Whether the variable a world call wrote is not written again later this tick."""
        return o["var"] not in self.write_log[o["at"] + 1:]

    # --- variables (get_var 0x8005CBA8, set_var 0x8005C97C) -----------------------------------
    def get_var(self, vid, m=None):
        m = m or self.m
        e = self.e
        vid = s32(vid)
        if 0x60 <= vid < 0x80:
            return self.sensor(vid, m)
        if vid & 0x1C0:
            if vid & 0x100:
                n = vid ^ 0x100
                return int(bool(m.u32(e + 0xEC + 4 * (n >> 5)) & (1 << (n & 31))))
            if vid & 0x40:
                return int(bool(m.u32(e + 0xC) & (1 << (vid - 0x40))))
            if vid & 0x80:
                return int(bool(m.u32(e + 0x10) & (1 << (vid - 0x80))))
            return 0
        return m.s32(e + 0x4C + 4 * vid)

    def set_var(self, vid, value):
        m, e = self.m, self.e
        vid = s32(vid)
        self.writes[vid] += 1
        self.write_log.append(vid)
        if 0x60 <= vid < 0x80:
            if vid == 0x6A:
                self.calls.append((self.ch, 0x13, "set_var_6A", [value]))
            return
        if vid & 0x1C0:
            if vid & 0x100:
                n = vid & ~0x100
                a, bit = e + 0xEC + 4 * (n >> 5), 1 << (n & 31)
            elif vid & 0x40:
                a, bit = e + 0xC, 1 << (vid - 0x40)
            elif vid & 0x80:
                a, bit = e + 0x10, 1 << (vid - 0x80)
            else:
                return
            m.w32(a, (m.u32(a) | bit) if value else (m.u32(a) & ~bit))
            return
        m.w32(e + 0x4C + 4 * vid, value)

    def sensor(self, vid, m):
        e, p = self.e, m.u32(G_SOLDIER)
        if vid == 0x60:
            return m.u16(m.u32(m.u32(p + 0x100) + 8) + 0xC8)
        if vid == 0x61:
            return m.u32(p + 0x10) & 0x400
        if vid == 0x62:
            return e + 0x3A4
        if vid == 0x63:
            dx = s16((self.pos(p, 0) >> 16) - (self.pos(e, 0) >> 16))
            dz = s16((self.pos(p, 2) >> 16) - (self.pos(e, 2) >> 16))
            return int(dx * m.s16(e + 0x344) + dz * m.s16(e + 0x350) < 0)
        if vid == 0x64:
            return m.s32(e + 0x29C)
        if vid == 0x65:
            return m.u8(m.u32(p + 0x388) + 0x55)
        if vid == 0x66:
            return m.s16(p + 0x11E)
        if vid == 0x69:
            return m.s32(G_ALARM)
        if vid == 0x6B:
            return m.u32(p + 0x10) & 0x800
        if vid == 0x6C:
            return (m.u32(m.u32(p + 0x388)) >> 1) & 1
        if vid == 0x6D:
            n = 0
            for o in m.entities():
                if m.s32(o + 8) == SOLDIER and o != e and not m.u32(o + 0xC) & 0x20080000:
                    d = [s16((self.pos(e, i) - self.pos(o, i)) >> 16) for i in range(3)]
                    if dist_approx(*d) < 3000 and abs(d[1]) < 1000:
                        n += 1
            return n
        if vid == 0x6E:
            return G_WITNESS_SPOT
        if vid == 0x6F:
            return s16(self.pos(e, 1) >> 16) - s16(self.pos(p, 1) >> 16)
        if vid == 0x70:
            return m.s32(m.u32(m.u32(e + 0x100) + 8))
        if vid == 0x72:
            return m.s32(G_PARAMS)
        if vid == 0x73:
            return m.s32(0x800939F8)
        if vid == 0x74:
            d = [s16((self.pos(p, i) >> 16) - m.u16(e + 0x3A4 + 2 * i)) for i in range(3)]
            return dist_approx(*d)
        if vid == 0x68:   # its last sound still playing (audio.md: g_voices 0x8009115C, enabled 0x800910D0)
            v = m.s32(e + 0xFC)
            if not 1 <= v <= 23:
                return 0
            voice = 0x8009115C + 0x34 * v
            return int(m.u8(voice + 0xE) == 0 and m.u8(0x800910D0 + v) & 1 == 1
                       and m.u16(voice + 0x28) < 120 and not m.u32(voice) & 1)
        if vid in (0x67, 0x71):   # fire held; own voice playing; the player looks at it
            self.unknown.append(("sensor", vid))
            return 0
        return 0

    def operand(self, n, mask, value):
        return self.get_var(value) if mask & (1 << (n - 1)) else value

    # --- scripts --------------------------------------------------------------------------------
    def script_entry(self, idx):
        return self.m.u32(self.model + 0xB8) + 16 * idx

    def script_of(self, code):
        lst, n = self.m.u32(self.model + 0xB8), self.m.u16(self.model + 0xBC)
        for i in range(n):
            if self.m.u32(lst + 16 * i + 0xC) == code:
                return i
        return None

    def code_base(self, ctx):
        m = self.m
        d = m.s16(ctx + 0x1E)
        if d:
            return m.u32(ctx + 0x24 + 16 * (d - 1) + 8)
        return m.u32(m.u32(ctx + 0x10) + 0xC)

    def code_len(self, ctx):
        m = self.m
        d = m.s16(ctx + 0x1E)
        if d:
            return m.u16(ctx + 0x24 + 16 * (d - 1) + 0xC)
        return m.u16(m.u32(ctx + 0x10) + 0xA)

    def script_start(self, idx, force, ch):
        """`script_start` (0x8003F454): returns 1 if the channel now runs script idx."""
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
        for i in range(count):              # only each record's condition word
            m.w32(table + 0x3C * i, 0)
        if ch == 0:
            self.calls.append((ch, -1, "hit_part_reset", []))
            n = m.u32(self.model + 0xB0)
            src = m.u32(self.model + 0xB4)
            k = min(n, count) * 0x3C
            m.d[table & 0x1FFFFF:(table & 0x1FFFFF) + k] = m.d[src & 0x1FFFFF:(src & 0x1FFFFF) + k]
        m.w16(ctx + 0x18, -1)
        m.w16(ctx + 0x1C, 0)
        m.w16(ctx + 0x1E, 0)
        return 1

    # --- one instruction (script_step, 0x8005D18C) -------------------------------------------
    def step(self, pc, ch, ctx):
        self.ch, self.ctx, self.yielded = ch, ctx, 0
        m, e = self.m, self.e
        w = m.u32(pc)
        op, mask = w >> 24, (w >> 16) & 0xFF
        imm, imm_lo, imm_hi = w & 0xFFFF, w & 0xFF, (w >> 8) & 0xFF
        W = lambda k: m.s32(pc + 4 * k)                      # operand word k (1-based)
        H = lambda off: m.s16(pc + off)
        HU = lambda off: m.u16(pc + off)
        O = lambda n, v: self.operand(n, mask, v)
        sc = s16(m.u16(ctx + 0x1A))
        if m.s16(ctx + 0x1E) > 0:
            sc = self.script_of(self.code_base(ctx))
        self.trace.append((ch, sc, (pc - self.code_base(ctx)) // 4, op))
        call = lambda name, *a: self.calls.append((ch, op, name, list(a)))
        inst = self.inst + 0x5C
        if op == 0x00:                      # anim
            anim, mode = imm & 0xFFF, s16(imm) >> 12
            a, b = HU(4), H(6)
            m.w16(inst, anim)
            m.w16(inst + 2, a)
            m.w16(inst + 4, max(s16(a), 3))
            if b == -1:
                if m.u32(self.inst + 0x74):
                    b = m.u8(m.u32(m.u32(self.inst + 0x78) + 4 * anim)) - 1
                else:
                    b = m.u16(m.u32(self.model + 0x98) + 16 * anim + 4) - 1
            elif b == s16(a):
                b = -1
            m.w16(ctx + 0x18, b)
            if mode == 1:
                m.w16(self.inst + 0x3E, 4)
                m.w16(self.inst + 0x40, -1)
            elif mode == 2:
                m.w16(self.inst + 0x3E, 0)
            elif m.s16(self.inst + 0x3E) > 0:
                m.w16(self.inst + 0x3E, m.s16(self.inst + 0x3E) - 1)
            call("anim", anim, mode, s16(a), H(6))
            m.w16(ctx + 0x1C, 0)
            for k in (0x240, 0x244, 0x248):
                m.w32(e + k, 0)
            self.yielded = 1
            return pc + 8
        if op == 0x01:                      # mask
            v = O(1, W(1))
            words = [m.u32(ctx + 4 * k) for k in range(4)]
            if imm_lo == 0:
                words = [x & ~v for x in words]
            elif 1 <= imm_lo <= 4:
                words = [(x | v) if k == imm_lo - 1 else (x & ~v) for k, x in enumerate(words)]
            for k in range(4):
                m.w32(ctx + 4 * k, words[k])
            return pc + 8
        if op == 0x02:
            call("sound", O(1, imm_lo), *[O(k + 2, W(k + 1) & 0xFF) for k in range(5)], imm_hi)
            return pc + 0x18
        if op == 0x03:
            m.w32(e + 0x1FC, O(1, W(1)))
            return pc + 8
        if op == 0x04:
            if not w & 4:
                m.w32(e + 0x200, O(1, W(1)))
            if not w & 8:
                m.w32(e + 0x21C, O(2, W(2)))
            if w & 2:
                return pc + 0xC
            if not w & 0x10:
                m.w32(e + 0x218, O(3, W(3)))
            return pc + 0x10
        if op == 0x05:
            if not w & 2:
                return pc + 0x10
            m.w32(e + 0x21C, m.s32(e + 0x21C) + O(2, W(2)))
            v = m.s32(e + 0x200) + O(1, W(1))
            cap = m.s32(e + 0x204)
            if cap:
                v = min(v, cap)
            m.w32(e + 0x200, v)
            return pc + 0xC
        if op == 0x06:
            if not w & 2:
                return pc + 0x10
            d = O(1, W(1))
            if w & 1 and m.s32(e + 0x288) < 0:
                d = -d
            m.w32(e + 0x20C, m.s32(e + 0x20C) + d)
            m.w32(e + 0x210, m.s32(e + 0x210) + O(2, W(2)))
            return pc + 0xC
        if op in (0x07, 0x26):
            npc = self.code_base(ctx) + imm * 4
            m.w32(ctx + 0x14, npc)
            return npc
        if op in (0x08, 0x1D):              # restart (not forced) / jump_script (forced)
            if self.script_start(O(1, imm), op == 0x1D, ch):
                return m.u32(ctx + 0x14)
            return pc + 4
        if op in (0x09, 0x0B):
            raise Hang(f"op {op:02x} returns its own pc")
        if op == 0x0A:
            m.w32(e + 0x22C, m.s32(e + 0x22C) + 0x08000000)
            m.w32(e + 0x28C, m.u32(e + 0x288))
            return pc + 4
        if op == 0x0C:
            a2 = -1 if w & 2 else (O(2, W(2)) if w & 1 else 0)
            a3 = -1 if w & 8 else (O(3, W(3)) if w & 4 else 0)
            a4 = -1 if w & 0x20 else (O(4, W(4)) if w & 0x10 else 0)
            call("hit_part", O(1, W(1)), a2, a3, a4)
            return pc + 0x14
        if op in (0x0D, 0x0E, 0x42):
            return self.sop_event(pc, op)
        if op == 0x0F:
            return pc + 4
        if op in (0x10, 0x11):
            a = e + (0x10 if imm_lo else 0xC)
            v = O(1, W(1))
            m.w32(a, (m.u32(a) | v) if op == 0x10 else (m.u32(a) & ~v))
            return pc + 8
        if op == 0x12:
            call("leave_ground")
            return pc + 4
        if op == 0x13:
            self.set_var(O(1, imm), O(2, W(1)))
            return pc + 8
        if op == 0x14:
            idx = s16(O(1, W(1)))
            m.w16(self.script_entry(idx) + 4, O(2, imm))
            return pc + 8
        if op == 0x15:
            call("spawn", O(1, imm_lo))
            return pc + 8
        if op == 0x16:
            m.w16(ctx + 0x1C, O(1, imm) - 1)
            self.yielded = 1
            return pc + 4
        if op == 0x17:
            call("remove_self")
            self.yielded = 1
            return pc + 4
        if op in (0x18, 0x19):
            base = e + (0x2A4 if op == 0x18 else 0x2AC) + 16 * s16(O(1, imm))
            for k in range(3):
                m.w16(base + 2 * k, O(k + 2, W(k + 1)))
            return pc + 0x10
        if op == 0x1A:
            for k in range(3):
                m.w8(e + 0x2A0 + k, O(k + 1, W(k + 1)))
            return pc + 0x10
        if op == 0x1B:
            call("op1b")
            return pc + 4
        if op == 0x1C:
            call("model_variant", O(1, imm_lo))
            return pc + 4
        if op == 0x1E:                      # random: chance (imm high), var (imm low), min, max
            r = self.rand()
            chance = O(4, imm_hi)
            var = O(1, imm_lo)
            lo, hi = O(2, H(4)), O(3, H(6))
            if r is None:
                v = self.world(op, var, cands=[-1] + list(range(lo, min(hi, lo + 1100) + 1)))
                if v != -1:
                    self.rand_used += 1
                self.set_var(var, v)
                return pc + 8
            if chance < cmod(r, 100) and chance != 0:
                self.set_var(var, -1)
            else:
                self.set_var(var, lo + cmod(self.rand(), hi - lo + 1))
            return pc + 8
        if op == 0x1F:
            call("property", imm_lo, O(1, W(1)), O(2, W(2)), O(3, imm_hi), O(4, W(3)))
            return pc + 0x10
        if op == 0x20:
            call("clear_input")
            return pc + 4
        if op == 0x21:                      # random_timer
            r = self.rand()
            var = O(1, imm_lo)
            div = (O(4, imm_hi) * 0x1E0) & 0xFFFFFFFF
            lo, hi = O(2, W(1)), O(3, W(2))
            if r is None:
                v = self.world(op, var, cands=[-1] + list(range(lo, min(hi, lo + 1100) + 1)))
                if v != -1:
                    self.rand_used += 1
                self.set_var(var, v)
                return pc + 0xC
            if (r % div if div else r) < 16:
                self.set_var(var, lo + cmod(self.rand(), hi - lo + 1))
            else:
                self.set_var(var, -1)
            return pc + 0xC
        if op in (0x22, 0x23, 0x24, 0x25):  # angular velocity +0x234 / angles +0x228: set, add
            base = e + (0x234 if op in (0x22, 0x23) else 0x228)
            for k in range(3):
                v = O(k + 1, W(k + 1))
                if op in (0x23, 0x25):
                    v += m.s32(base + 4 * k)
                m.w32(base + 4 * k, v)
            return pc + 0x10
        if op == 0x27:
            a, b = O(1, W(1)), O(3, W(2))
            t, f = HU(0xC), HU(0xE)
            cmp = s16(imm)
            if cmp == 1:
                j = t if a == b else f
            elif cmp == 2:
                j = t if b < a else f
            elif cmp == 3:
                j = t if a < b else f
            elif cmp == 4:
                j = f if a == b else t
            else:
                raise Hang("if with an unknown comparison")
            return self.code_base(ctx) + 4 * j
        if op in (0x28, 0x29, 0x2A):
            var, v = O(1, W(1)), O(2, W(2))
            old = self.get_var(var)
            self.set_var(var, (old ^ v) if op == 0x28 else (old | v) if op == 0x29 else (old & v))
            return pc + 0xC
        if op == 0x2B:
            return pc + 0xC
        if op in (0x2C, 0x2D, 0x30):        # raw word at +0x4C + 4*id
            var, v = O(1, W(1)), O(2, W(2))
            a = e + 0x4C + 4 * var
            old = m.s32(a)
            m.w32(a, old + v if op == 0x2C else old - v if op == 0x2D else old * v)
            self.writes[var] += 1
            self.write_log.append(var)
            return pc + 0xC
        if op == 0x2E:
            return self.op_query(pc, mask, w)
        if op == 0x2F:
            call("behaviour", O(1, W(1)), O(2, W(2)), O(3, H(0xC)), O(4, H(0xE)), O(5, HU(0)))
            return pc + 0x10
        if op == 0x31:
            return 0
        if op == 0x32:                      # nearest live entity of a type within 1000 vertically
            var = O(1, W(1))
            self.set_var(var, self.nearest(O(2, W(2))))
            return pc + 0xC
        if op == 0x33:
            var = O(1, W(1))
            call("find_node", *[O(k + 2, W(k + 2)) for k in range(4)])
            self.set_var(var, self.world(op, var))
            return pc + 0x18
        if op == 0x34:
            node, var = O(1, W(1)), O(2, W(2))
            world = m.u32(m.u32(G_WORLD) + 0x388)
            if node & 0xFFFFFFFF >= m.u32(world + 0xC):
                self.set_var(var, -1)
            elif 1 <= imm_lo <= 5:
                self.set_var(var, m.s32(m.u32(world + 8) + 0x34 * node + 4 * (imm_lo - 1)))
            elif imm_lo in (6, 7):
                self.set_var(var, self.world(op, var))
            return pc + 0xC
        if op == 0x35:
            call("node_write", imm_lo, O(1, W(1)), O(2, W(2)))
            return pc + 0xC
        if op == 0x36:
            node, var, how = O(1, W(1)), O(2, W(2)), O(3, W(3))
            call("node_take", node, how)
            if how == 0 or how & 6:
                self.set_var(var, self.world(op, var))
                m.w32(e + 0x22C, self.get_var(var))
            return pc + 0x10
        if op == 0x37:
            v1, v2 = O(4, W(4)), O(5, W(5))
            call("route", O(1, W(1)), O(2, W(2)), O(3, W(3)))
            a, b = self.world(op, v1), self.world(op, v2)
            self.set_var(v1, a)
            self.set_var(v2, b)
            return pc + 0x18
        if op == 0x38:                      # call
            d = m.s16(ctx + 0x1E) + 1
            m.w16(ctx + 0x1E, d)
            if d >= 6:
                m.w16(ctx + 0x1E, 5)
                call("call_overflow")
                return pc + 4
            s = self.script_entry(s16(O(1, imm)))
            f = ctx + 0x14 + 16 * d
            m.w32(f, pc + 4)
            m.w32(f + 4, m.u32(m.u32(ctx + 0x10) + 0xC))
            m.w32(f + 8, m.u32(s + 0xC))
            m.w16(f + 0xC, m.u16(s + 0xA))
            m.w32(ctx + 0x14, m.u32(s + 0xC))
            return m.u32(s + 0xC)
        if op == 0x39:                      # return
            d = m.s16(ctx + 0x1E) - 1
            m.w16(ctx + 0x1E, d)
            if d >= 0:
                r = m.u32(ctx + 0x24 + 16 * d)
                m.w32(ctx + 0x14, r)
                return r
            m.w16(ctx + 0x1E, 0)
            return pc + 4
        if op == 0x3A:
            v1, v2 = O(2, W(2)), O(3, W(3))
            call("route_node", O(1, W(1)))
            a, b = self.world(op, v1), self.world(op, v2)
            self.set_var(v1, a)
            self.set_var(v2, b)
            return pc + 0x10
        if op == 0x3B:
            if m.s16(e + 0x192) != s16(imm):
                self.script_start(O(1, s16(imm)), 0, 1)
            return pc + 4
        if op == 0x3C:
            m.w16(e + 0x192, -1)
            m.w16(e + 0x190, -1)
            m.w32(e + 0x188, 0)
            return pc + 4
        if op == 0x3D:
            self.set_var(O(1, imm), m.s16(e + 0x192))
            return pc + 4
        if op == 0x3E:
            var = s16(O(1, imm_lo))
            call("probe", O(2, imm_hi), O(3, W(1)))
            self.set_var(var, self.world(op, var, cands=[0, s16(O(3, W(1))) + 1]))
            return pc + 8
        if op in (0x3F, 0x40):
            t = O(1, W(1))
            if t:
                a = (t & 0xFFFFFFFF) + (0x3C if op == 0x3F else 0x38)
                m.w32(a, m.u32(a) | O(2, W(2)))
            return pc + 0xC
        if op == 0x41:
            m.w32(self.inst + 0x6C, O(1, imm_lo))
            return pc + 4
        if op == 0x43:
            call("send_event", O(1, imm))
            return pc + 4
        if op == 0x44:
            if imm_hi < 2:
                m.w32(e + 0x20C, m.s16(e + 0x3A4) << 16)
                m.w32(e + 0x214, m.s16(e + 0x3A8) << 16)
            if imm_hi in (0, 2):
                m.w32(e + 0x22C, m.s16(e + 0x3AE) << 16)
                self.set_var(O(1, imm_lo), m.s32(e + 0x22C))
            return pc + 4
        if op == 0x45:
            var, anim = O(1, W(1)), O(2, W(2))
            m.w32(e + 0x4C + 4 * var, int(m.u32(m.u32(self.inst + 0x78) + 4 * anim) != 0))
            self.writes[var] += 1
            self.write_log.append(var)
            return pc + 0xC
        if op == 0x46:
            call("follow", O(1, W(1) & 0xFFFF), O(2, imm), O(3, W(2) & 0xFFFF),
                 O(4, (W(2) >> 16) & 0xFFF), O(5, (W(2) >> 28) & 0xF))
            var = O(4, (W(2) >> 16) & 0xFFF)
            self.set_var(var, self.world(op, var))
            return pc + 0xC
        if op == 0x47:
            t = m.u32(ctx + 0x20)
            for i in range(m.u32(e + 0x1EC)):
                m.w32(t + 0x3C * i, 0)
            return pc + 4
        if op == 0x48:
            call("run_message", O(1, imm), O(2, W(1)))
            return pc + 8
        if op == 0x49:
            call("alarm", *[O(k + 1, W(k + 1)) for k in range(7)])
            return pc + 0x20
        raise Hang(f"unknown op {op:02x}")

    def op_query(self, pc, mask, w):
        m, e = self.m, self.e
        fact = w & 0xFF
        O = lambda n, v: self.operand(n, mask, v)
        var = O(1, m.s32(pc + 4))
        typ = O(2, m.s32(pc + 8))
        grp = O(3, (w >> 8) & 0xFF)
        tgt = O(4, m.s32(pc + 12))
        tgt &= 0xFFFFFFFF
        grp = s32(grp << 24) >> 24
        if tgt == 0:
            if s32(typ) != -1:
                tgt = self.find_type_group(typ, grp) if grp != -1 else self.nearest(typ)
            elif grp == -1:
                tgt = e
        if fact in (1, 3, 4, 5, 6) and tgt:
            off = {1: 0xF4, 3: 0x2D4, 4: 0x290, 5: 0x44, 6: 0x48}[fact]
            self.set_var(var, m.s32(tgt + off))
        elif fact == 8 and tgt:
            self.set_var(var, tgt)
        elif fact == 8:
            self.set_var(var, self.world(0x2E, var))
        elif fact == 9 and tgt:
            d = dist_approx(*[(self.pos(e, i) - self.pos(tgt, i)) >> 16 for i in range(3)])
            self.set_var(var, self.world(0x2E9, var, d))
        elif fact == 0xD:
            self.set_var(var, e)
        elif fact == 0xE:
            self.set_var(var, m.s16(self.inst + 0x5C))
        elif fact == 0xA:
            if typ:
                d = dist_approx(*[(self.pos(e, i) - self.pos(typ & 0xFFFFFFFF, i)) >> 16 for i in range(3)])
                self.set_var(var, self.world(0x2E9, var, d))
        elif fact == 0xC and tgt:
            self.set_var(var, self.world(0x2EC, var, cands=self.angle_cands(tgt)))
        elif fact in (0xB, 0xF, 0x10, 0x11, 0x12):   # sight, aimed at, long sight, hearing
            self.set_var(var, self.world(0x2E00 | fact, var, cands=[0, 1]))
        elif fact in (1, 3, 4, 5, 6, 9, 0xC):      # no target: reads address 0's fields
            self.set_var(var, self.world(0x2E00 | fact, var))
        return pc + 0x10

    def find_type_group(self, typ, grp):
        m = self.m
        for o in m.entities():
            if m.s32(o + 8) == s32(typ) and s32(m.u8(o + 0x35) << 24) >> 24 == grp \
                    and not m.u32(o + 0xC) & 0x20080000:
                return o
        return 0

    def nearest(self, typ):
        """`0x80057768`: the nearest live entity of a type within 1000 vertically."""
        m, e = self.m, self.e
        best, bd = 0, 0x7FFF
        for o in m.entities():
            if m.s32(o + 8) == s32(typ) and o != e and not m.u32(o + 0xC) & 0x20080000:
                d = [s16((self.pos(e, i) - self.pos(o, i)) >> 16) for i in range(3)]
                dd = dist_approx(*d) & 0xFFFF
                if dd < bd and abs(d[1]) < 1000:
                    best, bd = o, dd
        return best

    def angle_cands(self, tgt):
        """|angle_to(e, tgt)|: ratan2 approximated by atan2, so a few candidates around it."""
        import math
        dx = s32(self.pos(tgt, 0) - self.pos(self.e, 0)) >> 16
        dz = s32(self.pos(tgt, 2) - self.pos(self.e, 2)) >> 16
        a = round(math.atan2(dx, dz) * 4096 / (2 * math.pi)) - self.m.s16(self.e + 0x22E)
        a = (a + 0x800) % 0x1000 - 0x800
        return sorted({abs(a + k) for k in range(-3, 4)})

    def sop_event(self, pc, op):
        """`sop_event` (0x80058C78): store {condition, instruction} in an event table."""
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
            self.calls.append((self.ch, op, "event_to_model_table", []))
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
        n = m.s32(STORED_LENGTH + 4 * stored)
        if slot >= 0:
            r = table + 0x3C * slot
            m.w32(r, 0 if stored == 0x0F else cond)
            for b, off in slots.items():
                m.w32(r + off, vals.get(b, 0))
            m.w32(r + 4, (w >> 8) & 0xFF)
            src = (pc + 4 * k) & 0x1FFFFF
            m.d[(r + 0x28) & 0x1FFFFF:((r + 0x28) & 0x1FFFFF) + 4 * n] = m.d[src:src + 4 * n]
        return pc + 4 * (k + n)

    # --- the two runners -----------------------------------------------------------------------
    def run_b(self):
        """`script_run_b` (0x8003FBFC): match each channel's event records."""
        m, e = self.m, self.e
        if m.s32(e + 0x44) > 0:
            m.w32(e + 0x38, m.u32(e + 0x38) & ~0x600)
            m.w32(e + 0x44, m.s32(e + 0x44) - 1)
        if m.s32(e + 0x48) > 0:
            m.w32(e + 0x38, m.u32(e + 0x38) & 0xFFE7FE1F)
            m.w32(e + 0x48, m.s32(e + 0x48) - 1)
        w38, w3c, w40 = m.u32(e + 0x38), m.u32(e + 0x3C), m.u32(e + 0x40)
        matched = False
        for ch in (0, 1):
            ctx = e + 0x104 + 0x74 * ch
            if not m.u32(ctx + 0x10):
                continue
            for i in range(m.u32(e + 0x1EC)):
                r = m.u32(ctx + 0x20) + 0x3C * i
                c = m.u32(r)
                if c == 0:
                    if matched:
                        break
                    continue
                if c & 3:
                    self.unknown.append(("pad event", c))
                    continue
                ok = True
                for bit, off, word, want_all in ((4, 0x10, w38, 1), (8, 0x14, w38, 0),
                                                 (0x10, 0x18, w3c, 1), (0x20, 0x1C, w3c, 0),
                                                 (0x40, 0x20, w40, 1), (0x80, 0x24, w40, 0)):
                    if c & bit:
                        v = m.u32(r + off)
                        if want_all and v & word != v or not want_all and v & word:
                            ok = False
                            break
                if not ok:
                    continue
                stored = m.u8(r + 0x2B)
                self.trace.append((ch, s16(m.u16(ctx + 0x1A)), -1 - i, 0x100 | stored))
                if stored == 0x26:
                    m.w16(ctx + 0x18, -1)
                    m.w16(ctx + 0x1C, 0)
                    m.w16(ctx + 0x1E, 0)
                    m.w32(ctx + 0x14, m.u32(m.u32(ctx + 0x10) + 0xC) + 4 * m.u16(r + 0x28))
                elif stored != 0x38:
                    self.step(r + 0x28, ch, ctx)
                matched = True
                break

    def run_a(self):
        """`script_run_a` (0x8003F8F0): step channels 0 and 1 until each yields."""
        m, e = self.m, self.e
        for ch in (0, 1):
            ctx = e + 0x104 + 0x74 * ch
            if not m.u32(ctx + 0x10):
                if ch == 0:
                    self.script_start(0, 0, 0)
                continue
            if m.s16(ctx + 0x1C) > 0:
                m.w16(ctx + 0x1C, m.s16(ctx + 0x1C) - 1)
                continue
            if m.s16(self.inst + 0x5E) < m.s16(ctx + 0x18):
                m.w16(self.inst + 0x5E, m.s16(self.inst + 0x5E) + 1)
                if m.s16(self.inst + 0x3E) > 0:
                    m.w16(self.inst + 0x3E, m.s16(self.inst + 0x3E) - 1)
                m.w16(ctx + 0x1C, 0)
                self.calls.append((ch, -1, "root_motion", []))
                continue
            m.w16(ctx + 0x18, -1)
            steps = 0
            while True:
                pc = m.u32(ctx + 0x14)
                if s32(pc - self.code_base(ctx)) >> 2 >= self.code_len(ctx):
                    m.w32(ctx + 0x10, 0)
                    m.w16(ctx + 0x1A, -1)
                    m.w16(self.inst + 0x5C, -1)
                    m.w16(self.inst + 0x5E, 0)
                    if ch != 0:
                        break
                    r = self.rand()
                    if r is None:
                        self.unknown.append(("idle pick", 0))
                        r = 0
                    roll = cdiv(r * 100, 32767)
                    lst, n = m.u32(self.model + 0xB8), m.u16(self.model + 0xBC)
                    for i in range(n):
                        if roll < m.u16(lst + 16 * i + 4):
                            self.script_start(i, 0, 0)
                            break
                    else:
                        self.script_start(0, 0, 0)
                    continue
                npc = self.step(pc, ch, ctx)
                m.w32(ctx + 0x14, npc)
                if self.yielded:
                    break
                steps += 1
                if steps > 5000:
                    raise Hang("no yield after 5000 instructions")

    def think(self):
        """The script part of the think: the heading in and out of its variable around run_a."""
        m, e = self.m, self.e
        hv = 0x13 if self.type == SOLDIER else 0x0E
        if self.type == SOLDIER and self.nxt is not None:
            # enemy_weapon runs first and writes the fire state back (2 -> 0 after the shot)
            m.w32(e + 0x4C + 4 * 0x16, self.nxt.u8(self.nxt.u32(e + 0x388) + 0x11))
        self.run_b()
        self.set_var(hv, m.s32(e + 0x22C))
        self.writes[hv] -= 1
        self.run_a()
        m.w32(e + 0x22C, self.get_var(hv))


# --- comparing with the next tick ------------------------------------------------------------------

def channel_state(m, e, ch):
    c = e + 0x104 + 0x74 * ch
    script = m.u32(c + 0x10)
    st = {"script": s16(m.u16(c + 0x1A)) if script else None}
    if script:
        d = m.s16(c + 0x1E)
        base = m.u32(c + 0x24 + 16 * (d - 1) + 8) if d > 0 else m.u32(script + 0xC)
        st.update(pc=s32(m.u32(c + 0x14) - base) // 4, wait_frame=m.s16(c + 0x18),
                  wait=m.s16(c + 0x1C), depth=d,
                  stack=[(m.u32(c + 0x24 + 16 * k), m.u32(c + 0x2C + 16 * k)) for k in range(max(d, 0))],
                  masks=tuple(m.u32(c + 4 * k) for k in range(4)))
    n = m.u32(e + 0x1EC)
    t = m.u32(c + 0x20)
    st["events"] = bytes(m.d[t & 0x1FFFFF:(t & 0x1FFFFF) + 0x3C * n]) if t else b""
    return st


def compare(sim, nxt, e, vm, exclude=()):
    """Differences between the simulated memory and the next dump for entity e."""
    diffs = []
    for v in range(0x28):
        if v in exclude:
            continue
        a, b = sim.s32(e + 0x4C + 4 * v), nxt.s32(e + 0x4C + 4 * v)
        if a != b:
            diffs.append(("var", v, a, b))
    for name, off in (("flagsA", 0xC), ("flagsB", 0x10), ("bits", 0xEC)):
        a, b = sim.u32(e + off), nxt.u32(e + off)
        if a != b:
            diffs.append((name, off, hex(a), hex(b)))
    for ch in (0, 1):
        sa, sb = channel_state(sim, e, ch), channel_state(nxt, e, ch)
        for k in sorted(set(sa) | set(sb)):
            if sa.get(k) != sb.get(k):
                if k == "events":
                    diffs.append((f"ch{ch}", "events", event_diff(sa[k], sb[k]), ""))
                else:
                    diffs.append((f"ch{ch}", k, sa.get(k), sb.get(k)))
    inst = sim.u32(e + 0x100)
    for name, off in (("anim", 0x5C), ("frame", 0x5E), ("frame_min3", 0x60), ("blend", 0x3E)):
        a, b = sim.s16(inst + off), nxt.s16(inst + off)
        if name == "blend" and a > 0 and vm.pose_cache == -1 and nxt.s16(inst + 0x62) != -1:
            a = 0   # pose_build (the draw) zeroes a cross-fade with no pose to fade from; an
                    # entity not drawn (its pose cache still -1 after the frame) keeps it
        if a != b:
            diffs.append(("inst", name, a, b))
    return diffs


def event_diff(a, b):
    out = []
    for i in range(0, min(len(a), len(b)), 0x3C):
        if a[i:i + 0x3C] != b[i:i + 0x3C]:
            out.append(f"#{i // 0x3C} {record_text(a[i:i + 0x3C])} != {record_text(b[i:i + 0x3C])}")
    return "; ".join(out)


def record_text(r):
    w = struct.unpack("<15I", r)
    if not w[0]:
        return "-"
    conds = " ".join(f"{n}={w[k]:X}" for bit, n, k in ((4, "s38", 4), (8, "!s38", 5), (0x10, "s3C", 6),
                                                       (0x20, "!s3C", 7), (0x40, "s40", 8), (0x80, "!s40", 9))
                     if w[0] & bit)
    return f"[{conds}: {w[10]:08x} {w[11]:08x}]"


def rand_stream(seed_a, seed_b, limit=200000):
    """The rand() values between two seeds (PsyQ rand, docs/re/objects.md)."""
    out, s = [], seed_a
    for _ in range(limit):
        if s == seed_b:
            return out
        s = (s * 0x41C64E6D + 12345) & 0xFFFFFFFF
        out.append((s >> 16) & 0x7FFF)
    return None


def step_pair(a_path, b_path, only=None, verbose=False, stats=None, log=print):
    A, B = Mem(open(a_path, "rb").read()), Mem(open(b_path, "rb").read())
    ta, tb = A.u32(G_TICK), B.u32(G_TICK)
    log(f"== {os.path.basename(a_path)} tick {ta} -> {os.path.basename(b_path)} tick {tb}")
    if tb != ta + 1:
        log("   not one tick apart: skipped")
        return
    stream = rand_stream(A.u32(RAND_SEED), B.u32(RAND_SEED))
    sim = Mem(A.d)
    ents_b = set(B.entities())
    moved = []
    for e in A.entities():
        typ = A.s32(e + 8)
        if typ in (SOLDIER, DOG) and e in ents_b and (only is None or e == only) \
                and A.u32(e + 0x100) and A.u32(e + 0x36C) in THINKS:
            run_one(A, B, e, moved, stream, stats, verbose, log)
        moved.append(e)


def simulate(A, B, e, moved, rand=None, choices=None):
    sim = Mem(A.d)
    vm = VM(sim, e, nxt=B, rand=rand, moved=moved, choices=choices)
    err = None
    try:
        vm.think()
    except Hang as x:
        err = str(x)
    return sim, vm, err


def excluded(B, e):
    """Variables written outside the scripts after the think: the brain's distance variable,
    the hit-reaction hook's variable (0x1C, the dog's 0x13), the hit handlers' 0x0C/0x0D/0x18."""
    exclude = {0x1C if B.s32(e + 8) == SOLDIER else 0x13}
    exclude |= {0x0C, 0x0D, 0x18}     # the hit handlers (collision pass): part, health %, hit by
    brain = B.u32(e + 0x38C)
    if brain:
        goals = B.u32(brain + 4)
        if goals & 0x800:
            exclude.add(B.s32(brain + 0x7C))
        if goals & 0x1000:
            exclude.add(B.s32(brain + 0x80))
    return exclude


def search(A, B, e, moved, exclude, rand=None, budget=3000):
    """Greedy search over the world values the oracle can't give (a variable written by a world
    query, then again later in the tick): try each candidate, keep the best."""
    choices = {}
    sim, vm, err = simulate(A, B, e, moved, rand, choices)
    best = len(compare(sim, B, e, vm, exclude)) + bool(err)
    runs, done = 0, set()
    while best and runs < budget:
        amb = [i for i, o in enumerate(vm.oracle) if o["cands"] and i not in done
               and i not in choices]
        amb.sort(key=lambda i: vm.last_write(vm.oracle[i]))
        if not amb:
            break
        i = amb[0]
        done.add(i)
        pick = None
        for c in vm.oracle[i]["cands"]:
            trial = dict(choices)
            trial[i] = c
            s2, v2, e2 = simulate(A, B, e, moved, rand, trial)
            runs += 1
            n = len(compare(s2, B, e, v2, exclude)) + bool(e2)
            if n < best:
                best, pick = n, c
                if not n:
                    break
        if pick is not None:
            choices[i] = pick
            sim, vm, err = simulate(A, B, e, moved, rand, choices)
    return sim, vm, err, choices


def run_one(A, B, e, moved, stream, stats, verbose, log):
    typ = A.s32(e + 8)
    exclude = excluded(B, e)
    sim, vm, err = simulate(A, B, e, moved)
    diffs = compare(sim, B, e, vm, exclude)
    searched, used_k, rand_note = {}, None, ""
    if diffs or err:
        best = None
        ks = [None] + (list(range(len(stream))) if stream is not None else [])
        for k in ks:
            r = stream[k:] if k is not None else None
            s2, v2, e2, ch2 = search(A, B, e, moved, exclude, r, budget=400)
            d2 = compare(s2, B, e, v2, exclude)
            n = len(d2) + bool(e2)
            if best is None or n < best[0]:
                best = (n, s2, v2, e2, ch2, d2, k)
            if not n:
                break
        if best[0] and stream is not None:
            # one rand() drawn outside the scripts in the middle of the think: skip stream[k + j]
            for k in range(len(stream)):
                for j in range(1, 12):
                    if k + j >= len(stream):
                        break
                    r = stream[k:k + j] + stream[k + j + 1:]
                    s2, v2, e2, ch2 = search(A, B, e, moved, exclude, r, budget=200)
                    d2 = compare(s2, B, e, v2, exclude)
                    if not d2 and not e2:
                        best = (0, s2, v2, e2, ch2, d2, f"{k} skipping draw {j}")
                        break
                if not best[0]:
                    break
        _, sim, vm, err, searched, diffs, used_k = best
    if vm.rand_used:
        rand_note = f" rand: {vm.rand_used} draws" + (f", from stream[{used_k}] of {len(stream)}"
                                                      if used_k is not None else "")
        if stats is not None and used_k is not None:
            stats["rand"][0] += 1
            stats["rand"][1] += not diffs
    if searched:
        rand_note += f" searched {searched}"
    ok = not diffs and not err
    st = [channel_state(sim, e, ch) for ch in (0, 1)]
    head = f"  {e:08X} {typ:X} " + " | ".join(
        f"ch{ch} {s['script']}:{s.get('pc')} w{s.get('wait')} f{s.get('wait_frame')} d{s.get('depth')}"
        for ch, s in enumerate(st))
    log(head + ("  OK" if ok else "  DIFF") + rand_note)
    if verbose or not ok:
        if err:
            log(f"     error: {err}")
        for d in diffs:
            log(f"     {d}")
        for ch, sc, word, op in vm.trace:
            log(f"       trace ch{ch} {sc}:{word} {NAMES.get(op & 0xFF, hex(op))}{' (event)' if op & 0x100 else ''}")
        for c in vm.calls:
            log(f"       call ch{c[0]} {c[2]} {c[3]}")
        for o in vm.oracle:
            log(f"       oracle op {o['op']:x} v{o['var']:X} = {o['value']} ({o['src']}; computed "
                f"{o['computed']}, next {o['next']}{', last' if vm.last_write(o) else ''})")
        for u in vm.unknown:
            log(f"       unknown {u}")
    if stats is not None:
        ops = {op & 0xFF for _, _, _, op in vm.trace}
        for op in ops:
            stats[op][0] += 1
            stats[op][1] += ok
        stats["ents"][0] += 1
        stats["ents"][1] += ok
        for o in vm.oracle:
            stats[f"world {o['op']:x} {o['src']}"][0] += 1
            stats[f"world {o['op']:x} {o['src']}"][1] += ok
            if o["op"] == 0x2E9 and vm.last_write(o) and o["var"] not in exclude:
                stats["dist"][0] += 1
                stats["dist"][1] += o["computed"] == o["next"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dumps", nargs="*")
    ap.add_argument("--pairs", nargs="*", type=int)
    ap.add_argument("--dir", default="build/vm")
    ap.add_argument("--ent", type=lambda x: int(x, 16))
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    stats = defaultdict(lambda: [0, 0])
    if a.pairs:
        for v in a.pairs:
            pa, pb = f"{a.dir}/d{v}.bin", f"{a.dir}/d{v + 1}.bin"
            if os.path.exists(pa) and os.path.exists(pb):
                step_pair(pa, pb, a.ent, a.v, stats)
    else:
        step_pair(a.dumps[0], a.dumps[1], a.ent, a.v, stats)
    print("\nper opcode: entity-ticks that ran it, of which fully agreed")
    for op in sorted(k for k in stats if isinstance(k, int)):
        n, ok = stats[op]
        print(f"  {op:02X} {NAMES.get(op, '?'):14s} {ok:4d}/{n:<4d}")
    for k in ("ents", "dist", "rand"):
        if k in stats:
            print(f"  {k}: {stats[k][1]}/{stats[k][0]}")
    print("world values (op, source): uses, of which in agreeing entity-ticks")
    for k in sorted(k for k in stats if isinstance(k, str) and k.startswith("world")):
        print(f"  {k[6:]:22s} {stats[k][1]:4d}/{stats[k][0]}")


if __name__ == "__main__":
    main()
