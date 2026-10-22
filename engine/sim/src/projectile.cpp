// Firing, projectiles and what a shot leaves behind (docs/re/combat.md, objects.md):
// tools/sim/engine_projectile.py.
#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {
constexpr U G_WEAPONS = 0x80093640;
constexpr U G_SLOT_WEAPON = 0x8009E878;
constexpr U G_CHEAT_FLAGS = 0x800386A4;
constexpr U G_STAT_SHOTS = 0x80039CA8;
constexpr U G_CASINGS = 0x8009EC60;
constexpr U G_RUMBLE_MODE = 0x80093A04;
constexpr U G_LAST_DECAL = 0x800AB918;
constexpr U G_DECAL_INDEX = 0x80093C58;
constexpr U G_FLOOR_LAST = 0x800B7460;

constexpr U PROJECTILE_DRAW = 0x80044B0C;
constexpr U PROJECTILE_BRAIN = 0x80044BE4;
constexpr U NO_BRAIN = 0x8006A318;
constexpr U IMPACT_DRAW = 0x8006F7BC;
constexpr U PARTICLES_DRAW = 0x800732F8;

I weapon(Mem& m, I w, I off, int size = 2, bool sgn = true) {
    U a = U(G_WEAPONS + 0x44 * w + off);
    if (size == 4) return m.s32(a);
    return sgn ? m.s16(a) : m.u16(a);
}

}  // namespace

void rumble_set(Mem& m, I port, I a, I b, I c, I d) {
    I mode = m.u32(G_RUMBLE_MODE);
    if (mode == 1)
        b >>= 1;
    else if (mode == 2)
        b -= b >> 2;
    else if (mode != 3)
        return;
    m.w8(0x80093BE8 + 2 * port, a);
    m.w8(0x80093BE8 + 2 * port + 1, b);
    m.w32(0x80093BFC + 4 * port, c);
    m.w32(0x80093BF4 + 4 * port, (I(1) << d) - 1);
}

namespace {

void tracer(Mem& m, U p, I r, I g, I b, I r2, I g2, I b2) {
    U o = U(m.u32(p + 0x388));
    m.w16(o + 0x38, m.u16(o + 0x38) | 0xA0);
    for (U prim : {o + 4, o + 0x18}) {
        m.w8(prim + 3, 4);
        m.w8(prim + 7, 0x50);
    }
    I c1[3] = {r, g, b}, c2[3] = {r2, g2, b2};
    for (int k = 0; k < 3; k++) {
        m.w8(o + 8 + k, c1[k]);
        m.w8(o + 0x1C + k, c1[k]);
        m.w8(o + 0x10 + k, c2[k]);
        m.w8(o + 0x24 + k, c2[k]);
    }
    for (int k = 0; k < 3; k++) m.w32(p + 0x258 + 4 * k, m.u32(p + 0x20C + 4 * k));
}

void pull_back(Mem& m, U owner, U p) {
    I dx = S16(m.s16(p + 0x20E) - m.s16(owner + 0x20E));
    I dz = S16(m.s16(p + 0x216) - m.s16(owner + 0x216));
    I a = iabs(dx), b = iabs(dz);
    if (a < b) std::swap(a, b);
    I t = a + (b >> 2);
    t -= t >> 2;
    U offs[3] = {0x344, 0x34A, 0x350};
    for (int k = 0; k < 3; k++)
        m.w32(p + 0x20C + 4 * k, S32(m.s32(p + 0x20C + 4 * k) + (((-m.s16(p + offs[k]) * t) >> 12) << 16)));
}

V3 attachment_point(Engine& eng, U B, I idx) {
    Mem& m = eng.m;
    U vt = U(m.u32(B + 0x94 + 0x2C));
    V3 v = {m.s16(vt + 8 * idx), m.s16(vt + 8 * idx + 2), m.s16(vt + 8 * idx + 4)};
    M3 R;
    V3 t;
    if (m.s32(B + 8) && (m.u32(B) & 1)) {
        for (int i = 0; i < 3; i++)
            for (int k = 0; k < 3; k++) R[i][k] = m.s16(B + 0x74 + 6 * i + 2 * k);
        for (int k = 0; k < 3; k++) t[k] = m.s32(B + 0x88 + 4 * k);
    } else {
        auto r = rot_yxz9(m.s16(B + 0x62), m.s16(B + 0x66), m.s16(B + 0x6A));
        I sc[3] = {m.s32(B + 0x24) >> 4, m.s32(B + 0x28) >> 4, m.s32(B + 0x2C) >> 4};
        for (int i = 0; i < 3; i++)
            for (int k = 0; k < 3; k++) R[i][k] = S16((r[3 * i + k] * sc[k]) >> 12);
        for (int k = 0; k < 3; k++) t[k] = m.s16(B + 0x3E + 4 * k);
    }
    V3 mac = gmul_mac(R, v, t, 12);
    return {S16(mac[0]), S16(mac[1]), S16(mac[2])};
}

}  // namespace

void gun_points(Engine& eng, U owner, I bone_a, I bone_b, U attach, U off_a, U off_b, V3& pa, V3& pb) {
    if (attach) {
        pa = attachment_point(eng, attach, bone_a);
        pb = attachment_point(eng, attach, bone_b);
        return;
    }
    Mem& m = eng.m;
    V3 oa{}, ob{};
    if (off_a) oa = {m.s16(off_a), m.s16(off_a + 2), m.s16(off_a + 4)};
    if (off_b) ob = {m.s16(off_b), m.s16(off_b + 2), m.s16(off_b + 4)};
    pa = model_bone_point(eng, owner, bone_a, off_a ? &oa : nullptr);
    pb = model_bone_point(eng, owner, bone_b, off_b ? &ob : nullptr);
}

void projectile_aim_at_point(Engine& eng, U owner, U p) {
    Mem& m = eng.m;
    U inv = U(m.u32(owner + 0x388));
    U o = U(m.u32(p + 0x388));
    V3 start = {m.s32(inv + 0x198), m.s32(inv + 0x19C), m.s32(inv + 0x1A0)};
    V3 aim = {m.s32(inv + 0x1A4), m.s32(inv + 0x1A8), m.s32(inv + 0x1AC)};
    for (int k = 0; k < 3; k++) m.w32(p + 0x20C + 4 * k, start[k]);
    I pitch, yaw;
    angles_from_to(eng.gte, start, aim, pitch, yaw);
    m.w32(p + 0x228, S32(pitch << 16));
    m.w32(p + 0x22C, S32(yaw << 16));
    I dx = iabs(S32(aim[0] - start[0])), dz = iabs(S32(aim[2] - start[2]));
    I d = dx + dz - (imin(dx, dz) >> 1);
    I sp = m.s32(p + 0x200);
    I q = sp ? iabs(d) / iabs(sp) : 0;
    if ((d < 0) != (sp < 0)) q = -q;
    m.w8(o + 0x40, (q + 1) & 0xFF);
    m.w32(o + 0x3C, m.u32(p + 0x14));
    m.w32(p + 0x14, m.u32(p + 0x14) & 0xF7FFFDFF);
}

