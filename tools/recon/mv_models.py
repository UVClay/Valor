#!/usr/bin/env python3
"""Lists a level's loaded models by type and disassembles their scripts (docs/re/objects.md,
Doors), whether or not an entity uses them yet.

  mv_models.py RAMDUMP                     every model: index, hash, type (+0xFC), script count
  mv_models.py RAMDUMP --type 1776 [SCRIPT...]   scripts of every model of that type

Models are 0x100-byte entries at [0x8009EC58], count [0x8009EC54] (level_load); a model's
scripts as sst.py reads them (+0xB8 list, +0xBC count).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sst import Ram, disassemble  # noqa: E402


def main():
    ram = Ram(sys.argv[1])
    base, count = ram.u32(0x8009EC58), ram.u32(0x8009EC54)
    want = int(sys.argv[3], 16) if len(sys.argv) > 3 and sys.argv[2] == "--type" else None
    which = [int(x, 0) for x in sys.argv[4:]]
    for i in range(count):
        m = base + 0x100 * i
        mtype, nscripts = ram.u32(m + 0xFC), ram.u16(m + 0xBC)
        if want is None:
            print(f"model {i:3} at {m:08X} hash {ram.u32(m):08X} type {mtype:X} scripts {nscripts}")
            continue
        if mtype != want:
            continue
        print(f"== model {i} at {m:08X} hash {ram.u32(m):08X} type {mtype:X}, {nscripts} scripts")
        lst = ram.u32(m + 0xB8)
        for k in which or range(nscripts):
            s = lst + 16 * k
            n, code = ram.u16(s + 0xA), ram.u32(s + 0xC)
            print(f"script {k} ({n} words)")
            print("\n".join(disassemble([ram.u32(code + 4 * j) for j in range(n)])))


main()
