"""What the engine takes from outside, read from one replay of a recording (tools/sim/engine.py):

  VALOR_TRACE_STATE=build/engine/orig.jsonl VALOR_TRACE_RAND=1 VALOR_TRACE_INPUT=1 \
  VALOR_TRACE_DAMAGE=1 VALOR_TRACE_SHOTS=1 RP_BIN=build/valor-eng tools/recon/run.sh eng 1800 \
      build/engine/orig.log --replay recordings/2026-10-19_22-22-43_m1l3 --hidden

- the player's input per tick: the pad words as `player_update` sees them and the four control
  roles' values (buttons and stick/mouse; runtime/src/watch.cpp, VALOR_TRACE_INPUT);
- the frame structure: which ticks ran in the same drawn frame (the trace's vblank per tick),
  real time the engine can't know;
- the random draws, by the function that drew them (VALOR_TRACE_RAND: every rand() with its
  return address, mapped to the function containing it). The engine draws from the functions it
  reimplements, in order, and never sees the draws of sound, drawing and particles.
"""
import bisect
import json
import os
import re
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


class Functions:
    """Function starts of the exe and LEVEL.BIN (the recompiler's tables) and their names."""

    def __init__(self):
        self.starts = []
        for t in ("exe", "level"):
            p = os.path.join(ROOT, "build", "cmake", "gen", "game", f"{t}_table.c")
            self.starts += [int(m, 16) | 0x80000000 for m in re.findall(r"\{0x([0-9A-F]+)u,", open(p).read())]
        self.starts.sort()
        self.names = {}
        for line in open(os.path.join(ROOT, "docs", "re", "names.txt")):
            m = re.match(r"([0-9A-F]{8}) (\w+)", line)
            if m:
                self.names.setdefault(int(m[1], 16), m[2])

    def of(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        return self.starts[i] if i >= 0 else 0

    def name(self, start):
        return self.names.get(start, f"{start:08X}")


RAND = re.compile(r"^rand (\d+) (\d+): ([0-9A-F]+) (\d+)")
INPUT = re.compile(r"^input (\d+) (\d+): ([0-9A-F]+) ([0-9A-F]+) ([0-9A-F]+) ([0-9A-F]+)")
ROLE = re.compile(r"^role (\d+): (\d+) (\d+) (\d+) (-?\d+)")
MARK = re.compile(r"^replay: mark mission (\d) (\d) at vblank (\d+)")


class Feed:
    """The recording's outside inputs, per level (marks split the log) and tick."""

    def __init__(self, log_path):
        fns = Functions()
        self.fns = fns
        self.marks = []                         # (vblank, "m:l")
        self.rand = defaultdict(list)           # (vblank_of_level, tick) -> [(function, ra, value)]
        self.pad = {}                           # (level, tick) -> 4 words
        self.roles = defaultdict(dict)          # (level, tick) -> {(port, role, src): value}
        level = None
        for line in open(log_path, errors="replace"):
            c = line[0]
            if c == "r":
                m = RAND.match(line)
                if m:
                    vb, tick, ra, val = int(m[1]), int(m[2]), int(m[3], 16), int(m[4])
                    self.rand[(level, tick)].append((fns.of(ra), ra, val, vb))
                    continue
                m = ROLE.match(line)
                if m:
                    tick, port, role, src, val = (int(x) for x in m.groups())
                    self.roles[(level, tick)][(port, role, src)] = val
                    continue
                m = MARK.match(line)
                if m:
                    level = f"{m[1]}:{m[2]}"
                    self.marks.append((int(m[3]), level))
                    continue
            elif c == "i":
                m = INPUT.match(line)
                if m:
                    self.pad[(level, int(m[2]))] = tuple(int(m[k], 16) for k in range(3, 7))

    def role(self, level, tick, port, role):
        """The role's value as player_controls got it: the buttons', else the stick's."""
        r = self.roles.get((level, tick), {})
        v = r.get((port, role, 0), 0)
        return v if v else r.get((port, role, 1), 0)


class Rand:
    """The draws of one tick, taken by function in the order the original made them."""

    def __init__(self, feed, level):
        self.feed, self.level = feed, level
        self.tick = None
        self.left = []
        self.misses = []

    def start_tick(self, tick):
        self.tick = tick
        self.left = list(self.feed.rand.get((self.level, tick), ()))

    def draw(self, *functions):
        for i, (fn, ra, val, vb) in enumerate(self.left):
            if fn in functions:
                del self.left[i]
                return val
        self.misses.append((self.tick, functions))
        return 0

    def leftover(self, functions):
        """Draws of these functions the original made this tick and the engine didn't take."""
        return [(fn, ra, v) for fn, ra, v, vb in self.left if fn in functions]


def read_trace(path):
    out = []
    for line in open(path):
        try:
            out.append(json.loads(line))
        except json.JSONDecodeError:
            break
    return out


# The functions whose draws the engine takes, by the system that draws (objects.md, Randomness).
FN = {
    "scripts": (0x8003F8F0, 0x8005959C, 0x8005989C),        # script_run_a's pick, ops 1E, 21
    "hit_reaction": (0x80052084, 0x80072244, 0x800727B4),
    "enemy_weapon": (0x8005163C,),
    "enemy_aim": (0x80055198,),
    "bullet": (0x80045A78,),
    "pellet": (0x80045F14,),
    "fire_gun": (0x8007D5C8,),
    "player_hit": (0x8007C1D0,),
    "player_death": (0x80079D10,),
    "helmet": (0x80051200,),
    "mounted_gun": (0x8005DA24,),
    "grenade": (0x80045808, 0x80044120, 0x80043284, 0x80044470),
    "explosion": (0x800719C0,),
    "alarm": (0x8005EA54,),
    "casing": (0x80088040, 0x80087FA0),
    "hit": (0x80043E38, 0x80043AD4, 0x800523E4),
    "sound": (0x80041CE8, 0x80041B04, 0x80058420),
}
