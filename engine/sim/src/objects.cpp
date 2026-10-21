// Spawning from records and the world objects (docs/re/levels.md Spawning, objects.md):
// tools/sim/engine_objects.py.
#include "valor/sim/objects.h"

#include "valor/sim/audio.h"
#include "valor/sim/engine.h"

namespace valor::sim {

namespace {
constexpr I KNOWN_SPAWNERS[] = {0x1402, 0x1405, 0x1770, 0x1778, 0x1771, 0x1772, 0x1774, 0x1775, 0x1776};
}

void place(Engine& eng, U e, Record& rec) {
    Mem& m = eng.m;
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x20C + 4 * k, rec.pos[k] << 16);
        m.w32(e + 0x3BC + 4 * k, rec.pos[k] << 16);
        m.w32(e + 0x258 + 4 * k, rec.pos[k] << 16);
        m.w32(e + 0x228 + 4 * k, rec.ang[k] << 16);
    }
}

U model_by_hash(Engine& eng, U h) {
    for (I i = 0; i < eng.m.u32(G_NMODELS); i++) {
        U a = U(eng.m.u32(G_MODELS) + 0x100 * i);
        if (eng.m.u32(a) == h) return a;
    }
    return 0;
}

U record_spawn_entity(Engine& eng, Record& rec) {
    Mem& m = eng.m;
    U e = 0;
    if (rec.kind == 2 || rec.kind == 6) {
        U model = model_by_hash(eng, rec.model);
        if (!model) {
            eng.note("record model missing", "r" + std::to_string(rec.idx));
            return 0;
        }
        I t = m.s32(model + 0xFC);
        e = spawn_by_type(eng, t, model, rec);
        if (e) start_scripts(eng, e);
    } else if (rec.kind == 3)
        e = enemy_spawn(eng, rec);
    else if (rec.kind == 4)
        e = pickup_spawn_record(eng, rec);
    return e;
}

U spawn_by_type(Engine& eng, I t, U model, Record& rec) {
    Mem& m = eng.m;
    if (t == 0x1770 || t == 0x1778) {
        U e = eng.entity_create(t, 0, model, 0, -1, 0, 6);
        U r = rec.addr;
        I cull_v = m.s16(r + 0x5E);
        m.w32(e + 0x294, cull_v);
        m.w32(e + 0x290, cull_v);
        I g = m.u8(0x80099EC8);
        m.w8(0x80099EC8, g + 1);
        m.w8(e + 0x35, g);
        I hp = m.u8(r + 0x5C) << 16;
        m.w32(e + 0xF0, hp);
        m.w32(e + 0xF4, hp);
        for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, m.s16(r + 0x10 + 2 * k) << 16);
        I pos[3] = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
        I f = eng.world.floor(pos).height;
        I y = m.s16(r + 0x12);
        if ((0 <= y - f && y - f < 0x37) || (y - f < 0 && f - y < 0x37)) {
            m.w16(r + 0x12, f);
            rec.pos[1] = f;
        }
        for (int k = 0; k < 3; k++) {
            m.w32(e + 0x264 + 4 * k, m.s16(r + 0x18 + 2 * k) << 16);
            m.w32(e + 0x258 + 4 * k, m.s16(r + 0x10 + 2 * k) << 16);
        }
        m.w32(e + 0x370, 0x8006A5B4);
        m.w32(e + 0x384, 0x8006A37C);
        m.w16(e + 0x2EC, 0);
        m.w16(e + 0x36, 0);
        I mn[3], mx[3];
        for (int k = 0; k < 3; k++) mn[k] = m.s16(model + 0x44 + 2 * k), mx[k] = m.s16(model + 0x7C + 2 * k);
        if (t == 0x1778) {
            m.w32(e + 0x14, 0);
            m.w32(e + 0xC, 0x13028000);
            m.w32(e + 0x10, 0x40213);
            m.w16(e + 0x2EE, 0);
            box_sphere(m, e, mn, mx);
        } else {
            m.w32(e + 0x10, 0x44011);
            m.w32(e + 0xC, 0x13028400);
            m.w32(e + 0x14, 0x88214051);
            m.w32(e + 0x37C, 0x8005D32C);
            object_box(eng, e, mn, mx);
        }
        return e;
    }
    bool known = false;
    for (I k : KNOWN_SPAWNERS) known |= k == t;
    if (t == 0x1777 || t == 0x1779 || t == 0x177A || !known) return object_spawn(eng, rec, model);
    char buf[64];
    std::snprintf(buf, sizeof buf, "spawn of type %llX not implemented", (unsigned long long)t);
    eng.note(buf, "r" + std::to_string(rec.idx));
    U e = eng.entity_create(t, 0, model);
    place(eng, e, rec);
    return e;
}

U ladder_spawn(Engine& eng, U pair, U r) {
    Mem& m = eng.m;
    if (m.u32(G_LADDER_HELD) == pair)
        m.w32(G_LADDER_HELD, 0);
    else
        ladder_release(eng);
    U e = eng.entity_create(0x4C, 8, 0, 0, 0);
    m.w32(e + 8, 0x4C);
    m.w32(e + 0x10, 0x203);
    m.w32(e + 0x36C, LADDER_THINK);
    m.w32(e + 0x384, 0x8006A318);
    U o = U(m.u32(e + 0x388));
    for (U k : {0x14u, 0x370u, 0x374u, 0x378u}) m.w32(e + k, 0);
    m.w32(e + 0xC, m.u32(e + 0xC) | 0x20000);
    place_record(m, e, r);
    m.w32(o, m.s16(r + 0x2A));
    m.w32(o + 4, S32(m.s32(e + 0x210) - (m.s16(r + 0x2C) << 16)));
    return e;
}

