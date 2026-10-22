#include "valor/sim/level.h"

#include <algorithm>
#include <cstring>

#include "valor/gte.h"
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"

namespace valor::sim {

namespace {
constexpr I PLAYER_FLAG = 1;
}

// ---------------------------------------------------------------- the level's data

void Level::load(const Mem& m, const std::string& nm) {
    name = nm;
    mem = &m;
    g_level = U(m.u32(G_LEVEL));
    U table = U(m.u32(g_level + 0x53C));
    I n = m.u32(table);
    *this = Level{};  // a new level: nothing kept from the last
    name = nm;
    mem = &m;
    g_level = U(m.u32(G_LEVEL));
    records.resize(size_t(n));
    for (I i = 0; i < n; i++) {
        Record& r = records[size_t(i)];
        r.idx = int(i);
        r.pair = U(table + 16 + 8 * i);
        U a = r.addr = U(m.u32(r.pair + 4));
        r.cls = U(m.u32(a));
        r.kind = m.u32(a + 0xC);
        for (int k = 0; k < 3; k++) r.pos[k] = m.s16(a + 0x10 + 2 * k), r.ang[k] = m.s16(a + 0x18 + 2 * k);
        for (int k = 0; k < 4; k++) r.shape[k] = m.s16(a + 0x28 + 2 * k);
        if (r.kind == 0) {
            r.script = -1;
            continue;
        }
        static const U offs[6] = {0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C};
        for (int l = 0; l < 6; l++) {
            U p = U(m.u32(a + offs[l]));
            r.has_list[l] = p != 0;
            if (p)
                for (I k = 0; k < m.u32(p); k++) r.lists[l].push_back(U(m.u32(p + 4 + 4 * k)));
        }
        r.script = S32(m.u32(a + 0x54));
        r.flags30 = U(m.u32(a + 0x30));
        r.p34 = m.s16(a + 0x34);
        if (r.kind == 2 || r.kind == 6) {
            r.ent_script = S32(m.u32(a + 0x64));
            r.model = U(m.u32(m.u32(a + 0x60) + 4)), r.has_model = true;
        } else if (r.kind == 3) {
            r.ent_script = S32(m.u32(a + 0x78));
            r.model = U(m.u32(m.u32(a + 0x74) + 4)), r.has_model = true;
        } else if (r.kind == 4) {
            r.ent_script = S32(m.u32(a + 0x64));
            r.pickup = U(m.u32(a + 0x60));
        } else
            r.ent_script = -1;
    }
    for (Record& r : records) by_pair[r.pair] = &r;
    scripts = U(m.u32(G_SCRIPTS));
    nmsg = m.u32(scripts);
    zones = U(m.u32(G_ZONES));
    for (I i = 0; i < m.u32(G_NMODELS); i++) {
        U a = U(m.u32(G_MODELS) + 0x100 * i);
        models[U(m.u32(a))] = m.u32(a + 0xFC);
    }
    U wo = U(m.u32(m.u32(G_WORLD) + 0x388));
    for (I k = 0; k < m.u32(wo + 0xC); k++) nodes.push_back(U(m.u32(wo + 8) + 0x34 * k));
}

std::vector<I> Level::zone_records(I zone) const {
    std::vector<I> out;
    if (zone == 0xFFFF) return out;
    U p = U(mem->u32(zones + 4 * zone));
    I n = 0xFF - mem->u8(p);
    for (I k = 0; k < n; k++) out.push_back(mem->u16(p + 2 + 2 * k));
    return out;
}

// ---------------------------------------------------------------- volumes (record_contains)

bool contains(const Record& rec, const I p[3]) {
    I shape = rec.shape[0], a = rec.shape[1], b = rec.shape[2], c = rec.shape[3];
    I cx = rec.pos[0], cy = rec.pos[1], cz = rec.pos[2];
    if (shape == 0) {
        I d[3] = {S16(p[0] - cx), S16(p[1] - cy), S16(p[2] - cz)};
        return S32(a * a - (d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) > 0;
    }
    if (shape == 1) {
        I lo = -b, hi = -c;
        if (hi < lo) std::swap(lo, hi);
        I dy = p[1] - cy;
        if (dy < lo || hi < dy) return false;
        I dx = p[0] - cx, dz = p[2] - cz;
        if (a < -dx || a < dx || a < -dz || a < dz) return false;
        return S32(a * a - (dx * dx + dz * dz)) > 0;
    }
    if (shape == 2) {
        I hx = iabs(a) >> 1, hy = iabs(b) >> 1, hz = iabs(c) >> 1;
        I dx = iabs(p[0] - cx), dy = iabs(p[1] - cy), dz = iabs(p[2] - cz);
        if (hx < dx || hy < dy || hz < dz) return false;
        I ax = S16(-rec.ang[0]), ay = S16(-rec.ang[1]), az = S16(-rec.ang[2]);
        if (ax == 0 && ay == 0 && az == 0) return true;
        int16_t m[3][3];
        rot_yxz(int32_t(ax), int32_t(ay), int32_t(az), m);
        I v[3] = {S16(cx - p[0]), S16(cy - p[1]), S16(cz - p[2])};
        I r[3];
        for (int i = 0; i < 3; i++) r[i] = iabs((m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2]) >> 12);
        return !(hx < r[0] || hy < r[1] || hz < r[2]);
    }
    return false;
}

bool SVal::truthy() const {
    switch (kind) {
        case INT: return v != 0;
        case ENT: return true;
        case NONE: return false;
        case BYTES: return !bytes.empty();
        case FLOAT: return f != 0;
    }
    return false;
}

// ---------------------------------------------------------------- the logic

LevelLogic::LevelLogic(Engine& eng) : eng_(eng), m_(eng.m) {}

void LevelLogic::adopt() {
    for (U e : m_.entities()) bind(e);
    auto it = eng_.ents.find(U(m_.u32(G_SOLDIER)));
    player = it == eng_.ents.end() ? nullptr : it->second;
    head = m_.u32(G_HEAD), tail = m_.u32(G_TAIL);
    queue.clear();
    I i = head;
    auto ent = [&](I h) -> Ent* {
        if (!h) return nullptr;
        auto f = handles.find(U(h));
        return f == handles.end() ? nullptr : f->second;
    };
    while (i != tail) {
        I w[8];
        for (int k = 0; k < 8; k++) w[k] = m_.u32(RING + 32 * i + 4 * k);
        Ctx c;
        c.pc = w[0], c.sub = ent(w[1]), c.sender = ent(w[2]), c.actor = ent(w[3]);
        c.sub_obj = w[4], c.sender_obj = w[5], c.msg = w[6], c.script = w[7], c.src = "seed";
        queue.push_back(c);
        i = (i + 1) % 128;
    }
    spq_sync();
    ladder_held = U(m_.u32(0x8009A5B8));
}

Ent* LevelLogic::bind(U e) {
    U pair = U(m_.u32(e + 0x1C));
    Record* rec = nullptr;
    if (pair) {
        auto it = eng_.L.by_pair.find(pair);
        if (it != eng_.L.by_pair.end()) rec = it->second;
    }
    if (m_.s32(e + 8) == 1) {
        for (Record& r : eng_.L.records)
            if (r.cls == 0x8877B646 && r.p34 == 0) {
                rec = &r;
                break;
            }
    }
    owned_.push_back(std::make_unique<Ent>());
    Ent* ent = owned_.back().get();
    ent->handle = U(m_.u32(e));
    ent->type = m_.s32(e + 8);
    ent->rec = rec;
    ent->addr = e;
    // lv.Ent.__init__'s assignments through engine_level's properties
    m_.w32(e + 0x40, 0);
    m_.w32(e + 0x394, 0);
    m_.w32(e + 0x398, 0xFFFF);
    m_.w32(e + 0x39C, 0);
    ent->serial = ++serial_;
    eng_.ents[e] = ent;
    handles[ent->handle] = ent;
    return ent;
}

Ent* LevelLogic::spawn(Record& rec) {
    U e = record_spawn_entity(eng_, rec);
    if (!e) return nullptr;
    return bind(e);
}

void LevelLogic::record_spawn(U pair) {
    Record& rec = *eng_.L.by_pair.at(pair);
    if (!(rec.kind == 2 || rec.kind == 3 || rec.kind == 4 || rec.kind == 6)) return;
    stats["spawn requests"]++;
    U e = record_spawn_entity(eng_, rec);
    if (!e) {
        stats["spawn refused (queued)"]++;
        spq_push(pair);
        return;
    }
    eng_.link_front(e);
    m_.w32(pair, m_.u32(pair) | 1);
    m_.w32(e + 0x1C, pair);
    Ent* ent = bind(e);
    ent->rec = &rec;
    msg_start(ent, 0);
    if (rec.kind != 4) place(eng_, e, rec);
}

void LevelLogic::removed(Ent* ent, const std::string& why) {
    if (ent->removed) return;
    remove(ent, why);
}

// -- scripts
I LevelLogic::script_of(Ent* ent, I obj) {
    if (obj) return S32(m_.u32(m_.u32(obj + 4) + 0x54));
    if (!ent || !ent->rec) return -1;
    Record* r = ent->rec;
    return (r->kind == 2 || r->kind == 3 || r->kind == 4 || r->kind == 6) ? r->ent_script : -1;
}

U LevelLogic::script_base(I sid) { return U(eng_.L.scripts + m_.u32(eng_.L.scripts + 8 + 4 * sid)); }

bool LevelLogic::handler(I sid, I k, I& msg, U& pc) {
    U b = script_base(sid);
    if (k >= m_.u32(b)) return false;
    msg = m_.u16(b + 4 + 4 * k);
    pc = U(b + m_.u16(b + 6 + 4 * k));
    return true;
}

void LevelLogic::run_message(Ent* ent, I obj, I msg, const std::string& why) {
    if (!ent && obj == 0) return;
    I sid = script_of(ent, obj);
    if (sid == -1) return;
    U b = script_base(sid);
    for (I k = 0; k < m_.u32(b); k++) {
        if (m_.u16(b + 4 + 4 * k) == msg) {
            Ctx c;
            c.pc = b + m_.u16(b + 6 + 4 * k), c.sub = ent, c.sub_obj = obj, c.msg = msg, c.script = sid, c.src = why;
            vm(c);
            return;
        }
    }
}

// -- subscriptions
std::vector<U> LevelLogic::nodes(I msg) {
    std::vector<U> out;
    U n = U(m_.u32(m_.u32(G_SUBS) + 4 * msg));
    while (n) {
        out.push_back(n);
        n = U(m_.u32(n + 0x20));
    }
    return out;
}

Ent* LevelLogic::node_ent(U n, U off) {
    U h = U(m_.u32(n + off));
    if (!h) return nullptr;
    auto it = handles.find(h);
    return it == handles.end() ? nullptr : it->second;
}

bool LevelLogic::same(U n, U off, Ent* ent) {
    if (!ent) return false;
    return U(m_.u32(n + off)) == ent->handle;
}

void LevelLogic::unregister(I msg, Ent* ent, I obj, I filt, Ent* fent, I fobj, I val) {
    U head_a = U(m_.u32(G_SUBS) + 4 * msg);
    U prev = 0;
    U n = U(m_.u32(head_a));
    while (n) {
        U nxt = U(m_.u32(n + 0x20));
        bool hit = false;
        if ((ent && same(n, 0, ent)) || (obj && obj == m_.u32(n + 4))) {
            if (filt == 0)
                hit = true;
            else if (m_.u32(n + 8) == filt) {
                bool fmatch = (fent && same(n, 0xC, fent)) || (fobj && m_.u32(n + 0x10) == fobj);
                if (filt == 1 || filt == 2)
                    hit = S32(m_.u32(n + 0x14)) == val;
                else if (filt == 3 || filt == 4)
                    hit = fmatch;
                else if (filt == 5)
                    hit = S32(m_.u32(n + 0x14)) == val && fmatch;
            }
        }
        if (hit) {
            if (prev)
                m_.w32(prev + 0x20, nxt);
            else
                m_.w32(head_a, nxt);
            m_.w32(n + 0x20, m_.u32(G_FREE));
            m_.w32(G_FREE, n);
        } else
            prev = n;
        n = nxt;
    }
}

void LevelLogic::reg(I msg, Ent* ent, I obj, I filt, Ent* fent, I fobj, I val, I pc, I sid) {
    auto h = [](Ent* e) -> I { return e ? e->handle : 0; };
    if (filt == 0)
        unregister(msg, ent, obj, 0, nullptr, 0, 0);
    else {
        for (U n : nodes(msg)) {
            if (!((ent && same(n, 0, ent)) || (obj && obj == m_.u32(n + 4)))) continue;
            I nf = m_.u32(n + 8);
            if (nf == 0) {
                m_.w32(n + 8, filt);
                m_.w32(n + 0xC, h(fent));
                m_.w32(n + 0x10, fobj);
                m_.w32(n + 0x14, val);
                m_.w32(n + 0x18, pc);
                m_.w32(n + 0x1C, sid);
                return;
            }
            if (nf != filt) continue;
            bool fmatch = (fent && same(n, 0xC, fent)) || (fobj && m_.u32(n + 0x10) == fobj);
            if ((filt == 1 || filt == 2) && S32(m_.u32(n + 0x14)) == val) return;
            if ((filt == 3 || filt == 4) && fmatch) return;
            if (filt == 5 && S32(m_.u32(n + 0x14)) == val && fmatch) return;
        }
    }
    U n = U(m_.u32(G_FREE));
    if (!n) {
        stats["pool empty"]++;
        return;
    }
    m_.w32(G_FREE, m_.u32(n + 0x20));
    U head_a = U(m_.u32(G_SUBS) + 4 * msg);
    m_.w32(n + 0, h(ent));
    m_.w32(n + 4, obj);
    m_.w32(n + 8, filt);
    m_.w32(n + 0xC, h(fent));
    m_.w32(n + 0x10, fobj);
    m_.w32(n + 0x14, val);
    m_.w32(n + 0x18, pc);
    m_.w32(n + 0x1C, sid);
    m_.w32(n + 0x20, m_.u32(head_a));
    m_.w32(head_a, n);
}

void LevelLogic::drop_subscriptions(Ent* ent) {
    for (I msg = 0; msg < eng_.L.nmsg; msg++) {
        U head_a = U(m_.u32(G_SUBS) + 4 * msg);
        U prev = 0, n = U(m_.u32(head_a));
        while (n) {
            U nxt = U(m_.u32(n + 0x20));
            if (same(n, 0, ent) || same(n, 0xC, ent)) {
                if (prev)
                    m_.w32(prev + 0x20, nxt);
                else
                    m_.w32(head_a, nxt);
                m_.w32(n + 0x20, m_.u32(G_FREE));
                m_.w32(G_FREE, n);
            } else
                prev = n;
            n = nxt;
        }
    }
}

// -- records and tags
I LevelLogic::rec_of(Ent* ent, I obj) {
    if (ent) return ent->rec ? ent->rec->pair : 0;
    return obj;
}

I LevelLogic::has_tag(Ent* ent, I obj, I val) {
    I pair = rec_of(ent, obj);
    if (!pair) return 0;
    Record* r = eng_.L.by_pair.at(U(pair));
    for (U t : r->lists[L_TAGS])
        if ((t & 0xFF) == (val & 0xFF)) return 1;
    return 0;
}

I LevelLogic::tags_match(Ent* e1, I o1, Ent* e2, I o2, I typ) {
    I p1 = rec_of(e1, o1), p2 = rec_of(e2, o2);
    if (!p1 || !p2) {
        stats["tags_match without a record"]++;
        return 0;
    }
    for (U a : eng_.L.by_pair.at(U(p1))->lists[L_TAGS]) {
        if (I(a >> 16) != typ) continue;
        for (U b : eng_.L.by_pair.at(U(p2))->lists[L_TAGS])
            if (I(b >> 16) == typ && a == b) return 1;
    }
    return 0;
}

// -- sending
void LevelLogic::send(I msg, Ent* sender, I sender_obj, Ent* actor, I target, const std::string& src) {
    if (msg >= eng_.L.nmsg) {
        stats["invalid message"]++;
        return;
    }
    for (U n : nodes(msg)) {
        I filt = m_.u32(n + 8);
        bool ok;
        if (filt == 0)
            ok = true;
        else if (filt == 1 || filt == 4) {
            I pair = sender_obj ? sender_obj : (sender && sender->rec ? sender->rec->pair : 0);
            ok = pair && eng_.L.by_pair.at(U(pair))->p34 == S32(m_.u32(n + 0x14));
        } else if (filt == 2)
            ok = has_tag(sender, sender_obj, m_.u32(n + 0x14)) != 0;
        else if (filt == 3)
            ok = (sender && same(n, 0xC, sender)) || (sender_obj && m_.u32(n + 0x10) == sender_obj);
        else if (filt == 5)
            ok = tags_match(sender, sender_obj, node_ent(n, 0xC), m_.u32(n + 0x10), m_.u32(n + 0x14)) != 0;
        else
            ok = false;
        if (!ok) continue;
        if (target && m_.u32(n + 4) != target) continue;
        enqueue(n, sender, sender_obj, actor, msg, src);
    }
}

void LevelLogic::enqueue(U n, Ent* sender, I sender_obj, Ent* actor, I msg, const std::string& src) {
    Ent* sub = node_ent(n, 0);
    if (sub && sub->removed) return;
    if (sender && sender->removed) sender = nullptr;
    if (actor && actor->removed) actor = nullptr;
    Ctx c;
    c.pc = m_.u32(n + 0x18), c.sub = sub, c.sender = sender, c.actor = actor, c.sub_obj = m_.u32(n + 4);
    c.sender_obj = sender_obj, c.msg = msg, c.script = m_.u32(n + 0x1C), c.src = src;
    if ((tail + 1) % 128 == head) {
        stats["queue full"]++;
        return;
    }
    queue.push_back(c);
    tail = (tail + 1) % 128;
}

bool LevelLogic::still_subscribed(const Ctx& c) {
    Ent* sub = (c.sub && !c.sub->removed) ? c.sub : nullptr;
    if (c.msg == 0) return true;
    if (!sub && !c.sub_obj) return false;
    for (U n : nodes(c.msg)) {
        if (sub) {
            if (same(n, 0, sub)) return true;
        } else if (c.sub_obj == m_.u32(n + 4))
            return true;
    }
    return false;
}

I LevelLogic::run_queue(I limit) {
    I n = 0;
    for (;;) {
        while (!queue.empty() && (limit < 0 || limit > 0)) {
            n++;
            if (limit >= 0) limit--;
            Ctx c = queue.front();
            queue.pop_front();
            head = (head + 1) % 128;
            if (still_subscribed(c))
                vm(c);
            else
                stats["queued, unsubscribed"]++;
        }
        if (limit >= 0 && limit <= 0 && !queue.empty()) break;
        if (!spawn_retry() && queue.empty()) break;
    }
    // the ring's indices in memory, as the original keeps them
    m_.w32(G_HEAD, head);
    m_.w32(G_TAIL, tail);
    return n;
}

I LevelLogic::run_one() {
    if (queue.empty()) return 0;
    Ctx c = queue.front();
    queue.pop_front();
    head = (head + 1) % 128;
    if (still_subscribed(c))
        vm(c);
    else
        stats["queued, unsubscribed"]++;
    m_.w32(G_HEAD, head);
    m_.w32(G_TAIL, tail);
    return 1;
}

I LevelLogic::run_queue_all() {
    I n = 0;
    while (!queue.empty()) n += run_one();
    return n;
}

// -- spawning
void LevelLogic::activate(U pair) {
    I st = m_.u32(pair);
    if (st & 1) return;
    m_.w32(pair, st | 1);
    record_spawn(pair);
}

// The spawn queue, in RAM as the original keeps it (levels.md, Spawning).
void LevelLogic::spq_sync() {
    I n = m_.u32(SPQ_COUNT), h = m_.u32(SPQ_HEAD);
    spawn_q.clear();
    for (I k = 0; k < n; k++) spawn_q.push_back(U(m_.u32(SPQ + 4 * ((h + k) % 64))));
    spq_head = h;
}

// spawn_queue_push (0x80074F48): a full ring (64) drops its head; the pair at the tail.
void LevelLogic::spq_push(U pair) {
    if (m_.u32(SPQ_COUNT) >= 64) {
        I h = m_.u32(SPQ_HEAD);
        m_.w32(SPQ + 4 * h, 0);
        m_.w32(SPQ_HEAD, h + 1);
        m_.w32(SPQ_COUNT, m_.u32(SPQ_COUNT) - 1);
    }
    I t = m_.u32(SPQ_TAIL);
    m_.w32(SPQ_COUNT, m_.u32(SPQ_COUNT) + 1);
    m_.w32(SPQ + 4 * t, pair);
    m_.w32(SPQ_TAIL, t + 1 < 64 ? t + 1 : t - 0x3F);
    spq_sync();
}

// spawn_queue_run_one (0x80074F9C): the head popped (an emptied ring restarts at 0) and spawned,
// which pushes it back at the tail if refused. True if the ring shrank.
bool LevelLogic::spawn_retry_one() {
    I n = m_.u32(SPQ_COUNT);
    if (!n) return false;
    I h = m_.u32(SPQ_HEAD);
    U pair = U(m_.u32(SPQ + 4 * h));
    m_.w32(SPQ + 4 * h, 0);
    m_.w32(SPQ_HEAD, h + 1 < 64 ? h + 1 : h - 0x3F);
    m_.w32(SPQ_COUNT, n - 1);
    if (n - 1 == 0) {
        m_.w32(SPQ_HEAD, 0);
        m_.w32(SPQ_TAIL, 0);
    }
    spq_sync();
    record_spawn(pair);
    return m_.u32(SPQ_COUNT) < n;
}

bool LevelLogic::spawn_retry() {
    bool any_ok = false;
    while (m_.u32(SPQ_COUNT)) {
        if (!spawn_retry_one()) break;
        any_ok = true;
    }
    return any_ok;
}

void LevelLogic::remove(Ent* ent, const std::string& why) {
    if (ent->removed) return;
    ent->removed = true;
    stats["removed (" + why + ")"]++;
    if (ent->rec) {
        Record& r = *ent->rec;
        I st = m_.u32(r.pair);
        I flags = m_.u32(ent->addr + 0xC);
        if ((r.kind == 2 || r.kind == 3 || r.kind == 6) && (flags & DEAD) && !(r.flags30 & 8)) st |= 2;
        st &= ~I(1);
        if (ent->type == SOLDIER && (flags & DEAD)) st |= 2;
        m_.w32(r.pair, st);
        if (r.kind != 0) run_message(ent, 0, 0x1A, why);
    }
    drop_subscriptions(ent);
}

// -- triggers
void LevelLogic::zones_update(Ent* ent, I zone, const I pos[3]) {
    Level& L = eng_.L;
    U a = ent->addr;
    bool is_player = m_.u32(a + 0xC) & PLAYER_FLAG;
    I bits = 0;
    std::vector<I> idx = L.zone_records(zone);
    for (size_t k = 0; k < idx.size(); k++) {
        Record& r = L.records[size_t(idx[k])];
        if (is_player) {
            if (r.kind != 0 && !r.has_list[L_ENTER] && !r.has_list[L_LEAVE] && !r.has_list[L_USE]) continue;
        } else {
            if (r.kind == 0 || (!r.has_list[L_C_ENTER] && !r.has_list[L_C_LEAVE])) continue;
        }
        if (contains(r, pos)) bits |= I(1) << k;
    }
    m_.w32(a + 0x390, zone);
    m_.w32(a + 0x394, bits);
    I pz = m_.u32(a + 0x398), pb = m_.u32(a + 0x39C);
    if (zone == 0xFFFF) {
        if (pz != zone) {
            std::vector<I> old = L.zone_records(pz);
            for (size_t k = 0; k < old.size(); k++)
                if (pb >> k & 1) record_leave(ent, L.records[size_t(old[k])]);
        }
    } else if (pz == 0xFFFF) {
        for (size_t k = 0; k < idx.size(); k++)
            if (bits >> k & 1) record_enter(ent, L.records[size_t(idx[k])]);
    } else if (zone == pz) {
        I ch = bits ^ pb;
        for (size_t k = 0; k < idx.size(); k++)
            if ((pb & ch) >> k & 1) record_leave(ent, L.records[size_t(idx[k])]);
        for (size_t k = 0; k < idx.size(); k++)
            if ((bits & ch) >> k & 1) record_enter(ent, L.records[size_t(idx[k])]);
    } else
        merge(ent, idx, L.zone_records(pz), bits, pb);
    m_.w32(a + 0x398, zone);
    m_.w32(a + 0x39C, bits);
}

void LevelLogic::merge(Ent* ent, const std::vector<I>& a, const std::vector<I>& b, I bits, I pb) {
    Level& L = eng_.L;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        I s1 = a[i], s2 = b[j];
        if (s1 == s2) {
            I was = pb >> j & 1, now = bits >> i & 1;
            if (now != was) {
                if (was)
                    record_leave(ent, L.records[size_t(s1)]);
                else
                    record_enter(ent, L.records[size_t(s1)]);
            }
        } else if (s1 < s2 && (bits >> i & 1))
            stats["zone change: enter skipped"]++;
        else if (s2 < s1 && (pb >> j & 1))
            stats["zone change: leave skipped"]++;
        if (!(s2 < s1)) i++;
        if (s1 >= s2) j++;
    }
    if (i >= a.size()) {
        while (j < b.size()) {
            if (pb >> j & 1) record_leave(ent, L.records[size_t(b[j])]);
            j++;
        }
    }
    while (i < a.size()) {
        if (bits >> i & 1) record_enter(ent, L.records[size_t(a[i])]);
        i++;
    }
}

