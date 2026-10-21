// The GTE operations and PsyQ libgte routines the game logic uses (tools/sim/engine_gte.py), on the
// engine's 64-bit values: 44-bit accumulators, IR saturation, the UNR division of RTPS. The tables
// (ratan, square roots, sine, VectorNormal's) are the game's (valor::Gte).
#pragma once

#include <algorithm>
#include <array>

#include "valor/fixed.h"
#include "valor/gte.h"
#include "valor/sim/common.h"

namespace valor::sim {

using V3 = std::array<I, 3>;
using M3 = std::array<V3, 3>;

inline I sat16i(I v) { return v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : v; }

// MVMVA: MAC = (t * 0x1000 + m . v) >> sf (44-bit accumulators); mac and ir.
inline void gmul(const M3& m, const V3& v, const V3& t, int sf, V3& mac, V3* ir = nullptr) {
    for (int r = 0; r < 3; r++) {
        I acc = t[r] * 0x1000;
        acc = wrap44(acc + m[r][0] * v[0]);
        acc = wrap44(acc + m[r][1] * v[1]);
        acc = wrap44(acc + m[r][2] * v[2]);
        mac[r] = S32(acc >> sf);
        if (ir) (*ir)[r] = sat16i(mac[r]);
    }
}
inline V3 gmul_mac(const M3& m, const V3& v, const V3& t = {0, 0, 0}, int sf = 12) {
    V3 mac;
    gmul(m, v, t, sf, mac);
    return mac;
}
inline V3 gmul_ir(const M3& m, const V3& v, const V3& t = {0, 0, 0}, int sf = 12) {
    V3 mac, ir;
    gmul(m, v, t, sf, mac, &ir);
    return ir;
}

// OP: the outer product of the diagonal d and IR; mac.
inline V3 gop(const V3& d, const V3& ir, int sf = 12) {
    return {S32(wrap44(ir[2] * d[1] - ir[1] * d[2]) >> sf), S32(wrap44(ir[0] * d[2] - ir[2] * d[0]) >> sf),
            S32(wrap44(ir[1] * d[0] - ir[0] * d[1]) >> sf)};
}
inline V3 gsqr(const V3& ir, int sf = 0) { return {S32((ir[0] * ir[0]) >> sf), S32((ir[1] * ir[1]) >> sf), S32((ir[2] * ir[2]) >> sf)}; }

inline V3 outer12(const V3& a, const V3& b) { return gop({S16(a[0]), S16(a[1]), S16(a[2])}, {S16(b[0]), S16(b[1]), S16(b[2])}, 12); }

inline int lzc32(I v) {
    uint32_t a = uint32_t(v);
    if (a & 0x80000000u) a = ~a;
    int n = 0;
    while (n < 32 && !(a & (0x80000000u >> n))) n++;
    return n;
}

// VectorNormal / VectorNormalSS (0x80016A70): to length 4096.
inline V3 vnormal(const Gte& g, const V3& v) {
    V3 t = {S16(v[0]), S16(v[1]), S16(v[2])};
    V3 mac = gsqr(t, 0);
    I n = S32(mac[0] + mac[1] + mac[2]);
    int lz = lzc32(n) & ~1;
    int sh = (31 - lz) >> 1;
    I x = lz - 24 >= 0 ? S32(n << (lz - 24)) : (n >> (24 - lz));
    I k = g.vn_[x - 0x40];
    return {S32(k * t[0]) >> sh, S32(k * t[1]) >> sh, S32(k * t[2]) >> sh};
}

inline V3 apply_lv(const M3& m, const V3& v) {
    V3 hi, lo;
    for (int i = 0; i < 3; i++) {
        if (v[i] >= 0)
            hi[i] = v[i] >> 15, lo[i] = v[i] & 0x7FFF;
        else {
            I y = -v[i];
            hi[i] = -(y >> 15), lo[i] = -(y & 0x7FFF);
        }
    }
    V3 h = {S16(hi[0]), S16(hi[1]), S16(hi[2])};
    V3 mh = gmul_mac(m, h, {0, 0, 0}, 0), ml = gmul_mac(m, lo, {0, 0, 0}, 12);
    V3 out;
    for (int i = 0; i < 3; i++) {
        I b = mh[i];
        b = b < 0 ? -((-b) << 3) : b << 3;
        out[i] = S32(ml[i] + b);
    }
    return out;
}

// RotMatrixYXZ's lookup: (sin, cos) of a halfword angle.
inline void sc_angle(I a, I& s, I& c) {
    SinCos r = sincos_angle(int32_t(S16(a)));
    s = r.s, c = r.c;
}

inline M3 rot_y(I r) {
    I s, c;
    sc_angle(r, s, c);
    return {{{c, 0, s}, {0, 0x1000, 0}, {S16(-s), 0, c}}};
}

inline M3 to_m3(const int16_t m[3][3]) {
    M3 o;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[i][j] = m[i][j];
    return o;
}

// RotMatrixYXZ (0x8001AF98) of halfword angles.
inline M3 rot_yxz3(I ax, I ay, I az) {
    int16_t m[3][3];
    rot_yxz(int32_t(S16(ax)), int32_t(S16(ay)), int32_t(S16(az)), m);
    return to_m3(m);
}
// As brain_sim's: the nine elements in row order.
inline std::array<I, 9> rot_yxz9(I ax, I ay, I az) {
    M3 m = rot_yxz3(ax, ay, az);
    return {m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]};
}

