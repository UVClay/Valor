#!/usr/bin/env python3
"""Disassembles a level's scripts (docs/re/levels.md, Level logic) from a RAM dump.

  lvs.py [--raw] RAMDUMP            every script
  lvs.py [--raw] RAMDUMP ID...      those scripts (a record's +0x54, or its entity's +0x64/+0x78)
  lvs.py --check RAMDUMP...         verify every script of every dump: known opcodes and
                                    built-ins, handlers and jump targets on instructions, the
                                    same stack depth on every path into an instruction, an
                                    empty stack at every `end`; one line per dump

By default the stack code is folded into statements (calls with their arguments, conditions);
--raw lists the instructions one per line.

The table (`[0x8009D5AC]`): +0x0 the number of message ids (158 in every level), +0x4 slots
(167), +0x8 + 4*id the script's offset from the table (0: none). A script: u32 N, N handlers
{u16 message, u16 code offset}, then its byte variables (byte 0: the once-flag); code offsets
and jump targets are from the script's start, 2- and 4-byte operands aligned from there too.

Operands of binary operators: the VM computes TOP op NEXT (the value pushed last on the left),
so they print as (last pushed  op  first pushed): `ent[0] - 1`, `ent[0] < 3`. Built-in calls
print their arguments in push order (the doc's built-in table uses the same order).

Sources (push 02 F4..FF): self = the context's entity (+0x4), sender = +0x8, actor = +0xC;
self_obj = +0x10 (the record pair the script runs for), sender_obj = +0x14; `null` (F6 with
kind 0x10) is always 0. A property (`.pN`) reads the record of the object if there is one,
else of the entity (levels.md, Record properties).
"""
import struct
import sys

# Built-ins: name, values popped (None: register/unregister, by the filter), values pushed.
BUILTINS = {
    0: ("register_msg", None, 0), 1: ("unregister_msg", None, 0), 2: ("send", 1, 0),
    3: ("set_flags", 1, 0), 4: ("activate", 2, 0), 5: ("remove_entity", 1, 0),
    6: ("explosion", 1, 0), 7: ("entity_test", 3, 1), 8: ("show_text", 1, 0), 9: ("has_tag", 3, 1),
    10: ("tags_match", 5, 1), 11: ("set_home", 2, 0), 12: ("send_to", 3, 0),
    13: ("level_set", 2, 0), 14: ("objective_add", 2, 0), 15: ("objective_remove", 1, 0),
    16: ("objective_complete", 1, 0), 17: ("objective_is_complete", 1, 1),
    18: ("world_anim_step", 1, 0), 19: ("sound", 7, None), 20: ("sound_stop", 1, 0),
    21: ("world_anim_set", 2, 0),
}
# register_msg/unregister_msg pop the handler index and the filter, then by the filter:
FILTER_ARGS = {0: 0, 1: 1, 2: 1, 3: 2, 4: 2, 5: 3}
# Binary operators: TOP op NEXT.
BINOPS = {4: "==", 5: "&&", 6: "||", 8: "<", 9: ">", 0xA: "<=", 0xB: ">=",
          0xC: "+", 0xD: "-", 0xE: "*", 0xF: "/"}
WIDE = set(range(0xF7, 0xFA)) | set(range(0xFD, 0x100))


def source(s, t):
    """What push 02 S T pushes from. The VM picks the wide form by T (not S): T in F7..F9 or
    FD..FF reads a 16-bit index through a pointer made from one byte (broken; unused)."""
    if t in WIDE:  # the entity/object choice shifts by 3
        s -= 3
    if 0xFA <= s <= 0xFC:
        ent, obj = ("self", "sender", "actor")[s - 0xFA], ("self_obj", "sender_obj", None)[s - 0xFA]
    elif 0xF4 <= s <= 0xF6:
        ent, obj = ("self", "sender", "actor")[s - 0xF4], ("self_obj", "sender_obj", None)[s - 0xF4]
    else:
        ent, obj = None, None
    if t == 0x10:  # the thing itself: FA..FF the entity, F4..F9 the object
        return (ent if s >= 0xFA else obj) or "null"
    return ent or "none"


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


