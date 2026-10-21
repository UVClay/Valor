// The camera entity (docs/re/movement.md, The camera; tools/sim/engine_camera.py) and aim_update
// (docs/re/combat.md, Aiming; tools/sim/engine_aim.py), on the engine's memory.
#include "valor/aim.h"
#include "valor/sim/engine.h"
#include "valor/sim/objects.h"
#include "valor/sim/simgte.h"

namespace valor::sim {

namespace {

void ease(Mem& m, U co) {
    I sh = m.s32(co + 0x10);
    static const U pairs[9][2] = {{0x48, 0x54}, {0x4C, 0x58}, {0x50, 0x5C}, {0x18, 0x24}, {0x1C, 0x28},
                                  {0x20, 0x2C}, {0x78, 0x84}, {0x7C, 0x88}, {0x80, 0x8C}};
    for (auto& p : pairs) {
        I a = m.s32(co + p[0]), b = m.s32(co + p[1]);
        if (a != b) m.w32(co + p[0], S32(a + (S32(b - a) >> sh)));
    }
}

void look_points(Engine& eng, U, U co, U sol) {
    Mem& m = eng.m;
    I mode = m.u32(co + 0x9C);
    if (!(mode & 0x480)) {
        eng.note("camera look mode not implemented");
        return;
    }
    m.w32(co + 0xBC, m.s32(m.u32(sol + 0x388) + 0x184));
    M3 rot = rot_y(m.s16(co + 0xBE));
    V3 a = rot_trans_sv(rot, {0, 0, 0}, {m.s16(co + 0x1A), 0, m.s16(co + 0x22)});
    V3 b = rot_trans_sv(rot, {0, 0, 0}, {m.s16(co + 0x7A), 0, m.s16(co + 0x82)});
    I sx = m.s32(sol + 0x20C), sy = m.s32(sol + 0x210), sz = m.s32(sol + 0x214);
    m.w32(co + 0, S32(sx + (a[0] << 16)));
    m.w32(co + 8, S32(sz + (a[2] << 16)));
    m.w32(co + 4, S32(sy + m.s32(co + 0x1C)));
    m.w32(co + 0x6C, S32(sx + (b[0] << 16)));
    m.w32(co + 0x74, S32(sz + (b[2] << 16)));
    m.w32(co + 0x70, S32(sy + (m.s32(co + 0x94) << 16)));
}

void place_cam(Engine& eng, U e, U co, U sol) {
    Mem& m = eng.m;
    if (m.u32(co + 0x9C) & 0x80) {
        m.w32(e + 0x20C, m.s32(sol + 0x20C));
        m.w32(e + 0x210, S32(m.s32(sol + 0x210) + (m.s32(co + 0x94) << 16)));
        m.w32(e + 0x214, m.s32(sol + 0x214));
    } else
        eng.note("camera place mode not implemented");
}

void view_matrix(Mem& m, U co, M3& M, V3& t) {
    U base = co + 0x100;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) M[i][j] = m.s16(base + 6 * i + 2 * j);
    for (int k = 0; k < 3; k++) t[k] = m.s32(base + 0x14 + 4 * k);
}

I cheap(I a, I b, I c) {
    a = iabs(a), b = iabs(b), c = iabs(c);
    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    return a + ((b + c) >> 2);
}

I wrapa(I v) {
    v &= 0xFFF;
    return v >= 0x801 ? v - 0x1000 : v;
}

I scale(I s0, I s1) {
    if (s1 <= 0x257FFFF)
        s0 >>= 2;
    else if (s1 <= 0x63FFFFF)
        s0 >>= 1;
    else if (s1 <= 0xC7FFFFF)
        s0 -= s0 >> 2;
    if (iabs(s0) < 8) s0 >>= 2;
    return s0;
}

}  // namespace

void camera_think(Engine& eng, U e) {
    Mem& m = eng.m;
    U co = U(m.u32(e + 0x388));
    U sol = eng.resolve(m.u32(co + 0xC));
    if (sol) {
        if (m.u32(co + 0x9C) & 0x800) eng.note("death camera (mode 0x800) not implemented");
        ease(m, co);
        look_points(eng, e, co, sol);
        place_cam(eng, e, co, sol);
    }
    if (m.s16(co + 0xCE) > 0) {
        I a = m.s16(co + 0xCC);
        m.w32(e + 0x210, m.s32(e + 0x210) + (a << 16));
        m.w32(co + 4, m.s32(co + 4) + (a << 16));
        m.w32(co + 0x70, m.s32(co + 0x70) + (a << 16));
        m.w16(co + 0xCC, -(S32((a & 0xFFFF) << 16) >> 17));
        m.w16(co + 0xCE, m.s16(co + 0xCE) - 1);
    }
}

