#!/usr/bin/env python3
"""Weapon timings from a VALOR_WATCH log (docs/re/player.md, Weapons in play).

usage: mv_weapons.py LOG

The run must watch  sc=[8009D654]+11C:4  (the soldier's script context: +0x11C frames left,
+0x11E script number) and  inv=[8009D654]+40C:30  (the weapon block +0x40..+0x6F). Prints the
events (weapon drawn, scripts started, rounds fired, reloads) and, per weapon, the ticks
between shots, the reload script's length and the draw script's length. Ticks are vblanks / 2.
"""
import re
import sys
from collections import defaultdict

LINE = re.compile(r'^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)')
SCRIPTS = {1: "ready", 5: "idle", 8: "fire", 9: "reload", 10: "draw", 12: "lob", 14: "throw"}


def main():
    mem = {}       # (name, off) -> word
    frames = defaultdict(dict)
    for line in open(sys.argv[1], errors="replace"):
        m = LINE.match(line)
        if not m:
            continue
        f, name, off, old, new = int(m[1]), m[2], int(m[3], 16), int(m[4], 16), int(m[5], 16)
        mem.setdefault((name, off), old)
        frames[f][(name, off)] = new

    def half(name, off):   # a halfword of the watched block, off relative to its start
        w = mem.get((name, off & ~3), 0)
        v = (w >> (16 * ((off & 2) >> 1))) & 0xFFFF
        return v

    def byte(name, off):
        return (mem.get((name, off & ~3), 0) >> (8 * (off & 3))) & 0xFF

    weapon = byte("inv", 0x15)
    script = half("sc", 2)
    script_start = 0
    clips = [half("inv", 0x26 + 2 * s) for s in range(4)]
    shots = defaultdict(list)
    reloads = defaultdict(list)
    draws = defaultdict(list)
    for f in sorted(frames):
        mem.update(frames[f])
        w = byte("inv", 0x15)
        if w != weapon:
            print(f"{f:>7} weapon {weapon} -> {w}")
            weapon = w
        sc = half("sc", 2)
        if sc != script:
            n = (f - script_start) // 2
            print(f"{f:>7} script {script} ({SCRIPTS.get(script, '?')}, {n} ticks) -> {sc} "
                  f"({SCRIPTS.get(sc, '?')}, length {half('sc', 0)})")
            if script == 9:
                reloads[weapon].append(n)
            if script == 10:
                draws[weapon].append(n)
            script, script_start = sc, f
        slot = byte("inv", 0x14)
        for s in range(4):
            c = half("inv", 0x26 + 2 * s)
            if c != clips[s]:
                print(f"{f:>7} slot {s} clip {clips[s]} -> {c}  reserve {half('inv', 0x16 + 2 * s)}")
                if c == clips[s] - 1 and s == slot:
                    shots[weapon].append(f)
                clips[s] = c
    print()
    for w in sorted(set(shots) | set(reloads)):
        fs = shots[w]
        gaps = [(b - a) // 2 for a, b in zip(fs, fs[1:])]
        print(f"weapon {w}: {len(fs)} rounds; ticks between rounds {sorted(set(gaps))} "
              f"(most {max(set(gaps), key=gaps.count) if gaps else '-'}); reload script ticks {reloads[w]}; "
              f"draw ticks {draws[w]}")


main()
