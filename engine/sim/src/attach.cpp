// A soldier's attachments and the rest of its think (docs/re/enemy_types.md, Attachments; enemies.md,
// The think): tools/sim/engine_attach.py.
#include <algorithm>

#include "valor/sim/audio.h"
#include "valor/sim/engine.h"
#include "valor/sim/load.h"
#include "valor/sim/objects.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {
constexpr U TEMPLATES = 0x800980B0;
constexpr U G_ENEMY_WEAPONS = 0x80097FF0;
constexpr U G_RANGE_THIRD_SET = 0x80099B2C;
constexpr U G_RANGE_THIRD = 0x800AB930;
constexpr U G_BLOOD = 0x80093A05;
constexpr U PICKUPS = 0x8009E8E8;
constexpr U PICKUP_THINK = 0x80082E88;
constexpr U NO_MODEL_SPECIAL[] = {0x37C82DE4, 0x08E34FAC, 0x25C50BD4, 0x6C8F8AA8, 0x933987E0, 0x96593A51};
constexpr U G_SLOT_COUNT = 0x8009E874;
constexpr U G_SLOT_WEAPON = 0x8009E878;
constexpr U G_WEAPONS = 0x80093640;

}  // namespace

U attach_template(I k) { return U(TEMPLATES + 0x3C * k); }
U attach_entry(Mem& m, U e, I k) { return U(m.u32(e + 0x388) + 0x1C + 0x144 * k); }

// func_8006B1C4 -> model_instance_init: a model on an attachment, its block from the game's heap
// as the flags ask (0 when refused).
I instance_set(Engine& eng, U inst, U h, I flags) {
    Mem& m = eng.m;
    U model = model_by_hash(eng, h);
    if (!model) {
        m.w32(inst, 0);
        return 0;
    }
    if (eng.gheap) return eng.gheap->instance_init(inst, model, flags, 0) ? 1 : 0;
    m.w32(inst + 4, flags);
    m.w32(inst + 8, model);
    m.w32(inst + 0x2C, m.u32(model + 0x88));
    return 1;
}

// func_8006B234 (no entity): its +4 bit 8 given back, the block freed now or after delay frames.
void instance_release(Engine& eng, U inst, I delay) {
    if (eng.gheap) {
        eng.gheap->instance_release(inst, 0, delay);
        return;
    }
    Mem& m = eng.m;
    I f = m.u32(inst + 4);
    if (f & 8) m.w32(inst + 4, f & ~I(8));
    m.w32(inst, 0);
}

I m_hash(Engine& eng, U B) {
    U model = U(eng.m.u32(B + 0x94 + 8));
    return model ? eng.m.u32(model) : 0;
}

void swap_model(Engine& eng, U B, U h) {
    if (h && m_hash(eng, B) != h) {
        instance_release(eng, B + 0x94);
        instance_set(eng, B + 0x94, h, 0x156);
    }
}

void angles_from_matrix(Engine& eng, U B) {
    Mem& m = eng.m;
    M3 M;
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) M[i][k] = m.s16(B + 0x74 + 6 * i + 2 * k);
    M3 T = transpose(M);
    I v = eng.gte.sqrt12(int32_t(((T[1][1] * T[1][1]) >> 12) + ((T[0][1] * T[0][1]) >> 12)));
    I a[3];
    if (v) {
        a[0] = eng.gte.ratan2(int32_t(-T[2][1]), int32_t(v));
        a[1] = eng.gte.ratan2(int32_t(T[2][0]), int32_t(T[2][2]));
        a[2] = eng.gte.ratan2(int32_t(T[0][1]), int32_t(T[1][1]));
    } else {
        a[0] = eng.gte.ratan2(int32_t(-T[2][1]), 0);
        a[1] = eng.gte.ratan2(int32_t(-T[0][2]), int32_t(T[0][0]));
        a[2] = 0;
    }
    for (int k = 0; k < 3; k++) m.w32(B + 0x60 + 4 * k, S32(a[k] << 16));
}

