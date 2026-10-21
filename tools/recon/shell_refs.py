#!/usr/bin/env python3
"""Every access to an address range, across the binaries (run inside ./dev).

  shell_refs.py LO HI [BIN...]     loads/stores/address builds in [LO, HI), by address

Like `fn.py uses`, but for a range, and it also follows one step of base registers: a register
set to an address (lui + addiu) and then used as a base (`lw v0, 0xD4(a0)` with a0 = g_game)
counts as an access to address + offset. A straight-line scan: a register's value is forgotten
when it is overwritten, not at branches, so check each hit in `fn.py show`.
BIN defaults to SHELL.BIN LEVEL.BIN SLUS_009.74.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fn  # noqa: E402

MEM = ("lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb", "lwl", "lwr", "swl", "swr")


def scan(img, lo, hi):
    known = {}  # reg -> (address value, set at)
    for a in range(img.base, img.end, 4):
        if a in img.starts_set:
            known = {}
        i = img.ins(a)
        op = i.getOpcodeName()
        if op == "lui":
            known[i.rt.value] = (i.getProcessedImmediate() << 16, a)
            continue
        hit = None
        if op in ("addiu", "ori") and i.rs.value in known:
            base = known[i.rs.value][0]
            imm = i.getProcessedImmediate()
            v = (base | imm) if op == "ori" else (base + imm) & 0xFFFFFFFF
            if lo <= v < hi:
                hit = v
            if i.rt.value != 0:
                known[i.rt.value] = (v, a)
                if hit is not None:
                    yield a, hit, op, i
                continue
        elif op in MEM and i.rs.value in known:
            v = (known[i.rs.value][0] + i.getProcessedImmediate()) & 0xFFFFFFFF
            if lo <= v < hi:
                hit = v
        if hit is not None:
            yield a, hit, op, i
        dest = i.rd.value if i.modifiesRd() else i.rt.value if i.modifiesRt() else None
        if dest is not None:
            known.pop(dest, None)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
    bins = sys.argv[3:] or ["SHELL.BIN", "LEVEL.BIN", "SLUS_009.74"]
    for b in bins:
        fn.NAMES.update(fn.load_names(b)[0])
        img = fn.Image(b)
        img.starts_set = set(img.starts)
        for a, v, op, i in scan(img, lo, hi):
            f = img.function(a)[0]
            print(f"{v:08X} {b:12} {a:08X} {fn.name(f):24} {i.disassemble()}")


if __name__ == "__main__":
    main()
