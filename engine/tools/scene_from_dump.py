#!/usr/bin/env python3
"""The view and the characters of a RAM dump, as JSON for the Godot host's --scene (a side-by-side
check of its drawing with the original's frame).

  scene_from_dump.py DUMP.bin [STATE.jsonl VBLANK] > SCENE.json

With the replay's state trace and the dump's frame, the view is the camera's: the camera entity
runs first in a tick, so the frame drawn after the last tick shows the soldier and the view as the
tick before it left them (movement.md, The camera).

The player: the soldier's position, the camera's eye offset, the view yaw and pitch (inv +0x184,
+0x178). The characters: the five slots' soldiers and dogs (0x800B7B08 + i * 0x2814): the model
(the instance's +0x8), position, angles, and the animation and frame their pose was last built
from (the instance's cache, +0x62/+0x64: what the original's frame shows).
"""
import json
import struct
import sys

m = open(sys.argv[1], "rb").read()
u32 = lambda a: struct.unpack_from("<I", m, a & 0x1FFFFF)[0]
s32 = lambda a: struct.unpack_from("<i", m, a & 0x1FFFFF)[0]
s16 = lambda a: struct.unpack_from("<h", m, a & 0x1FFFFF)[0]
sol = u32(0x8009D654)
inv = u32(sol + 0x388)
cam = u32(u32(0x8009D638) + 0x388)
scene = {
    "player": {"x": s32(sol + 0x20C) / 65536, "y": s32(sol + 0x210) / 65536, "z": s32(sol + 0x214) / 65536,
               "eye": s32(cam + 0x94), "yaw": s32(inv + 0x184) / 65536, "pitch": s32(inv + 0x178) / 65536},
    "actors": [],
}
if len(sys.argv) > 3:
    ticks = []
    for line in open(sys.argv[2]):
        d = json.loads(line)
        if d["vblank"] >= int(sys.argv[3]):
            break
        ticks.append(d)
    prev = ticks[-2]
    sp = [e for e in prev["ents"] if e[1] == 1][0]
    scene["player"].update(x=sp[3] / 65536, y=sp[4] / 65536, z=sp[5] / 65536, yaw=prev["player"][0] / 65536,
                           pitch=prev["player"][1] / 65536)
for i in range(5):
    e = 0x800B7B08 + i * 0x2814
    if s32(e + 8) not in (0x1402, 0x1405):
        continue
    inst = u32(e + 0x100)
    if (inst & 0xFFE00003) != 0x80000000:
        continue
    scene["actors"].append({
        "model": u32(u32(inst + 8)),
        "pos": [s32(e + 0x20C + 4 * k) / 65536 for k in range(3)],
        "angles": [s32(e + 0x228 + 4 * k) >> 16 for k in range(3)],
        "anim": s16(inst + 0x62), "frame": s16(inst + 0x64),
    })
json.dump(scene, sys.stdout, indent=1)
