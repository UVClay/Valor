#!/usr/bin/env python3
"""Lists the live entities in a RAM dump (docs/re/objects.md).

  obj_ents.py RAMDUMP [--type HEX...] [--fields OFF:LEN,...]

One line per entity in g_entities (0x8009D64C, linked through +0x4): address, type, flags A/B,
mask, position (integer parts of +0x20C..), think/touch/hit_part handlers, the model hash
([[+0x100]+0x8]+0x0), the level record (+0x1C -> +0x4) and its kind. --fields adds raw words
(OFF hex, LEN words) for each entity.
"""
import struct
import sys


class Ram:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def ok(self, a):
        return 0x80000000 <= a < 0x80200000

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x1FFFFF)[0]


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    ram = Ram(args[0])
    types = set()
    fields = []
    i = 1
    while i < len(args):
        if args[i] == "--type":
            i += 1
            while i < len(args) and not args[i].startswith("--"):
                types.add(int(args[i], 16))
                i += 1
            continue
        if args[i] == "--fields":
            for f in args[i + 1].split(","):
                off, _, n = f.partition(":")
                fields.append((int(off, 16), int(n or "1")))
            i += 2
            continue
        i += 1
    e = ram.u32(0x8009D64C)
    seen = set()
    while ram.ok(e) and e not in seen:
        seen.add(e)
        t = ram.u32(e + 8)
        if not types or t in types:
            pos = [ram.s32(e + 0x20C + 4 * k) >> 16 for k in range(3)]
            model = 0
            inst = ram.u32(e + 0x100)
            if ram.ok(inst) and ram.ok(ram.u32(inst + 8)):
                model = ram.u32(ram.u32(inst + 8))
            rec, kind = 0, -1
            st = ram.u32(e + 0x1C)
            if ram.ok(st) and ram.ok(ram.u32(st + 4)):
                rec = ram.u32(st + 4)
                kind = ram.u32(rec + 0xC)
            line = (f"{e:08X} t={t:04X} A={ram.u32(e + 0xC):08X} B={ram.u32(e + 0x10):08X} "
                    f"m={ram.u32(e + 0x14):08X} pos={pos} think={ram.u32(e + 0x36C):08X} "
                    f"touch={ram.u32(e + 0x37C):08X} hit={ram.u32(e + 0x374):08X} "
                    f"model={model:08X} rec={rec:08X}/{kind}")
            for off, n in fields:
                line += f" +{off:X}=" + ",".join(f"{ram.u32(e + off + 4 * k):08X}"
                                                for k in range(n))
            print(line)
        e = ram.u32(e + 4)


if __name__ == "__main__":
    main()