void LevelLogic::record_enter(Ent* ent, Record& r) {
    I flags = m_.u32(ent->addr + 0xC);
    const std::vector<U>* lst;
    if (flags & PLAYER_FLAG) {
        if (r.kind == 0) {
            kind0_enter(ent, r);
            return;
        }
        lst = &r.lists[L_ENTER];
    } else if (flags & 0x50)
        lst = &r.lists[L_C_ENTER];
    else
        return;
    stats["enter"]++;
    std::vector<U> copy = *lst;
    for (U msg : copy) send(msg, nullptr, r.pair, ent, 0, "enter");
}

void LevelLogic::record_leave(Ent* ent, Record& r) {
    if (r.kind == 0) {
        kind0_leave(ent, r);
        return;
    }
    I flags = m_.u32(ent->addr + 0xC);
    const std::vector<U>* lst;
    if (flags & PLAYER_FLAG)
        lst = &r.lists[L_LEAVE];
    else if (flags & 0x50)
        lst = &r.lists[L_C_LEAVE];
    else
        return;
    stats["leave"]++;
    std::vector<U> copy = *lst;
    for (U msg : copy) send(msg, nullptr, r.pair, ent, 0, "leave");
}

void LevelLogic::kind0_enter(Ent*, Record& r) {
    if (r.cls == 0x3011B913) {
        record_spawn_type(r.pair);
        return;
    }
    if (r.cls == 0xD1BA7D1E) m_.w32(LEVEL_FLAGS, m_.u32(LEVEL_FLAGS) | 0x24);
    // chunk_stream_trigger (0x80054378): the streaming record entered (a state built from the
    // disc; the collision of recorded runs follows the CD's timing all the same)
    if (r.cls == 0x4D078F23 && eng_.gheap) {
        m_.w32(0x80099070, r.addr);
        eng_.chunk_stream();
    }
}