void knock_off(Engine& eng, U B, U player, I speed, I vy, I ticks, I drag, I gravity, I bounces, I spin, I action) {
    Mem& m = eng.m;
    auto rd = [&]() { return eng.rand.draw({0x80051200}); };
    I act = action & 0xE0000000;
    V3 v;
    if (act & 0x20000000) {
        v = {0, 0, 0};
        m.w32(B + 0xC, m.s32(B + 0xC) - 1);
    } else if (speed == -1) {
        for (int k = 0; k < 3; k++) v[k] = S32(m.s32(B + 0x3C + 4 * k) - m.s32(B + 0x48 + 4 * k));
        vy = v[1];
    } else {
        I sp = speed >> 16;
        if (act & 0x80000000) {
            I vx = sp * 2 ? (S32(rd() * 2 * sp) >> 15) - sp : -sp;
            I vz = sp * 2 ? (S32(rd() * 2 * sp) >> 15) - sp : -sp;
            v = {S32(vx << 16), vy, S32(vz << 16)};
        } else
            v = {S32(((m.s16(player + 0x344) * sp) >> 12) << 16), vy, S32(((m.s16(player + 0x350) * sp) >> 12) << 16)};
    }
    m.w32(B + 0x30, v[0]);
    m.w32(B + 0x34, (act & 0x20000000) || speed == -1 ? v[1] : vy);
    m.w32(B + 0x38, v[2]);
    if (!(act & 0x20000000))
        for (U o : {0x54u, 0x58u, 0x5Cu}) m.w32(B + o, spin * 2 ? S32(-spin + (S32(rd() * 2 * spin) >> 15)) : S32(-spin));
    m.w32(B + 4, ticks);
    m.w32(B + 0x1C, drag);
    m.w32(B + 0x20, gravity);
    m.w32(B + 0x10, 0);
    m.w32(B + 0xC, bounces);
}

I wrap_angle(I v) {
    while (v < -0x8000000) v += 0x10000000;
    while (v > 0x8000000) v -= 0x10000000;
    return v;
}

namespace {
I point_floor(Engine& eng, U p) {
    int32_t q[3] = {int32_t(eng.m.s16(p + 2)), int32_t(eng.m.s16(p + 6)), int32_t(eng.m.s16(p + 10))};
    return eng.world.w.point_floor(q);
}

}  // namespace

I fall_step(Engine& eng, U B, I feet) {
    Mem& m = eng.m;
    for (int k = 0; k < 3; k++) m.w32(B + 0x48 + 4 * k, m.u32(B + 0x3C + 4 * k));
    I t = S32(m.s32(B + 4) - 1);
    m.w32(B + 4, t);
    if (t <= 0) {
        I f = point_floor(eng, B + 0x3C);
        m.w32(B + 0x40, S32((f << 16) - 0xF0000));
        for (U o : {0x60u, 0x64u, 0x68u}) m.w32(B + o, wrap_angle(m.s32(B + o)));
        m.w32(B + 4, 0);
        m.w32(B, (m.u32(B) | 0x14) & ~I(2));
        return 0;
    }
    for (auto [a, s] : {std::pair<U, U>{0x60, 0x54}, {0x68, 0x5C}, {0x64, 0x58}}) m.w32(B + a, S32(m.s32(B + a) + m.s32(B + s)));
    I drag = m.s32(B + 0x1C);
    I vx = m.s32(B + 0x30), vz = m.s32(B + 0x38);
    m.w32(B + 0x3C, S32(m.s32(B + 0x3C) + (vx > 0 ? vx - drag : vx + drag)));
    m.w32(B + 0x40, S32(m.s32(B + 0x40) + m.s32(B + 0x34)));
    m.w32(B + 0x44, S32(m.s32(B + 0x44) + (vz > 0 ? vz - drag : vz + drag)));
    if (point_floor(eng, B + 0x3C) == -0x7FFF) {
        for (int k = 0; k < 3; k++) m.w32(B + 0x3C + 4 * k, m.u32(B + 0x48 + 4 * k));
        m.w32(B + 0x30, S32(-m.s32(B + 0x30)));
        m.w32(B + 0x38, S32(-m.s32(B + 0x38)));
    }
    I vyy = S32(m.s32(B + 0x34) + m.s32(B + 0x20));
    m.w32(B + 0x34, imin(vyy, 0x4B0000));
    I bounced = 0;
    if (feet < m.s32(B + 0x40)) {
        m.w32(B + 0x40, feet);
        if (m.s32(B + 0x10) < m.s32(B + 0xC)) {
            bounced = 1;
            m.w32(B + 0x20, m.s32(B + 0x20) >> 1);
            m.w32(B + 0x34, S32(-(m.s32(B + 0x34) >> 2)));
            m.w32(B + 0x30, m.s32(B + 0x30) >> 2);
            m.w32(B + 0x54, S32(-(m.s32(B + 0x54) >> 2)));
            m.w32(B + 0x38, m.s32(B + 0x38) >> 2);
            m.w32(B + 0x1C, m.s32(B + 0x1C) >> 2);
            m.w32(B + 0x58, m.s32(B + 0x58) >> 2);
            m.w32(B + 0x5C, S32(-(m.s32(B + 0x5C) >> 2)));
            m.w32(B + 0x10, m.s32(B + 0x10) + 1);
        } else
            m.w32(B + 4, 0);
        if (iabs(m.s32(B + 0x30)) <= 0xFFFF && iabs(m.s32(B + 0x38)) <= 0xFFFF) m.w32(B + 4, 0);
    }
    return bounced;
}