void projectile_aim(Engine& eng, U owner, U p, I bone_a, I bone_b, const V3* target, U attach, U off_a, U off_b, I aimed) {
    Mem& m = eng.m;
    if (aimed) return projectile_aim_at_point(eng, owner, p);
    if ((m.u32(owner + 0xC) & 1) && (m.u32(m.u32(owner + 0x388)) & 0x100)) {
        // through the scope: from the camera entity's position (copy_camera_pos 0x80045160)
        // toward the camera's look point, its object's first three words (angles_to_camera_look
        // 0x80045184)
        U cam = U(m.u32(G_PLAYER));
        V3 start, look;
        for (U k = 0; k < 3; k++) start[k] = m.s32(cam + 0x20C + 4 * k);
        for (U k = 0; k < 3; k++) m.w32(p + 0x20C + 4 * k, start[k]);
        U co = U(m.u32(cam + 0x388));
        for (U k = 0; k < 3; k++) look[k] = m.s32(co + 4 * k);
        I pitch, yaw;
        angles_from_to(eng.gte, start, look, pitch, yaw);
        m.w32(p + 0x228, S32(pitch << 16));
        m.w32(p + 0x22C, S32(yaw << 16));
        return;
    }
    I t = m.s32(p + 8);
    if (t == 0x13F3 || (t == 0x13F1 && (m.u32(owner + 0xC) & 1))) {
        // a grenade, the player's rocket: from the hand point with the thrower's angles
        // (copy_shooter_angles 0x80044EF0)
        V3 pa, pb;
        gun_points(eng, owner, bone_a, bone_b, attach, off_a, off_b, pa, pb);
        for (int k = 0; k < 3; k++) {
            m.w32(p + 0x20C + 4 * k, S32(pb[k] << 16));
            m.w32(p + 0x228 + 4 * k, m.u32(owner + 0x228 + 4 * k));
        }
        return;
    }
    V3 pa, pb;
    gun_points(eng, owner, bone_a, bone_b, attach, off_a, off_b, pa, pb);
    for (int k = 0; k < 3; k++) m.w32(p + 0x20C + 4 * k, S32(pb[k] << 16));
    I pitch, yaw;
    if (target) {
        V3 a = {m.s32(p + 0x20C), m.s32(p + 0x210), m.s32(p + 0x214)};
        V3 b = {S32((*target)[0] << 16), S32((*target)[1] << 16), S32((*target)[2] << 16)};
        angles_from_to(eng.gte, a, b, pitch, yaw);
        m.w32(p + 0x230, 0);
    } else {
        V3 a = {S32(pa[0] << 16), S32(pa[1] << 16), S32(pa[2] << 16)}, b = {S32(pb[0] << 16), S32(pb[1] << 16), S32(pb[2] << 16)};
        angles_from_to(eng.gte, a, b, pitch, yaw);
    }
    m.w32(p + 0x228, S32(pitch << 16));
    m.w32(p + 0x22C, S32(yaw << 16));
}

void parts_box(Mem& m, U e) {
    I lo[3] = {0x7FFF, 0x7FFF, 0x7FFF}, hi[3] = {-0x7FFF, -0x7FFF, -0x7FFF};
    U pp = U(m.u32(e + 0x304));
    for (I i = 0; i < m.u16(e + 0x36); i++) {
        U q = U(pp + 0x48 * i);
        I r = m.s16(q + 0x34);
        for (int k = 0; k < 3; k++) {
            I c = m.s32(q + 0x20 + 4 * k);
            lo[k] = S16(imin(lo[k], c - r));
            hi[k] = S16(imax(hi[k], c + r));
        }
    }
    for (int k = 0; k < 3; k++) {
        m.w16(e + 0x308 + 2 * k, lo[k]);
        m.w16(e + 0x30E + 2 * k, hi[k]);
    }
}

U projectile_create(Engine& eng, U owner, I etype, I model, I speed, I bone_a, I bone_b, const V3* a6, I, I dmg, I life, I extra,
                    U a11, U off_a, U off_b, I aimed, I fa, I fb, I mask) {
    Mem& m = eng.m;
    U p;
    if (model != -1) {
        // entity_create(1, ..., the model, 0x147, 2): an instance, model_instance_init (its parts)
        p = eng.entity_create(etype, extra + 0x90, 0, 0, 2, 0, 0, true);
        if (p && !eng.model_swap(p, U(model), 0x147)) {
            eng.free(p);
            p = 0;
        }
    } else
        p = eng.entity_create(etype, extra + 0x90, 0, 0, 2);
    if (!p) return 0;  // the heap refused
    U o = U(m.u32(p + 0x388));
    m.w32(p + 0x36C, PROJECTILE_THINK);
    m.w32(p + 0x370, PROJECTILE_DRAW);
    m.w32(p + 0x374, PROJECTILE_HIT_PART);
    m.w32(p + 0x378, PROJECTILE_WORLD_CONTACT);
    m.w32(p + 0x384, PROJECTILE_BRAIN);
    m.w32(p + 8, etype);
    m.w32(p + 0xC, fa | 0x108);
    m.w32(p + 0x14, mask);
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x200 | fb);
    if ((m.u32(owner + 0xC) & 1) && (m.u32(m.u32(owner + 0x388)) & 0x20000000)) dmg *= 4;
    m.w32(p + 0xF8, dmg);
    m.w32(p + 0xC, m.u32(p + 0xC) | (m.u32(owner + 0xC) & 4));
    m.w32(p + 0x30, 0);
    m.w8(p + 0x34, 0xFF);
    m.w16(o + 0x38, 0);
    handle_store(m, o + 0x54, owner);
    m.w16(o + 0x3A, life);
    m.w32(o + 0x2C, 0);
    m.w32(o + 0x30, 0);
    m.w8(o + 0x42, 0);
    m.w32(o + 0x34, 0x10000);
    for (int k = 0; k < 3; k++) m.w32(p + 0x218 + 4 * k, 0);
    m.w32(p + 0x200, speed);
    projectile_aim(eng, owner, p, bone_a, bone_b, a6, a11, off_a, off_b, aimed);
    m.w32(p + 0x284, 0);
    m.w32(o + 0x58, 0);
    m.w32(o + 0x44, 0);
    eng.matrix(p);
    for (int k = 0; k < 3; k++) m.w32(p + 0x258 + 4 * k, m.u32(p + 0x20C + 4 * k));
    if (m.u32(owner + 0xC) & 1) {
        U inv = U(m.u32(owner + 0x388));
        I w = m.u8(G_SLOT_WEAPON + m.s8(inv + 0x54));
        if (weapon(m, w, 0x2A, 2, false) & 2) pull_back(m, owner, p);
    }
    U parts = U(m.u32(p + 0x304));
    if ((model == 0 || model == -1) && parts) {
        m.w32(parts + 0x3C, 0);
        m.w32(parts + 0x84, 0);
    }
    parts_box(m, p);
    eng.link_end(p);
    for (int k = 0; k < 3; k++) m.w32(o + 0x84 + 4 * k, m.u32(p + 0x20C + 4 * k));
    return p;
}

// world_sweep's face record into +0x318 (0x28 bytes): the point, the normal (+6: the face's d),
// the three vertices (+0x10, +0x18, +0x20); the halfwords after the point and each vertex are left
// (the original's are scratch and vertex padding nothing reads).
void write_contact(Mem& m, U e, const I point[3], const I normal[3], const SweepFace& face) {
    for (int k = 0; k < 3; k++) {
        m.w16(e + 0x318 + 2 * k, point[k]);
        m.w16(e + 0x320 + 2 * k, normal[k]);
    }
    m.w16(e + 0x326, face.d);
    for (U j = 0; j < 3; j++)
        for (U k = 0; k < 3; k++) m.w16(e + 0x328 + 8 * j + 2 * k, face.v[j][k]);
}

namespace {

U pellet_spawn(Engine& eng, U owner, I bone_a, I bone_b, const V3* a3, U a4, U off_a, U off_b, I aimed, I etype, I fa, I fb, I mask) {
    Mem& m = eng.m;
    I speed = (((eng.rand.draw(FN("pellet")) * 0x190) >> 15) + 0x15E) << 16;
    U p = projectile_create(eng, owner, etype, -1, speed, bone_a, bone_b, a3, 0x20, 0x20000, 0xF, 0, a4, off_a, off_b, aimed, fa,
                            fb, mask);
    if (!p) return 0;
    U o = U(m.u32(p + 0x388));
    m.w32(p + 0x37C, BULLET_TOUCH);
    m.w16(o + 0x38, 0);
    if ((m.u32(owner + 0xC) & 1) && (m.u32(G_CHEAT_FLAGS) & 0x20)) m.w16(o + 0x38, 0xA40);
    tracer(m, p, 0xE6, 0xB4, 0x46, 0x28, 0x14, 0xA);
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x24);
    I r1 = (eng.rand.draw(FN("pellet")) & 0x3FFF) - 0x2000;
    I r2 = (eng.rand.draw(FN("pellet")) & 0x3FFF) - 0x2000;
    I r3 = eng.rand.draw(FN("pellet")) & 0x3FFF;
    m.w32(p + 0x228, S32(m.s32(p + 0x228) + (r1 << 9)));
    m.w32(p + 0x22C, S32(m.s32(p + 0x22C) + (r2 << 9)));
    m.w32(p + 0x230, S32(m.s32(p + 0x230) + ((r3 - 0x2000) << 9)));
    return p;
}


