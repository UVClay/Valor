#!/usr/bin/env python3
"""Writes the per-tick inputs and targets of the player's movement, as tools/sim/move_sim.py reads
them from a replay of the original, in a plain text form engine/tools/coretest reads.

  export_moves.py TRACE.jsonl WATCH.log M:L --ram DUMP@VBLANK [--from V] [--to V] > OUT.txt

The watch log is a replay's VALOR_WATCH="$(python3 tools/sim/move_sim.py watch)" and the trace its
VALOR_TRACE_STATE (move_sim.py's docstring). One line per tick with a full state; the first line
names the level. Fields, in order (see FIELDS): the original's own state at the end of the tick
(position, +0x3BC, on ground, floor, vertical speed) and what the control function produced
(forward, strafe, look yaw, eye height, radius, flags) plus the player-state blocks movement
reads (aim pin and lean, flinch and knockback, ladder, the end-of-tick push, the chunk slots).
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "sim"))
import move_sim  # noqa: E402

FIELDS = ["tick", "vblank", "single", "pos0", "pos1", "pos2", "yaw", "flags_b", "health", "fwd", "strafe",
          "vy", "ground", "floor", "eye", "radius", "look_flags", "local_fwd", "ep0", "ep1", "ep2", "flinch",
          "kn0", "kn1", "kn2", "pin0", "pin1", "pin2", "lean", "ladder", "has_old", "old0", "old1", "old2",
          "parent", "has_slots"] + [f"slot{i}_{j}" for i in range(4) for j in range(4)]


def main():
    args = sys.argv[1:]
    trace, watch, level = args[:3]
    lo = int(args[args.index("--from") + 1]) if "--from" in args else 0
    hi = int(args[args.index("--to") + 1]) if "--to" in args else 1 << 40
    seeds = []
    for i, a in enumerate(args):
        if a == "--ram":
            path, _, vbl = args[i + 1].partition("@")
            seeds.append((int(vbl or re.search(r"(\d+)\.bin$", path)[1]), move_sim.ram_seed(path)))
    out = sys.stdout
    out.write(f"valor-moves 1 {level}\n# " + " ".join(FIELDS) + "\n")
    for t in move_sim.ticks(trace, watch, lo, hi, seeds):
        old = t["old"] or [0, 0, 0]
        slots = t["slots"] or [(0, 0, 0, 0)] * 4
        v = [t["tick"], t["vblank"], int(t["single"]), *t["pos"], t["yaw"], t["flags_b"], t["health"],
             t["fwd"], t["strafe"], t["vy"], t["ground"], t["floor"], t["eye"], t["radius"], t["look_flags"],
             t["local_fwd"], *t["end_push"], t["flinch"], *t["knock"], *t["pin"], t["lean"], t["ladder"],
             int(t["old"] is not None), *old, t["parent"], int(t["slots"] is not None)]
        v += [x for s in slots for x in s]
        out.write(" ".join(str(int(x)) for x in v) + "\n")


if __name__ == "__main__":
    main()
