#!/usr/bin/env python3
"""List the sound banks (VABs) inside a level's TAF file, as the game loads them (bank = order).

  snd_taf.py FILE.TAF [-v]     per bank: programs, tones, VAGs, body size, SPU address it lands at;
                               -v: per VAG its size, the tone that names it (centre note, fine), the pitch
Format notes in docs/re/audio.md. Reads the file only; prints numbers, writes nothing.
"""
import struct
import sys

SPU_FIRST = 0xB010  # where the first bank's body goes (after the music ring buffer)


def tim_end(d, o):
    """Offset after the TIM at o, or None."""
    if o + 8 > len(d) or struct.unpack_from("<I", d, o)[0] != 0x10:
        return None
    flags = struct.unpack_from("<I", d, o + 4)[0]
    p = o + 8
    if flags & 8:
        p += struct.unpack_from("<I", d, p)[0]
    p += struct.unpack_from("<I", d, p)[0]
    return p


def banks(d):
    o = 0
    while True:
        e = tim_end(d, o)
        if e is None:
            break
        o = e
    n = struct.unpack_from("<I", d, o)[0]
    offs = struct.unpack_from(f"<{n}I", d, o + 4)
    return o, [o + x for x in offs]


def main():
    path = sys.argv[1]
    verbose = "-v" in sys.argv
    d = open(path, "rb").read()
    hdr, vabs = banks(d)
    print(f"{path}: VAB table at 0x{hdr:X}, {len(vabs)} banks")
    spu = SPU_FIRST
    for bank, v in enumerate(vabs):
        magic, ver, vid, size, _, ps, ts, vs, mvol, pan, a1, a2, _ = struct.unpack_from("<4sIIIHHHHBBBBI", d, v)
        head = 0x20 + 0x800 + ps * 0x200 + 0x200
        body = size - head
        sizes = struct.unpack_from("<256H", d, v + 0x20 + 0x800 + ps * 0x200)
        tones = []
        for t in range(ps * 16):
            r = v + 0x820 + t * 32
            prior, mode, vol, tpan, centre, fine, mn, mx = struct.unpack_from("<8B", d, r)
            prog, vag = struct.unpack_from("<hh", d, r + 0x14)
            if vag > 0:
                tones.append((vag, centre, fine, vol, tpan, prior, mode, prog))
        print(f" bank {bank}: {magic[::-1].decode()} id {vid} programs {ps} tones {ts} vags {vs} "
              f"master vol {mvol} body 0x{body:X} at SPU 0x{spu:X}..0x{spu + body:X}")
        if verbose:
            for i in range(vs):
                t = next((x for x in tones if x[0] == i + 1), None)
                desc = f"centre {t[1]} fine {t[2]} vol {t[3]} pan {t[4]} prio {t[5]} prog {t[7]}" if t else "no tone"
                print(f"   sound {i:3d} (0x{i:02X}): {sizes[i + 1] * 8:6d} bytes  {desc}")
        spu += body


if __name__ == "__main__":
    main()
