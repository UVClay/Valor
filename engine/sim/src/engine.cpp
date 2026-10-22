#include "valor/sim/engine.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "valor/sim/audio.h"
#include "valor/sim/load.h"
#include "valor/sim/objects.h"
#include "valor/sim/views.h"

namespace valor::sim {

// ---------------------------------------------------------------- the world's collision

bool Collision::load(Engine& eng, const std::string& data_dir, const std::string& timing_log, int mission, int level) {
    eng_ = &eng;
    std::string err;
    if (!w.load(data_dir, mission, level, &err)) return false;
    std::ifstream f(timing_log);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("watch ", 0) != 0) continue;
        long long v;
        char name[8];
        unsigned off, old, now;
        if (std::sscanf(line.c_str(), "watch %lld %7[a-z]+%X: %X -> %X", &v, name, &off, &old, &now) != 5) continue;
        std::string n = name;
        if (n == "k" || n == "ld" || n == "tmr" || n == "qi")
            events_.push_back({v, n == "k" ? 0 : n == "ld" ? 1 : 2, off, now});
    }
    for (U o = 0; o < 0xA0; o += 4) words_[o] = U(eng.m.u32(CHUNK_SLOTS + o));
    loading_ = eng.m.s32(G_CHUNK_LOADING);
    apply();
    return true;
}

void Collision::at(I vb) {
    bool changed = false;
    while (i_ < events_.size() && events_[i_].vblank < vb) {
        const Event& e = events_[i_];
        if (e.name == 0) {
            words_[e.off] = e.value;
            changed = true;
        } else if (e.name == 1) {
            loading_ = S32(e.value);
            changed = true;
        }
        i_++;
    }
    if (changed) apply();
}

void Collision::skip_to(I vb) {
    while (i_ < events_.size() && events_[i_].vblank <= vb) i_++;
}

void Collision::apply() {
    int16_t rects[4][4];
    for (int slot = 0; slot < 4; slot++) {
        U a = words_[0x28 * slot], b = words_[0x28 * slot + 4];
        rects[slot][0] = int16_t(a), rects[slot][1] = int16_t(a >> 16);
        rects[slot][2] = int16_t(b), rects[slot][3] = int16_t(b >> 16);
    }
    w.set_slots_by_rect(rects);
    if (0 <= loading_ && loading_ < 4) w.slots[size_t(loading_)] = nullptr;
    for (auto& [o, v] : words_) eng_->m.w32(CHUNK_SLOTS + o, v);
    eng_->m.w32(G_CHUNK_LOADING, loading_);
}

Floor Collision::floor(const I pos[3]) const {
    int32_t p[3] = {int32_t(pos[0]), int32_t(pos[1]), int32_t(pos[2])};
    return w.floor_find(p);
}

bool Collision::sweep(const I frm[3], const I to[3], I hit[3], I normal[3]) const {
    int32_t a[3] = {int32_t(frm[0]), int32_t(frm[1]), int32_t(frm[2])}, b[3] = {int32_t(to[0]), int32_t(to[1]), int32_t(to[2])};
    int32_t h[3];
    int16_t n[3];
    if (!w.sweep(a, b, h, n)) return false;
    for (int k = 0; k < 3; k++) hit[k] = h[k], normal[k] = n[k];
    return true;
}

// ---------------------------------------------------------------- the engine

Engine::Engine(const std::string& r, const std::string& lvl, const Feed& f) : root(r), level(lvl), feed(f), rand(f, lvl) {
    mission_n = lvl[0] - '0';
    level_n = lvl[2] - '0';
}

Engine::~Engine() = default;