def decode_one(ram, base, pos):
    """(next position, kind, value) of the instruction at pos."""
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
                if t in WIDE:
                    pos = align(pos, 2) + 2
                    x = "?"
                else:
                    x = ram.u8(base + pos)
                    pos += 1
                who = source(s, t)
                ins = ("push", who if t == 0x10 else f"{who}.p{x}")
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
            else:  # the VM ends the script here
                ins = ("bad", f"push type {s:02X}")
    elif op == 0x03:
        s = ram.u8(base + pos)
        if 0xF0 <= s <= 0xF3:
            ins = ("store", var())
        else:
            pos += 1
            ins = ("bad", f"store to {s:02X}")
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
        ins = ("bad", f"op {op:02X}")
    return (pos,) + ins


def decode(ram, base, start, end, offsets=()):
    """(offset, kind, value) for the code of the script at base, from start. With end None
    (the table's last script) it stops at the first `end` past every handler (offsets) and
    jump target."""
    out = []
    pos = start
    need = max([start, *offsets])
    while end is None or pos < end:
        at = pos
        pos, kind, v = decode_one(ram, base, pos)
        out.append((at, kind, v))
        if kind in ("jz", "jmp"):
            need = max(need, v)
        if end is None and kind in ("end", "bad") and at >= need:
            break
    return out


def call_name(n):
    return BUILTINS[n][0] if n in BUILTINS else f"builtin{n}"


def call_pops(n, args_on_stack):
    """Values a call pops, given the stack (as text, top last), or None if unknown."""
    name, pops, pushes = BUILTINS.get(n, (None, None, 0))
    if name is None:
        return None, 0
    if pops is None:
        if len(args_on_stack) < 2 or not args_on_stack[-2].lstrip("-").isdigit():
            return None, 0
        filt = int(args_on_stack[-2])
        if filt not in FILTER_ARGS:
            return None, 0
        pops = 2 + FILTER_ARGS[filt]
    if pushes is None:  # sound: returns a handle when the mode (last argument) is 3
        pushes = 1 if args_on_stack and args_on_stack[-1] == "3" else 0
    return pops, pushes


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


def folded_lines(code, entries, targets, handlers=None):
    """Statements: the stack folded into expressions, flushed at labels. handlers: the script's
    {index: message} to name what register_msg/unregister_msg subscribe."""
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
            n, t = take(2)
            stack.append(f"({t} {v} {n})")
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
            pops, pushes = call_pops(v, stack)
            args = take(pops if pops is not None else 0)
            expr = f"{call_name(v)}({', '.join(args)})"
            if pushes:
                stack.append(expr)
            else:
                note = ""
                if v in (2, 12) and args and args[-1].isdigit():
                    note = f"  # msg {int(args[-1]):X}"
                if v in (0, 1) and handlers is not None and args and args[-1].isdigit():
                    note = f"  # msg {handlers.get(int(args[-1]), '?'):X}" \
                        if int(args[-1]) in handlers else "  # no such handler"
                out.append(f"    {expr}{note}")
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


def scripts(ram):
    """(id, base, handlers [(message, offset)], code start, code end or None) per script."""
    table = ram.u32(0x8009D5AC)
    slots = ram.u32(table + 4)
    offs = sorted({ram.u32(table + 8 + 4 * i) for i in range(slots)} - {0})
    for sid in range(slots):
        off = ram.u32(table + 8 + 4 * sid)
        if not off:
            continue
        base = table + off
        n = ram.u32(base)
        hs = [(ram.u16(base + 4 + 4 * k), ram.u16(base + 6 + 4 * k)) for k in range(n)]
        start = min(o for _, o in hs) if hs else 4 + 4 * n
        nxt = next((o for o in offs if o > off), None)
        yield sid, base, hs, start, (nxt - off) if nxt else None