void camera_setup(Engine& eng, U e) {
    Mem& m = eng.m;
    U co = U(m.u32(e + 0x388));
    if (m.u32(co + 0x9C) & 0x1800) {
        eng.note("camera_setup in death mode not implemented");
        return;
    }
    V3 look = {m.s16(co + 2), m.s16(co + 6), m.s16(co + 0xA)};
    V3 eye = {m.s16(e + 0x20E), m.s16(e + 0x212), m.s16(e + 0x216)};
    V3 side = {m.s16(co + 0x6E), m.s16(co + 0x72), m.s16(co + 0x76)};
    for (int k = 0; k < 3; k++) {
        m.w32(co + 0xE4 + 4 * k, look[k]);
        m.w32(co + 0xF0 + 4 * k, eye[k]);
    }
    V3 d, sv;
    for (int k = 0; k < 3; k++) d[k] = S32(look[k] - eye[k]), sv[k] = S32(side[k] - eye[k]);
    V3 s_n = vnormal(eng.gte, sv), f_n = vnormal(eng.gte, d);
    V3 up = outer12(f_n, s_n), right = outer12(up, f_n);
    M3 M;
    for (int k = 0; k < 3; k++) {
        M[0][k] = S16(right[k] * 2);
        M[1][k] = S16(cdiv(up[k] * 5, 8) * 2);
        M[2][k] = S16(f_n[k] * 2);
    }
    U base = co + 0x100;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m.w16(base + 6 * i + 2 * j, M[i][j]);
    V3 t = apply_lv(M, {-eye[0], -eye[1], -eye[2]});
    for (int k = 0; k < 3; k++) m.w32(base + 0x14 + 4 * k, t[k]);
}

void view_pitch_target(Engine& eng, U inv, U co) {
    Mem& m = eng.m;
    I a = iabs(m.s16(co + 0x26)), c = iabs(m.s16(co + 0x2E));
    I d = a + c - (imin(a, c) >> 1);
    I p = m.s16(inv + 0x17A);
    I num = -(d * eng.gte.rsin(int32_t(p)));
    I v = cdiv(num, eng.gte.rcos(int32_t(p))) + m.s32(co + 0x98);
    m.w32(co + 0x28, S32(v << 16));
}

