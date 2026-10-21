#!/usr/bin/env python3
"""What completes each objective: the message chain back to its causes (docs/re/catalogue.md).

  cat_obj.py --ram DUMP BSD...

For each objective_complete(id) (and level_set(2/3, 1): primary objectives done, level end), the
handler's message M is traced back: record lists holding M (player enter/leave, character
enter/leave, Use), item pickups (pickup table entries 15..19 send 0x2F, 0x30, 0x31, 0x69, 0x6A),
messages the engine sends (below), and script handlers that send M (send_self/send_to with a
literal), which are traced in turn (3 levels deep). A handler reached only through a counter or
a condition shows its condition text. DUMP: any RAM dump with LEVEL.BIN loaded (texts).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cat_lvl  # noqa: E402

ENGINE = {
    0: "start/spawn", 1: "hit (soldier by a projectile, destructible by anything)", 2: "drawn while +0x290 <= 0",
    0x1A: "entity removed", 0x59: "1/3-second timer", 0x2B: "alarm raised (zone entities)",
    0x2F: "item pickup 15", 0x30: "item pickup 16", 0x31: "item pickup 17", 0x69: "item pickup 18",
    0x6A: "item pickup 19", 0x04: "soldier op 0x43 (papers: blown)", 0x1B: "soldier op 0x43",
    0x1E: "soldier op 0x43", 0x33: "soldier op 0x43", 0x75: "soldier op 0x43", 0x74: "type 0x1774 op 0x43",
}


def handlers_sending(lv):
    """{message: [(script, handler msg, condition-ish context)]} for literal send/send_to."""
    out = {}
    for sid, s in lv.scripts.items():
        cur = None
        for ln in s["lines"]:
            m = re.match(r"on msg ([0-9A-F]+):", ln)
            if m:
                cur = int(m.group(1), 16)
                continue
            for name, args in re.findall(r"\b(send_self|send_to|send)\(([^()]*)\)", ln):
                a = [x.strip() for x in args.split(",")]
                msg = a[-1]
                if msg.isdigit():
                    out.setdefault(int(msg), []).append((sid, cur))
    return out


def record_sources(lv, tb):
    out = {}
    for r in lv.records:
        for name, vals in r["lists"].items():
            if name == "tags":
                continue
            for v in vals:
                out.setdefault(v, []).append(f"r{r['i']}(k{r['kind']}) {name}")
        if r["kind"] == 4 and "template" in r:
            k = tb.pickups.get(r["template"][0])
            if k in cat_lvl.ITEM_MSG:
                out.setdefault(cat_lvl.ITEM_MSG[k], []).append(f"r{r['i']} item pickup {k}")
    return out


def trace(msg, lv, rec, snd, depth, seen):
    pad = "      " + "  " * depth
    lines = []
    if msg in seen:
        return [pad + f"{msg:X}: (loop)"]
    seen = seen | {msg}
    srcs = rec.get(msg, [])
    if srcs:
        lines.append(pad + f"{msg:X} <- " + "; ".join(srcs[:5]) + (f" (+{len(srcs) - 5})" if len(srcs) > 5 else ""))
    if msg in ENGINE:
        lines.append(pad + f"{msg:X} <- engine: {ENGINE[msg]}")
    for sid, h in snd.get(msg, []):
        lines.append(pad + f"{msg:X} <- script {sid} on msg {h:X}")
        if depth < 3 and h is not None:
            lines += trace(h, lv, rec, snd, depth + 1, seen)
    if not lines:
        lines.append(pad + f"{msg:X} <- (no literal source)")
    return lines


def main():
    args = sys.argv[1:]
    i = args.index("--ram")
    tb = cat_lvl.Tables(args[i + 1])
    args = args[:i] + args[i + 2:]
    for f in args:
        lv = cat_lvl.Level(f)
        rec, snd = record_sources(lv, tb), handlers_sending(lv)
        print(f"== {lv.name}")
        for sid, s in lv.scripts.items():
            for msg, name, a in cat_lvl.calls(s["lines"]):
                if name == "objective_add":
                    t = cat_lvl.num(a[0])
                    print(f"  objective {a[1]}: '{tb.text(t)}' (added on msg {msg:X})")
                if name == "objective_complete" or (name == "level_set" and a[:2] in (["2", "1"], ["3", "1"])):
                    print(f"  {name}({', '.join(a)}) in script {sid} on msg {msg:X}")
                    print("\n".join(trace(msg, lv, rec, snd, 0, frozenset())))
        print()


if __name__ == "__main__":
    main()
