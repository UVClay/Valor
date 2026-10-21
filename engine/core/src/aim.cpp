#include "valor/aim.h"

#include <algorithm>

#include "valor/fixed.h"
#include "valor/player.h"

namespace valor {

namespace {

int64_t cdiv(int64_t a, int64_t b) {
    int64_t q = iabs(a) / iabs(b);
    return (a >= 0) == (b >= 0) ? q : -q;
}

// GTE OP (sf = 1) of the diagonal d with IR = u, saturated to IR's 16 bits.
void cross12(const int64_t d[3], const int32_t u[3], int64_t out[3]) {
    out[0] = sat16((d[1] * u[2] - d[2] * u[1]) >> 12);
    out[1] = sat16((d[2] * u[0] - d[0] * u[2]) >> 12);
    out[2] = sat16((d[0] * u[1] - d[1] * u[0]) >> 12);
}

int32_t wrap12(int32_t v) {
    v &= 0xFFF;
    return v >= 0x801 ? v - 0x1000 : v;
}

int32_t aim_scale(int32_t s0, int64_t s1) {
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

int32_t dist_cheap(int64_t a, int64_t b, int64_t c) {
    a = iabs(a), b = iabs(b), c = iabs(c);
    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    return int32_t(a + ((b + c) >> 2));
}

bool segment_sphere(const int32_t start[3], const int32_t u[3], const int16_t centre[3], int32_t r) {
    if (!r) return false;
    int64_t d[3] = {s16(centre[0] - start[0]), s16(centre[1] - start[1]), s16(centre[2] - start[2])}, c[3];
    cross12(d, u, c);
    return !(int64_t(r) * r < c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
}

int segment_parts(const int32_t start[3], const int32_t u[3], int32_t len2, const std::vector<AimPart>& parts) {
    for (size_t j = 0; j < parts.size(); j++) {
        const AimPart& p = parts[j];
        if (p.kind == 7) continue;
        int64_t d[3] = {s16(p.c[0] - start[0]), s16(p.c[1] - start[1]), s16(p.c[2] - start[2])};
        int64_t a1 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - p.r2;
        if (a1 > 0) {
            if (d[0] * u[0] + d[1] * u[1] + d[2] * u[2] <= 0 || len2 < a1) continue;
            int64_t x[3];
            cross12(d, u, x);
            if (p.r2 < x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) continue;
        }
        return int(j);
    }
    return -1;
}

// model_bone_point (0x8008DABC) for a gun bone: its joint plus its rotation times the offset, into
// the world by the soldier's matrix (+0x340) and whole position (+0x354); the first-person model
// hangs from the eye, so the camera's eye offset is added to y.
void Player::bone_point(int which, int32_t out[3]) const {
    const AimArms& a = aim.arms;
    int32_t j[3] = {a.joint[which][0], a.joint[which][1], a.joint[which][2]};
    if (a.model) {
        const int16_t* off = a.model->gun_off[which];
        if (off[0] || off[1] || off[2]) {
            int16_t r[3][3];
            quat_matrix(a.quat[which].data(), r);
            int32_t v[3] = {off[0], off[1], off[2]}, rv[3];
            mvmva12(r, v, rv);
            for (int k = 0; k < 3; k++) j[k] = s16(rv[k] + j[k]);
        }
    }
    for (int i = 0; i < 3; i++)
        out[i] = s16(((int64_t(matrix[i][0]) * j[0] + int64_t(matrix[i][1]) * j[1] + int64_t(matrix[i][2]) * j[2]) >> 12) + ipos[i]);
    out[1] = s16(out[1] + uint16_t(aim.camera->obj.get(0x94)));
}

void Player::aim_update() {
    const Gte& g = *aim.gte;
    uint32_t flags = inv.flags;
    if (inv.zoom >= 0x191 && !(flags & 0x200000)) {
        ang_vel[0] = ang_vel[1] = 0;
        return;
    }
    uint32_t stance = inv.stance;
    if (flags & 0x800000) {
        int32_t yb;
        if (stance < 2) {
            ang_vel[0] = s32(int64_t(inv.pitch) - 4 * int64_t(inv.cross_y) + inv.aim_pitch - body[0]);
            yb = s32(int64_t(inv.yaw) + 8 * int64_t(inv.turn_rate));
        } else {
            ang_vel[0] = s32(int64_t(inv.pitch) + inv.aim_pitch - body[0]);
            yb = inv.yaw;
        }
        ang_vel[1] = s32(int64_t(yb) + inv.aim_yaw - body[1]);
        return;
    }
    // The crosshair's ray: the view's transpose, its middle column scaled by 64/25, applied to the
    // crosshair's screen point at the projection distance.
    const Camera& cam = *aim.camera;
    int16_t M[3][3], T[3][3];
    int32_t t[3];
    cam.view(M, t);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) T[i][j] = M[j][i];
    for (int i = 0; i < 3; i++) {
        int64_t v = int64_t(T[i][1]) << 6;
        int64_t q = iabs(v) / 25;
        T[i][1] = s16(v >= 0 ? q : -q);
    }
    int32_t v[3] = {s16(((int64_t(inv.cross_sx - 0x100) << 6) - t[0]) >> 2),
                    s16(((int64_t(inv.cross_sy - (int16_t(uint32_t(inv.kick) >> 16) + 0x78)) << 6) - t[1]) >> 2),
                    s16(((int64_t(inv.zoom) << 6) - t[2]) >> 2)};
    int32_t P[3], zero[3] = {0, 0, 0};
    gte_mul(T, v, zero, 12, P, nullptr);
    const int32_t* C = cam.pos;
    int32_t end[3];
    bool big = false;
    for (int k = 0; k < 3; k++) big |= iabs(P[k]) >= 0x4001;
    for (int k = 0; k < 3; k++)
        end[k] = big ? s32(int64_t(C[k]) + (int64_t(s32(int64_t(s32(int64_t(P[k] >> 2) << 16)) - (C[k] >> 2))) << 1))
                     : s32(int64_t(C[k]) + (s32(int64_t(s32(int64_t(P[k]) << 16)) - C[k]) >> 1));
    int32_t B0[3], C0[3];
    for (int k = 0; k < 3; k++) {
        B0[k] = s32(int64_t(C[k]) + (s32(int64_t(end[k]) - C[k]) >> 3));
        C0[k] = s32(int64_t(C[k]) + (s32(int64_t(end[k]) - C[k]) >> 1));
    }
    int32_t aimp[3];
    bool hit = false;
    const int32_t* segs[3][2] = {{B0, C}, {C0, B0}, {end, C0}};
    for (auto& sg : segs) {
        int32_t a[3] = {sg[0][0] >> 16, sg[0][1] >> 16, sg[0][2] >> 16}, b[3] = {sg[1][0] >> 16, sg[1][1] >> 16, sg[1][2] >> 16};
        int16_t n[3];
        if (aim.world->sweep(a, b, aimp, n)) {
            hit = true;
            break;
        }
    }
    if (!hit)
        for (int k = 0; k < 3; k++) aimp[k] = s16(end[k] >> 16);
    flags &= ~0x100000u & 0x7FFFFFFFu;
    int32_t Ci[3] = {s16(C[0] >> 16), s16(C[1] >> 16), s16(C[2] >> 16)};
    int32_t d[3], ds[3], u[3];
    for (int k = 0; k < 3; k++) d[k] = s32(int64_t(aimp[k]) - Ci[k]), ds[k] = s16(d[k]);
    int32_t length = dist_cheap(d[0], d[1], d[2]);
    int32_t len2 = s32(int64_t(ds[0]) * ds[0] + int64_t(ds[1]) * ds[1] + int64_t(ds[2]) * ds[2]);
    g.vector_normal(ds, u);
    int32_t best = length;
    if (aim.targets) {
        for (const AimTarget& o : *aim.targets) {
            if (o.handle == aim.self || (o.flags_a & 0x20000000) || (o.flags_b & 0x206)) continue;
            if (!segment_sphere(Ci, u, o.centre, o.radius)) continue;
            if (o.flags_a & 0x10400) {
                if (o.box) continue;  // an object's box (0x8004B0F0): not ported
                if (segment_parts(Ci, u, len2, o.parts) < 0) continue;
            }
            int32_t dd = dist_cheap(s32(int64_t(C[0]) - o.pos[0]) >> 16, s32(int64_t(C[1]) - o.pos[1]) >> 16,
                                    s32(int64_t(C[2]) - o.pos[2]) >> 16);
            if (dd < best) {
                best = dd;
                if (dd >= 0x4B0 && (o.flags_a & 0x50)) {
                    flags |= 0x100000;
                    if (dd >= 0xABE) flags |= 0x80000000u;
                }
            }
        }
    }
    if (best != length) {
        int64_t den = int64_t(dist_cheap(d[0], d[1], d[2])) + 1;
        for (int k = 0; k < 3; k++) aimp[k] = s16(Ci[k] + cdiv(int64_t(d[k]) * best, den));
    }
    inv.flags = flags;
    for (int k = 0; k < 3; k++) inv.aim_point[k] = s32(int64_t(aimp[k]) << 16);
    // The gun's two points (gun_points 0x80044E0C): near and far.
    int32_t nearp[3], farp[3], far32[3];
    bone_point(0, nearp);
    bone_point(1, farp);
    for (int k = 0; k < 3; k++) far32[k] = s32(int64_t(farp[k]) << 16);
    auto quarter = [](int64_t x) { return iabs(int64_t(s32(x)) >> 2); };
    int64_t ax = quarter(int64_t(C[0]) - inv.aim_point[0]), az = quarter(int64_t(C[2]) - inv.aim_point[2]);
    int64_t t5 = ax + az - (std::min(ax, az) >> 1);
    int64_t fx = quarter(int64_t(C[0]) - far32[0]), fz = quarter(int64_t(C[2]) - far32[2]);
    int64_t a3 = fx + fz - (std::min(fx, fz) >> 1);
    int64_t s1 = dist_cheap(s32(int64_t(C[0]) - inv.aim_point[0]), s32(int64_t(C[1]) - inv.aim_point[1]),
                            s32(int64_t(C[2]) - inv.aim_point[2]));
    if (t5 < a3 + 0x140000) {
        std::copy(C, C + 3, inv.shot_start);
        inv.flags |= 0x20000;
        int32_t sx, sy;
        g.rtps(M, t, inv.zoom & 0xFFFF, 256 << 16, 120 << 16, farp, sx, sy);
        int32_t s0 = inv.cross_sx - sx;
        ang_vel[1] = iabs(s0) >= 9 ? s32(int64_t(s0) << 16) : (s32(int64_t(s0) << 16) >> 1);
        s0 = sy - inv.cross_sy;
        if (iabs(s0) >= 9)
            ang_vel[0] = s32(int64_t(s0 + (s0 >> 2)) << 16);
        else if (iabs(s0) >= 5)
            ang_vel[0] = s32(int64_t(s0) << 16);
        else
            ang_vel[0] = s32(int64_t(s0) << 16) >> 1;
    } else {
        const int32_t* start = (inv.flags & 0x200000) ? C : far32;
        std::copy(start, start + 3, inv.shot_start);
        inv.flags &= 0xFFFDFFFFu;
        int32_t gv[3], av[3], gn[3], an[3];
        for (int k = 0; k < 3; k++) gv[k] = farp[k] - nearp[k], av[k] = aimp[k] - nearp[k];
        g.vector_normal(gv, gn);
        g.vector_normal(av, an);
        int32_t s0 = wrap12(g.ratan2(gn[2], gn[0]) - g.ratan2(an[2], an[0]));
        s0 = aim_scale(s0, s1);
        ang_vel[1] = s32(int64_t(s0) << 16);
        int32_t hg = g.sqrt0(s32(int64_t(gn[0]) * gn[0] + int64_t(gn[2]) * gn[2]));
        int32_t ha = g.sqrt0(s32(int64_t(an[0]) * an[0] + int64_t(an[2]) * an[2]));
        s0 = wrap12(g.ratan2(gn[1], hg) - g.ratan2(an[1], ha));
        s0 = aim_scale(s0, s1);
        int32_t bp = body[0];
        int32_t v1 = s32(int64_t(bp) + (int64_t(s0) << 16));
        if (0x2AA0000 < v1) v1 = 0x2AA0000;
        if (!(int32_t(0xFD560000) < v1)) v1 = int32_t(0xFD560000);
        ang_vel[0] = s32((int64_t(s32(int64_t(s32(int64_t(v1) * 8)) - v1 + bp)) >> 3) - bp);
    }
    int32_t bp = body[0], by = body[1], ap = ang_vel[0], ay = ang_vel[1];
    if (stance < 2) {
        inv.aim_pitch = s32(int64_t(bp) + ap - (int64_t(inv.pitch) - 4 * int64_t(inv.cross_y)));
        inv.aim_yaw = s32(int64_t(by) + ay - (int64_t(inv.yaw) + 8 * int64_t(inv.turn_rate)));
    } else {
        inv.aim_pitch = s32(int64_t(bp) + ap - inv.pitch);
        inv.aim_yaw = s32(int64_t(by) + ay - inv.yaw);
    }
}

}  // namespace valor
