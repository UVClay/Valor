#!/usr/bin/env python3
"""List the calls of the sound players with their constant arguments (run inside ./dev).

  snd_calls.py [ADDR...]   default: sound_play (0x80041CE8)

For each call site: the function, and the arguments set by constants in the instructions before the
jal (a0..a3, and the stack words sp+0x10.. for arguments 5 on), scanning back to the previous call or
branch target. '?' = computed. Argument order of sound_play: bank, sound, entity, volume,
volume spread, pitch, pitch spread, fade-in, loop, falloff, flags (docs/re/audio.md).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fn  # noqa: E402

REGS = {"$a0": 0, "$a1": 1, "$a2": 2, "$a3": 3}


def args_at(img, call):
    vals, known = {}, {}
    regs = {}
    a = call + 4  # delay slot first
    seq = [a]
    b = call - 4
    start, _ = img.function(call)
    while b >= start and len(seq) < 60:
        ins = img.ins(b)
        if ins.isJump() and b != call:
            break
        seq.append(b)
        b -= 4
    # replay forward to track constants in temporaries
    for a in reversed(seq[1:]):
        _step(img.ins(a), regs, vals)
    _step(img.ins(call + 4), regs, vals)
    return vals


def _step(ins, regs, vals):
    op = ins.getOpcodeName()
    ops = ins.disassemble().split(None, 1)
    if len(ops) < 2:
        return
    f = [x.strip() for x in ops[1].split(",")]
    def val(r):
        return regs.get(r)
    try:
        if op == "addiu" and f[1] == "$zero":
            regs[f[0]] = int(f[2], 0)
        elif op == "ori" and f[1] == "$zero":
            regs[f[0]] = int(f[2], 0)
        elif op == "lui":
            regs[f[0]] = int(f[1], 0) << 16
        elif op == "ori" and val(f[1]) is not None:
            regs[f[0]] = val(f[1]) | int(f[2], 0)
        elif op == "addiu" and val(f[1]) is not None:
            regs[f[0]] = (val(f[1]) + int(f[2], 0)) & 0xFFFFFFFF
        elif op in ("addu", "or") and f[2] == "$zero":
            regs[f[0]] = val(f[1])
        elif op == "sll" and val(f[1]) is not None:
            regs[f[0]] = (val(f[1]) << int(f[2], 0)) & 0xFFFFFFFF
        elif op == "sw" and f[1].endswith("($sp)"):
            off = int(f[1].split("(")[0], 0)
            if off >= 0x10:
                vals[4 + (off - 0x10) // 4] = val(f[0]) if f[0] != "$zero" else 0
        elif f and f[0].startswith("$"):
            regs[f[0]] = None
        if f and f[0] in REGS:
            vals[REGS[f[0]]] = regs.get(f[0]) if f[0] != "$zero" else 0
    except (ValueError, IndexError):
        if f and f[0].startswith("$"):
            regs[f[0]] = None


def fmt(v):
    if v is None:
        return "?"
    if v >= 0x80000000:
        v -= 1 << 32
    return str(v) if -16 < v < 16 else hex(v)


def main():
    targets = [int(x, 16) for x in sys.argv[1:]] or [0x80041CE8]
    for binname in ("LEVEL.BIN",):
        img = fn.Image(binname)
        fn.load_names(binname)
        for t in targets:
            for a in range(img.base, img.end, 4):
                ins = img.ins(a)
                if ins.getOpcodeName() == "jal" and ins.getInstrIndexAsVram() == t:
                    v = args_at(img, a)
                    s, _ = img.function(a)
                    n = max(v) + 1 if v else 0
                    print(f"{a:08X} {fn.name(s):28s} " + ", ".join(fmt(v.get(i)) for i in range(max(n, 11))))


if __name__ == "__main__":
    main()
