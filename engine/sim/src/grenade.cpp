// Grenades and explosions (docs/re/objects.md, Grenades, Explosions): the player's throw, the
// grenade, what soldiers do to it, its direct hit; explosion_spawn, its think with the fireballs'
// and smoke puffs' particle systems, the first think's burst, the light, the sound
// (tools/sim/engine_grenade.py, engine_explosion.py). The flight, bounces and rolling are the
// projectile's (projectile.cpp); the explosion's damage is the contact pass's.
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"
#include "valor/sim/simgte.h"

#include <algorithm>

namespace valor::sim {

namespace {
constexpr U G_STAT_SHOTS = 0x80039CA8;
constexpr U G_CHEAT_FLAGS = 0x800386A4;
constexpr U G_WEAPONS = 0x80093640;
constexpr U GRENADE_MODEL = 0x55A51DAB, STICK_GRENADE_MODEL = 0x61282777;

constexpr U EXPLOSION_DRAW = 0x80070F24;
constexpr U EXPLOSION_DESTROY = 0x8007192C;
constexpr U DAMAGE_FROM_HIT = 0x8004C1B4;
constexpr U BURST_DRAW = 0x8006FBF0;
constexpr U NO_BRAIN = 0x8006A318;
constexpr U G_LIGHT = 0x800AA188;  // r, g, b, +4 radius, +8 radius^2
constexpr U G_LIGHT_STEP = 0x8009E278;
constexpr U G_LIGHT_ON = 0x800AA160;

I grenade_rd(Engine& eng) { return eng.rand.draw(FN("grenade")); }
I explosion_rd(Engine& eng) { return eng.rand.draw(FN("explosion")); }

// 0x8005788C(e, 0x50, -1): the nearest character, alive and not removed, by dist_approx, less
// than 1000 apart in y.
U near_soldier(Engine& eng, U e) {
    Mem& m = eng.m;
    I best = 0x7FFF;
    U near = 0;
    for (U o : m.entities()) {
        if (!(m.u32(o + 0xC) & 0x50) || o == e || (m.u32(o + 0xC) & 0x20080000)) continue;
        I a[3];
        for (int k = 0; k < 3; k++) a[k] = iabs(S16((m.s32(e + 0x20C + 4 * k) - m.s32(o + 0x20C + 4 * k)) >> 16));
        I dy = S16((m.s32(e + 0x210) - m.s32(o + 0x210)) >> 16);
        std::sort(a, a + 3);
        I t = (a[2] + ((a[0] + a[1]) >> 2)) & 0xFFFF;
        if (t < (best & 0xFFFF) && iabs(dy) < 0x3E8) near = o, best = t;
    }
    return near;
}

// throw_weapon after grenade_spawn: the grenade's type and mask, its start pulled within the
// thrower's reach (engine_grenade.py start_within_reach).
void start_within_reach(Engine& eng, U e, U g, U inv) {
    Mem& m = eng.m;
    m.w32(g + 8, 0x13F3);
    m.w32(g + 0x14, 0x08000451);
    m.w16(g + 0x3CA, m.u8(inv + 0x55));
    m.w32(g + 0xC, m.u32(g + 0xC) | 0x80000000);
    I floor = m.s32(e + 0x208);
    if (floor < m.s32(g + 0x210)) {
        m.w32(g + 0x210, S32(floor - 0x200000));
        if (m.s32(g + 0x21C) > 0x370000) m.w32(g + 0x21C, 0x370000);
    }
    I fwd[3] = {m.s16(g + 0x344), m.s16(g + 0x34A), m.s16(g + 0x350)};
    I dx = iabs(S16(m.s16(g + 0x20E) - m.s16(e + 0x20E)));
    I dz = iabs(S16(m.s16(g + 0x216) - m.s16(e + 0x216)));
    I a = dx < dz ? dz : dx, b = dx < dz ? dx : dz;
    I t = a + (b >> 2);
    I end[3], from[3], to[3];
    for (int k = 0; k < 3; k++) {
        end[k] = S32(m.s32(g + 0x20C + 4 * k) + (((-fwd[k] * t) >> 12) << 16));
        from[k] = m.s32(g + 0x20C + 4 * k) >> 16;
        to[k] = end[k] >> 16;
    }
    I point[3], normal[3];
    if (eng.world.sweep(from, to, point, normal)) {
        for (int k = 0; k < 3; k++) m.w32(g + 0x20C + 4 * k, S32((point[k] + ((fwd[k] * -5) >> 10)) << 16));
        if (floor < m.s32(g + 0x210)) m.w32(g + 0x210, S32(floor - 0x200000));
    }
    for (int k = 0; k < 3; k++) m.w32(g + 0x258 + 4 * k, end[k]);
    V3 u = vnormal(eng.gte, {S16((m.s32(g + 0x20C) - end[0]) >> 16), S16((m.s32(g + 0x210) - end[1]) >> 16),
                             S16((m.s32(g + 0x214) - end[2]) >> 16)});
    I best = 0x3B9AC9FF;
    U near = 0;
    for (U o : m.entities()) {
        I fa = m.u32(o + 0xC);
        if ((fa & 0x20080000) || (m.u32(o + 0x10) & 6) || !(m.u32(g + 0x14) & fa) || o == g || o == e || (fa & 0x08000000)) continue;
        I d;
        if (segment_sphere(m, g, o, u, d) && d < best) best = d, near = o;
    }
    if (near) {
        for (int k = 0; k < 3; k++) m.w32(g + 0x20C + 4 * k, end[k]);
        eng.matrix(g);
        entity_bounds(eng, g);
    }
}

// explosion_spawn without a contact normal: the source's yaw turned round, rsin and rcos scaled
// to (x 40/65536, 0, z 5/8192).
void normal_from_angle(Engine& eng, U e, I a) {
    Mem& m = eng.m;
    I sn = eng.gte.rsin(int32_t(a)), cs = eng.gte.rcos(int32_t(a));
    I v = S32(sn << 16) + ((sn & 0x8000) ? 0xFFF : 0);
    I w = S32(cs << 16) + ((cs & 0x8000) ? 0xFFF : 0);
    m.w16(e + 0x364, S16(((v >> 12) * 0x28) >> 16));
    m.w16(e + 0x366, 0);
    m.w16(e + 0x368, S16(((w >> 12) * 5) >> 13));
}

void sys_header(Mem& m, U s, I n, U recs, I scale, I drag, I grav, const I cols[8]) {
    m.w32(s, 1);
    m.w16(s + 4, n);
    m.w16(s + 6, n);
    m.w32(s + 0x28, recs);
    m.w32(s + 0xC, scale);
    m.w32(s + 0x10, drag);
    m.w32(s + 0x14, grav);
    for (int k = 0; k < 8; k++) m.w16(s + 0x18 + 2 * k, cols[k]);
}

// A particle at a random point of the cube +-spread around the explosion; no velocity.
void place(Engine& eng, U e, U r, I spread) {
    Mem& m = eng.m;
    for (int k = 0; k < 3; k++) {
        I c = m.s32(e + 0x20C + 4 * k);
        if (spread * 2)
            c = S32(c + ((-spread + ((explosion_rd(eng) * spread * 2) >> 15)) << 16));
        else
            c = S32(c - spread * 0x10000);
        m.w32(r + 0x24 + 4 * k, c);
        m.w32(r + 0x30 + 4 * k, 0);
    }
}

// 0x800712A0: one particle system's tick (engine_explosion.py particles_update).
void particles_update(Engine& eng, U s) {
    Mem& m = eng.m;
    m.w32(s, 2);
    U recs = U(m.u32(s + 0x28));
    for (I i = 0; i < m.s16(s + 4); i++) {
        U r = recs + 0x3C * i;
        if (m.s16(r + 6)) {
            m.w16(r + 6, m.u16(r + 6) - 1);
            m.w32(s, 1);
            continue;
        }
        I g = m.s32(s + 0x14);
        if (g) m.w32(r + 0x34, S32(m.s32(r + 0x34) + g));
        m.w32(r + 0x24, S32(m.s32(r + 0x24) + m.s32(r + 0x30)));
        m.w32(r + 0x2C, S32(m.s32(r + 0x2C) + m.s32(r + 0x38)));
        m.w32(r + 0x28, S32(m.s32(r + 0x28) + m.s32(r + 0x34)));
        I drag = m.s32(s + 0x10);
        for (U o : {0x38u, 0x30u}) {
            I v = m.s32(r + o);
            m.w32(r + o, drag < iabs(v) ? (v > 0 ? v - drag : v + drag) : 0);
        }
        m.w16(r + 0xA, m.u16(r + 0xA) + m.u16(r + 8));
        I st = m.u8(r);
        I fire = m.u16(r + 4) & 1;
        if (st == 3) {
            I size = S32(m.s32(r + 0x20) + m.s32(r + 0x10));
            m.w32(r + 0x20, size);
            if (size >= m.s32(r + 0x18)) {
                m.w8(r, 4);
                m.w32(r + 0x20, m.u32(r + 0x18));
            }
            m.w32(s, 1);
        } else if (st == 4) {
            I h = (m.u8(r + 2) - 1) & 0xFF;
            m.w8(r + 2, h);
            if (!h) {
                m.w8(r, 5);
                if (fire) {
                    m.w16(s + 6, m.u16(s + 6) - 1);
                    m.w16(r + 0xC, m.u16(r + 0xC) + ((eng.rand.draw({0x800712A0}) * 0x14) >> 15) - 0xA);
                    m.w16(r + 0xE, m.u16(r + 0xE) + ((eng.rand.draw({0x800712A0}) * 0x14) >> 15) - 0xA);
                }
            }
            m.w32(s, 1);
        } else if (st == 5) {
            I size = S32(m.s32(r + 0x20) - m.s32(r + 0x1C));
            m.w32(r + 0x20, size);
            if (size <= 0) {
                m.w8(r, 6);
                m.w32(r + 0x20, 0);
            }
            if (fire) m.w32(r + 0x34, S32(m.s32(r + 0x34) - 0x8000));
            m.w32(s, 1);
        } else if (fire && st != 6)
            eng.note("explosion particle in an unknown state");
    }
}

// explosion_burst (0x80070DEC) -> 0x800708F0: an entity of type 0x23 with 5 pairs of sparks.
U burst(Engine& eng, U e) {
    Mem& m = eng.m;
    U p = eng.entity_create(0x23, 5 * 0x48 + 0x30);
    U o = U(m.u32(p + 0x388));
    m.w32(o + 4, 3);
    m.w32(o, 10);
    m.w32(o + 8, 2);
    m.w32(o + 0x2C, o + 0x30);
    m.w32(p + 8, 0x23);
    m.w32(p + 0x10, 0x203);
    m.w32(p + 0x36C, BURST_THINK);
    m.w32(p + 0x370, BURST_DRAW);
    for (U k : {0xCu, 0x374u, 0x378u, 0x228u, 0x22Cu, 0x230u, 0x284u}) m.w32(p + k, 0);
    m.w32(p + 0x384, NO_BRAIN);
    I pos[3] = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
    for (int k = 0; k < 3; k++) {
        m.w32(p + 0x20C + 4 * k, pos[k]);
        m.w32(p + 0x270 + 4 * k, 0x10000);
    }
    for (int i = 0; i < 5; i++) {
        U bases[2] = {U(o + 0x30 + 0x24 * i), U(o + 0x30 + 0x24 * (i + 5))};
        for (int j = 0; j < 2; j++) {
            I r = eng.rand.draw({0x800708F0});
            for (int k = 0; k < 3; k++) m.w32(bases[j] + 4 + 4 * k, S32(pos[k] + ((((r >> (4 * k)) & 0xF) - 8) << 16)));
            eng.rand.draw({0x800708F0});
            eng.rand.draw({0x800708F0});
            m.w32(bases[j], j + 1);
        }
    }
    eng.link_front(p);
    return p;
}

}  // namespace

I dist2d_approx(Mem& m, U a, U b) {
    I dx = iabs(S32(m.s32(b + 0x20C) - m.s32(a + 0x20C)));
    I dz = iabs(S32(m.s32(b + 0x214) - m.s32(a + 0x214)));
    return (dx >> 16) + (dz >> 16) - (imin(dx, dz) >> 17);
}

// grenade_spawn (0x80045808): engine_grenade.py grenade_spawn.
U grenade_spawn(Engine& eng, U owner, U model_hash, I bone_a, I bone_b, I fuse, I speed, U off_a, U off_b) {
    Mem& m = eng.m;
    U model = model_by_hash(eng, model_hash);
    U p = projectile_create(eng, owner, 0x13F3, model ? I(model) : -1, S32(speed << 16), bone_a, bone_b, nullptr, 0x20, 0xA0000, 0x96,
                            0, 0, off_a, off_b, 0, 0, 0, 0);
    if (!p) return 0;
    m.w16(p + 0x2EC, 0x28);
    m.w32(p + 0x2D4, 0x280000);
    m.w32(p + 0x37C, BULLET_TOUCH);
    m.w32(p + 0x374, GRENADE_HIT_PART);
    U o = U(m.u32(p + 0x388));
    m.w32(p + 0xC, m.u32(p + 0xC) | 0x040000A0);
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x40);
    m.w16(o + 0x38, 0x4349);
    m.w8(o + 0x41, fuse);
    m.w32(o + 0x34, 0x30000);
    m.w32(o + 0x68, S32((S32(grenade_rd(eng) << 12) >> 15) << 16));
    m.w32(o + 0x6C, 0);
    m.w32(o + 0x70, 0);
    m.w32(o + 0x74, S32((((grenade_rd(eng) * 0x28A) >> 15) + 0xC8) << 16));
    m.w32(o + 0x78, S32((((grenade_rd(eng) * 0x1E) >> 15) + 0xA) << 16));
    m.w32(o + 0x7C, 0);
    m.w8(o + 0x42, 0x78);
    m.w32(p + 0x230, 0);
    // RotMatrixX of the pitch applied to (0, 0, speed): vertical +0x21C, forward +0x200
    M3 r = rot_yxz3(m.s16(p + 0x22A), 0, 0);
    V3 mac = gmul_mac(r, {0, 0, m.s16(p + 0x202)}, {0, 0, 0}, 12);
    m.w32(p + 0x21C, S32(mac[1] << 16));
    m.w32(p + 0x228, 0);
    m.w16(p + 0x2E8, 0x4C);
    m.w32(p + 0x200, S32(mac[2] << 16));
    return p;
}