void aim_update(Engine& eng, U e, U inv) {
    Mem& m = eng.m;
    I flags = m.u32(inv);
    if (m.u32(inv + 0xA4) >= 0x191 && !(flags & 0x200000)) {
        m.w32(e + 0x234, 0);
        m.w32(e + 0x238, 0);
        return;
    }
    I stance = m.u32(inv + 0x1C8);
    if (flags & 0x800000) {
        I yb;
        if (stance < 2) {
            m.w32(e + 0x234, S32(m.s32(inv + 0x178) - 4 * m.s32(inv + 0x17C) + m.s32(inv + 0x180) - m.s32(e + 0x228)));
            yb = S32(m.s32(inv + 0x184) + 8 * m.s32(inv + 0x188));
        } else {
            m.w32(e + 0x234, S32(m.s32(inv + 0x178) + m.s32(inv + 0x180) - m.s32(e + 0x228)));
            yb = m.s32(inv + 0x184);
        }
        m.w32(e + 0x238, S32(yb + m.s32(inv + 0x18C) - m.s32(e + 0x22C)));
        return;
    }
    U cam = U(m.u32(G_PLAYER));
    U co = U(m.u32(cam + 0x388));
    M3 M;
    V3 t;
    view_matrix(m, co, M, t);
    M3 T = transpose(M);
    for (int i = 0; i < 3; i++) {
        I v = T[i][1] << 6;
        I q = iabs(v) / 25;
        T[i][1] = S16(v >= 0 ? q : -q);
    }
    V3 v = {S16((((m.s16(inv + 0x190) - 0x100) << 6) - t[0]) >> 2),
            S16((((m.s16(inv + 0x192) - (m.s16(inv + 0x4E) + 0x78)) << 6) - t[1]) >> 2),
            S16(((m.u32(inv + 0xA4) << 6) - t[2]) >> 2)};
    V3 P = gmul_mac(T, v, {0, 0, 0}, 12);
    V3 C = {m.s32(cam + 0x20C), m.s32(cam + 0x210), m.s32(cam + 0x214)};
    V3 end;
    bool big = iabs(P[0]) >= 0x4001 || iabs(P[1]) >= 0x4001 || iabs(P[2]) >= 0x4001;
    for (int k = 0; k < 3; k++)
        end[k] = big ? S32(C[k] + (S32(S32((P[k] >> 2) << 16) - (C[k] >> 2)) << 1)) : S32(C[k] + (S32(S32(P[k] << 16) - C[k]) >> 1));
    V3 B0, C0;
    for (int k = 0; k < 3; k++) {
        B0[k] = S32(C[k] + (S32(end[k] - C[k]) >> 3));
        C0[k] = S32(C[k] + (S32(end[k] - C[k]) >> 1));
    }
    I aim[3];
    bool hit = false;
    const V3* segs[3][2] = {{&B0, &C}, {&C0, &B0}, {&end, &C0}};
    for (auto& sg : segs) {
        I a[3] = {(*sg[0])[0] >> 16, (*sg[0])[1] >> 16, (*sg[0])[2] >> 16}, b[3] = {(*sg[1])[0] >> 16, (*sg[1])[1] >> 16, (*sg[1])[2] >> 16};
        I n[3];
        if (eng.world.sweep(a, b, aim, n)) {
            hit = true;
            break;
        }
    }
    if (!hit)
        for (int k = 0; k < 3; k++) aim[k] = S16(end[k] >> 16);
    flags &= ~I(0x100000) & 0x7FFFFFFF;
    I Ci[3] = {S16(C[0] >> 16), S16(C[1] >> 16), S16(C[2] >> 16)};
    I d[3];
    V3 ds;
    for (int k = 0; k < 3; k++) d[k] = S32(aim[k] - Ci[k]), ds[k] = S16(d[k]);
    I length = cheap(d[0], d[1], d[2]);
    V3 sq = gsqr(ds, 0);
    I len2 = S32(sq[0] + sq[1] + sq[2]);
    V3 u = vnormal(eng.gte, ds);
    I best = length;
    int32_t ci32[3] = {int32_t(Ci[0]), int32_t(Ci[1]), int32_t(Ci[2])}, u32a[3] = {int32_t(u[0]), int32_t(u[1]), int32_t(u[2])};
    for (U o : eng.entities()) {
        if (o == e || (m.u32(o + 0xC) & 0x20000000) || (m.u32(o + 0x10) & 0x206)) continue;
        int16_t centre[3] = {int16_t(m.s16(o + 0x2E0)), int16_t(m.s16(o + 0x2E2)), int16_t(m.s16(o + 0x2E4))};
        if (!valor::segment_sphere(ci32, u32a, centre, int32_t(m.u16(o + 0x2E8)))) continue;
        bool ok = true;
        if (m.u32(o + 0xC) & 0x10400) {
            if (m.u16(o + 0x2EE)) {
                I pt[3], nn[3], fl;
                ok = segment_box(eng, Ci, aim, o, pt, nn, fl);
            } else {
                U pp = U(m.u32(o + 0x304));
                std::vector<AimPart> parts;
                for (I i = 0; i < m.u16(o + 0x36); i++)
                    parts.push_back({{int32_t(m.s32(pp + 0x48 * i + 0x20)), int32_t(m.s32(pp + 0x48 * i + 0x24)), int32_t(m.s32(pp + 0x48 * i + 0x28))},
                                     int32_t(m.s32(pp + 0x48 * i + 0x38)), int32_t(m.s32(pp + 0x48 * i + 8))});
                ok = valor::segment_parts(ci32, u32a, int32_t(len2), parts) >= 0;
            }
        }
        if (!ok) continue;
        I dd = cheap(S32(C[0] - m.s32(o + 0x20C)) >> 16, S32(C[1] - m.s32(o + 0x210)) >> 16, S32(C[2] - m.s32(o + 0x214)) >> 16);
        if (dd < best) {
            best = dd;
            if (dd >= 0x4B0 && (m.u32(o + 0xC) & 0x50)) {
                flags |= 0x100000;
                if (dd >= 0xABE) flags |= 0x80000000;
            }
        }
    }
    if (best != length) {
        I den = cheap(d[0], d[1], d[2]) + 1;
        for (int k = 0; k < 3; k++) aim[k] = S16(Ci[k] + cdiv(d[k] * best, den));
    }
    m.w32(inv, flags);
    for (int k = 0; k < 3; k++) m.w32(inv + 0x1A4 + 4 * k, S32(aim[k] << 16));
    U model = U(m.u32(m.u32(e + 0x100) + 8));
    V3 on = {m.s16(model + 0xD8), m.s16(model + 0xDA), m.s16(model + 0xDC)}, of = {m.s16(model + 0xE0), m.s16(model + 0xE2), m.s16(model + 0xE4)};
    V3 nearp = model_bone_point(eng, e, m.s16(model + 0xD4), &on);
    V3 far = model_bone_point(eng, e, m.s16(model + 0xD6), &of);
    V3 far32 = {S32(far[0] << 16), S32(far[1] << 16), S32(far[2] << 16)};
    I ax = iabs(S32(C[0] - m.s32(inv + 0x1A4)) >> 2), az = iabs(S32(C[2] - m.s32(inv + 0x1AC)) >> 2);
    I t5 = ax + az - (imin(ax, az) >> 1);
    I fx = iabs(S32(C[0] - far32[0]) >> 2), fz = iabs(S32(C[2] - far32[2]) >> 2);
    I a3 = fx + fz - (imin(fx, fz) >> 1);
    I s1 = cheap(S32(C[0] - m.s32(inv + 0x1A4)), S32(C[1] - m.s32(inv + 0x1A8)), S32(C[2] - m.s32(inv + 0x1AC)));
    if (t5 < a3 + 0x140000) {
        for (int k = 0; k < 3; k++) m.w32(inv + 0x198 + 4 * k, C[k]);
        m.w32(inv, m.u32(inv) | 0x20000);
        I sx, sy;
        rtps(eng.gte, M, t, m.u32(inv + 0xA4) & 0xFFFF, 256 << 16, 120 << 16, far, sx, sy);
        I s0 = m.s16(inv + 0x190) - sx;
        m.w32(e + 0x238, iabs(s0) >= 9 ? S32(s0 << 16) : S32(s0 << 16) >> 1);
        s0 = sy - m.s16(inv + 0x192);
        I av;
        if (iabs(s0) >= 9)
            av = S32((s0 + (s0 >> 2)) << 16);
        else if (iabs(s0) >= 5)
            av = S32(s0 << 16);
        else
            av = S32(s0 << 16) >> 1;
        m.w32(e + 0x234, av);
    } else {
        const V3& start = (m.u32(inv) & 0x200000) ? C : far32;
        for (int k = 0; k < 3; k++) m.w32(inv + 0x198 + 4 * k, start[k]);
        m.w32(inv, m.u32(inv) & 0xFFFDFFFF);
        V3 g = vnormal(eng.gte, {far[0] - nearp[0], far[1] - nearp[1], far[2] - nearp[2]});
        V3 a = vnormal(eng.gte, {aim[0] - nearp[0], aim[1] - nearp[1], aim[2] - nearp[2]});
        I s0 = wrapa(eng.gte.ratan2(int32_t(g[2]), int32_t(g[0])) - eng.gte.ratan2(int32_t(a[2]), int32_t(a[0])));
        s0 = scale(s0, s1);
        m.w32(e + 0x238, S32(s0 << 16));
        I hg = eng.gte.sqrt0(int32_t(S32(g[0] * g[0] + g[2] * g[2]) & 0xFFFFFFFF));
        I ha = eng.gte.sqrt0(int32_t(S32(a[0] * a[0] + a[2] * a[2]) & 0xFFFFFFFF));
        s0 = wrapa(eng.gte.ratan2(int32_t(g[1]), int32_t(hg)) - eng.gte.ratan2(int32_t(a[1]), int32_t(ha)));
        s0 = scale(s0, s1);
        I bp = m.s32(e + 0x228);
        I v1 = S32(bp + (s0 << 16));
        if (0x2AA0000 < v1) v1 = 0x2AA0000;
        if (!(S32(0xFD560000) < v1)) v1 = S32(0xFD560000);
        m.w32(e + 0x234, S32((S32(S32(v1 * 8) - v1 + bp) >> 3) - bp));
    }
    I bp = m.s32(e + 0x228), by = m.s32(e + 0x22C), ap = m.s32(e + 0x234), ay = m.s32(e + 0x238);
    if (stance < 2) {
        m.w32(inv + 0x180, S32(bp + ap - (m.s32(inv + 0x178) - 4 * m.s32(inv + 0x17C))));
        m.w32(inv + 0x18C, S32(by + ay - (m.s32(inv + 0x184) + 8 * m.s32(inv + 0x188))));
    } else {
        m.w32(inv + 0x180, S32(bp + ap - m.s32(inv + 0x178)));
        m.w32(inv + 0x18C, S32(by + ay - m.s32(inv + 0x184)));
    }
}

}  // namespace valor::sim
