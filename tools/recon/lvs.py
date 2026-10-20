#!/usr/bin/env python3
"""Disassembles a level's scripts (docs/re/levels.md, Level logic) from a RAM dump.

  lvs.py [--raw] RAMDUMP            every script
  lvs.py [--raw] RAMDUMP ID...      those scripts (the id an entity record holds, +0x64 or +0x78)

By default the stack code is folded into statements (calls with their arguments, conditions);
--raw lists the instructions one per line.

The table (`[0x8009D5AC]`): +0x0 count, +0x4 slots, +0x8 + 4*id the script's offset from the
table (0: none). A script: u32 N, N handlers {u16 message, u16 code offset}, then its byte
variables (byte 0: the once-flag); code offsets and jump targets are from the script's start,
2- and 4-byte operands aligned from there too.
"""
import struct
import sys

# Built-ins: name, values popped (None: register/unregister, by the filter), values pushed.
BUILTINS = {
    0: ("register_msg", None, 0), 1: ("unregister_msg", None, 0), 2: ("send_self", 1, 0),
    3: ("set_flags", 1, 0), 4: ("activate", 2, 0), 5: ("remove_entity", 1, 0),
    6: ("explosion", 1, 0), 7: ("level_func", 3, 1), 8: ("show_text", 1, 0), 9: ("has_tag", 3, 1),
    10: ("tags_match", 5, 1), 11: ("set_home", 2, 0), 12: ("send_to", 3, 0),
    13: ("level_set", 2, 0), 14: ("objective_add", 2, 0), 15: ("objective_remove", 1, 0),
    16: ("objective_complete", 1, 0), 17: ("objective_is_complete", 1, 1),
    18: ("world_anim_step", 1, 0), 19: ("sound", 7, None), 20: ("sound_stop", 1, 0),
    21: ("world_anim_set", 2, 0),
}
# register_msg/unregister_msg pop the handler index and the filter kind, then by the filter:
FILTER_ARGS = {1: 1, 2: 1, 3: 2, 4: 2, 5: 3}
BINOPS = {4: "==", 5: "&&", 6: "||", 8: ">", 9: "<", 0xA: ">=", 0xB: "<=",
          0xC: "+", 0xD: "-", 0xE: "*", 0xF: "/"}


def source(s):
    """Sources of push 02: 0xFA..0xFF the entity of the context's handle +0x4 (self), +0x8, +0xC
    by (S - 0xF4) % 3; 0xF4..0xF9 the context's objects +0x10, +0x14 (or none)."""
    k = (s - 0xF4) % 3
    if s >= 0xFA:
        return ("self", "other", "third")[k]
    return ("obj", "obj2", "none")[k]


class Ram:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def u8(self, a):
        return self.d[a & 0x1FFFFF]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x1FFFFF)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFF)[0]


def align(pos, n):
    return pos + (-pos % n)


def var_name(scope, sel, idx):
    where = "ent" if scope in (0xF0, 0xF1) else "local"
    if sel == 9:
        return f"{where}[{idx}]"
    if 1 <= sel <= 8:
        return f"{where}[{idx}].bit{sel - 1}"
    return f"{where}[{idx}]?sel{sel}"


def decode(ram, base, start, end):
    """(offset, kind, value) for the code in [start, end) of the script at base."""
    out = []
    pos = start
    while pos < end:
        at = pos
        op = ram.u8(base + pos)
        pos += 1

        def var():
            nonlocal pos
            s = ram.u8(base + pos)
            sel = ram.u8(base + pos + 1)
            pos += 2
            if s in (0xF1, 0xF3):
                pos = align(pos, 2)
                idx = ram.u16(base + pos)
                pos += 2
            else:
                idx = ram.u8(base + pos)
                pos += 1
            return var_name(s, sel, idx)

        if op == 0x00:
            ins = ("nop", None)
        elif op == 0x01:
            ins = ("pop", None)
        elif op == 0x02:
            s = ram.u8(base + pos)
            if 0xF0 <= s <= 0xF3:
                ins = ("push", var())
            else:
                pos += 1
                if s >= 0xF4:
                    t = ram.u8(base + pos)
                    pos += 1
                    if 0xF7 <= t <= 0xF9 or t >= 0xFD:
                        pos = align(pos, 2) + 2
                        x = None
                    else:
                        x = ram.u8(base + pos)
                        pos += 1
                    who = source(s)
                    ins = ("push", who if t == 0x10 else f"{who}.prop{x}" + (f":t{t:X}" if t else ""))
                elif s == 9:
                    ins = ("push", str(ram.u8(base + pos)))
                    pos += 1
                elif 0xA <= s <= 0xE or s == 0x10:
                    pos = align(pos, 4)
                    if s == 0xE:
                        v = struct.unpack("<f", struct.pack("<I", ram.u32(base + pos)))[0]
                        ins = ("push", f"{v:g}f")
                    elif s == 0x10:
                        ins = ("push", f"&data_{pos:X}")
                    else:
                        v = ram.s32(base + pos) if s == 0xA else ram.u32(base + pos)
                        ins = ("push", str(v) + ("" if s in (0xA, 0xB) else f":t{s:X}"))
                    pos += 4
                elif s == 0xF:
                    n = ram.u8(base + pos)
                    raw = ram.d[(base + pos + 1) & 0x1FFFFF:(base + pos + 1 + n) & 0x1FFFFF]
                    ins = ("push", '"' + raw.rstrip(bytes(1)).decode("latin-1") + '"')
                    pos += 1 + n
                else:
                    ins = ("push", f"?{s:02X}")
        elif op == 0x03:
            s = ram.u8(base + pos)
            if 0xF0 <= s <= 0xF3:
                ins = ("store", var())
            else:
                pos += 1
                ins = ("store", f"?{s:02X}")
        elif op in BINOPS:
            ins = ("binop", BINOPS[op])
        elif op == 0x07:
            ins = ("not", None)
        elif op in (0x10, 0x11):
            pos = align(pos, 2)
            ins = ("jz" if op == 0x10 else "jmp", ram.u16(base + pos))
            pos += 2
        elif op in (0x12, 0x13):
            if op == 0x12:
                n = ram.u8(base + pos)
                pos += 1
            else:
                pos = align(pos, 2)
                n = ram.u16(base + pos)
                pos += 2
            ins = ("call", n)
        elif op == 0x14:
            ins = ("end", None)
        elif op == 0x15:
            ins = ("push", "first_run")
        elif op == 0x16:
            ins = ("set_ran", None)
        elif op == 0x17:
            t = ram.u8(base + pos)
            pos += 1
            if t == 9:
                ins = ("print", ram.u8(base + pos))
                pos += 1
            elif t in (0xA, 0xB):
                pos = align(pos, 4)
                ins = ("print", ram.s32(base + pos))
                pos += 4
            else:
                ins = ("print", f"t{t:X}")
        else:
            ins = ("??", f"{op:02X}")
        out.append((at,) + ins)
    return out


