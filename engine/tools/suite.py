#!/usr/bin/env python3
"""The engine's regression test: every recording run by coretest --engine from its seed (a RAM dump
of the original) and from the disc (the level built from the game's files), each compared tick by
tick with the original's state trace.

    engine/tools/suite.py [--coretest build/gd/cmake/coretest] [-j N] [--only 1:3,5:4] [--seed-only]

The recordings: the session (build/engine: 1:3, 2:1, 2:2) and every coverage tape
build/engine/in_m{M}l{L}. Prints a row per recording (ticks, identical from the seed, from the
disc, the first difference) and checks them against engine/tools/suite_expect.txt (the identical
counts the engine has reached): exit 1 when any run falls below it. --update rewrites that file
with this run's counts where they rose.
"""
import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXPECT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "suite_expect.txt")
SESSION = ["1:3", "2:1", "2:2"]
RESULT = re.compile(r"^(\d+) ticks compared, (\d+) identical \(handles aside\); first difference at tick (-?\d+)")


def recordings():
    out = [(lv, os.path.join(ROOT, "build", "engine")) for lv in SESSION]
    d = os.path.join(ROOT, "build", "engine")
    tapes = []
    for name in os.listdir(d):
        mm = re.match(r"^in_m(\d+)l(\d+)$", name)
        if mm and os.path.exists(os.path.join(d, name, "orig.log")):
            tapes.append((int(mm[1]), int(mm[2]), os.path.join(d, name)))
    out += [(f"{m}:{l}", p) for m, l, p in sorted(tapes)]
    return out


def run(coretest, level, inputs, disc):
    env = dict(os.environ)
    if inputs != os.path.join(ROOT, "build", "engine"):
        env["ENG_INPUTS"] = inputs
    else:
        env.pop("ENG_INPUTS", None)
    cmd = [coretest, "--engine", level] + (["--disc"] if disc else [])
    p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)
    for line in reversed(p.stdout.splitlines()):
        mm = RESULT.match(line)
        if mm:
            return int(mm[1]), int(mm[2]), int(mm[3])
    tail = (p.stderr.strip().splitlines() or p.stdout.strip().splitlines() or ["no output"])[-1]
    return None, None, tail


def load_expect():
    exp = {}
    if os.path.exists(EXPECT):
        for line in open(EXPECT):
            f = line.split("#")[0].split()
            if len(f) == 3:
                exp[(f[0], f[1])] = int(f[2])
    return exp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--coretest", default=os.path.join(ROOT, "build", "gd", "cmake", "coretest"))
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--only", default="")
    ap.add_argument("--seed-only", action="store_true")
    ap.add_argument("--update", action="store_true")
    a = ap.parse_args()
    recs = recordings()
    if a.only:
        want = set(a.only.split(","))
        recs = [r for r in recs if r[0] in want]
    modes = ["seed"] if a.seed_only else ["seed", "disc"]
    jobs = [(lv, inp, mode) for lv, inp in recs for mode in modes]
    # the longest first: the session's levels and the tapes run 5,000 to 10,000 ticks each
    with ThreadPoolExecutor(a.j) as ex:
        res = dict(zip(jobs, ex.map(lambda j: run(a.coretest, j[0], j[1], j[2] == "disc"), jobs)))
    exp = load_expect()
    failed = []
    print(f"{'recording':<10} {'ticks':>6}  {'from the seed':>15}  {'from the disc':>15}  first difference")
    for lv, inp in recs:
        cells, first = [], []
        ticks = None
        for mode in ["seed", "disc"]:
            if mode not in modes:
                cells.append(f"{'-':>15}")
                continue
            n, same, fd = res[(lv, inp, mode)]
            if n is None:
                cells.append(f"{'fails':>15}")
                first.append(f"{mode}: {fd}")
                failed.append((lv, mode, "no result"))
                continue
            ticks = n
            mark = "whole" if same == n else f"{same * 100 // n}%"
            cells.append(f"{same:>7,} {mark:>7}")
            if fd >= 0:
                first.append(f"{mode} {fd}")
            want = exp.get((lv, mode))
            if want is not None and same < want:
                failed.append((lv, mode, f"{same} < {want}"))
        print(f"{lv:<10} {ticks if ticks is not None else '-':>6}  {cells[0]}  {cells[1]}  {', '.join(first)}")
    if a.update:
        for (lv, _, mode), (n, same, _) in res.items():
            if same is not None and same > exp.get((lv, mode), -1):
                exp[(lv, mode)] = same
        with open(EXPECT, "w") as f:
            f.write("# engine/tools/suite.py: the ticks identical with the original each run has reached\n")
            for (lv, mode), same in sorted(exp.items(), key=lambda kv: (tuple(map(int, kv[0][0].split(":"))), kv[0][1])):
                f.write(f"{lv} {mode} {same}\n")
    if failed:
        print("\nbelow the expected:")
        for lv, mode, why in failed:
            print(f"  {lv} from the {mode}: {why}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