// entity_move's floor part for the general case (0x8006C83C): see engine_projectile.py floor_step.
I floor_step(Engine& eng, U e, I res, I ground0, I vy16, bool walled = false) {
    Mem& m = eng.m;
    if ((m.u32(e + 0x18) & 1) || (m.u32(e + 0x10) & 0x10)) return res;
    I fa = m.u32(e + 0xC);
    if (!(m.s32(e + 0x284) || (fa & 8))) return res;
    I pos[3] = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
    Floor fl = eng.world.floor(pos);
    m.w32(e + 0x390, fl.zone);
    I f = fl.height;
    m.w16(G_FLOOR_LAST, f);
    if (f == -0x7FFF) {
        if ((fa & 7) && !(m.u32(e + 0x10) & 0x10000)) {
            for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, m.u32(e + 0x3BC + 4 * k));
            if (!ground0) m.w32(e + 0x210, m.u32(e + 0x208));
            res |= 2;
            m.w32(e + 0x284, 1);
            m.w32(e + 0x38, m.u32(e + 0x38) | 0x80008000);
        }
        return res;
    }
    m.w32(e + 0x208, S32(f << 16));
    I d = m.s16(e + 0x212) - f;
    if (m.s32(e + 0x284)) {
        if ((0 <= d + 0x20 && d + 0x20 < 0x121) || d >= 0)
            m.w32(e + 0x210, S32(f << 16));
        else if ((m.u32(e + 0x10) & 0x200000) && d < -0x39) {
            res |= 0x80;
            for (U k : {0x220u, 0x21Cu, 0x218u, 0x200u}) m.w32(e + k, 0);
            for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, m.u32(e + 0x3BC + 4 * k));
            m.w32(e + 0x284, ground0);
            m.w32(e + 0x38, m.u32(e + 0x38) | 0x80000000);
        } else {
            m.w32(e + 0x284, 0);
            m.w32(e + 0x38, m.u32(e + 0x38) | 1);
        }
        return res;
    }
    if (!(fa & 0x20)) return res;
    if (d < 0) {
        I lim = vy16 < 0x30 ? vy16 + 0x10 : 0x40;
        if (lim < iabs(d)) return res;
    }
    if (fa & 1) {
        if (vy16 < 0) return res;
    } else if ((fa & 0x80) && iabs(vy16) >= 8) {
        if (!walled && fl.has_normal)
            for (int k = 0; k < 4; k++) m.w16(e + 0x364 + 2 * k, fl.normal[k]);
        res |= 1;
        m.w32(e + 0x284, 0);
        m.w32(e + 0x210, S32(f << 16));
        m.w32(e + 0x21C, S32(-iabs(m.s32(e + 0x21C))));
        return res;
    }
    res |= 1;
    m.w32(e + 0x284, 1);
    m.w32(e + 0x210, S32(f << 16));
    m.w32(e + 0x1F4, 0);
    m.w32(e + 0x21C, 0);
    m.w32(e + 0x38, m.u32(e + 0x38) & ~I(1));
    return res;
}

// 0x8004E354's fitting (0x8004DE80 / 0x8004E01C / 0x8004E1B8 by the normal's largest axis: y, x,
// else z): the hole, a square of half size 5..8, is kept inside the face's bounding box on the two
// other axes, moving the record's point (which the impact's sparks then use): where it sticks out
// past one side it moves in by the overhang; past both, to the low side + the half size (shrunk to
// half the narrower extent).
static void decal_fit(Mem& m, U rec, I half) {
    I n[3];
    for (U k = 0; k < 3; k++) n[k] = iabs(m.s16(rec + 8 + 2 * k));
    int axes[2];
    if (imax(n[0], n[2]) < n[1])
        axes[0] = 0, axes[1] = 2;
    else if (n[2] < n[0])
        axes[0] = 1, axes[1] = 2;
    else
        axes[0] = 0, axes[1] = 1;
    I lo[2], hi[2];
    for (int i = 0; i < 2; i++) {
        lo[i] = 0x7FFFFFFF, hi[i] = -0x7FFFFFFF;
        for (U j = 0; j < 3; j++) {
            I v = m.s16(rec + 0x10 + 8 * j + 2 * U(axes[i]));
            lo[i] = imin(lo[i], v), hi[i] = imax(hi[i], v);
        }
    }
    I a = half;
    for (int i = 0; i < 2; i++)
        if (iabs(hi[i] - lo[i]) < half * 2) a = imin(a, iabs(hi[i] - lo[i]) >> 1);
    for (int i = 0; i < 2; i++) {
        U at = rec + 2 * U(axes[i]);
        I p = m.s16(at);
        bool under = p - half < lo[i];
        if (hi[i] < p + half)
            p = under ? lo[i] + a : p - (p + half - hi[i]);
        else if (under)
            p = p - (p - half - lo[i]);
        m.w16(at, p);
    }
}

I decal(Engine& eng, U rec) {
    Mem& m = eng.m;
    bool near = true;
    for (int k = 0; k < 3; k++) {
        I last = m.s16(G_LAST_DECAL + 2 * k), p = m.s16(rec + 2 * k);
        if (!(last - 0x10 < p && p < last + 0x10)) near = false;
    }
    if (near) return 0;
    if (!(m.s16(rec + 8) || m.s16(rec + 0xA) || m.s16(rec + 0xC))) return 0;
    I i = m.s32(G_DECAL_INDEX) + 1;
    if (i >= 0x64) i = 0;
    decal_fit(m, rec, ((eng.rand.draw({0x8004E354}) * 4) >> 15) + 5);
    U a = U(0x80093C5C + (i << 7));
    m.w16(a + 0x26, 0xC0);
    m.w16(a + 0x28, 7);
    m.w16(a + 0x2A, 0x1F);
    m.w16(a + 0x2E, 0xFC);
    m.w16(a + 0x24, 0x3E0);
    m.w16(a + 0x2C, 0x3E0);
    m.w32(G_DECAL_INDEX, i);
    m.w32(a, m.u32(a) | 7);
    for (int k = 0; k < 4; k++) m.w16(G_LAST_DECAL + 2 * k, m.u16(rec + 2 * k));
    return 1;
}

U impact_effect_spawn(Engine& eng, const V3& pos, const V3&, const I c0[3], const I*, I n, I life) {
    Mem& m = eng.m;
    U p = eng.entity_create(0x23, 0x130);
    if (!p) return 0;
    U o = U(m.u32(p + 0x388));
    m.w8(o + 1, life);
    m.w8(o, n);
    m.w32(p + 8, 0x23);
    m.w32(p + 0x10, 0x203);
    m.w32(p + 0x36C, IMPACT_THINK);
    m.w32(p + 0x370, IMPACT_DRAW);
    for (U off : {0x14u, 0xCu, 0x374u, 0x378u, 0x228u, 0x22Cu, 0x230u, 0x284u}) m.w32(p + off, 0);
    m.w32(p + 0x384, NO_BRAIN);
    for (int k = 0; k < 3; k++) {
        m.w32(p + 0x20C + 4 * k, pos[k]);
        m.w32(p + 0x270 + 4 * k, 0x10000);
    }
    m.w16(o + 6, 0x3FF);
    m.w16(o + 2, 1);
    m.w16(o + 4, 1);
    m.w16(o + 0xA, 0x3E0);
    m.w16(o + 8, 0);
    m.w16(o + 0xC, 0xFD);
    m.w16(o + 0xE, 4);
    for (I i = 0; i < n; i++) {
        U q = U(o + 0x10 + 0x24 * i);
        I r = eng.rand.draw({0x800702E0});
        for (int k = 0; k < 3; k++) m.w32(q + 4 + 4 * k, S32(pos[k] + ((((r >> (4 * k)) & 0xF) - 8) << 16)));
        eng.rand.draw({0x800702E0});
        eng.rand.draw({0x800702E0});
        for (int k = 0; k < 3; k++) {
            m.w8(q + 0x1C + k, c0[k]);
            m.w8(q + 0x20 + k, c0[k]);
        }
    }
    return p;
}

