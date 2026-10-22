#!/usr/bin/env python3
"""The game logic rebuilt from docs/re as one free-running engine, checked against the original.

  engine.py run LEVEL [--to TICK] [--out TRACE.jsonl] [-v]     e.g. engine.py run 1:3
  engine.py cmp LEVEL [--to TICK] [--max N]                     run, then compare with the original

The engine carries its own state from tick to tick. What it takes from outside is what a new
engine would get from outside (engine_feed.py): the player's input per tick, the frame structure
(which ticks ran in one drawn frame: real time), the idle work done while each frame waited for
the vblank (which messages, spawn retries and handle freeing ran: real time too), the CD's timing
for streamed world chunks, and the random draws by the function that made them. Its starting state is one RAM dump of the
original taken just after the level started (build/engine/seed/), standing for what loading
the level's files produces: the engine does not parse the BSD itself. Everything else, every
think, move, collision, script and message, is the engine's, following the documents; the
reference simulators in this folder provide the rules where they already state them.

It writes the state after every tick in VALOR_TRACE_STATE's format (runtime/src/watch.cpp), so
tools/recon/trace_cmp.py (or `cmp`, which groups the differences by kind) compares it with the
original's trace of the same recording.

The state is kept in the original's memory layout (the dump's RAM, 8 MB wide here: entities the
engine creates live above the console's 2 MB), so the simulators' rules, written against that
layout, apply as they are.
"""
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
sys.path.insert(0, os.path.join(ROOT, "tools", "recon"))

import level_vm as lv  # noqa: E402
import move_sim as ms  # noqa: E402
from engine_feed import Feed, Rand, read_trace  # noqa: E402

# ---------------------------------------------------------------- addresses (docs/re)

G_TICK = 0x8009A5DC
G_PLAYER = 0x8009D638          # the camera entity
G_WORLD = 0x8009D648
G_ENTITIES = 0x8009D64C
G_LAST_EXPLOSION = 0x8009D650
G_SOLDIER = 0x8009D654
G_HANDLES = 0x8009D678
G_SERIALS = 0x8009DA78
G_REMOVED = 0x8009DE78
G_SERIAL = 0x8009D670
G_SLOTS_USED = 0x8009D674
G_SOLDIER_COUNT = 0x80097FE8
G_SOLDIER_CAP = 0x80097FEC
G_VBL_TIMER = 0x8009A5C0
G_CHUNK_LOADING = 0x800AA184
CHUNK_SLOTS = 0x80090DE8
PAD_NOW, PAD_PREV = 0x80093B7C, 0x80093B84
ACTIONS = 0x800B7790
CHAR_SLOTS = [0x800B7B08 + i * 0x2814 for i in range(5)]
HEAP = 0x80200000              # the engine's own allocations

DEAD, REMOVED = 0x20000000, 0x80000


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ---------------------------------------------------------------- memory

class Mem:
    """RAM in the original's layout; addresses 0x80000000.. (8 MB here)."""
    SIZE = 0x800000

    def __init__(self, data):
        self.d = bytearray(self.SIZE)
        self.d[:len(data)] = data

    def u8(self, a):
        return self.d[a & 0x7FFFFF]

    def s8(self, a):
        v = self.d[a & 0x7FFFFF]
        return v - 0x100 if v & 0x80 else v

    def u16(self, a):
        return struct.unpack_from("<H", self.d, a & 0x7FFFFF)[0]

    def s16(self, a):
        return struct.unpack_from("<h", self.d, a & 0x7FFFFF)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.d, a & 0x7FFFFF)[0]

    def s32(self, a):
        return struct.unpack_from("<i", self.d, a & 0x7FFFFF)[0]

    def w8(self, a, v):
        self.d[a & 0x7FFFFF] = v & 0xFF

    def w16(self, a, v):
        struct.pack_into("<H", self.d, a & 0x7FFFFF, v & 0xFFFF)

    def w32(self, a, v):
        struct.pack_into("<I", self.d, a & 0x7FFFFF, v & 0xFFFFFFFF)

    def blk(self, a, n):
        a &= 0x7FFFFF
        return bytes(self.d[a:a + n])

    def wblk(self, a, data):
        a &= 0x7FFFFF
        self.d[a:a + len(data)] = data

    def copy(self, dst, src, n):
        dst &= 0x7FFFFF
        src &= 0x7FFFFF
        self.d[dst:dst + n] = self.d[src:src + n]

    def zero(self, a, n):
        a &= 0x7FFFFF
        self.d[a:a + n] = bytes(n)

    def entities(self):
        out, e = [], self.u32(G_ENTITIES)
        while e and len(out) < 1024:
            out.append(e)
            e = self.u32(e + 4)
        return out


# ---------------------------------------------------------------- the engine