// throw_weapon (0x8007CF4C), weapons 3 and 10 (engine_grenade.py throw_weapon).
void throw_weapon(Engine& eng, U e, I bone_a, I bone_b, I fire, I charge) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    I slot = m.s8(inv + 0x54);
    if ((fire & 0xFF) && m.s16(inv + 0x66 + 2 * slot) != 0 && m.s32(inv + 0x124) == 0) {
        m.w32(e + 0x4C + 4 * 0x10, 1);  // variable 0x10
        bool stick = m.u8(inv + 0x55) == 0xA;
        I wind;
        if (charge < 0xC) {
            VM(eng, e).script_start(0xC, 1, 0);
            wind = stick ? 0x12 : 8;
        } else {
            VM(eng, e).script_start(0xE, 1, 0);
            wind = stick ? 0xF : 8;
        }
        m.w32(inv + 0x120, wind);
        I d = m.s32(inv + 0x40);
        m.w32(inv + 0x124, 1);
        m.w32(inv + 0x44, d);
        m.w32(inv + 0x48, iabs(d));
        ammo_hud_update(eng, e);
    }
    if (m.s32(inv + 0x124) == 0) return;
    I c = m.s32(inv + 0x124);
    m.w32(inv + 0x124, c + 1);
    if (c < m.s32(inv + 0x120)) return;  // the count before this tick's step
    U model = m.u8(inv + 0x55) == 0xA ? STICK_GRENADE_MODEL : GRENADE_MODEL;
    U off_a = 0, off_b = 0;
    U inst = U(m.u32(e + 0x100));
    if (m.u32(inst + 0x84)) {
        U mdl = U(m.u32(inst + 8));
        off_a = mdl + 0xD8, off_b = mdl + 0xE0;
        bone_a = m.s16(mdl + 0xD4), bone_b = m.s16(mdl + 0xD6);
    }
    m.w32(G_STAT_SHOTS, m.u32(G_STAT_SHOTS) + 1);
    U g = grenade_spawn(eng, e, model, bone_a, bone_b, 0x5A, charge * 2 + 0x37, off_a, off_b);
    if (g) start_within_reach(eng, e, g, inv);
    if (!(m.u32(G_CHEAT_FLAGS) & 0x40)) {
        U a = inv + 0x66 + 2 * slot;
        m.w16(a, (m.u16(a) - 1) & 0xFFFF);
    }
    U w = U(G_WEAPONS + 0x44 * m.u8(inv + 0x55));
    I snd = m.s16(w + 0x14);
    if (snd >= 0) {
        I bank = m.s16(w + 0x16);
        if (bank < 0) bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
        eng.audio->play(S16(bank), snd, e, 0x20000, 0, 0x10000, 0x1800, 0, 0, 0, 0);
    }
    m.w32(inv + 0x124, 0);
}