bool Engine::load(const std::string& seed_path, std::string* err) {
    std::ifstream f(seed_path, std::ios::binary);
    if (!f) {
        *err = "no seed " + seed_path;
        return false;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::copy(data.begin(), data.begin() + std::min(data.size(), m.d.size()), m.d.begin());
    std::string data_dir = root + "/extracted/moh/DATA";
    if (!gte.load(data_dir, err)) return false;
    // the recording's projection (its settings.ini: 21:9, FOV 110), which aim_update's RTPS reads
    gte.set_projection(21.0 / 9.0, 110.0);
    L.load(m, level);
    if (!world.load(*this, data_dir, root + "/build/engine/timing.log", mission_n, level_n)) {
        *err = "the level's collision";
        return false;
    }
    audio = std::make_unique<Audio>(*this);
    logic = std::make_unique<LevelLogic>(*this);
    tick_no = m.u32(G_TICK);
    logic->adopt();
    if (std::getenv("VALOR_VIEW_DUMPCHECK")) {
        // the view test on the dump's own state against its drawn flags
        int agree = 0, n = 0;
        for (U e : m.entities()) {
            I fa = m.u32(e + 0xC);
            if (!(fa & 0x01000000) || (fa & 0x80000) || !m.u32(e + 0x298)) continue;
            bool iv = entity_in_view_mem(*this, e), iv2 = entity_in_view(*this, e);
            bool drawn = m.u32(e + 0x29C);
            n++, agree += iv == drawn;
            std::printf("dumpcheck %X type %llX drawn %d mem %d computed %d\n", U(m.u32(e)), (unsigned long long)m.s32(e + 8), drawn, iv, iv2);
        }
        std::printf("dumpcheck: %d of %d agree\n", agree, n);
    }
    return true;
}

void Engine::note(const std::string& kind, const std::string& detail) {
    stats[kind]++;
    auto& v = notes[kind];
    if (v.size() < 5) v.push_back({tick_no, detail});
}

U Engine::alloc(I n) {
    if (gheap) {
        U a = gheap->calloc(n);
        if (!a) {
            std::fprintf(stderr, "the game's heap is full (%lld bytes asked) at tick %lld\n", (long long)n, (long long)tick_no);
            for (auto& [k, v] : stats)
                if (k.find("destructor") != std::string::npos || k.rfind("created", 0) == 0) std::fprintf(stderr, "  %s: %lld\n", k.c_str(), (long long)v);
            std::abort();
        }
        return a;
    }
    n = (n + 15) & ~I(15);
    U a;
    auto& fb = free_blocks_[n];
    if (!fb.empty()) {
        a = fb.back();
        fb.pop_back();
    } else {
        a = heap_next_;
        heap_next_ += U(n);
        if (heap_next_ >= 0x80000000u + Mem::kSize) {
            std::fprintf(stderr, "engine heap full\n");
            std::abort();
        }
    }
    block_size_[a] = n;
    m.zero(a, n);
    return a;
}

void Engine::free(U a) {
    if (gheap) {
        gheap->free(a);
        return;
    }
    auto it = block_size_.find(a);
    if (it == block_size_.end()) return;
    I n = it->second;
    block_size_.erase(it);
    free_blocks_[n].push_back(a);
}

void Engine::link_front(U e) {
    U head = U(m.u32(G_PLAYER));
    if (!head) {
        m.w32(e + 4, m.u32(G_ENTITIES));
        m.w32(G_ENTITIES, e);
        return;
    }
    m.w32(e + 4, m.u32(head + 4));
    m.w32(head + 4, e);
}

void Engine::link_end(U e) {
    U prev = 0, cur = U(m.u32(G_ENTITIES));
    while (cur) prev = cur, cur = U(m.u32(cur + 4));
    m.w32(e + 4, 0);
    if (prev)
        m.w32(prev + 4, e);
    else
        m.w32(G_ENTITIES, e);
}

void Engine::unlink(U e) {
    U prev = 0, cur = U(m.u32(G_ENTITIES));
    while (cur && cur != e) prev = cur, cur = U(m.u32(cur + 4));
    if (!cur) return;
    if (prev)
        m.w32(prev + 4, m.u32(e + 4));
    else
        m.w32(G_ENTITIES, m.u32(e + 4));
}

U Engine::handle_alloc(U e) {
    I used = m.u32(G_SLOTS_USED);
    I slot;
    if (used < 256) {
        slot = used;
        m.w32(G_SLOTS_USED, used + 1);
    } else {
        slot = 0;
        while (slot < 256 && m.u32(G_HANDLES + 4 * slot)) slot++;
    }
    I serial = (m.u32(G_SERIAL) + 1) & 0xFFFF;
    m.w32(G_SERIAL, serial);
    U h = U(serial << 16 | slot);
    m.w32(G_HANDLES + 4 * slot, e);
    m.w32(G_SERIALS + 4 * slot, serial);
    m.w32(G_REMOVED + 4 * slot, 0);
    m.w32(e, h);
    return h;
}

bool Engine::handle_dead(I h) const {
    if (!h) return true;
    I slot = h & 0xFFFF;
    return m.u32(G_SERIALS + 4 * slot) != (U32(h) >> 16) || !m.u32(G_HANDLES + 4 * slot);
}

void Engine::handles_free_removed() {
    for (I slot = 0; slot < 256; slot++) {
        if (m.u32(G_REMOVED + 4 * slot)) {
            I e = m.u32(G_HANDLES + 4 * slot);
            for (U g : {G_LAST_EXPLOSION, G_SOLDIER, U(0x8009D644)})
                if (m.u32(g) == e) m.w32(g, 0);
            m.w32(G_HANDLES + 4 * slot, 0);
            m.w32(G_SERIALS + 4 * slot, 0);
            m.w32(G_REMOVED + 4 * slot, 0);
        }
    }
    I used = m.u32(G_SLOTS_USED);
    while (used && !m.u32(G_HANDLES + 4 * (used - 1))) used--;
    m.w32(G_SLOTS_USED, used);
}

U Engine::entity_create(I etype, I obj_size, U model, U addr, I parts, I channels, I list_n, bool instance) {
    I nparts = parts < 0 ? (model ? m.u16(model + 0xBE) : 0) : parts;
    I inst_size = (model || instance) ? 0xB0 : 0;
    I size = 0x3CC + obj_size + inst_size + 0x48 * nparts + 2 * 0x3C * channels + 0x1C * list_n;
    U e = addr ? addr : alloc(size);
    if (addr) m.zero(e, 0x3CC);
    U p = e + 0x3CC;
    if (obj_size) {
        m.w32(e + 0x388, p);
        p += U(obj_size);
    }
    if (inst_size) {
        m.w32(e + 0x100, p);
        p += U(inst_size);
    }
    m.w16(e + 0x36, nparts);
    if (nparts) {
        m.w32(e + 0x304, p);
        if (model) m.copy(p, m.u32(model + 0xC0), 0x48 * nparts);
        p += U(0x48 * nparts);
    }
    m.w32(e + 0x1EC, channels);
    if (channels) {
        m.w32(e + 0x124, p);
        m.w32(e + 0x104 + 0x20, p);
        p += U(0x3C * channels);
        m.w32(e + 0x198, p);
        m.w32(e + 0x178 + 0x20, p);
        p += U(0x3C * channels);
    }
    m.w16(e + 0x2EE, list_n);
    if (list_n) m.w32(e + 0x300, p);
    m.w32(e + 8, etype);
    handle_alloc(e);
    m.w32(e + 0x390, 0xFFFF);
    m.w32(e + 0x394, 0);
    m.w32(e + 0x398, 0xFFFF);
    m.w32(e + 0x39C, 0);
    entity_defaults(e);
    m.w32(e + 8, etype);
    if (model) instance_init(e, model);
    char buf[32];
    std::snprintf(buf, sizeof buf, "created %llX", (unsigned long long)etype);
    stats[buf]++;
    return e;
}

void Engine::entity_defaults(U e) {
    for (U o : {0xCu, 0x10u, 0x18u, 0x28u, 0x2Cu, 0x30u, 0xF8u, 0x298u, 0x36Cu, 0x370u, 0x374u, 0x378u, 0x37Cu, 0x380u,
                0x384u, 0x20Cu, 0x210u, 0x214u, 0x228u, 0x22Cu, 0x230u, 0x218u, 0x21Cu, 0x220u, 0x200u, 0x204u, 0x1FCu,
                0x24Cu, 0x250u, 0x254u, 0x1F4u, 0x2D4u, 0x44u, 0x48u, 0x3Cu, 0x40u, 0x38u, 0x3A0u})
        m.w32(e + o, 0);
    m.w32(e + 0xF4, 0x640000);
    m.w32(e + 0x294, 1);
    m.w32(e + 0x290, 1);
    for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, 0x10000);
    U inst = U(m.u32(e + 0x100));
    if (inst) {
        m.w16(inst + 0x5C, -1);
        m.w16(inst + 0x5E, 0);
        m.w16(inst + 0x62, -1);
        m.w16(inst + 0x64, 0);
        m.w16(inst + 0x3E, 0);
        m.w32(inst + 8, 0);
    }
    for (int ch = 0; ch < 2; ch++) {
        U c = e + 0x74 * ch;
        m.w32(c + 0x114, 0);
        m.w16(c + 0x11E, -1);
        for (U o : {0x104u, 0x108u, 0x10Cu, 0x110u}) m.w32(c + o, 0);
        m.w16(c + 0x122, 0);
        m.w16(c + 0x120, 0);
        m.w16(c + 0x11C, -1);
    }
    m.w32(e + 0x288, 1);
    m.w32(e + 0x28C, 1);
    m.w32(e + 0x284, 1);
    m.w16(e + 0x36, 0);
    for (U o : {0x2D8u, 0x2DAu, 0x2DCu, 0x2E0u, 0x2E2u, 0x2E4u, 0x2ECu}) m.w16(e + o, 0);
    m.w16(e + 0x2E8, 0x64);
    for (int k = 0; k < 3; k++) {
        m.w8(e + 0x2A0 + k, 0x40);
        m.w16(e + 0x2A4 + 2 * k, 0x80);
        m.w16(e + 0x2AC + 2 * k, 0x1000);
    }
    m.w32(e + 0x224, 0x10000);
    for (U o : {0x2B4u, 0x2B6u, 0x2B8u, 0x2BCu, 0x2BEu, 0x2C0u, 0x2C4u, 0x2C6u, 0x2C8u, 0x2CCu, 0x2CEu, 0x2D0u}) m.w16(e + o, 0);
    m.w16(e + 0x2EA, -1);
    m.w16(e + 0x3C8, 0x64);
    m.w16(e + 0x3CA, -1);
}

