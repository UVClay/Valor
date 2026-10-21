// Soldiers and dogs (docs/re/enemies.md, soldier.md, enemy_types.md): spawning into the character
// pool, the think around the scripts (tools/sim/engine_enemy.py). The brain is brain.cpp.
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"

namespace valor::sim {

namespace {
constexpr U ENEMY_THINK = 0x80055628, DOG_THINK = 0x80054AB8;
constexpr U G_GROUP = 0x80099B30;
constexpr U G_DOG_GROUP = 0x80099B24;
constexpr U G_DOG_COUNT = 0x80099B20;
constexpr U G_LAST_DOG = 0x8009D644;
constexpr U G_EWEAPONS = 0x80097FF0;
constexpr U G_HELMET_SAVE = 0x8009810C;
constexpr U WITNESS_VICTIM = 0x80098178, WITNESS_SPOT = 0x8009817C, WITNESSES = 0x80098164, WITNESS_COUNT = 0x80098184;

U pose_pool(int i) { return U(0x800C4378 + i * 0x1308); }

I cdiv0(I a, I b) { return b == 0 ? 0 : cdiv(a, b); }

constexpr U G_CHARS_IN_USE = 0x800AB8C8;     // max of the two below: enemy_spawn refuses at 4
constexpr U G_CHAR_SLOTS_USED = 0x800AB8CC;  // only lowered (char_release with countdown 0)
constexpr U G_POSES_USED = 0x800AB8D0;       // pose buffers in use

void chars_in_use(Mem& m) { m.w32(G_CHARS_IN_USE, imax(m.s32(G_CHAR_SLOTS_USED), m.s32(G_POSES_USED))); }

int char_alloc(Engine& eng, U& a) {
    Mem& m = eng.m;
    for (int i = 0; i < 5; i++) {
        a = char_slot(i);
        if (m.u32(a + 0x2810)) {
            m.zero(a, 0x280C);
            m.w32(a + 0x280C, -0x43);
            m.w32(a + 0x2810, 0);
            chars_in_use(m);
            return i;
        }
    }
    return -1;
}

// 0x8008ED30, from model_instance_init for a character model with bones: the first free pose
// buffer of five, zeroed; the count of those in use + 1.
U pose_alloc(Engine& eng) {
    Mem& m = eng.m;
    for (int i = 0; i < 5; i++) {
        U a = pose_pool(i);
        if (m.u32(a + 0x1304)) {
            m.w32(a + 0x1300, -0x43);
            m.w32(a + 0x1304, 0);
            m.zero(a, 0x1300);
            m.w32(G_POSES_USED, m.s32(G_POSES_USED) + 1);
            chars_in_use(m);
            return a;
        }
    }
    return 0;
}

// func_8006B234(instance, entity, 2) for a character -> 0x8008ECA0: the pose buffer's countdown 2.
void pose_release(Engine& eng, U inst) {
    Mem& m = eng.m;
    U buf = U(m.u32(inst));
    for (int i = 0; i < 5; i++)
        if (pose_pool(i) == buf) {
            m.w32(buf + 0x1300, 2);
            return;
        }
}

I nbones(Mem& m, U model) {
    U tbl = U(m.u32(model + 8));
    U first = tbl ? U(m.u32(tbl + 4)) : 0;
    return first ? m.u8(first + 1) : 0;
}

void char_instance_init(Engine& eng, U e, U inst, U model, U pool) {
    Mem& m = eng.m;
    I nb = 0;
    U tbl = U(m.u32(model + 8));
    if (tbl) {
        U first = U(m.u32(tbl + 4));
        nb = first ? m.u8(first + 1) : 0;
    }
    m.w32(inst + 0x0, pool);
    m.w32(inst + 0x8, model);
    m.w16(inst + 0x3E, 0);
    m.w16(inst + 0x40, -1);
    m.w16(inst + 0x5C, -1);
    m.w16(inst + 0x5E, 0);
    m.w16(inst + 0x60, 3);
    m.w16(inst + 0x62, -1);
    m.w16(inst + 0x64, 0);
    m.w32(inst + 0x6C, 0x80);
    m.w32(inst + 0x74, tbl);
    if (tbl) {
        m.w16(inst + 0x70, m.u16(tbl));
        m.w32(inst + 0x78, tbl + 4);
    }
    m.w16(inst + 0x72, nb);
    m.w32(inst + 0x7C, m.u32(model + 0x38));
    m.w32(inst + 0x80, m.u32(model + 0x84));
    m.w32(inst + 0x84, m.u32(model + 0x9C));
    m.w32(inst + 0x90, pool);
    m.w32(inst + 0x88, pool);
    m.w32(inst + 0x8C, pool + 8 * nb);
    m.w32(inst + 0x94, pool + 16 * nb);
    for (int k = 0; k < 3; k++) m.w32(e + 0x270 + 4 * k, m.u32(model + 0xA4 + 4 * k));
    m.w32(e + 0x2D4, m.u32(model + 0xA0));
}

U character_create(Engine& eng, U model, I channels) {
    U a;
    int i = char_alloc(eng, a);
    if (i < 0) return 0;
    Mem& m = eng.m;
    I nparts = m.u16(model + 0xBE);
    eng.entity_create(m.s32(model + 0xFC), 0xBF0, 0, a, 0, 0);
    U p = a + 0x3CC;
    m.w32(a + 0x388, p);
    p += 0xBF0;
    m.w32(a + 0x100, p);
    U inst = p;
    p += 0xB0;
    m.w16(a + 0x36, nparts);
    m.w32(a + 0x304, nparts ? p : 0);
    m.copy(p, m.u32(model + 0xC0), 0x48 * nparts);
    p += U(0x48 * nparts);
    for (int ch = 0; ch < 2; ch++) {
        m.w32(a + 0x124 + 0x74 * ch, p);
        p += U(0x3C * channels);
    }
    m.w32(a + 0x1EC, channels);
    char_instance_init(eng, a, inst, model, nbones(m, model) ? pose_alloc(eng) : 0);
    return a;
}

void ev_set_var(Mem& m, U e, I v, I value) {
    if (v & 0x100) {
        I n = v & 0xFF;
        U a = U(e + 0xEC + 4 * (n >> 5));
        I bit = I(1) << (n & 31);
        m.w32(a, value ? (m.u32(a) | bit) : (m.u32(a) & ~bit));
    } else
        m.w32(e + 0x4C + 4 * v, value);
}

U soldier_spawn(Engine& eng, Record& rec, U model, U tmpl) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = character_create(eng, model, 0x28);
    if (!e) return 0;
    ev_set_var(m, e, 0x101, 0);
    U weapon = U(m.u32(0x8003C19C)), head = U(m.u32(0x8003C19C + 4)), grenade = U(m.u32(0x8003C19C + 8));
    I n = m.u32(tmpl);
    if (n >= 2) {
        weapon = U(m.u32(tmpl + 8));
        if (n >= 3 && m.u32(tmpl + 0xC) != 0x4CBF0BA5) head = U(m.u32(tmpl + 0xC));
        if (n >= 4) {
            grenade = 0x61282777;
            ev_set_var(m, e, 0x101, 1);
        }
    }
    I kind = 4;
    for (int k = 0; k < 4; k++)
        if (m.u32(G_EWEAPONS + 0x30 * k) == weapon) {
            kind = k;
            break;
        }
    kind &= 0xFF;
    U obj = U(m.u32(e + 0x388));
    U b = eng.alloc(0x124);
    m.w32(e + 0x38C, b);
    m.w32(e + 0xC, 0x1300903C);
    m.w32(e + 0x14, 0x88204401);
    m.w32(e + 8, SOLDIER);
    m.w32(e + 0x10, 0x68000);
    I cull = m.s16(r + 0x68);
    m.w32(e + 0x294, cull);
    m.w32(e + 0x290, cull);
    m.w16(e + 0x2DA, -0x78);
    m.w16(e + 0x2E8, 0x96);
    m.w16(e + 0x2EC, 0x3C);
    m.w16(e + 0x3C8, 0x3C);
    I g = m.u8(G_GROUP);
    m.w8(G_GROUP, g + 1);
    m.w8(e + 0x35, g);
    I hp = m.u8(r + 0x61) << 16;
    m.w32(e + 0xF4, hp);
    m.w32(e + 0xF0, hp);
    m.w32(obj + 0x18, 0);
    m.w16(e + 0x2EA, 0);
    ev_set_var(m, e, 0xF, kind);
    ev_set_var(m, e, 0xD, cdiv0(m.s32(e + 0xF4) * 100, m.s32(e + 0xF0)));
    ev_set_var(m, e, 0x22, m.s16(r + 0x6E));
    ev_set_var(m, e, 0x23, 0);
    ev_set_var(m, e, 0x10, m.u8(r + 0x5E));
    I d5c = m.u8(r + 0x5C);
    ev_set_var(m, e, 0x24, d5c == 0 ? 10 : d5c == 1 ? 20 : d5c == 2 ? 1000 : 100);
    I d5b = m.u8(r + 0x5B);
    ev_set_var(m, e, 8, d5b == 0 ? 33 : d5b == 2 ? 100 : 67);
    m.w32(e + 0x36C, ENEMY_THINK);
    m.w32(e + 0x370, 0x8005608C);
    m.w32(e + 0x374, 0x80055864);
    m.w32(e + 0x378, 0x80056250);
    m.w32(e + 0x384, 0x80056288);
    m.w32(e + 0x37C, 0x8005291C);
    m.w16(b + 0xAA, 0xD);
    m.w16(b + 0xB0, m.u16(r + 0x6C));
    I hear = m.s16(r + 0x70);
    m.w16(b + 0xB2, hear >= 10 ? hear : 0xAF0);
    hear = m.s16(b + 0xB2);
    ev_set_var(m, e, 5, hear);
    ev_set_var(m, e, 6, (hear & 0xFFFF) >> 1);
    ev_set_var(m, e, 7, S16(cdiv0(hear, 3)));
    ev_set_var(m, e, 0x26, m.u8(r + 0x65));
    I v = m.u8(r + 0x66);
    ev_set_var(m, e, 0xB, v == 0x1E ? 1 : v);
    m.w16(b + 0xC8, m.u8(r + 0x63));
    m.w16(b + 0xB6, -1);
    m.w16(b + 0xBE, 1);
    m.w16(b + 0xBA, 2);
    m.w16(b + 0xC0, 0);
    m.w16(b + 0xC2, 0);
    m.w32(b + 0xD8, 0);
    m.w16(b + 0xB4, m.s16(r + 0x6A) * 11);
    m.w16(b + 0xC4, m.u16(e + 0x2EC));
    m.w16(b + 0xC6, m.u16(e + 0x2E8));
    m.w32(b + 0xDC, 0x10000);
    for (U o : {0xCCu, 0xCEu, 0xD0u, 0xD2u}) m.w16(b + o, 0x64);
    m.w16(b + 0xCA, m.u16(e + 0x3C8));
    attachments_init(eng, e, {weapon, head, grenade, 0});
    for (U o : {0x10u, 0x11u, 0x12u, 0x14u}) m.w8(obj + o, 0);
    U s0 = obj + 0x18;
    m.w32(s0 + 4, 0x41);
    m.w32(s0 + 0xC, 0x15);
    if (kind == 0)
        m.w16(s0 + 0x18, 0xF), m.w16(s0 + 0x1A, 0), m.w16(s0 + 0x1C, 0xF);
    else if (kind == 2)
        m.w16(s0 + 0x18, 0), m.w16(s0 + 0x1A, 0), m.w16(s0 + 0x1C, 0x32);
    else if (kind == 1 || kind == 3)
        m.w16(s0 + 0x18, 5), m.w16(s0 + 0x1A, 0), m.w16(s0 + 0x1C, 0x19);
    m.w32(s0 + 0x150, 0xD);
    m.w16(s0 + 0x15C, -7);
    m.w16(s0 + 0x15E, 1);
    m.w32(s0 + 0x148, head != 0xFFFFFFFF ? 1 : 0x20);
    m.w16(s0 + 0x160, 0x24);
    m.w32(G_HELMET_SAVE, head == 0x8A522920 ? 0xAA1714FE : 0);
    m.w32(s0 + 0x294, 0x11);
    m.w32(G_SOLDIER_COUNT, m.s32(G_SOLDIER_COUNT) + 1);
    start_scripts(eng, e);
    return e;
}

U dog_spawn(Engine& eng, Record& rec, U model) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = character_create(eng, model, 0x14);
    if (!e) return 0;
    m.w32(G_LAST_DOG, e);
    U b = eng.alloc(0x124);
    m.w32(e + 0x38C, b);
    place(eng, e, rec);
    m.w32(e + 0x1C, 0);
    eng.matrix(e);
    m.w32(e + 8, DOG);
    m.w32(e + 0x14, 0x88204401);
    m.w32(e + 0xC, 0x1300906C);
    m.w32(e + 0x10, 0x68000);
    I cull = m.s16(r + 0x68);
    m.w32(e + 0x294, cull);
    m.w32(e + 0x290, cull);
    I hp = m.u8(r + 0x61);
    m.w16(e + 0x2EC, 0x50);
    m.w16(e + 0x2DA, -0x32);
    m.w16(e + 0x3C8, 0x50);
    m.w32(e + 0xF4, hp << 16);
    m.w32(e + 0xF0, hp << 16);
    m.w16(e + 0x2E8, 0x7D);
    m.w32(e + 0x36C, DOG_THINK);
    m.w32(e + 0x370, 0x80054B80);
    m.w32(e + 0x374, 0x80054C24);
    m.w32(e + 0x378, 0x80054E14);
    m.w32(e + 0x384, 0x80054D70);
    m.w32(e + 0x37C, 0x8005291C);
    m.w8(e + 0x35, m.u8(G_DOG_GROUP));
    for (int k = 0; k < 3; k++) m.w16(e + 0x3A4 + 2 * k, m.u16(r + 0x10 + 2 * k));
    m.w32(e + 0x4C + 4 * 0x1F, m.s16(r + 0x34));
    ev_set_var(m, e, 0xD, hp ? cdiv0(hp * 0x190000 * 4, hp << 16) : 0);
    ev_set_var(m, e, 0x1C, m.s16(r + 0x6E));
    ev_set_var(m, e, 0x1D, m.u8(r + 0x5E));
    m.w16(b + 0xAA, 0xD);
    m.w16(b + 0xB0, m.u16(r + 0x6C));
    I hear = m.s16(r + 0x70);
    m.w16(b + 0xB2, hear >= 10 ? hear : 0xAF0);
    hear = m.s16(b + 0xB2);
    ev_set_var(m, e, 5, hear);
    ev_set_var(m, e, 6, (hear & 0xFFFF) >> 1);
    ev_set_var(m, e, 7, S16(cdiv0(hear, 3)));
    ev_set_var(m, e, 0x1F, m.s16(r + 0x34));
    m.w16(b + 0xC0, 0);
    m.w16(b + 0xC2, 0);
    m.w32(b + 0xD8, 0);
    m.w16(b + 0xB4, m.s16(r + 0x6A) * 11);
    m.w16(b + 0xC4, m.u16(e + 0x2EC));
    m.w32(G_SOLDIER_COUNT, m.s32(G_SOLDIER_COUNT) + 1);
    m.w32(G_DOG_COUNT, m.s32(G_DOG_COUNT) + 1);
    m.w16(b + 0xC6, m.u16(e + 0x2E8));
    m.w32(b + 0xDC, 0x10000);
    for (U o : {0xCCu, 0xCEu, 0xD0u, 0xD2u}) m.w16(b + o, 0x64);
    m.w16(b + 0xCA, m.u16(e + 0x3C8));
    start_scripts(eng, e);
    return e;
}

