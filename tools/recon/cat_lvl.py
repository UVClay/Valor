#!/usr/bin/env python3
"""Catalogue a level from its BSD file alone (docs/re/catalogue.md).

  cat_lvl.py --ram DUMP BSD...           a report per level: header, records by class and kind,
                                         spawned types, models and their scripts, level scripts
                                         (builtins, objectives, messages and their sources)
  cat_lvl.py --ram DUMP --scripts BSD    the level scripts, folded (lvs.py on the file)
  cat_lvl.py --ram DUMP --check DUMP2 BSD  compare the file's script table with a RAM dump's

DUMP is any RAM dump with LEVEL.BIN loaded (e.g. build/re/mid_14000.bin): the message table
(0x80098188), the weapon table (0x80093640) and the pickup table (0x8009E8E8) are LEVEL.BIN's
own data, the same in every level; nothing from them is stored here.

The level scripts are read from the file: the table at header +0x59C (body-relative) is
position independent (script offsets from the table, code offsets from the script), so the
file is mapped into a fake RAM image and handed to lvs.py's decoder unchanged.
"""
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvs  # noqa: E402
import sst  # noqa: E402

BODY = 0x800
FAKE_BASE = 0x80100000

KNOWN_CLASSES = {
    0x4F292333: "soldier spawn", 0xB81C10E3: "trigger volume", 0xBD12927E: "pickup",
    0x4D078F23: "chunk streaming", 0x166A85FB: "object (kind 2)", 0x3011B913: "ladder",
    0xD31F988A: "object (kind 6)", 0x8877B646: "object (kind 2)", 0x413B09BF: "volume (kind 5)",
    0xD1BA7D1E: "level end",
}
TYPE_NAMES = {
    0x1402: "soldier", 0x1405: "dog", 0x1770: "destructible", 0x1771: "spinning prop",
    0x1772: "mounted gun", 0x1774: "rocket/vehicle", 0x1775: "scripted, box", 0x1776: "door/scripted solid",
    0x1777: "destructible, bullets pass", 0x1778: "scenery", 0x1779: "box", 0x177A: "inert solid",
    1: "weapon model", 0x65: "pickup",
}
LIST_NAMES = {0x38: "p_enter", 0x3C: "p_leave", 0x40: "c_enter", 0x44: "c_leave", 0x48: "tags",
              0x4C: "use", 0x50: "+50"}
PICKUP_KIND = {**{i: "health" for i in range(3)}, **{i: "ammo" for i in range(3, 15)},
               **{i: "item" for i in range(15, 20)}}
ITEM_MSG = {15: 0x2F, 16: 0x30, 17: 0x31, 18: 0x69, 19: 0x6A}


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def s16(d, o):
    return struct.unpack_from("<h", d, o)[0]


class Tables:
    """LEVEL.BIN's static tables, from a RAM dump."""

    def __init__(self, path):
        self.ram = lvs.Ram(path)
        r = self.ram
        self.texts = []
        i = 0
        while True:
            p = r.u32(0x80098188 + 4 * i)
            if not 0x80000000 <= p < 0x80200000:
                break
            a = p & 0x1FFFFF
            self.texts.append(r.d[a:r.d.index(b"\0", a)].decode("latin-1"))
            i += 1
        self.weapons = {r.u32(0x80093640 + 0x44 * k): k for k in range(13)}
        self.pickups = {r.u32(0x8009E8E8 + 20 * k): k for k in range(20)}
        self.enemy_weapons = {r.u32(0x80097FF0 + 48 * k): k for k in range(4)}

    def text(self, n):
        return self.texts[n] if 0 <= n < len(self.texts) else f"?{n}"


class FileRam(lvs.Ram):
    """A BSD body mapped at FAKE_BASE, with the script table pointer at 0x8009D5AC."""

    def __init__(self, d):
        img = bytearray(0x200000)
        body = d[BODY:]
        off = FAKE_BASE & 0x1FFFFF
        img[off:off + len(body)] = body[:0x200000 - off]
        struct.pack_into("<I", img, 0x9D5AC, FAKE_BASE + u32(d, BODY + 0x59C))
        self.d = bytes(img)


