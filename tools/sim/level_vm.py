#!/usr/bin/env python3
"""A reference reimplementation of the level logic (docs/re/levels.md "Level logic: messages
and scripts", "Spawning", "Triggers", "Objectives"; docs/re/messages.md), checked frame by
frame against the original.

  level_vm.py watch                          the VALOR_WATCH list the check needs
  level_vm.py check LOG TRACE DUMP@VBLANK[,LATER@VBLANK]... [--level M:L] [--max N] [--rebase DUMP@VBLANK]...

tools/sim/check.py runs it on any recording. LOG and TRACE come from one replay, e.g. of the
recorded session:
  VALOR_WATCH="$(python3 tools/sim/level_vm.py watch)" VALOR_TRACE_DAMAGE=1 \
  VALOR_TRACE_STATE=build/lsim/tr.jsonl RP_BIN=build/valor-wave3 tools/recon/run.sh lsfull 900 \
      build/lsim/W.log --replay recordings/2026-10-19_22-22-43_m1l3 --hidden
and two RAM dumps per level, one before the level's first tick and one inside it (1:3: 1712
and 2000, 2:1: 20480 and 20600, 2:2: 40720 and 40800; --dump-ram V:build/lsim/dV.bin, as many
as wanted in one run); the level is the one the first dump holds (0x80038660):
  level_vm.py check build/lsim/W.log build/lsim/tr.jsonl build/lsim/d1712.bin@1712,build/lsim/d2000.bin@2000 \
      build/lsim/d20480.bin@20480,build/lsim/d20600.bin@20600 build/lsim/d40720.bin@40720,build/lsim/d40800.bin@40800
The first dump gives the level's
static data (records, scripts, zone lists, models, AI nodes); the record variables and the
scripts' own bytes are put back to the BSD file's values (extracted disc), so the level start
itself is simulated: the player's message 0, the states cleared, message 0 to every record.
The second fills in the watched words that never change after the first frame (a watch logs
changes only) and checks the variables of every record-spawned entity alive in it.
LVM_DEBUG=V0-V1 prints what the simulator does in those frames; LVM_NODES=V the first
subscription node where the two pools part at frame V.

What the simulator owns and steps (the level logic): the level-script VM (24 opcodes, the 22
built-ins), subscriptions and their node pool (the same free list, so node addresses compare),
sending with the five filters, the 128-entry queue, record state bits, activation and the spawn
queue, the player's and the characters' trigger volumes (zone bits, enter/leave lists, Use
lists, kind-0 classes), objectives, level_set, entity-context variables and set_flags.

What it takes from the original as the engine's events (as the other simulators take the world):
positions and the entity list per tick (the trace), the zone id under each walker (+0x390, the
floor's leaf: levels.md Collision), the pad (Use), hits (VALOR_TRACE_DAMAGE), removals (dead,
culled, picked up), the 1/3-second counter, alarms raised by model scripts, messages sent by
model scripts (op 0x43, recognised in the queue), and where a spawn's entity lands (handle and
address) and whether the soldier cap refused it.

Compared at every frame's watch point (the start of the drawing pass: the frame's ticks are
done, its queue not yet run): every record's state word and variable bytes, every script's own
bytes, every subscription list (node addresses and contents), the queued contexts, the
objectives (ids, texts, completion flags, count, primary flag), the level-end flags, the spawn
queue, the player's and the five character slots' variables (+0x20..+0x27) and zone bits.
"""
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
from combat_sim import rot_yxz  # noqa: E402  (RotMatrixYXZ from the exe's table)

G_LEVEL = 0x8009CC64
G_SCRIPTS = 0x8009D5AC
G_SUBS = 0x8009D5B0
G_FREE = 0x8009D5B4
G_HEAD = 0x8009D5B8
G_TAIL = 0x8009D5BC
RING = 0x800B6460
POOL = 0x800AD460
OBJ_COUNT = 0x80098620
OBJ_PRIMARY = 0x80098624
OBJ_DONE = 0x800988DC
OBJ_TEXT = 0x80098904
OBJ_ID = 0x8009892C
MSG_TEXTS = 0x80098188
LEVEL_FLAGS = 0x8009C840
SPQ_HEAD, SPQ_TAIL, SPQ_COUNT, SPQ = 0x8009E280, 0x8009E284, 0x8009E288, 0x8009E294
G_WORLD = 0x8009D648
G_ZONES = 0x8009EC5C
G_MODELS, G_NMODELS = 0x8009EC58, 0x8009EC54
G_ALARM = 0x80097FE0
G_SOLDIER = 0x8009D654
PAD_NOW, PAD_PREV = 0x80093B7C, 0x80093B84
ACT_MASK, ACT_EXCL = 0x800B7790, 0x800B77E0
SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]
OBJECTIVE_PICKUPS = {0x25C50BD4: 0x2F, 0x933987E0: 0x30, 0x96593A51: 0x31,
                     0x4A5B62D4: 0x69, 0xD352336E: 0x6A}
USE_BIT = 0x08
MODEL_SENDS = {0x1B, 4, 0x1E, 0x33, 0x75, 0x74}   # op 0x43 literals on the disc (messages.md)

DEAD, REMOVING, PLAYER = 0x20000000, 0x80000, 1


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


class Mem:
    def __init__(self, data):
        self.d = bytearray(data)

    def u8(self, a):
        return self.d[a & 0x1FFFFF]

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x1FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x1FFFFF)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFF)[0]

    def w8(self, a, v):
        self.d[a & 0x1FFFFF] = v & 0xFF

    def w32(self, a, v):
        struct.pack_into("<I", self.d, a & 0x1FFFFF, v & 0xFFFFFFFF)

    def blk(self, a, n):
        return bytes(self.d[a & 0x1FFFFF:(a & 0x1FFFFF) + n])


# ---------------------------------------------------------------- the watch list

def watch_string():
    items = ["rec=[[8009CC64]+53C]+0:4000", "scr=[8009D5AC]+0:3400", "subh=[8009D5B0]+0:280",
             "pool=800AD460:9000", "ring=800B6460:1000", "qi=8009D5B8:8", "obj=800988DC:90",
             "objn=80098620:8", "lvf=8009C840:4", "tmr=8009A5C0:4", "tick=8009A5DC:4",
             "spq=8009E280:114", "hnd=8009D670:408", "alarm=80097FE0:8", "pad=80093B7C:10",
             "p=[8009D654]+0:44", "pz=[8009D654]+390:10"]
    for i, a in enumerate(SLOTS):
        items += [f"c{i}={a:X}:44", f"z{i}={a:X}+390:10"]
    return ";".join(items)


WATCH_RE = re.compile(r"^watch (\d+) (\w+)\+([0-9A-F]+): ([0-9A-F]+) -> ([0-9A-F]+)")
BASE_RE = re.compile(r"^watch (\d+) (\w+) at ([0-9A-F]+)")
DMG_RE = re.compile(r"^damage (\d+): victim ([0-9A-F]+) type ([0-9A-F]+) part (-?\d+), "
                    r"attacker ([0-9A-F]+) type ([0-9A-F]+).*?( dead)?$")


class WatchLog:
    """Per item, segments {base, start frame, diffs {offset: [(frame, old, new)]}}."""

    def __init__(self, path):
        self.items = defaultdict(list)
        self.damage = []
        self.lengths = {}
        for line in open(path, errors="replace"):
            if line.startswith("watch: "):
                m = re.match(r"watch: (\w+) = .*, 0x([0-9A-F]+) bytes", line)
                if m:
                    self.lengths[m.group(1)] = int(m.group(2), 16)
                continue
            m = WATCH_RE.match(line)
            if m:
                f, name, off, old, new = m.groups()
                self.items[name][-1]["diffs"][int(off, 16)].append((int(f), int(old, 16), int(new, 16)))
                continue
            m = BASE_RE.match(line)
            if m:
                f, name, base = m.groups()
                self.items[name].append({"base": int(base, 16), "start": int(f), "diffs": defaultdict(list)})
                continue
            m = DMG_RE.match(line)
            if m:
                g = m.groups()
                self.damage.append((int(g[0]), int(g[1], 16), int(g[2], 16), int(g[3]), int(g[4], 16), int(g[5], 16),
                                    bool(g[6])))

    def frames(self, lo, hi):
        """Watch frames in [lo, hi) with the g_tick at each."""
        seg = self.items["tick"][0]
        out = []
        ds = seg["diffs"][0]
        for f, old, new in ds:
            if lo <= f < hi:
                out.append((f, new))
        first_after = [d for d in ds if d[0] >= lo]
        if first_after and (not out or out[0][0] != lo):
            out.insert(0, (lo, first_after[0][1]))
        return out


class Observed:
    """The original's memory at each watch frame of one level: the level's dump with every
    watched word set to its value at that frame."""

    def __init__(self, log, dump, f0, f1, later=None, rebase=()):
        """later: a second dump taken inside the level, for the words that never change after
        the first watch frame (a watch logs changes only, and the first frame's ticks have
        already run when it takes its first copy). rebase: [(vblank, dump)] taken after a
        pointer watch moved inside the level (a restart reallocates the subscription heads):
        the moved block's first copy is the first such dump after the move."""
        self.mem = Mem(dump)
        lm = Mem(later) if later else None
        self.events = defaultdict(list)       # frame -> [(addr, new)]
        self.timeline = defaultdict(list)     # addr -> [(frame, new)]
        self.now = f0
        pointers = {}
        for spec in watch_string().split(";"):
            m = re.match(r"(\w+)=\[([0-9A-F]+)\]\+0:", spec)
            if m:
                pointers[m[1]] = int(m[2], 16)
        for name, segs in log.items.items():
            seg = None
            for s in segs:
                if s["start"] <= f0:
                    seg = s
            # blocks moved inside the level: the pointer, the first copy, the changes after
            for s in segs:
                if not (f0 < s["start"] < f1) or name not in pointers:
                    continue
                rd = next((Mem(d) for v, d in sorted(rebase) if v > s["start"]), None)
                ev = [(pointers[name], s["base"])]
                if rd is not None:
                    ev += [(s["base"] + off, rd.u32(s["base"] + off)) for off in range(0, log.lengths.get(name, 0), 4)]
                for f, (a, v) in [(s["start"], e) for e in ev] + [
                        (f, (s["base"] + off, new)) for off, ds in s["diffs"].items() for f, old, new in ds if f < f1]:
                    self.events[f].append((a, v))
                    self.timeline[a].append((f, v))
            if seg is None:
                continue
            base = seg["base"]
            if lm is not None:
                for off in range(0, log.lengths.get(name, 0), 4):
                    if not any(f0 <= d[0] < f1 for d in seg["diffs"].get(off, ())):
                        self.mem.w32(base + off, lm.u32(base + off))
            for off, ds in seg["diffs"].items():
                before = [d for d in ds if d[0] <= f0]
                after = [d for d in ds if f0 < d[0] < f1]
                if before:
                    self.mem.w32(base + off, before[-1][2])
                elif after:
                    self.mem.w32(base + off, after[0][1])
                for f, old, new in after:
                    self.events[f].append((base + off, new))
                    self.timeline[base + off].append((f, new))

        for tl in self.timeline.values():
            tl.sort(key=lambda x: x[0])

    def peek(self, addr, frame):
        """The word at addr at a later watch frame (without advancing)."""
        tl = self.timeline.get(addr)
        if tl:
            import bisect
            i = bisect.bisect_right(tl, (frame, 1 << 33)) - 1
            if i >= 0 and tl[i][0] > self.now:
                return tl[i][1]
        return self.mem.u32(addr)

    def advance(self, frame):
        self.now = frame
        for a, v in self.events.get(frame, ()):
            self.mem.w32(a, v)