// record_spawn_type(0x4C, pair) (0x80074BB8): only while the record's bit 0 is clear; the entity
// linked at the front, bit 0, +0x1C, message 0 to its script if the record has one, placed.
void LevelLogic::record_spawn_type(U pair) {
    if (m_.u32(pair) & 1) return;
    U r = U(m_.u32(pair + 4));
    U e = ladder_spawn(eng_, pair, r);
    eng_.link_front(e);
    m_.w32(pair, m_.u32(pair) | 1);
    m_.w32(e + 0x1C, pair);
    Ent* ent = bind(e);
    if (m_.u32(r + 0xC)) msg_start(ent, 0);
    place_record(m_, e, r);
}

void LevelLogic::kind0_leave(Ent*, Record& r) {
    if (r.cls == 0x3011B913 && m_.u32(G_LADDER_HELD) != r.pair) {
        if (m_.u32(G_LADDER_HELD)) ladder_release(eng_);
        m_.w32(G_LADDER_HELD, r.pair);
    }
}

void LevelLogic::use(Ent* ent) {
    I zone = m_.u32(ent->addr + 0x390), bits = m_.u32(ent->addr + 0x394);
    std::vector<I> idx = eng_.L.zone_records(zone);
    for (size_t k = 0; k < idx.size(); k++) {
        if (bits >> k & 1) {
            Record& r = eng_.L.records[size_t(idx[k])];
            if (r.kind != 0) {
                std::vector<U> copy = r.lists[L_USE];
                for (U msg : copy) send(msg, nullptr, r.pair, ent, 0, "use");
            }
        }
    }
}

