// Integer arithmetic as the PS1 game does it: 16.16 positions, 4096-per-turn angles, and the
// wrap-around of 16- and 32-bit registers. Everything in the game state is an integer.
#pragma once

#include <cstdint>

namespace valor {

constexpr int32_t kOne = 1 << 16;  // 1.0 in 16.16
constexpr int16_t kNoFloor = -0x7FFF;

// Two's-complement truncation (defined in C++20).
inline int32_t s32(int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }
inline int16_t s16(int64_t v) { return static_cast<int16_t>(static_cast<uint16_t>(v)); }
inline int64_t iabs(int64_t v) { return v < 0 ? -v : v; }

// PsyQ's rcossin_tbl (exe 0x8003040C): round(4096 sin), 4096 per turn.
int32_t sin4096(int32_t a);  // a & 0xFFF
int32_t cos4096(int32_t a);

struct SinCos {
    int32_t s, c;
};

// RotMatrixYXZ's lookup of a halfword angle (negative angles mirror the sine).
SinCos sincos_angle(int32_t a);

// GTE NCLIP of three (x, y) points, as the 32-bit MAC0.
inline int32_t nclip(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy) {
    int64_t v = int64_t(ax) * by + int64_t(bx) * cy + int64_t(cx) * ay - int64_t(ax) * cy -
                int64_t(bx) * ay - int64_t(cx) * by;
    return s32(v);
}

}  // namespace valor