void witness_report(Engine& eng, U e) {
    Mem& m = eng.m;
    if (!(m.u32(e + 0x38) & 0x60)) return;
    m.w32(WITNESS_VICTIM, e);
    for (int k = 0; k < 3; k++) m.w16(WITNESS_SPOT + 2 * k, S16(m.s32(e + 0x20C + 4 * k) >> 16));
    I n = 0;
    for (U o : eng.entities()) {
        if (n >= 5) break;
        I t = m.s32(o + 8);
        if (o != e && (t == SOLDIER || t == DOG) && !(m.u32(o + 0xC) & 0x20080000)) {
            m.w32(WITNESSES + 4 * n, o);
            n++;
        }
    }
    m.w32(WITNESS_COUNT, n);
}

void scripts(Engine& eng, U e, I hv) {
    Mem& m = eng.m;
    VM vm(eng, e);
    vm.run_b();
    m.w32(e + 0x4C + 4 * hv, m.s32(e + 0x22C));
    vm.run_a();
    m.w32(e + 0x22C, m.s32(e + 0x4C + 4 * hv));
}

}  // namespace

// char_pool_update (0x8008EAC8), at the start of every frame: per index, the character slot then
// the pose buffer, each in use counted down when positive and freed at 0.
void char_pool_update(Engine& eng) {
    Mem& m = eng.m;
    for (int i = 0; i < 5; i++) {
        U a = char_slot(i), q = pose_pool(i);
        if (!m.u32(a + 0x2810)) {
            I c = m.s32(a + 0x280C);
            if (c > 0) {
                c--;
                m.w32(a + 0x280C, c);
            }
            if (c == 0) {
                m.w32(a + 0x2810, 1);
                chars_in_use(m);
            }
        }
        if (!m.u32(q + 0x1304)) {
            I c = m.s32(q + 0x1300);
            if (c > 0) {
                c--;
                m.w32(q + 0x1300, c);
            }
            if (c == 0) {
                m.w32(G_POSES_USED, m.s32(G_POSES_USED) - 1);
                chars_in_use(m);
                m.w32(q + 0x1304, 1);
            }
        }
    }
}