void Engine::instance_init(U e, U model) {
    U inst = U(m.u32(e + 0x100));
    m.w32(inst + 8, model);
    m.w16(inst + 0x3E, 0);
    m.w16(inst + 0x40, -1);
    m.w16(inst + 0x5C, -1);
    m.w16(inst + 0x5E, 0);
    m.w16(inst + 0x62, -1);
    m.w16(inst + 0x64, -1);
    U tbl = U(m.u32(model + 8));
    m.w32(inst + 0x74, tbl);
    if (tbl) {
        m.w16(inst + 0x70, m.u16(tbl));
        m.w32(inst + 0x78, tbl + 4);
    }
    m.w32(inst + 0x7C, m.u32(model + 0x38));
    m.w32(inst + 0x80, m.u32(model + 0x84));
    m.w32(inst + 0x84, m.u32(model + 0x9C));
    for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k));
    m.w32(e + 0x2D4, m.u32(model + 0xA0));
}

void Engine::model_swap(U e, U model, I flags) {
    if (swap_hook) {
        swap_hook(e, model, flags);
        return;
    }
    if (gheap) {
        gheap->model_swap(e, model, flags);
        return;
    }
    U inst = U(m.u32(e + 0x100));
    m.w32(inst + 0x6C, 0x80);
    m.w32(inst + 0x68, 0);
    m.w32(inst + 4, flags);
    m.w32(inst + 8, model);
    U tbl = U(m.u32(model + 8));
    I nb = tbl ? m.u8(m.u32(tbl + 4) + 1) : 0;
    U buf = alloc(imax(0x18 * nb, 0x10));
    m.w32(inst, buf);
    if (tbl) {
        m.w16(inst + 0x70, m.u16(tbl));
        m.w32(inst + 0x78, tbl + 4);
        m.w16(inst + 0x72, nb);
        m.w32(inst + 0x74, tbl);
        m.w32(inst + 0x7C, m.u32(model + 0x38));
        m.w32(inst + 0x80, m.u32(model + 0x84));
        m.w32(inst + 0x90, buf);
        m.w32(inst + 0x8C, buf + 8 * nb);
        m.w32(inst + 0x94, buf + 16 * nb);
        m.w32(inst + 0x84, m.u32(model + 0x9C));
        U first = U(m.u32(tbl + 4));
        m.copy(buf + 8 * nb, m.u32(first + 0x10), 8 * nb);
        m.copy(buf, m.u32(first + 0x10), 8 * nb);
        m.w32(inst + 0x88, 0);
        I w = m.u32(first + 4);
        m.w16(inst + 0x98, S32(w << 22) >> 22);
        m.w16(inst + 0x9A, S32(w << 11) >> 21);
        m.w16(inst + 0x9C, S32(w << 1) >> 22);
    } else {
        for (U o : {0x74u, 0x7Cu, 0x84u, 0x88u, 0x90u, 0x78u, 0x80u, 0x8Cu, 0x94u}) m.w32(inst + o, 0);
        m.w16(inst + 0x70, 0);
        m.w16(inst + 0x72, 0);
    }
    m.w16(inst + 0x3C, m.u16(model + 0x8C));
    if (flags & 2) m.w32(inst + 0x2C, m.u32(model + 0x88));
    m.w16(inst + 0x42, m.u16(model + 0x8E));
    m.w16(inst + 0x5C, -1);
    m.w16(inst + 0x5E, 0);
    m.w32(inst + 0x54, 0);
    m.w32(inst + 0x58, 0);
    m.w32(e + 0x2D4, m.u32(model + 0xA0));
    for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k));
    I n = m.u16(model + 0xBE);
    m.w16(e + 0x36, n);
    U pp = U(m.u32(e + 0x304)), tp = U(m.u32(model + 0xC0));
    for (I i = 0; i < n; i++) {
        U q = U(pp + 0x48 * i), t = U(tp + 0x48 * i);
        m.w32(q + 4, m.u32(t + 4));
        m.w32(q + 8, m.u32(t + 0xC));
        m.w32(q + 0xC, m.u32(t + 0xC));
        m.w16(q + 0x1C, m.u16(t + 0x1C));
        m.w16(q + 0x1E, m.u16(t + 0x1E));
        for (U o : {0x34u, 0x30u, 0x38u, 0x10u, 0x14u}) {
            I v = m.u32(t + o);
            m.w32(q + o, ((flags & 0x200) && (o == 0x30 || o == 0x34 || o == 0x38)) ? S32(v * 2) : v);
        }
        for (U o : {0x40u, 0x42u, 0x44u}) m.w16(q + o, m.u16(t + o));
        m.w32(q + 0x18, inst);
        m.w32(q + 0x3C, 0);
    }
}