inline V3 rot_trans_sv(const M3& m, const V3& t, const V3& v) { return gmul_ir(m, v, t, 12); }

inline M3 transpose(const M3& m) {
    M3 o;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[i][j] = m[j][i];
    return o;
}

// max + (the other two) / 4 of the absolute values.
inline I dist_cheap(I a, I b, I c) {
    I v[3] = {iabs(a), iabs(b), iabs(c)};
    std::sort(v, v + 3);
    return v[2] + ((v[0] + v[1]) >> 2);
}

// RTPS (sf = 1) with the recording's projection (the Gte's scale).
inline void rtps(const Gte& g, const M3& rt, const V3& tr, I h, I ofx, I ofy, const V3& v, I& x, I& y) {
    int16_t r[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r[i][j] = int16_t(rt[i][j]);
    int32_t t[3] = {int32_t(tr[0]), int32_t(tr[1]), int32_t(tr[2])}, vv[3] = {int32_t(v[0]), int32_t(v[1]), int32_t(v[2])};
    int32_t sx, sy;
    g.rtps(r, t, uint32_t(h), int32_t(ofx), int32_t(ofy), vv, sx, sy);
    x = sx, y = sy;
}

// The bone rotation from a quaternion {x, y, z, w} (combat_sim.quat_matrix).
inline M3 quat_m3(I q0, I q1, I q2, I q3) {
    M3 m;
    m[0][0] = ((q0 * 2 * q0 + q3 * 2 * q3) >> 12) - 0x1000;
    m[1][0] = (q1 * 2 * q0 - q2 * 2 * q3) >> 12;
    m[2][0] = (q2 * 2 * q0 + q1 * 2 * q3) >> 12;
    m[0][1] = (q0 * 2 * q1 + q2 * 2 * q3) >> 12;
    m[1][1] = ((q1 * 2 * q1 + q3 * 2 * q3) >> 12) - 0x1000;
    m[2][1] = (q2 * 2 * q1 - q0 * 2 * q3) >> 12;
    m[0][2] = (-q3 * 2 * q1 + q2 * 2 * q0) >> 12;
    m[1][2] = (q2 * 2 * q1 + q3 * 2 * q0) >> 12;
    m[2][2] = (((q2 * q2) >> 12) + ((q3 * q3) >> 12)) * 2 - 0x1000;
    for (auto& r : m)
        for (I& x : r) x = S16(x);
    return m;
}

// GTE mvmva with sf = 1, no translation (saturation ignored).
inline V3 mvmva12(const M3& m, const V3& v) {
    V3 o;
    for (int i = 0; i < 3; i++) o[i] = (m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2]) >> 12;
    return o;
}

// combat_sim.to_world: (M c >> 12) + t (not truncated).
inline V3 to_world(const M3& m, const V3& t, const V3& c) {
    V3 o;
    for (int i = 0; i < 3; i++) o[i] = ((m[i][0] * c[0] + m[i][1] * c[1] + m[i][2] * c[2]) >> 12) + t[i];
    return o;
}

}  // namespace valor::sim

namespace valor::sim {

// angles_from_to (0x80044D18): pitch, yaw (integer angle units) from a to b (16.16).
inline void angles_from_to(const Gte& g, const V3& a, const V3& b, I& pitch, I& yaw, I lift = 0) {
    I dx = S32(b[0] - a[0]) >> 16;
    I dz = S32(b[2] - a[2]) >> 16;
    yaw = g.ratan2(int32_t(dx), int32_t(dz));
    I dy = S32(S32(b[1] - a[1]) - (lift >> 1)) >> 16;
    pitch = -I(g.ratan2(int32_t(dy), g.sqrt0(int32_t((S16(dx) * S16(dx) + S16(dz) * S16(dz)) & 0xFFFFFFFF))));
}

}  // namespace valor::sim