U enemy_spawn(Engine& eng, Record& rec) {
    Mem& m = eng.m;
    U tmpl = U(m.u32(rec.addr + 0x74));
    U model = model_by_hash(eng, U(m.u32(tmpl + 4)));
    if (!model) {
        eng.note("soldier model missing");
        return 0;
    }
    I cap = m.s32(G_SOLDIER_CAP), n = m.s32(G_SOLDIER_COUNT);
    if (m.s32(model + 0xFC) == DOG) {
        // dog_spawn (0x80054E4C): a cap of 0 or the count at the cap refuses
        if (cap == 0 || n >= cap) return 0;
        return dog_spawn(eng, rec, model);
    }
    // enemy_spawn (0x800563F0): with a cap above 0 the count at the cap refuses; then 4 or more
    // characters in use (0x800AB8C8); then a cap of 0
    if ((cap > 0 && n >= cap) || m.s32(G_CHARS_IN_USE) >= 4 || cap == 0) return 0;
    return soldier_spawn(eng, rec, model, tmpl);
}

void start_scripts(Engine& eng, U e) {
    Mem& m = eng.m;
    U inst = U(m.u32(e + 0x100));
    if (inst && m.u32(inst + 8)) {
        eng.matrix(e);
        I s = m.s16(m.u32(inst + 8) + 0x94);
        if (s >= 0) {
            VM vm(eng, e);
            vm.script_start(s, 1, 0);
            vm.run_a();
        }
    }
}