// grenade_soldier_bits (0x80044120): what soldiers' scripts set on the grenade's +0x3C.
void grenade_soldier_bits(Engine& eng, U e) {
    Mem& m = eng.m;
    I b = m.u32(e + 0x3C);
    U o = U(m.u32(e + 0x388));
    if (b & 1) {
        // kicked: by the nearest soldier within reach, along his facing
        U s = near_soldier(eng, e);
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0);
        if (!s) {
            m.w16(o + 0x38, 0x348);
            return;
        }
        for (int k = 0; k < 3; k++) m.w32(e + 0x228 + 4 * k, m.u32(s + 0x228 + 4 * k));
        m.w32(e + 0x200, 0x190000);
        m.w32(e + 0x21C, S32(0xFFDF0000));
        m.w32(e + 0x284, 0);
        m.w32(o + 0x68, S32((S32(grenade_rd(eng) << 12) >> 15) << 16));
        m.w32(o + 0x6C, 0);
        m.w32(o + 0x70, 0);
        m.w32(o + 0x74, S32((((grenade_rd(eng) * 0x28A) >> 15) + 0xC8) << 16));
        I y = ((grenade_rd(eng) * 0x1E) >> 15) + 0xA;
        m.w16(o + 0x38, 0x348);
        m.w32(o + 0x78, S32(y << 16));
        m.w32(o + 0x7C, 0);
        m.w16(o + 0x3A, m.u16(o + 0x3A) + 8);
        m.w8(o + 0x41, (m.u8(o + 0x41) + 8) & 0xFF);
        return;
    }
    if (b & 8) {
        // dropped (the holder was hit)
        handle_store(m, e + 0x30, 0);
        m.w32(e + 0x284, 0);
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0);
        m.w16(o + 0x38, 0x348);
        return;
    }
    if (b & 2) {
        // picked up: held on the nearest soldier's bone 17, life 120, the fuse back to 90
        U s = near_soldier(eng, e);
        if (!s) {
            m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0);
            m.w16(o + 0x38, 0x348);
            return;
        }
        handle_store(m, e + 0x30, s);
        m.w8(e + 0x34, 0x11);
        m.w16(o + 0x3A, 0x78);
        m.w8(o + 0x41, 0x5A);
        for (U k : {0x68u, 0x6Cu, 0x70u, 0x74u, 0x78u, 0x7Cu}) m.w32(o + k, 0);
        m.w16(o + 0x38, m.u16(o + 0x38) & 0xFDF6);
        return;
    }
    if (b & 4) {
        // thrown back along the soldier's facing, harder the further the player is
        U s = near_soldier(eng, e);
        handle_store(m, e + 0x30, 0);
        m.w32(e + 0xC, m.u32(e + 0xC) | 0x040000A0);
        if (!s) {
            m.w16(o + 0x38, 0x348);
            return;
        }
        for (int k = 0; k < 3; k++) m.w32(e + 0x228 + 4 * k, m.u32(s + 0x228 + 4 * k));
        I d = imax(dist2d_approx(m, e, U(m.u32(G_SOLDIER))), 0);
        I a1 = d >> 6;
        m.w32(e + 0x21C, S32(0xFFE70000));
        I sp = a1 + 0x1E;
        if (d >= 0x7D1) {
            sp = a1 + 0x23;
            m.w32(e + 0x21C, S32(0xFFE20000));
        } else if (d >= 0x515)
            sp = a1 + 0x23;
        m.w32(e + 0x200, S32(imin(sp, 0x41) << 16));
        m.w32(e + 0x284, 0);
        m.w32(o + 0x74, S32((((grenade_rd(eng) * 0x28A) >> 15) + 0xC8) << 16));
        I y = S32((((grenade_rd(eng) * 0x1E) >> 15) + 0xA) << 16);
        m.w16(o + 0x3A, 0x78);
        m.w8(o + 0x41, 0x5A);
        m.w32(o + 0x78, y);
        m.w16(o + 0x38, 0x348);
    }
}