void Engine::remove(U e, const std::string& why) {
    if (m.u32(e + 0xC) & REMOVED) return;
    m.w32(e + 0xC, m.u32(e + 0xC) | REMOVED);
    auto it = ents.find(e);
    Ent* ent = it == ents.end() ? nullptr : it->second;
    unlink(e);
    I h = m.u32(e);
    m.w32(G_REMOVED + 4 * (h & 0xFFFF), 1);
    run_destroy(*this, m.s32(e + 8), e);
    if (ent) logic->removed(ent, why);
    if (gheap)
        gheap->destroy(e);
    else if (e >= HEAP)
        free(e);
}

bool Engine::action_pressed(int a, int port) const {
    I mask = m.u32(ACTIONS + 4 * (port * 9 + a)) & 0xFFFF;
    return (pad_now & mask) && !(pad_prev & mask);
}

bool Engine::action_held(int a, int port) const {
    I mask = m.u32(ACTIONS + 4 * (port * 9 + a)) & 0xFFFF;
    return pad_now & mask;
}

void Engine::tick(bool) {
    tick_no++;
    m.w32(G_TICK, tick_no);
    rand.start_tick(tick_no);
    auto it = feed.pad.find({level, tick_no});
    if (feed.live || it != feed.pad.end()) {
        const auto& pad = feed.live ? feed.live_pad : it->second;
        pad_now = pad[0] & 0xFFFF, pad_prev = pad[2] & 0xFFFF;
        m.w32(PAD_NOW, pad[0]);
        m.w32(PAD_NOW + 4, pad[1]);
        m.w32(PAD_PREV, pad[2]);
        m.w32(PAD_PREV + 4, pad[3]);
    }
    entities_update();
    entities_collide(*this);
    timer_message();
}

