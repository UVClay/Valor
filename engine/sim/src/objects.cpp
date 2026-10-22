// Spawning from records and the world objects (docs/re/levels.md Spawning, objects.md):
// tools/sim/engine_objects.py.
#include <array>

#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"

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

// scripted_think (0x8005E61C): the event matcher, then the channels.
void scripted_think(Engine& eng, U e) {
    VM vm(eng, e);
    vm.run_b();
    vm.run_a();
}

// prop1771_spawn (0x80046B2C): entity_create(3, no object, the model, instance flags 0x15); flags
// A 0x03008004, mask 0, B 0x211, the model's type; the record's cull counter (+0x5E), group
// 0x800939D8, health (+0x5C); no collision (radius 0), drawn as a model, destructor 0x8006A37C;
// variable 1 = the record's +0x34.
U prop1771_spawn(Engine& eng, Record& rec, U model) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = eng.entity_create(m.s32(model + 0xFC), 0, model, 0, 0, 0, 0, false, 0x15);
    if (!e) return 0;
    m.w32(e + 0xC, 0x03008004);
    m.w32(e + 0x14, 0);
    m.w32(e + 0x10, 0x211);
    m.w32(e + 8, m.s32(model + 0xFC));
    I cv = m.s16(r + 0x5E);
    m.w32(e + 0x294, cv);
    m.w32(e + 0x290, cv);
    I g = m.u8(0x800939D8);
    m.w8(0x800939D8, (g + 1) & 0xFF);
    m.w32(e + 0x36C, PROP1771_THINK);
    m.w32(e + 0x370, 0x8006A5B4);
    m.w16(e + 0x2EC, 0);
    m.w16(e + 0x2E8, 0);
    m.w8(e + 0x35, g);
    m.w32(e + 0x374, 0);
    m.w32(e + 0x378, 0);
    m.w32(e + 0x384, 0x8006A37C);
    I hp = m.u8(r + 0x5C) << 16;
    m.w32(e + 0xF0, hp);
    m.w32(e + 0xF4, hp);
    m.w32(e + 0x4C + 4, m.s16(r + 0x34));
    return e;
}

// prop1771_think (0x80046A88): removed once variable 6 is below variable 2; otherwise its scripts
// (channels only) and the spin from variables 3..5 (angular velocity, whole units).
void prop1771_think(Engine& eng, U e) {
    Mem& m = eng.m;
    if (m.s32(e + 0x4C + 4 * 6) < m.s32(e + 0x4C + 4 * 2)) {
        eng.remove(e, "prop done");
        return;
    }
    VM(eng, e).run_a();
    for (U k = 0; k < 3; k++) m.w32(e + 0x234 + 4 * k, S32(m.s32(e + 0x4C + 4 * (3 + k)) << 16));
}

// door_spawn (0x8005E648), type 0x1776 (engine_objects.py door_spawn, objects.md), and the
// switches' spawn (0x8005E510, type 0x1775): the same but for 5 channels, no collision mask,
// flags A 0x03008000, B 0x40213, their own group counter (0x80099ECA) and think (0x8005E4E4:
// script_run_b, script_run_a, as scripted_think).
static U scripted_spawn(Engine& eng, Record& rec, U model, bool sw) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = eng.entity_create(m.s32(model + 0xFC), 0, 0, 0, m.u16(model + 0xBE), sw ? 5 : 0xA, 6, true);
    eng.model_swap(e, model, 0x157);
    m.w32(e + 0x14, sw ? 0 : 0x88214051);
    m.w32(e + 0xC, sw ? 0x03008000 : 0x03008400);
    m.w32(e + 0x10, sw ? 0x40213 : 0x44030);
    m.w32(e + 8, m.s32(model + 0xFC));
    I cv = m.s16(r + 0x5E);
    U gc = sw ? 0x80099ECA : 0x80099ECB;
    I g = m.u8(gc);
    m.w32(e + 0x294, cv);
    m.w32(e + 0x290, cv);
    m.w8(gc, (g + 1) & 0xFF);
    m.w32(e + 0x36C, sw ? SWITCH_THINK : SCRIPTED_THINK);
    m.w32(e + 0x370, 0x8006A5B4);
    m.w32(e + 0x384, 0x8006A37C);
    m.w16(e + 0x2EC, 0);
    m.w8(e + 0x35, g);
    m.w32(e + 0x374, 0);
    m.w32(e + 0x378, 0);
    I hp = m.u8(r + 0x5C) << 16;
    m.w32(e + 0xF0, hp);
    m.w32(e + 0xF4, hp);
    U mdl = U(m.u32(m.u32(e + 0x100) + 8));
    I mn[3], mx[3];
    for (int k = 0; k < 3; k++) mn[k] = m.s16(mdl + 0x44 + 2 * k), mx[k] = m.s16(mdl + 0x7C + 2 * k);
    object_box(eng, e, mn, mx);
    m.w16(e + 0x36, 0);
    return e;
}