// grenade_hit_part (0x80044014): not the thrower, not a projectile, not a 0x1777: the damage and,
// with bit 0x100, the small explosion (6, 6, 75) at once, the grenade removed. 1 if it hit.
I grenade_hit_part(Engine& eng, U e, I part, U other, I opart) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    if (handle_same(eng, o + 0x54, other)) return 0;
    I t = m.s32(other + 8);
    if ((0x13F0 <= t && t < 0x13FA) || t == 0x1777) return 0;
    damage_from_hit(eng, e, part, other, opart);
    if (m.u16(o + 0x38) & 0x100) {
        for (int k = 0; k < 3; k++) m.w16(e + 0x364 + 2 * k, 0);
        explosion_spawn(eng, e, 6, 6, 0x4B, 0x10000, 0x1000, 0x40000, 5, 0);
        explosion_sound(eng, e);
        eng.remove(e, "grenade hit");
    }
    return 1;
}

// explosion_spawn(source, fireballs, puffs, spread, scale, var0, damage, reach, record)
// (0x800719C0): engine_explosion.py explosion_spawn.
U explosion_spawn(Engine& eng, U src, I fireballs, I puffs, I spread, I scale, I var0, I damage, I reach, U rec) {
    Mem& m = eng.m;
    U e = eng.entity_create(0x31, (fireballs + puffs + 1) * 0x3C + 0x64, 0, 0, 1);
    m.w32(e + 8, 0x31);
    m.w32(e + 0x294, 0xB4);
    m.w32(e + 0x290, 0xB4);
    m.w32(e + 0x36C, EXPLOSION_THINK);
    m.w32(e + 0x370, EXPLOSION_DRAW);
    m.w32(e + 0x374, DAMAGE_FROM_HIT);
    m.w32(e + 0x14, 0x88004451);
    U o = U(m.u32(e + 0x388));
    m.w32(e + 0x37C, 0);
    m.w32(e + 0xC, 0x01228000);
    m.w32(e + 0x378, 0);
    m.w32(e + 0x384, EXPLOSION_DESTROY);
    m.w32(e + 0x284, 0);
    m.w32(e + 0x10, m.u32(e + 0x10) | 0x201);
    if (src) {
        for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, m.u32(src + 0x20C + 4 * k));
        m.w16(e + 0x3CA, m.u16(src + 0x3CA));
        m.w32(e + 0x10, m.u32(e + 0x10) | (m.u32(src + 0x10) & 0x100000));
        if (m.s32(src + 0x364) == 0 && m.s16(src + 0x368) == 0)
            normal_from_angle(eng, e, m.s16(src + 0x22E) + 0x800);
        else
            for (int k = 0; k < 4; k++) m.w16(e + 0x364 + 2 * k, m.u16(src + 0x364 + 2 * k));
    } else if (rec) {
        for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(m.s16(rec + 0x10 + 2 * k) << 16));
        m.w16(e + 0x3CA, 0xFFFF);
        normal_from_angle(eng, e, m.s16(rec + 0x1A) + 0x800);
    } else {
        eng.remove(e, "explosion without a place");
        return 0;
    }
    for (int k = 0; k < 3; k++) m.w32(e + 0x258 + 4 * k, m.u32(e + 0x20C + 4 * k));
    m.w32(e + 0x4C, var0);
    m.w32(o, 0);
    m.w32(o + 4, 0);
    U recs = o + 0x64;
    // the fireballs (system at obj+0xC)
    static const I fire_cols[8] = {0x3E0, 0x20, 0x3E0, 0xFF, 0x3F, 0x3F, 0x10, 2};
    sys_header(m, o + 0xC, fireballs, recs, scale, 0xF0000, -0x200, fire_cols);
    m.w32(o + 0x14, 1);  // the first think's countdown (the burst)
    I delay = 0;
    for (I i = 0; i < fireballs; i++) {
        U r = recs + 0x3C * i;
        m.w8(r, 3);
        m.w32(r + 0x20, 0x50000);
        place(eng, e, r, spread);
        m.w16(r + 8, ((explosion_rd(eng) * 0x32) >> 15) - 0x19);
        m.w16(r + 0xA, explosion_rd(eng) >> 3);
        m.w32(r + 0x10, 0x140000);
        m.w32(r + 0x18, 0x400000);
        m.w32(r + 0x14, 0x400000);
        m.w8(r + 2, 5);
        m.w8(r + 1, 5);
        m.w32(r + 0x1C, S32((((explosion_rd(eng) * 2) >> 15) + 3) << 16));
        m.w16(r + 0xC, (((explosion_rd(eng) * 0x64) >> 15) + 0x23) * 2);
        m.w16(r + 0xE, (((explosion_rd(eng) * 0x64) >> 15) + 0x23) * 2);
        m.w16(r + 6, delay);
        delay += explosion_rd(eng) >> 15;
        m.w8(r + 3, 0);
        m.w16(r + 4, 1);
    }
    U recs2 = recs + 0x3C * fireballs;
    // the smoke puffs (system at obj+0x38)
    static const I puff_cols[8] = {0x3E8, 0, 0x3E0, 0xFA, 0x1F, 0x1F, 8, 2};
    sys_header(m, o + 0x38, puffs, recs2, S32(scale * 2), 0x1E0000, -0x2000, puff_cols);
    for (I i = 0; i < puffs; i++) {
        U r = recs2 + 0x3C * i;
        m.w8(r, 3);
        m.w32(r + 0x20, 0);
        place(eng, e, r, spread);
        m.w16(r + 8, ((explosion_rd(eng) * 3) & 0xFFFFFFFF) >> 13);
        m.w16(r + 0xA, explosion_rd(eng) >> 3);
        m.w32(r + 0x10, 0x50000);
        I big = S32((((explosion_rd(eng) * 0xA) >> 15) + 0x14) << 16);
        m.w8(r + 2, 0x2D);
        m.w8(r + 1, 0x2D);
        m.w32(r + 0x18, big);
        m.w32(r + 0x14, big);
        m.w32(r + 0x1C, 0x50000);
        m.w16(r + 0xC, 0x78);
        m.w16(r + 0xE, 0x78);
        m.w16(r + 6, ((explosion_rd(eng) * 0x14) >> 15) + 8);
        m.w8(r + 3, 0);
        m.w16(r + 4, 2);
    }
    m.w16(e + 0x2E8, spread * 5);
    m.w16(e + 0x36, 1);
    U q = U(m.u32(e + 0x304));
    m.w32(q, 0);
    m.w32(q + 0x18, 0);
    m.w32(q + 8, 5);
    m.w32(q + 0xC, 5);
    m.w32(q + 0x10, damage);
    m.w32(e + 0xF8, damage);
    for (int k = 0; k < 3; k++) m.w32(q + 0x20 + 4 * k, m.s16(e + 0x20E + 4 * k));
    I rr = S32(spread * reach);
    m.w32(q + 0x34, rr);
    m.w32(q + 0x30, rr);
    m.w32(q + 0x38, S32(rr * rr));
    m.w32(q + 0x3C, 0);  // the fireball model's instance: drawing only
    parts_box(m, e);
    eng.link_front(e);
    m.w8(G_LIGHT, 0x50);
    m.w8(G_LIGHT + 1, 0x28);
    m.w8(G_LIGHT + 2, 0xA);
    m.w16(G_LIGHT + 4, 0x96);
    m.w32(G_LIGHT + 8, 0x57E4);
    m.w32(G_LAST_EXPLOSION, e);
    m.w8(G_LIGHT_STEP, 0);
    m.w32(G_LIGHT_ON, 1);
    return e;
}