void Engine::entities_update() {
    witness_check(*this);
    U e = U(m.u32(G_ENTITIES));
    while (e) {
        U nxt = U(m.u32(e + 4));
        if (!(m.u32(e + 0xC) & REMOVED)) update(e);
        e = nxt;
    }
}

void Engine::update(U e) {
    I t = m.s32(e + 8);
    U think = U(m.u32(e + 0x36C));
    if (think) {
        if (!run_think(*this, think, e)) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "think %08X (type %llX) not implemented", think, (unsigned long long)t);
            note(buf);
        }
        if (m.u32(e + 0xC) & REMOVED) return;
        m.w32(e + 0x38, 0);
        m.w32(e + 0x3C, 0);
        m.w32(e + 0x40, 0);
        // attached (+0x30): to a stale or removed target it is let go; else it sits on the
        // target's bone +0x34 (model_bone_point) and does not move itself
        U attach = resolve(m.u32(e + 0x30));
        if (attach) {
            if (handle_dead(m.u32(e + 0x30)) || (m.u32(attach + 0xC) & REMOVED))
                m.w32(e + 0x30, 0);
            else {
                V3 p = model_bone_point(*this, attach, m.s8(e + 0x34), nullptr);
                for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(S16(p[k]) << 16));
            }
        }
        attach = resolve(m.u32(e + 0x30));
        if (!attach && !(m.u32(e + 0xC) & 0x20000)) {
            I res = entity_move(*this, e);
            if (m.u32(e + 0xC) & 5) zones_update(e);
            if (res) {
                U wc = U(m.u32(e + 0x378));
                if (!run_world_contact(*this, wc, e, res) && wc) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "world_contact %08X not implemented", wc);
                    note(buf);
                }
            }
        }
        if (!(m.u32(e + 0xC) & REMOVED) && m.u32(e + 0x38C)) brain_update_entity(*this, e);
    }
    if (!(m.u32(e + 0x18) & 2)) m.w32(e + 0x18, m.u32(e + 0x18) & ~I(1));
    matrix(e);
    if (m.u32(e + 0xC) & 1) player_weapon_update(*this, e);
    entity_bounds(*this, e);
    if (t == 2) camera_setup(*this, e);
}

void Engine::matrix(U e) {
    int16_t r[3][3];
    rot_yxz(int32_t(S16(m.s32(e + 0x228) >> 16)), int32_t(S16(m.s32(e + 0x22C) >> 16)), int32_t(S16(m.s32(e + 0x230) >> 16)), r);
    // the translation +0x354 = the integer position (TransMatrix), then ScaleMatrix by the
    // scale +0x270 >> 4 (each column times its axis' scale >> 12; 1.0 leaves it)
    I sc[3] = {m.s32(e + 0x270) >> 4, m.s32(e + 0x274) >> 4, m.s32(e + 0x278) >> 4};
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) {
            I v = r[i][k];
            if (sc[k] != 0x1000) v = S16((v * sc[k]) >> 12);
            m.w16(e + 0x340 + 6 * i + 2 * k, v);
        }
    for (int k = 0; k < 3; k++) m.w32(e + 0x354 + 4 * k, m.s16(e + 0x20E + 4 * k));
}

void Engine::zones_use(U e) {
    auto it = ents.find(e);
    if (it != ents.end()) logic->use(it->second);
}

void Engine::zones_update(U e) {
    if (!(m.u32(e + 0xC) & 0x51)) return;
    auto it = ents.find(e);
    if (it == ents.end()) return;
    I pos[3];
    for (int k = 0; k < 3; k++) pos[k] = S16(m.s32(e + 0x20C + 4 * k) >> 16);
    logic->zones_update(it->second, m.u32(e + 0x390), pos);
}