// -- objectives
void LevelLogic::objective_add(I oid, I text) {
    I n = S32(m_.u32(OBJ_COUNT));
    I tp = m_.u32(MSG_TEXTS + 4 * text);
    for (I i = 0; i < imax(n, 0); i++) {
        if (S32(m_.u32(OBJ_ID + 4 * i)) == oid) {
            m_.w32(OBJ_DONE + 4 * i, 0);
            m_.w32(OBJ_TEXT + 4 * i, tp);
            return;
        }
    }
    m_.w32(OBJ_DONE + 4 * n, 0);
    m_.w32(OBJ_TEXT + 4 * n, tp);
    m_.w32(OBJ_ID + 4 * n, oid);
    m_.w32(OBJ_COUNT, n + 1);
}

void LevelLogic::objective_remove(I oid) {
    I n = S32(m_.u32(OBJ_COUNT));
    I i = 0;
    while (i < n && S32(m_.u32(OBJ_ID + 4 * i)) != oid) i++;
    n--;
    m_.w32(OBJ_COUNT, n);
    for (U base : {OBJ_DONE, OBJ_TEXT, OBJ_ID}) m_.w32(base + 4 * i, m_.u32(base + 4 * n));
}

I LevelLogic::objective_find(I oid) {
    for (I i = 0; i < imax(S32(m_.u32(OBJ_COUNT)), 0); i++)
        if (S32(m_.u32(OBJ_ID + 4 * i)) == oid) return i;
    return -1;
}