void impact_sound(Engine& eng, U e) {
    Mem& m = eng.m;
    I snd = (eng.rand.draw({0x80043140}) * 7) >> 15;
    eng.audio->play(0, snd, e, 0x30000, 0, 0x10000, 0x7FFF, 0, 0, 3, 0);
    V3 normal = {m.s16(e + 0x364), m.s16(e + 0x366), m.s16(e + 0x368)};
    V3 u = vnormal(eng.gte, normal);
    V3 vec = {sat16i((0xC00 * u[0]) >> 12), sat16i((0xC00 * u[1]) >> 12), sat16i((0xC00 * u[2]) >> 12)};
    V3 pos = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
    I c0[3] = {0xF0, 0x48, 0}, c1[3] = {0x40, 0x10, 0};
    U p = impact_effect_spawn(eng, pos, vec, c0, c1, 8, 4);
    if (p) eng.link_front(p);
}

void particles_step(Mem& m, U sys) {
    U arr = U(m.u32(sys + 0x28));
    m.w32(sys, 2);
    for (I i = 0; i < m.s32(sys + 4); i++) {
        U q = U(arr + 0x34 * i);
        if (m.u8(q) & 2) continue;
        I life = m.u8(q + 2);
        if (life)
            m.w8(q + 2, life - 1);
        else
            m.w8(q, 2);
        I d = m.u8(q + 1);
        if (d)
            m.w8(q + 1, d - 1);
        else {
            I vy = m.s32(q + 0x14);
            I g = m.s32(sys + 0x10);
            if (iabs(vy) < iabs(m.s32(sys + 0x14)) && g) m.w32(q + 0x14, S32(vy + g));
            I vz = m.s32(q + 0x18);
            m.w32(q + 4, S32(m.s32(q + 4) + m.s32(q + 0x10)));
            m.w32(q + 0xC, S32(m.s32(q + 0xC) + vz));
            m.w32(q + 8, S32(m.s32(q + 8) + m.s32(q + 0x14)));
            I drag = m.s32(sys + 0xC);
            I vx = m.s32(q + 0x10);
            for (auto [off, v] : {std::pair<U, I>{0x18, vz}, {0x10, vx}}) {
                if (drag < iabs(v))
                    m.w32(q + off, v > 0 ? S32(v - drag) : S32(v + drag));
                else
                    m.w32(q + off, 0);
            }
            for (int k = 0; k < 3; k++) m.w16(q + 0x1C + 2 * k, (m.u16(q + 0x1C + 2 * k) + m.u16(q + 0x24 + 2 * k)) & 0xFFFF);
        }
        m.w32(sys, 1);
    }
}

}  // namespace

void handle_store(Mem& m, U dst, U ent) { m.w32(dst, ent ? m.u32(ent) : 0); }

bool handle_same(Engine& eng, U ptr, U ent) {
    I h = eng.m.u32(ptr);
    return h && !eng.handle_dead(h) && eng.resolve(h) == ent;
}

void fire_gun(Engine& eng, U e, U inv, I fire) {
    Mem& m = eng.m;
    if (!fire) return;
    I w = m.u8(inv + 0x55);
    I bone_a = weapon(m, w, 0xC), bone_b = weapon(m, w, 0xE);
    U off_a = 0, off_b = 0;
    U inst = U(m.u32(e + 0x100));
    if (inst && m.u32(inst + 0x84)) {
        U model = U(m.u32(inst + 8));
        off_a = model + 0xD8, off_b = model + 0xE0;
        bone_a = m.s16(model + 0xD4), bone_b = m.s16(model + 0xD6);
    }
    I slot = m.s8(inv + 0x54);
    if (m.s16(inv + 0x66 + 2 * slot) <= 0) return;
    I stance = m.u32(inv + 0x1C8);
    I aimed = (stance == 0 || stance == 1 || stance == 7 || stance == 8) ? 1 : 0;
    I vol = 0x17FFF;
    m.w32(G_STAT_SHOTS, m.u32(G_STAT_SHOTS) + 1);
    I dmg = weapon(m, w, 0x2C, 4);
    U shot = 0;
    I rumble = -1;
    I mask = 0x08000450;
    if (w == 0 || w == 8 || w == 9 || w == 5) {
        shot = bullet_spawn(eng, e, bone_a, bone_b, nullptr, 0, off_a, off_b, dmg, 0, aimed, 0x13F6, 0x2000, 0x20, mask);
        rumble = 3;
    } else if (w == 12) {
        shot = bullet_spawn(eng, e, bone_a, bone_b, nullptr, 0, off_a, off_b, 0x28000, 0, aimed, 0x13F6, 0x2000, 0x20, mask);
        rumble = 3;
    } else if (w == 1 || w == 6 || w == 11) {
        shot = bullet_spawn(eng, e, bone_a, bone_b, nullptr, 0, off_a, off_b, dmg, 0xB, aimed, 0x13F7, 0x2000, 0x20, mask);
        I r = S32(m.s32(inv + 0x50) + 0x10000);
        m.w32(inv + 0x50, imin(r, 0x10000));
        rumble = 3;
    } else if (w == 2) {
        shot = rocket_spawn(eng, e, bone_a, bone_b, nullptr, off_a, off_b, aimed, 0x13F1, 0x2000, 0, mask);
        rumble = 7;
    } else if (w == 4) {
        for (int i = 0; i < 6; i++) {
            shot = pellet_spawn(eng, e, bone_a, bone_b, nullptr, 0, off_a, off_b, aimed, 0x13F7, 0x2000, 0, mask);
            if (shot && i < 5) m.w16(shot + 0x3CA, w);
        }
        vol = 0x20000;
        rumble = 7;
    }
    if (rumble != -1) rumble_set(m, m.u8(inv + 0x152), 0, 100, rumble, 0);
    if (shot) m.w16(shot + 0x3CA, w);
    if (!(m.u32(inv) & 0x200040)) m.w16(inv + 0x1D4 + 2, 1);
    I snd = weapon(m, w, 0x14);
    if (snd >= 0) {
        I bank = weapon(m, w, 0x16);
        if (bank < 0) bank = m.s16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
        if (w == 5 || w == 11) vol = 0x20000;
        eng.audio->play(bank, snd, e, vol, 0, 0x10000, 0x1800, 0, 0, 0, (weapon(m, w, 0x2A, 2, false) >> 3) & 2);
    }
    if (w != 7) {
        if (!(weapon(m, w, 0x2A, 2, false) & 0x10) && ((eng.rand.draw(FN("fire_gun")) * 100) >> 15) >= 0x33) {
            V3 pos = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
            casing_spawn(eng, e, pos, 0x14000);
        }
    }
    U a = U(inv + 0x66 + 2 * slot);
    I left = S16(m.u16(a) - 1);
    m.w16(a, left);
    if (left == 0) m.w32(e + 0x4C + 4 * 0x10, 1);
    if (m.u32(G_CHEAT_FLAGS) & 0x10) eng.note("refire cheat not implemented");
    I d = m.s32(inv + 0x40);
    m.w32(inv + 0x44, d);
    m.w32(inv + 0x48, iabs(d));
    ammo_hud_update(eng, e);
}