void Engine::timer_message() {
    if (m.s32(G_VBL_TIMER) >= 10) {
        m.w32(G_VBL_TIMER, m.s32(G_VBL_TIMER) - 20);
        logic->send(0x59, nullptr, 0, nullptr, 0, "timer");
    }
}

void Engine::end_frame() {
    draw_pass(*this);
    idle_work();
    audio->update();
}

// The frame's wait for the vblank as recorded: frame_idle_job / frame_idle_loop calls.
void Engine::idle_work() {
    auto it = feed.idle.find({level, tick_no});
    size_t& used = idle_used_[tick_no];
    if (it == feed.idle.end() || used >= it->second.size()) {
        note("idle work: no record of the frame (all messages, spawns, handles)");
        logic->run_queue();
        handles_free_removed();
    } else {
        for (I j : it->second[used++]) {
            if (j < 0)
                idle_loop();
            else
                idle_job(j);
        }
    }
}

void Engine::idle_job(I state) {
    if (state != m.s32(0x800AB8E8)) note("idle job state differs from the original's");
    logic->run_one();
    if (state == 1)
        logic->spawn_retry_one();
    else if (state == 2) {
        handles_free_removed();
        logic->run_one();
        if (gheap) chunk_stream();
    } else if (state == 3 && gheap)
        chunk_stream();
    m.w32(0x800AB8E8, state >= 2 ? 0 : state + 1);
}

void Engine::idle_loop() {
    I k = m.s32(0x800AB8EC);
    if (k == 0)
        logic->run_queue_all();
    else if (k == 1)
        logic->spawn_retry();
    m.w32(0x800AB8EC, (0 <= k && k < 3) ? k + 1 : 0);
}

static void write_dump(const Mem& m, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(m.d.data()), std::streamsize(m.d.size()));
}

// Live play: from here on the engine takes its input from the host (feed.live_*), draws with
// PsyQ's rand, collides with every chunk of the level, and asks the host what the view shows.
void Engine::start_live(std::function<bool(U)> in_view) {
    // without the host's own test, the original's (entity_in_view, at the recording's projection)
    live_view = in_view ? std::move(in_view) : [this](U e) { return entity_in_view(*this, e); };
    rand.live_mem = &m;
    chunks_apply();
    vblank_prev = vblank;
}

// The collision tests' slots: the slot table's chunks, the one being read left out.
void Engine::chunks_apply() {
    int nums[4];
    for (int k = 0; k < 4; k++) nums[k] = int(m.s32(0x80098970 + 4 * k));
    world.w.set_slots_by_number(nums);
    I ld = m.s32(G_CHUNK_LOADING);
    if (0 <= ld && ld < 4) world.w.slots[size_t(ld)] = nullptr;
}

// A chunk's read: in the session the four reads took 244 to 353 vblanks each, the music's
// reads sharing the CD; live play takes 250 (125 frames).
constexpr I STREAM_READ_FRAMES = 125;

// game_frame: a chunk read that has ended is parsed and its slot opened (0x8008C798).
// Live play counts the read's frames down; a recorded run ends it when the CD did (the timing
// log clears the loading slot).
void Engine::stream_end() {
    if (!gheap || stream_slot < 0) return;
    if (live_view) {
        if (--stream_frames_left > 0) return;
    } else if (m.s32(G_CHUNK_LOADING) != -1)
        return;
    gheap->tsp_load(m.s32(0x80098970 + 4 * stream_slot), stream_slot);
    stream_slot = -1;
    if (live_view) {
        m.w32(G_CHUNK_LOADING, -1);
        chunks_apply();
    }
}

// The campaign's slots (shell.md, The campaign): mission index, level count; slot 6 (MSN8) isn't
// on the disc; slot 7's third level is LVL4.
namespace {
constexpr int kSlots[8][2] = {{0, 3}, {1, 4}, {2, 4}, {3, 3}, {4, 4}, {6, 3}, {7, 3}, {8, 3}};
}

