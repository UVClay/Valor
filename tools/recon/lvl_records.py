#!/usr/bin/env python3
"""Lists a level's entity records from a RAM dump (docs/re/levels.md, Entity records).

  lvl_records.py RAMDUMP            one line per record
  lvl_records.py --msgs RAMDUMP     only the message lists, as "who sends what"

The table is `[g_level + 0x53C]`: count, size, two words, then `count` pairs {state, record}
(the pair is what scripts and messages call the record's "object"). Per record: class hash, kind
(+0xC), position, script id (+0x54, and +0x64 / +0x78 by kind), the player's enter/leave lists
(+0x38/+0x3C), characters' enter/leave lists (+0x40/+0x44), the tag list (+0x48), the Use list
(+0x4C). A list is a u32 count, then u32 values.
"""
import struct
import sys

G_LEVEL = 0x8009CC64


class Ram:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def u8(self, a):
        return self.d[a & 0x1FFFFF]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x1FFFFF)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFF)[0]



LISTS = (("enter", 0x38), ("leave", 0x3C), ("c_enter", 0x40), ("c_leave", 0x44),
         ("tags", 0x48), ("use", 0x4C))


def records(ram):
    """(index, pair address, state, record address) for every record."""
    table = ram.u32(ram.u32(G_LEVEL) + 0x53C)
    for i in range(ram.u32(table)):
        pair = table + 16 + 8 * i
        yield i, pair, ram.u32(pair), ram.u32(pair + 4)


def lists(ram, rec):
    out = {}
    for name, off in LISTS:
        p = ram.u32(rec + off)
        if 0x80000000 <= p < 0x80200000:
            n = ram.u32(p)
            if 0 < n < 64:
                out[name] = [ram.u32(p + 4 + 4 * k) for k in range(n)]
    return out


def script_ids(ram, rec):
    kind = ram.u32(rec + 0xC)
    sid = ram.s32(rec + 0x54)
    own = ram.s32(rec + 0x78) if kind == 3 else ram.s32(rec + 0x64) if kind in (2, 4, 6) else None
    return kind, sid, own


def main():
    args = sys.argv[1:]
    only_msgs = args[:1] == ["--msgs"]
    if only_msgs:
        args = args[1:]
    ram = Ram(args[0])
    for i, pair, state, rec in records(ram):
        kind, sid, own = script_ids(ram, rec)
        ls = lists(ram, rec)
        if only_msgs:
            for name in ("enter", "leave", "c_enter", "c_leave", "use"):
                if name in ls:
                    print(f"rec {i} kind {kind} {name}: {' '.join(f'{m:X}' for m in ls[name])}")
            continue
        x, y, z = ram.s16(rec + 0x10), ram.s16(rec + 0x12), ram.s16(rec + 0x14)
        text = (f"{i:3} {pair:08X} state {state:X} class {ram.u32(rec):08X} kind {kind} "
                f"at ({x}, {y}, {z}) script {sid}")
        if own is not None and own != sid:
            text += f" (own {own})"
        for name, vals in ls.items():
            text += f" {name}=[{' '.join(f'{v:X}' for v in vals)}]"
        print(text)


if __name__ == "__main__":
    main()