class Engine:
    def __init__(self, level, seed_path, feed, frames, log=print):
        from engine_level import LevelLogic
        from engine_world import Collision
        self.level = level
        self.log = log
        self.feed = feed
        self.frames = frames                 # [(vblank, [tick, ...])], from the original's trace
        data = open(seed_path, "rb").read()
        m, l = level.split(":")
        bsd = open(os.path.join(ROOT, "extracted", "moh", "DATA", f"MSN{m}", f"LVL{l}", f"{m}_{l}.BSD"), "rb").read()
        self.L = lv.Level(data, level)
        self.m = Mem(data)
        self.L.mem = self.m
        self.rand = Rand(feed, level)
        self.stats = Counter()
        self.notes = defaultdict(list)       # things the engine did not do, by kind, with ticks
        self.heap_next = HEAP
        self.free_blocks = defaultdict(list)
        self.block_size = {}
        self.ents = {}                       # address -> Ent (level logic's view)
        self.world = Collision(self)
        from engine_audio import Audio
        self.audio = Audio(self)
        self.logic = LevelLogic(self, bsd)
        self.tick_no = self.m.u32(G_TICK)
        self.frame_ticks = 0
        self.vblank = 0
        self.vblank_prev = None
        self.think = {}                      # think address -> handler
        self.handlers = {}
        import engine_player
        import engine_objects
        import engine_enemy
        engine_enemy.register(self)
        engine_player.register(self)
        engine_objects.register(self)
        import engine_projectile
        engine_projectile.register(self)
        import engine_combat
        engine_combat.register(self)
        import engine_attach
        engine_attach.register(self)
        import engine_planes
        engine_planes.register(self)
        import engine_grenade
        engine_grenade.register(self)
        import engine_explosion
        engine_explosion.register(self)
        self.logic.adopt()

    # -- notes: what the engine left out, kept to report
    def note(self, kind, detail=""):
        self.stats[kind] += 1
        if len(self.notes[kind]) < 5:
            self.notes[kind].append((self.tick_no, detail))

    # -- memory blocks
    def alloc(self, n):
        n = (n + 15) & ~15
        if self.free_blocks[n]:
            a = self.free_blocks[n].pop()
        else:
            a = self.heap_next
            self.heap_next += n
            if self.heap_next >= 0x80000000 + Mem.SIZE:
                raise MemoryError("engine heap full")
        self.block_size[a] = n
        self.m.zero(a, n)
        return a

    def free(self, a):
        n = self.block_size.pop(a, None)
        if n:
            self.free_blocks[n].append(a)

    # -- the entity list (entities.md, Lists and handles)
    def entities(self):
        return self.m.entities()

    def link_front(self, e):
        """entity_link_front: right after the camera, the list's head."""
        m = self.m
        head = m.u32(G_PLAYER)
        if not head:
            m.w32(e + 4, m.u32(G_ENTITIES))
            m.w32(G_ENTITIES, e)
            return
        m.w32(e + 4, m.u32(head + 4))
        m.w32(head + 4, e)

    def link_end(self, e):
        m = self.m
        prev, cur = 0, m.u32(G_ENTITIES)
        while cur:
            prev, cur = cur, m.u32(cur + 4)
        m.w32(e + 4, 0)
        if prev:
            m.w32(prev + 4, e)
        else:
            m.w32(G_ENTITIES, e)

    def unlink(self, e):
        m = self.m
        prev, cur = 0, m.u32(G_ENTITIES)
        while cur and cur != e:
            prev, cur = cur, m.u32(cur + 4)
        if not cur:
            return
        if prev:
            m.w32(prev + 4, m.u32(e + 4))
        else:
            m.w32(G_ENTITIES, m.u32(e + 4))

    def handle_alloc(self, e):
        """handle_alloc (0x8006D930): serial << 16 | slot; the next slot while the high-water
        mark is below 256, else the first free one."""
        m = self.m
        used = m.u32(G_SLOTS_USED)
        if used < 256:
            slot = used
            m.w32(G_SLOTS_USED, used + 1)
        else:
            slot = next(i for i in range(256) if not m.u32(G_HANDLES + 4 * i))
        serial = (m.u32(G_SERIAL) + 1) & 0xFFFF
        m.w32(G_SERIAL, serial)
        h = (serial << 16) | slot
        m.w32(G_HANDLES + 4 * slot, e)
        m.w32(G_SERIALS + 4 * slot, serial)
        m.w32(G_REMOVED + 4 * slot, 0)
        m.w32(e, h)
        return h

    def resolve(self, h):
        return self.m.u32(G_HANDLES + 4 * (h & 0xFFFF)) if h else 0

    def handle_dead(self, h):
        m = self.m
        if not h:
            return True
        slot = h & 0xFFFF
        return m.u32(G_SERIALS + 4 * slot) != (h >> 16) or not m.u32(G_HANDLES + 4 * slot)

    def handles_free_removed(self):
        """0x8006DA88, at the end of the frame."""
        m = self.m
        for slot in range(256):
            if m.u32(G_REMOVED + 4 * slot):
                e = m.u32(G_HANDLES + 4 * slot)
                for g in (G_LAST_EXPLOSION, G_SOLDIER, 0x8009D644):
                    if m.u32(g) == e:
                        m.w32(g, 0)
                m.w32(G_HANDLES + 4 * slot, 0)
                m.w32(G_SERIALS + 4 * slot, 0)
                m.w32(G_REMOVED + 4 * slot, 0)
        used = m.u32(G_SLOTS_USED)
        while used and not m.u32(G_HANDLES + 4 * (used - 1)):
            used -= 1
        m.w32(G_SLOTS_USED, used)

    # -- creating and removing (entities.md, Creating, activating and removing entities)
    def entity_create(self, etype, obj_size=0, model=0, addr=None, parts=None, channels=0, list_n=0,
                      instance=False):
        """entity_create (0x8006B5B8): the entity, its object, the animation instance, the hit
        parts, the script channels' blocks; a handle; the defaults (0x8006B350)."""
        m = self.m
        nparts = (m.u16(model + 0xBE) if model else 0) if parts is None else parts
        inst_size = 0xB0 if model or instance else 0
        size = 0x3CC + obj_size + inst_size + 0x48 * nparts + 2 * 0x3C * channels + 0x1C * list_n
        e = addr if addr is not None else self.alloc(size)
        if addr is not None:
            m.zero(e, 0x3CC)
        p = e + 0x3CC
        if obj_size:
            m.w32(e + 0x388, p)
            p += obj_size
        if inst_size:
            m.w32(e + 0x100, p)
            p += inst_size
        m.w16(e + 0x36, nparts)
        if nparts:
            m.w32(e + 0x304, p)
            if model:
                m.copy(p, m.u32(model + 0xC0), 0x48 * nparts)
            p += 0x48 * nparts
        m.w32(e + 0x1EC, channels)
        if channels:
            m.w32(e + 0x124, p)
            m.w32(e + 0x104 + 0x20, p)
            p += 0x3C * channels
            m.w32(e + 0x198, p)
            m.w32(e + 0x178 + 0x20, p)
            p += 0x3C * channels
        m.w16(e + 0x2EE, list_n)
        if list_n:
            m.w32(e + 0x300, p)
        m.w32(e + 8, etype)
        self.handle_alloc(e)
        m.w32(e + 0x390, 0xFFFF)
        m.w32(e + 0x394, 0)
        m.w32(e + 0x398, 0xFFFF)
        m.w32(e + 0x39C, 0)
        self.entity_defaults(e)
        m.w32(e + 8, etype)
        if model:
            self.instance_init(e, model)
        self.stats[f"created {etype:X}"] += 1
        return e

    def entity_defaults(self, e):
        """entity_defaults (0x8006B350)."""
        m = self.m
        for o in (0xC, 0x10, 0x18, 0x28, 0x2C, 0x30, 0xF8, 0x298, 0x36C, 0x370, 0x374, 0x378, 0x37C, 0x380,
                  0x384, 0x20C, 0x210, 0x214, 0x228, 0x22C, 0x230, 0x218, 0x21C, 0x220, 0x200, 0x204, 0x1FC,
                  0x24C, 0x250, 0x254, 0x1F4, 0x2D4, 0x44, 0x48, 0x3C, 0x40, 0x38, 0x3A0):
            m.w32(e + o, 0)
        m.w32(e + 0xF4, 0x640000)
        m.w32(e + 0x294, 1)
        m.w32(e + 0x290, 1)
        for k in range(3):
            m.w32(e + 0x270 + 4 * k, 0x10000)
        inst = m.u32(e + 0x100)
        if inst:
            m.w16(inst + 0x5C, -1)
            m.w16(inst + 0x5E, 0)
            m.w16(inst + 0x62, -1)
            m.w16(inst + 0x64, 0)
            m.w16(inst + 0x3E, 0)
            m.w32(inst + 8, 0)
        for ch in (0, 1):
            c = e + 0x74 * ch
            m.w32(c + 0x114, 0)
            m.w16(c + 0x11E, -1)
            for o in (0x104, 0x108, 0x10C, 0x110):
                m.w32(c + o, 0)
            m.w16(c + 0x122, 0)
            m.w16(c + 0x120, 0)
            m.w16(c + 0x11C, -1)
        m.w32(e + 0x288, 1)
        m.w32(e + 0x28C, 1)
        m.w32(e + 0x284, 1)
        m.w16(e + 0x36, 0)
        for o in (0x2D8, 0x2DA, 0x2DC, 0x2E0, 0x2E2, 0x2E4, 0x2EC):
            m.w16(e + o, 0)
        m.w16(e + 0x2E8, 0x64)
        for k in range(3):
            m.w8(e + 0x2A0 + k, 0x40)
            m.w16(e + 0x2A4 + 2 * k, 0x80)
            m.w16(e + 0x2AC + 2 * k, 0x1000)
        m.w32(e + 0x224, 0x10000)
        for o in (0x2B4, 0x2B6, 0x2B8, 0x2BC, 0x2BE, 0x2C0, 0x2C4, 0x2C6, 0x2C8, 0x2CC, 0x2CE, 0x2D0):
            m.w16(e + o, 0)
        m.w16(e + 0x2EA, -1)
        m.w16(e + 0x3C8, 0x64)
        m.w16(e + 0x3CA, -1)

    def instance_init(self, e, model):
        """model_instance_init (0x8006A96C), the fields animation reads (animation.md)."""
        m = self.m
        inst = m.u32(e + 0x100)
        m.w32(inst + 8, model)
        m.w16(inst + 0x3E, 0)
        m.w16(inst + 0x40, -1)
        m.w16(inst + 0x5C, -1)
        m.w16(inst + 0x5E, 0)
        m.w16(inst + 0x62, -1)
        m.w16(inst + 0x64, -1)
        tbl = m.u32(model + 8)
        m.w32(inst + 0x74, tbl)
        if tbl:
            m.w16(inst + 0x70, m.u16(tbl))
            m.w32(inst + 0x78, tbl + 4)
        m.w32(inst + 0x7C, m.u32(model + 0x38))
        m.w32(inst + 0x80, m.u32(model + 0x84))
        m.w32(inst + 0x84, m.u32(model + 0x9C))
        for k in range(3):
            m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k))
        m.w32(e + 0x2D4, m.u32(model + 0xA0))

    def model_swap(self, e, model, flags):
        """func_8006B234 + func_8006B1C4 -> model_instance_init (0x8006A96C) on a live entity (the
        player's weapon change): the instance rebuilt for the new model with fresh pose buffers
        (frame 0's quaternions in +0x8C and +0x90, none current), the root offset (+0x98..), the
        static vertices (+0x2C), no animation; the entity's eye height, scale, part count and
        parts from the model's templates (animation.md, The instance)."""
        m = self.m
        inst = m.u32(e + 0x100)
        m.w32(inst + 0x6C, 0x80)
        m.w32(inst + 0x68, 0)
        m.w32(inst + 4, flags)
        m.w32(inst + 8, model)
        tbl = m.u32(model + 8)
        nb = m.u8(m.u32(tbl + 4) + 1) if tbl else 0
        buf = self.alloc(max(0x18 * nb, 0x10))
        m.w32(inst, buf)
        if tbl:
            m.w16(inst + 0x70, m.u16(tbl))
            m.w32(inst + 0x78, tbl + 4)
            m.w16(inst + 0x72, nb)
            m.w32(inst + 0x74, tbl)
            m.w32(inst + 0x7C, m.u32(model + 0x38))
            m.w32(inst + 0x80, m.u32(model + 0x84))
            m.w32(inst + 0x90, buf)
            m.w32(inst + 0x8C, buf + 8 * nb)
            m.w32(inst + 0x94, buf + 16 * nb)
            m.w32(inst + 0x84, m.u32(model + 0x9C))
            first = m.u32(tbl + 4)
            m.copy(buf + 8 * nb, m.u32(first + 0x10), 8 * nb)
            m.copy(buf, m.u32(first + 0x10), 8 * nb)
            m.w32(inst + 0x88, 0)
            w = m.u32(first + 4)
            m.w16(inst + 0x98, s32(w << 22) >> 22)
            m.w16(inst + 0x9A, s32(w << 11) >> 21)
            m.w16(inst + 0x9C, s32(w << 1) >> 22)
        else:
            for o in (0x74, 0x7C, 0x84, 0x88, 0x90, 0x78, 0x80, 0x8C, 0x94):
                m.w32(inst + o, 0)
            m.w16(inst + 0x70, 0)
            m.w16(inst + 0x72, 0)
        m.w16(inst + 0x3C, m.u16(model + 0x8C))
        if flags & 2:
            m.w32(inst + 0x2C, m.u32(model + 0x88))
        m.w16(inst + 0x42, m.u16(model + 0x8E))
        m.w16(inst + 0x5C, -1)
        m.w16(inst + 0x5E, 0)
        m.w32(inst + 0x54, 0)
        m.w32(inst + 0x58, 0)
        m.w32(e + 0x2D4, m.u32(model + 0xA0))
        for k in range(3):
            m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k))
        n = m.u16(model + 0xBE)
        m.w16(e + 0x36, n)
        pp, tp = m.u32(e + 0x304), m.u32(model + 0xC0)
        for i in range(n):
            q, t = pp + 0x48 * i, tp + 0x48 * i
            m.w32(q + 4, m.u32(t + 4))
            m.w32(q + 8, m.u32(t + 0xC))
            m.w32(q + 0xC, m.u32(t + 0xC))
            m.w16(q + 0x1C, m.u16(t + 0x1C))
            m.w16(q + 0x1E, m.u16(t + 0x1E))
            for o in (0x34, 0x30, 0x38, 0x10, 0x14):
                v = m.u32(t + o)
                m.w32(q + o, s32(v * 2) if flags & 0x200 and o in (0x30, 0x34, 0x38) else v)
            for o in (0x40, 0x42, 0x44):
                m.w16(q + o, m.u16(t + o))
            m.w32(q + 0x18, inst)
            m.w32(q + 0x3C, 0)

    def remove(self, e, why=""):
        """entity_remove (0x8006B8F0): flagged, the record's state and messages, unlinked, the
        handle flagged for the end of the frame, the destructor."""
        m = self.m
        if m.u32(e + 0xC) & REMOVED:
            return
        m.w32(e + 0xC, m.u32(e + 0xC) | REMOVED)
        ent = self.ents.get(e)
        self.unlink(e)
        h = m.u32(e)
        m.w32(G_REMOVED + 4 * (h & 0xFFFF), 1)
        self.stats[f"removed {m.s32(e + 8):X} ({why})"] += 1
        d = self.handlers.get(("destroy", m.s32(e + 8)))
        if d:
            d(e)
        if ent is not None:
            self.logic.removed(ent, why)
        if e >= HEAP:
            self.free(e)

    # -- input (engine_feed.py)
    def action_pressed(self, a, port=0):
        mask = self.m.u32(ACTIONS + 4 * (port * 9 + a)) & 0xFFFF
        return bool(self.pad_now & mask) and not (self.pad_prev & mask)

    def action_held(self, a, port=0):
        mask = self.m.u32(ACTIONS + 4 * (port * 9 + a)) & 0xFFFF
        return bool(self.pad_now & mask)

    def role(self, r, port=0):
        return self.feed.role(self.level, self.tick_no, port, r)

    def role_alt(self, r, port=0):
        return self.feed.role_alt(self.level, self.tick_no, port, r)

    # -- one tick (entities.md, A frame; objects.md, The frame and the tick)
    def tick(self, first_in_frame):
        m = self.m
        self.tick_no += 1
        m.w32(G_TICK, self.tick_no)
        self.rand.start_tick(self.tick_no)
        tr = os.environ.get("ENG_TRACE_LEVEL")      # FROM:TO, the level logic's own trace
        if tr:
            lo, hi = (int(x) for x in tr.split(":"))
            self.logic.debug = lo <= self.tick_no <= hi
            if self.logic.debug:
                print(f"-- tick {self.tick_no}")
        pad = self.feed.pad.get((self.level, self.tick_no))
        if pad:
            self.pad_now, self.pad_prev = pad[0] & 0xFFFF, pad[2] & 0xFFFF
            m.w32(PAD_NOW, pad[0])
            m.w32(PAD_NOW + 4, pad[1])
            m.w32(PAD_PREV, pad[2])
            m.w32(PAD_PREV + 4, pad[3])
        self.entities_update()
        self.entities_collide()
        self.timer_message()

    def entities_update(self):
        m = self.m
        from engine_enemy import witness_check
        witness_check(self)
        e = m.u32(G_ENTITIES)
        while e:
            nxt = m.u32(e + 4)
            if not m.u32(e + 0xC) & REMOVED:
                self.update(e)
            e = nxt

    def update(self, e):
        """entities_update for one entity (0x8006BA28; entities.md, The tick): the think, the
        event words cleared, the move, the triggers, the world contact, the brain, the matrix,
        the player's weapon, the bounds."""
        m = self.m
        t = m.s32(e + 8)
        think = m.u32(e + 0x36C)
        if think:
            fn = self.think.get(think)
            if fn:
                fn(e)
            else:
                self.note(f"think {think:08X} (type {t:X}) not implemented")
            if m.u32(e + 0xC) & REMOVED:
                return
            m.w32(e + 0x38, 0)
            m.w32(e + 0x3C, 0)
            m.w32(e + 0x40, 0)
            # attached (+0x30): to a stale or removed target it is let go; else it sits on the
            # target's bone +0x34 (model_bone_point) and does not move itself
            attach = self.resolve(m.u32(e + 0x30))
            if attach:
                if self.handle_dead(m.u32(e + 0x30)) or m.u32(attach + 0xC) & REMOVED:
                    m.w32(e + 0x30, 0)
                else:
                    from engine_sense import model_bone_point
                    p = model_bone_point(self, attach, m.s8(e + 0x34))
                    for k in range(3):
                        m.w32(e + 0x20C + 4 * k, s32(s16(p[k]) << 16))
            attach = self.resolve(m.u32(e + 0x30))
            if not attach and not m.u32(e + 0xC) & 0x20000:
                from engine_move import entity_move
                res = entity_move(self, e)
                if m.u32(e + 0xC) & 5:
                    self.zones_update(e)
                if res:
                    wc = self.handlers.get(("world_contact", m.u32(e + 0x378)))
                    if wc:
                        wc(e, res)
                    elif m.u32(e + 0x378):
                        self.note(f"world_contact {m.u32(e + 0x378):08X} not implemented")
            if not m.u32(e + 0xC) & REMOVED and m.u32(e + 0x38C):
                from engine_enemy import brain_update
                brain_update(self, e)
        if not m.u32(e + 0x18) & 2:
            m.w32(e + 0x18, m.u32(e + 0x18) & ~1)
        self.matrix(e)
        if m.u32(e + 0xC) & 1:
            from engine_player import player_weapon_update
            player_weapon_update(self, e)
        from engine_anim import entity_bounds
        entity_bounds(self, e)
        if t == 2:
            from engine_camera import camera_setup
            camera_setup(self, e)

    def matrix(self, e):
        """0x8006A76C: +0x340 = RotMatrixYXZ of the angles, the translation +0x354 = the integer
        position (TransMatrix), then ScaleMatrix by the scale +0x270 >> 4 (each column times its
        axis' scale >> 12; 1.0 leaves it) (enemies.md, The brain)."""
        import brain_sim as bs
        m = self.m
        rot = bs.rot_yxz(s16(m.s32(e + 0x228) >> 16), s16(m.s32(e + 0x22C) >> 16), s16(m.s32(e + 0x230) >> 16))
        sc = [m.s32(e + 0x270 + 4 * k) >> 4 for k in range(3)]
        for i in range(3):
            for k in range(3):
                v = rot[3 * i + k]
                if sc[k] != 0x1000:
                    v = s16((v * sc[k]) >> 12)
                m.w16(e + 0x340 + 6 * i + 2 * k, v)
        for k in range(3):
            m.w32(e + 0x354 + 4 * k, m.s16(e + 0x20E + 4 * k))

    def zones_use(self, e):
        """player_use (0x80084670): the Use lists of the records whose bits are set."""
        ent = self.ents.get(e)
        if ent is not None:
            self.logic.use(ent)

    def zones_update(self, e):
        """zones_update (0x800845A8): only for flags A & 0x51 (levels.md, Triggers)."""
        m = self.m
        if not m.u32(e + 0xC) & 0x51:
            return
        ent = self.ents.get(e)
        if ent is None:
            return
        pos = tuple(s16(m.s32(e + 0x20C + 4 * k) >> 16) for k in range(3))
        self.logic.zones_update(ent, m.u32(e + 0x390), pos)

    def entities_collide(self):
        from engine_combat import collide
        collide(self)

    def timer_message(self):
        """The 1/3-second message: the vblank counter at 10 or more, -20 and send 0x59."""
        m = self.m
        if m.s32(G_VBL_TIMER) >= 10:
            m.w32(G_VBL_TIMER, m.s32(G_VBL_TIMER) - 20)
            self.logic.send(0x59, None, 0, None, src="timer")

    def end_frame(self):
        """After the frame's ticks: the drawing pass's work (out-of-sight counters), then the work
        done while waiting for the vblank (frame_wait_vblank, 0x80090B80; objects.md, The frame
        and the tick): one frame_idle_job (0x80090A30), then frame_idle_loop (0x80090AEC) until
        the vblank. How many of each ran is real time: taken from the recording's trace."""
        from engine_draw import draw_pass
        draw_pass(self)
        self.idle_work()
        self.audio.update()

    def idle_work(self):
        """The frame's wait for the vblank as recorded: frame_idle_job / frame_idle_loop calls."""
        frames = self.feed.idle.get((self.level, self.tick_no))
        jobs = frames.pop(0) if frames else None
        if jobs is None:
            self.note("idle work: no record of the frame (all messages, spawns, handles)")
            self.logic.run_queue()
            self.handles_free_removed()
        else:
            for j in jobs:
                if j is None:
                    self.idle_loop()
                else:
                    self.idle_job(j)

    def idle_job(self, state):
        """frame_idle_job (0x80090A30), round-robin 0x800AB8E8: 0 one message; 1 one message and
        one spawn retry; 2 one message, the removed handles freed, one more message (and the chunk
        streaming check), then back to 0."""
        m = self.m
        if state != s32(m.u32(0x800AB8E8)):
            self.note("idle job state differs from the original's", f"{s32(m.u32(0x800AB8E8))} / {state}")
        self.msg_one()
        if state == 1:
            self.logic.spawn_retry_one()
        elif state == 2:
            self.handles_free_removed()
            self.msg_one()
        elif state == 3:
            pass
        m.w32(0x800AB8E8, 0 if state >= 2 else state + 1)

    def idle_loop(self):
        """frame_idle_loop (0x80090AEC), round-robin 0x800AB8EC: 0 all messages, 1 all spawn
        retries, 2 a frame_idle_job (traced on its own), 3 nothing (back to 0)."""
        m = self.m
        k = s32(m.u32(0x800AB8EC))
        if k == 0:
            self.logic.run_queue_all()
        elif k == 1:
            self.logic.spawn_retry()
        m.w32(0x800AB8EC, k + 1 if 0 <= k < 3 else 0)

    def msg_one(self):
        """msg_run_one: the queue's first context, if any."""
        self.logic.run_one()

    def run(self, to=None, out=None, compare=None):
        """Every frame of the recording's level: vblanks advance the timer, ticks run."""
        f = open(out, "w") if out else None
        for vb, ticks in self.frames:
            if to and ticks and ticks[0] > to:
                break
            # vblanks since the last frame feed the 1/3-second counter (the vblank handler)
            if self.vblank_prev is not None:
                self.m.w32(G_VBL_TIMER, self.m.s32(G_VBL_TIMER) + (vb - self.vblank_prev))
            self.vblank_prev = vb
            self.vblank = vb
            self.chunks_at(vb)
            from engine_enemy import char_pool_update
            char_pool_update(self)
            if not ticks:
                # a paused frame (no tick due): the timer reset, then the draw and the idle work
                # all the same (game_frame, objects.md)
                self.m.w32(G_VBL_TIMER, 0)
            for i, t in enumerate(ticks):
                if t != self.tick_no + 1:
                    self.note("tick numbering", f"{t} after {self.tick_no}")
                self.tick(i == 0)
                line = self.state_line()
                if f:
                    f.write(json.dumps(line, separators=(",", ":")) + "\n")
                if compare:
                    compare(line)
            path = getattr(self, "dumps", {}).get(self.tick_no)
            if path:
                open(path, "wb").write(bytes(self.m.d))
            self.end_frame()
            path = getattr(self, "dumps", {}).get(f"{self.tick_no}a")
            if path:
                open(path, "wb").write(bytes(self.m.d))
        if f:
            f.close()

    def chunks_at(self, vb):
        """The world chunks loaded, as the CD's timing had them (engine_feed: the slot table)."""
        self.world.at(vb)

    # -- the trace line (runtime/src/watch.cpp, hook_texts_update)
    def state_line(self):
        m = self.m
        out = {"tick": self.tick_no, "vblank": self.vblank}
        sol = m.u32(G_SOLDIER)
        if sol:
            look = m.u32(sol + 0x388)
            inv = sol + 0x3CC
            slot = m.u8(inv + 0x54)
            out["player"] = [m.s32(look + 0x184) if look else 0, m.s32(look + 0x178) if look else 0, slot,
                             m.u8(inv + 0x55), m.s16(inv + 0x66 + 2 * slot), m.s16(inv + 0x56 + 2 * slot),
                             m.u32(sol + 0x10)]
        ents = []
        for e in m.entities():
            inst = m.u32(e + 0x100)
            ents.append([m.u32(e), m.s32(e + 8), m.u32(e + 0xC), m.s32(e + 0x20C), m.s32(e + 0x210),
                         m.s32(e + 0x214), m.s32(e + 0x228), m.s32(e + 0x22C), m.s32(e + 0x230),
                         m.s32(e + 0xF4), m.s16(inst + 0x5C) if inst else -1, m.s16(inst + 0x5E) if inst else -1])
        out["ents"] = ents
        return out