def call_name(n):
    return BUILTINS[n][0] if n in BUILTINS else f"builtin{n}"


def raw_lines(code, entries, targets):
    out = []
    for at, kind, v in code:
        if at in entries:
            out.append(f"msg_{entries[at]:X}:")
        if at in targets:
            out.append(f"L{at:X}:")
        if kind == "binop":
            text = v
        elif kind == "call":
            text = f"call {call_name(v)}"
        elif kind in ("jz", "jmp"):
            text = f"{kind} L{v:X}"
        elif kind == "not":
            text = "!"
        else:
            text = kind if v is None else f"{kind} {v}"
        out.append(f"  {at:5X}: {text}")
    return out


def folded_lines(code, entries, targets):
    """Statements: the stack folded into expressions, flushed at labels."""
    out = []
    stack = []

    def flush():
        for e in stack:
            out.append(f"    push {e}")
        stack.clear()

    def take(n):
        if len(stack) >= n:
            args = stack[len(stack) - n:]
            del stack[len(stack) - n:]
            return args
        missing = n - len(stack)
        args = [f"<pop{i}>" for i in range(missing)] + stack[:]
        stack.clear()
        return args

    for at, kind, v in code:
        if at in entries or at in targets:
            flush()
            if at in entries:
                out.append(f"on msg {entries[at]:X}:  # {at:X}")
            if at in targets:
                out.append(f"  L{at:X}:")
        if kind == "push":
            stack.append(v)
        elif kind == "binop":
            a, b = take(2)
            stack.append(f"({a} {v} {b})")
        elif kind == "not":
            (a,) = take(1)
            stack.append(f"!{a}")
        elif kind == "store":
            (a,) = take(1)
            out.append(f"    {v} = {a}")
        elif kind == "pop":
            (a,) = take(1)
            out.append(f"    {a}")
        elif kind == "jz":
            (a,) = take(1)
            flush()
            out.append(f"    if !{a} goto L{v:X}")
        elif kind == "jmp":
            flush()
            out.append(f"    goto L{v:X}")
        elif kind == "call":
            name, pops, pushes = BUILTINS.get(v, (f"builtin{v}", 0, 0))
            if pops is None:
                filt = stack[-2] if len(stack) >= 2 else None
                pops = 2 + FILTER_ARGS.get(int(filt) if filt and filt.isdigit() else -1, 0)
            args = take(pops)
            expr = f"{name}({', '.join(args)})"
            if pushes is None:  # sound: returns a handle when the mode (last argument) is 3
                pushes = 1 if args and args[-1] == "3" else 0
            if pushes:
                stack.append(expr)
            else:
                out.append(f"    {expr}")
        elif kind == "end":
            flush()
            out.append("    end")
        elif kind == "set_ran":
            out.append("    set_ran")
        elif kind == "print":
            out.append(f"    print {v}")
        elif kind == "nop":
            pass
        else:
            flush()
            out.append(f"    {kind} {v}")
    flush()
    return out


def main():
    args = sys.argv[1:]
    raw = args[:1] == ["--raw"]
    if raw:
        args = args[1:]
    ram = Ram(args[0])
    table = ram.u32(0x8009D5AC)
    slots = ram.u32(table + 4)
    ids = [int(x, 0) for x in args[1:]] or range(slots)
    offs = sorted({ram.u32(table + 8 + 4 * i) for i in range(slots)} - {0})
    for sid in ids:
        off = ram.u32(table + 8 + 4 * sid)
        if not off:
            continue
        base = table + off
        nxt = next((o for o in offs if o > off), None)
        n = ram.u32(base)
        entries = {}
        for k in range(n):
            msg, code = ram.u16(base + 4 + 4 * k), ram.u16(base + 6 + 4 * k)
            entries.setdefault(code, msg)
        start = min(entries) if entries else 4 + 4 * n
        end = (nxt - off) if nxt else start
        nvars = start - (4 + 4 * n)
        handlers = ", ".join(f"{ram.u16(base + 4 + 4 * k):X}" for k in range(n))
        print(f"script {sid} at {base:08X}: handlers [{handlers}], {nvars} variable bytes")
        code = decode(ram, base, start, end)
        targets = {v for _, kind, v in code if kind in ("jz", "jmp")}
        print("\n".join((raw_lines if raw else folded_lines)(code, entries, targets)))
        print()


if __name__ == "__main__":
    main()