void attachments_init(Engine& eng, U e, const std::vector<U>& hashes) {
    Mem& m = eng.m;
    U obj = U(m.u32(e + 0x388));
    m.w32(obj + 0x18, 0);
    for (size_t k = 0; k < hashes.size(); k++) {
        U h = hashes[k];
        if (!h || k == 3) break;
        m.w32(obj + 0x18, m.u32(obj + 0x18) + 1);
        U B = U(obj + 0x1C + 0x144 * k);
        if (h != 0xFFFFFFFF) instance_set(eng, B + 0x94, h, 0x157);
        m.w32(B + 0xE8, m.u32(e));
        m.w32(B + 0xEC, m.u32(e + 0x100));
        for (int i = 0; i < 3; i++) m.w32(B + 0x24 + 4 * i, m.u32(e + 0x270 + 4 * i));
        m.w32(B + 8, 0);
        m.w32(B, 0);
    }
}

void attachment_draw(Engine& eng, U e, U B, I bone) {
    Mem& m = eng.m;
    U inst = U(m.u32(e + 0x100));
    for (int k = 0; k < 3; k++) m.w32(B + 0x48 + 4 * k, m.u32(B + 0x3C + 4 * k));
    if (!m.u32(inst + 0x84)) {
        eng.note("attachment on a model without joints not implemented");
        return;
    }
    for (int k = 0; k < 3; k++) m.w32(B + 0x60 + 4 * k, m.u32(e + 0x228 + 4 * k));
    U qp = U(m.u32(inst + 0x88));
    if (!qp) return;
    M3 R = quat_m3(m.s16(qp + 8 * bone), m.s16(qp + 8 * bone + 2), m.s16(qp + 8 * bone + 4), m.s16(qp + 8 * bone + 6));
    U jp = U(m.u32(inst + 0x94));
    V3 j = {m.s16(jp + 8 * bone), m.s16(jp + 8 * bone + 2), m.s16(jp + 8 * bone + 4)};
    V3 off = {m.s16(B + 0x14), m.s16(B + 0x16), m.s16(B + 0x18)};
    V3 mac = gmul_mac(R, off, j, 12);
    M3 E;
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) E[i][k] = m.s16(e + 0x340 + 6 * i + 2 * k);
    V3 w = apply_lv(E, mac);
    V3 t;
    for (int k = 0; k < 3; k++) t[k] = S32(w[k] + m.s32(e + 0x354 + 4 * k));
    M3 RR;
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) RR[i][k] = S16((E[i][0] * R[0][k] + E[i][1] * R[1][k] + E[i][2] * R[2][k]) >> 12);
    for (int k = 0; k < 3; k++) m.w32(B + 0x3C + 4 * k, S32(t[k] << 16));
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) m.w16(B + 0x74 + 6 * i + 2 * k, RR[i][k]);
    m.w32(B + 0x84, S32(RR[2][2]));
    for (int k = 0; k < 3; k++) m.w32(B + 0x88 + 4 * k, t[k]);
}