// -- the VM
void LevelLogic::vm(Ctx c) {
    I sid = script_of(c.sub_obj == 0 ? c.sub : nullptr, c.sub_obj);
    if (sid == -1) return;
    U base = script_base(sid);
    I nh = m_.u32(base);
    U vars_at = U(base + nh * 4 + 4);
    I pos = c.pc - base;
    std::vector<SVal> st;
    // a variable's place: a record's byte, an entity's (idx < 8), or nothing
    struct VA {
        int kind;  // 0 none, 1 rec, 2 ent
        U addr;
        Ent* e;
        I idx;
    };
    auto var_addr = [&](I scope, I idx) -> VA {
        if (scope == 0xF0 || scope == 0xF1) {
            if (c.sub_obj) return {1, U(m_.u32(c.sub_obj + 4) + 0x20 + idx), nullptr, 0};
            if (c.sub) return {2, 0, c.sub, idx};
            return {0, 0, nullptr, 0};
        }
        return {1, U(vars_at + idx), nullptr, 0};
    };
    auto var_get = [&](const VA& a) -> I {
        if (a.kind == 0) return 0;
        if (a.kind == 1) return m_.u8(a.addr);
        return a.idx < 8 ? m_.u8(a.e->addr + 0x20 + a.idx) : 0;
    };
    auto var_set = [&](const VA& a, I v) {
        if (a.kind == 0) {
            stats["store to entity 0"]++;
            return;
        }
        if (a.kind == 1)
            m_.w8(a.addr, v);
        else if (a.idx < 8)
            m_.w8(a.e->addr + 0x20 + a.idx, v & 0xFF);
    };
    auto align = [](I p, I n) { return p + pmod(-p, n); };
    auto ent_of = [&](I sel) -> Ent* { return sel == 0 ? c.sub : sel == 1 ? c.sender : c.actor; };
    auto pop_back = [&]() -> SVal {
        SVal v = st.back();
        st.pop_back();
        return v;
    };
    int steps = 0;
    for (;;) {
        if (++steps > 100000) {
            stats["runaway script"]++;
            return;
        }
        I op = m_.u8(base + pos);
        pos++;
        if (op == 0x00) continue;
        if (op == 0x01) {
            if (!st.empty()) st.pop_back();
            continue;
        }
        if (op == 0x02) {
            I s = m_.u8(base + pos);
            pos++;
            if (0xF4 <= s && s <= 0xFF) {
                I t = m_.u8(base + pos);
                pos++;
                I pr;
                if ((0xF7 <= t && t <= 0xF9) || (0xFD <= t && t <= 0xFF)) {
                    stats["wide push"]++;
                    pos = align(pos, 2) + 2;
                    pr = 0;
                } else {
                    pr = m_.u8(base + pos);
                    pos++;
                }
                Ent* e = nullptr;
                I o = 0;
                if (0xFA <= s && s <= 0xFC) {
                    e = ent_of(s - 0xFA);
                    I objs[3] = {c.sub_obj, c.sender_obj, 0};
                    o = objs[s - 0xFA];
                } else if (0xF4 <= s && s <= 0xF6) {
                    e = ent_of(s - 0xF4);
                    I objs[3] = {c.sub_obj, c.sender_obj, 0};
                    o = objs[s - 0xF4];
                }
                if (t == 0x10) {
                    SVal v;
                    v.t = 0x10;
                    if (s >= 0xFA) {
                        if (e)
                            v.kind = SVal::ENT, v.e = e;
                        else
                            v.kind = SVal::NONE;
                    } else
                        v.v = o;
                    st.push_back(v);
                } else {
                    SVal v;
                    if (prop(e, o, pr, v))
                        st.push_back(v);
                    else
                        st.push_back(SVal::num(0, 0));
                }
                continue;
            }
            if (0xF0 <= s && s <= 0xF3) {
                I sel = m_.u8(base + pos);
                pos++;
                I idx;
                if (s == 0xF1 || s == 0xF3) {
                    pos = align(pos, 2);
                    idx = m_.u16(base + pos);
                    pos += 2;
                } else {
                    idx = m_.u8(base + pos);
                    pos++;
                }
                I v = var_get(var_addr(s, idx));
                if (sel == 9)
                    st.push_back(SVal::num(0xB, v));
                else if (1 <= sel && sel <= 8)
                    st.push_back(SVal::num(0xB, v >> (sel - 1) & 1));
                else
                    st.push_back(SVal::num(0, 0));
                continue;
            }
            if (s == 9) {
                st.push_back(SVal::num(0xB, m_.u8(base + pos)));
                pos++;
            } else if (0xA <= s && s <= 0xD) {
                pos = align(pos, 4);
                st.push_back(SVal::num(s, s == 0xA ? S32(m_.u32(base + pos)) : m_.u32(base + pos)));
                pos += 4;
            } else if (s == 0xE) {
                pos = align(pos, 4);
                SVal v;
                v.kind = SVal::FLOAT, v.t = 0xE;
                uint32_t w = uint32_t(m_.u32(base + pos));
                float f;
                std::memcpy(&f, &w, 4);
                v.f = f;
                st.push_back(v);
                pos += 4;
            } else if (s == 0x10) {
                pos = align(pos, 4);
                st.push_back(SVal::num(0x10, base + pos));
                pos += 4;
            } else if (s == 0xF) {
                I n = m_.u8(base + pos);
                SVal v;
                v.kind = SVal::BYTES, v.t = 0xF;
                v.bytes.assign(reinterpret_cast<const char*>(m_.ptr(base + pos + 1)), size_t(n));
                st.push_back(v);
                pos += 1 + n;
            } else {
                stats["bad push"]++;
                return;
            }
            continue;
        }
        if (op == 0x03) {
            I s = m_.u8(base + pos);
            pos++;
            if (0xF0 <= s && s <= 0xF3) {
                I sel = m_.u8(base + pos);
                pos++;
                I idx;
                if (s == 0xF1 || s == 0xF3) {
                    pos = align(pos, 2);
                    idx = m_.u16(base + pos);
                    pos += 2;
                } else {
                    idx = m_.u8(base + pos);
                    pos++;
                }
                VA a = var_addr(s, idx);
                SVal top = st.empty() ? SVal::num(0, 0) : st.back();
                I tv = top.kind == SVal::FLOAT ? I(top.f) : top.kind == SVal::INT ? top.v : top.truthy();
                if (sel == 9) {
                    if (top.t == 0xA || top.t == 0xB || top.t == 0xE) var_set(a, tv);
                } else if (1 <= sel && sel <= 8) {
                    I old = var_get(a);
                    I bit = I(1) << (sel - 1);
                    var_set(a, top.truthy() ? (old | bit) : (old & ~bit));
                }
            }
            if (!st.empty()) st.pop_back();
            continue;
        }
        if (0x04 <= op && op <= 0x0F && op != 0x07) {
            SVal t = st.back();
            SVal n = st[st.size() - 2];
            st.resize(st.size() - 2);
            st.push_back(binop(op, t, n));
            continue;
        }
        if (op == 0x07) {
            SVal t = pop_back();
            st.push_back(SVal::num(0xB, !t.truthy()));
            continue;
        }
        if (op == 0x10) {
            pos = align(pos, 2);
            I tgt = m_.u16(base + pos);
            pos += 2;
            SVal t = pop_back();
            if (!t.truthy()) pos = tgt;
            continue;
        }
        if (op == 0x11) {
            pos = m_.u16(base + align(pos, 2));
            continue;
        }
        if (op == 0x12 || op == 0x13) {
            I n;
            if (op == 0x12) {
                n = m_.u8(base + pos);
                pos++;
            } else {
                pos = align(pos, 2);
                n = m_.u16(base + pos);
                pos += 2;
            }
            builtin(n, c, st, base, sid);
            continue;
        }
        if (op == 0x14) return;
        if (op == 0x15) {
            st.push_back(SVal::num(0xB, m_.u8(vars_at) == 0));
            continue;
        }
        if (op == 0x16) {
            m_.w8(vars_at, 1);
            continue;
        }
        if (op == 0x17) {
            I t = m_.u8(base + pos);
            pos++;
            if (t == 9)
                pos++;
            else if (t == 0xA || t == 0xB)
                pos = align(pos, 4) + 4;
            continue;
        }
        stats["invalid opcode"]++;
        return;
    }
}