// rocket_spawn (0x80045CA0): projectile_create(type, the model 0x9EED43D5, speed 200, the two
// points, the target, damage 30.0, life 30, object extra 0xB64: its smoke trail); object +0 = the
// trail (object +0x90, drawing: 0x8006E8D8 / 0x80070704 set it up), bullet_touch, flags A |
// 0x04000000, B | 4, behaviour 0x130 (big bullets cheat on the player's: | 0xA40); its model
// scripts (script_start 0, channel 1); the light (180, 160, 16), radius 500, and it is the latest
// explosion (0x8009D650), lighting the world while it flies (objects.md).
U rocket_spawn(Engine& eng, U owner, I bone_a, I bone_b, const V3* target, U off_a, U off_b, I aimed, I etype, I fa, I fb, I mask) {
    Mem& m = eng.m;
    U model = model_by_hash(eng, 0x9EED43D5);
    U p = projectile_create(eng, owner, etype, model ? I(model) : -1, 0xC80000, bone_a, bone_b, target, 0x20, 0x1E0000, 0x1E,
                            0xB64, 0, off_a, off_b, aimed, fa, fb, mask);
    if (!p) return 0;
    U o = U(m.u32(p + 0x388));
    m.w32(o, o + 0x90);
    m.w32(p + 0x37C, BULLET_TOUCH);
    m.w32(p + 0xC, m.u32(p + 0xC) | 0x04000000);
    m.w32(p + 0x10, m.u32(p + 0x10) | 4);
    m.w16(o + 0x38, 0x130);
    if ((m.u32(owner + 0xC) & 1) && (m.u32(G_CHEAT_FLAGS) & 0x20)) m.w16(o + 0x38, m.u16(o + 0x38) | 0xA40);
    VM(eng, p).script_start(0, 1, 0);
    constexpr U G_LIGHT = 0x800AA188;  // r, g, b, +4 radius, +8 radius^2
    m.w8(G_LIGHT, 0xB4);
    m.w8(G_LIGHT + 1, 0xA0);
    m.w8(G_LIGHT + 2, 0x10);
    m.w16(G_LIGHT + 4, 0x1F4);
    m.w32(G_LIGHT + 8, 0x3D090);
    m.w32(0x8009D650, p);
    return p;
}

U bullet_spawn(Engine& eng, U owner, I bone_a, I bone_b, const V3* a3, U a4, U off_a, U off_b, I dmg, I spread, I aimed, I etype,
               I fa, I fb, I mask) {
    Mem& m = eng.m;
    I speed = (pmod(eng.rand.draw(FN("bullet")), 70) + 600) << 16;
    U p = projectile_create(eng, owner, etype, -1, speed, bone_a, bone_b, a3, 0x20, dmg, 0xF, 0, a4, off_a, off_b, aimed, fa, fb,
                            mask);
    if (!p) return 0;
    U o = U(m.u32(p + 0x388));
    m.w32(p + 0x37C, BULLET_TOUCH);
    m.w16(o + 0x38, 0);
    if ((m.u32(owner + 0xC) & 1) && (m.u32(G_CHEAT_FLAGS) & 0x20)) m.w16(o + 0x38, 0xA40);
    tracer(m, p, 0xE6, 0xB4, 0x46, 0x28, 0x14, 0xA);
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x90004);
    if (spread) {
        I msk = (I(2) << spread) - 1, half = I(1) << spread;
        I r1 = (eng.rand.draw(FN("bullet")) & msk) - half;
        I r2 = (eng.rand.draw(FN("bullet")) & msk) - half;
        I r3 = eng.rand.draw(FN("bullet")) & msk;
        m.w32(p + 0x228, S32(m.s32(p + 0x228) + (r1 << 9)));
        m.w32(p + 0x22C, S32(m.s32(p + 0x22C) + (r2 << 9)));
        m.w32(p + 0x230, S32(m.s32(p + 0x230) + ((r3 - half) << 9)));
    }
    return p;
}

namespace {

// projectile_think, a rolling grenade (0x400): on the ground, the bounce sound on a bounce tick,
// the pitch and the yaw spin halved, obj+0x70 = 0x04000000, the speed down 3.0 a tick (1.0 at
// 24.99 or less); below 0 it stops; off the ground (or stopped) its rolling voice is stopped.
void rolling(Engine& eng, U e, U o, I f) {
    Mem& m = eng.m;
    if (m.s32(e + 0x284)) {
        if (f & 2) {
            I snd = ((eng.rand.draw(FN("grenade")) * 2) >> 15) + 7;
            eng.audio->play(1, snd, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 0, 0);
        }
        if (m.s32(e + 0x228)) m.w32(e + 0x228, m.s32(e + 0x228) >> 1);
        if (m.s32(o + 0x78)) m.w32(o + 0x78, m.s32(o + 0x78) >> 1);
        m.w32(o + 0x70, 0x04000000);
        I sp = m.s32(e + 0x200);
        sp = S32(sp - ((sp > 0x18FFFF ? 3 : 1) << 16));
        m.w32(e + 0x200, sp);
        if (sp >= 0) return;
        for (U k : {0x220u, 0x218u, 0x23Cu, 0x238u, 0x234u, 0x200u}) m.w32(e + k, 0);
        for (U k : {0x7Cu, 0x78u, 0x74u, 0x68u}) m.w32(o + k, 0);
        m.w16(o + 0x38, m.u16(o + 0x38) & 0xFBFF);
    }
    if (m.s32(o + 0x30) > 0) m.w32(o + 0x30, 0);  // voice_stop: sound only
}

// projectile_world_contact, a bouncing projectile (0x208) not rolling (engine_projectile.py bounce).
void bounce(Engine& eng, U e, U o, I res) {
    Mem& m = eng.m;
    I f = m.u16(o + 0x38);
    if (!(f & 0x800)) {
        I sp = m.s32(e + 0x200);
        sp = S32(sp - cdiv(sp, 3));
        m.w32(e + 0x200, imax(sp, 0));
        if ((res & 1) && m.s32(e + 0x208) < S32(m.s32(e + 0x210) + m.s32(e + 0x21C) + m.s32(o + 0x34) + m.s32(e + 0x1F4))) {
            m.w16(o + 0x38, (m.u16(o + 0x38) & 0xFFF6) | 0x400);
            m.w32(e + 0x284, 1);
            m.w32(e + 0x21C, 0);
            m.w32(e + 0x228, 0);
            m.w32(e + 0xC, (m.u32(e + 0xC) & ~I(0x80)) | 0x28);
            m.w32(o + 0x74, 0);
            if (m.s32(e + 0x200) <= 0x1DFFFF) {
                m.w32(o + 0x80, 0x1E0000);
                m.w32(e + 0x200, 0x1E0000);
            }
        }
    }
    if (m.s32(e + 8) != 0x13F3) {
        I snd = (eng.rand.draw(FN("grenade")) * 7) >> 15;
        eng.audio->play(0, snd, e, 0x30000, 0, 0x10000, 0x7FFF, 0, 0, 3, 0);
    } else {
        I snd = ((eng.rand.draw(FN("grenade")) * 2) >> 15) + 7;
        eng.audio->play(1, snd, e, 0x40000, 0, 0x10000, 0x40AC, 0, 0, 0, 0);
    }
    if (!(m.u16(o + 0x38) & 0x800)) {
        m.w32(o + 0x34, 0xA0000);
        m.w32(e + 0x21C, m.s32(e + 0x21C) >> 2);
    }
}

// A rolling grenade's first contact: every living soldier and dog within 1200 gets +0x3C 0x40;
// the nearest 0x80 if a dog or within 1000 (fewer than three near), else 0x20 if three or more;
// then the grenade's hit part kind 7.
void warn_soldiers(Engine& eng, U e) {
    Mem& m = eng.m;
    U near = 0;
    I best = 0x7FFFFFFF, n = 0;
    for (U o : m.entities()) {
        I t = m.s32(o + 8);
        if ((t != 0x1402 && t != 0x1405) || (m.u32(o + 0xC) & 0x20080000)) continue;
        I d = dist2d_approx(m, e, o) & 0xFFFFFFFF;
        if (d < 0x4B0) {
            n++;
            m.w32(o + 0x3C, m.u32(o + 0x3C) | 0x40);
            if (!near || d < best) near = o, best = d;
        }
    }
    if (!near) return;
    if (m.s32(near + 8) == 0x1405 || ((n & 0xFF) < 3 && best < 0x3E8))
        m.w32(near + 0x3C, m.u32(near + 0x3C) | 0x80);
    else if ((n & 0xFF) >= 3)
        m.w32(near + 0x3C, m.u32(near + 0x3C) | 0x20);
    else
        return;
    U pp = U(m.u32(e + 0x304));
    for (I i = 0; i < m.u16(e + 0x36); i++) m.w32(pp + 0x48 * i + 8, 7);
}

// projectile_world_contact, 0x200: the speed, the spin, and the heading reflected about the
// contact normal (engine_projectile.py bounce_turn).
void bounce_turn(Engine& eng, U e, U o, I res) {
    Mem& m = eng.m;
    if (!(m.u32(e + 0xC) & 0x80)) m.w32(e + 0x200, m.u32(o + 0x80));
    I f = m.u16(o + 0x38);
    if (!(f & 0x800)) {
        if (f & 0x400) {
            I sp = m.s32(e + 0x200);
            sp = S32(sp - (sp >> 1));
            m.w32(e + 0x200, imax(sp, 0));
            if (f & 0x4000) {
                warn_soldiers(eng, e);
                m.w16(o + 0x38, m.u16(o + 0x38) & 0xBFFF);
            }
        } else if (res & 2) {
            I sp = m.s32(e + 0x200);
            sp = S32(sp - cdiv(sp, 3));
            m.w32(e + 0x200, imax(sp, 0));
        }
        m.w32(o + 0x74, m.s32(o + 0x74) >> 1);
        m.w32(o + 0x78, m.s32(o + 0x78) >> 1);
    }
    I d[3] = {m.s16(e + 0x344), 0, m.s16(e + 0x350)};
    I n[3] = {m.s16(e + 0x364), m.s16(e + 0x366), m.s16(e + 0x368)};
    I dot = S32(d[0] * n[0] + d[2] * n[2]);
    I k0 = -(dot >> 17);
    I t[3];
    for (int k = 0; k < 3; k++) t[k] = sat16i((k0 * n[k]) >> 12);
    d[0] = S16(d[0] + t[0]), d[2] = S16(d[2] + t[2]);
    m.w32(e + 0x22C, S32(I(eng.gte.ratan2(int32_t(d[0]), int32_t(d[2]))) << 16));
    m.w32(e + 0x228, 0);
    m.w32(e + 0x230, 0);
    if ((m.s32(e + 0x228) ^ m.s32(o + 0x68)) >= 0) m.w32(o + 0x74, S32(-m.s32(o + 0x74)));
    if ((m.s32(e + 0x22C) ^ m.s32(o + 0x6C)) >= 0) m.w32(o + 0x78, S32(-m.s32(o + 0x78)));
    if (m.s32(o + 0x74) == 0 && m.s32(o + 0x78) == 0)
        for (int k = 0; k < 3; k++) m.w32(o + 0x68 + 4 * k, m.u32(e + 0x228 + 4 * k));
    if (m.s32(e + 0x228) > 0) m.w32(e + 0x21C, iabs(m.s32(e + 0x21C)));
}

}  // namespace