void attachments_update(Engine& eng, U e) {
    Mem& m = eng.m;
    U obj = U(m.u32(e + 0x388));
    for (I k = 0; k < m.s32(obj + 0x18); k++) {
        U B = U(obj + 0x1C + 0x144 * k), T = attach_template(k);
        I f = m.u32(B);
        if (f & 2) {
            if (fall_step(eng, B, m.s32(e + 0x210))) {
                I snd = m.s32(T + 0x24);
                if (snd != -1) {
                    I att = imax(m.s32(B + 0x10) - 1, 0);
                    if (snd >= 0) {
                        if (m.s32(T + 0x28)) snd += (eng.rand.draw(FN("enemy_aim")) * m.s32(T + 0x28)) >> 15;
                        I bank = m.s32(T + 0x2C);
                        if (bank < 0) bank = m.u16(m.u32(m.u32(e + 0x100) + 8) + 0xC8);
                        eng.audio->play(S16(bank), snd, e, 0x10000 >> att, 0, 0x10000, 0x1800, 0, 0, 0, 0);
                    }
                }
            }
        } else if ((f & 1) && m.s32(e + 0x4C + 4 * 0xD) == -1) {
            swap_model(eng, B, U(m.u32(T + 0x20)));
            m.w32(B + 0xE8, 0);
            m.w32(B + 0xEC, 0);
            m.w32(B, m.u32(B) | 8);
        }
        if (m.u32(B) & 0x10) {
            bool done = true;
            I ax = m.s32(T + 0x30);
            I x = m.s32(B + 0x60);
            if (x < 0 && x < S32(0xFC000000))
                ax = S32(ax - 0x08000000);
            else if (x > 0x04000000)
                ax = S32(ax + 0x08000000);
            if (x != ax) {
                done = false;
                I x0 = m.s32(B + 0x60);
                x = ax < x ? S32(x - 0x02240000) : S32(x + 0x02240000);
                if (ax < x0 ? (x < ax) : (ax < x)) x = ax;
                m.w32(B + 0x60, x);
            }
            I z = m.s32(B + 0x68);
            I az = m.s32(T + 0x38);
            if (z < 0) az = -az;
            if (z != az) {
                done = false;
                I z2 = az < z ? S32(z - 0x02240000) : S32(z + 0x02240000);
                if (az < z ? (z2 < az) : (az < z2)) z2 = az;
                m.w32(B + 0x68, z2);
            }
            if (done) {
                I fl = m.u32(B);
                m.w32(B, fl & ~I(0x10));
                U tp = m.u32(e + 0x1C) ? U(m.u32(m.u32(e + 0x1C) + 4)) : 0;
                if ((fl & 0x40) && tp && m.u32(tp + 0x7C)) {
                    I w = m.s32(e + 0x4C + 4 * 0xF) & 0xFF;
                    m.w32(B, (m.u32(B) & ~I(1)) | 0x20);
                    drop_pickup(eng, e, B + 0x3C, B + 0x60, U(m.u32(G_ENEMY_WEAPONS + 0x30 * w)), w);
                }
            }
        }
    }
}

void attachments_op(Engine& eng, U e, I ops, I arg) {
    Mem& m = eng.m;
    U obj = U(m.u32(e + 0x388));
    for (I k = 0; k < m.s32(obj + 0x18); k++) {
        if (!((ops >> k) & 1)) continue;
        U B = U(obj + 0x1C + 0x144 * k), T = attach_template(k);
        I f = m.u32(B);
        if (f & 6) continue;
        if (ops & 0x10000000) {
            m.w32(B + 8, arg);
            continue;
        }
        if (ops & 0x08000000) {
            m.w32(B, (f & ~I(1)) | 0x20);
            continue;
        }
        if (ops & 0x04000000) {
            m.w32(B, (f | 1) & ~I(0x20));
            continue;
        }
        if ((ops & 0x20000000) && k == 2) {
            if (f & 1) {
                eng.note("grenade drop (grenade_spawn) not implemented");
                m.w32(B, (f | 0x20) & ~I(1));
            }
            continue;
        }
        if (ops & 0x03000000) {
            if ((f & 1) && m_hash(eng, B) == 0x61282777) {
                eng.note("grenade throw/lob (grenade_spawn) not implemented");
                m.w32(B, (f | 0x20) & ~I(1));
            }
            continue;
        }
        if (!(f & 0x20)) {
            if (m.s32(B + 8) == 0) {
                U q = U(m.u32(e + 0x304) + 0x48 * arg);
                for (int i = 0; i < 3; i++) m.w32(B + 0x3C + 4 * i, S32(m.s32(q + 0x20 + 4 * i) << 16));
            } else {
                angles_from_matrix(eng, B);
                m.w32(B + 8, 0);
            }
            swap_model(eng, B, U(m.u32(T + 0x20)));
            knock_off(eng, B, U(m.u32(G_SOLDIER)), m.s32(T + 0x10), m.s32(T + 0x14), m.s32(T), m.s32(T + 8), m.s32(T + 0xC),
                      m.s32(T + 4), m.s32(T + 0x1C), ops);
            m.w32(B + 0xE8, 0);
            m.w32(B + 0xEC, 0);
            m.w32(B, m.u32(B) | 3);
        }
    }
}

