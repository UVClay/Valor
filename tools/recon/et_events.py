#!/usr/bin/env python3
"""Lists a model's event registrations (ops 0x0D/0x0E/0x42) by condition (docs/re/enemy_types.md).

  et_events.py RAMDUMP [--type 1402 | --model HASH]

Conditions (sop_event 0x80058C78, script_run_b 0x8003FBFC): mask bit 1 a pad test, 2 a button
test, 4/8 entity +0x38 all/none of the bits, 0x10/0x20 +0x3C all/none, 0x40/0x80 +0x40 all/none.
One line per (condition, stored action) with the scripts registering it.
"""
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(__file__))
import sst  # noqa: E402
import et_sst  # noqa: E402

FIELDS = [(1, "pad"), (2, "button"), (4, "s38 all"), (8, "s38 none"), (0x10, "s3C all"),
          (0x20, "s3C none"), (0x40, "s40 all"), (0x80, "s40 none")]
STORED = {0x07: "goto", 0x26: "goto", 0x08: "other_script", 0x1D: "jump_script", 0x38: "call",
          0x3B: "start_script", 0x0F: "(unregister)", 0x13: "set"}


def main():
    a = sys.argv[1:]
    ram = sst.Ram(a[0])
    if "--model" in a:
        m = et_sst.find_model(ram, hsh=int(a[a.index("--model") + 1], 16))
    else:
        t = int(a[a.index("--type") + 1], 16) if "--type" in a else 0x1402
        m = et_sst.find_model(ram, typ=t)
    S = et_sst.script_words(ram, m)
    out = defaultdict(list)
    for k, (_, w) in enumerate(S):
        i = 0
        while i < len(w):
            op = w[i] >> 24
            n = max(sst.length(op, w, i), 1)
            if op in (0x0D, 0x0E, 0x42):
                mask = w[i] & 0xFF
                vals = []
                j = i + 1
                for bit, name in FIELDS:
                    if mask & bit:
                        vals.append(f"{name} {w[j]:#x}")
                        j += 1
                st = w[j] if j < len(w) else 0
                sop = st >> 24
                act = STORED.get(sop, f"op{sop:02x}")
                if sop in (0x08, 0x1D, 0x38, 0x3B):
                    act += f" {st & 0xFFFF}"
                elif sop == 0x13:
                    act += f" {st & 0xFFFF:#x} = {w[j + 1] if j + 1 < len(w) else '?'}"
                elif sop in (0x07, 0x26):
                    act += " (here)"
                out[(", ".join(vals), act, op)].append(k)
            i += n
    for (cond, act, op), ks in sorted(out.items()):
        ks = sorted(set(ks))
        shown = " ".join(map(str, ks[:12])) + (f" ... ({len(ks)})" if len(ks) > 12 else "")
        print(f"op{op:02X} {cond:28} -> {act:20} in {shown}")


if __name__ == "__main__":
    main()
