#include "valor/gte.h"

#include <cmath>
#include <cstring>
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
    loaded = h(0x8002D4F8, ratan_, 1025) && h(0x8003440C - 2 * 192, sqrtw_, 384) && h(0x800347EC, sin1_, 1025) &&
             h(0x8002DD3C, vn_, 192);
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
    int32_t t4 = n2 >= 24 ? int32_t(uint32_t(a) << (n2 - 24)) : (a >> (24 - n2));
    uint32_t t5 = uint32_t(int32_t(sqrtw_[192 + t4 - 0x40]));
    return int32_t(uint32_t(t5 << t1) >> 12);
}

int32_t Gte::sqrt12(int32_t a) const {
    int n = lzc(uint32_t(a));
    if (n == 32) return 0;
    int n2 = n & ~1;
    int t1 = (0x13 - n2) >> 1;
    int32_t t4 = n2 >= 24 ? int32_t(uint32_t(a) << (n2 - 24)) : (a >> (24 - n2));
    uint32_t t5 = uint32_t(int32_t(sqrtw_[192 + t4 - 0x40]));
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

int32_t Gte::rcos(int32_t a) const {
    a = int32_t(iabs(a) & 0xFFF);
    if (a <= 0x400) return sin1_[0x400 - a];
    if (a <= 0x800) return -sin1_[a - 0x400];
    if (a <= 0xC00) return -sin1_[0xC00 - a];
    return sin1_[a - 0xC00];
}

int64_t wrap44(int64_t v) {
    v &= (int64_t(1) << 44) - 1;
    return (v & (int64_t(1) << 43)) ? v - (int64_t(1) << 44) : v;
}

int32_t sat16(int64_t v) { return v < -0x8000 ? -0x8000 : v > 0x7FFF ? 0x7FFF : int32_t(v); }

void gte_mul(const int16_t m[3][3], const int32_t v[3], const int32_t t[3], int sf, int32_t mac[3], int32_t ir[3]) {
    for (int r = 0; r < 3; r++) {
        int64_t acc = int64_t(t ? t[r] : 0) * 0x1000;
        acc = wrap44(acc + int64_t(m[r][0]) * v[0]);
        acc = wrap44(acc + int64_t(m[r][1]) * v[1]);
        acc = wrap44(acc + int64_t(m[r][2]) * v[2]);
        mac[r] = s32(acc >> sf);
        if (ir) ir[r] = sat16(mac[r]);
    }
}

void outer_product12(const int32_t a[3], const int32_t b[3], int32_t out[3]) {
    int64_t d[3] = {s16(a[0]), s16(a[1]), s16(a[2])}, r[3] = {s16(b[0]), s16(b[1]), s16(b[2])};
    out[0] = s32(wrap44(r[2] * d[1] - r[1] * d[2]) >> 12);
    out[1] = s32(wrap44(r[0] * d[2] - r[2] * d[0]) >> 12);
    out[2] = s32(wrap44(r[1] * d[0] - r[0] * d[1]) >> 12);
}

void apply_matrix_lv(const int16_t m[3][3], const int32_t v[3], int32_t out[3]) {
    int32_t hi[3], lo[3];
    for (int i = 0; i < 3; i++) {
        if (v[i] >= 0) {
            hi[i] = v[i] >> 15, lo[i] = v[i] & 0x7FFF;
        } else {
            int64_t y = -int64_t(v[i]);
            hi[i] = int32_t(-(y >> 15)), lo[i] = int32_t(-(y & 0x7FFF));
        }
        hi[i] = s16(hi[i]);
    }
    int32_t mh[3], ml[3];
    gte_mul(m, hi, nullptr, 0, mh, nullptr);
    gte_mul(m, lo, nullptr, 12, ml, nullptr);
    for (int i = 0; i < 3; i++) {
        int64_t b = mh[i] < 0 ? -(int64_t(-int64_t(mh[i])) << 3) : int64_t(mh[i]) << 3;
        out[i] = s32(ml[i] + b);
    }
}

void rot_matrix_y(int32_t r, int16_t m[3][3]) {
    SinCos sc = sincos_angle(r);
    int16_t s = int16_t(sc.s), c = int16_t(sc.c);
    int16_t v[3][3] = {{c, 0, s}, {0, 0x1000, 0}, {s16(-sc.s), 0, c}};
    std::memcpy(m, v, sizeof v);
}

void Gte::vector_normal(const int32_t v[3], int32_t out[3]) const {
    int64_t t[3] = {s16(v[0]), s16(v[1]), s16(v[2])};
    int32_t n = s32(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    int lz = lzc(uint32_t(n)) & ~1;
    int sh = (31 - lz) >> 1;
    int32_t x = lz - 24 >= 0 ? s32(int64_t(n) << (lz - 24)) : (n >> (24 - lz));
    int32_t k = vn_[x - 0x40];
    for (int i = 0; i < 3; i++) out[i] = s32(int64_t(k) * t[i]) >> sh;
}

namespace {
// RTPS's division: h / sz3 by the UNR table, as the GTE does it.
uint32_t gte_divide(uint32_t h, uint32_t sz3) {
    static uint8_t unr[0x101];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 0x101; i++) {
            int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
            unr[i] = uint8_t(v < 0 ? 0 : v);
        }
        init = true;
    }
    if (h >= sz3 * 2) return 0x1FFFF;
    int z = 0;
    while (z < 16 && !(sz3 & (0x8000u >> z))) z++;
    int64_t n = int64_t(h) << z;
    int64_t d = int64_t(sz3) << z;
    int64_t u = unr[(d - 0x7FC0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    int64_t r = (n * d + 0x8000) >> 16;
    return r > 0x1FFFF ? 0x1FFFF : uint32_t(r);
}
}  // namespace

void Gte::set_projection(double aspect, double hfov_deg) {
    double squeeze = (4.0 / 3.0) / aspect, k = 1.0;
    if (hfov_deg > 0) k = (120.0 / 400.0) / (std::tan(hfov_deg * 3.14159265358979323846 / 360.0) / aspect);
    proj_kx = k * squeeze, proj_ky = k;
}

void Gte::rtps(const int16_t rt[3][3], const int32_t tr[3], uint32_t h, int32_t ofx, int32_t ofy, const int32_t v[3],
               int32_t& sx, int32_t& sy) const {
    int64_t acc[3];
    for (int r = 0; r < 3; r++) {
        int64_t a = int64_t(tr[r]) * 0x1000;
        a = wrap44(a + int64_t(rt[r][0]) * v[0]);
        a = wrap44(a + int64_t(rt[r][1]) * v[1]);
        a = wrap44(a + int64_t(rt[r][2]) * v[2]);
        acc[r] = a;
    }
    int32_t ir1 = sat16(s32(acc[0] >> 12)), ir2 = sat16(s32(acc[1] >> 12));
    int32_t z12 = s32(acc[2] >> 12);
    uint32_t sz3 = z12 < 0 ? 0 : z12 > 0xFFFF ? 0xFFFF : uint32_t(z12);
    uint32_t q = gte_divide(h, sz3);
    int64_t x = int64_t(q) * ir1, y = int64_t(q) * ir2;
    if (proj_kx != 1.0 || proj_ky != 1.0) x = int64_t(double(x) * proj_kx), y = int64_t(double(y) * proj_ky);
    x += ofx, y += ofy;
    int32_t X = s32(x) >> 16, Y = s32(y) >> 16;
    sx = X < -0x400 ? -0x400 : X > 0x3FF ? 0x3FF : X;
    sy = Y < -0x400 ? -0x400 : Y > 0x3FF ? 0x3FF : Y;
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