void enemy_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U obj = U(m.u32(e + 0x388));
    if (m.s16(e + 0xF6) <= 0) {
        m.w32(e + 0xF4, 0);
        U holder = eng.resolve(m.u32(e + 0x30));
        if (holder && m.u32(holder + 0x30) == m.u32(e)) m.w32(holder + 0x30, 0);
    }
    ranges_update(eng, e);
    m.w8(obj + 0x11, m.s32(e + 0x4C + 4 * 0x16));
    enemy_weapon(eng, e);
    m.w32(e + 0x4C + 4 * 0x16, m.u8(obj + 0x11));
    attachments_update(eng, e);
    if (m.s32(e + 0x4C + 4 * 0x1A)) attachments_op(eng, e, m.u32(e + 0x4C + 4 * 0x1A), m.s32(e + 0x4C + 4 * 0x1B));
    witness_report(eng, e);
    scripts(eng, e, 0x13);
    if (m.s32(e + 0x4C + 4 * 0x1C)) hit_reaction(eng, e, 0x1C, m.s32(e + 0x4C + 4 * 0xC));
    m.w32(obj + 4, puffs_update(m, obj + 0x3E8));
    m.w32(obj, drops_update(eng, e, obj + 0x49C));
}

void dog_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U obj = U(m.u32(e + 0x388));
    witness_report(eng, e);
    scripts(eng, e, 0x0E);
    if (m.s32(e + 0x4C + 4 * 0x13)) hit_reaction(eng, e, 0x13, m.s32(e + 0x4C + 4 * 0xC));
    m.w32(obj + 4, puffs_update(m, obj + 0x3E8));
    m.w32(obj, drops_update(eng, e, obj + 0x49C));
}

