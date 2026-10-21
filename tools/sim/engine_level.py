"""The engine's level logic: level_vm.py's simulator (docs/re/levels.md, messages.md) driven by
the engine's own entities instead of the original's trace."""
import level_vm as lv
from level_vm import Ctx, s32, s16

G_ALARM = 0x80097FE0
RING = 0x800B6460


class Ent(lv.Ent):
    """An entity as the level logic sees it, backed by the engine's memory: flags A, the variables
    (+0x20..+0x27), the zone fields (+0x390..+0x39C) are the entity's own words."""

    def __init__(self, eng, addr, rec):
        m = eng.m
        self._eng, self.addr = eng, addr
        lv.Ent.__init__(self, m.u32(addr), m.s32(addr + 8), rec, addr)
        self.vars = memoryview(m.d)[(addr + 0x20) & 0x7FFFFF:(addr + 0x28) & 0x7FFFFF]

    m = property(lambda self: self._eng.m)
    flags = property(lambda self: self.m.u32(self.addr + 0xC), lambda self, v: None)
    zone = property(lambda self: self.m.u32(self.addr + 0x390),
                    lambda self, v: v is not None and self.m.w32(self.addr + 0x390, v))
    bits = property(lambda self: self.m.u32(self.addr + 0x394), lambda self, v: self.m.w32(self.addr + 0x394, v))
    prev_zone = property(lambda self: self.m.u32(self.addr + 0x398), lambda self, v: self.m.w32(self.addr + 0x398, v))
    prev_bits = property(lambda self: self.m.u32(self.addr + 0x39C), lambda self, v: self.m.w32(self.addr + 0x39C, v))
    flags40 = property(lambda self: self.m.u32(self.addr + 0x40), lambda self, v: self.m.w32(self.addr + 0x40, v))
    pos = property(lambda self: tuple(s16(self.m.s32(self.addr + 0x20C + 4 * k) >> 16) for k in range(3)),
                   lambda self, v: None)


