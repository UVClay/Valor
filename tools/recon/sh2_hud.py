#!/usr/bin/env python3
"""Print the HUD's game state from a LEVEL RAM dump (docs/re/hud.md).

  sh2_hud.py RAMDUMP

The dump is the 2 MB of main RAM (--dump-ram), LEVEL.BIN loaded. Prints the health ring,
the ammo strings and icon slot, the compass angle and quad, the hit-direction timers, the
on-screen texts and the message line.
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()


def w(a):
    return struct.unpack_from("<I", d, a - 0x80000000)[0]


def sw(a):
    return struct.unpack_from("<i", d, a - 0x80000000)[0]


def h(a):
    return struct.unpack_from("<h", d, a - 0x80000000)[0]


def b(a):
    return d[a - 0x80000000]


def s(a):
    if not a:
        return None
    o = a - 0x80000000
    return d[o:d.index(b"\0", o)].decode("latin-1")


soldier = w(0x8009D654)
inv = w(soldier + 0x388)
health = sw(soldier + 0xF4)
lit = (h(soldier + 0xF6) & 0xFFFF) >> 2
print(f"health {health / 65536:.2f}: ring {lit}/32 segments lit (segments {32 - lit}..31)")
slot = b(inv + 0x54)
print(f"slot {slot} weapon {b(inv + 0x55)}: clip text {s(inv + 0x108)!r} reserve text {s(inv + 0x10C)!r}"
      f" (clip {h(inv + 0x66 + 2 * slot)}, reserve {h(inv + 0x56 + 2 * slot)})")
yaw = h(inv + 0x186) & 0xFFF
print(f"compass: yaw {yaw} ({yaw * 360 / 4096:.1f} deg); rose top drawn at {(90 - yaw * 360 / 4096) % 360:.1f} deg clockwise from up")
q = 0x800AD170
print("  quad", [(h(q + 8 + 8 * i), h(q + 10 + 8 * i)) for i in range(4)])
names = {0x8009A590: "right", 0x8009A594: "left", 0x8009A598: "front (top)", 0x8009A59C: "behind (bottom)"}
print("hit timers:", ", ".join(f"{n} {sw(a)}" for a, n in names.items()))
print(f"aim: inv flags {w(inv):08X}, stance {sw(inv + 0x1C8)}, crosshair ({h(inv + 0x190)}, {h(inv + 0x192)}),"
      f" zoom {sw(inv + 0xA4)}")
cam = w(w(0x8009D638) + 0x388)
print(f"camera shake: amplitude {h(cam + 0xCC)} ticks {h(cam + 0xCE)}")
n = sw(0x80099ED0)
print(f"texts {n}, message line y {sw(0x80098628)}, last message {sw(0x8009862C)}")
for i in range(n):
    e = 0x80099F4C + 32 * i
    print(f"  {s(w(e))!r} at ({h(e + 4)}, {h(e + 6)}) rgb {b(e + 8)},{b(e + 9)},{b(e + 10)} semi {b(e + 11)}"
          f" life {sw(e + 0xC)} hold {sw(e + 0x10)} fade-in {sw(e + 0x14)} mask {sw(e + 0x18)}"
          f" step {b(e + 0x1C)},{b(e + 0x1D)},{b(e + 0x1E)} id {b(e + 0x1F)}")
