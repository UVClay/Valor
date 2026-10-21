// The closed-loop engine's arithmetic and memory (tools/sim/engine.py): the game state kept in the
// original's memory layout, so the rules written against that layout in docs/re and tools/sim
// apply as they are. Values are 64-bit integers as Python's are, truncated where the Python (and
// the original) truncates: S32, S16, S8.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace valor::sim {

using I = int64_t;
using U = uint32_t;

inline I S32(I v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }
inline I S16(I v) { return static_cast<int16_t>(static_cast<uint16_t>(v)); }
inline I S8(I v) { return static_cast<int8_t>(static_cast<uint8_t>(v)); }
inline I U32(I v) { return static_cast<uint32_t>(v); }
inline I iabs(I v) { return v < 0 ? -v : v; }
// C division and remainder (toward zero), and Python's floor division and modulo.
inline I cdiv(I a, I b) {
    I q = iabs(a) / iabs(b);
    return (a >= 0) == (b >= 0) ? q : -q;
}
inline I cmod(I a, I b) { return a - b * cdiv(a, b); }
inline I fdiv(I a, I b) {
    I q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}
inline I pmod(I a, I b) { return a - b * fdiv(a, b); }
inline I imin(I a, I b) { return a < b ? a : b; }
inline I imax(I a, I b) { return a > b ? a : b; }

// RAM in the original's layout: addresses 0x80000000.. (8 MB: the engine's own allocations live
// above the console's 2 MB).
struct Mem {
    static constexpr U kSize = 0x800000;
    static constexpr U kMask = kSize - 1;
    std::vector<uint8_t> d = std::vector<uint8_t>(kSize);

    I u8(I a) const { return d[a & kMask]; }
    I s8(I a) const { return int8_t(d[a & kMask]); }
    I u16(I a) const {
        U o = a & kMask;
        return uint16_t(d[o] | d[o + 1] << 8);
    }
    I s16(I a) const { return int16_t(u16(a)); }
    I u32(I a) const {
        U o = a & kMask;
        return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
    }
    I s32(I a) const { return int32_t(u32(a)); }
    void w8(I a, I v) { d[a & kMask] = uint8_t(v); }
    void w16(I a, I v) {
        U o = a & kMask;
        d[o] = uint8_t(v), d[o + 1] = uint8_t(v >> 8);
    }
    void w32(I a, I v) {
        U o = a & kMask;
        d[o] = uint8_t(v), d[o + 1] = uint8_t(v >> 8), d[o + 2] = uint8_t(v >> 16), d[o + 3] = uint8_t(v >> 24);
    }
    void copy(I dst, I src, I n) {
        if (n > 0) std::memmove(&d[dst & kMask], &d[src & kMask], size_t(n));
    }
    void zero(I a, I n) {
        if (n > 0) std::memset(&d[a & kMask], 0, size_t(n));
    }
    const uint8_t* ptr(I a) const { return &d[a & kMask]; }
    // The entity list from g_entities (0x8009D64C), at most 1024.
    std::vector<U> entities() const;
};

// Globals (docs/re; tools/sim/engine.py).
constexpr U G_TICK = 0x8009A5DC;
constexpr U G_PLAYER = 0x8009D638;  // the camera entity
constexpr U G_WORLD = 0x8009D648;
constexpr U G_ENTITIES = 0x8009D64C;
constexpr U G_LAST_EXPLOSION = 0x8009D650;
constexpr U G_SOLDIER = 0x8009D654;
constexpr U G_HANDLES = 0x8009D678;
constexpr U G_SERIALS = 0x8009DA78;
constexpr U G_REMOVED = 0x8009DE78;
constexpr U G_SERIAL = 0x8009D670;
constexpr U G_SLOTS_USED = 0x8009D674;
constexpr U G_SOLDIER_COUNT = 0x80097FE8;
constexpr U G_SOLDIER_CAP = 0x80097FEC;
constexpr U G_VBL_TIMER = 0x8009A5C0;
constexpr U G_CHUNK_LOADING = 0x800AA184;
constexpr U CHUNK_SLOTS = 0x80090DE8;
constexpr U PAD_NOW = 0x80093B7C, PAD_PREV = 0x80093B84;
constexpr U ACTIONS = 0x800B7790;
constexpr U HEAP = 0x80200000;  // the engine's own allocations
constexpr U G_ALARM = 0x80097FE0;
constexpr U G_FRAME_COUNT = 0x8009A5D8;
constexpr U G_MODELS = 0x8009EC58, G_NMODELS = 0x8009EC54;
inline U char_slot(int i) { return 0x800B7B08 + i * 0x2814; }

constexpr U DEAD = 0x20000000, REMOVED = 0x80000;
constexpr int SOLDIER = 0x1402, DOG = 0x1405;

}  // namespace valor::sim