void projectile_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    I life = (m.u16(o + 0x3A) - 1) & 0xFFFF;
    m.w16(o + 0x3A, life);
    if (!life && m.s32(e + 8)) {
        eng.remove(e, "projectile life");
        return;
    }
    I c = m.u8(o + 0x40);
    if (c) {
        c--;
        m.w8(o + 0x40, c);
        if (!c) m.w32(e + 0x14, m.u32(o + 0x3C));
    }
    parts_box(m, e);
    if (m.s32(e + 8) == 0x13F3) grenade_soldier_bits(eng, e);
    if (m.u16(o + 0x38) & 0x40) {
        I t = m.u8(o + 0x41);
        if (t) {
            t--;
            m.w8(o + 0x41, t);
            if (!t) {
                if (m.u32(o + 0x44))
                    eng.note("projectile fuse callback (obj+0x44) not implemented");
                else if (m.u16(o + 0x38) & 0x100) {
                    // the fuse: the explosion (8, 10, 85), sparks, the sound
                    for (int k = 0; k < 3; k++) m.w16(e + 0x364 + 2 * k, 0);
                    explosion_spawn(eng, e, 8, 0xA, 0x55, 0x10000, 0x1000, 0x40000, 5, 0);
                    I n = ((eng.rand.draw(FN("grenade")) * 5) >> 15) + 0xF;
                    V3 pos = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
                    particles_spawn(eng, pos, 5, n, 0x28, 0x19, 0x50, 0x1E, 0x3FF, 0x3F, 0x3E0, 0xFF, 1, 0x10000, 0, 0);
                    explosion_sound(eng, e);
                }
                eng.remove(e, "fuse");
                return;
            }
        }
    }
    U att = eng.resolve(m.u32(e + 0x30));
    if (att && m.s8(e + 0x34) >= 0) {
        // held on a bone (entities_update places it): no velocity, no spin, behaviour & 0xFDF6
        for (U k : {0x23Cu, 0x238u, 0x234u, 0x220u, 0x21Cu, 0x218u, 0x200u}) m.w32(e + k, 0);
        m.w16(o + 0x38, m.u16(o + 0x38) & 0xFDF6);
        return;
    }
    for (int k = 0; k < 3; k++) m.w32(o + 0x68 + 4 * k, S32(m.s32(o + 0x68 + 4 * k) + m.s32(o + 0x74 + 4 * k)));
    I f = m.u16(o + 0x38);
    if (f & 0x400) rolling(eng, e, o, f);
    f = m.u16(o + 0x38);
    if (f & 1) {
        if (!(f & 2))
            m.w32(e + 0x21C, S32(m.s32(e + 0x21C) + m.s32(o + 0x34)));
        else
            m.w16(o + 0x38, f & 0xFFFD);
    } else if (f & 2)
        m.w16(o + 0x38, f & 0xFFFD);
    if (m.u16(o + 0x38) & 0x10) eng.note("projectile smoke trail (0x10) not implemented");
    m.w32(o + 0x80, m.u32(e + 0x200));
}

I generic_move(Engine& eng, U e) {
    Mem& m = eng.m;
    I pos0[3];
    for (int k = 0; k < 3; k++) {
        pos0[k] = m.s32(e + 0x20C + 4 * k);
        m.w32(e + 0x258 + 4 * k, pos0[k]);
        m.w32(e + 0x264 + 4 * k, m.u32(e + 0x228 + 4 * k));
    }
    I ground0 = m.s32(e + 0x284);
    I res = 0;
    for (int k = 0; k < 3; k++) m.w32(e + 0x228 + 4 * k, S32(m.s32(e + 0x228 + 4 * k) + m.s32(e + 0x234 + 4 * k)));
    U inst = U(m.u32(e + 0x100));
    if (inst && m.s16(inst + 0x5E) >= 0 && m.s16(inst + 0x5C) >= 0) eng.note("generic entity_move root turn not implemented");
    if (m.s32(e + 0x1FC)) m.w32(e + 0x200, imin(m.s32(e + 0x204), S32(m.s32(e + 0x200) + m.s32(e + 0x1FC))));
    m.w32(e + 0x220, m.u32(e + 0x200));
    I fa = m.u32(e + 0xC);
    if (!m.s32(e + 0x284) && (fa & 0x20)) {
        I vy = S32(m.s32(e + 0x21C) + 0x20000);
        m.w32(e + 0x21C, imin(vy, 0x4B0000));
    }
    I sc = m.s32(e + 0x224);
    V3 v;
    if (sc == 0x10000)
        for (int k = 0; k < 3; k++) v[k] = S16(m.s32(e + 0x218 + 4 * k) >> 12);
    else
        for (int k = 0; k < 3; k++) v[k] = S16((m.s16(e + 0x21A + 4 * k) * sc) >> 12);
    M3 M = rot_yxz3(S16(m.s32(e + 0x228) >> 16), S16(m.s32(e + 0x22C) >> 16), S16(m.s32(e + 0x230) >> 16));
    V3 mac = gmul_mac(M, v, {0, 0, 0}, 12);
    for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(m.s32(e + 0x20C + 4 * k) + S32(mac[k] << 12) + m.s32(e + 0x1F0 + 4 * k)));
    I vy16 = mac[1] >> 4;
    U world = U(m.u32(G_WORLD));
    if (world && (m.u32(e + 0x14) & m.u32(world + 0xC))) {
        I fb = m.u32(e + 0x10);
        bool walled = false;
        if ((fa & 8) && !(fa & 1) && !(fb & 1)) {
            if (!(fb & 0x44))
                eng.note("generic entity_move: walker_slide for a non-walker not implemented");
            else {
                I nw[3], old[3], point[3], normal[3];
                SweepFace face;
                for (int k = 0; k < 3; k++) nw[k] = m.s32(e + 0x20C + 4 * k) >> 16, old[k] = pos0[k] >> 16;
                if (eng.world.sweep(nw, old, point, normal, &face)) {
                    walled = true;
                    write_contact(m, e, point, normal, face);
                    if (normal[1] >= 0x801 && vy16 < 0 && m.s32(e + 0x21C) > 0) {
                        m.w32(e + 0x21C, 0);
                        m.w32(e + 0x210, m.u32(e + 0x3C0));
                    }
                    res |= 2;
                    m.w32(e + 0x218, 0);
                    m.w32(e + 0x220, 0);
                    for (int k = 0; k < 3; k++) m.w16(e + 0x364 + 2 * k, normal[k]);
                    m.w32(e + 0x38, m.u32(e + 0x38) | 0x8000);
                    if (!(fa & 0x80)) {
                        for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(point[k] << 16));
                    } else {
                        // a bouncer: back to the old x, z plus the normal << 5, rounded to units
                        for (int k : {0, 2})
                            m.w32(e + 0x20C + 4 * k, S32(pos0[k] + (((normal[k] << 5) + 0x8000) & 0xFFFF0000)));
                    }
                }
            }
        }
        res = floor_step(eng, e, res, ground0, vy16, walled);
    }
    for (int k = 0; k < 3; k++) {
        m.w32(e + 0x1F0 + 4 * k, 0);
        m.w32(e + 0x3BC + 4 * k, m.u32(e + 0x20C + 4 * k));
    }
    return res;
}

