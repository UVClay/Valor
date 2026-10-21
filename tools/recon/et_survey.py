#!/usr/bin/env python3
"""Surveys a level's actors from a RAM dump (docs/re/enemy_types.md).

  et_survey.py RAMDUMP...            models (type, scripts), records by class and kind
  et_survey.py --soldiers RAMDUMP... the soldier records' fields (enemies.md, Spawning)
  et_survey.py --summary RAMDUMP...  soldier records grouped by body, weapon and part count
  et_survey.py --scripts RAMDUMP...  one line per typed model: script count, start, list hash

Models: [0x8009EC58], count [0x8009EC54], 0x100 bytes: +0x0 hash, +0x94 start script, +0xB8
scripts (16 bytes each: +0x4 weight, +0xA length, +0xC code), +0xBC count (u16), +0xFC type.
Records: [g_level+0x53C]: count, size, 2 words, then {state, record} pairs; a record starts
{class hash, size, 4, kind, x, y, z}; kind 2/6 template at +0x60, kind 3 at +0x74.
"""
import hashlib
import struct
import sys
from collections import Counter

TYPES = {0x1402: "soldier", 0x1405: "dog?", 0x1770: "destructible", 0x1772: "mounted gun",
         0x1774: "vehicle?", 0x1771: "timed fx", 0x1775: "scripted", 0x1776: "scripted solid",
         0x1777: "inert", 0x1778: "scenery", 0x1779: "box", 0x177A: "inert"}


class Ram:
    def __init__(self, path):
        self.d = open(path, "rb").read()

    def ok(self, a):
        return 0x80000000 <= a < 0x80200000

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFF)[0]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x1FFFFF)[0]

    def u8(self, a):
        return self.d[a & 0x1FFFFF]

    def s8(self, a):
        return struct.unpack_from("<b", self.d, a & 0x1FFFFF)[0]

    def blob(self, a, n):
        return self.d[a & 0x1FFFFF:(a & 0x1FFFFF) + n]


def level_name(r):
    lv = r.u32(0x8009CC64)
    s = r.blob(lv, 0x80).split(b"\0")[0].decode("latin1")
    return s.split("\\")[-1].replace("_c", "")


def models(r):
    n, base = r.u32(0x8009EC54), r.u32(0x8009EC58)
    out = []
    for i in range(n):
        m = base + i * 0x100
        h, t = r.u32(m), r.u32(m + 0xFC) & 0xFFFF
        cnt, lst, start = r.u16(m + 0xBC), r.u32(m + 0xB8), r.s32(m + 0x94)
        hs = hashlib.md5()
        if r.ok(lst):
            for k in range(cnt):
                e = lst + 16 * k
                ln, code = r.u16(e + 0xA), r.u32(e + 0xC)
                hs.update(struct.pack("<iH", r.s32(e + 4), ln))
                if r.ok(code):
                    hs.update(r.blob(code, 4 * ln))
        out.append(dict(i=i, addr=m, hash=h, type=t, n=cnt, start=start, list=lst,
                        md5=hs.hexdigest()[:8]))
    return out


def records(r):
    lv = r.u32(0x8009CC64)
    t = r.u32(lv + 0x53C)
    n = r.u32(t)
    out = []
    for i in range(n):
        rec = r.u32(t + 16 + 8 * i + 4)
        if not r.ok(rec):
            continue
        out.append(dict(i=i, addr=rec, state=r.u32(t + 16 + 8 * i), cls=r.u32(rec),
                        size=r.u32(rec + 4), kind=r.u32(rec + 0xC),
                        pos=(r.s16(rec + 0x10), r.s16(rec + 0x12), r.s16(rec + 0x14))))
    return out


def template_hashes(r, rec):
    if rec["kind"] in (2, 6):
        p = r.u32(rec["addr"] + 0x60)
        return [r.u32(p + 4)] if r.ok(p) else []
    if rec["kind"] == 3:
        p = r.u32(rec["addr"] + 0x74)
        if not r.ok(p):
            return []
        c = r.u32(p)
        return [r.u32(p + 4 + 4 * k) for k in range(min(c, 8))]
    return []


def survey(path):
    r = Ram(path)
    ms = models(r)
    bytype = {m["hash"]: m for m in ms}
    print(f"== {path}: {level_name(r)}")
    for m in ms:
        if m["type"] >= 0x1400:
            print(f"  model {m['i']:2} {m['hash']:08X} type {m['type']:04X} {TYPES.get(m['type'], ''):14}"
                  f" scripts {m['n']:3} start {m['start']:4} list {m['list']:08X} md5 {m['md5']}")
    recs = records(r)
    c = Counter((x["kind"], x["cls"]) for x in recs)
    for (k, cl), n in sorted(c.items()):
        tm = Counter()
        for x in recs:
            if x["kind"] == k and x["cls"] == cl:
                hs = template_hashes(r, x)
                if hs:
                    if k == 3:
                        tm[" ".join(f"{h:08X}" for h in hs)] += 1
                    else:
                        mm = bytype.get(hs[0])
                        tm[f"{hs[0]:08X}/{mm['type']:04X}" if mm else f"{hs[0]:08X}/?"] += 1
        extra = "; ".join(f"{k2} x{v}" for k2, v in tm.most_common())
        print(f"  kind {k} class {cl:08X} x{n} {extra}")