void ladder_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U p = U(m.u32(G_SOLDIER));
    U o = U(m.u32(e + 0x388));
    I dx = iabs(S32(m.s32(e + 0x20C) - m.s32(p + 0x20C))), dz = iabs(S32(m.s32(e + 0x214) - m.s32(p + 0x214)));
    I d = (dx >> 16) + (dz >> 16) - (imin(dx, dz) >> 17);
    if (m.s32(o) >= d) {
        I top = m.s32(o + 4);
        if (m.s32(p + 0x210) >= top) m.w32(m.u32(p + 0x388) + 0x70, top);
    }
}

void ladder_release(Engine& eng) {
    Mem& m = eng.m;
    U pair = U(m.u32(G_LADDER_HELD));
    if (!pair) return;
    m.w32(G_LADDER_HELD, 0);
    if (!(m.u32(pair) & 1)) return;
    for (U e : m.entities())
        if (m.u32(e + 0x1C) == pair) {
            eng.remove(e, "ladder released");
            return;
        }
}

void place_record(Mem& m, U e, U r) {
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x228 + 4 * k, S32(m.s16(r + 0x18 + 2 * k) << 16));
        m.w32(e + 0x20C + 4 * k, S32(m.s16(r + 0x10 + 2 * k) << 16));
        m.w32(e + 0x258 + 4 * k, m.u32(e + 0x20C + 4 * k));
        m.w32(e + 0x3BC + 4 * k, m.u32(e + 0x20C + 4 * k));
    }
}

void cull(Engine& eng, U e, Record& rec) {
    Mem& m = eng.m;
    if (m.u32(rec.addr + 0x58) & 1) {
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x8000);
        I v = m.s16(rec.addr + 0x5E);
        m.w32(e + 0x290, v);
        m.w32(e + 0x294, v);
    }
}

U pickup_spawn_record(Engine& eng, Record& rec) {
    Mem& m = eng.m;
    U r = rec.addr;
    PickupRec rd;
    for (int k = 0; k < 3; k++) rd.pos[k] = m.s16(r + 0x10 + 2 * k), rd.ang[k] = m.s16(r + 0x18 + 2 * k);
    rd.flags58 = m.u32(r + 0x58), rd.cull = m.s16(r + 0x5E), rd.count = m.u8(r + 0x5C), rd.flags30 = m.u32(r + 0x30);
    rd.w = m.s16(r + 0x36), rd.addr = r;
    U e = pickup_spawn(eng, rd, U(m.u32(r + 0x60)), 0xFFFFFFFF, 0);
    if (!e) return 0;
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x20C + 4 * k, m.s16(r + 0x10 + 2 * k) << 16);
        m.w32(e + 0x3BC + 4 * k, m.s16(r + 0x10 + 2 * k) << 16);
        m.w32(e + 0x258 + 4 * k, m.s16(r + 0x10 + 2 * k) << 16);
        m.w32(e + 0x228 + 4 * k, m.s16(r + 0x18 + 2 * k) << 16);
    }
    start_scripts(eng, e);
    return e;
}

void level_explosion(Engine& eng, Ent* sub, I sub_obj, I kind) {
    Mem& m = eng.m;
    U e = sub && sub->addr ? sub->addr : 0;
    V3 pos;
    if (e) {
        for (int k = 0; k < 3; k++) pos[k] = m.s32(e + 0x20C + 4 * k);
    } else {
        U r = U(m.u32(sub_obj + 4));
        for (int k = 0; k < 3; k++) pos[k] = S32(m.s16(r + 0x10 + 2 * k) << 16);
        for (int k = 0; k < 3; k++) m.w32(0x800AB920 + 4 * k, pos[k]);
    }
    auto rd = [&]() { return eng.rand.draw({0x80052F14}); };
    if (kind == 3) {
        I n = ((rd() * 5) >> 15) + 0xF;
        particles_spawn(eng, pos, 5, n, 0x28, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x17FFF, 0, 0);
    } else if (kind == 1 || kind == 6) {
        I n = ((rd() * 3) >> 15) + 2;
        I smoke = ((rd() * 4) >> 15) + 6;
        particles_spawn(eng, pos, 1, n, 0x1E, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke);
        if (kind == 1 && ((rd() * 100) >> 15) < 0x32) {
            I s = pmod(rd(), 4);
            eng.audio->play(0, s, e, 0xCCCC, 0, 0x8000, 0x2000, 0, 0, 0, 0);
        }
    } else {
        char buf[48];
        std::snprintf(buf, sizeof buf, "level explosion(%lld) not implemented", (long long)kind);
        eng.note(buf);
    }
}

void set_home(Engine& eng, const SVal&, const SVal&) { eng.note("set_home not implemented"); }

}  // namespace valor::sim