// explosion_think (0x800715D4): engine_explosion.py explosion_think.
void explosion_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    U s1 = o + 0xC;
    I c = m.s32(s1 + 8);
    if (c) {
        c--;
        m.w32(s1 + 8, c);
        if (c <= 0) {
            burst(eng, e);
            m.w32(s1 + 8, 0);
            U sol = U(m.u32(G_SOLDIER));
            I d[3] = {iabs(S16((m.s32(sol + 0x20C) - m.s32(e + 0x20C)) >> 16)), 0,
                      iabs(S16((m.s32(sol + 0x214) - m.s32(e + 0x214)) >> 16))};
            std::sort(d, d + 3);
            I t = d[2] + ((d[0] + d[1]) >> 2);
            if (t < 0xBB8) {
                U co = U(m.u32(m.u32(G_PLAYER) + 0x388));
                m.w16(co + 0xCC, (0xBB8 - t) >> 4);  // camera_shake (0x80046C50)
                m.w16(co + 0xCE, 0xA);
            }
        }
    }
    if (m.s16(s1 + 6) <= 0) {
        m.w16(e + 0x2E8, 0);
        m.w16(e + 0x36, 0);
        m.w32(e + 0x10, m.u32(e + 0x10) & ~I(2));
    }
    if (m.s32(o + 0xC) != 2) {
        particles_update(eng, s1);
        if (m.s32(o + 0xC) == 2) m.w32(o, 1);
    }
    if (m.s32(o + 0x38) == 2)
        m.w32(o + 4, 1);
    else
        particles_update(eng, o + 0x38);
    if (m.s32(o + 4) && m.s32(o)) eng.remove(e, "explosion done");
    if (U(m.u32(G_LAST_EXPLOSION)) == e) {
        if (m.u8(G_LIGHT_STEP) < 8) {
            I r = (m.u16(G_LIGHT + 4) + 0xAF) & 0xFFFF;
            m.w8(G_LIGHT_STEP, m.u8(G_LIGHT_STEP) + 1);
            m.w8(G_LIGHT, (m.u8(G_LIGHT) + 8) & 0xFF);
            m.w16(G_LIGHT + 4, r);
            m.w8(G_LIGHT + 1, (m.u8(G_LIGHT + 1) + 3) & 0xFF);
            m.w8(G_LIGHT + 2, (m.u8(G_LIGHT + 2) + 1) & 0xFF);
            m.w32(G_LIGHT + 8, S32(S16(r) * S16(r)));
            return;
        }
        m.w8(G_LIGHT, (m.u8(G_LIGHT) - 4) & 0xFF);
        m.w8(G_LIGHT + 1, (m.u8(G_LIGHT + 1) - 3) & 0xFF);
        if (m.u8(G_LIGHT + 2)) m.w8(G_LIGHT + 2, m.u8(G_LIGHT + 2) - 1);
        I r = (m.u16(G_LIGHT + 4) - 0x4B) & 0xFFFF;
        m.w16(G_LIGHT + 4, r);
        m.w32(G_LIGHT + 8, S32(S16(r) * S16(r)));
        if (m.s32(o)) {
            m.w32(G_LAST_EXPLOSION, 0);
            m.w8(G_LIGHT_STEP, 0);
        }
    }
}

// 0x800702A4: the life (obj+4) down; removed at 0.
void burst_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    I c = m.s32(o + 4) - 1;
    m.w32(o + 4, c);
    if (c <= 0) eng.remove(e, "burst done");
}

// explosion_sound (0x80042DF8): two or three layers of sounds 9/10.
void explosion_sound(Engine& eng, U e) {
    Mem& m = eng.m;
    I t = m.s32(e + 8);
    auto r = [&]() { return ((eng.rand.draw({0x80042DF8}) * 2) >> 15) + 9; };
    if (t == 0x13F1 || t == 0x13F3) eng.audio->play(0, r(), e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 3, 0);
    I a = r();
    eng.audio->play(0, a, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 3, 0);
    eng.audio->play(0, a, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 3, 0);
}

}  // namespace valor::sim
