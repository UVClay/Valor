#!/usr/bin/env python3
"""Decode PS1 SPU ADPCM (the level .VB music files, VAB bodies) for inspection.

  snd_vb.py stats FILE [FILE2]      length, loop/end flags; with FILE2: per-second RMS of both and of the difference
  snd_vb.py wav FILE OUT.wav [RATE] decode to a mono 16-bit WAV (default 22050 Hz) under build/
"""
import struct
import sys
import wave

F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def decode(data):
    out, s1, s2 = [], 0, 0
    for i in range(0, len(data) - 15, 16):
        hdr, flags = data[i], data[i + 1]
        shift, filt = hdr & 0xF, min(hdr >> 4, 4)
        f0, f1 = F[filt]
        for b in data[i + 2:i + 16]:
            for nib in (b & 0xF, b >> 4):
                n = nib - 16 if nib >= 8 else nib
                s = (n << 12) >> shift if shift <= 12 else 0
                s += (s1 * f0 + s2 * f1 + 32) >> 6
                s = max(-32768, min(32767, s))
                s2, s1 = s1, s
                out.append(s)
    return out


def rms(x):
    return (sum(v * v for v in x) / max(1, len(x))) ** 0.5


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    data = open(path, "rb").read()
    if cmd == "stats":
        flags = {}
        for i in range(0, len(data), 16):
            flags[data[i + 1]] = flags.get(data[i + 1], 0) + 1
        print(path, len(data), "bytes", len(data) // 16 * 28 / 22050, "s at 22050; flag bytes", flags)
        if len(sys.argv) > 3:
            a, b = decode(data), decode(open(sys.argv[3], "rb").read())
            for t in range(0, min(len(a), len(b)) // 22050):
                w = slice(t * 22050, (t + 1) * 22050)
                d = [x - y for x, y in zip(a[w], b[w])]
                print(f"{t:4d}s  rms {rms(a[w]):7.0f} {rms(b[w]):7.0f}  diff {rms(d):7.0f}")
    elif cmd == "wav":
        rate = int(sys.argv[4]) if len(sys.argv) > 4 else 22050
        pcm = decode(data)
        with wave.open(sys.argv[3], "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))


if __name__ == "__main__":
    main()