bool Engine::level_transition() {
    constexpr U G_GAME = 0x800385CC, G_LEVEL_END = 0x80038D4C;
    I flags = m.u32(0x8009C840);
    // level_exit (0x80062298): reason 4 (flags 4) or 1 (flags 8), both carry: level_end_carry
    m.w32(0x80039C58, (flags & 8) ? 3 : 0);
    U sol = U(m.u32(G_SOLDIER));
    U inv = sol ? U(m.u32(sol + 0x388)) : 0;
    for (I i = 0; i < m.s32(0x8009E874) && inv; i++) {
        U w = U(0x80093640 + 0x44 * m.u8(0x8009E878 + i));
        m.w16(w + 6, m.u16(inv + 0x56 + 2 * i) + m.u16(inv + 0x66 + 2 * i));
    }
    for (I k = 0; k < 0xD; k++) {
        m.w16(G_LEVEL_END + 0x10 + 4 * k, m.u16(0x80093640 + 0x44 * k + 6));
        m.w16(G_LEVEL_END + 0x12 + 4 * k, m.u16(0x80093640 + 0x44 * k + 0x30));
    }
    I n = m.s32(0x80098620), done = 0;
    for (I i = 0; i < n; i++) done += m.u32(0x800988DC + 4 * i) != 0;
    m.w8(G_LEVEL_END + 1, done);
    m.w8(G_LEVEL_END, n);
    if (done == n) m.w32(0x80098624, 1);
    m.w32(G_LEVEL_END + 0xC, m.u32(0x80098624));
    m.w32(G_LEVEL_END + 8, sol ? m.u32(sol + 0xF4) : 0);
    // progress_commit (CONTINUE): carry_to_next_level within the slot, campaign_advance
    I mission = m.s32(G_GAME + 0x94), cur_level = m.s32(G_GAME + 0x98);
    int slot = -1;
    for (int k = 0; k < 8; k++)
        if (kSlots[k][0] == mission) slot = k;
    I idx = (slot == 7 && cur_level == 3) ? 2 : cur_level;  // slot 7's third level is LVL4
    if (slot < 0) {
        note("level transition: not a campaign mission");
        campaign_done = true;
        return false;
    }
    I next_mission = mission, next_level = idx + 1;
    bool carry = next_level < kSlots[slot][1];
    if (!carry) {
        if (slot == 7) {
            campaign_done = true;
            stats["the campaign's end"]++;
            return false;
        }
        int ns = slot == 5 ? 7 : slot + 1;
        next_mission = kSlots[ns][0], next_level = 0, slot = ns;
    }
    I dir_level = (slot == 7 && next_level == 2) ? 3 : next_level;
    std::vector<uint8_t> game(m.ptr(G_GAME), m.ptr(G_GAME) + 0x780);
    auto w16 = [&](size_t o, I v) { game[o] = uint8_t(v), game[o + 1] = uint8_t(v >> 8); };
    auto w32 = [&](size_t o, I v) {
        for (int k = 0; k < 4; k++) game[o + size_t(k)] = uint8_t(v >> (8 * k));
    };
    size_t row = 0x18C + size_t(next_mission) * 0x98 + size_t(dir_level) * 0x24;
    if (carry) {
        for (I k = 0; k < 0xD; k++) w16(row + 2 * size_t(k), m.u16(G_LEVEL_END + 0x10 + 4 * k));
        w32(row + 0x1C, m.u32(G_LEVEL_END + 8));
    }
    w32(row + 0x20, 1);  // unlocked
    w32(0x94, next_mission);
    w32(0x98, dir_level);
    w32(0xA8, slot);
    w32(0xAC, next_level);
    // the next LEVEL: the overlay and heap afresh, rand's seed and the save data carried over
    LevelBuild lb;
    lb.rand_seed = U(m.u32(0x800CA2A4));
    mission_n = int(next_mission + 1), level_n = int(dir_level + 1);
    level = std::to_string(mission_n) + ":" + std::to_string(level_n);
    std::string err;
    auto view = live_view;
    I timer = m.s32(G_VBL_TIMER);
    ents.clear();
    if (!load_disc(game, lb, &err)) {
        note("level transition: " + err);
        campaign_done = true;
        return false;
    }
    m.w8(0x80093B78, 1);
    m.w32(G_VBL_TIMER, timer);
    stream_slot = -1;
    stream_frames_left = 0;
    start_live(view);
    level_changes++;
    stats["level transitions"]++;
    return true;
}

void Engine::level_restart() {
    for (U e : m.entities()) {
        I t = m.s32(e + 8);
        if (t == 0 || (t >= 4 && t != 8)) remove(e, "level restart");
    }
    handles_free_removed();
    gheap->level_start();
    stream_slot = -1;
    stream_frames_left = 0;
    chunks_apply();
    restarts++;
    stats["level restarts"]++;
}