U drop_pickup(Engine& eng, U e, U pos, U ang, U model_hash, I w) {
    Mem& m = eng.m;
    U tp = U(m.u32(m.u32(e + 0x1C) + 4));
    U kind_hash = U(m.u32(tp + 0x7C));
    PickupRec rec;
    rec.flags58 = 1, rec.cull = 0x96, rec.count = m.u8(tp + 0x62), rec.w = w, rec.flags30 = 0;
    if (std::find(std::begin(NO_MODEL_SPECIAL), std::end(NO_MODEL_SPECIAL), kind_hash) != std::end(NO_MODEL_SPECIAL)) {
        rec.pos[0] = m.s16(e + 0x20E), rec.pos[1] = m.s16(e + 0x212), rec.pos[2] = m.s16(e + 0x216);
        model_hash = 0xFFFFFFFF;
    } else {
        for (int k = 0; k < 3; k++) rec.pos[k] = m.s16(pos + 4 * k + 2), rec.ang[k] = m.s16(ang + 4 * k + 2);
    }
    U pk = pickup_spawn(eng, rec, kind_hash, model_hash, e);
    if (pk) {
        eng.link_front(pk);
        m.w32(pk + 0x1C, 0);
    }
    return pk;
}

U pickup_spawn(Engine& eng, PickupRec& rec, U kind_hash, U model_hash, U) {
    Mem& m = eng.m;
    I kind = -1;
    for (I i = 0; i < 0x14; i++)
        if (m.u32(PICKUPS + 0x14 * i) == kind_hash) {
            kind = i;
            break;
        }
    if (kind < 0) {
        eng.note("pickup kind unknown");
        return 0;
    }
    U t = U(PICKUPS + 0x14 * kind);
    if (model_hash == 0xFFFFFFFF) model_hash = U(m.u32(t + 4));
    U model = model_by_hash(eng, model_hash);
    if (!model) {
        eng.note("pickup model missing");
        return 0;
    }
    // entity_create(3, ...): room for the model's hit parts, which the pickup then drops (+0x36 0)
    U p = eng.entity_create(0x65, m.u16(t + 8), model, 0, -1, 0, 0, false, 0x157);
    if (!p) return 0;
    m.w16(p + 0x36, 0);
    m.w32(p + 8, 0x65);
    m.w32(p + 0x36C, PICKUP_THINK);
    m.w32(p + 0xC, 0x47028200);
    m.w32(p + 0x10, 0x40211);
    m.w32(p + 0x374, 0);
    m.w32(p + 0x370, 0x8006A5B4);
    m.w32(p + 0x378, 0);
    m.w32(p + 0x384, 0x8006A37C);
    m.w32(p + 0x14, 1);
    m.w32(p + 0x37C, m.u32(t + 0x10));
    for (int k = 0; k < 3; k++) {
        m.w32(p + 0x20C + 4 * k, S32(rec.pos[k] << 16));
        m.w32(p + 0x228 + 4 * k, S32(rec.ang[k] << 16));
    }
    I pp[3] = {m.s32(p + 0x20C), m.s32(p + 0x210), m.s32(p + 0x214)};
    I f = eng.world.floor(pp).height;
    if (f != -0x7FFF) {
        I y = rec.pos[1];
        if (iabs(y - f) < 0x37) {
            rec.pos[1] = f;
            if (rec.addr) m.w16(rec.addr + 0x12, f);
            m.w32(p + 0x210, S32(f << 16));
        }
    }
    m.w16(p + 0x2EC, 0);
    m.w16(p + 0x36, 0);
    I lo[3], hi[3], d[3];
    for (int k = 0; k < 3; k++) {
        lo[k] = m.s16(model + 0x44 + 2 * k), hi[k] = m.s16(model + 0x7C + 2 * k);
        d[k] = iabs(hi[k] - lo[k]) & 0xFFFF;
    }
    I big = imax(d[0], d[1]);
    I r = big < d[2] ? d[2] + 0x1E : big + 0x1E;
    m.w16(p + 0x2E8, r >> 1);
    for (int k = 0; k < 3; k++) m.w16(p + 0x2D8 + 2 * k, (lo[k] + (S32(d[k] << 16) >> 17)) & 0xFFFF);
    if (rec.flags58 & 1) {
        m.w32(p + 0xC, m.u32(p + 0xC) | 0x8000);
        m.w32(p + 0x294, rec.cull);
        m.w32(p + 0x290, rec.cull);
    }
    U o = U(m.u32(p + 0x388));
    m.w32(o, kind);
    m.w32(o + 4, model_hash);
    m.w32(o + 8, 0x384);
    m.w32(o + 0xC, (rec.flags30 & 8) ? rec.w : -1);
    I init = m.u32(t + 0xC);
    if (init == 0x80083058) {
        if (kind == 0)
            m.w32(o + 0x14, 0xD0000);
        else if (kind == 1)
            m.w32(o + 0x14, 0x200000);
        else if (kind == 2)
            m.w32(o + 0x14, 0x400000);
    } else if (init == 0x800830B0)
        m.w32(o + 0x14, rec.count);
    return p;
}

