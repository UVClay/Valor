// The alarm (docs/re/audio.md, Pickups, interface, alarm; enemies.md op 49): raised by the
// soldiers' op 49 (op_alarm, 0x8005C710) or the level's level_set(4, id) (alarm_from_level,
// 0x8005F068); an entity (alarm_spawn, 0x8005EA54) that re-triggers one sound, timed by the vblank
// counter (VSync(-1)), until 1801 thinks or until its node's record is switched off.
// tools/sim/engine_alarm.py.
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/level.h"
#include "valor/sim/objects.h"

namespace valor::sim {

namespace {
constexpr U G_ALARM_ENT = 0x80097FE4;
constexpr U G_PARAMS = 0x800939F4;  // +0 mission, +4 level
constexpr U G_CHEATS_ON = 0x800386A4;
constexpr U ALARM_DESTROY = 0x8005F008;

// The node (0x34 bytes) and its link (+0x2C) into the world object's table +0x14 (8 bytes: +0
// flags, +4 the record).
std::pair<U, U> node_rec(Mem& m, I node) {
    U wo = U(m.u32(m.u32(0x8009D648) + 0x388));
    U n = U(m.u32(wo + 8)) + 0x34 * U(node);
    return {n, U(m.u32(wo + 0x14)) + 8 * U(m.s32(n + 0x2C))};
}

bool switched_off(Mem& m, I node) {
    auto [n, ent] = node_rec(m, node);
    return m.s16(U(m.u32(ent + 4)) + 0x34) == m.s16(n + 4) && (m.u32(ent) & 2);
}

// alarm_spawn: an entity of type 0x20 with a 0x2C-byte object (+0 node, +4 thinks, +8 the next
// vblank, +0xC delay, +0x10 random delay, +0x14 volume, +0x18 its spread, +0x1C pitch, +0x20 its
// spread, +0x24 sound, +0x26 voice (-1), +0x27 bank, +0x28 bit 0x80 waiting), not positional
// (flags A 0x40000000), flags B | 0x200; the pitch by mission: 1.0 in 4, 8, 9 and 3 (0.5 in 3:1),
// 0.5 in 2 and 5, else the caller's; the start chime 0:0x13. (Cheat 4: a random sound of 25, not
// done.)
U alarm_spawn(Engine& eng, I sound, I bank, I delay, I rnd, I volume, I vspread, I pitch, I pspread, I node) {
    Mem& m = eng.m;
    U e = eng.entity_create(0x20, 0x2C);
    if (!e) return 0;
    m.w32(e + 8, 0x20);
    m.w32(e + 0x36C, ALARM_THINK);
    m.w32(e + 0x384, ALARM_DESTROY);
    for (U k : {0x14u, 0x370u, 0x374u, 0x378u}) m.w32(e + k, 0);
    m.w32(e + 0x10, m.u32(e + 0x10) | 0x200);
    U o = U(m.u32(e + 0x388));
    if (m.u32(G_CHEATS_ON) & 4) eng.note("alarm cheat 4 (random sounds) not implemented");
    I mission = m.s32(G_PARAMS), level = m.s32(G_PARAMS + 4);
    if (mission == 4 || mission == 8 || mission == 9)
        pitch = 0x10000;
    else if (mission == 3)
        pitch = level == 1 ? 0x8000 : 0x10000;
    else if (mission == 2 || mission == 5)
        pitch = 0x8000;
    m.w8(o + 0x27, bank);
    m.w16(o + 0x24, sound);
    m.w32(o + 8, 0);
    m.w32(o + 0xC, delay);
    m.w32(o + 0x10, rnd);
    m.w32(o + 0x14, volume);
    m.w32(o + 0x1C, pitch);
    m.w32(o + 0x18, vspread);
    m.w8(o + 0x28, 0);
    m.w32(o + 0x20, pspread);
    m.w8(o + 0x26, 0xFF);
    m.w32(o + 4, 0);
    m.w32(o, node);
    m.w32(e + 0xC, m.u32(e + 0xC) | 0x40000000);
    eng.audio->play(0, 0x13, e, 0x10000, 0, 0x10000, 0x3333, 0, 0, 0, 0);
    return e;
}

// After a spawn: g_alarm, the entity at the front, every living soldier (but the raiser) gets
// +0x3C 0x100, and every entity of the node's record is sent message 0x2B.
void raised(Engine& eng, U a, I node, U src) {
    Mem& m = eng.m;
    m.w32(G_ALARM, node);
    eng.link_front(a);
    m.w32(G_ALARM_ENT, a);
    for (U o : m.entities())
        if (m.s32(o + 8) == 0x1402 && !(m.u32(o + 0xC) & 0x20080000) && o != src) m.w32(o + 0x3C, m.u32(o + 0x3C) | 0x100);
    U rec = node_rec(m, node).second;
    for (U o : m.entities())
        if (U(m.u32(o + 0x1C)) == rec) {
            auto it = eng.ents.find(o);
            eng.logic->send(0x2B, it == eng.ents.end() ? nullptr : it->second, 0, nullptr, 0, "alarm");
        }
}
}  // namespace

// Op 49 (0x8005C710): when no alarm is up: bank -1 is the model's (+0xC8); delay 10.
void op_alarm(Engine& eng, U e, I node, I sound, I pitch, I pspread, I volume, I vspread, I bank) {
    Mem& m = eng.m;
    if (m.s32(G_ALARM) >= 0) return;
    if (S32(bank) == -1) {
        U mdl = U(m.u32(U(m.u32(e + 0x100)) + 8));
        bank = mdl ? m.u16(mdl + 0xC8) : 0;
    }
    U a = alarm_spawn(eng, sound & 0xFFFF, bank & 0xFF, 0xA, 0, volume, vspread, pitch, pspread, node);
    if (a) raised(eng, a, node, e);
}

// level_set(4, id): the node whose +4 is id (the last such); if its record isn't switched off
// (record +0x34 == node +4 with the table flag 2): with an alarm up, it is ended (its thinks set
// to 1801); else 0:0x2C, delay 10, volume 0x20000, pitch 0.5 (by mission).
void alarm_from_level(Engine& eng, I nid) {
    Mem& m = eng.m;
    U wo = U(m.u32(m.u32(0x8009D648) + 0x388));
    I node = -1;
    for (I i = 0; i < m.s32(wo + 0xC); i++)
        if (m.s32(U(m.u32(wo + 8)) + 0x34 * U(i) + 4) == nid) node = i;
    if (node < 0 || switched_off(m, node)) return;
    if (m.s32(G_ALARM) != -1) {
        U a = U(m.u32(G_ALARM_ENT));
        if (a) m.w32(U(m.u32(a + 0x388)) + 4, 0x709);
        return;
    }
    U a = alarm_spawn(eng, 0x2C, 0, 0xA, 0, 0x20000, 0, 0x8000, 0, node);
    if (a) raised(eng, a, node, 0);
}

// alarm_think: thinks + 1; at 1801, or with its node's record switched off, removed. Not waiting:
// when its voice is free (none yet, or its status not 1) the next play is due now (a first sound)
// or after the delay (+ rand % random); then, waiting, when the vblank counter reaches it, the
// sound plays again.
void alarm_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    I vb = eng.vblank;
    I t = m.s32(o + 4) + 1;
    m.w32(o + 4, t);
    if (t >= 0x709 || switched_off(m, m.s32(o))) {
        eng.remove(e, "alarm over");
        return;
    }
    I f = m.u8(o + 0x28);
    if (!(f & 0x80)) {
        I v = m.s8(o + 0x26);
        if (v == -1 || m.u8(0x800910D0 + U(v)) != 1) {
            m.w32(o + 8, vb);
            m.w8(o + 0x28, f | 0x80);
            if (v != -1) {
                m.w32(o + 8, vb + m.s32(o + 0xC));
                if (m.s32(o + 0x10) > 0) m.w32(o + 8, m.s32(o + 8) + pmod(eng.rand.draw({ALARM_THINK}), m.s32(o + 0x10)));
            }
        }
    }
    if ((m.u8(o + 0x28) & 0x80) && vb >= m.s32(o + 8)) {
        I v = eng.audio->play(m.u8(o + 0x27), m.u16(o + 0x24), e, m.s32(o + 0x14), m.s32(o + 0x18), m.s32(o + 0x1C), m.s32(o + 0x20), 0, 0, 0, 0);
        m.w8(o + 0x26, v & 0xFF);
        m.w8(o + 0x28, m.u8(o + 0x28) ^ 0x80);
    }
}

// 0x8005F008: its voice stopped, no alarm up, the think cleared (then 0x8006A318's frees).
void alarm_destroy(Engine& eng, U e) {
    Mem& m = eng.m;
    I v = m.s8(U(m.u32(e + 0x388)) + 0x26);
    if (v > 0) eng.audio->stop(v);
    m.w32(G_ALARM, -1);
    m.w32(G_ALARM_ENT, 0);
    m.w32(e + 0x36C, 0);
}

}  // namespace valor::sim