I projectile_world_contact(Engine& eng, U e, I res) {
    Mem& m = eng.m;
    I fb = m.u32(e + 0x10);
    U o = U(m.u32(e + 0x388));
    if (fb & 4) {
        if (!(fb & 8)) {
            m.w32(e + 0x10, fb | 8);
            m.w32(o + 0x5C, res);
            return 1;
        }
        m.w32(e + 0x10, fb & ~I(8));
        res = m.s32(o + 0x5C);
        if (fb & 0x20) {
            if (decal(eng, e + 0x318))
                for (int k = 0; k < 3; k++) m.w32(e + 0x20C + 4 * k, S32(m.s16(e + 0x318 + 2 * k) << 16));
            if (!(m.u16(o + 0x38) & 0x200)) {
                I n = ((eng.rand.draw(FN("grenade")) * 3) >> 15) + 2;
                I smoke = ((eng.rand.draw(FN("grenade")) * 4) >> 15) + 6;
                V3 pos = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
                particles_spawn(eng, pos, 1, n, 0x1E, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke);
            }
        }
    }
    I f = m.u16(o + 0x38);
    if ((res & 3) || (f & 0x200)) {
        m.w16(o + 0x38, f | 2);
        f |= 2;
        if ((f & 4) && (res & 1))
            m.w32(e + 0xC, m.u32(e + 0xC) & ~I(0x80));
        else if ((f & 0x208) && !(f & 0x400))
            bounce(eng, e, o, res);
        f = m.u16(o + 0x38);
        if ((f & 0x10) && (res & 1)) eng.note("projectile smoke trail (0x10) on the ground not implemented");
        if (f & 0x200) bounce_turn(eng, e, o, res);
    }
    f = m.u16(o + 0x38);
    if (!(f & 0x40)) {
        if (f & 0x100) {
            // an explosive round or a rocket hitting the world: the explosion (10, 12, 130)
            explosion_spawn(eng, e, 0xA, 0xC, 0x82, 0x10000, 0x1000, 0x40000, 5, 0);
            I n = ((eng.rand.draw(FN("grenade")) * 5) >> 15) + 0xF;
            V3 pos = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
            particles_spawn(eng, pos, 5, n, 0x28, 0x19, 0x50, 0x1E, 0x3FF, 0x3F, 0x3E0, 0xFF, 1, 0x17FFF, 0, 0);
            explosion_sound(eng, e);
        } else
            impact_sound(eng, e);
        I y = S32(m.s32(e + 0x22C) + ((((eng.rand.draw(FN("grenade")) & 0x3FF) - 0x200) << 16) + 0x08000000));
        if (y > 0x10000000) y -= 0x10000000;
        m.w32(e + 0x22C, y);
        eng.remove(e, "projectile hit the world");
    }
    return 1;
}

void impact_effect_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    I t = m.u8(o + 1);
    if (!t) {
        eng.remove(e, "impact effect done");
        return;
    }
    t--;
    m.w8(o + 1, t);
    if (t < 3)
        for (I i = 0; i < m.u8(o); i++) {
            U q = U(o + 0x10 + 0x24 * i);
            for (U off : {0x1Cu, 0x1Du, 0x1Eu, 0x20u, 0x21u, 0x22u}) m.w8(q + off, m.u8(q + off) >> 1);
        }
}

U particles_spawn(Engine& eng, const V3& pos, I spread, I n, I vel_xz, I vel_y, I col, I col_rand, I a7, I a8, I a9, I a10, I a11,
                  I a12, I smoke_on, I smoke_n) {
    Mem& m = eng.m;
    I size = n * 0x34 + 0x64 + (smoke_on ? smoke_n * 0x34 : 0);
    U p = eng.entity_create(0x31, size);
    if (!p) return 0;
    U o = U(m.u32(p + 0x388));
    auto rd = [&]() { return eng.rand.draw({0x80073BA8}); };
    m.w32(p + 8, 0x31);
    m.w32(p + 0x294, 0xB4);
    m.w32(p + 0x290, 0xB4);
    m.w32(p + 0x36C, PARTICLES_THINK);
    m.w32(p + 0x370, PARTICLES_DRAW);
    m.w32(p + 0x38, 0);
    m.w32(p + 0xC, 0x01028000);
    m.w32(p + 0x374, 0);
    m.w32(p + 0x378, 0);
    m.w32(p + 0x384, NO_BRAIN);
    m.w32(p + 0x284, 0);
    m.w32(p + 0x10, m.u32(p + 0x10) | 0x203);
    for (int k = 0; k < 3; k++) {
        m.w32(p + 0x20C + 4 * k, pos[k]);
        m.w32(p + 0x258 + 4 * k, pos[k]);
    }
    U arr = o + 0x64;
    U sa = o + 0xC;
    m.w32(o, 0);
    m.w32(o + 4, 0);
    m.w32(sa + 0x28, arr);
    m.w32(sa + 4, n);
    m.w32(sa, 1);
    m.w32(sa + 0xC, 0x20000);
    m.w32(sa + 0x10, 0x20000);
    m.w32(sa + 0x14, 0x4B0000);
    m.w32(sa + 8, a12);
    m.w16(sa + 0x18, a7);
    m.w16(sa + 0x1A, a8);
    m.w16(sa + 0x1C, a9);
    m.w16(sa + 0x1E, a10);
    m.w16(sa + 0x20, a11);
    m.w16(sa + 0x22, a11);
    m.w16(sa + 0x24, 8);
    m.w16(sa + 0x26, 0);
    I s5 = spread * 2, s4 = vel_y * 2;
    for (I i = 0; i < n; i++) {
        U q = U(arr + 0x34 * i);
        m.w8(q, 1);
        m.w8(q + 1, 0);
        m.w8(q + 2, ((rd() * 0xA) >> 15) + 0x19);
        for (int k = 0; k < 3; k++) {
            I c = m.s32(p + 0x20C + 4 * k);
            m.w32(q + 4 + 4 * k, s5 == 0 ? S32(c + (-spread << 16)) : S32(c + ((-spread + ((rd() * s5) >> 15)) << 16)));
        }
        for (int k = 0; k < 3; k++) m.w32(q + 0x10 + 4 * k, s4 == 0 ? S32(-vel_xz << 16) : S32((-vel_xz + ((rd() * s4) >> 15)) << 16));
        for (int k = 0; k < 3; k++) m.w16(q + 0x1C + 2 * k, rd() >> 3);
        m.w16(q + 0x24, 0x96);
        m.w16(q + 0x26, 0x64);
        m.w16(q + 0x28, 0xC8);
        I c = col;
        if (col_rand) c += (rd() * col_rand) >> 15;
        for (int k = 0; k < 3; k++) m.w8(q + 0x2C + k, c);
        m.w8(q + 3, ((rd() * 100) >> 15) >= 0x33 ? 1 : 0);
        m.w16(q + 0x30, ((rd() * 7) >> 15) + 5);
        m.w16(q + 0x32, ((rd() * 7) >> 15) + 5);
    }
    U arr2 = U(arr + 0x34 * n);
    U sb = o + 0x38;
    if (smoke_on) {
        m.w32(sb + 4, smoke_n);
        m.w32(sb + 0x28, arr2);
        m.w32(sb, 1);
        m.w32(sb + 8, a12);
        m.w32(sb + 0x10, S32(-4));
        m.w32(sb + 0x14, 0x960000);
        m.w16(sb + 0x18, 0x3E8);
        m.w16(sb + 0x1C, 0x3E0);
        m.w16(sb + 0x1E, 0xFA);
        m.w16(sb + 0x20, 0x1F);
        m.w16(sb + 0x22, 0x1F);
        m.w16(sb + 0x1A, 0);
        m.w16(sb + 0x24, 8);
        m.w16(sb + 0x26, 2);
        m.w32(sb + 0xC, 2);
        for (I i = 0; i < smoke_n; i++) {
            U q = U(arr2 + 0x34 * i);
            m.w8(q, 1);
            m.w8(q + 1, i);
            m.w8(q + 2, ((rd() * 2) >> 15) + 0x14);
            for (int k = 0; k < 3; k++) m.w32(q + 4 + 4 * k, m.u32(p + 0x20C + 4 * k));
            m.w32(q + 0x10, 0);
            m.w32(q + 0x14, S32((((-(rd() * 2)) >> 15) - 2) << 16));
            m.w16(q + 0x30, 5);
            m.w32(q + 0x18, 0);
            for (U off : {0x1Cu, 0x1Eu, 0x20u, 0x24u, 0x26u, 0x28u}) m.w16(q + off, 0);
            m.w16(q + 0x32, 6);
        }
    } else {
        m.w32(sb + 4, 0);
        m.w32(sb + 0x28, 0);
        m.w32(sb, 2);
    }
    eng.link_front(p);
    return p;
}