class Level:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path).rsplit(".", 1)[0]
        d = self.d = open(path, "rb").read()
        h = BODY
        self.tsp_count = u32(d, h + 0x80)
        self.slots = u32(d, h + 0x84)
        self.lights = u32(d, h + 0xD8)
        self.fog = s16(d, h + 0xCC)
        self.view = u32(d, h + 0x590)
        self.sky = d[h + 0x58C:h + 0x58F] != b"\0\0\0"
        self.moon = s16(d, h + 0x594) != 0 or s16(d, h + 0x596) != 0
        self.models = []
        nm = u32(d, h + 0x5A4)
        raw = [h + 0x5A8 + k * 0x100 for k in range(nm)]
        byhash = {u32(d, m): m for m in raw}
        for k, m in enumerate(raw):
            hsh = u32(d, m)
            base = byhash.get(u32(d, m + 0xE8)) if u32(d, m + 0xE8) != 0xFFFFFFFF else None
            typ = u32(d, m + 0xFC)
            if typ == 0xFFFFFFFF and base:
                typ = u32(d, base + 0xFC)
            sc = u32(d, m + 0xC)
            if not sc and base:
                sc = u32(d, base + 0xC)
            self.models.append(dict(k=k, hash=hsh, type=typ, base=u32(d, m + 0xE8), scripts=self.model_scripts(sc),
                                    hitparts=u16(d, m + 0xBE) if u32(d, m + 0xC0) else 0))
        self.model_by_hash = {m["hash"]: m for m in self.models}
        self.records = list(self.read_records())
        self.ram = FileRam(d)
        self.scripts = self.read_scripts()

    def model_scripts(self, off):
        if not off:
            return []
        d, b = self.d, BODY + off
        n = u16(d, b)
        out = []
        for k in range(n):
            e = b + 8 + 16 * k
            ln, code = u16(d, e + 0xA), u32(d, e + 0xC)
            words = [u32(d, b + code + 4 * j) for j in range(ln)]
            out.append(words)
        return out

    def read_records(self):
        d = self.d
        et = BODY + u32(d, BODY + 0x53C)
        n = u32(d, et)
        base = et + 16 + n * 8
        for i in range(n):
            r = base + u32(d, et + 16 + i * 8 + 4)
            kind = u32(d, r + 0xC)
            rec = dict(i=i, at=r, cls=u32(d, r), size=u32(d, r + 4), kind=kind,
                       pos=struct.unpack_from("<3h", d, r + 0x10), ang=struct.unpack_from("<3h", d, r + 0x18),
                       shape=struct.unpack_from("<4h", d, r + 0x28), w30=u32(d, r + 0x30),
                       script54=struct.unpack_from("<i", d, r + 0x54)[0], lists={})
            for o, name in LIST_NAMES.items():
                p = u32(d, r + o) if kind != 0 else 0  # kind 0 has no lists (0x8007529C)
                if p:
                    cnt = u32(d, r + p)
                    rec["lists"][name] = [u32(d, r + p + 4 + 4 * k) for k in range(min(cnt, 64))]
            if kind == 4:  # +0x60 is the pickup's hash itself (not relocated)
                rec["script"] = struct.unpack_from("<i", d, r + 0x64)[0]
                rec["template"] = [u32(d, r + 0x60)]
                t = 0
            elif kind in (2, 6):
                rec["script"] = struct.unpack_from("<i", d, r + 0x64)[0]
                t = u32(d, r + 0x60)
            elif kind == 3:
                rec["script"] = struct.unpack_from("<i", d, r + 0x78)[0]
                t = u32(d, r + 0x74)
            else:
                t = 0
            if t:
                cnt = u32(d, r + t)
                rec["template"] = [u32(d, r + t + 4 + 4 * k) for k in range(min(cnt, 8))]
            yield rec

    def section_end(self, table):
        """The nearest header section (or model data) after the script table, as a RAM address."""
        d, h = self.d, BODY
        offs = [u32(d, h + o) for o in (0x53C, 0x540, 0x598, 0x5A0)]
        offs += [u32(d, h + o) for o in range(0x544, 0x58C, 8)]
        for k in range(len(self.models)):
            m = h + 0x5A8 + k * 0x100
            offs += [u32(d, m + o) for o in (0x8, 0xC, 0x2C, 0x88, 0xC4)]
        t = table - FAKE_BASE
        nxt = min((o for o in offs if o > t and o != 0xFFFFFFFF), default=t + 0x1000)
        return FAKE_BASE + nxt

    @staticmethod
    def trim(code, entries):
        """Instructions up to the last 'end' that no jump or handler goes past."""
        reach = max(entries) if entries else 0
        out = []
        for ins in code:
            at, kind, v = ins
            if kind == "??" and at > reach:
                break
            out.append(ins)
            if kind in ("jz", "jmp"):
                reach = max(reach, v)
            if kind == "end" and at >= reach:
                break
        return out

    def read_scripts(self):
        ram = self.ram
        table = ram.u32(0x8009D5AC)
        count, slots = ram.u32(table), ram.u32(table + 4)
        self.msg_count = count
        offs = sorted({ram.u32(table + 8 + 4 * i) for i in range(slots)} - {0})
        out = {}
        for sid in range(slots):
            off = ram.u32(table + 8 + 4 * sid)
            if not off:
                continue
            b = table + off
            nxt = next((o for o in offs if o > off), None)
            n = ram.u32(b)
            entries = {}
            handlers = []
            for k in range(n):
                msg, code = ram.u16(b + 4 + 4 * k), ram.u16(b + 6 + 4 * k)
                entries.setdefault(code, msg)
                handlers.append(msg)
            start = min(entries) if entries else 4 + 4 * n
            if nxt:
                end = nxt - off
                code = lvs.decode(ram, b, start, end)
            else:  # the last script: up to the next section, cut after the last reachable 'end'
                code = lvs.decode(ram, b, start, self.section_end(table) - b)
                code = self.trim(code, set(entries))
            targets = {v for _, kind, v in code if kind in ("jz", "jmp")}
            lines = lvs.folded_lines(code, entries, targets)
            out[sid] = dict(handlers=handlers, code=code, lines=lines)
        return out