void witness_check(Engine& eng) {
    Mem& m = eng.m;
    I n = m.s32(WITNESS_COUNT);
    if (n <= 0) return;
    n--;
    m.w32(WITNESS_COUNT, n);
    U w = U(m.u32(WITNESSES + 4 * n)), v = U(m.u32(WITNESS_VICTIM));
    if (!w || !v) return;
    if (sees(eng, w, v, true)) m.w32(w + 0x3C, m.u32(w + 0x3C) | 0x10000);
}

void enemy_destroy(Engine& eng, U e) {
    Mem& m = eng.m;
    m.w32(G_SOLDIER_COUNT, imax(m.s32(G_SOLDIER_COUNT) - 1, 0));
    if (m.u32(e + 0x100)) pose_release(eng, U(m.u32(e + 0x100)));
    U b = U(m.u32(e + 0x38C));
    if (b) eng.free(b);
    m.w32(e + 0x280C, 2);
}

void dog_destroy(Engine& eng, U e) {
    Mem& m = eng.m;
    m.w32(G_SOLDIER_COUNT, imax(m.s32(G_SOLDIER_COUNT) - 1, 0));
    m.w32(G_DOG_COUNT, imax(m.s32(G_DOG_COUNT) - 1, 0));
    if (m.u32(e + 0x100)) pose_release(eng, U(m.u32(e + 0x100)));
    U b = U(m.u32(e + 0x38C));
    if (b) eng.free(b);
    m.w32(e + 0x280C, 2);
}

}  // namespace valor::sim
