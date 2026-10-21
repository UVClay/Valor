#!/usr/bin/env python3
"""Writes a capture (engine/tools/capture.sh) as the per-tick input coretest reads in its pad mode:
the pad as the game read it, every control-role call, and the soldier as each tick's update found
it (the events of systems the core doesn't run yet, and what to compare the core's last tick with).

  export_pad.py CAPTURE_DIR DUMP_VBLANK [--to VBLANK] > OUT.txt

DUMP_VBLANK names CAPTURE_DIR/dVBLANK.bin, taken a few vblanks into the level: the core starts from
it (the control configuration, the player's state) with the first tick after it, and the export
runs until the level ends (the tick counter restarts), restarts in place after a death (the player
alive again), or --to.

One line per tick (FIELDS), then the entity list after the tick (a count and (handle, type) pairs, in
list order), then its role calls: a count and (role, source, value) triples, the
source 0 for role_button (the core computes it: a check) and 1 for role_axis (the stick's curve
plus what the runtime's mouse and keyboard add: an input). The `s_` fields are the soldier at the
start of the tick's player_update (VALOR_TRACE_INPUT), which is how the tick before left him. A
last line holds the start of the tick after the last, with -1 calls.
"""
import json
import re
import sys

FIELDS = ["tick", "vblank",
          # the trace after the tick: position, the view's yaw and pitch, flags B, health
          "x", "y", "z", "yaw", "pitch", "flags_b", "health", "roll", "body_pitch", "body_yaw",
          # the pad (player_update's read)
          "pad_now", "pad_prev", "sticks", "analog",
          # the soldier at the start of the update
          "s_x", "s_y", "s_z", "s_old_x", "s_old_y", "s_old_z", "s_push_x", "s_push_y", "s_push_z", "s_eye",
          "s_contact", "s_flags_b", "s_inv", "s_inv54", "s_flinch", "s_kn_x", "s_kn_y", "s_kn_z"]

INPUT = re.compile(r"^input (\d+) (\d+): ((?:[0-9A-F]+ ){6}[0-9A-F]+) \|((?: [0-9A-F]+){18})")
ROLE = re.compile(r"^role (\d+): (\d+) (\d+) (\d+) (-?\d+)")


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def main():
    cap, dv = sys.argv[1], int(sys.argv[2])
    hi = int(sys.argv[sys.argv.index("--to") + 1]) if "--to" in sys.argv else 1 << 40
    trace = []
    for line in open(f"{cap}/state.jsonl"):
        try:
            trace.append(json.loads(line))
        except json.JSONDecodeError:
            break
    # The dump is taken when the frame counter reaches DUMP_VBLANK, before that frame's ticks.
    start = next(i for i, d in enumerate(trace) if d["vblank"] >= dv)
    ticks = []
    def health(d):
        sol = [e for e in d["ents"] if e[1] == 1]
        return sol[0][9] if sol else 0

    for d in trace[start:]:
        # The level ends, or restarts in place after a death (the player alive again).
        if ticks and (d["tick"] != ticks[-1]["tick"] + 1 or (health(ticks[-1]) <= 0 < health(d))) or d["vblank"] > hi:
            break
        ticks.append(d)
    first, last = ticks[0]["tick"], ticks[-1]["tick"]
    lo_v, hi_v = ticks[0]["vblank"], ticks[-1]["vblank"]
    inputs, roles = {}, {}
    cur = None
    for line in open(f"{cap}/run.log", errors="replace"):
        m = INPUT.match(line)
        if m:
            v, t = int(m[1]), int(m[2])
            cur = None
            # The tick after the last is read too: its start is the last tick's end.
            if lo_v <= v <= hi_v + 4 and first <= t <= last + 1 and t not in inputs:
                inputs[t] = ([int(x, 16) for x in m[3].split()], [int(x, 16) for x in m[4].split()])
                roles[t] = []
                cur = t
            continue
        m = ROLE.match(line)
        if m and cur is not None and int(m[1]) == cur and int(m[2]) == 0:
            roles[cur].append((int(m[3]), int(m[4]), int(m[5])))
    out = sys.stdout
    out.write(f"valor-pad 5 {dump_level(cap, dv)} {dv}\n# " + " ".join(FIELDS) + " ents... calls...\n")
    for d in ticks:
        t = d["tick"]
        sol = [e for e in d["ents"] if e[1] == 1]
        if not sol or t not in inputs:
            continue
        pad, st = inputs[t]
        v = [t, d["vblank"], *sol[0][3:6], d["player"][0], d["player"][1], d["player"][6], sol[0][9], sol[0][8], sol[0][6], sol[0][7],
             pad[0], pad[2], pad[4], pad[6] & 0xFF] + [s32(x) for x in st]
        calls = roles.get(t, [])
        ents = " ".join(f"{e[0]} {e[1]}" for e in d["ents"])
        out.write(" ".join(str(int(x)) for x in v) + f" {len(d['ents'])} {ents} {len(calls)} " +
                  " ".join(f"{r} {src} {val}" for r, src, val in calls) + "\n")
    if last + 1 in inputs:
        pad, st = inputs[last + 1]
        v = [last + 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, pad[0], pad[2], pad[4], pad[6] & 0xFF] + [s32(x) for x in st]
        out.write(" ".join(str(int(x)) for x in v) + " 0 -1\n")


def dump_level(cap, dv):
    """The level of the dump: the replay's last mission mark before it."""
    lv = None
    for line in open(f"{cap}/run.log", errors="replace"):
        m = re.match(r"replay: mark mission (\d+) (\d+) at vblank (\d+)", line)
        if m and int(m[3]) <= dv:
            lv = f"{m[1]}:{m[2]}"
    if lv is None:
        sys.exit("export_pad: no mission mark before the dump")
    return lv


if __name__ == "__main__":
    main()