SVal LevelLogic::binop(I op, const SVal& ts, const SVal& ns) {
    I t, n;
    I tt = ts.t, tn = ns.t;
    auto val = [](const SVal& s) -> I {
        switch (s.kind) {
            case SVal::INT: return s.v;
            case SVal::ENT: return s.e->serial;
            case SVal::NONE: return 0;
            case SVal::FLOAT: return I(s.f);
            case SVal::BYTES: return 0;
        }
        return 0;
    };
    bool te = ts.kind == SVal::ENT, ne = ns.kind == SVal::ENT;
    if (te || ne || ts.kind == SVal::NONE || ns.kind == SVal::NONE) {
        I a = val(ts), b = val(ns);
        if (op == 4) {
            if (te || ne) return SVal::num(0xB, te && ne && ts.e == ns.e);
            return SVal::num(0xB, a == b);
        }
        t = a, n = b;
    } else
        t = val(ts), n = val(ns);
    if (tt == 0xE || tn == 0xE) stats["float op"]++;
    bool uns = tt == 0xB && tn == 0xB;
    I T = t, N = n;
    if (op == 4) return SVal::num(0xB, T == N);
    if (op == 5) return SVal::num(0xB, N != 0 && (T & 0xFFFFFFFF) > 0);
    if (op == 6) return SVal::num(0xB, N != 0 || T != 0);
    if (op == 8 || op == 9 || op == 0xA || op == 0xB) {
        if (uns)
            T &= 0xFFFFFFFF, N &= 0xFFFFFFFF;
        else
            T = S32(T), N = S32(N);
        bool r = op == 8 ? T < N : op == 9 ? T > N : op == 0xA ? T <= N : T >= N;
        return SVal::num(0xB, r);
    }
    I rt = uns ? 0xB : 0xA;
    I r;
    if (op == 0xC)
        r = T + N;
    else if (op == 0xD)
        r = T - N;
    else if (op == 0xE)
        r = T * N;
    else {
        if (N == 0) {
            stats["divide by 0"]++;
            r = 0;
        } else if (uns)
            r = (T & 0xFFFFFFFF) / (N & 0xFFFFFFFF);
        else
            r = cdiv(S32(T), S32(N));
    }
    r &= 0xFFFFFFFF;
    return SVal::num(rt, rt == 0xA ? S32(r) : r);
}