void Engine::chunk_stream() {
    U rec = U(m.u32(0x80099070));
    auto loaded = [&](I c) {
        for (int k = 0; k < 4; k++)
            if (m.s32(0x80098970 + 4 * k) == c) return true;
        return false;
    };
    auto not_in_record = [&](I c) {
        for (int k = 0; k < 4; k++)
            if (m.s16(rec + 0x30 + 2 * k) == c) return false;
        return true;
    };
    I ld = m.s32(G_CHUNK_LOADING);
    if (ld != -1 && !(rec && not_in_record(m.s32(0x80098970 + 4 * ld)))) return;
    if (ld != -1) note("chunk_stream: a read replaced");
    m.w32(0x8009896C, rec);
    // chunk_stream_needed (0x80054100): the record's chunks not loaded, in its order
    for (int k = 0; k < 4; k++) m.w32(0x80099074 + 4 * k, -1);
    if (rec) {
        I n = 0;
        for (int k = 0; k < 4; k++) {
            I c = m.s16(rec + 0x30 + 2 * k);
            if (!loaded(c)) m.w32(0x80099074 + 4 * n++, c);
        }
        m.w32(0x80098964, n);
    }
    // chunk_stream_evictable (0x80054298): the slots whose chunk the record doesn't list
    m.w32(0x80098968, 0);
    for (int k = 0; k < 4; k++) m.w32(0x80099084 + 4 * k, -1);
    if (rec)
        for (int k = 0; k < 4; k++) {
            I c = m.s32(0x80098970 + 4 * k);
            if (c != -1 && not_in_record(c)) {
                I n = m.s32(0x80098968);
                m.w32(0x80099084 + 4 * n, k);
                m.w32(0x80098968, n + 1);
            }
        }
    if (!m.s32(0x80098964)) return;
    if (m.s32(0x80098964) != m.s32(0x80098968)) note("chunk_stream: needed and evictable differ");
    I slot = m.s32(0x80099084);
    I file = m.s32(0x80099074);
    if (slot < 0) return;
    if (gheap) gheap->tsp_unload(slot);
    m.w32(G_CHUNK_LOADING, slot);  // tsp_load_async (0x8008C718): the read under way
    m.w32(0x80098970 + 4 * slot, file);
    stream_frames_left = STREAM_READ_FRAMES;
    stream_slot = slot;
    stats["chunk reads"]++;
    if (live_view) chunks_apply();
}

// One frame of live play: the vblanks since the last (the 20-vblank timer), the character pools,
// one tick on the input the host set, the draw pass and the idle work while the frame waits for
// its vblank.
void Engine::live_frame(I vblanks) {
    vblank += vblanks;
    m.w32(G_VBL_TIMER, m.s32(G_VBL_TIMER) + vblanks);
    vblank_prev = vblank;
    stream_end();
    if (gheap) gheap->frame();
    char_pool_update(*this);
    tick(true);
    draw_pass(*this);
    // frame_wait_vblank (0x80090B80) at 30 Hz, one tick a frame: one idle job, then, the frame's
    // work having run past its first vblank, idle jobs until the second: 189 more, the 1:3
    // session's median for such frames (2:1 204, 2:2 498; objects.md, The frame and the tick)
    for (int k = 0; k < 1 + 189; k++) idle_job(m.s32(0x800AB8E8));
    audio->update();
    // level_main: a death (flags 2) ends the frame loop; the level restarts in place
    if ((m.u32(0x8009C840) & 2) && gheap) level_restart();
    if ((m.u32(0x8009C840) & 0xC) && gheap) level_transition();
}

void Engine::run(I to, const std::function<void(const StateLine&)>& line) {
    for (auto& [vb, ticks] : frames) {
        if (to && !ticks.empty() && ticks[0] > to) break;
        if (vblank_prev) m.w32(G_VBL_TIMER, m.s32(G_VBL_TIMER) + (vb - *vblank_prev));
        vblank_prev = vb;
        vblank = vb;
        world.at(vb);
        if (gheap) stream_end(), gheap->frame();
        char_pool_update(*this);
        // a paused frame (no tick due): the timer reset, then the draw and the idle work all
        // the same (game_frame, objects.md)
        if (ticks.empty()) m.w32(G_VBL_TIMER, 0);
        for (size_t i = 0; i < ticks.size(); i++) {
            if (ticks[i] != tick_no + 1) note("tick numbering", std::to_string(ticks[i]) + " after " + std::to_string(tick_no));
            tick(i == 0);
            if (line) line(state_line());
        }
        auto d = dumps.find(std::to_string(tick_no));
        if (d != dumps.end()) write_dump(m, d->second);
        end_frame();
        d = dumps.find(std::to_string(tick_no) + "a");
        if (d != dumps.end()) write_dump(m, d->second);
    }
}

// The state trace's line, read through the accessor layer (views.h) as a host would.
StateLine Engine::state_line() const {
    StateLine out;
    out.tick = tick_no, out.vblank = vblank;
    view::Entity sol = view::soldier(m);
    if (sol.a) {
        view::Player p = view::player(m);
        out.has_player = true;
        out.player = {p.a ? p.look_yaw() : 0, p.a ? p.look_pitch() : 0, p.slot() & 0xFF, p.weapon(), p.clip(), p.reserve(), sol.flags_b()};
    }
    for (const view::Entity& e : view::entities(m)) {
        view::Instance in = view::instance(e);
        out.ents.push_back({e.handle(), e.type(), e.flags_a(), e.x(), e.y(), e.z(), e.pitch(), e.yaw(), e.roll(), e.health(),
                            in.a ? in.anim() : -1, in.a ? in.frame() : -1});
    }
    return out;
}

}  // namespace valor::sim
