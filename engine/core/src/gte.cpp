#include "valor/gte.h"

#include <vector>

#include "valor/fixed.h"
#include "valor/io.h"

namespace valor {

namespace {

int64_t trunc_div(int64_t a, int64_t b) {
    int64_t q = iabs(a) / iabs(b);
    return (a >= 0) == (b >= 0) ? q : -q;
}

int lzc(uint32_t a) {
    if (a & 0x80000000u) a = ~a;
    int n = 0;
    while (n < 32 && !(a & (0x80000000u >> n))) n++;
    return n;
}

}  // namespace

bool Gte::load(const std::string& data_dir, std::string* err) {
    std::string exe = data_dir + "/../SLUS_009.74";
    std::vector<uint8_t> d;
    if (!read_file(exe, d) || d.size() < 0x800) {
        if (err) *err = "can't read " + exe;
        return false;
    }
    uint32_t base = uint32_t(d[0x18]) | (uint32_t(d[0x19]) << 8) | (uint32_t(d[0x1A]) << 16) | (uint32_t(d[0x1B]) << 24);
    auto h = [&](uint32_t a, int16_t* out, int n) {
        size_t o = 0x800 + (a - base);
        if (o + 2 * size_t(n) > d.size()) return false;
        for (int i = 0; i < n; i++) out[i] = int16_t(d[o + 2 * i] | (d[o + 2 * i + 1] << 8));
        return true;
    };
    loaded = h(0x8002D4F8, ratan_, 1025) && h(0x8003440C, sqrt_, 192) && h(0x800347EC, sin1_, 1025);
    if (!loaded && err) *err = "bad executable: " + exe;
    return loaded;
}

int32_t Gte::ratan2(int32_t y, int32_t x) const {
    bool nx = false, ny = false;
    int64_t X = x, Y = y;
    if (X < 0) nx = true, X = -X;
    if (Y < 0) ny = true, Y = -Y;
    if (X == 0 && Y == 0) return 0;
    int32_t v;
    if (Y < X) {
        int64_t i = (Y & 0x7FE00000) ? trunc_div(Y, X >> 10) : trunc_div(Y << 10, X);
        v = ratan_[i];
    } else {
        int64_t i = (X & 0x7FE00000) ? trunc_div(X, Y >> 10) : trunc_div(X << 10, Y);
        v = 0x400 - ratan_[i];
    }
    if (nx) v = 0x800 - v;
    if (ny) v = -v;
    return v;
}

int32_t Gte::sqrt0(int32_t a) const {
    int n = lzc(uint32_t(a));
    if (n == 32) return 0;
    int n2 = n & ~1;
    int t1 = (31 - n2) >> 1;
    uint32_t ua = uint32_t(a);
    uint32_t t4 = n2 >= 24 ? (ua << (n2 - 24)) : (ua >> (24 - n2));
    uint32_t t5 = uint32_t(sqrt_[t4 - 0x40]);
    return int32_t(uint32_t(t5 << t1) >> 12);
}

int32_t Gte::sqrt12(int32_t a) const {
    int n = lzc(uint32_t(a));
    if (n == 32) return 0;
    int n2 = n & ~1;
    int t1 = (0x13 - n2) >> 1;
    uint32_t ua = uint32_t(a);
    uint32_t t4 = n2 >= 24 ? (ua << (n2 - 24)) : (ua >> (24 - n2));
    uint32_t t5 = uint32_t(sqrt_[t4 - 0x40]);
    return t1 >= 0 ? int32_t(t5 << t1) : int32_t(t5 >> -t1);
}

int32_t Gte::rsin(int32_t a) const {
    auto sin_1 = [&](int32_t x) -> int32_t {
        if (x <= 0x400) return sin1_[x];
        if (x <= 0x800) return sin1_[0x800 - x];
        if (x <= 0xC00) return -sin1_[x - 0x800];
        return -sin1_[0x1000 - x];
    };
    return a >= 0 ? sin_1(a & 0xFFF) : -sin_1((-a) & 0xFFF);
}

void rot_yxz(int32_t ax, int32_t ay, int32_t az, int16_t m[3][3]) {
    SinCos x = sincos_angle(ax), y = sincos_angle(ay), z = sincos_angle(az);
    int64_t sx = x.s, cx = x.c, sy = y.s, cy = y.c, sz = z.s, cz = z.c;
    m[1][2] = s16(-sx);
    m[0][2] = s16((sy * cx) >> 12);
    m[2][2] = s16((cy * cx) >> 12);
    m[1][0] = s16((sz * cx) >> 12);
    m[1][1] = s16((cz * cx) >> 12);
    int64_t t = (sy * sx) >> 12;
    m[0][0] = s16(((cy * cz) >> 12) + ((t * sz) >> 12));
    m[0][1] = s16(-((cy * sz) >> 12) + ((t * cz) >> 12));
    t = (cy * sx) >> 12;
    m[2][1] = s16(((sy * sz) >> 12) + ((t * cz) >> 12));
    m[2][0] = s16(-((sy * cz) >> 12) + ((t * sz) >> 12));
}

void quat_matrix(const int16_t q[4], int16_t m[3][3]) {
    int64_t q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
    m[0][0] = s16(((q0 * 2 * q0 + q3 * 2 * q3) >> 12) - 0x1000);
    m[1][0] = s16((q1 * 2 * q0 - q2 * 2 * q3) >> 12);
    m[2][0] = s16((q2 * 2 * q0 + q1 * 2 * q3) >> 12);
    m[0][1] = s16((q0 * 2 * q1 + q2 * 2 * q3) >> 12);
    m[1][1] = s16(((q1 * 2 * q1 + q3 * 2 * q3) >> 12) - 0x1000);
    m[2][1] = s16((q2 * 2 * q1 - q0 * 2 * q3) >> 12);
    m[0][2] = s16((-q3 * 2 * q1 + q2 * 2 * q0) >> 12);
    m[1][2] = s16((q2 * 2 * q1 + q3 * 2 * q0) >> 12);
    m[2][2] = s16((((q2 * q2) >> 12) + ((q3 * q3) >> 12)) * 2 - 0x1000);
}

void mvmva12(const int16_t m[3][3], const int32_t v[3], int32_t out[3]) {
    for (int i = 0; i < 3; i++)
        out[i] = s32((int64_t(m[i][0]) * v[0] + int64_t(m[i][1]) * v[1] + int64_t(m[i][2]) * v[2]) >> 12);
}

}  // namespace valor
