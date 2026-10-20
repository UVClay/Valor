#!/usr/bin/env python3
"""Disassembles the behaviour/animation scripts of an entity's model (docs/re/enemies.md).

  sst.py RAMDUMP ENTITY [SCRIPT...]   scripts of the entity at ENTITY (hex) in a RAM dump
  sst.py RAMDUMP --type 1402          the first entity of that type

Scripts live in the model data: model = [[entity+0x100]+0x8], its list at +0xB8 (count +0xBC),
16 bytes per script: +0x4 weight (random pick), +0xA length (words), +0xC code.
An instruction is [opcode:8][mask:8][imm:16] then operand words; mask bit n-1 set: operand n is a
variable (docs/re/enemies.md, Variables), else a literal.
"""
import struct
import sys

# Instruction length in words: an int, or a function of the first word.
def _flags_len(skip_bit):
    return lambda w: 3 if w & skip_bit else 4

LENGTH = {
    0x00: 2, 0x01: 2, 0x02: 6, 0x03: 2, 0x04: _flags_len(0x2), 0x05: _flags_len(0x2),
    0x06: _flags_len(0x2), 0x07: 1, 0x08: 1, 0x09: 1, 0x0A: 1, 0x0B: 1, 0x0C: 5, 0x0F: 1,
    0x10: 2, 0x11: 2, 0x12: 1, 0x13: 2, 0x14: 2, 0x15: 2, 0x16: 1, 0x17: 1, 0x18: 4, 0x19: 4,
    0x1A: 4, 0x1B: 1, 0x1C: 1, 0x1D: 1, 0x1E: 2, 0x1F: 4, 0x20: 1, 0x21: 3, 0x22: 4, 0x23: 4,
    0x24: 4, 0x25: 4, 0x26: 1, 0x27: 4, 0x28: 3, 0x29: 3, 0x2A: 3, 0x2B: 3, 0x2C: 3, 0x2D: 3,
    0x2E: 4, 0x2F: 4, 0x30: 3, 0x31: 1, 0x32: 3, 0x33: 6, 0x34: 3, 0x35: 3, 0x36: 4, 0x37: 6,
    0x38: 1, 0x39: 1, 0x3A: 4, 0x3B: 1, 0x3C: 1, 0x3D: 1, 0x3E: 2, 0x3F: 3, 0x40: 3, 0x41: 1,
    0x43: 1, 0x44: 1, 0x45: 3, 0x46: 3, 0x47: 1, 0x48: 2, 0x49: 8,
}
# Event records (0x0D, 0x0E, 0x42): 1 + one word per set bit of the mask's low byte, then a
# stored instruction whose length comes from this table (LEVEL.BIN 0x80090EA0) by its opcode.
STORED_LENGTH = [2, 2, 5, 2, 3, 3, 3, 1, 1, 1, 1, 1, 5, 7, 7, 1, 2, 2, 1, 2, 1, 2, 1, 1, 4, 4, 4,
                 1, 1, 1, 2, 4]

NAMES = {
    0x00: "anim", 0x01: "mask", 0x02: "sound", 0x03: "gravity", 0x04: "fall", 0x05: "impulse",
    0x06: "nudge", 0x07: "goto", 0x08: "other_script", 0x09: "stop", 0x0A: "op0a", 0x0B: "stop",
    0x0C: "hit_part", 0x0D: "event", 0x0E: "event", 0x0F: "nop", 0x10: "flags_set",
    0x11: "flags_clear", 0x12: "op12", 0x13: "set", 0x14: "weight", 0x15: "remove_handle",
    0x16: "wait", 0x17: "remove_self", 0x18: "vec_a", 0x19: "vec_b", 0x1A: "bytes",
    0x1B: "op1b", 0x1C: "model_variant", 0x1D: "jump_script", 0x1E: "random_chance",
    0x1F: "property", 0x20: "clear_input", 0x21: "random_timer", 0x22: "accel",
    0x23: "accel_add", 0x24: "velocity", 0x25: "velocity_add", 0x26: "goto", 0x27: "if",
    0x28: "var_op28", 0x29: "var_op29", 0x2A: "var_op2a", 0x2B: "nop3", 0x2C: "add",
    0x2D: "sub", 0x2E: "query", 0x2F: "behaviour", 0x30: "mul", 0x31: "end", 0x32: "lookup",
    0x33: "compute", 0x34: "node_eval", 0x35: "node_write", 0x36: "node_take", 0x37: "route",
    0x38: "call", 0x39: "return", 0x3A: "route_node", 0x3B: "start_script",
    0x3C: "clear_script", 0x3D: "current_script", 0x3E: "lookup3e", 0x3F: "or_flags3c",
    0x40: "or_flags38", 0x41: "model_6c", 0x42: "event", 0x43: "send_event", 0x44: "to_spawn",
    0x45: "has_anim", 0x46: "follow", 0x47: "clear_events", 0x48: "decal", 0x49: "alarm",
}
CMP = {1: "==", 2: ">", 3: "<", 4: "!="}


