#!/usr/bin/env python3
"""Compares two state traces (VALOR_TRACE_STATE, runtime/src/watch.cpp) tick by tick.

  trace_cmp.py A.jsonl B.jsonl [--from LINE] [--max N]

Lines are compared in order (the tick counter restarts with each level). Prints the first N
differences (default 10): the line and tick, the player field or the entity (by list position and
handle) and the field, with both values. A new engine writing the same format from the same
inputs can be checked against the original this way.
"""
import json
import sys

PLAYER = ["look yaw", "look pitch", "slot", "weapon", "clip", "reserve", "flags B"]
ENT = ["handle", "type", "flags A", "x", "y", "z", "pitch", "yaw", "roll", "health", "animation", "frame"]


def states(path):
    with open(path) as f:
        for line in f:
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                return  # a run stopped mid-line


def main():
    args = sys.argv[1:]
    start, limit = 0, 10
    if "--from" in args:
        i = args.index("--from")
        start = int(args[i + 1])
        del args[i:i + 2]
    if "--max" in args:
        i = args.index("--max")
        limit = int(args[i + 1])
        del args[i:i + 2]
    found = compared = 0
    for line, (da, db) in enumerate(zip(states(args[0]), states(args[1]))):
        if line < start:
            continue
        compared += 1
        t = f"{line} (tick {da['tick']})"
        diffs = [] if da["tick"] == db["tick"] else [f"tick counter: {da['tick']} / {db['tick']}"]
        for k, (x, y) in enumerate(zip(da.get("player", []), db.get("player", []))):
            if x != y:
                diffs.append(f"player {PLAYER[k]}: {x} / {y}")
        ea, eb = da["ents"], db["ents"]
        if len(ea) != len(eb):
            diffs.append(f"entity count: {len(ea)} / {len(eb)}")
        for n, (u, v) in enumerate(zip(ea, eb)):
            for k, (x, y) in enumerate(zip(u, v)):
                if x != y:
                    diffs.append(f"entity {n} ({u[0]:08X}, type {u[1]:X}) {ENT[k]}: {x} / {y}")
        for d in diffs:
            print(f"line {t}: {d}")
            found += 1
            if found >= limit:
                return
    print(f"{compared} lines compared, {found} differences" + ("" if found else ": identical"))


if __name__ == "__main__":
    main()
