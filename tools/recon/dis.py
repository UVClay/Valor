#!/usr/bin/env python3
"""Quick-look disassembler for recon.

  dis.py <binary> range <addr> [count]   disassemble count instructions at addr
  dis.py <binary> refs <addr>            find lui/addiu|ori|lw|sw pairs that build addr
  dis.py <binary> find <hex-bytes>       find byte pattern, print addresses

<binary> is an extracted file; PS-X EXEs and known overlays get their load address automatically.
"""
import struct
import sys
from pathlib import Path

import rabbitizer

OVERLAY_BASE = 0x80039D90
IMM_USERS = {"addiu", "ori", "lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb", "lwc2", "swc2"}


def load(path: Path):
    data = path.read_bytes()
    if data[:8] == b"PS-X EXE":
        base = struct.unpack_from("<I", data, 0x18)[0]
        return data[0x800:], base
    return data, OVERLAY_BASE


def instr(data, base, addr):
    word = struct.unpack_from("<I", data, addr - base)[0]
    return rabbitizer.Instruction(word, vram=addr, category=rabbitizer.InstrCategory.R3000GTE)


def cmd_range(data, base, addr, count):
    for i in range(count):
        a = addr + i * 4
        ins = instr(data, base, a)
        print(f"{a:08X}: {ins.getRaw():08X}  {ins.disassemble()}")


def cmd_refs(data, base, target):
    hi = {}  # reg -> (addr, value) from the most recent lui
    for off in range(0, len(data) - 3, 4):
        a = base + off
        ins = instr(data, base, a)
        name = ins.getOpcodeName()
        if name == "lui":
            hi[ins.rt.value] = (a, ins.getProcessedImmediate() << 16)
        elif name in IMM_USERS:
            rs = ins.rs.value
            if rs in hi and a - hi[rs][0] < 0x80:
                imm = ins.getProcessedImmediate()
                value = (hi[rs][1] | imm) if name == "ori" else (hi[rs][1] + imm) & 0xFFFFFFFF
                if value == target:
                    print(f"{a:08X}: {ins.disassemble()}   (lui at {hi[rs][0]:08X})")


def cmd_find(data, base, pattern):
    start = 0
    while (i := data.find(pattern, start)) >= 0:
        print(f"{base + i:08X}")
        start = i + 1


def main():
    path, cmd, *rest = sys.argv[1:]
    data, base = load(Path(path))
    if cmd == "range":
        cmd_range(data, base, int(rest[0], 16), int(rest[1]) if len(rest) > 1 else 32)
    elif cmd == "refs":
        cmd_refs(data, base, int(rest[0], 16))
    elif cmd == "find":
        cmd_find(data, base, bytes.fromhex(rest[0]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