void particles_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    for (auto [flag, sys] : {std::pair<U, U>{o, o + 0xC}, {o + 4, o + 0x38}}) {
        if (m.s32(sys) == 2)
            m.w32(flag, 1);
        else
            particles_step(m, sys);
    }
    if (m.s32(o + 4) && m.s32(o)) eng.remove(e, "particles done");
}

U casing_spawn(Engine& eng, U owner, const V3& pos, I pitch) {
    Mem& m = eng.m;
    if (m.u32(G_CASINGS) >= 5) return 0;
    U p = eng.entity_create(0x66, 0xC, 0, 0, -1, 0, 0, true);
    if (!p) return 0;
    U o = U(m.u32(p + 0x388));
    m.w32(p + 8, 0x66);
    m.w32(p + 0xC, 0x07020000);
    m.w32(p + 0x10, 0x213);
    m.w32(p + 0x36C, CASING_THINK);
    for (U off : {0x374u, 0x370u, 0x378u, 0x37Cu, 0x14u}) m.w32(p + off, 0);
    m.w32(p + 0x384, NO_BRAIN);
    for (int k = 0; k < 3; k++) m.w32(p + 0x20C + 4 * k, pos[k]);
    m.w32(o, (eng.rand.draw(FN("casing")) & 0xF) + 8);
    handle_store(m, o + 4, owner);
    m.w32(o + 8, pitch);
    m.w32(G_CASINGS, m.u32(G_CASINGS) + 1);
    eng.link_front(p);
    return p;
}

void casing_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    I t = S32(m.s32(o) - 1);
    m.w32(o, t);
    if (t == 0) {
        I snd = (eng.rand.draw(FN("casing")) & 1) | 0x10;
        eng.audio->play(0, snd, e, 0xCCCC, 0, m.s32(o + 8), 0x199B, 0, 0, 0, 0);
        m.w32(G_CASINGS, m.u32(G_CASINGS) - 1);
        eng.remove(e, "casing landed");
    }
}

static bool projectile_type(I t) { return t >= 0x13F0 && t < 0x13FA; }

I bullet_touch(Engine& eng, U e, U other) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    if (handle_same(eng, o + 0x54, other) || projectile_type(m.s32(other + 8))) return 0;
    bool swept = m.u32(e + 0x10) & 4;
    I point[3], normal[3], fl;
    bool hit;
    V3 nrm{};
    if (swept)
        hit = projectile_planes(eng, e, other, point, normal, fl);
    else {
        // a sphere (grenades): sphere_vs_object_planes, its normal kept
        hit = sphere_vs_object_planes(eng, e, other, &nrm) != 0;
    }
    if (!hit) return 0;
    I res = 0;
    if (m.s32(other + 8) != 0x1777) {
        res = 1;
        if (swept)
            for (int k = 0; k < 3; k++) {
                m.w32(e + 0x20C + 4 * k, S32(point[k] << 16));
                m.w16(e + 0x364 + 2 * k, S16(-normal[k]));
            }
        else
            for (int k = 0; k < 3; k++) m.w16(e + 0x364 + 2 * k, nrm[k]);
        if (m.u16(o + 0x38) & 0x100) {
            for (int k = 0; k < 3; k++) m.w16(e + 0x364 + 2 * k, 0);
            U x = explosion_spawn(eng, e, 6, 6, 0x4B, 0x10000, 0x1000, 0x40000, 5, 0);
            if (x && (m.u32(other + 0xC) & 0x10000000)) m.w32(x + 0x10, m.u32(x + 0x10) | 0x100000);
            explosion_sound(eng, e);
            eng.remove(e, "explosive hit on an object");
            return 1;
        }
        if (m.u32(other + 0x10) & 0x4000) {
            impact_sound(eng, e);
            I n = ((eng.rand.draw(FN("hit")) * 3) >> 15) + 2;
            I smoke = ((eng.rand.draw(FN("hit")) * 4) >> 15) + 6;
            V3 pos = {m.s32(e + 0x20C), m.s32(e + 0x210), m.s32(e + 0x214)};
            particles_spawn(eng, pos, 1, n, 0x1E, 0x19, 0xF, 0x64, 0x3F4, 0xE0, 0x3E0, 0xFE, 0xF, 0x10000, 1, smoke);
        }
        if (m.s32(other + 8) == 0x1402) {
            I snd = (eng.rand.draw(FN("hit")) >> 15) + 0xC;
            eng.audio->play(0, snd, e, 0x20000, 0, 0x10000, 0x4CCD, 0, 0, 0, 0);
        }
        eng.remove(e, "bullet hit an object");
    }
    if (swept) call_touch(eng, U(m.u32(other + 0x37C)), other, e);
    return res;
}

I projectile_hit_part(Engine& eng, U e, I part, U other, I opart) {
    Mem& m = eng.m;
    U o = U(m.u32(e + 0x388));
    if (handle_same(eng, o + 0x54, other)) return 0;
    I t = m.s32(other + 8);
    if (projectile_type(t) || t == 0x25) return 0;
    damage_from_hit(eng, e, part, other, opart);
    if (m.u16(o + 0x38) & 0x100) eng.note("explosive round's hit not implemented");
    if (t == 0x1402) {
        I snd = (eng.rand.draw(FN("hit")) >> 15) + 0xC;
        eng.audio->play(0, snd, e, 0x20000, 0, 0x10000, 0x4CCD, 0, 0, 0, 0);
    }
    eng.remove(e, "bullet hit");
    return 1;
}

}  // namespace valor::sim
