// The mounted gun, type 0x1772 (docs/re/enemy_types.md, The mounted gun and its gunner;
// objects.md, firing): mounted_gun_spawn (0x8005E2AC) and mounted_gun_think (0x8005DA24). Its
// scripts take events from the gunner (soldier scripts) or the player; while variable 5 is set it
// fires every third tick: at the player's middle when a soldier works it, along the gun when the
// player does (variable 9). tools/sim/engine_mounted.py.
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/level.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"

namespace valor::sim {

namespace {
constexpr U MOUNTED_GUN_DRAW = 0x8005D8B0;
constexpr U G_GROUP = 0x80099EC9;

I var(Mem& m, U e, I n) { return m.s32(e + 0x4C + 4 * U(n)); }
void set_var(Mem& m, U e, I n, I v) { m.w32(e + 0x4C + 4 * U(n), S32(v)); }
}  // namespace

// 0x8005E288: the object planes.
I mounted_gun_touch(Engine& eng, U e, U o) { return sphere_vs_object_planes(eng, e, o) ? 1 : 0; }

// mounted_gun_spawn (0x8005E2AC): entity_create(3, object 0x88, the model, instance flags 0x157,
// no parts, 10 channels); a brain (head bone 13, both ranges 3000, field of view 0x200, turn
// 1.0); flags B 0x40211, A 0x03038000, mask 0x411; the record's cull counter (+0x5E), group
// 0x80099EC9, health (+0x5C), position (+0x10) and angles (+0x18) (also its old position); its
// yaw kept in variable 7 (+0x68); sphere 100 centred half up; the muzzle flash at vertex 0x38
// (flash_setup 0x8006DCAC).
U mounted_gun_spawn(Engine& eng, Record& rec, U model) {
    Mem& m = eng.m;
    U r = rec.addr;
    U e = eng.entity_create(m.s32(model + 0xFC), 0x88, model, 0, 0, 0xA, 0, true, 0x157);
    if (!e) return 0;
    U b = eng.alloc(0x124);  // brain_alloc
    m.w32(e + 0x38C, b);
    m.w32(e + 0x10, 0x40211);
    m.w32(e + 0xC, 0x03038000);
    m.w32(e + 0x14, 0x411);
    m.w32(e + 8, m.s32(model + 0xFC));
    I cv = m.s16(r + 0x5E);
    m.w32(e + 0x294, cv);
    m.w32(e + 0x290, cv);
    I g = m.u8(G_GROUP);
    m.w8(G_GROUP, g + 1);
    m.w8(e + 0x35, g);
    m.w32(e + 0x5C, 0);
    I hp = m.u8(r + 0x5C) << 16;
    m.w32(e + 0xF0, hp);
    m.w32(e + 0xF4, hp);
    for (U k = 0; k < 3; k++) {
        m.w32(e + 0x20C + 4 * k, S32(m.s16(r + 0x10 + 2 * k) << 16));
        m.w32(e + 0x264 + 4 * k, S32(m.s16(r + 0x18 + 2 * k) << 16));
        m.w32(e + 0x258 + 4 * k, S32(m.s16(r + 0x10 + 2 * k) << 16));
    }
    m.w32(e + 0x36C, MOUNTED_GUN_THINK);
    m.w32(e + 0x370, MOUNTED_GUN_DRAW);
    m.w32(e + 0x384, 0x8006A37C);
    m.w32(e + 0x37C, MOUNTED_GUN_TOUCH);
    m.w16(e + 0x2EC, 0);
    m.w32(e + 0x74, 0);
    m.w32(e + 0x70, 0);
    m.w32(e + 0x374, 0);
    m.w32(e + 0x378, 0);
    m.w32(e + 0x68, m.u32(e + 0x268));
    if (b) {
        m.w16(b + 0xAA, 0xD);
        m.w16(b + 0xB0, 0xBB8);
        m.w16(b + 0xB2, 0xBB8);
        m.w16(b + 0xB4, 0x200);
        m.w16(b + 0xB6, -1);
        m.w16(b + 0xC0, 0);
        m.w16(b + 0xC2, 0);
        m.w32(b + 0xDC, 0x10000);
    }
    m.w16(e + 0x2E8, 0x64);
    m.w16(e + 0x3C8, 0x64);
    m.w16(e + 0x2DA, -(0x64 >> 1));
    if (b) {
        m.w16(b + 0xC4, m.u16(e + 0x2EC));
        for (U o : {0xD2u, 0xD0u, 0xCEu, 0xCCu}) m.w16(b + o, 0);
        m.w16(b + 0xC6, m.u16(e + 0x2E8));
    }
    // flash_setup(object, entity, vertex 0x38): the flash's vertex, counter 0, its holder
    U o = U(m.u32(e + 0x388));
    m.w16(o, 0x38);
    m.w16(o + 2, 0);
    m.w32(o + 0x18, m.u32(e));
    m.w32(o + 0x1C, 0);
    return e;
}

// mounted_gun_think (0x8005DA24): not attached while variable 4 was: contact 0x2000; the event
// matcher, the channels with variable 8 = its yaw (and back); the player working it (variable 9)
// not done; firing: while variable 5, every third tick (variable 6 counts 2 down), a bullet
// (0x13F4, damage 5.0, vertices 0x94 -> 0x38) at the player's middle when a soldier works it
// (mask 0x08000401, flags A 0x800), the flash, a recoil jitter (variables 11..13: rand +-10,
// +-33, +-20, the last two onto the bullet's pitch and yaw), the sound 1:0xB.
void mounted_gun_think(Engine& eng, U e) {
    Mem& m = eng.m;
    if (!(m.u32(e + 0x18) & 1) && var(m, e, 4)) m.w32(e + 0x38, m.u32(e + 0x38) | 0x2000);
    VM vm(eng, e);
    vm.run_b();
    set_var(m, e, 8, m.s32(e + 0x22C));
    vm.run_a();
    m.w32(e + 0x22C, var(m, e, 8));
    if (var(m, e, 9))
        eng.note("mounted gun worked by the player not implemented");
    else if (m.u32(e + 0x18) & 1) {
        U p = eng.resolve(I(m.u32(e + 0x2C)));
        if (p) m.w32(p + 0x10, m.u32(p + 0x10) | 0x4000);
    }
    auto rd = [&]() { return eng.rand.draw(FN("mounted_gun")); };
    if (var(m, e, 5)) {
        I c = var(m, e, 6);
        if (c > 0) {
            set_var(m, e, 6, c - 1);
            for (I k : {0xB, 0xC, 0xD}) set_var(m, e, k, 0);
        } else {
            U sol = U(m.u32(G_SOLDIER));
            V3 tgt = {m.s16(sol + 0x20E), S16(S32(m.s32(sol + 0x210) - (m.s32(sol + 0x2D4) >> 1)) >> 16), m.s16(sol + 0x216)};
            U p = bullet_spawn(eng, e, 0x94, 0x38, &tgt, 0, 0, 0, 0x50000, 0, 0, 0x13F4, 0, 0, 0);
            if (p) {
                m.w32(p + 0x14, 0x08000401);
                m.w32(p + 0xC, m.u32(p + 0xC) | 0x800);
            }
            U o = U(m.u32(e + 0x388));
            m.w16(o + 2, 1);  // the muzzle flash (0x8006DCA0)
            set_var(m, e, 0xB, ((rd() * 0x14) >> 15) - 0xA);
            set_var(m, e, 0xC, ((rd() * 0x42) >> 15) - 0x21);
            set_var(m, e, 0xD, ((rd() * 0x28) >> 15) - 0x14);
            if (p) {
                m.w32(p + 0x228, S32(m.s32(p + 0x228) + (var(m, e, 0xC) << 16)));
                m.w32(p + 0x22C, S32(m.s32(p + 0x22C) + (var(m, e, 0xD) << 16)));
            }
            eng.audio->play(1, 0xB, e, 0x20000, 0, 0x10000, 0x1800, 0, 0, 2, 0);
            set_var(m, e, 6, 2);
        }
    } else {
        set_var(m, e, 6, 0);
        for (I k : {0xB, 0xC, 0xD}) set_var(m, e, k, 0);
    }
    set_var(m, e, 4, m.u32(e + 0x18) & 1);
    set_var(m, e, 0xA, var(m, e, 9));
}

}  // namespace valor::sim