def read_trace(path, lo, hi):
    """Ticks with vblank in [lo, hi): (tick, vblank, {handle: (type, flags, x, y, z)}, order)."""
    import json
    out = []
    for line in open(path):
        i = line.index(',"vblank":')
        j = line.index(',', i + 10)
        v = int(line[i + 10:j])
        if v < lo:
            continue
        if v >= hi:
            break
        d = json.loads(line)
        ents = {e[0]: (e[1], e[2], e[3], e[4], e[5]) for e in d["ents"]}
        out.append((d["tick"], v, ents, [e[0] for e in d["ents"]]))
    return out


# ---------------------------------------------------------------- the level's data

class Record:
    def __init__(self, mem, idx, pair):
        self.idx, self.pair = idx, pair
        self.addr = r = mem.u32(pair + 4)
        self.cls = mem.u32(r)
        self.kind = mem.u32(r + 0xC)
        self.pos = (mem.s16(r + 0x10), mem.s16(r + 0x12), mem.s16(r + 0x14))
        self.ang = (mem.s16(r + 0x18), mem.s16(r + 0x1A), mem.s16(r + 0x1C))
        if self.kind == 0:
            self.lists = {}
            self.shape = (mem.s16(r + 0x28), mem.s16(r + 0x2A), mem.s16(r + 0x2C), mem.s16(r + 0x2E))
            self.script = -1
            return
        self.shape = (mem.s16(r + 0x28), mem.s16(r + 0x2A), mem.s16(r + 0x2C), mem.s16(r + 0x2E))
        self.lists = {}
        for name, off in (("enter", 0x38), ("leave", 0x3C), ("c_enter", 0x40), ("c_leave", 0x44),
                          ("tags", 0x48), ("use", 0x4C)):
            p = mem.u32(r + off)
            self.lists[name] = None if p == 0 else [mem.u32(p + 4 + 4 * k) for k in range(mem.u32(p))]
        self.script = s32(mem.u32(r + 0x54))
        self.flags30 = mem.u32(r + 0x30)
        self.p34 = mem.s16(r + 0x34)
        if self.kind in (2, 6):
            self.ent_script = s32(mem.u32(r + 0x64))
            self.model = mem.u32(mem.u32(r + 0x60) + 4)
        elif self.kind == 3:
            self.ent_script = s32(mem.u32(r + 0x78))
            self.model = mem.u32(mem.u32(r + 0x74) + 4)
        elif self.kind == 4:
            self.ent_script = s32(mem.u32(r + 0x64))
            self.model = None
            self.pickup = mem.u32(r + 0x60)
        else:
            self.ent_script = -1
            self.model = None


class Level:
    def __init__(self, dump, name):
        self.name = name
        m = self.mem = Mem(dump)
        self.g_level = m.u32(G_LEVEL)
        table = m.u32(self.g_level + 0x53C)
        self.records = [Record(m, i, table + 16 + 8 * i) for i in range(m.u32(table))]
        self.by_pair = {r.pair: r for r in self.records}
        self.scripts = m.u32(G_SCRIPTS)
        self.nmsg = m.u32(self.scripts)
        self.zones = m.u32(G_ZONES)
        self.models = {}
        for i in range(m.u32(G_NMODELS)):
            a = m.u32(G_MODELS) + 0x100 * i
            self.models[m.u32(a)] = m.u32(a + 0xFC)
        wo = m.u32(m.u32(G_WORLD) + 0x388)
        self.nodes = [(m.u32(wo + 8) + 0x34 * k) for k in range(m.u32(wo + 0xC))]

    def zone_records(self, zone):
        if zone == 0xFFFF:
            return []
        p = self.mem.u32(self.zones + 4 * zone)
        n = 0xFF - self.mem.u8(p)
        return [self.mem.u16(p + 2 + 2 * k) for k in range(n)]

    def spawn_type(self, rec):
        if rec.kind == 4:
            return 0x65
        return self.models.get(rec.model)


# ---------------------------------------------------------------- volumes (record_contains)

def contains(rec, p):
    shape, a, b, c = rec.shape
    cx, cy, cz = rec.pos
    if shape == 0:      # sphere: the GTE squares s16 differences
        d = [s16(p[0] - cx), s16(p[1] - cy), s16(p[2] - cz)]
        return s32(a * a - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) > 0
    if shape == 1:      # cylinder
        lo, hi = -b, -c
        if hi < lo:
            lo, hi = hi, lo
        dy = p[1] - cy
        if dy < lo or hi < dy:
            return False
        dx, dz = p[0] - cx, p[2] - cz
        if a < -dx or a < dx or a < -dz or a < dz:
            return False
        return s32(a * a - (dx * dx + dz * dz)) > 0
    if shape == 2:      # box: an axis-aligned pre-test, then the turned one
        hx, hy, hz = abs(a) >> 1, abs(b) >> 1, abs(c) >> 1
        dx, dy, dz = abs(p[0] - cx), abs(p[1] - cy), abs(p[2] - cz)
        if hx < dx or hy < dy or hz < dz:
            return False
        ax, ay, az = (s16(-rec.ang[0]), s16(-rec.ang[1]), s16(-rec.ang[2]))
        if ax == 0 and ay == 0 and az == 0:
            return True
        m = rot_yxz(ax, ay, az)
        v = [s16(cx - p[0]), s16(cy - p[1]), s16(cz - p[2])]
        r = [abs((m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2]) >> 12) for i in range(3)]
        return not (hx < r[0] or hy < r[1] or hz < r[2])
    return False


# ---------------------------------------------------------------- entities

class Ent:
    """An entity as the level logic sees it."""
    _n = 0

    def __init__(self, handle, etype, rec=None, addr=None):
        self.handle, self.type, self.rec, self.addr = handle, etype, rec, addr
        self.vars = bytearray(8)
        self.flags40 = 0
        self.flags = 0            # flags A, from the trace
        self.pos = (0, 0, 0)
        self.removed = False
        self.zone = None          # +0x390 (this tick), +0x398/+0x39C (last tick)
        self.bits = 0
        self.prev_zone = 0xFFFF   # a new entity: "no zone" (as the original starts them)
        self.prev_bits = 0
        Ent._n += 1
        self.serial = Ent._n

    def __repr__(self):
        r = f" r{self.rec.idx}" if self.rec else ""
        return f"<{self.handle:08X} {self.type:X}{r}>"


# ---------------------------------------------------------------- the simulator

class Ctx:
    __slots__ = ("pc", "sub", "sender", "actor", "sub_obj", "sender_obj", "msg", "script", "src")

    def __init__(self, pc, sub, sender, actor, sub_obj, sender_obj, msg, script, src=""):
        self.pc, self.sub, self.sender, self.actor = pc, sub, sender, actor
        self.sub_obj, self.sender_obj, self.msg, self.script = sub_obj, sender_obj, msg, script
        self.src = src

    def words(self):
        h = lambda e: e.handle if e else 0
        return (self.pc, h(self.sub), h(self.sender), h(self.actor), self.sub_obj, self.sender_obj,
                self.msg, self.script)


