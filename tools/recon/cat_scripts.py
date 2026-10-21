#!/usr/bin/env python3
"""The level-script library across every level (docs/re/catalogue.md, Level scripts).

  cat_scripts.py BSD...            one line per script id: the levels that have it, how many
                                   distinct bodies, its handlers, the builtins it calls and who
                                   uses it (record kind/class, model type)
  cat_scripts.py --show ID BSD...  each distinct body of that script id, folded (lvs.py)

Script ids are global: the same id names the same behaviour in every level (most bodies are
byte-identical; some differ only in constants). Bodies are compared by their folded text.
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cat_lvl  # noqa: E402


def users(lv):
    out = collections.defaultdict(collections.Counter)
    for r in lv.records:
        tag = f"k{r['kind']}"
        if r["kind"] in (2, 3, 6) and "template" in r:
            m = lv.model_by_hash.get(r["template"][0])
            tag += f":{m['type']:#x}" if m else ":?"
        elif r["cls"] == 0x8877B646:
            tag += ":player"
        if r.get("script", -1) >= 0:
            out[r["script"]][tag] += 1
        if r["script54"] >= 0 and r["script54"] < 1000:
            out[r["script54"]][tag + "(+54)"] += 1
    return out


def main():
    args = sys.argv[1:]
    show = None
    if args[:1] == ["--show"]:
        show = int(args[1])
        args = args[2:]
    lib = collections.defaultdict(dict)
    use = collections.defaultdict(collections.Counter)
    for f in args:
        lv = cat_lvl.Level(f)
        for sid, s in lv.scripts.items():
            body = "\n".join(s["lines"])
            body = re.sub(r"#.*", "", body)
            lib[sid].setdefault(body, []).append(lv.name)
        for sid, c in users(lv).items():
            use[sid].update(c)
    if show is not None:
        for body, lvls in lib[show].items():
            print(f"--- script {show} in {' '.join(lvls)}")
            print(body)
        return
    for sid in sorted(lib):
        bodies = lib[sid]
        lvls = sorted({lv for v in bodies.values() for lv in v})
        first = next(iter(bodies))
        handlers = re.findall(r"on msg ([0-9A-F]+):", first)
        names = sorted({m for m in re.findall(r"\b([a-z_]+)\(", first) if m in
                        {b[0] for b in cat_lvl.lvs.BUILTINS.values()}})
        u = ", ".join(f"{k} {n}" for k, n in sorted(use[sid].items()))
        print(f"{sid:3} bodies {len(bodies)} levels {len(lvls):2} [{' '.join(lvls)}]\n"
              f"     msgs {' '.join(handlers)} | {' '.join(names)} | used by {u}")


if __name__ == "__main__":
    main()
