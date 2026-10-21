"""Comparing the engine's trace with the original's, tick by tick, grouped by kind of difference."""
from collections import Counter, defaultdict

PLAYER = ["look yaw", "look pitch", "slot", "weapon", "clip", "reserve", "flags B"]
ENT = ["handle", "type", "flags A", "x", "y", "z", "pitch", "yaw", "roll", "health", "animation", "frame"]
# Entities that only draw (hud.md fades; impact sparks; particle bursts without parts; shell
# casings): compared for being there, not for their fields.
PRESENTATION = {0x32, 0x23, 0x66}


class Compare:
    def __init__(self, lines, maxn=20):
        self.orig = {d["tick"]: d for d in lines}
        self.maxn = maxn
        self.shown = 0
        self.first = {}                     # kind -> first tick it differed
        self.count = Counter()              # kind -> ticks differing
        self.ticks = 0
        self.identical = 0
        self.first_any = None
        self.examples = defaultdict(list)

    def kind(self, what, tick, detail):
        self.first.setdefault(what, tick)
        self.count[what] += 1
        if len(self.examples[what]) < 3:
            self.examples[what].append(f"tick {tick}: {detail}")

    def line(self, eng_line):
        t = eng_line["tick"]
        o = self.orig.get(t)
        if o is None:
            return
        self.ticks += 1
        kinds = set()
        diffs = []
        for k, (x, y) in enumerate(zip(eng_line.get("player", []), o.get("player", []))):
            if x != y:
                w = f"player {PLAYER[k]}"
                kinds.add(w)
                diffs.append(f"{w}: {x} / {y}")
        ea, eb = eng_line["ents"], o["ents"]
        ta = [e[1] for e in ea]
        tb = [e[1] for e in eb]
        if ta != tb:
            kinds.add("entity list (types in order)")
            sa, sb = Counter(ta), Counter(tb)
            diffs.append(f"list: engine only {dict(sa - sb)}, original only {dict(sb - sa)}")
        # entities matched by order within their type
        byt_a, byt_b = defaultdict(list), defaultdict(list)
        for e in ea:
            byt_a[e[1]].append(e)
        for e in eb:
            byt_b[e[1]].append(e)
        for typ in byt_b:
            for i, (u, v) in enumerate(zip(byt_a.get(typ, []), byt_b[typ])):
                if typ in PRESENTATION:
                    continue
                for k in range(1, 12):
                    if u[k] != v[k]:
                        w = f"type {typ:X} {ENT[k]}"
                        kinds.add(w)
                        diffs.append(f"{w} #{i}: {u[k]} / {v[k]}")
                if u[0] != v[0]:
                    kinds.add("handles")
        for w in kinds:
            self.kind(w, t, next((d for d in diffs if d.startswith(w) or w.startswith("entity list")), ""))
        if not kinds - {"handles"}:
            self.identical += 1
        elif self.first_any is None:
            self.first_any = t
        if diffs and self.shown < self.maxn:
            self.shown += 1
            print(f"tick {t}: " + "; ".join(diffs[:8]) + (" ..." if len(diffs) > 8 else ""))

    def report(self):
        print(f"\n{self.ticks} ticks compared, {self.identical} identical (handles aside); "
              f"first difference at tick {self.first_any}")
        for w, t in sorted(self.first.items(), key=lambda x: x[1]):
            print(f"  {w:<36} first at tick {t:>6}, {self.count[w]:>6} ticks   {self.examples[w][0]}")