class Sim:
    def __init__(self, lvl, bsd, log=print):
        self.L = lvl
        self.m = Mem(lvl.mem.d)          # the simulator's memory (level logic regions)
        self.log = log
        self.calls = Counter()
        self.handles = {}                # handle -> Ent
        self.player = None
        self.queue = []                  # this frame's contexts (the ring between head and tail)
        self.head = self.tail = 0
        self.spawn_q = []                # record pairs
        self.spq_head = 0                # the ring's head index (0x8009E280)
        self.pending_spawns = []         # (record, Ent) spawned during the end of a frame
        self.oracle_spawn = None         # callback(record) -> Ent or None (refused)
        self.stats = Counter()
        self.debug = False
        self.ladder_held = 0                 # 0x8009A5B8
        self.restore_bsd(bsd)

    def dbg(self, *a):
        if self.debug:
            print("   ", *a)

    # -- level start state
    def restore_bsd(self, bsd):
        """The record variables and the scripts' bytes as on the disc (the BSD is loaded
        contiguously from g_level - 0x800)."""
        base = self.L.g_level - 0x800
        m = self.m
        for r in self.L.records:
            fo = r.addr - base
            m.d[(r.addr + 0x20) & 0x1FFFFF:(r.addr + 0x28) & 0x1FFFFF] = bsd[fo + 0x20:fo + 0x28]
        st = self.L.scripts
        nslots = m.u32(st + 4)
        for sid in range(nslots):
            off = m.u32(st + 8 + 4 * sid)
            if not off:
                continue
            b = st + off
            n = m.u32(b)
            code = min(m.u16(b + 6 + 4 * k) for k in range(n)) if n else 4
            fo = b - base
            m.d[(b + 4 + 4 * n) & 0x1FFFFF:(b + code) & 0x1FFFFF] = bsd[fo + 4 + 4 * n:fo + code]
        for r in self.L.records:
            m.w32(r.pair, 0)

    def level_start(self, player):
        m = self.m
        # msg_lists_init: empty lists, the free list in pool order; the queue empty
        for i in range(self.L.nmsg):
            m.w32(m.u32(G_SUBS) + 4 * i, 0)
        for k in range(1024):
            m.w32(POOL + 0x24 * k + 0x20, POOL + 0x24 * (k + 1) if k < 1023 else 0)
        m.w32(G_FREE, POOL)
        self.head = self.tail = 0
        m.w32(OBJ_COUNT, 0)
        self.spawn_q = []
        self.spq_head = 0
        self.player = player
        self.handles[player.handle] = player
        self.msg_start(player, 0)                       # msg_start(g_soldier, 0)
        for r in self.L.records:                        # records_clear_state
            m.w32(r.pair, m.u32(r.pair) & ~3)
        for r in self.L.records:                        # records_start_scripts
            if 1 <= r.kind <= 6:
                self.msg_start(None, r.pair)

    # -- scripts
    def script_of(self, ent, obj):
        if obj:
            return s32(self.m.u32(self.m.u32(obj + 4) + 0x54))
        if ent is None or ent.rec is None:
            return -1
        r = ent.rec
        return r.ent_script if r.kind in (2, 3, 4, 6) else -1

    def script_base(self, sid):
        return self.L.scripts + self.m.u32(self.L.scripts + 8 + 4 * sid)

    def handler(self, sid, k):
        b = self.script_base(sid)
        if k >= self.m.u32(b):
            return None
        return self.m.u16(b + 4 + 4 * k), b + self.m.u16(b + 6 + 4 * k)

    def run_message(self, ent, obj, msg, why=""):
        """script_run_message: the handler for msg in the script, run now (only the first
        handler for the message, as the loop returns after it runs)."""
        if ent is None and obj == 0:
            return
        sid = self.script_of(ent, obj)
        if sid == -1:
            return
        b = self.script_base(sid)
        for k in range(self.m.u32(b)):
            if self.m.u16(b + 4 + 4 * k) == msg:
                self.stats[f"direct {msg:X}"] += 1
                self.dbg(f"direct {msg:X} to s{sid} {ent} {obj:X} ({why})")
                self.vm(Ctx(b + self.m.u16(b + 6 + 4 * k), ent, None, None, obj, 0, msg, sid, why))
                return

    def msg_start(self, ent, obj):
        self.run_message(ent, obj, 0, "start")

    # -- subscriptions
    def nodes(self, msg):
        out, n = [], self.m.u32(self.m.u32(G_SUBS) + 4 * msg)
        while n:
            out.append(n)
            n = self.m.u32(n + 0x20)
        return out

    def node_ent(self, n, off=0):
        h = self.m.u32(n + off)
        return self.handles.get(h) if h else None

    def same(self, n, off, ent):
        """handle_same: the node's handle against an entity (dead handles are equal to 0)."""
        h = self.m.u32(n + off)
        if ent is None:
            return False
        return h == ent.handle

    def unregister(self, msg, ent, obj, filt, fent, fobj, val):
        m = self.m
        head = m.u32(G_SUBS) + 4 * msg
        prev = 0
        n = m.u32(head)
        while n:
            nxt = m.u32(n + 0x20)
            hit = False
            if (ent is not None and self.same(n, 0, ent)) or (obj and obj == m.u32(n + 4)):
                if filt == 0:
                    hit = True
                elif m.u32(n + 8) == filt:
                    if filt in (1, 2):
                        hit = s32(m.u32(n + 0x14)) == val
                    elif filt in (3, 4):
                        hit = (fent is not None and self.same(n, 0xC, fent)) or (fobj and m.u32(n + 0x10) == fobj)
                    elif filt == 5:
                        hit = s32(m.u32(n + 0x14)) == val and (
                            (fent is not None and self.same(n, 0xC, fent)) or (fobj and m.u32(n + 0x10) == fobj))
            if hit:
                if prev:
                    m.w32(prev + 0x20, nxt)
                else:
                    m.w32(head, nxt)
                m.w32(n + 0x20, m.u32(G_FREE))
                m.w32(G_FREE, n)
            else:
                prev = n
            n = nxt

    def register(self, msg, ent, obj, filt, fent, fobj, val, pc, sid):
        m = self.m
        h = lambda e: e.handle if e else 0
        if filt == 0:
            self.unregister(msg, ent, obj, 0, None, 0, 0)
        else:
            for n in self.nodes(msg):
                if not ((ent is not None and self.same(n, 0, ent)) or (obj and obj == m.u32(n + 4))):
                    continue
                nf = m.u32(n + 8)
                if nf == 0:      # the subscriber's filter-0 node becomes this one
                    m.w32(n + 8, filt)
                    m.w32(n + 0xC, h(fent))
                    m.w32(n + 0x10, fobj)
                    m.w32(n + 0x14, val)
                    m.w32(n + 0x18, pc)
                    m.w32(n + 0x1C, sid)
                    return
                if nf != filt:
                    continue
                if filt in (1, 2) and s32(m.u32(n + 0x14)) == val:
                    return
                if filt in (3, 4) and ((fent is not None and self.same(n, 0xC, fent)) or (fobj and m.u32(n + 0x10) == fobj)):
                    return
                if filt == 5 and s32(m.u32(n + 0x14)) == val and (
                        (fent is not None and self.same(n, 0xC, fent)) or (fobj and m.u32(n + 0x10) == fobj)):
                    return
        n = m.u32(G_FREE)
        if not n:
            self.stats["pool empty"] += 1
            return
        m.w32(G_FREE, m.u32(n + 0x20))
        head = m.u32(G_SUBS) + 4 * msg
        for off, v in ((0, h(ent)), (4, obj), (8, filt), (0xC, h(fent)), (0x10, fobj), (0x14, val),
                       (0x18, pc), (0x1C, sid), (0x20, m.u32(head))):
            m.w32(n + off, v)
        m.w32(head, n)

    def drop_subscriptions(self, ent):
        """msg_entity_removed's second half: every node whose subscriber or filter entity is ent."""
        m = self.m
        for msg in range(self.L.nmsg):
            head = m.u32(G_SUBS) + 4 * msg
            prev, n = 0, m.u32(head)
            while n:
                nxt = m.u32(n + 0x20)
                if self.same(n, 0, ent) or self.same(n, 0xC, ent):
                    if prev:
                        m.w32(prev + 0x20, nxt)
                    else:
                        m.w32(head, nxt)
                    m.w32(n + 0x20, m.u32(G_FREE))
                    m.w32(G_FREE, n)
                else:
                    prev = n
                n = nxt

    # -- records and tags
    def rec_of(self, ent, obj):
        if ent is not None:
            return ent.rec.pair if ent.rec else 0
        return obj

    def has_tag(self, ent, obj, val):
        pair = self.rec_of(ent, obj)
        if not pair:
            return 0
        tags = self.L.by_pair[pair].lists.get("tags") or []
        return int(any((t & 0xFF) == (val & 0xFF) for t in tags))

    def tags_match(self, e1, o1, e2, o2, typ):
        p1, p2 = self.rec_of(e1, o1), self.rec_of(e2, o2)
        if not p1 or not p2:
            self.stats["tags_match without a record"] += 1
            return 0
        t1 = [t for t in (self.L.by_pair[p1].lists.get("tags") or []) if t >> 16 == typ]
        t2 = [t for t in (self.L.by_pair[p2].lists.get("tags") or []) if t >> 16 == typ]
        return int(bool(set(t1) & set(t2)))

    # -- sending
    def send(self, msg, sender, sender_obj, actor, target=0, src=""):
        if msg >= self.L.nmsg:
            self.stats["invalid message"] += 1
            return
        m = self.m
        for n in self.nodes(msg):
            filt = m.u32(n + 8)
            if filt == 0:
                ok = True
            elif filt in (1, 4):
                pair = sender_obj or (sender.rec.pair if sender and sender.rec else 0)
                ok = bool(pair) and self.L.by_pair[pair].p34 == s32(m.u32(n + 0x14))
            elif filt == 2:
                ok = bool(self.has_tag(sender, sender_obj, m.u32(n + 0x14)))
            elif filt == 3:
                ok = (sender is not None and self.same(n, 0xC, sender)) or (
                    sender_obj and m.u32(n + 0x10) == sender_obj)
            elif filt == 5:
                ok = bool(self.tags_match(sender, sender_obj, self.node_ent(n, 0xC), m.u32(n + 0x10),
                                          m.u32(n + 0x14)))
            else:
                ok = False
            if not ok:
                continue
            if target and m.u32(n + 4) != target:
                continue
            self.enqueue(n, sender, sender_obj, actor, msg, src)

    def enqueue(self, n, sender, sender_obj, actor, msg, src):
        sub = self.node_ent(n, 0)
        if sub is not None and sub.removed:
            return
        if sender is not None and sender.removed:
            sender = None
        if actor is not None and actor.removed:
            actor = None
        m = self.m
        c = Ctx(m.u32(n + 0x18), sub, sender, actor, m.u32(n + 4), sender_obj, msg, m.u32(n + 0x1C), src)
        if (self.tail + 1) % 128 == self.head:
            self.stats["queue full"] += 1
            return
        self.dbg(f"queue {msg:X} -> s{c.script} {sub}/{c.sub_obj:X} from {sender}/{sender_obj:X} ({src})")
        self.queue.append(c)
        self.tail = (self.tail + 1) % 128

    def still_subscribed(self, c):
        sub = c.sub if (c.sub is not None and not c.sub.removed) else None
        if c.msg == 0:
            return True
        if sub is None and not c.sub_obj:
            return False
        for n in self.nodes(c.msg):
            if sub is not None:
                if self.same(n, 0, sub):
                    return True
            elif c.sub_obj == self.m.u32(n + 4):
                return True
        return False

    def run_queue(self, limit=None):
        """Queued contexts in order, those queued while running included; spawn retries
        between passes (objects.md, The frame and the tick). limit: how many the original ran
        before the vblank came (its head index at the next frame)."""
        n = 0
        while True:
            ran = False
            while self.queue and (limit is None or limit > 0):
                n += 1
                if limit is not None:
                    limit -= 1
                c = self.queue.pop(0)
                self.head = (self.head + 1) % 128
                ran = True
                if self.still_subscribed(c):
                    self.dbg(f"run {c.msg:X} s{c.script} {c.sub}/{c.sub_obj:X} ({c.src})")
                    self.vm(c)
                else:
                    self.stats["queued, unsubscribed"] += 1
            if limit is not None and limit <= 0 and self.queue:
                break
            if not self.spawn_retry() and not self.queue:
                break
        return n

    # -- spawning
    def activate(self, pair):
        m = self.m
        st = m.u32(pair)
        if st & 1:
            return
        m.w32(pair, st | 1)
        self.record_spawn(pair)

    def record_spawn(self, pair):
        rec = self.L.by_pair[pair]
        if rec.kind not in (2, 3, 4, 6):
            return
        self.stats["spawn requests"] += 1
        self.dbg(f"spawn r{rec.idx}")
        ent = self.oracle_spawn(rec)
        if ent is None:
            self.stats["spawn refused (queued)"] += 1
            if len(self.spawn_q) >= 64:
                self.spawn_q.pop(0)
            self.spawn_q.append(pair)
            return
        self.m.w32(pair, self.m.u32(pair) | 1)
        ent.rec = rec
        self.handles[ent.handle] = ent
        self.msg_start(ent, 0)

    def spawn_retry_one(self):
        """0x80074F9C: pop the front (the ring's head index moves on; an emptied ring restarts at
        0) and spawn it, which queues it again at the back if it fails. True if it spawned."""
        if not self.spawn_q:
            return False
        n = len(self.spawn_q)
        if n >= 2 and getattr(self, "oracle_pick", None):
            # which of several waiting records gets a freed slot is the parity of the retries
            # the frame's idle time allowed (timing): the one the original spawned
            k = self.oracle_pick(self.spawn_q)
            if k:
                # as k failed retries would have turned it
                self.spawn_q = self.spawn_q[k:] + self.spawn_q[:k]
                self.spq_head = (self.spq_head + k) % 64
                self.stats["spawn queue turned to the record the original spawned"] += 1
        pair = self.spawn_q.pop(0)
        self.spq_head = 0 if n == 1 else (self.spq_head + 1) % 64
        self.record_spawn(pair)
        return len(self.spawn_q) < n

    def spawn_retry(self):
        """spawn_queue_run_all: retry from the front while a retry succeeds."""
        any_ok = False
        while self.spawn_q:
            if not self.spawn_retry_one():
                break
            any_ok = True
        return any_ok

    def remove(self, ent, why):
        """entity_remove for an entity spawned from a record, then its destructor's
        msg_entity_removed."""
        if ent.removed:
            return
        ent.removed = True
        self.stats[f"removed ({why})"] += 1
        self.stats[f"removed ({why}, type {ent.type:X})"] += 1
        self.dbg(f"remove {ent} ({why})")
        if ent.rec is not None:
            r = ent.rec
            st = self.m.u32(r.pair)
            if r.kind in (2, 3, 6) and (ent.flags & DEAD) and not (r.flags30 & 8):
                st |= 2
            st &= ~1
            if ent.type == 0x1402 and ent.flags & DEAD:
                st |= 2           # enemy_destroy (0x80056290), whatever +0x30 bit 3 says
            self.m.w32(r.pair, st)
            if r.kind != 0:
                self.run_message(ent, 0, 0x1A, why)
        self.drop_subscriptions(ent)

    # -- triggers
    def zones_update(self, ent, zone, pos):
        """zones_mask + zones_events for one walker this tick."""
        L = self.L
        is_player = bool(ent.flags & PLAYER)
        bits = 0
        idx = L.zone_records(zone)
        for k, ri in enumerate(idx):
            r = L.records[ri]
            if is_player:
                if r.kind != 0 and r.lists["enter"] is None and r.lists["leave"] is None and r.lists["use"] is None:
                    continue
            else:
                if r.kind == 0 or (r.lists["c_enter"] is None and r.lists["c_leave"] is None):
                    continue
            if contains(r, pos):
                bits |= 1 << k
        ent.zone, ent.bits = zone, bits
        pz, pb = ent.prev_zone, ent.prev_bits
        if zone == 0xFFFF:
            if pz != zone:
                for k, ri in enumerate(L.zone_records(pz)):
                    if pb >> k & 1:
                        self.record_leave(ent, L.records[ri])
        elif pz == 0xFFFF:
            for k, ri in enumerate(idx):
                if bits >> k & 1:
                    self.record_enter(ent, L.records[ri])
        elif zone == pz:
            ch = bits ^ pb
            for k, ri in enumerate(idx):
                if (pb & ch) >> k & 1:
                    self.record_leave(ent, L.records[ri])
            for k, ri in enumerate(idx):
                if (bits & ch) >> k & 1:
                    self.record_enter(ent, L.records[ri])
        else:
            self._merge(ent, idx, L.zone_records(pz), bits, pb)
        ent.prev_zone, ent.prev_bits = zone, bits

    def _merge(self, ent, a, b, bits, pb):
        """zones_events' walk of two sorted zone lists (new a, old b)."""
        L = self.L
        i = j = 0
        while i < len(a) and j < len(b):
            s1, s2 = a[i], b[j]
            if s1 == s2:
                was, now = pb >> j & 1, bits >> i & 1
                if now != was:
                    (self.record_leave if was else self.record_enter)(ent, L.records[s1])
            elif s1 < s2 and bits >> i & 1:
                self.stats["zone change: enter skipped"] += 1
            elif s2 < s1 and pb >> j & 1:
                self.stats["zone change: leave skipped"] += 1
            if not (s2 < s1):
                i += 1
            if s1 >= s2:
                j += 1
        if i >= len(a):
            while j < len(b):
                if pb >> j & 1:
                    self.record_leave(ent, L.records[b[j]])
                j += 1
        while i < len(a):
            if bits >> i & 1:
                self.record_enter(ent, L.records[a[i]])
            i += 1

    def record_enter(self, ent, r):
        if ent.flags & PLAYER:
            if r.kind == 0:
                self.kind0_enter(ent, r)
                return
            lst = r.lists["enter"]
        elif ent.flags & 0x50:
            lst = r.lists["c_enter"]
        else:
            return
        self.stats["enter"] += 1
        for msg in lst or ():
            self.send(msg, None, r.pair, ent, src=f"enter r{r.idx}")

    def record_leave(self, ent, r):
        if r.kind == 0:
            self.kind0_leave(ent, r)
            return
        if ent.flags & PLAYER:
            lst = r.lists["leave"]
        elif ent.flags & 0x50:
            lst = r.lists["c_leave"]
        else:
            return
        self.stats["leave"] += 1
        for msg in lst or ():
            self.send(msg, None, r.pair, ent, src=f"leave r{r.idx}")

    def kind0_enter(self, ent, r):
        self.stats[f"kind0 enter {r.cls:08X}"] += 1
        if r.cls == 0xD1BA7D1E:
            self.m.w32(LEVEL_FLAGS, self.m.u32(LEVEL_FLAGS) | 0x24)
        elif r.cls == 0x3011B913:
            # record_spawn_type(0x4C): only while bit 0 is clear; ladder_spawn (0x80061244)
            # forgets the held ladder if it is this one, else releases it first
            st = self.m.u32(r.pair)
            if not st & 1:
                if self.ladder_held == r.pair:
                    self.ladder_held = 0
                else:
                    self.ladder_release()
                self.m.w32(r.pair, self.m.u32(r.pair) | 1)

    def kind0_leave(self, ent, r):
        """Leaving a ladder (0x80074DC4 -> 0x80061518(pair)) holds it in 0x8009A5B8,
        releasing the one held before; the held ladder's entity is removed when another
        ladder's is made or another ladder is left (0x8006149C)."""
        self.stats[f"kind0 leave {r.cls:08X}"] += 1
        if r.cls == 0x3011B913 and self.ladder_held != r.pair:
            if self.ladder_held:
                self.ladder_release()
            self.ladder_held = r.pair

    def ladder_release(self):
        p, self.ladder_held = self.ladder_held, 0
        if p and self.m.u32(p) & 1:
            self.stats["ladder released"] += 1
            self.m.w32(p, self.m.u32(p) & ~1)    # entity_remove of the ladder entity

    def use(self, ent):
        """player_use: the Use list of every record whose bit is set."""
        for k, ri in enumerate(self.L.zone_records(ent.zone if ent.zone is not None else 0xFFFF)):
            if ent.bits >> k & 1:
                r = self.L.records[ri]
                if r.kind != 0:
                    for msg in r.lists["use"] or ():
                        self.send(msg, None, r.pair, ent, src=f"use r{r.idx}")

    # -- objectives
    def objective_add(self, oid, text):
        m = self.m
        n = s32(m.u32(OBJ_COUNT))
        tp = m.u32(MSG_TEXTS + 4 * text)
        for i in range(max(n, 0)):
            if s32(m.u32(OBJ_ID + 4 * i)) == oid:
                m.w32(OBJ_DONE + 4 * i, 0)
                m.w32(OBJ_TEXT + 4 * i, tp)
                return
        m.w32(OBJ_DONE + 4 * n, 0)
        m.w32(OBJ_TEXT + 4 * n, tp)
        m.w32(OBJ_ID + 4 * n, oid)
        m.w32(OBJ_COUNT, n + 1)

    def objective_remove(self, oid):
        m = self.m
        n = s32(m.u32(OBJ_COUNT))
        i = 0
        while i < n and s32(m.u32(OBJ_ID + 4 * i)) != oid:
            i += 1
        n -= 1
        m.w32(OBJ_COUNT, n)
        for base in (OBJ_DONE, OBJ_TEXT, OBJ_ID):
            m.w32(base + 4 * i, m.u32(base + 4 * n))

    def objective_find(self, oid):
        for i in range(max(s32(self.m.u32(OBJ_COUNT)), 0)):
            if s32(self.m.u32(OBJ_ID + 4 * i)) == oid:
                return i
        return None

    # -- the VM
    def vm(self, c):
        m = self.m
        sid = c.script
        sid = self.script_of(c.sub if c.sub_obj == 0 else None, c.sub_obj)
        if sid == -1:
            return
        base = self.script_base(sid)
        nh = m.u32(base)
        vars_at = base + nh * 4 + 4
        pos = c.pc - base
        st = []

        def var_addr(scope, idx):
            if scope in (0xF0, 0xF1):
                if c.sub_obj:
                    return ("rec", m.u32(c.sub_obj + 4) + 0x20 + idx)
                if c.sub is not None:
                    return ("ent", c.sub, idx)
                return None
            return ("rec", vars_at + idx)

        def var_get(a):
            if a is None:
                return 0
            if a[0] == "rec":
                return m.u8(a[1])
            return a[1].vars[a[2]] if a[2] < 8 else 0

        def var_set(a, v):
            if a is None:
                self.stats["store to entity 0"] += 1
                return
            if a[0] == "rec":
                m.w8(a[1], v)
            elif a[2] < 8:
                a[1].vars[a[2]] = v & 0xFF

        def align(p, n):
            return p + (-p % n)

        def ent_of(sel):
            return (c.sub, c.sender, c.actor)[sel]

        steps = 0
        while True:
            steps += 1
            if steps > 100000:
                self.stats["runaway script"] += 1
                return
            op = m.u8(base + pos)
            pos += 1
            if op == 0x00:
                continue
            if op == 0x01:
                if st:
                    st.pop()
                continue
            if op == 0x02:
                s = m.u8(base + pos)
                pos += 1
                if 0xF4 <= s <= 0xFF:
                    t = m.u8(base + pos)
                    pos += 1
                    if (0xF7 <= t <= 0xF9) or (0xFD <= t <= 0xFF):
                        self.stats["wide push"] += 1
                        pos = align(pos, 2) + 2
                        prop = 0
                    else:
                        prop = m.u8(base + pos)
                        pos += 1
                    if 0xFA <= s <= 0xFC:
                        e, o = ent_of(s - 0xFA), (c.sub_obj, c.sender_obj, 0)[s - 0xFA]
                    elif 0xF4 <= s <= 0xF6:
                        e, o = ent_of(s - 0xF4), (c.sub_obj, c.sender_obj, 0)[s - 0xF4]
                    else:
                        e, o = None, 0
                    if t == 0x10:
                        st.append((0x10, e if s >= 0xFA else o))
                    else:
                        v = self.prop(e, o, prop)
                        st.append(v if v is not None else (0, 0))
                    continue
                if 0xF0 <= s <= 0xF3:
                    sel = m.u8(base + pos)
                    pos += 1
                    if s in (0xF1, 0xF3):
                        pos = align(pos, 2)
                        idx = m.u16(base + pos)
                        pos += 2
                    else:
                        idx = m.u8(base + pos)
                        pos += 1
                    v = var_get(var_addr(s, idx))
                    if sel == 9:
                        st.append((0xB, v))
                    elif 1 <= sel <= 8:
                        st.append((0xB, v >> (sel - 1) & 1))
                    else:
                        st.append((0, 0))
                    continue
                if s == 9:
                    st.append((0xB, m.u8(base + pos)))
                    pos += 1
                elif 0xA <= s <= 0xD:
                    pos = align(pos, 4)
                    st.append((s, s32(m.u32(base + pos)) if s == 0xA else m.u32(base + pos)))
                    pos += 4
                elif s == 0xE:
                    pos = align(pos, 4)
                    st.append((0xE, struct.unpack("<f", m.blk(base + pos, 4))[0]))
                    pos += 4
                elif s == 0x10:
                    pos = align(pos, 4)
                    st.append((0x10, base + pos))
                    pos += 4
                elif s == 0xF:
                    n = m.u8(base + pos)
                    st.append((0xF, m.blk(base + pos + 1, n)))
                    pos += 1 + n
                else:
                    self.stats["bad push"] += 1
                    return
                continue
            if op == 0x03:
                s = m.u8(base + pos)
                pos += 1
                if 0xF0 <= s <= 0xF3:
                    sel = m.u8(base + pos)
                    pos += 1
                    if s in (0xF1, 0xF3):
                        pos = align(pos, 2)
                        idx = m.u16(base + pos)
                        pos += 2
                    else:
                        idx = m.u8(base + pos)
                        pos += 1
                    a = var_addr(s, idx)
                    t, v = st[-1] if st else (0, 0)
                    if sel == 9:
                        if t in (0xA, 0xB):
                            var_set(a, int(v))
                        elif t == 0xE:
                            var_set(a, int(v))
                    elif 1 <= sel <= 8:
                        old = var_get(a)
                        bit = 1 << (sel - 1)
                        var_set(a, (old | bit) if v else (old & ~bit))
                if st:
                    st.pop()
                continue
            if 0x04 <= op <= 0x0F and op != 0x07:
                (tn, n), (tt, t) = st[-2], st[-1]
                del st[-2:]
                st.append(self.binop(op, tt, t, tn, n))
                continue
            if op == 0x07:
                t, v = st.pop()
                st.append((0xB, int(not v)))
                continue
            if op == 0x10:
                pos = align(pos, 2)
                tgt = m.u16(base + pos)
                pos += 2
                t, v = st.pop()
                if not v:
                    pos = tgt
                continue
            if op == 0x11:
                pos = m.u16(base + align(pos, 2))
                continue
            if op in (0x12, 0x13):
                if op == 0x12:
                    n = m.u8(base + pos)
                    pos += 1
                else:
                    pos = align(pos, 2)
                    n = m.u16(base + pos)
                    pos += 2
                self.builtin(n, c, st, base, sid)
                continue
            if op == 0x14:
                return
            if op == 0x15:
                st.append((0xB, int(m.u8(vars_at) == 0)))
                continue
            if op == 0x16:
                m.w8(vars_at, 1)
                continue
            if op == 0x17:
                t = m.u8(base + pos)
                pos += 1
                if t == 9:
                    pos += 1
                elif t in (0xA, 0xB):
                    pos = align(pos, 4) + 4
                continue
            self.stats["invalid opcode"] += 1
            return

    def binop(self, op, tt, t, tn, n):
        """T op N, T the top. Integer paths only (no script on the disc mixes in floats)."""
        if isinstance(t, Ent) or isinstance(n, Ent) or t is None or n is None:
            a = t.serial if isinstance(t, Ent) else (t or 0)
            b = n.serial if isinstance(n, Ent) else (n or 0)
            if op == 4:
                return (0xB, int(t is n if (isinstance(t, Ent) or isinstance(n, Ent)) else a == b))
            t, n = a, b
        if tt == 0xE or tn == 0xE:
            self.stats["float op"] += 1
        uns = (tt == 0xB and tn == 0xB)
        T, N = t, n
        if op == 4:
            return (0xB, int(T == N))
        if op == 5:
            return (0xB, int(bool(N) and (T & 0xFFFFFFFF) > 0))
        if op == 6:
            return (0xB, int(bool(N) or bool(T)))
        if op in (8, 9, 0xA, 0xB):
            if uns:
                T, N = T & 0xFFFFFFFF, N & 0xFFFFFFFF
            else:
                T, N = s32(T), s32(N)
            r = {8: T < N, 9: T > N, 0xA: T <= N, 0xB: T >= N}[op]
            return (0xB, int(r))
        rt = 0xB if uns else 0xA
        if op == 0xC:
            r = T + N
        elif op == 0xD:
            r = T - N
        elif op == 0xE:
            r = T * N
        else:
            if N == 0:
                self.stats["divide by 0"] += 1
                r = 0
            elif uns:
                r = (T & 0xFFFFFFFF) // (N & 0xFFFFFFFF)
            else:
                r = cdiv(s32(T), s32(N))
        r &= 0xFFFFFFFF
        return (rt, s32(r) if rt == 0xA else r)

    def prop(self, e, o, n):
        """push_property: a record field by kind (levels.md, Record properties)."""
        pair = o or (e.rec.pair if e is not None and e.rec else 0)
        if not pair:
            return None
        r = self.L.by_pair[pair]
        m, a = self.m, r.addr
        if r.kind == 0:
            return None
        if n <= 3:
            return (0xA, m.s16(a + 0x28 + 2 * n))
        if n in (4, 5):
            return (0xA, m.s16(a + 0x32 + 2 * (n - 4)))
        if n == 13:
            return (0xB, m.u32(a + 0x54))
        if 14 <= n <= 17:
            return (0xB, m.u32(a + 0x30) >> (n - 14) & 1)
        if n == 18:
            return (0xA, m.s16(a + 0x36))
        if n == 19:
            if r.kind == 5:
                return (0xB, m.u32(a + 0x58))
            if r.kind in (2, 3, 4, 6):
                return (0xB, m.u32(a + 0x58) & 1)
        if n == 29 and r.kind == 6:
            return (0xB, m.u32(a + 0x6C))
        self.stats[f"property {n} kind {r.kind}"] += 1
        return None

    def builtin(self, n, c, st, base, sid):
        self.calls[n] += 1
        pop = lambda: st.pop()[1] if st else 0
        if n in (0, 1):
            h = pop()
            filt = pop()
            fent = fobj = None
            val = -1
            if filt in (1, 2):
                val = pop()
            elif filt in (3, 4):
                fobj = pop()
                fent = pop()
            elif filt == 5:
                fobj = pop()
                fent = pop()
                val = pop()
            elif filt != 0:
                self.stats["unhandled filter"] += 1
                return
            hd = self.handler(sid, h)
            if hd is None:
                return
            if isinstance(fobj, Ent):
                fobj = fobj.rec.pair if fobj.rec else 0
            fent = fent if isinstance(fent, Ent) else None
            sub = c.sub if (c.sub is not None and not c.sub.removed) else None
            if n == 0:
                self.register(hd[0], sub, c.sub_obj, filt, fent, fobj or 0, s32(val) if val != -1 else -1,
                              hd[1], sid)
            else:
                self.unregister(hd[0], sub, c.sub_obj, filt, fent, fobj or 0, s32(val) if val != -1 else -1)
        elif n == 2:
            msg = pop()
            self.send(msg, c.sub, c.sub_obj, None, src=f"s{sid} send")
        elif n == 3:
            v = pop()
            if c.sub is not None:
                c.sub.flags40 |= v
        elif n == 4:
            obj = pop()
            pop()
            if obj:
                self.activate(obj)
        elif n == 5:
            e = pop()
            if isinstance(e, Ent):
                self.remove(e, "remove_entity")
                self.removed_by_script.add(e.handle)
        elif n == 6:
            pop()
        elif n == 7:
            k = pop()
            o = pop()
            e = pop()
            if not isinstance(e, Ent):
                st.append((0xB, 0))
            elif k == 0:
                st.append((0xB, e.flags & 1))
            else:
                st.append((0xB, e.flags & DEAD))
        elif n == 8:
            pop()
        elif n == 9:
            v = pop()
            o = pop()
            e = pop()
            st.append((0xB, self.has_tag(e if isinstance(e, Ent) else None, o if not isinstance(o, Ent) else 0, v)))
        elif n == 10:
            typ = pop()
            o2 = pop()
            e2 = pop()
            o1 = pop()
            e1 = pop()
            st.append((0xB, self.tags_match(e1 if isinstance(e1, Ent) else None, o1 or 0,
                                           e2 if isinstance(e2, Ent) else None, o2 or 0, typ)))
        elif n == 11:
            pop()
            pop()
        elif n == 12:
            msg = pop()
            pop()
            tgt = pop()
            target = tgt.rec.pair if isinstance(tgt, Ent) and tgt.rec else 0
            self.send(msg, c.sub, c.sub_obj, None, target, src=f"s{sid} send_to")
        elif n == 13:
            v = s16(pop())
            what = pop()
            if what == 2:
                self.m.w32(OBJ_PRIMARY, v)
            elif what == 3:
                self.m.w32(LEVEL_FLAGS, self.m.u32(LEVEL_FLAGS) | 0x24)
            elif what == 4:
                self.alarm_level(v)
            self.stats[f"level_set {what}"] += 1
        elif n == 14:
            oid = pop()
            text = pop()
            self.objective_add(s32(oid), text)
        elif n == 15:
            self.objective_remove(s32(pop()))
        elif n == 16:
            i = self.objective_find(s32(pop()))
            if i is not None:
                self.m.w32(OBJ_DONE + 4 * i, 1)
        elif n == 17:
            i = self.objective_find(s32(pop()))
            st.append((0xB, 1 if i is None else self.m.u32(OBJ_DONE + 4 * i)))
        elif n == 18:
            pop()
        elif n == 19:
            mode = pop()
            for _ in range(6):
                pop()
            if mode == 3:
                st.append((0xB, 0))
        elif n == 20:
            pop()
        elif n == 21:
            pop()
            pop()
        else:
            self.stats["unknown built-in"] += 1

    def alarm_level(self, node_id):
        """level_set(4, n): alarm_from_level. Raising sends 0x2B from every entity of the
        node's record; the outcome (raised or not) is checked against g_alarm."""
        self.stats["alarm from level"] += 1
        self.alarm_requests.append(node_id)