bool LevelLogic::prop(Ent* e, I o, I n, SVal& out) {
    I pair = o ? o : (e && e->rec ? e->rec->pair : 0);
    if (!pair) return false;
    Record& r = *eng_.L.by_pair.at(U(pair));
    U a = r.addr;
    if (r.kind == 0) return false;
    if (n <= 3) return out = SVal::num(0xA, m_.s16(a + 0x28 + 2 * n)), true;
    if (n == 4 || n == 5) return out = SVal::num(0xA, m_.s16(a + 0x32 + 2 * (n - 4))), true;
    if (n == 13) return out = SVal::num(0xB, m_.u32(a + 0x54)), true;
    if (14 <= n && n <= 17) return out = SVal::num(0xB, m_.u32(a + 0x30) >> (n - 14) & 1), true;
    if (n == 18) return out = SVal::num(0xA, m_.s16(a + 0x36)), true;
    if (n == 19) {
        if (r.kind == 5) return out = SVal::num(0xB, m_.u32(a + 0x58)), true;
        if (r.kind == 2 || r.kind == 3 || r.kind == 4 || r.kind == 6) return out = SVal::num(0xB, m_.u32(a + 0x58) & 1), true;
    }
    if (n == 29 && r.kind == 6) return out = SVal::num(0xB, m_.u32(a + 0x6C)), true;
    stats["property " + std::to_string(n) + " kind " + std::to_string(r.kind)]++;
    return false;
}

void LevelLogic::builtin(I n, Ctx& c, std::vector<SVal>& st, U base, I sid) {
    // engine_level's built-ins, then level_vm's
    auto popv = [&]() -> SVal {
        if (st.empty()) return SVal::num(0, 0);
        SVal v = st.back();
        st.pop_back();
        return v;
    };
    auto as_int = [](const SVal& v) -> I { return v.kind == SVal::INT ? v.v : v.kind == SVal::FLOAT ? I(v.f) : 0; };
    if (n == 3) {
        I v = as_int(popv());
        if (c.sub) m_.w32(c.sub->addr + 0x40, m_.u32(c.sub->addr + 0x40) | (v & 0xFFFFFFFF));
        calls[n]++;
        return;
    }
    if (n == 5) {
        SVal e = popv();
        calls[n]++;
        if (e.kind == SVal::ENT && !e.e->removed) {
            removed_by_script.insert(e.e->handle);
            eng_.remove(e.e->addr, "remove_entity");
        }
        return;
    }
    if (n == 6) {
        I kind = as_int(popv());
        calls[n]++;
        level_explosion(eng_, c.sub, c.sub_obj, kind);
        return;
    }
    if (n == 11) {
        SVal src = popv();
        SVal tgt = popv();
        calls[n]++;
        set_home(eng_, tgt, src);
        return;
    }
    if (n == 8) {
        // bi_show_text (0x80069FC4) -> show_text (0x80053464)
        I v = as_int(popv());
        calls[n]++;
        show_text(eng_, S32(v));
        return;
    }
    if (n == 19) {
        // bi_sound (0x80069B88) -> lvl_sound (0x800533D4): pops mode, bank + 1, sound, pitch,
        // pitch spread, volume, volume spread; sound_play on the script's entity (bank 0 for 0),
        // looping for mode 3, whose voice is pushed (audio.md, Scripts)
        I v7[7];
        for (I& x : v7) x = S32(as_int(popv()));
        I mode = v7[0], bank1 = v7[1];
        U ent = c.sub && !c.sub->removed ? c.sub->addr : 0;
        I v = eng_.audio->play(bank1 ? bank1 - 1 : 0, v7[2], ent, v7[5], v7[6], v7[3], v7[4], 0, mode == 3 ? 1 : 0, 0, 0);
        calls[n]++;
        if (mode == 3) st.push_back(SVal::num(0xB, v));
        return;
    }
    if (n == 18 || n == 21) eng_.note("world animation (drawing only)");
    builtin_base(n, c, st, base, sid);
}

