#!/usr/bin/env python3
"""Who handles, subscribes to and sends each level message, over one or more RAM dumps
(docs/re/messages.md).

  lvl_msgs.py RAMDUMP...            a table: message, then per source the levels it appears in
  lvl_msgs.py --level RAMDUMP       one level in detail (script ids, record indices, models)

Sources read from the dumps:
  handler    a level script has a handler for it (lvs.py)
  register   a level script subscribes to it (register_msg; the filter in brackets)
  send       a level script sends it (send / send_to with a constant)
  zone       a record's list: enter/leave (+0x38/+0x3C, the player), c_enter/c_leave
             (+0x40/+0x44, characters), use (+0x4C, Use pressed inside)
  model      a model script's op 0x43 (send_event, broadcast) or op 0x48 (run that entity's
             handler directly) with a literal operand
  pickup     a kind-4 record of an objective item (0x2F..0x31, 0x69, 0x6A when touched)
The engine's own senders (fixed in the code) are listed in messages.md, not found here.
"""
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(__file__))
import lvs  # noqa: E402
import lvl_records  # noqa: E402
import sst  # noqa: E402

MODELS, MODEL_COUNT = 0x8009EC58, 0x8009EC54


def level_name(path):
    m = re.search(r"(\d)_(\d)", os.path.basename(path)) or re.search(r"(\d)_(\d)", path)
    return f"{m.group(1)}:{m.group(2)}" if m else os.path.basename(path)


def script_facts(ram):
    """Yields (kind, message, detail) from the level scripts."""
    for sid, base, hs, start, end in lvs.scripts(ram):
        for msg, _ in hs:
            yield "handler", msg, f"s{sid}"
        code = lvs.decode(ram, base, start, end, [o for _, o in hs])
        stack = []
        for at, kind, v in code:
            if kind == "push":
                stack.append(v)
                continue
            if kind == "call":
                if v in (0, 1) and len(stack) >= 2 and stack[-1].isdigit() and stack[-2].isdigit():
                    idx, filt = int(stack[-1]), int(stack[-2])
                    if idx < len(hs) and v == 0:
                        yield "register", hs[idx][0], f"s{sid}[f{filt}]"
                if v == 2 and stack and stack[-1].isdigit():
                    yield "send", int(stack[-1]), f"s{sid}"
                if v == 12 and stack and stack[-1].isdigit():
                    yield "send", int(stack[-1]), f"s{sid}(to)"
            stack = []  # only a call's own literal arguments matter here


def zone_facts(ram):
    for i, pair, state, rec in lvl_records.records(ram):
        kind = ram.u32(rec + 0xC)
        for name, vals in lvl_records.lists(ram, rec).items():
            if name == "tags":
                continue
            for m in vals:
                yield "zone", m, f"{name}(k{kind} r{i})"


# Pickup kinds 15..19 (the table at 0x8009E8E8, by the kind-4 record's +0x60): touching one
# sends these with the pickup as sender (0x800832C0).
ITEM_MESSAGES = {0x25C50BD4: 0x2F, 0x933987E0: 0x30, 0x96593A51: 0x31, 0x4A5B62D4: 0x69,
                 0xD352336E: 0x6A}


def pickup_facts(ram):
    for i, pair, state, rec in lvl_records.records(ram):
        if ram.u32(rec + 0xC) == 4 and ram.u32(rec + 0x60) in ITEM_MESSAGES:
            yield "pickup", ITEM_MESSAGES[ram.u32(rec + 0x60)], f"item(r{i})"


def model_facts(ram):
    base, count = ram.u32(MODELS), ram.u32(MODEL_COUNT)
    if not (0x80000000 <= base < 0x80200000) or not 0 < count < 512:
        return
    for k in range(count):
        model = base + 0x100 * k
        lst, n = ram.u32(model + 0xB8), ram.u32(model + 0xBC) & 0xFFFF
        if not (0x80000000 <= lst < 0x80200000) or n > 400:
            continue
        for s in range(n):
            ent = lst + 16 * s
            words_n, code = ram.u32(ent + 0x8) >> 16, ram.u32(ent + 0xC)
            if not (0x80000000 <= code < 0x80200000) or words_n > 20000:
                continue
            words = [ram.u32(code + 4 * j) for j in range(words_n)]
            i = 0
            while i < len(words):
                w = words[i]
                op, mask, imm = w >> 24, (w >> 16) & 0xFF, w & 0xFFFF
                if op in (0x43, 0x48) and not mask & 1:
                    what = "send_event" if op == 0x43 else "run"
                    yield "model", imm, f"{what}(m{ram.u32(model):08X}:{s})"
                i += max(sst.length(op, words, i), 1)


def facts(ram):
    yield from script_facts(ram)
    yield from zone_facts(ram)
    yield from pickup_facts(ram)
    yield from model_facts(ram)


def main():
    args = sys.argv[1:]
    if args[:1] == ["--level"]:
        ram = lvl_records.Ram(args[1])
        table = defaultdict(lambda: defaultdict(list))
        for kind, msg, detail in facts(ram):
            table[msg][kind].append(detail)
        for msg in sorted(table):
            parts = [f"{k}: {' '.join(sorted(set(v)))}" for k, v in sorted(table[msg].items())]
            print(f"{msg:3X}  " + "; ".join(parts))
        return
    table = defaultdict(lambda: defaultdict(set))
    for path in args:
        ram = lvl_records.Ram(path)
        name = level_name(path)
        for kind, msg, detail in facts(ram):
            if kind == "zone":
                detail = detail.split("(")[0]
            elif kind == "register":
                detail = detail[detail.index("["):]
            elif kind in ("model", "pickup"):
                detail = detail.split("(")[0]
            else:
                detail = ""
            table[msg][(kind, detail)].add(name)
    for msg in sorted(table):
        parts = []
        for (kind, detail), levels in sorted(table[msg].items()):
            parts.append(f"{kind}{detail} {len(levels)}" + (f" ({' '.join(sorted(levels))})" if len(levels) <= 4 else ""))
        print(f"{msg:3X}  " + "; ".join(parts))


if __name__ == "__main__":
    main()