# ---------------------------------------------------------------- the check



class Stats:
    def __init__(self):
        self.n = Counter()
        self.ok = Counter()
        self.first = defaultdict(list)

    def add(self, what, ok, detail=""):
        self.n[what] += 1
        if ok:
            self.ok[what] += 1
        elif len(self.first[what]) < 6:
            self.first[what].append(detail)

    def report(self, out=print):
        for k in sorted(self.n):
            out(f"  {k:28s} {self.ok[k]:7d} / {self.n[k]:7d}  {'exact' if self.ok[k] == self.n[k] else 'FAILS'}")
            for d in self.first[k]:
                out(f"      {d}")


def obs_nodes(mem, msg):
    out, n = [], mem.u32(mem.u32(G_SUBS) + 4 * msg)
    k = 0
    while n and k < 1100 and POOL <= n < POOL + 1024 * 0x24:
        out.append((n,) + tuple(mem.u32(n + 4 * i) for i in range(8)))
        n = mem.u32(n + 0x20)
        k += 1
    return out


def ring_entries(mem, head, tail):
    out = []
    i = head
    while i != tail:
        a = RING + 32 * i
        w = [mem.u32(a + 4 * k) for k in range(8)]
        # ring layout: +0 pc, +4 sub, +8 sender, +C actor, +10 sub_obj, +14 sender_obj, +18 msg, +1C script
        out.append((w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]))
        i = (i + 1) % 128
    return out