void ranges_update(Engine& eng, U e) {
    Mem& m = eng.m;
    U b = U(m.u32(e + 0x38C));
    if (!m.u32(G_RANGE_THIRD_SET)) {
        m.w32(G_RANGE_THIRD_SET, 1);
        I h = m.s16(b + 0xB2);
        I q = iabs(h) / 3;
        m.w16(G_RANGE_THIRD, h >= 0 ? q : -q);
    }
    I a2 = m.u16(b + 0xB2);
    I v = S32(a2 << 16);
    I s0, s1;
    if (m.u32(m.u32(m.u32(G_SOLDIER) + 0x388)) & 2) {
        I a0 = v >> 18;
        a2 = a2 - a0;
        s0 = (v >> 17) - (v >> 19);
        s1 = a0;
    } else {
        s0 = v >> 17;
        s1 = m.u16(G_RANGE_THIRD);
    }
    m.w32(e + 0x4C + 4 * 5, S16(a2));
    m.w32(e + 0x4C + 4 * 6, S16(s0));
    m.w32(e + 0x4C + 4 * 7, S16(s1));
}

static void puff_spawn(Engine& eng, U base, const V3& pos) {
    Mem& m = eng.m;
    for (int i = 0; i < 5; i++) {
        U p = base + 0x24 * i;
        if (m.u32(p) & 1) continue;
        m.w32(p, m.u32(p) | 1);
        m.w32(p + 4, 0x3C);
        m.w32(p + 8, (eng.rand.draw(FN("hit_reaction")) << 12) >> 15);
        for (int k = 0; k < 3; k++) m.w32(p + 0x10 + 4 * k, pos[k]);
        m.w32(p + 0x1C, 0x20000);
        for (int k = 0; k < 3; k++) m.w8(p + 0x20 + k, 0x3C);
        m.w32(p + 0xC, ((eng.rand.draw(FN("hit_reaction")) * 0x19) >> 15) + 8);
        return;
    }
}

static void drops_spawn(Engine& eng, U base, const V3& pos, I n, I life, I spread, I vy, I a6, I a7, I a8) {
    Mem& m = eng.m;
    auto rd = [&]() { return eng.rand.draw(FN("hit_reaction")); };
    for (int i = 0; i < 0xF; i++) {
        U p = base + 0x74 * i;
        if (m.u32(p) & 1) continue;
        m.w32(p, m.u32(p) | 0x31 | a8);
        m.w32(p + 4, life);
        m.w32(p + 8, (rd() << 12) >> 15);
        for (U c : {0x28u, 0x34u, 0x40u})
            for (int k = 0; k < 3; k++) m.w32(p + c + 4 * k, pos[k]);
        m.w32(p + 0x10, 0x10000);
        m.w8(p + 0x4C, 0x3C);
        m.w8(p + 0x4E, 0);
        m.w8(p + 0x4D, 0);
        m.w32(p + 0xC, 0);
        I sxz = spread * 2, sy = vy + 5;
        m.w32(p + 0x1C, sxz ? S32((-spread + ((rd() * sxz) >> 15)) << 16) : S32(-spread << 16));
        m.w32(p + 0x20, sy ? S32((-vy + ((rd() * sy) >> 15)) << 16) : S32(-vy << 16));
        m.w32(p + 0x24, sxz ? S32((-spread + ((rd() * sxz) >> 15)) << 16) : S32(-spread << 16));
        m.w32(p + 0x14, 0x10000);
        m.w8(p + 0x70, a6 + 0xF);
        m.w8(p + 0x71, a6 + 0xF);
        m.w32(p + 0x18, a7);
        if (--n <= 0) return;
    }
}