SOLDIER_FIELDS = [("5B", 0x5B, "u8"), ("5C", 0x5C, "u8"), ("5E", 0x5E, "u8"), ("60", 0x60, "u8"),
                  ("hp", 0x61, "u8"), ("62", 0x62, "u8"), ("aim", 0x63, "u8"), ("dmg", 0x64, "u8"),
                  ("fire%", 0x65, "u8"), ("66", 0x66, "u8"), ("68", 0x68, "s16"), ("6A", 0x6A, "s16"),
                  ("sight", 0x6C, "s16"), ("path", 0x6E, "s16"), ("hear", 0x70, "s16"),
                  ("78", 0x78, "s32")]


def soldier_summary(path):
    """Per (body model, weapon, part count): count and the sets of the main record fields."""
    r = Ram(path)
    groups = {}
    for x in records(r):
        if x["kind"] != 3:
            continue
        a = x["addr"]
        hs = template_hashes(r, x)
        key = (hs[0] if hs else 0, hs[1] if len(hs) > 1 else 0, len(hs))
        g = groups.setdefault(key, {"n": 0, "hp": set(), "aim": set(), "fire": set(), "dodge": set(),
                                    "skill": set(), "poll": set(), "sight": set(), "hear": set(),
                                    "fov": set(), "path": 0, "+34": set()})
        g["n"] += 1
        g["hp"].add(r.u8(a + 0x61))
        g["aim"].add(r.u8(a + 0x63))
        g["fire"].add(r.u8(a + 0x65))
        g["dodge"].add(r.u8(a + 0x5C))
        g["skill"].add(r.u8(a + 0x5B))
        g["poll"].add(r.u8(a + 0x66))
        g["sight"].add(r.s16(a + 0x6C))
        g["hear"].add(r.s16(a + 0x70))
        g["fov"].add(r.s16(a + 0x6A))
        g["+34"].add(r.s16(a + 0x34))
        g["path"] += r.s16(a + 0x6E) != 0
    print(f"== {level_name(r)}")
    fmt = lambda v: ",".join(map(str, sorted(v)))
    for (b, w, n), g in sorted(groups.items(), key=lambda kv: -kv[1]["n"]):
        print(f"  {b:08X} {w:08X} parts {n}: x{g['n']} hp {fmt(g['hp'])} aim {fmt(g['aim'])} fire% {fmt(g['fire'])}"
              f" dodge {fmt(g['dodge'])} skill {fmt(g['skill'])} poll {fmt(g['poll'])} sight {fmt(g['sight'])}"
              f" hear {fmt(g['hear'])} fov {fmt(g['fov'])} on-path {g['path']} +34 {fmt(g['+34'])}")


def soldiers(path):
    r = Ram(path)
    print(f"== {path}: {level_name(r)}")
    rows = Counter()
    for x in records(r):
        if x["kind"] != 3:
            continue
        a = x["addr"]
        vals = []
        for name, off, t in SOLDIER_FIELDS:
            v = {"u8": r.u8, "s16": r.s16, "s32": r.s32}[t](a + off)
            vals.append(f"{name}={v}")
        hs = template_hashes(r, x)
        vals.append("tmpl=" + ",".join(f"{h:08X}" for h in hs))
        rows[" ".join(vals)] += 1
    for k, v in sorted(rows.items()):
        print(f"  x{v:2} {k}")


def scripts(path):
    r = Ram(path)
    lv = level_name(r)
    seen = set()
    for m in models(r):
        if m["type"] >= 0x1400 and m["n"] and (m["type"], m["md5"]) not in seen:
            seen.add((m["type"], m["md5"]))
            print(f"{lv:5} {m['type']:04X} {m['hash']:08X} n={m['n']:3} start={m['start']:4} md5={m['md5']}")


def main():
    a = sys.argv[1:]
    f = survey
    if a and a[0] == "--soldiers":
        f, a = soldiers, a[1:]
    elif a and a[0] == "--summary":
        f, a = soldier_summary, a[1:]
    elif a and a[0] == "--scripts":
        f, a = scripts, a[1:]
    for p in a:
        f(p)


if __name__ == "__main__":
    main()