class Run:
    def __init__(self, log, trace_path, dump_path, name, f0, f1, verbose=False, out=print, later=None,
                 later_v=None, rebase=()):
        m, l = int(name[0]), int(name[2])
        self.name, self.f0, self.f1, self.v, self.out = name, f0, f1, verbose, out
        dump = open(dump_path, "rb").read()
        bsd = open(os.path.join(ROOT, "extracted", "moh", "DATA", f"MSN{m}", f"LVL{l}", f"{m}_{l}.BSD"), "rb").read()
        self.L = Level(dump, name)
        self.later = Mem(open(later, "rb").read()) if later else None
        self.later_v = later_v
        self.obs = Observed(log, dump, f0, f1, self.later.d if later else None, rebase)
        self.frames = log.frames(f0, f1)
        self.ticks = read_trace(trace_path, f0, f1)
        # the entity list in the dump (before the first tick) stands for "tick 0"
        dm = Mem(dump)
        ents, order = {}, []
        e = dm.u32(0x8009D64C)
        while e and len(order) < 1024:
            h = dm.u32(e)
            ents[h] = (s32(dm.u32(e + 8)), dm.u32(e + 0xC), s32(dm.u32(e + 0x20C)), s32(dm.u32(e + 0x210)),
                       s32(dm.u32(e + 0x214)))
            order.append(h)
            e = dm.u32(e + 4)
        self.tick0 = (0, f0 - 1, ents, order)
        self.damage = [d for d in log.damage if f0 <= d[0] < f1]
        self.sim = Sim(self.L, bsd)
        self.sim.oracle_spawn = self.oracle_spawn
        self.sim.oracle_pick = self.oracle_pick
        self.sim.removed_by_script = set()
        self.sim.alarm_requests = []
        self.st = Stats()
        self.ents = {}          # handle -> Ent (the simulator's view of the live list)
        self.claimed = set()
        self.next_new = []      # new entities of the next tick, oldest first (for the spawn oracle)
        self.next_frame = None
        self.later_done = False
        self.ghosts = []        # pickups made and taken before the trace saw them
        self.ghost_slots = set()
        self.first_frame = True
        r = os.environ.get("LVM_DEBUG")
        self.dbgrange = tuple(int(x) for x in r.split("-")) if r else None

    def oracle_pick(self, pairs):
        """Of the queued records, the index of the one nearest a new, unclaimed entity of its
        type (a spawn lands at its record), or None."""
        best = None
        for h, t, e in self.next_new:
            if h in self.claimed:
                continue
            p = (e[2] >> 16, e[3] >> 16, e[4] >> 16)
            for k, pair in enumerate(pairs):
                rec = self.L.by_pair[pair]
                want = self.L.spawn_type(rec)
                if not (t == want or (want in (0x1402, 0x1405) and t in (0x1402, 0x1405))):
                    continue
                d = sum((s16(a) - b) ** 2 for a, b in zip(p, rec.pos))
                if best is None or d < best[0]:
                    best = (d, k)
        return best[1] if best else None

    # The engine's side of a spawn: the entity the original created for this record, if any.
    def oracle_spawn(self, rec):
        want = self.L.spawn_type(rec)
        # the new entity of that type nearest the record (a spawn lands at its record; with two
        # alike the list order can't tell, after a restart it is rebuilt)
        cands = [(sum((s16(e[k + 2] >> 16) - rec.pos[k]) ** 2 for k in range(3)), i)
                 for i, (h, t, e) in enumerate(self.next_new) if h not in self.claimed and
                 (t == want or (want in (0x1402, 0x1405) and t in (0x1402, 0x1405)))]
        order = [i for _, i in sorted(cands)]
        if order:
            h, t, e = self.next_new[order[0]]
            self.claimed.add(h)
            ent = Ent(h, t, rec)
            self.ents[h] = ent
            self.sim.handles[h] = ent
            ent.flags, ent.pos = e[1], (e[2] >> 16, e[3] >> 16, e[4] >> 16)
            self.st.add("spawn lands", True)
            return ent
        # a pickup made under the player is taken in the next tick, before the trace sees it;
        # the handle table at the next watch still holds it (handles are freed after the frame)
        if want == 0x65 and self.next_frame is not None:
            known = {h & 0xFFFF for h in self.ents if not self.ents[h].removed} | {h & 0xFFFF for h, _, _ in self.next_new}
            for slot in range(256):
                a = 0x8009D678 + 4 * slot
                if slot in known or slot in self.ghost_slots:
                    continue
                nxt = self.obs.peek(a, self.next_frame)
                # a slot taken by the next frame (empty now, or still holding an entity of the
                # level before, at a level's first frame)
                if nxt and nxt != self.obs.mem.u32(a) and (not self.obs.mem.u32(a) or self.first_frame):
                    ent = Ent(0xFFFF0000 | slot, 0x65, rec)
                    self.ghost_slots.add(slot)
                    self.ghosts.append(ent)
                    self.st.add("spawn lands", True)
                    return ent
        live = sum(1 for e in self.ents.values() if not e.removed and e.type in (0x1402, 0x1405))
        self.sim.stats[f"spawn refused (type {want:X}, {live} characters alive)"] += 1
        return None

    def new_entities(self, k):
        """Entities in tick k that weren't in tick k-1, oldest created first."""
        if k >= len(self.ticks):
            return []
        cur = self.ticks[k]
        prev = self.ticks[k - 1][2] if k > 0 else self.tick0[2]
        new = [h for h in cur[3] if h not in prev]
        return [(h, cur[2][h][0], cur[2][h]) for h in reversed(new)]

    def run(self, maxf=None):
        L, sim, obs = self.L, self.sim, self.obs
        t0 = self.tick0
        ph = next(h for h in t0[3] if t0[2][h][0] == 1)
        ps = next(r for r in L.records if r.cls == 0x8877B646 and r.p34 == 0)
        player = Ent(ph, 1, ps)
        player.flags = t0[2][ph][1]
        self.ents[ph] = player
        # the level start's spawns (record scripts activating at message 0) are in the first tick
        self.next_new = [(h, t0[2][h][0], t0[2][h]) for h in reversed(t0[3]) if h != ph]
        sim.level_start(player)
        # the other entities alive at the start (the camera, the world): no records
        for h in t0[3]:
            if h not in self.ents:
                e = Ent(h, t0[2][h][0])
                e.flags = t0[2][h][1]
                self.ents[h] = e
                sim.handles[h] = e
        tick_i = 0
        prev_gt = 0
        prev_tmr = None
        prev_f = None
        alarm_prev = s32(obs.mem.u32(G_ALARM))
        last_now = 0
        restart = False
        self.first_frame = True
        for fi, (F, gt) in enumerate(self.frames):
            if maxf and fi >= maxf:
                break
            if restart and tick_i < len(self.ticks):
                # the player died: level_restart -> level_start without reloading the BSD
                # (record variables and script bytes keep what the scripts wrote); every entity
                # gone from the first tick after it was removed first, with its 0x1A
                restart = False
                sim.stats["restarts"] += 1
                first = self.ticks[tick_i][2]
                for h, e in list(self.ents.items()):
                    if not e.removed and h not in first:
                        if e.rec is not None:
                            sim.remove(e, "restart")
                        e.removed = True
                # the player's soldier stays (player_reset): his last zone and bits carry over
                self.next_new, self.claimed = self.new_entities(tick_i), set()
                self.first_frame = True
                sim.level_start(sim.player)
            ticks = []
            while tick_i < len(self.ticks) and self.ticks[tick_i][0] <= gt:
                ticks.append(tick_i)
                tick_i += 1
            obs.advance(F)
            om = obs.mem
            sim.debug = bool(self.dbgrange) and self.dbgrange[0] <= F <= self.dbgrange[1]
            if sim.debug:
                print(f"  frame {F} ticks {[self.ticks[k][0] for k in range(tick_i, len(self.ticks)) if self.ticks[k][0] <= gt]}")
            # the 1/3-second messages this frame: from the counter (+1 per vblank, -20 per send)
            tmr = s32(om.u32(0x8009A5C0))
            nsend = 0
            if prev_tmr is not None:
                d = prev_tmr + (F - prev_f) - tmr
                if d % 20 == 0 and d >= 0:
                    nsend = d // 20
                else:   # a pause resets the counter (objects.md)
                    sim.stats["timer reset (pause)"] += 1
            prev_tmr, prev_f = tmr, F
            # Use: action 5's button pressed this tick (action_pressed: now has it, the last poll
            # didn't), the button the controller configuration gives action 5 (0x800B7790 + 5*4:
            # 0x80 in the default layout, 0x08 in the recorded session's; player.md).
            # The watch sees the last two polls; a press that began on an earlier tick of a
            # multi-tick frame shows in both (then it is put on the frame's first tick).
            # player_pad_config fills the table from the button layout option once the level runs
            # (the dump before the first tick still holds the default): read it from the later dump
            use_bit = (self.later or om).u32(0x800B7790 + 20) & 0xFFFF or USE_BIT
            pad_now, pad_prev = om.u32(PAD_NOW), om.u32(PAD_PREV)
            use_at = None
            if pad_now & use_bit == use_bit and last_now & use_bit != use_bit:
                use_at = len(ticks) - 1 if pad_prev & use_bit != use_bit else 0
            last_now = pad_now
            for j, k in enumerate(ticks):
                self.tick(k, om, use_at == j, j < nsend)
            # alarms raised by model scripts (op 0x49): g_alarm -1 -> node
            al = s32(om.u32(G_ALARM))
            if al != alarm_prev and al != -1:
                if sim.alarm_requests:
                    sim.alarm_requests.clear()
                else:
                    self.alarm(al)
            alarm_prev = al
            self.compare(F, om)
            if self.later is not None and self.later_v is not None and F >= self.later_v and not self.later_done:
                self.later_done = True
                self.compare_dump_entities(F)
            # end of frame: culls in the drawing pass, then the queue and the spawn retries
            nk = tick_i
            # a frame ending in a death (level flags 2) restarts next: the next tick's new
            # entities are level_start's, not for this frame's spawn retries
            self.next_new = [] if om.u32(LEVEL_FLAGS) & 2 else self.new_entities(nk)
            self.next_frame = self.frames[fi + 1][0] if fi + 1 < len(self.frames) else None
            self.claimed = set()
            nxt_f = self.frames[fi + 1][0] if fi + 1 < len(self.frames) else None
            limit = None
            if nxt_f is not None:
                limit = (obs.peek(G_HEAD, nxt_f) - om.u32(G_HEAD)) % 128
                if limit < len(sim.queue):
                    sim.stats["frames ending with messages left"] += 1
            if nk < len(self.ticks):
                # culls come first in the drawing pass; an entity the queue removes is no
                # cull, which only running the queue tells: try it on a copy first
                cands = self.cull_candidates(self.ticks[nk - 1], self.ticks[nk])
                if cands:
                    snap = self.snapshot()
                    sim.debug, dbg = False, sim.debug
                    sim.run_queue(limit)
                    cands = [e for e in cands if not e.removed]
                    self.restore(snap)
                    sim.debug = dbg
                for e in cands:
                    sim.run_message(None, e.rec.pair, 2, "cull")
                    sim.remove(e, "culled")
            sim.run_queue(limit)
            if nxt_f is not None and len(sim.spawn_q) >= 2:
                # while waiting for the vblank the game retries the spawns over and over, failing
                # ones going to the back: how many times is the frame's idle time, which the
                # ring's head index at the next frame tells (as `limit` does for messages)
                want = obs.peek(SPQ_HEAD, nxt_f)
                extra = (want - sim.spq_head) % 64 % len(sim.spawn_q)
                for _ in range(extra):
                    if sim.spawn_retry_one() or len(sim.spawn_q) < 2:
                        break
                else:
                    sim.spq_head = want
                sim.stats["spawn retries (idle time)"] += extra
            self.end_frame_alarms(om)
            self.first_frame = False
            if om.u32(LEVEL_FLAGS) & 2:
                restart = True
        return self.st

    def alarm(self, node_idx):
        L, sim = self.L, self.sim
        node = L.nodes[node_idx]
        pair = None
        tbl = L.mem.u32(L.mem.u32(L.mem.u32(G_WORLD) + 0x388) + 0x14)
        pair = tbl + 8 * L.mem.u32(node + 0x2C)
        self.sim.stats["alarm (model)"] += 1
        for e in list(self.ents.values()):
            if e.rec is not None and e.rec.pair == pair and not e.removed:
                sim.send(0x2B, e, 0, None, src="alarm")

    def end_frame_alarms(self, om):
        self.sim.alarm_requests.clear()

    def cull_candidates(self, last, nxt):
        """Entities gone at the next frame's first tick, alive, cullable (flags 0x8000), not
        hit then and not picked up: removed by entities_draw this frame, their record told
        first (message 2) - unless this frame's queue removes them."""
        out = []
        hit = {id(self.victim(d)) for d in self.hits_in(last, nxt, False)}
        for h in last[3]:
            if h in nxt[2]:
                continue
            e = self.ents.get(h)
            if e is None or e.removed or e.rec is None or id(e) in hit:
                continue
            typ, flags = last[2][h][0], last[2][h][1]
            if typ == 0x65 and self.near_player(last, h, nxt):
                continue
            # 1:1's timed prop removes itself in its think (variable 6 < variable 2; objects.md),
            # which the trace can't tell from a cull: the think, as it always was when watched
            if typ == 0x1771:
                continue
            # dead characters are taken away by their own code (never culled in the session);
            # anything else cullable goes, dead or not (a broken window off screen)
            if flags & 0x8000 and not (flags & DEAD and typ in (0x1402, 0x1405)):
                e.flags = flags
                out.append(e)
        return out

    def snapshot(self):
        sim = self.sim
        return (bytes(sim.m.d), list(sim.queue), sim.head, sim.tail, list(sim.spawn_q), sim.ladder_held,
                Counter(sim.stats), Counter(sim.calls), dict(sim.handles), dict(self.ents), set(self.claimed),
                set(sim.removed_by_script), list(sim.alarm_requests),
                {id(e): (e.removed, bytes(e.vars), e.flags40, e.flags, e.rec) for e in self.ents.values()},
                Counter(self.st.n), Counter(self.st.ok), list(self.ghosts), set(self.ghost_slots))

    def restore(self, snap):
        sim = self.sim
        (d, sim.queue, sim.head, sim.tail, sim.spawn_q, sim.ladder_held, sim.stats, sim.calls, sim.handles,
         self.ents, self.claimed, sim.removed_by_script, sim.alarm_requests, es, self.st.n, self.st.ok,
         self.ghosts, self.ghost_slots) = snap
        sim.m.d[:] = d
        for e in self.ents.values():
            if id(e) in es:
                e.removed, v, e.flags40, e.flags, e.rec = es[id(e)]
                e.vars[:] = v

    def near_player(self, tk, h, nxt=None):
        """A pickup gone next to the player was picked up (touched after the player's move),
        one gone far from him was culled."""
        p = (nxt or tk)[2].get(self.sim.player.handle) or tk[2][self.sim.player.handle]
        e = tk[2][h]
        dx, dz = (p[2] - e[2]) >> 16, (p[4] - e[4]) >> 16
        return dx * dx + dz * dz < 300 * 300

    def hits_in(self, prev, tk, first):
        return [d for d in self.damage if prev[1] < d[0] <= tk[1] or (first and d[0] <= tk[1])]

    def victim(self, d):
        """The simulator's entity a damage event names (the slot, or the handle table)."""
        victim = d[1]
        if victim in SLOTS:
            return self.ents.get(self.obs.mem.u32(victim))
        hnd = self.obs.mem
        for slot in range(256):
            if hnd.u32(0x8009D678 + 4 * slot) == victim:
                return next((x for x in self.ents.values() if (x.handle & 0xFFFF) == slot and not x.removed), None)
        return None

    def tick(self, k, om, use, timer):
        sim = self.sim
        tk = self.ticks[k]
        prev = self.ticks[k - 1] if k > 0 else self.tick0
        hits = self.hits_in(prev, tk, k == 0)
        hit_ents = {id(self.victim(d)) for d in hits}
        # bind new entities that no spawn claimed (bullets, effects, the ladder...)
        for h in tk[3]:
            if h not in self.ents:
                e = Ent(h, tk[2][h][0])
                self.ents[h] = e
                sim.handles[h] = e
        for h, e in self.ents.items():
            if h in tk[2]:
                v = tk[2][h]
                e.flags, e.pos = v[1], (s16(v[2] >> 16), s16(v[3] >> 16), s16(v[4] >> 16))
        # the entity walk, in last tick's list order: removals in a think, the walkers' triggers
        gone_after_hit = []
        picked = []
        for h in prev[3]:
            e = self.ents.get(h)
            if e is None:
                continue
            if h not in tk[2]:
                if not e.removed and e.rec is not None:
                    pv = prev[2][h]
                    e.flags = pv[1]
                    if id(e) in hit_ents:
                        gone_after_hit.append(e)
                        continue       # removed by its hit (in the collisions below)
                    if pv[0] == 0x65 and self.near_player(prev, h, tk):
                        picked.append(e)
                        continue       # picked up: in the collisions below
                    sim.remove(e, "dead" if pv[1] & DEAD else "removed in a think")
                if e.rec is None:
                    e.removed = True
                continue
            if e is sim.player:
                if use:
                    sim.use(e)
                # entity_move and zones_update are skipped for flags A 0x20000 (the dead player,
                # who follows the death camera): his zone bits stay as they were
                if not e.flags & 0x20000:
                    self.walker(e, om)
            elif e.type in (0x1402, 0x1405) and e.flags & 0x51:
                self.walker(e, om)
        # collisions: hits (message 1 to the victim's script, once per hit: a hit that leaves a
        # soldier under 10% logs a second damage event) and pickups
        seen = set()
        for d in hits:
            if (d[1], d[4]) in seen:
                continue
            seen.add((d[1], d[4]))
            e = self.victim(d)
            if e is None or e.rec is None or e.removed:
                continue
            if d[6]:
                e.flags |= DEAD
            if d[2] in (0x1402, 0x1405):
                if 0x13F0 <= d[5] <= 0x13F9:
                    sim.run_message(e, 0, 1, "hit")
            else:
                sim.run_message(e, 0, 1, "hit")
        for e in gone_after_hit:
            if not e.removed:
                sim.remove(e, "dead" if e.flags & DEAD else "removed by its hit")
        if self.ghosts:
            picked += self.ghosts
            self.ghosts = []
            self.ghost_slots = set()
        for e in picked:
            msg = OBJECTIVE_PICKUPS.get(e.rec.pickup)
            if msg is not None:
                sim.send(msg, e, 0, None, src="pickup")
            sim.remove(e, "picked up")
        if timer:
            sim.send(0x59, None, 0, None, src="timer")

    def walker(self, e, om):
        if e is self.sim.player:
            zone = om.u32(om.u32(G_SOLDIER) + 0x390)
        else:
            slot = next((a for a in SLOTS if om.u32(a) == e.handle), None)
            if slot is None:
                return
            zone = om.u32(slot + 0x390)
        self.sim.zones_update(e, zone, e.pos)

    # -- comparisons at the watch point
    def compare(self, F, om):
        sim, sm, L, st = self.sim, self.sim.m, self.L, self.st
        for r in L.records:
            a, b = sm.u32(r.pair), om.u32(r.pair)
            st.add("record state", a == b, f"{F}: r{r.idx} kind {r.kind} script {r.script}: sim {a} orig {b}")
            if r.kind:
                a, b = sm.blk(r.addr + 0x20, 8), om.blk(r.addr + 0x20, 8)
                st.add("record variables", a == b, f"{F}: r{r.idx} script {r.script}: sim {a.hex()} orig {b.hex()}")
        sc = L.scripts
        for sid in range(sm.u32(sc + 4)):
            off = sm.u32(sc + 8 + 4 * sid)
            if not off:
                continue
            b0 = sc + off
            n = sm.u32(b0)
            code = min(sm.u16(b0 + 6 + 4 * k) for k in range(n)) if n else 4
            a, b = sm.blk(b0 + 4 + 4 * n, code - 4 - 4 * n), om.blk(b0 + 4 + 4 * n, code - 4 - 4 * n)
            st.add("script variables", a == b, f"{F}: script {sid}: sim {a.hex()} orig {b.hex()}")
        if os.environ.get("LVM_NODES") == str(F):
            for side, lists in (("sim", [[(n,) + tuple(sm.u32(n + 4 * i) for i in range(8)) for n in sim.nodes(k)] for k in range(L.nmsg)]),
                                ("orig", [obs_nodes(om, k) for k in range(L.nmsg)])):
                allv = sorted((x[0], k, x[1:]) for k, lst in enumerate(lists) for x in lst)
                setattr(self, "_dbg_" + side, allv)
            a_, b_ = self._dbg_sim, self._dbg_orig
            for i, (x, y) in enumerate(zip(a_, b_)):
                if x != y:
                    print("first diff", i, [(hex(z[0]), hex(z[1]), [hex(v) for v in z[2]]) for z in a_[i - 1:i + 3]])
                    print("orig      ", i, [(hex(z[0]), hex(z[1]), [hex(v) for v in z[2]]) for z in b_[i - 1:i + 3]])
                    break
            print("counts", len(a_), len(b_))
        for msg in range(L.nmsg):
            a = [(n,) + tuple(sm.u32(n + 4 * i) for i in range(8)) for n in sim.nodes(msg)]
            b = obs_nodes(om, msg)
            if a != b:
                sa, sb = set(a), set(b)
                d = (f"{F}: msg {msg:X}: sim only {fmt_nodes([x for x in a if x not in sb])} "
                     f"orig only {fmt_nodes([x for x in b if x not in sa])}"
                     + ("" if sa != sb else " (order)"))
            st.add("subscriptions", a == b, d if a != b else "")
        oh, ot = om.u32(G_HEAD), om.u32(G_TAIL)
        ob = ring_entries(om, oh, ot)
        sq = [c.words() for c in sim.queue]
        self.match_queue(F, sq, ob, oh, ot)
        n_obj = s32(om.u32(OBJ_COUNT))
        a = [s32(sm.u32(OBJ_COUNT)), s32(sm.u32(OBJ_PRIMARY))] + [
            (sm.u32(OBJ_ID + 4 * i), sm.u32(OBJ_DONE + 4 * i), sm.u32(OBJ_TEXT + 4 * i)) for i in range(max(0, s32(sm.u32(OBJ_COUNT))))]
        b = [n_obj, s32(om.u32(OBJ_PRIMARY))] + [
            (om.u32(OBJ_ID + 4 * i), om.u32(OBJ_DONE + 4 * i), om.u32(OBJ_TEXT + 4 * i)) for i in range(max(0, n_obj))]
        st.add("objectives", a == b, f"{F}: sim {a} orig {b}")
        a, b = sm.u32(LEVEL_FLAGS) & 0x24, om.u32(LEVEL_FLAGS) & 0x24
        st.add("level end flags", a == b, f"{F}: sim {a:X} orig {b:X}")
        n = om.u32(SPQ_COUNT)
        oq = [om.u32(SPQ + 4 * ((om.u32(SPQ_HEAD) + i) % 64)) for i in range(n)]
        st.add("spawn queue", oq == sim.spawn_q, f"{F}: sim {[L.by_pair[p].idx for p in sim.spawn_q]} "
               f"orig {[L.by_pair[p].idx if p in L.by_pair else hex(p) for p in oq]}")
        # entity variables, set_flags bits and trigger bits of the player and the character slots
        pa = om.u32(G_SOLDIER)
        for addr, e in [(pa, sim.player)] + [(a, self.ents.get(om.u32(a))) for a in SLOTS]:
            if e is None or e.removed or om.u32(addr) != e.handle:
                continue
            a, b = bytes(e.vars), om.blk(addr + 0x20, 8)
            st.add("entity variables", a == b, f"{F}: {e}: sim {a.hex()} orig {b.hex()}")
            if e.zone is not None:
                a, b = (e.zone, e.bits), (om.u32(addr + 0x390), om.u32(addr + 0x394))
                st.add("zone bits", a == b, f"{F}: {e}: sim {a} orig {b}")

    def compare_dump_entities(self, F):
        """Every live entity spawned from a record in the later dump (objects and pickups the
        watch doesn't cover too): its variables against the simulator's."""
        m = self.later
        e = m.u32(0x8009D64C)
        n = 0
        while e and n < 1024:
            n += 1
            h = m.u32(e)
            x = self.ents.get(h)
            if m.u32(e + 0x1C) and x is not None and x.rec is not None:
                a, b = bytes(x.vars), m.blk(e + 0x20, 8)
                self.st.add("entity variables (dump)", a == b, f"{F}: {x}: sim {a.hex()} orig {b.hex()}")
            e = m.u32(e + 4)

    def match_queue(self, F, sq, ob, oh, ot):
        """The frame's queued contexts against the ring. Contexts sent by model scripts (op
        0x43) are the engine's: they are taken from the ring and added in their place."""
        sim, st = self.sim, self.st
        out = []
        i = 0
        injected = 0
        inj = set()
        injc = set()
        for k, w in enumerate(ob):
            if i < len(sq) and sq[i] == w:
                out.append(sim.queue[i])
                i += 1
                continue
            e = self.ents.get(w[2])
            if w[6] in MODEL_SENDS and w[5] == 0 and w[3] == 0 and e is not None and e.type in (0x1402, 0x1405) \
                    and (w not in sq[i:]):
                c = Ctx(w[0], self.ents.get(w[1]) if w[1] else None, e, None, w[4], 0, w[6], w[7], "model")
                out.append(c)
                sim.stats[f"model send {w[6]:X}"] += 1
                injected += 1
                inj.add(k)
                injc.add(id(c))
                continue
            if w in sq[i:]:
                j = sq.index(w, i)
                for x in range(i, j):
                    st.add("queued contexts", False, f"{F}: sim only {fmt_ctx(sq[x], sim.queue[x].src)}")
                    out.append(sim.queue[x])
                out.append(sim.queue[j])
                i = j + 1
                continue
            st.add("queued contexts", False, f"{F}: orig only {fmt_ctx(w)}")
        for x in range(i, len(sq)):
            st.add("queued contexts", False, f"{F}: sim only {fmt_ctx(sq[x], sim.queue[x].src)}")
            out.append(sim.queue[x])
        ok = [c.words() for c in out if id(c) not in injc] == [w for k, w in enumerate(ob) if k not in inj]
        st.add("queued contexts", ok and len(sq) == len(ob) - injected,
               f"{F}: (order or count) sim {[fmt_ctx(w) for w in sq]} orig {[fmt_ctx(w) for w in ob]}")
        sim.stats["model sends taken from the ring"] += injected
        sim.queue = out
        st.add("queue indices", (sim.head, (sim.head + len(out)) % 128) == (oh, ot),
               f"{F}: sim {sim.head}..{(sim.head + len(out)) % 128} orig {oh}..{ot}")
        sim.head, sim.tail = oh, ot