U door_spawn(Engine& eng, Record& rec, U model) { return scripted_spawn(eng, rec, model, false); }
U switch_spawn(Engine& eng, Record& rec, U model) { return scripted_spawn(eng, rec, model, true); }

// obj1774_spawn (0x800905C0), the V2 of 9:4 (objects.md, 0x1774): entity_create(3, object
// 0x1344, the model, instance flags 0x15, no parts, 5 channels, a list of 6); a 0x3CC-byte block
// from the heap kept at 0x800AB8D8; placed at its record, its matrix; mask 0x88214051, flags A
// 0x408, B 0x4200; the record's cull counter and health, group 0x800AB8DC; think 0x8008FDF8,
// draw 0x8009053C, destructor 0x80090578; the model's box (0x8004A8F8). Its object: the point
// between the model's vertices 16 and 32 (+0x2C) and that point in the world (+0x34, +0x3C),
// and two particle systems of 40, smoke at +0x44 and fire at +0x9C4 (their records' state 6).
U obj1774_spawn(Engine& eng, Record& rec, U model) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = eng.entity_create(m.s32(model + 0xFC), 0x1344, model, 0, 0, 5, 6, false, 0x15);
    if (!e) return 0;
    m.w32(0x800AB8D8, eng.alloc(0x3CC));
    place(eng, e, rec);
    {  // 0x8006A76C: the matrix +0x340 (RotMatrixYXZ, the integer position, the scale >> 4)
        int16_t rot[3][3];
        rot_yxz(int32_t(m.s16(e + 0x22A)), int32_t(m.s16(e + 0x22E)), int32_t(m.s16(e + 0x232)), rot);
        I sc[3] = {m.s32(e + 0x270) >> 4, m.s32(e + 0x274) >> 4, m.s32(e + 0x278) >> 4};
        for (U i = 0; i < 3; i++)
            for (U j = 0; j < 3; j++) m.w16(e + 0x340 + 6 * i + 2 * j, S16((rot[i][j] * sc[j]) >> 12));
        for (U k = 0; k < 3; k++) m.w32(e + 0x354 + 4 * k, m.s16(e + 0x20E + 4 * k));
    }
    m.w32(e + 0x14, 0x88214051);
    m.w32(e + 0xC, 0x408);
    m.w32(e + 0x10, 0x4200);
    m.w32(e + 8, m.s32(model + 0xFC));
    I cv = m.s16(r + 0x5E);
    I g = m.u8(0x800AB8DC);
    m.w32(e + 0x294, cv);
    m.w32(e + 0x290, cv);
    m.w8(0x800AB8DC, g + 1);
    m.w32(e + 0x36C, 0x8008FDF8);
    m.w32(e + 0x370, 0x8009053C);
    m.w32(e + 0x384, 0x80090578);
    m.w16(e + 0x2EC, 0);
    m.w8(e + 0x35, g);
    m.w32(e + 0x374, 0);
    m.w32(e + 0x378, 0);
    I hp = m.u8(r + 0x5C) << 16;
    m.w32(e + 0xF0, hp);
    m.w32(e + 0xF4, hp);
    U mdl = U(m.u32(U(m.u32(e + 0x100)) + 8));
    I mn[3], mx[3];
    for (U k = 0; k < 3; k++) mn[k] = m.s16(mdl + 0x44 + 2 * k), mx[k] = m.s16(mdl + 0x7C + 2 * k);
    object_box(eng, e, mn, mx);
    m.w32(e + 0x54, 0);
    U vt = U(m.u32(mdl + 0x88)), o = U(m.u32(e + 0x388));
    for (U k = 0; k < 3; k++) m.w16(o + 0x2C + 2 * k, (m.s16(vt + 0x80 + 2 * k) + m.s16(vt + 0x100 + 2 * k)) >> 1);
    // the point in the world: MVMVA (sf 1) of the entity's matrix and translation
    M3 M;
    for (U i = 0; i < 3; i++)
        for (U j = 0; j < 3; j++) M[i][j] = m.s16(e + 0x340 + 6 * i + 2 * j);
    V3 mac = gmul_mac(M, {m.s16(o + 0x2C), m.s16(o + 0x2E), m.s16(o + 0x30)}, {m.s32(e + 0x354), m.s32(e + 0x358), m.s32(e + 0x35C)}, 12);
    for (U k = 0; k < 3; k++) m.w16(o + 0x34 + 2 * k, mac[k]);
    // the fire (system +0x9C4, records +0x9E4)
    U s1 = o + 0x9C4;
    m.w16(o + 2, 1);
    m.w8(s1 + 1, 0x28);
    m.w8(s1, 2);
    m.w32(s1 + 8, 0xF0000);
    m.w32(s1 + 0xC, -0x2000);
    m.w16(s1 + 0x10, 0x3E8);
    m.w16(s1 + 0x14, 0x3E0);
    m.w16(s1 + 0x16, 0xFA);
    m.w16(s1 + 0x18, 0x1F);
    m.w16(s1 + 0x1A, 0x1F);
    m.w16(s1 + 0x1C, 8);
    m.w32(s1 + 4, 0x27FFF);
    m.w16(s1 + 0x12, 0);
    m.w16(s1 + 0x1E, 2);
    for (U i = 0; i < 40; i++) m.w8(o + 0x9E4 + 0x3C * i, 6);
    // the smoke (system +0x44, records +0x64); the world point kept again at +0x3C
    for (U k = 0; k < 8; k++) m.w8(o + 0x3C + k, m.u8(o + 0x34 + k));
    U s2 = o + 0x44;
    m.w16(o, 1);
    m.w16(o + 0x1C, 5);
    m.w8(s2 + 1, 0x28);
    m.w8(s2, 2);
    m.w32(s2 + 4, 0x10000);
    m.w32(s2 + 8, 0xF0000);
    m.w32(s2 + 0xC, -0x200);
    m.w16(s2 + 0x12, 0x20);
    m.w16(s2 + 0x16, 0xFF);
    m.w16(s2 + 0x18, 0x3F);
    m.w16(s2 + 0x1A, 0x3F);
    m.w16(s2 + 0x1C, 0x10);
    m.w16(s2 + 0x10, 0x3E0);
    m.w16(s2 + 0x14, 0x3E0);
    m.w16(s2 + 0x1E, 2);
    for (U i = 0; i < 40; i++) m.w8(o + 0x64 + 0x3C * i, 6);
    m.w16(o + 0x26, 0x64);
    m.w16(o + 0x28, 0x190);
    m.w16(o + 0x24, 0x190);
    m.w16(o + 0x20, 0);
    m.w16(o + 0x22, 0x19);
    return e;
}

