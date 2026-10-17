// GTE (Geometry Transformation Engine, COP2), following the psx-spx documentation:
// fixed-point math, saturation and FLAG bits, and the hardware's UNR division.
#include <algorithm>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "interp.h"
#include "runtime.h"

namespace {

// ---------------------------------------------------------------------------------------------
// Registers

struct Vec3s { int16_t x, y, z; };

struct Gte {
    // Data registers
    Vec3s v[3];
    uint8_t rgbc[4];
    uint16_t otz;
    int16_t ir[4];
    int16_t sxy[4][2];  // SXY0, SXY1, SXY2, SXYP (SXYP mirrors SXY2 on read)
    uint16_t sz[4];
    uint32_t rgb[3];    // RGB0..RGB2 FIFO
    uint32_t res1;
    int32_t mac[4];
    uint32_t lzcs, lzcr;
    // Control registers
    int16_t rt[3][3], llm[3][3], lcm[3][3];
    int32_t tr[3], bk[3], fc[3];
    int32_t ofx, ofy;
    uint16_t h;
    int16_t dqa;
    int32_t dqb;
    int16_t zsf3, zsf4;
    uint32_t flag;
} g;

// FLAG bits
constexpr uint32_t F_MAC1_POS = 1u << 30, F_MAC2_POS = 1u << 29, F_MAC3_POS = 1u << 28;
constexpr uint32_t F_MAC1_NEG = 1u << 27, F_MAC2_NEG = 1u << 26, F_MAC3_NEG = 1u << 25;
constexpr uint32_t F_IR1 = 1u << 24, F_IR2 = 1u << 23, F_IR3 = 1u << 22;
constexpr uint32_t F_R = 1u << 21, F_G = 1u << 20, F_B = 1u << 19;
constexpr uint32_t F_SZ3_OTZ = 1u << 18, F_DIV = 1u << 17;
constexpr uint32_t F_MAC0_POS = 1u << 16, F_MAC0_NEG = 1u << 15;
constexpr uint32_t F_SX2 = 1u << 14, F_SY2 = 1u << 13, F_IR0 = 1u << 12;

int sf_shift;   // 12 when the sf bit is set, else 0
uint32_t current_pc;  // address of the GTE command being executed

// Projection adjustments for widescreen and FOV (see gte_set_projection).
struct {
    bool active;
    uint16_t world_h;
    struct { double x, y; } world{1, 1}, other{1, 1};
} projection;
bool lm;        // saturate IR to 0..7FFF instead of -8000..7FFF

// ---------------------------------------------------------------------------------------------
// Saturation helpers

// 44-bit MAC1..3 overflow check (on the unshifted value), then shift.
int64_t check_mac(int i, int64_t v) {
    static const uint32_t pos[] = {0, F_MAC1_POS, F_MAC2_POS, F_MAC3_POS};
    static const uint32_t neg[] = {0, F_MAC1_NEG, F_MAC2_NEG, F_MAC3_NEG};
    if (v > 0x7FFFFFFFFFFLL) g.flag |= pos[i];
    else if (v < -0x80000000000LL) g.flag |= neg[i];
    return (v << 20) >> 20;  // wrap to 44 bits
}

int32_t set_mac(int i, int64_t v) {
    v = check_mac(i, v);
    g.mac[i] = int32_t(v >> sf_shift);
    return g.mac[i];
}

int32_t set_mac0(int64_t v) {
    if (v > 0x7FFFFFFFLL) g.flag |= F_MAC0_POS;
    else if (v < -0x80000000LL) g.flag |= F_MAC0_NEG;
    g.mac[0] = int32_t(v);
    return g.mac[0];
}

int16_t sat_ir(int i, int32_t v, bool lm_) {
    static const uint32_t bit[] = {0, F_IR1, F_IR2, F_IR3};
    int32_t lo = lm_ ? 0 : -0x8000;
    if (v < lo) { g.flag |= bit[i]; return int16_t(lo); }
    if (v > 0x7FFF) { g.flag |= bit[i]; return 0x7FFF; }
    return int16_t(v);
}

void set_ir(int i, int32_t v, bool lm_) { g.ir[i] = sat_ir(i, v, lm_); }

void set_ir0(int32_t v) {
    if (v < 0) { g.flag |= F_IR0; v = 0; }
    else if (v > 0x1000) { g.flag |= F_IR0; v = 0x1000; }
    g.ir[0] = int16_t(v);
}

void mac_to_ir() {
    set_ir(1, g.mac[1], lm);
    set_ir(2, g.mac[2], lm);
    set_ir(3, g.mac[3], lm);
}

uint8_t sat_color(int i, int32_t v) {
    static const uint32_t bit[] = {F_R, F_G, F_B};
    if (v < 0) { g.flag |= bit[i]; return 0; }
    if (v > 0xFF) { g.flag |= bit[i]; return 0xFF; }
    return uint8_t(v);
}

void push_color() {
    uint8_t r = sat_color(0, g.mac[1] >> 4), gg = sat_color(1, g.mac[2] >> 4), b = sat_color(2, g.mac[3] >> 4);
    g.rgb[0] = g.rgb[1];
    g.rgb[1] = g.rgb[2];
    g.rgb[2] = uint32_t(r) | uint32_t(gg) << 8 | uint32_t(b) << 16 | uint32_t(g.rgbc[3]) << 24;
}

void push_sz(int32_t v) {
    if (v < 0) { g.flag |= F_SZ3_OTZ; v = 0; }
    else if (v > 0xFFFF) { g.flag |= F_SZ3_OTZ; v = 0xFFFF; }
    g.sz[0] = g.sz[1];
    g.sz[1] = g.sz[2];
    g.sz[2] = g.sz[3];
    g.sz[3] = uint16_t(v);
}

void push_sxy(int32_t x, int32_t y) {
    if (x < -0x400) { g.flag |= F_SX2; x = -0x400; }
    else if (x > 0x3FF) { g.flag |= F_SX2; x = 0x3FF; }
    if (y < -0x400) { g.flag |= F_SY2; y = -0x400; }
    else if (y > 0x3FF) { g.flag |= F_SY2; y = 0x3FF; }
    for (int i = 0; i < 2; i++) {
        g.sxy[0][i] = g.sxy[1][i];
        g.sxy[1][i] = g.sxy[2][i];
    }
    g.sxy[2][0] = int16_t(x);
    g.sxy[2][1] = int16_t(y);
}

void set_otz(int64_t v) {
    int64_t z = v >> 12;
    if (z < 0) { g.flag |= F_SZ3_OTZ; z = 0; }
    else if (z > 0xFFFF) { g.flag |= F_SZ3_OTZ; z = 0xFFFF; }
    g.otz = uint16_t(z);
}

// ---------------------------------------------------------------------------------------------
// Division (Unsigned Newton-Raphson, as the hardware does it)

uint8_t unr_table[0x101];

void init_unr() {
    for (int i = 0; i < 0x101; i++) unr_table[i] = uint8_t(std::max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101));
}

