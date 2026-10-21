// What model scripts read from the world (docs/re/enemies.md Variables, op 2E query; soldier.md):
// sight, hearing, angles, routes, probes, nodes. tools/sim/engine_sense.py.
#include <cmath>

#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/scripts.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {
constexpr U G_VOICES = 0x8009115C;
constexpr U G_VOICE_ENABLED = 0x800910D0;

I wrap(I a) {
    a = S16(a);
    while (a < -0x800) a = S16(a + 0x1000);
    while (a >= 0x801) a = S16(a - 0x1000);
    return a;
}

M3 ent_m3(Mem& m, U e) {
    M3 M;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) M[i][j] = m.s16(e + 0x340 + 6 * i + 2 * j);
    return M;
}

V3 vector_normal_ss(const V3& v) {
    double n = std::sqrt(double(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    if (n == 0) return {0, 0, 0};
    V3 o;
    for (int k = 0; k < 3; k++) o[k] = I(std::nearbyint(double(v[k]) * 4096.0 / n));
    return o;
}

V3 cross12(const V3& r, const V3& ir) {
    return {sat16i((r[1] * ir[2] - r[2] * ir[1]) >> 12), sat16i((r[2] * ir[0] - r[0] * ir[2]) >> 12),
            sat16i((r[0] * ir[1] - r[1] * ir[0]) >> 12)};
}
}  // namespace

I dist_approx(I dx, I dy, I dz) {
    I a[3] = {iabs(S16(dx)), iabs(S16(dy)), iabs(S16(dz))};
    std::sort(a, a + 3);
    return a[2] + ((a[0] + a[1]) >> 2);
}

I angle_to(Engine& eng, U e, U t) {
    Mem& m = eng.m;
    I h = m.s16(e + 0x22E);
    I dx = S32(m.s32(t + 0x20C) - m.s32(e + 0x20C)) >> 16;
    I dz = S32(m.s32(t + 0x214) - m.s32(e + 0x214)) >> 16;
    I v = eng.gte.ratan2(int32_t(dx), int32_t(dz)) - h;
    while (v < -0x800) v += 0x1000;
    v = S16(v);
    while (v >= 0x801) v = S16(v - 0x1000);
    return v;
}

U sense_nearest(Mem& m, U e, I typ) {
    U best = 0;
    I bd = 0x7FFF;
    for (U o : m.entities()) {
        if (m.s32(o + 8) == S32(typ) && o != e && !(m.u32(o + 0xC) & 0x20080000)) {
            I d[3];
            for (int k = 0; k < 3; k++) d[k] = S16((m.s32(e + 0x20C + 4 * k) - m.s32(o + 0x20C + 4 * k)) >> 16);
            I dd = dist_approx(d[0], d[1], d[2]) & 0xFFFF;
            if (dd < (bd & 0xFFFF) && iabs(d[1]) < 1000) best = o, bd = dd;
        }
    }
    return best;
}

U find_type_group(Mem& m, I typ, I grp) {
    for (U o : m.entities())
        if (m.s32(o + 8) == S32(typ) && m.s8(o + 0x35) == (S32(grp << 24) >> 24) && !(m.u32(o + 0xC) & 0x20080000)) return o;
    return 0;
}

V3 model_bone_point(Engine& eng, U e, I bone, const V3* off) {
    Mem& m = eng.m;
    U inst = U(m.u32(e + 0x100));
    if (!inst || !m.u32(inst + 0x84)) return {S16(m.s32(e + 0x20C) >> 16), S16(m.s32(e + 0x210) >> 16), S16(m.s32(e + 0x214) >> 16)};
    U jp = U(m.u32(inst + 0x94));
    V3 j = {m.s16(jp + 8 * bone), m.s16(jp + 8 * bone + 2), m.s16(jp + 8 * bone + 4)};
    if (off && ((*off)[0] || (*off)[1] || (*off)[2])) {
        U qp = U(m.u32(inst + 0x88));
        if (!qp) qp = U(m.u32(inst + 0x8C));
        if (qp) {
            V3 r = mvmva12(quat_m3(m.s16(qp + 8 * bone), m.s16(qp + 8 * bone + 2), m.s16(qp + 8 * bone + 4), m.s16(qp + 8 * bone + 6)), *off);
            for (int k = 0; k < 3; k++) j[k] = S16(r[k] + j[k]);
        }
    }
    V3 t = {m.s32(e + 0x354), m.s32(e + 0x358), m.s32(e + 0x35C)};
    V3 w = to_world(ent_m3(m, e), t, j);
    for (I& x : w) x = S16(x);
    if (m.s32(e + 8) == 1) w[1] = S16(w[1] + m.u16(m.u32(m.u32(G_PLAYER) + 0x388) + 0x94));
    return w;
}

I sight_test(Engine& eng, U e, U t, I rng, bool blockers) {
    Mem& m = eng.m;
    U b = U(m.u32(e + 0x38C));
    I eye = m.s32(t + 0x2D4);
    if (m.u32(t + 0x10) & 0x800) eye >>= 1;
    if (!b) {
        eng.note("sight without a brain (0x80056D08) not implemented");
        return 0;
    }
    I bone = m.s16(b + 0xAA);
    V3 p0 = model_bone_point(eng, e, bone, nullptr);
    V3 o1 = {-100, 0, 15};
    V3 p1 = model_bone_point(eng, e, bone, &o1);
    V3 fwd = {S16(p1[0] - p0[0]), S16(p1[1] - p0[1]), S16(p1[2] - p0[2])};
    I a0 = eng.gte.ratan2(int32_t(fwd[0]), int32_t(fwd[2]));
    V3 d = {S16(m.s16(t + 0x20E) - p0[0]), S16((S32(m.s32(t + 0x210) - eye) >> 16) - p0[1]), S16(m.s16(t + 0x216) - p0[2])};
    I ang = iabs(wrap(S16(eng.gte.ratan2(int32_t(d[0]), int32_t(d[2])) - a0)));
    I dist = dist_approx(d[0], d[1], d[2]);
    if (!(dist < rng && ang < m.s16(b + 0xB4))) return 0;
    if (blockers) {
        V3 u = vector_normal_ss(d);
        for (U o : m.entities()) {
            if (!(m.u32(o + 0x10) & 0x4000) || (m.u32(o + 0xC) & 0x20080000)) continue;
            V3 v;
            for (int k = 0; k < 3; k++) v[k] = S16(m.u16(o + 0x2E0 + 2 * k) - p0[k]);
            I dv = dist_approx(v[0], v[1], v[2]);
            if (rng < dv || !(dv < dist)) continue;
            if (S32(v[0] * u[0] + v[1] * u[1] + v[2] * u[2]) <= 0) continue;
            V3 c = cross12(v, u);
            I r = m.u16(o + 0x2E8);
            if (!(r * r < c[0] * c[0] + c[1] * c[1] + c[2] * c[2])) return 0;
        }
    }
    I a[3] = {p0[0], p0[1], p0[2]};
    I tb[3] = {m.s32(t + 0x20C) >> 16, S32(m.s32(t + 0x210) - eye) >> 16, m.s32(t + 0x214) >> 16};
    I h[3], n[3];
    return eng.world.sweep(a, tb, h, n) ? 0 : 1;
}

bool sees(Engine& eng, U w, U v, bool long_range) {
    Mem& m = eng.m;
    U b = U(m.u32(w + 0x38C));
    if (!b) return false;
    return sight_test(eng, w, v, m.s16(b + (long_range ? 0xB2 : 0xB0)), true) != 0;
}

I hearing(Engine& eng, U e) {
    Mem& m = eng.m;
    U b = U(m.u32(e + 0x38C));
    I rng = b ? m.u16(b + 0xB0) : 0xBB8;
    I p[3] = {m.s16(e + 0x20E), m.s16(e + 0x212), m.s16(e + 0x216)};
    for (int v = 1; v < 24; v++) {
        U a = G_VOICES + 0x34 * v;
        if (m.u8(a + 0xE) != 0 || !(m.u8(G_VOICE_ENABLED + v) & 1) || m.u16(a + 0x28) >= 0x3C) continue;
        if (m.u32(a) & 3) continue;
        I h = m.u32(a + 4);
        if (!eng.handle_dead(h) && h == m.u32(e)) continue;
        I d[3];
        for (int k = 0; k < 3; k++) d[k] = S16(p[k] - m.u16(a + 0x2C + 2 * k));
        if ((dist_approx(d[0], d[1], d[2]) & 0xFFFF) < S16(rng)) return 1;
    }
    return 0;
}

void route_turn_kind(Engine& eng, U e, I px, I pz, I& kind, I& out) {
    Mem& m = eng.m;
    I fx = m.s16(e + 0x344), fz = m.s16(e + 0x350);
    I dx = S16(px - m.s16(e + 0x20E)), dz = S16(pz - m.s16(e + 0x216));
    I a2 = eng.gte.ratan2(int32_t(dx), int32_t(dz));
    I h = m.s32(e + 0x22C);
    while (h < -0x8000000) h += 0x10000000;
    while (h > 0x8000000) h -= 0x10000000;
    I a0 = (h >> 16) - a2;
    I v1 = 0x1000 - a0;
    if (a0 < v1) v1 = a0;
    out = S32(v1 << 16);
    I a = iabs(v1);
    I ir2 = sat16i((fz * dx - fx * dz) >> 12);
    if (0 <= a - 0x641 && a - 0x641 < 0x2BB)
        kind = 3;
    else if (0 <= a - 0x259 && a - 0x259 < 0x383)
        kind = ir2 < 0 ? 1 : 2;
    else
        kind = 0;
}

I brain_node_target(Mem& m, U e, NodeTarget& w, I node) {
    U nodes = U(m.u32(m.u32(m.u32(0x8009D648) + 0x388) + 8));
    U nd = U((nodes + 0x34 * node) & 0xFFFFFFFF);
    I fx = m.s16(nd + 0x24), fz = m.s16(nd + 0x28);
    I side = m.s32(nd + 0x10);
    I kind = m.s32(nd + 0xC);
    I px = m.s16(nd + 0x14), py = m.s16(nd + 0x16), pz = m.s16(nd + 0x18);
    I d[3] = {S16(px - m.s16(e + 0x20E)), S16(py - m.s16(e + 0x212)), S16(pz - m.s16(e + 0x216))};
    I via = 1, sx = 0, sz = 0, ox = 0, oz = 0;
    if (side != 4) {
        if (iabs(fz) < iabs(fx)) {
            if (fx < 0) {
                if (side == 1) sz = 0x64, oz = -0x28;
                else if (side == 2) sz = -0x64, oz = 0x28;
                else via = 0;
            } else if (fx > 0) {
                if (side == 1) sz = -0x64, oz = 0x28;
                else if (side == 2) sz = 0x64, oz = -0x28;
                else via = 0;
            } else
                via = 0;
        } else {
            if (fz < 0) {
                if (side == 1) sx = -0x64, ox = 0x28;
                else if (side == 2) sx = 0x64, ox = -0x28;
                else via = 0;
            } else if (fz > 0) {
                if (side == 1) sx = 0x64, ox = -0x28;
                else if (side == 2) sx = -0x64, ox = 0x28;
                else via = 0;
            } else
                via = 0;
        }
    } else
        via = 0;
    if (dist_approx(d[0], d[1], d[2]) < 0xFA) via = 0;
    if (kind != 8) {
        w.node_point = {S16(px + ox - ((fx * 5) >> 10)), py, S16(pz + oz - ((fz * 5) >> 10))};
        w.side[0] = S16(sx + px + ox - ((fx * 0x19) >> 12));
        w.side[1] = S16(sz + pz + oz - ((fz * 0x19) >> 12));
    } else {
        w.node_point = {px, py, pz};
        w.side[0] = sx, w.side[1] = sz;
    }
    w.via = via;
    w.claimed = node;
    return via;
}

I probe(Engine& eng, U e, I how, I r) {
    Mem& m = eng.m;
    I k = how & 0xF;
    V3 v;
    if (k == 1) v = {-r, 0, 0};
    else if (k == 2) v = {r, 0, 0};
    else if (k == 4) v = {0, 0, r};
    else if (k == 8) v = {0, 0, -r};
    else return 0;
    for (I& x : v) x = S16(x);
    V3 t = {m.s32(e + 0x354), m.s32(e + 0x358), m.s32(e + 0x35C)};
    V3 p = to_world(ent_m3(m, e), t, v);
    I eye = (how & 0x10) ? m.s32(e + 0x2D4) : m.s32(e + 0x2D4) >> 1;
    I y = S32(m.s32(e + 0x210) - eye);
    I a[3] = {S16(p[0]), y >> 16, S16(p[2])};
    I b[3] = {m.s32(e + 0x20C) >> 16, y >> 16, m.s32(e + 0x214) >> 16};
    I h[3], n[3];
    if (eng.world.sweep(a, b, h, n)) return 0;
    if (m.u32(e + 0x10) & 0x200000) {
        I pp[3] = {a[0] << 16, a[1] << 16, a[2] << 16};
        I f = eng.world.floor(pp).height;
        if (S16(m.s16(e + 0x212) - f) < -0x190) return 0;
    }
    return S16(r) + 1;
}

I compute_node(Engine& eng, U, I, I, I) {
    Mem& m = eng.m;
    U wo = U(m.u32(m.u32(0x8009D648) + 0x388));
    if (m.s32(wo + 0xC) <= 0) return -1;
    eng.note("node search with nodes not implemented");
    return -1;
}

static I aimed_at(Engine& eng, U e) {
    Mem& m = eng.m;
    U pl = U(m.u32(G_SOLDIER));
    I r = m.u16(e + 0x2E8);
    if (!r && m.u32(e + 0x38C)) r = m.u16(m.u32(e + 0x38C) + 0xC6);
    U inst = U(m.u32(pl + 0x100));
    V3 a, b;
    if (m.u32(inst + 0x84)) {
        U model = U(m.u32(inst + 8));
        I bone = m.s16(model + 0xD6);
        U qp = U(m.u32(inst + 0x88));
        M3 R = quat_m3(m.s16(qp + 8 * bone), m.s16(qp + 8 * bone + 2), m.s16(qp + 8 * bone + 4), m.s16(qp + 8 * bone + 6));
        U jp = U(m.u32(inst + 0x94));
        V3 j = {m.s16(jp + 8 * bone), m.s16(jp + 8 * bone + 2), m.s16(jp + 8 * bone + 4)};
        V3 off = {m.s16(model + 0xE0), m.s16(model + 0xE2), m.s16(model + 0xE4)};
        V3 mac = gmul_mac(R, off, j, 12);
        V3 p1 = {S16(mac[0]), S16(mac[1]), S16(mac[2])};
        V3 p2 = {p1[0], p1[1], S16(p1[2] + 100)};
        M3 E = ent_m3(m, pl);
        V3 T = {m.s32(pl + 0x354), m.s32(pl + 0x358), m.s32(pl + 0x35C)};
        I eye = m.u16(m.u32(m.u32(G_PLAYER) + 0x388) + 0x94);
        a = gmul_mac(E, p1, T, 12);
        b = gmul_mac(E, p2, T, 12);
        for (int k = 0; k < 3; k++) a[k] = S16(a[k]), b[k] = S16(b[k]);
        a[1] = S16(a[1] + eye);
        b[1] = S16(b[1] + eye);
    } else {
        I w = m.u8(m.u32(pl + 0x388) + 0x55);
        a = model_bone_point(eng, pl, m.s16(0x80093640 + 0x44 * w + 0xC), nullptr);
        b = model_bone_point(eng, pl, m.s16(0x80093640 + 0x44 * w + 0xE), nullptr);
    }
    V3 u = vnormal(eng.gte, {S16(b[0] - a[0]), S16(b[1] - a[1]), S16(b[2] - a[2])});
    V3 d;
    for (int k = 0; k < 3; k++) d[k] = S16(m.s16(e + 0x2E0 + 2 * k) - m.s16(pl + 0x2E0 + 2 * k));
    V3 mac = gop(d, u, 12);
    V3 c = {sat16i(mac[0]), sat16i(mac[1]), sat16i(mac[2])};
    return r * r >= c[0] * c[0] + c[1] * c[1] + c[2] * c[2] ? 1 : 0;
}

I world_value(VM& vm, I op, I) {
    Engine& eng = vm.eng;
    Mem& m = eng.m;
    U e = vm.e;
    if (op == 0x2EC) op = 0x2E0C;
    if ((op & 0xFF00) == 0x2E00) {
        I fact = op & 0xFF;
        U tgt = vm.query_target;
        U b = U(m.u32(e + 0x38C));
        bool crouch = m.u32(m.u32(G_SOLDIER) + 0x388) & 2;
        if (fact == 0xB || fact == 0x10 || fact == 0x12) {
            if (!tgt) return 0;
            if (!b) {
                eng.note("sight without a brain not implemented");
                return 0;
            }
            I rng = m.s16(b + (fact == 0xB ? 0xB0 : 0xB2));
            if (crouch && fact != 0x12) rng = (rng & 0xFFFF) - (S32((rng & 0xFFFF) << 16) >> 18);
            return sight_test(eng, e, tgt, S16(rng), true);
        }
        if (fact == 0x11) return hearing(eng, e);
        if (fact == 0xC) return tgt ? iabs(angle_to(eng, e, tgt)) : 0;
        if (fact == 0xF) return aimed_at(eng, e);
        eng.note("query fact " + std::to_string(fact) + " not implemented");
        return 0;
    }
    eng.note("script world read op " + std::to_string(op) + " not implemented");
    return 0;
}

I sense_sensor(VM& vm, I vid) {
    if (vid == 0x67) return vm.eng.action_held(0) ? 1 : 0;
    if (vid == 0x71) vm.eng.note("sensor 0x71 (the player looks at it) not implemented");
    return 0;
}

}  // namespace valor::sim