void hit_reaction(Engine& eng, U e, I var, I part) {
    Mem& m = eng.m;
    U obj = U(m.u32(e + 0x388));
    U q = U(m.u32(e + 0x304) + 0x48 * part);
    V3 pos;
    for (int k = 0; k < 3; k++) pos[k] = S32(m.s32(q + 0x20 + 4 * k) << 16);
    I blood = m.u8(G_BLOOD);
    I r = m.s32(e + 0x4C + 4 * var);
    if (blood == 0 && (r == 1 || r == 4 || r == 8))
        r = 2;
    else if (blood == 1 && (r == 4 || r == 8))
        r = 1;
    m.w32(e + 0x4C + 4 * var, r);
    auto rd = [&]() { return eng.rand.draw(FN("hit_reaction")); };
    if (r == 1) {
        I n, life, spread;
        if (blood == 1)
            n = 0xB, life = 0x19, spread = ((rd() * 0xA) >> 15) + 0xF;
        else
            n = 0xF, life = 0x23, spread = ((rd() * 0xF) >> 15) + 0x14;
        drops_spawn(eng, obj + 0x49C, pos, n, life, spread, 0x10000, 4, 0, 0);
        m.w32(e + 0x4C + 4 * var, 0);
    } else if (r == 2) {
        puff_spawn(eng, obj + 0x3E8, pos);
        m.w32(e + 0x4C + 4 * var, 0);
    } else if (r == 4 || r == 8)
        eng.note("blood spray reactions (4, 8) not implemented");
}

I puffs_update(Mem& m, U base) {
    I any = 0;
    for (int i = 0; i < 5; i++) {
        U p = base + 0x24 * i;
        if (!(m.u32(p) & 1)) continue;
        any = 1;
        I t = S32(m.s32(p + 4) - 1);
        m.w32(p + 4, t);
        if (t <= 0) {
            m.w32(p, 0);
            m.w32(p + 4, 0);
            continue;
        }
        m.w32(p + 0x14, S32(m.s32(p + 0x14) - 0x10000));
        m.w32(p + 8, S32(m.s32(p + 8) + m.s32(p + 0xC)));
        for (int k = 0; k < 3; k++) {
            I c = m.u8(p + 0x20 + k);
            m.w8(p + 0x20 + k, c - (c >> 3));
        }
    }
    return any;
}

I drops_update(Engine& eng, U e, U base) {
    Mem& m = eng.m;
    I any = 0;
    for (int i = 0; i < 0xF; i++) {
        U p = base + 0x74 * i;
        if (!(m.u32(p) & 1)) continue;
        any = 1;
        I t = S32(m.s32(p + 4) - 1);
        m.w32(p + 4, t);
        if (t <= 0) {
            m.w32(p, 0);
            m.w32(p + 4, 0);
            continue;
        }
        for (int k = 0; k < 3; k++) m.w32(p + 0x40 + 4 * k, m.u32(p + 0x34 + 4 * k));
        I y = S32(m.s32(p + 0x38) + m.s32(p + 0x20));
        m.w32(p + 0x38, y);
        if (m.s32(e + 0x208) < y) {
            if (!(m.u32(p) & 8)) {
                m.w32(p + 0x38, m.u32(e + 0x208));
                for (U o : {0x24u, 0x20u, 0x1Cu}) m.w32(p + o, 0);
                eng.rand.draw(FN("hit_reaction"));
                m.w32(p, (m.u32(p) & ~I(6)) | 8);
            }
        } else if (!(m.u32(p) & 8)) {
            m.w32(p + 0x34, S32(m.s32(p + 0x34) + m.s32(p + 0x1C)));
            m.w32(p + 0x3C, S32(m.s32(p + 0x3C) + m.s32(p + 0x24)));
            I drag = m.s32(p + 0x14);
            for (U o : {0x1Cu, 0x24u}) {
                I v = m.s32(p + o);
                if (v > 0)
                    m.w32(p + o, imax(S32(v - drag), 0));
                else if (v < 0)
                    m.w32(p + o, imin(S32(v + drag), 0));
            }
            I g = m.s32(p + 0x18);
            m.w32(p + 0x20, imin(S32(m.s32(p + 0x20) + g), g * 2));
            m.w32(p + 8, S32(m.s32(p + 8) + m.s32(p + 0xC)));
        }
    }
    return any;
}