uint32_t divide(uint16_t h, uint16_t sz3) {
    if (h >= sz3 * 2) {
        g.flag |= F_DIV;
        return 0x1FFFF;
    }
    int z = __builtin_clz(uint32_t(sz3)) - 16;  // leading zeros in 16 bits
    uint32_t n = uint32_t(h) << z;
    uint32_t d = uint32_t(sz3) << z;
    uint32_t u = unr_table[(d - 0x7FC0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    uint32_t r = uint32_t((uint64_t(n) * d + 0x8000) >> 16);
    return std::min<uint32_t>(r, 0x1FFFF);
}

// ---------------------------------------------------------------------------------------------
// Commands

// MAC = translation * t_scale + matrix * vector.
void mul_matrix(const int16_t m[3][3], const int32_t t[3], int64_t t_scale, int16_t vx, int16_t vy, int16_t vz) {
    for (int r = 0; r < 3; r++) {
        int64_t acc = int64_t(t[r]) * t_scale;
        acc = check_mac(r + 1, acc + int64_t(m[r][0]) * vx);
        acc = check_mac(r + 1, acc + int64_t(m[r][1]) * vy);
        acc = check_mac(r + 1, acc + int64_t(m[r][2]) * vz);
        set_mac(r + 1, acc);
    }
}

}  // namespace

namespace psx {
uint64_t gte_projections;  // RTPS/RTPT vertices projected so far
}

namespace {

// VALOR_TRACE_GTE_MAT: per frame, the transforms (RT, TR, H) used for projections and how often.
struct MatUse { int16_t rt[3][3]; int32_t tr[3]; uint16_t h; uint32_t pc; int count; };
std::vector<MatUse> mat_uses;
bool trace_mat = getenv("VALOR_TRACE_GTE_MAT");

void note_matrix() {
    for (auto &m : mat_uses)
        if (!memcmp(m.rt, g.rt, sizeof m.rt) && !memcmp(m.tr, g.tr, sizeof m.tr) && m.h == g.h) { m.count++; return; }
    MatUse m{};
    memcpy(m.rt, g.rt, sizeof m.rt);
    memcpy(m.tr, g.tr, sizeof m.tr);
    m.h = g.h;
    m.pc = current_pc;
    m.count = 1;
    mat_uses.push_back(m);
}

void rtp(int n, bool last) {
    psx::gte_projections++;
    if (trace_mat) note_matrix();
    const Vec3s &v = g.v[n];
    int64_t mac3_raw = 0, accs[3];
    for (int r = 0; r < 3; r++) {
        int64_t acc = int64_t(g.tr[r]) * 0x1000;
        acc = check_mac(r + 1, acc + int64_t(g.rt[r][0]) * v.x);
        acc = check_mac(r + 1, acc + int64_t(g.rt[r][1]) * v.y);
        acc = check_mac(r + 1, acc + int64_t(g.rt[r][2]) * v.z);
        if (r == 2) mac3_raw = acc;
        accs[r] = acc;
        set_mac(r + 1, acc);
    }
    set_ir(1, g.mac[1], lm);
    set_ir(2, g.mac[2], lm);
    // IR3 is saturated normally, but its FLAG bit is checked against the unshifted value >> 12.
    int32_t z12 = int32_t(mac3_raw >> 12);
    g.ir[3] = sat_ir(3, g.mac[3], lm);
    g.flag &= ~F_IR3;
    if (z12 < -0x8000 || z12 > 0x7FFF) g.flag |= F_IR3;
    push_sz(z12);
    uint32_t q = divide(g.h, g.sz[3]);
    int64_t sx = int64_t(q) * g.ir[1], sy = int64_t(q) * g.ir[2];
    bool hud = std::find(psx::config.hud_projections.begin(), psx::config.hud_projections.end(), current_pc) !=
               psx::config.hud_projections.end();
    double kx = 1, ky = 1;
    if (projection.active) {
        // Widescreen / FOV: scale the projected offset from the screen center. HUD elements that
        // are projected (config.hud_projections, by cop2 instruction address) keep their size.
        // VALOR_TRACE_GTE_PC=1 lists where projections come from (H, cop2 address, result).
        bool world = g.h == projection.world_h;
        if (getenv("VALOR_TRACE_GTE_PC")) {
            static uint32_t seen[64];
            bool known = false;
            for (uint32_t v : seen) known |= v == current_pc;
            if (!known) {
                for (auto &v : seen) if (!v) { v = current_pc; break; }
                psx::log("gte: H=%u projection at %08X -> (%d,%d)", g.h, current_pc, int32_t((sx + g.ofx) >> 16), int32_t((sy + g.ofy) >> 16));
            }
        }
        if (!hud) {
            const auto &k = world ? projection.world : projection.other;
            sx = int64_t(double(sx) * k.x);
            sy = int64_t(double(sy) * k.y);
            kx = k.x;
            ky = k.y;
        }
    }
    sx += g.ofx;
    sy += g.ofy;
    set_mac0(sx);
    int32_t x = int32_t(sx >> 16);
    set_mac0(sy);
    int32_t y = int32_t(sy >> 16);
    push_sxy(x, y);
    if (!hud && psx::interp_enabled()) {
        int16_t model[3] = {v.x, v.y, v.z};
        psx::interp_record(g.sxy[2][0], g.sxy[2][1], accs, g.h, kx, ky, g.rt, g.tr, model, current_pc);
    }
    if (last) {
        int64_t dq = int64_t(g.dqa) * q + g.dqb;
        set_mac0(dq);
        set_ir0(int32_t(dq >> 12));
    }
}

// Depth cue toward the far color: MAC = base + (FC - base) * IR0.
void depth_cue(int64_t m1, int64_t m2, int64_t m3) {
    int64_t base[3] = {m1, m2, m3};
    for (int i = 0; i < 3; i++) {
        int64_t fc = int64_t(g.fc[i]) << 12;
        int32_t diff = set_mac(i + 1, fc - base[i]);
        int16_t sat = sat_ir(i + 1, diff, false);
        set_mac(i + 1, base[i] + int64_t(g.ir[0]) * sat);
    }
}

const int32_t NO_TRANSLATION[3] = {0, 0, 0};

void ncd_core(int n, bool color, bool depth) {
    const Vec3s &v = g.v[n];
    mul_matrix(g.llm, NO_TRANSLATION, 0, v.x, v.y, v.z);
    mac_to_ir();
    mul_matrix(g.lcm, g.bk, 0x1000, g.ir[1], g.ir[2], g.ir[3]);
    mac_to_ir();
    if (color || depth) {
        int64_t m1 = (int64_t(g.rgbc[0]) * g.ir[1]) << 4;
        int64_t m2 = (int64_t(g.rgbc[1]) * g.ir[2]) << 4;
        int64_t m3 = (int64_t(g.rgbc[2]) * g.ir[3]) << 4;
        if (depth) {
            depth_cue(m1, m2, m3);
        } else {
            set_mac(1, m1);
            set_mac(2, m2);
            set_mac(3, m3);
        }
        mac_to_ir();
    }
    push_color();
}

void cmd(uint32_t op) {
    sf_shift = (op & (1u << 19)) ? 12 : 0;
    lm = op & (1u << 10);
    g.flag = 0;
    switch (op & 0x3F) {
    case 0x01:  // RTPS
        rtp(0, true);
        break;
    case 0x30:  // RTPT
        rtp(0, false);
        rtp(1, false);
        rtp(2, true);
        break;
    case 0x06: {  // NCLIP
        int64_t s = int64_t(g.sxy[0][0]) * g.sxy[1][1] + int64_t(g.sxy[1][0]) * g.sxy[2][1] +
                    int64_t(g.sxy[2][0]) * g.sxy[0][1] - int64_t(g.sxy[0][0]) * g.sxy[2][1] -
                    int64_t(g.sxy[1][0]) * g.sxy[0][1] - int64_t(g.sxy[2][0]) * g.sxy[1][1];
        set_mac0(s);
        break;
    }
    case 0x0C: {  // OP (outer product)
        int32_t d1 = g.rt[0][0], d2 = g.rt[1][1], d3 = g.rt[2][2];
        set_mac(1, int64_t(g.ir[3]) * d2 - int64_t(g.ir[2]) * d3);
        set_mac(2, int64_t(g.ir[1]) * d3 - int64_t(g.ir[3]) * d1);
        set_mac(3, int64_t(g.ir[2]) * d1 - int64_t(g.ir[1]) * d2);
        mac_to_ir();
        break;
    }
    case 0x10: {  // DPCS
        depth_cue(int64_t(g.rgbc[0]) << 16, int64_t(g.rgbc[1]) << 16, int64_t(g.rgbc[2]) << 16);
        mac_to_ir();
        push_color();
        break;
    }
    case 0x2A:  // DPCT
        for (int i = 0; i < 3; i++) {
            uint32_t c = g.rgb[0];
            depth_cue(int64_t(c & 0xFF) << 16, int64_t((c >> 8) & 0xFF) << 16, int64_t((c >> 16) & 0xFF) << 16);
            mac_to_ir();
            push_color();
        }
        break;
    case 0x11:  // INTPL
        depth_cue(int64_t(g.ir[1]) << 12, int64_t(g.ir[2]) << 12, int64_t(g.ir[3]) << 12);
        mac_to_ir();
        push_color();
        break;
    case 0x12: {  // MVMVA
        int mx = (op >> 17) & 3, vx = (op >> 15) & 3, tx = (op >> 13) & 3;
        int16_t garbage[3][3] = {{int16_t(-(int(g.rgbc[0]) << 4)), int16_t(g.rgbc[0] << 4), g.ir[0]},
                                 {g.rt[0][2], g.rt[0][2], g.rt[0][2]},
                                 {g.rt[1][1], g.rt[1][1], g.rt[1][1]}};
        const int16_t(*m)[3] = mx == 0 ? g.rt : mx == 1 ? g.llm : mx == 2 ? g.lcm : garbage;
        int16_t vv[3];
        if (vx < 3) { vv[0] = g.v[vx].x; vv[1] = g.v[vx].y; vv[2] = g.v[vx].z; }
        else { vv[0] = g.ir[1]; vv[1] = g.ir[2]; vv[2] = g.ir[3]; }
        const int32_t *t = tx == 0 ? g.tr : tx == 1 ? g.bk : tx == 2 ? g.fc : NO_TRANSLATION;
        if (tx == 2) {
            // Far color translation is buggy: the first column's result is only used for flags.
            for (int r = 0; r < 3; r++) {
                int64_t acc = int64_t(t[r]) * 0x1000 + int64_t(m[r][0]) * vv[0];
                set_ir(r + 1, int32_t(check_mac(r + 1, acc) >> sf_shift), false);
                acc = check_mac(r + 1, int64_t(m[r][1]) * vv[1]);
                acc = check_mac(r + 1, acc + int64_t(m[r][2]) * vv[2]);
                set_mac(r + 1, acc);
            }
        } else {
            mul_matrix(m, t, 0x1000, vv[0], vv[1], vv[2]);
        }
        mac_to_ir();
        break;
    }
    case 0x13: ncd_core(0, false, true); break;  // NCDS
    case 0x16:  // NCDT
        for (int i = 0; i < 3; i++) ncd_core(i, false, true);
        break;
    case 0x1B: ncd_core(0, true, false); break;  // NCCS
    case 0x3F:  // NCCT
        for (int i = 0; i < 3; i++) ncd_core(i, true, false);
        break;
    case 0x1E: ncd_core(0, false, false); break;  // NCS
    case 0x20:  // NCT
        for (int i = 0; i < 3; i++) ncd_core(i, false, false);
        break;
    case 0x14:    // CDP
    case 0x1C: {  // CC
        mul_matrix(g.lcm, g.bk, 0x1000, g.ir[1], g.ir[2], g.ir[3]);
        mac_to_ir();
        int64_t m1 = (int64_t(g.rgbc[0]) * g.ir[1]) << 4;
        int64_t m2 = (int64_t(g.rgbc[1]) * g.ir[2]) << 4;
        int64_t m3 = (int64_t(g.rgbc[2]) * g.ir[3]) << 4;
        if ((op & 0x3F) == 0x14) depth_cue(m1, m2, m3);
        else { set_mac(1, m1); set_mac(2, m2); set_mac(3, m3); }
        mac_to_ir();
        push_color();
        break;
    }
    case 0x28:  // SQR
        for (int i = 1; i <= 3; i++) set_mac(i, int64_t(g.ir[i]) * g.ir[i]);
        mac_to_ir();
        break;
    case 0x29: {  // DCPL
        int64_t m1 = (int64_t(g.rgbc[0]) * g.ir[1]) << 4;
        int64_t m2 = (int64_t(g.rgbc[1]) * g.ir[2]) << 4;
        int64_t m3 = (int64_t(g.rgbc[2]) * g.ir[3]) << 4;
        depth_cue(m1, m2, m3);
        mac_to_ir();
        push_color();
        break;
    }
    case 0x2D:  // AVSZ3
        set_otz(set_mac0(int64_t(g.zsf3) * (g.sz[1] + g.sz[2] + g.sz[3])));
        break;
    case 0x2E:  // AVSZ4
        set_otz(set_mac0(int64_t(g.zsf4) * (g.sz[0] + g.sz[1] + g.sz[2] + g.sz[3])));
        break;
    case 0x3D:  // GPF
        for (int i = 1; i <= 3; i++) set_mac(i, int64_t(g.ir[0]) * g.ir[i]);
        mac_to_ir();
        push_color();
        break;
    case 0x3E:  // GPL
        for (int i = 1; i <= 3; i++) set_mac(i, (int64_t(g.mac[i]) << sf_shift) + int64_t(g.ir[0]) * g.ir[i]);
        mac_to_ir();
        push_color();
        break;
    default:
        psx::log("gte: unknown command %07X", op);
        break;
    }
    // Bit 31: error summary (bits 30-23 and 18-13).
    if (g.flag & 0x7F87E000u) g.flag |= 0x80000000u;
}

// ---------------------------------------------------------------------------------------------
// Register access

uint32_t lzc(uint32_t v) {
    if (int32_t(v) < 0) v = ~v;
    return v ? __builtin_clz(v) : 32;
}

uint32_t read_data(int r) {
    switch (r) {
    case 0: return uint16_t(g.v[0].x) | uint32_t(uint16_t(g.v[0].y)) << 16;
    case 1: return uint32_t(int32_t(g.v[0].z));
    case 2: return uint16_t(g.v[1].x) | uint32_t(uint16_t(g.v[1].y)) << 16;
    case 3: return uint32_t(int32_t(g.v[1].z));
    case 4: return uint16_t(g.v[2].x) | uint32_t(uint16_t(g.v[2].y)) << 16;
    case 5: return uint32_t(int32_t(g.v[2].z));
    case 6: return g.rgbc[0] | g.rgbc[1] << 8 | g.rgbc[2] << 16 | uint32_t(g.rgbc[3]) << 24;
    case 7: return g.otz;
    case 8: case 9: case 10: case 11: return uint32_t(int32_t(g.ir[r - 8]));
    case 12: case 13: case 14:
        return uint16_t(g.sxy[r - 12][0]) | uint32_t(uint16_t(g.sxy[r - 12][1])) << 16;
    case 15: return uint16_t(g.sxy[2][0]) | uint32_t(uint16_t(g.sxy[2][1])) << 16;
    case 16: case 17: case 18: case 19: return g.sz[r - 16];
    case 20: case 21: case 22: return g.rgb[r - 20];
    case 23: return g.res1;
    case 24: case 25: case 26: case 27: return uint32_t(g.mac[r - 24]);
    case 28: case 29: {  // IRGB / ORGB: IR1..3 as 5:5:5
        auto c = [](int16_t v) { return uint32_t(std::clamp(v / 0x80, 0, 0x1F)); };
        return c(g.ir[1]) | c(g.ir[2]) << 5 | c(g.ir[3]) << 10;
    }
    case 30: return g.lzcs;
    case 31: return g.lzcr;
    }
    return 0;
}

void write_data(int r, uint32_t v) {
    int16_t lo = int16_t(v), hi = int16_t(v >> 16);
    switch (r) {
    case 0: g.v[0].x = lo; g.v[0].y = hi; break;
    case 1: g.v[0].z = lo; break;
    case 2: g.v[1].x = lo; g.v[1].y = hi; break;
    case 3: g.v[1].z = lo; break;
    case 4: g.v[2].x = lo; g.v[2].y = hi; break;
    case 5: g.v[2].z = lo; break;
    case 6: for (int i = 0; i < 4; i++) g.rgbc[i] = uint8_t(v >> (8 * i)); break;
    case 7: g.otz = uint16_t(v); break;
    case 8: case 9: case 10: case 11: g.ir[r - 8] = lo; break;
    case 12: case 13: case 14: g.sxy[r - 12][0] = lo; g.sxy[r - 12][1] = hi; break;
    case 15:  // SXYP: push onto the FIFO
        for (int i = 0; i < 2; i++) { g.sxy[0][i] = g.sxy[1][i]; g.sxy[1][i] = g.sxy[2][i]; }
        g.sxy[2][0] = lo;
        g.sxy[2][1] = hi;
        break;
    case 16: case 17: case 18: case 19: g.sz[r - 16] = uint16_t(v); break;
    case 20: case 21: case 22: g.rgb[r - 20] = v; break;
    case 23: g.res1 = v; break;
    case 24: case 25: case 26: case 27: g.mac[r - 24] = int32_t(v); break;
    case 28:  // IRGB: sets IR1..3 from 5:5:5
        g.ir[1] = int16_t((v & 0x1F) * 0x80);
        g.ir[2] = int16_t(((v >> 5) & 0x1F) * 0x80);
        g.ir[3] = int16_t(((v >> 10) & 0x1F) * 0x80);
        break;
    case 29: break;  // ORGB is read-only
    case 30: g.lzcs = v; g.lzcr = lzc(v); break;
    case 31: break;  // LZCR is read-only
    }
}

uint32_t read_ctrl(int r) {
    auto pair = [](int16_t a, int16_t b) { return uint16_t(a) | uint32_t(uint16_t(b)) << 16; };
    switch (r) {
    case 0: return pair(g.rt[0][0], g.rt[0][1]);
    case 1: return pair(g.rt[0][2], g.rt[1][0]);
    case 2: return pair(g.rt[1][1], g.rt[1][2]);
    case 3: return pair(g.rt[2][0], g.rt[2][1]);
    case 4: return uint32_t(int32_t(g.rt[2][2]));
    case 5: case 6: case 7: return uint32_t(g.tr[r - 5]);
    case 8: return pair(g.llm[0][0], g.llm[0][1]);
    case 9: return pair(g.llm[0][2], g.llm[1][0]);
    case 10: return pair(g.llm[1][1], g.llm[1][2]);
    case 11: return pair(g.llm[2][0], g.llm[2][1]);
    case 12: return uint32_t(int32_t(g.llm[2][2]));
    case 13: case 14: case 15: return uint32_t(g.bk[r - 13]);
    case 16: return pair(g.lcm[0][0], g.lcm[0][1]);
    case 17: return pair(g.lcm[0][2], g.lcm[1][0]);
    case 18: return pair(g.lcm[1][1], g.lcm[1][2]);
    case 19: return pair(g.lcm[2][0], g.lcm[2][1]);
    case 20: return uint32_t(int32_t(g.lcm[2][2]));
    case 21: case 22: case 23: return uint32_t(g.fc[r - 21]);
    case 24: return uint32_t(g.ofx);
    case 25: return uint32_t(g.ofy);
    case 26: return uint32_t(int32_t(int16_t(g.h)));  // hardware quirk: H reads sign-extended
    case 27: return uint32_t(int32_t(g.dqa));
    case 28: return uint32_t(g.dqb);
    case 29: return uint32_t(int32_t(g.zsf3));
    case 30: return uint32_t(int32_t(g.zsf4));
    case 31: return g.flag;
    }
    return 0;
}

void write_ctrl(int r, uint32_t v) {
    int16_t lo = int16_t(v), hi = int16_t(v >> 16);
    switch (r) {
    case 0: g.rt[0][0] = lo; g.rt[0][1] = hi; break;
    case 1: g.rt[0][2] = lo; g.rt[1][0] = hi; break;
    case 2: g.rt[1][1] = lo; g.rt[1][2] = hi; break;
    case 3: g.rt[2][0] = lo; g.rt[2][1] = hi; break;
    case 4: g.rt[2][2] = lo; break;
    case 5: case 6: case 7: g.tr[r - 5] = int32_t(v); break;
    case 8: g.llm[0][0] = lo; g.llm[0][1] = hi; break;
    case 9: g.llm[0][2] = lo; g.llm[1][0] = hi; break;
    case 10: g.llm[1][1] = lo; g.llm[1][2] = hi; break;
    case 11: g.llm[2][0] = lo; g.llm[2][1] = hi; break;
    case 12: g.llm[2][2] = lo; break;
    case 13: case 14: case 15: g.bk[r - 13] = int32_t(v); break;
    case 16: g.lcm[0][0] = lo; g.lcm[0][1] = hi; break;
    case 17: g.lcm[0][2] = lo; g.lcm[1][0] = hi; break;
    case 18: g.lcm[1][1] = lo; g.lcm[1][2] = hi; break;
    case 19: g.lcm[2][0] = lo; g.lcm[2][1] = hi; break;
    case 20: g.lcm[2][2] = lo; break;
    case 21: case 22: case 23: g.fc[r - 21] = int32_t(v); break;
    case 24: g.ofx = int32_t(v); break;
    case 25: g.ofy = int32_t(v); break;
    case 26: g.h = uint16_t(v); break;
    case 27: g.dqa = lo; break;
    case 28: g.dqb = int32_t(v); break;
    case 29: g.zsf3 = lo; break;
    case 30: g.zsf4 = lo; break;
    case 31:
        g.flag = v & 0x7FFFF000u;
        if (g.flag & 0x7F87E000u) g.flag |= 0x80000000u;
        break;
    }
}

struct Init {
    Init() { init_unr(); }
} init;

}  // namespace

namespace psx {

// aspect: output width/height (4/3 = original). hfov_deg: horizontal FOV of the world projection
// (H == world_h) at that aspect, 0 keeps the original vertical FOV (Hor+). base_tan: the game's
// original vertical half-FOV tangent for the world projection. Other projections (the weapon) are
// scaled by the same factor.
void gte_set_projection(double aspect, double hfov_deg, uint16_t world_h, double base_tan) {
    double squeeze = (4.0 / 3.0) / aspect;  // keeps 3D proportions when stretched to `aspect`
    double k = 1.0;
    if (hfov_deg > 0) {
        double tv = std::tan(hfov_deg * M_PI / 360.0) / aspect;  // vertical half-FOV tangent wanted
        k = base_tan / tv;
    }
    // The FOV applies to every projection, the weapon included: its muzzle flash and tracers are
    // world objects, so a weapon left at the original framing would detach from them.
    projection.world_h = world_h;
    projection.world = {k * squeeze, k};
    projection.other = {k * squeeze, k};
    projection.active = squeeze != 1.0 || k != 1.0;
    log("gte: projection aspect %.3f, world FOV %.1f deg horizontal", aspect,
        2.0 * std::atan(base_tan / k * aspect) * 180.0 / M_PI);
}

void gte_trace_frame() {
    if (!trace_mat) return;
    std::sort(mat_uses.begin(), mat_uses.end(), [](const MatUse &a, const MatUse &b) { return a.count > b.count; });
    for (size_t i = 0; i < mat_uses.size() && i < 40; i++) {
        const auto &m = mat_uses[i];
        log("gtemat %d H=%u pc=%08X n=%d rt=[%d %d %d; %d %d %d; %d %d %d] tr=(%d %d %d)", int(i), m.h, m.pc, m.count,
            m.rt[0][0], m.rt[0][1], m.rt[0][2], m.rt[1][0], m.rt[1][1], m.rt[1][2], m.rt[2][0], m.rt[2][1], m.rt[2][2],
            m.tr[0], m.tr[1], m.tr[2]);
    }
    log("gtemat total %zu", mat_uses.size());
    mat_uses.clear();
}

}  // namespace psx

extern "C" {

uint32_t gte_mfc2(Cpu *, int reg) { return read_data(reg); }
uint32_t gte_cfc2(Cpu *, int reg) { return read_ctrl(reg); }
void gte_mtc2(Cpu *, int reg, uint32_t v) { write_data(reg, v); }
void gte_ctc2(Cpu *, int reg, uint32_t v) { write_ctrl(reg, v); }
void gte_cmd(Cpu *, uint32_t op, uint32_t pc) {
    current_pc = pc;
    cmd(op);
}

}  // extern "C"
