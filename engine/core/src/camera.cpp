#include "valor/camera.h"

#include "valor/fixed.h"

namespace valor {

namespace {

int64_t cdiv(int64_t a, int64_t b) {
    int64_t q = iabs(a) / iabs(b);
    return (a >= 0) == (b >= 0) ? q : -q;
}

// RotTransSV: (m . v >> 12) + t, saturated to s16.
void rot_trans_sv(const int16_t m[3][3], const int32_t v[3], int32_t out[3]) {
    int32_t mac[3], t[3] = {0, 0, 0};
    gte_mul(m, v, t, 12, mac, out);
}

}  // namespace

void Camera::think(const int32_t sol_pos[3], int32_t view_yaw) {
    CameraObj& o = obj;
    // camera_ease (0x80046C64): current points toward their targets, >> obj+0x10.
    int32_t sh = o.get(0x10);
    static const uint32_t pairs[9][2] = {{0x48, 0x54}, {0x4C, 0x58}, {0x50, 0x5C}, {0x18, 0x24}, {0x1C, 0x28},
                                         {0x20, 0x2C}, {0x78, 0x84}, {0x7C, 0x88}, {0x80, 0x8C}};
    for (const auto& p : pairs) {
        int32_t a = o.get(p[0]), b = o.get(p[1]);
        if (a != b) o.at(p[0]) = s32(int64_t(a) + (s32(int64_t(b) - a) >> sh));
    }
    // camera_look_points (0x800479D8), modes 0x480: the look point (obj+0x18.. turned by the view
    // yaw) and a side point (obj+0x78..), from the soldier's position.
    uint32_t mode = uint32_t(o.get(0x9C));
    if (mode & 0x480) {
        o.at(0xBC) = view_yaw;
        int16_t rot[3][3];
        rot_matrix_y(o.s16(0xBE), rot);
        int32_t va[3] = {o.s16(0x1A), 0, o.s16(0x22)}, vb[3] = {o.s16(0x7A), 0, o.s16(0x82)}, a[3], b[3];
        rot_trans_sv(rot, va, a);
        rot_trans_sv(rot, vb, b);
        o.at(0x0) = s32(int64_t(sol_pos[0]) + (int64_t(a[0]) << 16));
        o.at(0x8) = s32(int64_t(sol_pos[2]) + (int64_t(a[2]) << 16));
        o.at(0x4) = s32(int64_t(sol_pos[1]) + o.get(0x1C));
        o.at(0x6C) = s32(int64_t(sol_pos[0]) + (int64_t(b[0]) << 16));
        o.at(0x74) = s32(int64_t(sol_pos[2]) + (int64_t(b[2]) << 16));
        o.at(0x70) = s32(int64_t(sol_pos[1]) + (int64_t(o.get(0x94)) << 16));
    }
    // camera_place (0x800482A4), mode 0x80: the eye at the soldier, y + obj+0x94.
    if (mode & 0x80) {
        pos[0] = sol_pos[0];
        pos[1] = s32(int64_t(sol_pos[1]) + (int64_t(o.get(0x94)) << 16));
        pos[2] = sol_pos[2];
    }
    // The shake (camera_shake): y += amplitude, amplitude = -amplitude / 2, count - 1.
    if (o.s16(0xCE) > 0) {
        int32_t a = o.s16(0xCC);
        pos[1] = s32(int64_t(pos[1]) + (int64_t(a) << 16));
        o.at(0x4) = s32(int64_t(o.get(0x4)) + (int64_t(a) << 16));
        o.at(0x70) = s32(int64_t(o.get(0x70)) + (int64_t(a) << 16));
        o.set16(0xCC, int16_t(-(s32(int64_t(uint16_t(a)) << 16) >> 17)));
        o.set16(0xCE, int16_t(o.s16(0xCE) - 1));
    }
}

void Camera::setup(const Gte& g) {
    CameraObj& o = obj;
    if (uint32_t(o.get(0x9C)) & 0x1800) return;  // the death camera's modes: not ported
    int32_t look[3] = {o.s16(0x2), o.s16(0x6), o.s16(0xA)};
    int32_t eye[3] = {s16(pos[0] >> 16), s16(pos[1] >> 16), s16(pos[2] >> 16)};
    int32_t side[3] = {o.s16(0x6E), o.s16(0x72), o.s16(0x76)};
    for (int k = 0; k < 3; k++) o.at(0xE4 + 4 * k) = look[k], o.at(0xF0 + 4 * k) = eye[k];
    // view_lookat: rows right, up (x 5/8), forward, each x 2; t = M . -eye.
    int32_t d[3], sv[3], s_n[3], f_n[3], up[3], right[3];
    for (int k = 0; k < 3; k++) d[k] = s32(int64_t(look[k]) - eye[k]), sv[k] = s32(int64_t(side[k]) - eye[k]);
    g.vector_normal(sv, s_n);
    g.vector_normal(d, f_n);
    outer_product12(f_n, s_n, up);
    outer_product12(up, f_n, right);
    int16_t M[3][3];
    for (int k = 0; k < 3; k++) {
        M[0][k] = s16(int64_t(right[k]) * 2);
        M[1][k] = s16(cdiv(int64_t(up[k]) * 5, 8) * 2);
        M[2][k] = s16(int64_t(f_n[k]) * 2);
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o.set16(0x100 + 6 * i + 2 * j, M[i][j]);
    int32_t neg[3] = {-eye[0], -eye[1], -eye[2]}, t[3];
    apply_matrix_lv(M, neg, t);
    for (int k = 0; k < 3; k++) o.at(0x114 + 4 * k) = t[k];
}

void Camera::view(int16_t m[3][3], int32_t t[3]) const {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i][j] = obj.s16(0x100 + 6 * i + 2 * j);
    for (int k = 0; k < 3; k++) t[k] = obj.get(0x114 + 4 * k);
}

void Camera::pitch_target(const Gte& g, int32_t pitch) {
    // view_pitch_target (0x8007BADC): obj+0x28 = eye + 2 - d tan(pitch), d the look offset's
    // cheap length.
    int32_t a = int32_t(iabs(obj.s16(0x26))), c = int32_t(iabs(obj.s16(0x2E)));
    int32_t d = a + c - (std::min(a, c) >> 1);
    int16_t p = int16_t(uint32_t(pitch) >> 16);
    int64_t num = -(int64_t(d) * g.rsin(p));
    int64_t v = cdiv(num, g.rcos(p)) + obj.get(0x98);
    obj.at(0x28) = s32(v << 16);
}

}  // namespace valor