// ---------------------------------------------------------------- taking pickups

static void ammo_add(Engine& eng, U e, I slot, I n) {
    Mem& m = eng.m;
    U inv = U(m.u32(e + 0x388));
    U a = U(inv + 0x56 + 2 * slot);
    n = S16(n);
    if (m.s16(a) < 0)
        m.w16(a, n);
    else
        m.w16(a, imin(S16(n + m.s16(a)), m.s16(inv + 0x5E + 2 * slot)));
    U w = U(G_WEAPONS + 0x44 * m.u8(G_SLOT_WEAPON + slot));
    I snd = m.s16(w + 0x20);
    if (snd >= 0) {
        I bank = m.s16(w + 0x22);
        if (bank < 0) bank = m.s16(m.u32(w + 0x40) + 0xC8);
        I flags = (m.u16(G_WEAPONS + 0x44 * m.u8(inv + 0x55) + 0x2A) >> 3) & 2;
        eng.audio->play(bank, snd, e, 0x10000, 0, 0x7FFF, 0, 0, 0, 0, flags);
    }
    ammo_hud_update(eng, e);
}

I health_touch(Engine& eng, U p, U e) {
    Mem& m = eng.m;
    if ((m.u32(e + 0xC) & 0x20180000) || m.s32(e + 0xF4) == 0x800000) return 0;
    U o = U(m.u32(p + 0x388));
    I k = m.s32(o);
    // the message (kind 0..2: 0x8009E864..)
    pickup_message(eng, e, (k == 0 || k == 1 || k == 2) ? U(m.u32(0x8009E864 + 4 * U(k))) : 0);
    m.w32(e + 0xF4, imin(S32(m.s32(e + 0xF4) + m.s32(o + 0x14)), 0x800000));
    I snd = k == 0 ? 0x1F : (k == 1 || k == 2) ? 0x1E : -1;
    if (snd != -1) eng.audio->play(0, snd, e, 0x20000, 0, 0x10000, 0x3333, 0, 0, 0, 0);
    eng.remove(p, "health taken");
    return 1;
}

I ammo_touch(Engine& eng, U p, U e) {
    Mem& m = eng.m;
    if (m.u32(e + 0xC) & 0x20180000) return 0;
    U o = U(m.u32(p + 0x388));
    I w = m.u16(PICKUPS + 0x14 * m.s32(o) + 0xA);
    U inv = U(m.u32(e + 0x388));
    for (I slot = 0; slot < m.s32(G_SLOT_COUNT); slot++) {
        if (m.u8(G_SLOT_WEAPON + slot) == w) {
            if (m.s16(inv + 0x56 + 2 * slot) < m.s16(inv + 0x5E + 2 * slot)) {
                pickup_message(eng, e, U(m.u32(G_WEAPONS + 0x44 * U(w) + 0x3C)));
                eng.audio->play(0, 0x20, e, 0x10000, 0, 0x10000, 0x3333, 0, 0, 0, 0);
                ammo_add(eng, e, slot, m.s32(o + 0x14));
                eng.remove(p, "ammo taken");
            }
            return 1;
        }
    }
    return 0;
}

I item_touch(Engine& eng, U p, U) {
    Mem& m = eng.m;
    I k = m.s32(m.u32(p + 0x388));
    I msg = k == 15 ? 0x2F : k == 16 ? 0x30 : k == 17 ? 0x31 : k == 18 ? 0x69 : k == 19 ? 0x6A : -1;
    if (msg != -1) {
        auto it = eng.ents.find(p);
        eng.logic->send(msg, it == eng.ents.end() ? nullptr : it->second, 0, nullptr, 0, "item pickup");
    }
    eng.remove(p, "item taken");
    return 1;
}

}  // namespace valor::sim