class LevelLogic(lv.Sim):
    def __init__(self, eng, bsd):
        self.eng = eng
        lv.Sim.__init__(self, eng.L, bsd, log=eng.log)
        # one memory: the engine's (the seed was taken after the level start, so nothing of the
        # disc's bytes needs restoring: restore_bsd above wrote into the copy it made, dropped here)
        self.m = eng.m
        self.L.mem = eng.m
        self.removed_by_script = set()
        self.alarm_requests = []
        self.oracle_spawn = self.spawn

    def adopt(self):
        """The level logic's state as the seed holds it: an Ent for every live entity, the
        queued contexts in the ring, the spawn queue, the held ladder."""
        m, eng = self.m, self.eng
        for e in m.entities():
            self.bind(e)
        self.player = eng.ents.get(m.u32(lv.G_SOLDIER))
        head, tail = m.u32(lv.G_HEAD), m.u32(lv.G_TAIL)
        self.head, self.tail = head, tail
        self.queue = []
        i = head
        while i != tail:
            w = [m.u32(RING + 32 * i + 4 * k) for k in range(8)]
            ent = lambda h: self.handles.get(h) if h else None
            self.queue.append(Ctx(w[0], ent(w[1]), ent(w[2]), ent(w[3]), w[4], w[5], w[6], w[7], "seed"))
            i = (i + 1) % 128
        n = m.u32(lv.SPQ_COUNT)
        self.spawn_q = [m.u32(lv.SPQ + 4 * ((m.u32(lv.SPQ_HEAD) + i) % 64)) for i in range(n)]
        self.ladder_held = m.u32(0x8009A5B8)

    def bind(self, e):
        m, eng = self.m, self.eng
        pair = m.u32(e + 0x1C)
        rec = self.L.by_pair.get(pair) if pair else None
        if m.s32(e + 8) == 1:
            rec = next((r for r in self.L.records if r.cls == 0x8877B646 and r.p34 == 0), rec)
        ent = Ent(eng, e, rec)
        eng.ents[e] = ent
        self.handles[ent.handle] = ent
        return ent

    # -- spawning (levels.md, Spawning): the engine makes the entity
    def spawn(self, rec):
        from engine_objects import record_spawn
        e = record_spawn(self.eng, rec)
        if e is None:
            return None
        return self.bind(e)

    def record_spawn(self, pair):
        """record_spawn (0x8007505C) -> record_spawn_model (0x80074C54): the entity made and its
        scripts run at the origin, linked at the front, the record's bit 0, +0x1C, message 0 to
        its entity script, and only then its position and angles (0x80074AD8)."""
        from engine_objects import record_spawn, place
        rec = self.L.by_pair[pair]
        if rec.kind not in (2, 3, 4, 6):
            return
        self.stats["spawn requests"] += 1
        e = record_spawn(self.eng, rec)
        if e is None:
            self.stats["spawn refused (queued)"] += 1
            if len(self.spawn_q) >= 64:
                self.spawn_q.pop(0)
            self.spawn_q.append(pair)
            return
        m = self.m
        self.eng.link_front(e)
        m.w32(pair, m.u32(pair) | 1)
        m.w32(e + 0x1C, pair)
        ent = self.bind(e)
        ent.rec = rec
        self.msg_start(ent, 0)
        if rec.kind != 4:
            place(self.eng, e, rec)

    # -- ladders (levels.md, Triggers): the engine's entities, the held ladder in 0x8009A5B8
    def kind0_enter(self, ent, r):
        if r.cls != 0x3011B913:
            return lv.Sim.kind0_enter(self, ent, r)
        self.stats[f"kind0 enter {r.cls:08X}"] += 1
        self.record_spawn_type(r.pair)

    def record_spawn_type(self, pair):
        """record_spawn_type(0x4C, pair) (0x80074BB8): only while the record's bit 0 is clear;
        the entity linked at the front, bit 0, +0x1C, message 0 to its script if the record has
        one, placed at the record."""
        from engine_objects import ladder_spawn, place_record
        m = self.m
        if m.u32(pair) & 1:
            return
        r = m.u32(pair + 4)
        e = ladder_spawn(self.eng, pair, r)
        self.eng.link_front(e)
        m.w32(pair, m.u32(pair) | 1)
        m.w32(e + 0x1C, pair)
        ent = self.bind(e)
        if m.u32(r + 0xC):
            self.msg_start(ent, 0)
        place_record(m, e, r)

    def kind0_leave(self, ent, r):
        """0x80074DC4 -> 0x80061518(pair): a ladder left is held, the one held before (another)
        released."""
        from engine_objects import ladder_release, G_LADDER_HELD
        self.stats[f"kind0 leave {r.cls:08X}"] += 1
        m = self.m
        if r.cls == 0x3011B913 and m.u32(G_LADDER_HELD) != r.pair:
            if m.u32(G_LADDER_HELD):
                ladder_release(self.eng)
            m.w32(G_LADDER_HELD, r.pair)

    def ladder_release(self):
        from engine_objects import ladder_release
        ladder_release(self.eng)

    def removed(self, ent, why):
        """The engine removed an entity: the level logic's part of entity_remove."""
        if ent.removed:
            return
        self.remove(ent, why)

    def send(self, msg, sender, sender_obj, actor, target=0, src=""):
        lv.Sim.send(self, msg, sender, sender_obj, actor, target, src)

    def run_one(self):
        """msg_run_one: one queued context (none queued: nothing)."""
        if not self.queue:
            return 0
        c = self.queue.pop(0)
        self.head = (self.head + 1) % 128
        if self.still_subscribed(c):
            self.vm(c)
        else:
            self.stats["queued, unsubscribed"] += 1
        self.m.w32(lv.G_HEAD, self.head)
        self.m.w32(lv.G_TAIL, self.tail)
        return 1

    def run_queue_all(self):
        """msg_run_all (0x800663B8): every queued context, those queued meanwhile included."""
        n = 0
        while self.queue:
            n += self.run_one()
        return n

    def run_queue(self, limit=None):
        n = lv.Sim.run_queue(self, limit)
        # the ring's indices in memory, as the original keeps them
        self.m.w32(lv.G_HEAD, self.head)
        self.m.w32(lv.G_TAIL, self.tail)
        return n

    def builtin(self, n, c, st, base, sid):
        eng = self.eng
        if n == 3:
            v = st.pop()[1] if st else 0
            if c.sub is not None:
                c.sub.flags40 = c.sub.flags40 | (v & 0xFFFFFFFF)
            self.calls[n] += 1
            return
        if n == 5:
            e = st.pop()[1] if st else 0
            self.calls[n] += 1
            if isinstance(e, lv.Ent) and not e.removed:
                self.removed_by_script.add(e.handle)
                eng.remove(e.addr, "remove_entity")
            return
        if n == 6:
            kind = st.pop()[1] if st else 0
            self.calls[n] += 1
            from engine_objects import level_explosion
            level_explosion(eng, c.sub, c.sub_obj, kind)
            return
        if n == 11:
            src = st.pop()[1] if st else 0
            tgt = st.pop()[1] if st else 0
            self.calls[n] += 1
            from engine_objects import set_home
            set_home(eng, tgt, src)
            return
        if n == 13:
            v = st[-1][1] if st else 0
            what = st[-2][1] if len(st) > 1 else 0
            if what == 4:
                eng.note("level_set(4): alarm from level not implemented")
        if n == 19:
            # sound(...): the level's sounds; positional ones are heard by soldiers
            eng.note("level sound not implemented")
        if n in (18, 21):
            eng.note("world animation (drawing only)")
        lv.Sim.builtin(self, n, c, st, base, sid)