U spawn_by_type(Engine& eng, I t, U model, Record& rec) {
    Mem& m = eng.m;
    if (t == 0x1771) return prop1771_spawn(eng, rec, model);
    if (t == 0x1772) return mounted_gun_spawn(eng, rec, model);
    if (t == 0x1774) return obj1774_spawn(eng, rec, model);
    if (t == 0x1770 || t == 0x1778) {
        U e = eng.entity_create(t, 0, model, 0, -1, 0, 6, false, 0x157);
        if (!e) return 0;
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
    if (t == 0x1776) return door_spawn(eng, rec, model);
    if (t == 0x1775) return switch_spawn(eng, rec, model);
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

// 0x80074A60 -> 0x80074898: entity_create(0, object 0xB64, no model, 2 hit parts), type 0x1644,
// flags B | 0x200, its emitter set up (0x8006E8D8, 0x80070704: drawing), its think the emitter's
// update (0x80074A38: endless, drawing only); variables 0, 2, 3 = 0.
U smoke_emitter(Engine& eng) {
    Mem& m = eng.m;
    U p = eng.entity_create(0x1644, 0xB64, 0, 0, 2);
    if (!p) return 0;
    m.w32(p + 8, 0x1644);
    m.w32(p + 0x36C, SMOKE_THINK);
    m.w32(p + 0x370, 0x800747CC);
    m.w32(p + 0x384, 0x8006A318);
    m.w32(p + 0x374, 0);
    m.w32(p + 0x378, 0);
    m.w32(U(m.u32(p + 0x388)) + 0xB48, 0);
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x200);
    for (U v : {0u, 2u, 3u}) m.w32(p + 0x4C + 4 * v, 0);
    return p;
}

// 0x8004FE84: an entity of type 0 (object 0x64; flags A 0x20000, B 0x203) that waits the delay,
// steps its colour over the length (mode 1), holds, then goes (think 0x8004FC14); its quads are
// drawing; linked at the front.
U screen_flash(Engine& eng, I mode, I delay, I length, I hold, std::array<I, 3> c0, std::array<I, 3> c1) {
    Mem& m = eng.m;
    U p = eng.entity_create(0, 0x64);
    if (!p) return 0;
    m.w32(p + 8, 0);
    m.w32(p + 0x14, 0);
    m.w32(p + 0x374, 0);
    m.w32(p + 0x378, 0);
    m.w32(p + 0xC, 0x20000);
    m.w32(p + 0x10, 0x203);
    m.w32(p + 0x36C, FLASH_THINK);
    m.w32(p + 0x370, 0x8004FDA8);
    m.w32(p + 0x384, 0x8006A318);
    U o = U(m.u32(p + 0x388));
    m.w32(o, mode);
    m.w32(o + 4, delay);
    m.w32(o + 8, length);
    m.w32(o + 0xC, hold);
    for (int k = 0; k < 3; k++) {
        m.w8(o + 0x18 + k, c0[k]);
        m.w16(o + 0x10 + 2 * k, (c1[k] - c0[k]) / S16(length));
    }
    eng.link_front(p);
    return p;
}

// 0x8004FC14: the delay, then the ramp (mode 1: the colour stepped), then the hold; then removed.
void flash_think(Engine& eng, U p) {
    Mem& m = eng.m;
    U o = U(m.u32(p + 0x388));
    if (m.s32(o + 4) > 0) {
        m.w32(o + 4, m.s32(o + 4) - 1);
        return;
    }
    if (m.s32(o + 8) > 0) {
        if (m.u32(o) & 1)
            for (int k = 0; k < 3; k++) m.w8(o + 0x18 + k, (m.u8(o + 0x18 + k) + m.u8(o + 0x10 + 2 * k)) & 0xFF);
        m.w32(o + 8, m.s32(o + 8) - 1);
        return;
    }
    if (m.s32(o + 0xC) <= 0) {
        eng.remove(p, "flash done");
        return;
    }
    m.w32(o + 0xC, m.s32(o + 0xC) - 1);
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
    U rec = sub_obj ? U(m.u32(sub_obj + 4)) : 0;
    if (kind == 0 || kind == 5) {
        // an explosion (4 fireballs, 4 puffs, spread 60; scale 1.0, var 0x1000, damage 0x40000,
        // reach 7) and a burst of sparks; 0 also its sound
        explosion_spawn(eng, e, 4, 4, 0x3C, 0x10000, 0x1000, 0x40000, 7, rec);
        I n = ((rd() * 5) >> 15) + 0xF;
        particles_spawn(eng, pos, 5, n, 0x28, 0x19, 0x50, 0x1E, 0x3FF, 0x3F, 0x3E0, 0xFF, 1, 0x17FFF, 0, 0);
        if (kind == 0) explosion_sound(eng, e);
    } else if (kind == 7 || kind == 8) {
        // a small explosion (6, 10, 15; 0x3000, 0x200, 0x20000, 5) with sparks and smoke; 7 its sound
        explosion_spawn(eng, e, 6, 0xA, 0xF, 0x3000, 0x200, 0x20000, 5, rec);
        I n = ((rd() * 3) >> 15) + 2;
        I smoke = ((rd() * 4) >> 15) + 6;
        particles_spawn(eng, pos, 1, n, 0x1E, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke);
        if (kind == 7) explosion_sound(eng, e);
    } else if (kind == 2) {
        // a smoke emitter (0x80074A60): its particles are drawing; it stays
        U p = smoke_emitter(eng);
        if (p) {
            for (int k = 0; k < 3; k++) m.w32(p + 0x20C + 4 * k, pos[k]);
            eng.link_front(p);
        }
    } else if (kind == 4) {
        U g = explosion_spawn(eng, e, 0x12, 0x1E, 0x320, 0x40000, 0x5000, 0x140000, 3, rec);
        if (g) m.w32(g + 0x14, 0x88004450);
        explosion_sound(eng, e);
        screen_flash(eng, 1, 1, 2, 2, {0xC0, 0xC0, 0xC0}, {0x80, 0x80, 0x80});
    } else if (kind == 3) {
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