void LevelLogic::builtin_base(I n, Ctx& c, std::vector<SVal>& st, U, I sid) {
    calls[n]++;
    auto popv = [&]() -> SVal {
        if (st.empty()) return SVal::num(0, 0);
        SVal v = st.back();
        st.pop_back();
        return v;
    };
    auto as_int = [](const SVal& v) -> I { return v.kind == SVal::INT ? v.v : v.kind == SVal::FLOAT ? I(v.f) : 0; };
    if (n == 0 || n == 1) {
        I h = as_int(popv());
        I filt = as_int(popv());
        SVal fent_v, fobj_v;
        bool have_fent = false, have_fobj = false;
        I val = -1;
        if (filt == 1 || filt == 2)
            val = as_int(popv());
        else if (filt == 3 || filt == 4) {
            fobj_v = popv(), have_fobj = true;
            fent_v = popv(), have_fent = true;
        } else if (filt == 5) {
            fobj_v = popv(), have_fobj = true;
            fent_v = popv(), have_fent = true;
            val = as_int(popv());
        } else if (filt != 0) {
            stats["unhandled filter"]++;
            return;
        }
        I hmsg;
        U hpc;
        if (!handler(sid, h, hmsg, hpc)) return;
        I fobj = 0;
        if (have_fobj) {
            if (fobj_v.kind == SVal::ENT)
                fobj = fobj_v.e->rec ? fobj_v.e->rec->pair : 0;
            else
                fobj = as_int(fobj_v);
        }
        Ent* fent = have_fent && fent_v.kind == SVal::ENT ? fent_v.e : nullptr;
        Ent* sub = (c.sub && !c.sub->removed) ? c.sub : nullptr;
        I v = val != -1 ? S32(val) : -1;
        if (n == 0)
            reg(hmsg, sub, c.sub_obj, filt, fent, fobj, v, hpc, sid);
        else
            unregister(hmsg, sub, c.sub_obj, filt, fent, fobj, v);
    } else if (n == 2) {
        I msg = as_int(popv());
        send(msg, c.sub, c.sub_obj, nullptr, 0, "send");
    } else if (n == 3) {
        I v = as_int(popv());
        if (c.sub) m_.w32(c.sub->addr + 0x40, m_.u32(c.sub->addr + 0x40) | v);
    } else if (n == 4) {
        I obj = as_int(popv());
        popv();
        if (obj) activate(U(obj));
    } else if (n == 5) {
        SVal e = popv();
        if (e.kind == SVal::ENT) {
            remove(e.e, "remove_entity");
            removed_by_script.insert(e.e->handle);
        }
    } else if (n == 6) {
        popv();
    } else if (n == 7) {
        I k = as_int(popv());
        popv();
        SVal e = popv();
        I flags = e.kind == SVal::ENT ? m_.u32(e.e->addr + 0xC) : 0;
        if (e.kind != SVal::ENT)
            st.push_back(SVal::num(0xB, 0));
        else if (k == 0)
            st.push_back(SVal::num(0xB, flags & 1));
        else
            st.push_back(SVal::num(0xB, flags & DEAD));
    } else if (n == 8) {
        popv();
    } else if (n == 9) {
        I v = as_int(popv());
        SVal o = popv();
        SVal e = popv();
        st.push_back(SVal::num(0xB, has_tag(e.kind == SVal::ENT ? e.e : nullptr, o.kind == SVal::ENT ? 0 : as_int(o), v)));
    } else if (n == 10) {
        I typ = as_int(popv());
        SVal o2 = popv(), e2 = popv(), o1 = popv(), e1 = popv();
        st.push_back(SVal::num(0xB, tags_match(e1.kind == SVal::ENT ? e1.e : nullptr, as_int(o1), e2.kind == SVal::ENT ? e2.e : nullptr,
                                               as_int(o2), typ)));
    } else if (n == 11) {
        popv(), popv();
    } else if (n == 12) {
        I msg = as_int(popv());
        popv();
        SVal tgt = popv();
        I target = tgt.kind == SVal::ENT && tgt.e->rec ? tgt.e->rec->pair : 0;
        send(msg, c.sub, c.sub_obj, nullptr, target, "send_to");
    } else if (n == 13) {
        I v = S16(as_int(popv()));
        I what = as_int(popv());
        if (what == 2)
            m_.w32(OBJ_PRIMARY, v);
        else if (what == 3)
            m_.w32(LEVEL_FLAGS, m_.u32(LEVEL_FLAGS) | 0x24);
        else if (what == 4)
            alarm_level(v);
        else if (what == 1) {
            // the music: while the level starts only the request (audio.md, Music)
            if (m_.u32(0x8009A60C))
                m_.w32(0x8009A608, 1);
            else
                eng_.note("level_set(1): music started mid-level");
        }
        stats["level_set " + std::to_string(what)]++;
    } else if (n == 14) {
        I oid = as_int(popv());
        I text = as_int(popv());
        objective_add(S32(oid), text);
    } else if (n == 15) {
        objective_remove(S32(as_int(popv())));
    } else if (n == 16) {
        I i = objective_find(S32(as_int(popv())));
        if (i >= 0) m_.w32(OBJ_DONE + 4 * i, 1);
    } else if (n == 17) {
        I i = objective_find(S32(as_int(popv())));
        st.push_back(SVal::num(0xB, i < 0 ? 1 : m_.u32(OBJ_DONE + 4 * i)));
    } else if (n == 18) {
        popv();
    } else if (n == 19) {
        I mode = as_int(popv());
        for (int k = 0; k < 6; k++) popv();
        if (mode == 3) st.push_back(SVal::num(0xB, 0));
    } else if (n == 20) {
        popv();
    } else if (n == 21) {
        popv(), popv();
    } else
        stats["unknown built-in"]++;
}

// level_set(4, id): alarm_from_level (alarm.cpp).
void LevelLogic::alarm_level(I node_id) {
    stats["alarm from level"]++;
    alarm_from_level(eng_, S32(node_id));
}

}  // namespace valor::sim