def fmt_ctx(w, src=""):
    return (f"[msg {w[6]:X} script {w[7]} pc {w[0]:X} sub {w[1]:X}/{w[4]:X} sender {w[2]:X}/{w[5]:X} "
            f"actor {w[3]:X}{' ' + src if src else ''}]")


def fmt_nodes(ns):
    return [f"{n[0]:X}:{n[1]:X}/{n[2]:X} f{n[3]} {n[4]:X}/{n[5]:X} {s32(n[6])} s{s32(n[8])}" for n in ns]


def main():
    args = sys.argv[1:]
    if args[:1] == ["watch"]:
        print(watch_string())
        return
    if args[:1] != ["check"]:
        print(__doc__)
        return
    args = args[1:]
    verbose = "-v" in args
    args = [a for a in args if a != "-v"]
    only = None
    maxf = None
    if "--level" in args:
        i = args.index("--level")
        only = args[i + 1]
        del args[i:i + 2]
    if "--max" in args:
        i = args.index("--max")
        maxf = int(args[i + 1])
        del args[i:i + 2]
    rebase = []
    while "--rebase" in args:
        i = args.index("--rebase")
        path, _, v = args[i + 1].partition("@")
        rebase.append((int(v), open(path, "rb").read()))
        del args[i:i + 2]
    log_path, trace_path, *dumps = args
    log = WatchLog(log_path)
    starts = sorted({s["start"] for s in log.items["rec"]})
    total = Stats()
    for dump in dumps:
        dump, _, later = dump.partition(",")
        path, _, v = dump.partition("@")
        # the level: the campaign's 0-based mission and level (0x80038660) in the first dump
        name = "%d:%d" % tuple(x + 1 for x in struct.unpack_from("<ii", open(path, "rb").read(), 0x38660))
        later, _, later_v = later.partition("@")
        later = later or None
        later_v = int(later_v) if later_v else None
        k = next(i for i, s in enumerate(starts) if s > int(v))
        f0, f1 = starts[k], starts[k + 1] if k + 1 < len(starts) else 1 << 40
        if only and only != name:
            continue
        r = Run(log, trace_path, path, name, f0, f1, verbose, later=later, later_v=later_v, rebase=rebase)
        st = r.run(maxf)
        print(f"{name}: frames {f0}..{f1}, {len(r.frames)} frames, {len(r.ticks)} ticks")
        st.report()
        print("  events:", dict(sorted(r.sim.stats.items())))
        print("  built-ins called:", dict(sorted(r.sim.calls.items())))


if __name__ == "__main__":
    main()