CALL = re.compile(r"(\w+)\(([^()]*(?:\([^()]*\)[^()]*)*)\)")


def calls(lines):
    """(handler msg, name, [args]) for each builtin call in folded lines."""
    msg = None
    for ln in lines:
        m = re.match(r"on msg ([0-9A-F]+):", ln)
        if m:
            msg = int(m.group(1), 16)
            continue
        for c in CALL.finditer(ln):
            name = c.group(1)
            if name in lvs.BUILTINS.values() or any(name == b[0] for b in lvs.BUILTINS.values()) or name.startswith("builtin"):
                args = [a.strip() for a in split_args(c.group(2))]
                yield msg, name, args


def split_args(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        depth += ch == "("
        depth -= ch == ")"
        cur += ch
    if cur.strip():
        out.append(cur)
    return out


def num(a):
    try:
        return int(a)
    except ValueError:
        return None


def model_ops(words_list):
    ops = collections.Counter()
    sends = set()
    for words in words_list:
        i = 0
        while i < len(words):
            w = words[i]
            op = w >> 24
            n = max(sst.length(op, words, i), 1)
            ops[op] += 1
            if op == 0x43:
                sends.add(tuple(words[i:i + n]))
            if op in (0x0D, 0x0E, 0x42):
                k = 1 + bin(w & 0xFF).count("1")
                if i + k < len(words):
                    ops[0x100 | (words[i + k] >> 24)] += 1  # stored instruction
            i += n
    return ops, sends


def report(lv, tb, out):
    p = out.append
    p(f"== {lv.name}: TSP {lv.tsp_count} (slots {lv.slots}), lights {lv.lights}, fog {lv.fog}, view {lv.view}, "
      f"sky {int(lv.sky)}, moon {int(lv.moon)}, models {len(lv.models)}, records {len(lv.records)}, "
      f"script slots {len(lv.scripts)}, messages {lv.msg_count}")
    # records by class
    by = collections.Counter((r["cls"], r["kind"], r["size"]) for r in lv.records)
    p("records (class kind size count):")
    for (c, k, s), n in sorted(by.items(), key=lambda x: (x[0][1], -x[1])):
        p(f"  {c:08X} k{k} 0x{s:X} x{n}  {KNOWN_CLASSES.get(c, 'UNKNOWN CLASS')}")
    # spawned types
    types = collections.Counter()
    tmodels = collections.defaultdict(set)
    pick = collections.Counter()
    for r in lv.records:
        if r["kind"] in (2, 3, 6) and "template" in r:
            m = lv.model_by_hash.get(r["template"][0])
            t = m["type"] if m else None
            types[t] += 1
            tmodels[t].add(r["template"][0])
        if r["kind"] == 4 and "template" in r:
            k = tb.pickups.get(r["template"][0])
            pick[(PICKUP_KIND.get(k, "?"), k)] += 1
    p("spawned types (by records): " + ", ".join(
        f"{t:#x} {TYPE_NAMES.get(t, 'UNDOCUMENTED?')} x{n} ({len(tmodels[t])} models)" if t is not None else f"no model x{n}"
        for t, n in sorted(types.items(), key=lambda x: (x[0] is None, x[0] or 0))))
    p("pickups: " + ", ".join(f"{k} {i} x{n}" for (k, i), n in sorted(pick.items(), key=lambda x: (x[0][1] is None, x[0][1] or 0))))
    # weapons (type-1 models)
    slots = [tb.weapons.get(m["hash"]) for m in lv.models if m["type"] == 1]
    p(f"weapon slots (type-1 models): {slots}")
    # all model types present
    mt = collections.Counter(m["type"] for m in lv.models)
    p("model types: " + ", ".join(f"{t:#x}x{n}" for t, n in sorted(mt.items())))
    # model scripts per spawned model
    p("model scripts (models with scripts):")
    for m in lv.models:
        if not m["scripts"]:
            continue
        ops, sends = model_ops(m["scripts"])
        unk = sorted(o for o in ops if o < 0x100 and (o not in sst.LENGTH))
        p(f"  model {m['k']:2} {m['hash']:08X} type {m['type']:#x} scripts {len(m['scripts'])} ops {len([o for o in ops if o < 0x100])}"
          + (f" UNKNOWN-LEN {['%02X' % o for o in unk]}" if unk else "")
          + (" send_event " + " ".join("/".join(f"{w:x}" for w in s) for s in sorted(sends)) if sends else ""))
        p("     ops: " + " ".join(f"{o:02X}" if o < 0x100 else f"s{o & 0xFF:02X}" for o in sorted(ops)))
    # record lists and special classes
    msgsrc = collections.defaultdict(list)
    for r in lv.records:
        for name, vals in r["lists"].items():
            if name == "tags":
                continue
            for v in vals:
                msgsrc[v].append(f"r{r['i']}.{name}(k{r['kind']})")
        if r["kind"] == 4 and "template" in r:
            k = tb.pickups.get(r["template"][0])
            if k in ITEM_MSG:
                msgsrc[ITEM_MSG[k]].append(f"r{r['i']}.item{k}")
    lists = collections.Counter()
    for r in lv.records:
        for name in r["lists"]:
            lists[(r["kind"], name)] += 1
    p("record lists (kind.list count): " + ", ".join(f"k{k}.{n} {c}" for (k, n), c in sorted(lists.items())))
    l50 = [(r["i"], r["kind"], r["lists"]["+50"]) for r in lv.records if "+50" in r["lists"]]
    if l50:
        p(f"  +0x50 lists: {l50[:6]}")
    shapes = collections.Counter((r["kind"], r["shape"][0]) for r in lv.records if r["kind"] in (0, 1, 5))
    p("volume shapes (kind,shape): " + ", ".join(f"{k}/{s} x{n}" for (k, s), n in sorted(shapes.items())))
    low = sum(1 for r in lv.records if 0xD in r["lists"].get("p_enter", []))
    hide = sum(1 for r in lv.records if 8 in r["lists"].get("p_enter", []))
    use = sum(1 for r in lv.records if "use" in r["lists"])
    p(f"zones: low(0xD enter) {low}, undetectable(8 enter) {hide}, use lists {use}")
    # scripts
    used = collections.defaultdict(list)
    sends = collections.defaultdict(set)
    for sid, s in lv.scripts.items():
        for msg, name, args in calls(s["lines"]):
            used[name].append((sid, msg, args))
            if name in ("send_self", "send") and num(args[0]) is not None:
                sends[num(args[0])].add(f"s{sid}.self")
            if name == "send_to" and len(args) == 3 and num(args[2]) is not None:
                sends[num(args[2])].add(f"s{sid}.to({args[0]})")
    for m, ss in sends.items():
        msgsrc[m].extend(sorted(ss))
    p("builtins: " + ", ".join(f"{n} {len(v)}" for n, v in sorted(used.items())))
    for name in ("level_set", "explosion", "level_func", "register_msg", "activate", "set_flags", "world_anim_step",
                 "world_anim_set", "has_tag", "tags_match", "set_home"):
        if name in used:
            vals = collections.Counter()
            for sid, msg, args in used[name]:
                if name == "level_set":
                    vals[f"{args[0]}={args[1]}" if len(args) > 1 else str(args)] += 1
                elif name == "explosion":
                    vals[args[0] if args else "?"] += 1
                elif name == "level_func":
                    vals[f"f{args[-1]}" if args else "?"] += 1
                elif name == "register_msg":
                    vals[f"filter{args[-2]}" if len(args) >= 2 else "?"] += 1
                elif name == "set_flags":
                    vals[args[0]] += 1
                else:
                    vals[len(args)] += 1
            p(f"  {name}: " + ", ".join(f"{k} x{v}" for k, v in sorted(vals.items(), key=str)))
    # objectives
    p("objectives:")
    for sid, msg, args in used.get("objective_add", []):
        t = num(args[0])
        p(f"  add s{sid} on {msg:X}: id {args[1]} text {t} '{tb.text(t) if t is not None else args[0]}'")
    for name in ("objective_complete", "objective_remove", "objective_is_complete"):
        for sid, msg, args in used.get(name, []):
            src = msgsrc.get(msg, [])
            p(f"  {name}({args[0]}) s{sid} on msg {msg:X} <- {src[:6]}{' ...' if len(src) > 6 else ''}")
    for sid, msg, args in used.get("level_set", []):
        if args and args[0] in ("2", "3"):
            src = msgsrc.get(msg, [])
            p(f"  level_set({', '.join(args)}) s{sid} on msg {msg:X} <- {src[:6]}")
    texts = sorted({num(a[0]) for _, _, a in used.get("show_text", []) if num(a[0]) is not None})
    p("show_text: " + "; ".join(f"{t} '{tb.text(t)}'" for t in texts))
    props = collections.Counter()
    for s in lv.scripts.values():
        for _, kind, v in s["code"]:
            if kind == "push" and isinstance(v, str):
                m = re.match(r"(\w+)\.p(?:rop)?(\d+|None)(:t\w+)?$", v)
                if m:
                    props[(m.group(1), m.group(2), m.group(3) or "")] += 1
            if kind == "??" or (kind == "push" and isinstance(v, str) and v.startswith("?")):
                props[("BADOP", str(v), "")] += 1
            if kind == "print":
                props[("print", str(v), "")] += 1
    p("properties: " + ", ".join(f"{a}.p{b}{c} x{n}" for (a, b, c), n in sorted(props.items())))
    handled = sorted({m for s in lv.scripts.values() for m in s["handlers"]})
    p("handled msgs: " + " ".join(f"{m:X}" for m in handled))
    nosrc = [m for m in handled if m not in msgsrc and m != 0]
    p("handled msgs with no record/script source: " + " ".join(f"{m:X}" for m in nosrc))
    # kind-3 templates
    tmpl = collections.Counter()
    for r in lv.records:
        if r["kind"] == 3 and "template" in r:
            t = r["template"]
            m = lv.model_by_hash.get(t[0])
            tmpl[(m["type"] if m else None, len(t), ["smg", "rifle", "pistol", "panzerfaust"][tb.enemy_weapons[t[1]]]
                  if len(t) > 1 and t[1] in tb.enemy_weapons else None)] += 1
    p("kind-3 templates (type, entries, enemy weapon): " + ", ".join(f"{k} x{n}" for k, n in sorted(tmpl.items(), key=str)))
    return msgsrc


def main():
    args = sys.argv[1:]
    ramp = args[args.index("--ram") + 1]
    args = args[:args.index("--ram")] + args[args.index("--ram") + 2:]
    tb = Tables(ramp)
    if args and args[0] == "--scripts":
        lv = Level(args[1])
        for sid, s in lv.scripts.items():
            print(f"script {sid}: handlers [{', '.join(f'{m:X}' for m in s['handlers'])}]")
            print("\n".join(s["lines"]))
            print()
        return
    if args and args[0] == "--check":
        dump = lvs.Ram(args[1])
        lv = Level(args[2])
        t_ram, t_file = dump.u32(0x8009D5AC), lv.ram.u32(0x8009D5AC)
        n = 8 + 4 * lv.ram.u32(t_file + 4)
        size = max(lv.ram.u32(t_file + 8 + 4 * i) for i in range(lv.ram.u32(t_file + 4))) + 0x400
        a = dump.d[t_ram & 0x1FFFFF:(t_ram & 0x1FFFFF) + size]
        b = lv.ram.d[t_file & 0x1FFFFF:(t_file & 0x1FFFFF) + size]
        diff = sum(x != y for x, y in zip(a, b))
        print(f"{lv.name}: table header {n} bytes, compared {size}: {diff} bytes differ")
        return
    for f in args:
        out = []
        report(Level(f), tb, out)
        print("\n".join(out))
        print()



def features(lv, tb):
    """One dict of per-level counts for the catalogue's tables."""
    f = collections.Counter()
    for r in lv.records:
        if r["kind"] in (2, 3, 6) and "template" in r:
            m = lv.model_by_hash.get(r["template"][0])
            if m:
                f[f"type {m['type']:#x}"] += 1
            if r["kind"] == 3:
                t = r["template"]
                w = tb.enemy_weapons.get(t[1]) if len(t) > 1 else None
                f[f"soldier weapon {w}"] += 1
                if len(t) == 4:
                    f["grenadier"] += 1
        if r["kind"] == 4 and "template" in r:
            k = tb.pickups.get(r["template"][0])
            f[f"pickup {PICKUP_KIND.get(k, '?')}"] += 1
            if k in ITEM_MSG:
                f[f"item {k}"] += 1
        if r["cls"] == 0x3011B913:
            f["ladder"] += 1
        if r["cls"] == 0x4D078F23:
            f["stream"] += 1
        if r["kind"] == 5:
            f["kind5"] += 1
        pe = r["lists"].get("p_enter", [])
        if 0xD in pe:
            f["low zone"] += 1
        if 8 in pe:
            f["hide zone"] += 1
        if "use" in r["lists"]:
            f["use list"] += 1
        if "c_enter" in r["lists"] or "c_leave" in r["lists"]:
            f["char list"] += 1
    f["weapons"] = [tb.weapons.get(m["hash"]) for m in lv.models if m["type"] == 1]
    for m in lv.models:
        if m["type"] == 0x1402 and m["scripts"]:
            ops, _ = model_ops(m["scripts"])
            if 0x49 in ops:
                f["alarm op"] = 1
    ids = set(lv.scripts)
    f["scripts"] = sorted(ids)
    for sid, s in lv.scripts.items():
        for msg, name, args in calls(s["lines"]):
            if name == "level_set" and args[:1] == ["4"]:
                f["level alarm"] += 1
    return f



if __name__ == "__main__":
    main()