class Ram:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x1FFFFF)[0]


# Variable names (docs/re/enemies.md, Variables): sensors and the known word variables.
VARS = {
    0x05: "hear_range", 0x06: "hear_half", 0x07: "hear_third", 0x08: "skill", 0x0B: "v0B",
    0x0C: "part_hit", 0x0D: "health_pct", 0x0E: "roll", 0x0F: "weapon_kind", 0x10: "v10",
    0x13: "vspeed", 0x16: "fire", 0x18: "hit_by", 0x1C: "hit_react", 0x22: "v22", 0x24: "v24",
    0x26: "v26",
    0x60: "player_model", 0x61: "player_b400", 0x62: "&home", 0x63: "player_behind",
    0x64: "e29C", 0x65: "player_weapon", 0x66: "player_11E", 0x67: "player_firing",
    0x68: "node_usable", 0x69: "alarm", 0x6B: "player_b800", 0x6C: "player_crouched?",
    0x6D: "allies_near", 0x6E: "&8009817C", 0x6F: "height_over_player", 0x70: "my_model",
    0x71: "player_sees_me", 0x72: "params", 0x73: "v73", 0x74: "home_to_player",
}


def var(value):
    if value in VARS:
        return VARS[value]
    if 0x40 <= value < 0x60:
        return f"flagA.{value - 0x40}"
    if 0x80 <= value < 0xA0:
        return f"flagB.{value - 0x80}"
    if value >= 0x100:
        return f"bit{value - 0x100}"
    return f"v{value:X}"


def operand(mask, n, value):
    return var(value) if mask & (1 << (n - 1)) else str(value if value < 0x80000000 else value - (1 << 32))


def length(op, words, i):
    if op in (0x0D, 0x0E, 0x42):
        w = words[i]
        n = 1 + bin(w & 0xFF).count("1")
        if i + n < len(words):
            stored = words[i + n] >> 24
            return n + (STORED_LENGTH[stored] if stored < len(STORED_LENGTH) else 1)
        return n
    spec = LENGTH.get(op, 1)
    return spec(words[i]) if callable(spec) else spec


def disassemble(words):
    out = []
    i = 0
    while i < len(words):
        w = words[i]
        op, mask, imm = w >> 24, (w >> 16) & 0xFF, w & 0xFFFF
        n = length(op, words, i)
        args = words[i + 1:i + n]
        name = NAMES.get(op, f"op{op:02x}")
        if op == 0x27:
            a, b, t = args[0], args[1], args[2]
            text = f"if {operand(mask, 1, a)} {CMP.get(imm, '?')} {operand(mask, 3, b)} goto {t & 0xFFFF} else {t >> 16}"
        elif op in (0x07, 0x26):
            text = f"goto {imm}"
        elif op in (0x38, 0x1D, 0x08, 0x3B):
            text = f"{name} {operand(mask, 1, imm)}"
        elif op == 0x13:
            text = f"set {operand(mask, 1, imm)} = {operand(mask, 2, args[0])}"
        elif op == 0x16:
            text = f"wait {operand(mask, 1, imm)}"
        elif op == 0x00:
            text = f"anim {imm & 0xFFF} mode {imm >> 12} frames {args[0] & 0xFFFF}..{struct.unpack('<h', struct.pack('<H', args[0] >> 16))[0]}"
        else:
            text = f"{name}.{imm:X} " + " ".join(operand(mask, k + 1, a) for k, a in enumerate(args))
        out.append(f"  {i:4d}: " + " ".join(f"{x:08x}" for x in words[i:i + n]).ljust(40) + "  " + text)
        i += max(n, 1)
    return out


def main():
    ram = Ram(sys.argv[1])
    if sys.argv[2] == "--type":
        want = int(sys.argv[3], 16)
        e = ram.u32(0x8009D64C)
        while e and ram.u32(e + 8) != want:
            e = ram.u32(e + 4)
        rest = sys.argv[4:]
    else:
        e = int(sys.argv[2], 16)
        rest = sys.argv[3:]
    model = ram.u32(ram.u32(e + 0x100) + 0x8)
    lst, count = ram.u32(model + 0xB8), ram.u16(model + 0xBC)
    print(f"entity {e:08X} type {ram.u32(e + 8):X}: model {model:08X}, {count} scripts")
    which = [int(x, 0) for x in rest] or range(count)
    for k in which:
        s = lst + 16 * k
        weight, n, code = ram.s16(s + 4), ram.u16(s + 0xA), ram.u32(s + 0xC)
        print(f"script {k} (weight {weight}, {n} words at {code:08X})")
        words = [ram.u32(code + 4 * j) for j in range(n)]
        print("\n".join(disassemble(words)))


if __name__ == "__main__":
    main()