def check(ram):
    """Problems in every script: list of (script id, text)."""
    problems = []
    for sid, base, hs, start, end in scripts(ram):
        code = decode(ram, base, start, end, [o for _, o in hs])
        at_index = {at: i for i, (at, _, _) in enumerate(code)}
        stack_types = {}  # instruction offset -> depth on entry
        for msg, off in hs:
            if off not in at_index:
                problems.append((sid, f"handler {msg:X} at {off:X} not on an instruction"))
        for at, kind, v in code:
            if kind == "bad":
                problems.append((sid, f"{at:X}: {v}"))
            if kind == "call" and v not in BUILTINS:
                problems.append((sid, f"{at:X}: unknown built-in {v}"))
            if kind in ("jz", "jmp") and v not in at_index:
                problems.append((sid, f"{at:X}: jump to {v:X} not on an instruction"))
        # Walk every path from every handler with symbolic values (only constants matter:
        # register's filter, sound's mode).
        work = [(off, ()) for _, off in hs if off in at_index]
        seen = {}
        while work:
            pos, st = work.pop()
            while True:
                if pos not in at_index:
                    problems.append((sid, f"falls off the code at {pos:X}"))
                    break
                depth = len(st)
                if pos in seen:
                    if seen[pos] != depth:
                        problems.append((sid, f"{pos:X}: stack depth {seen[pos]} or {depth}"))
                    break
                seen[pos] = depth
                i = at_index[pos]
                at, kind, v = code[i]
                nxt = code[i + 1][0] if i + 1 < len(code) else None
                st = list(st)
                if kind == "push":
                    st.append(v)
                elif kind in ("binop",):
                    if len(st) < 2:
                        problems.append((sid, f"{at:X}: underflow"))
                        break
                    st[-2:] = ["?"]
                elif kind == "not":
                    if not st:
                        problems.append((sid, f"{at:X}: underflow"))
                        break
                    st[-1] = "?"
                elif kind in ("store", "pop", "jz"):
                    if not st:
                        problems.append((sid, f"{at:X}: underflow"))
                        break
                    st.pop()
                elif kind == "call":
                    pops, pushes = call_pops(v, st)
                    if pops is None:
                        problems.append((sid, f"{at:X}: {call_name(v)} with unknown arity"))
                        break
                    if len(st) < pops:
                        problems.append((sid, f"{at:X}: underflow in {call_name(v)}"))
                        break
                    del st[len(st) - pops:]
                    st += ["?"] * pushes
                elif kind == "end":
                    if st:
                        problems.append((sid, f"{at:X}: end with {len(st)} on the stack"))
                    break
                elif kind == "bad":
                    break
                st = tuple(st)
                if kind == "jz":
                    work.append((v, st))
                if kind == "jmp":
                    pos = v
                    continue
                pos = nxt
    return problems


def main():
    args = sys.argv[1:]
    if args[:1] == ["--check"]:
        bad = 0
        for path in args[1:]:
            ram = Ram(path)
            n = sum(1 for _ in scripts(ram))
            probs = check(ram)
            bad += len(probs)
            print(f"{path}: {n} scripts, {len(probs)} problems")
            for sid, text in probs:
                print(f"  script {sid}: {text}")
        sys.exit(1 if bad else 0)
    raw = args[:1] == ["--raw"]
    if raw:
        args = args[1:]
    ram = Ram(args[0])
    want = {int(x, 0) for x in args[1:]}
    for sid, base, hs, start, end in scripts(ram):
        if want and sid not in want:
            continue
        entries = {}
        for msg, off in hs:
            entries.setdefault(off, msg)
        nvars = start - (4 + 4 * len(hs))
        print(f"script {sid} at {base:08X}: handlers [{', '.join(f'{m:X}' for m, _ in hs)}], "
              f"{nvars} variable bytes")
        code = decode(ram, base, start, end, [o for _, o in hs])
        targets = {v for _, kind, v in code if kind in ("jz", "jmp")}
        if raw:
            print("\n".join(raw_lines(code, entries, targets)))
        else:
            print("\n".join(folded_lines(code, entries, targets,
                                         {k: m for k, (m, _) in enumerate(hs)})))
        print()


if __name__ == "__main__":
    main()