# ---------------------------------------------------------------- the recording

LEVEL_SEED = {"1:3": 1714, "2:1": 20496, "2:2": 40737}
ORIG_TRACE = os.path.join(ROOT, "build", "engine", "orig.jsonl")
ORIG_LOG = os.path.join(ROOT, "build", "engine", "orig.log")


def level_lines(trace, feed, level):
    """The original's trace lines of one level (the marks split them)."""
    marks = feed.marks
    i = [k for k, (v, l) in enumerate(marks) if l == level][0]
    lo, hi = marks[i][0], marks[i + 1][0] if i + 1 < len(marks) else 1 << 40
    return [d for d in trace if lo <= d["vblank"] < hi]


FRAMES_LOG = os.path.join(ROOT, "build", "engine", "frames.log")
FRAME_ONLY = re.compile(r"^watch (\d+) fc\+0: ")
FRAME_WATCH = re.compile(r"^watch (\d+) fc\+4: ([0-9A-F]+) -> ([0-9A-F]+)")


def frames_of(lines):
    """The frames of the level's ticks, (vblank, [ticks]). From a watch of the frame and tick
    counters (`VALOR_WATCH="fc=8009A5D8:8"`, build/engine/frames.log: one line per frame, the
    tick counter's change giving the ticks it ran) when there is one; the trace's per-tick
    vblank is not the frame (a frame's second tick can be stamped a vblank later)."""
    if os.path.exists(FRAMES_LOG) and lines:
        want = {d["tick"] for d in lines}
        lo = lines[0]["vblank"] - 2
        hi = lines[-1]["vblank"] + 2
        out = []
        for line in open(FRAMES_LOG, errors="replace"):
            mm = FRAME_WATCH.match(line)
            if not mm:
                continue
            vb = int(mm[1])
            if not lo <= vb <= hi:
                continue
            a, b = int(mm[2], 16), int(mm[3], 16)
            ticks = [t for t in range(a + 1, b + 1) if t in want]
            if ticks:
                out.append((vb, ticks))
        if out and sum(len(t) for _, t in out) == len(want):
            # frames with no tick between the level's first and last (paused): the frame
            # counter (fc+0) moved, the tick counter did not
            ticked = {vb for vb, _ in out}
            first, last = out[0][0], out[-1][0]
            for line in open(FRAMES_LOG, errors="replace"):
                mm = FRAME_ONLY.match(line)
                if mm and first < int(mm[1]) < last and int(mm[1]) not in ticked:
                    out.append((int(mm[1]), []))
            out.sort(key=lambda f: f[0])
            return out
    out = []
    for d in lines:
        if out and out[-1][0] == d["vblank"]:
            out[-1][1].append(d["tick"])
        else:
            out.append((d["vblank"], [d["tick"]]))
    return out


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("run", "cmp"):
        print(__doc__)
        return
    cmd, level = args[0], args[1]
    to = int(args[args.index("--to") + 1]) if "--to" in args else None
    out = args[args.index("--out") + 1] if "--out" in args else os.path.join(ROOT, "build", "engine", f"eng_{level.replace(':', '_')}.jsonl")
    feed = Feed(ORIG_LOG)
    trace = read_trace(ORIG_TRACE)
    lines = level_lines(trace, feed, level)
    seed_v = LEVEL_SEED[level]
    seed = os.path.join(ROOT, "build", "engine", "seed", f"d{seed_v}.bin")
    sm = Mem(open(seed, "rb").read())
    t0 = sm.u32(G_TICK)
    # the level's lines end where the tick counter starts again (the next level's first tick can
    # come before its mark)
    for i in range(1, len(lines)):
        if lines[i]["tick"] < lines[i - 1]["tick"]:
            lines = lines[:i]
            break
    before = [d for d in lines if d["tick"] <= t0]
    lines = [d for d in lines if d["tick"] > t0]
    eng = Engine(level, seed, feed, frames_of(lines))
    # --dump TICK:PATH writes RAM after the tick's update, TICKa:PATH after its frame's end (drawn)
    eng.dumps = {(int(k) if k.isdigit() else k): v for k, v in
                 (a.split(":", 1) for i, a in enumerate(args) if i and args[i - 1] == "--dump")}
    # the seed is the state at the drawing of tick t0's frame: the frame's end comes first
    # the seed's own tick's vblank (the dump at vblank V holds the tick stamped V - 1)
    vb0 = before[-1]["vblank"] if before else seed_v - 1
    eng.vblank_prev = vb0
    eng.world.skip_to(vb0)
    eng.audio.adopt(vb0)
    # the seed frame's own wait (its idle work as recorded)
    eng.tick_no = t0
    eng.idle_work()
    from engine_cmp import Compare
    cmpr = Compare(lines, maxn=int(args[args.index("--max") + 1]) if "--max" in args else 20) if cmd == "cmp" else None
    try:
        eng.run(to=to, out=out, compare=cmpr.line if cmpr else None)
    finally:
        print(f"ran to tick {eng.tick_no}; trace {out}")
        for k, v in sorted(eng.stats.items()):
            if k.startswith("think") or "not" in k:
                print(f"  {k}: {v}  {eng.notes.get(k, '')}")
        if cmpr:
            cmpr.report()


if __name__ == "__main__":
    main()
