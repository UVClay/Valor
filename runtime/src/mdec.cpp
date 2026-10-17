// MDEC (motion decoder) at 0x1F801820: decodes run-length coded DCT macroblocks for FMV.
// Follows the decoding steps documented in psx-spx.
#include <algorithm>
#include <deque>
#include <vector>

#include "hw.h"

namespace psx::hw {

namespace {

const uint8_t ZIGZAG[64] = {
    0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42, 3,  8,  12, 17, 25, 30,
    41, 43, 9,  11, 18, 24, 31, 40, 44, 53, 10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38,
    46, 51, 55, 60, 21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};
uint8_t zagzig[64];  // inverse of ZIGZAG

uint8_t quant_y[64], quant_c[64];
int16_t scale[64];

uint32_t control;
uint32_t command;       // current command word
uint32_t words_left;    // parameter words still expected for `command`
std::vector<uint16_t> params;
std::deque<uint32_t> out;  // decoded output, in words

int depth() { return (command >> 27) & 3; }  // 0=4bit 1=8bit 2=24bit 3=15bit
bool out_signed() { return command & (1u << 26); }
bool out_bit15() { return command & (1u << 25); }

int signed10(uint16_t v) { return int(v << 22) >> 22; }

// Decode one 8x8 block. Returns false when the input runs out.
bool rl_decode_block(int16_t blk[64], size_t &src, const uint8_t qt[64]) {
    std::fill(blk, blk + 64, 0);
    uint16_t n;
    do {
        if (src >= params.size()) return false;
        n = params[src++];
    } while (n == 0xFE00);
    int k = 0;
    int q_scale = (n >> 10) & 0x3F;
    int val = signed10(n & 0x3FF) * qt[k];
    for (;;) {
        if (q_scale == 0) val = signed10(n & 0x3FF) * 2;
        val = std::clamp(val, -0x400, 0x3FF);
        if (q_scale > 0) blk[zagzig[k]] = int16_t(val);
        else blk[k] = int16_t(val);
        if (src >= params.size()) break;
        n = params[src++];
        k += ((n >> 10) & 0x3F) + 1;
        if (k > 63) break;
        val = (signed10(n & 0x3FF) * qt[k] * q_scale + 4) / 8;
    }
    // Two-pass IDCT with the scale table (entries are fixed point with 16 fractional bits).
    int32_t tmp[64];
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int64_t sum = 0;
            for (int z = 0; z < 8; z++) sum += int64_t(blk[y + z * 8]) * scale[x + z * 8];
            tmp[x + y * 8] = int32_t((sum + 0x8000) >> 16);
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int64_t sum = 0;
            for (int z = 0; z < 8; z++) sum += int64_t(tmp[y + z * 8]) * scale[x + z * 8];
            blk[x + y * 8] = int16_t(std::clamp<int64_t>((sum + 0x8000) >> 16, -128, 127));
        }
    return true;
}

void emit_bytes(const uint8_t *b, size_t n) {
    static uint32_t pending, count;
    for (size_t i = 0; i < n; i++) {
        pending |= uint32_t(b[i]) << (8 * count);
        if (++count == 4) {
            out.push_back(pending);
            pending = count = 0;
        }
    }
}

void color_macroblock(const int16_t cr[64], const int16_t cb[64], const int16_t *y[4]) {
    uint8_t rgb[16 * 16 * 3];
    uint16_t px15[16 * 16];
    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 16; xx++) {
            const int16_t *yb = y[(yy / 8) * 2 + xx / 8];
            int c = (xx / 2) + (yy / 2) * 8;
            double R = cr[c], B = cb[c];
            double G = -0.3437 * B - 0.7143 * R;
            R *= 1.402;
            B *= 1.772;
            int Y = yb[(xx % 8) + (yy % 8) * 8];
            int r = std::clamp(int(Y + R), -128, 127), g = std::clamp(int(Y + G), -128, 127),
                b = std::clamp(int(Y + B), -128, 127);
            if (!out_signed()) { r += 128; g += 128; b += 128; }
            uint8_t *p = rgb + 3 * (yy * 16 + xx);
            p[0] = uint8_t(r); p[1] = uint8_t(g); p[2] = uint8_t(b);
            px15[yy * 16 + xx] = uint16_t((p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10) |
                                          (out_bit15() ? 0x8000 : 0));
        }
    if (depth() == 2) emit_bytes(rgb, sizeof rgb);
    else emit_bytes(reinterpret_cast<uint8_t *>(px15), sizeof px15);
}

void decode() {
    size_t src = 0;
    int16_t blocks[6][64];
    if (depth() >= 2) {
        // Color: Cr, Cb, Y1..Y4 per 16x16 macroblock.
        while (src < params.size()) {
            bool ok = true;
            for (int i = 0; i < 6 && ok; i++) ok = rl_decode_block(blocks[i], src, i < 2 ? quant_c : quant_y);
            if (!ok) break;
            const int16_t *y[4] = {blocks[2], blocks[3], blocks[4], blocks[5]};
            color_macroblock(blocks[0], blocks[1], y);
        }
    } else {
        // Monochrome: one 8x8 Y block per "macroblock".
        while (rl_decode_block(blocks[0], src, quant_y)) {
            uint8_t px[64];
            for (int i = 0; i < 64; i++) px[i] = uint8_t(blocks[0][i] + (out_signed() ? 0 : 128));
            if (depth() == 1) {
                emit_bytes(px, 64);
            } else {
                uint8_t nib[32];
                for (int i = 0; i < 32; i++) nib[i] = uint8_t((px[2 * i] >> 4) | (px[2 * i + 1] & 0xF0));
                emit_bytes(nib, 32);
            }
        }
    }
}

void parameter_word(uint32_t w) {
    uint32_t cmd = command >> 29;
    params.push_back(uint16_t(w));
    params.push_back(uint16_t(w >> 16));
    if (--words_left) return;
    if (cmd == 1) {
        decode();
    } else if (cmd == 2) {
        // 64 luma bytes, then (if bit 0) 64 chroma bytes.
        for (int i = 0; i < 64; i++) quant_y[i] = uint8_t(params[i / 2] >> (8 * (i & 1)));
        if (command & 1)
            for (int i = 0; i < 64; i++) quant_c[i] = uint8_t(params[32 + i / 2] >> (8 * (i & 1)));
    } else if (cmd == 3) {
        for (int i = 0; i < 64; i++) scale[i] = int16_t(params[i]);
    }
    params.clear();
}

void write_command(uint32_t v) {
    if (words_left) return parameter_word(v);
    if (config.trace_cd) {
        static int n;
        if (n++ < 12) log("mdec: command %08X", v);
    }
    command = v;
    params.clear();
    switch (v >> 29) {
    case 1: words_left = v & 0xFFFF; break;
    case 2: words_left = (v & 1) ? 32 : 16; break;
    case 3: words_left = 32; break;
    default: words_left = 0; break;
    }
}

}  // namespace

void mdec_init() {
    for (int i = 0; i < 64; i++) zagzig[ZIGZAG[i]] = uint8_t(i);
    control = command = words_left = 0;
    params.clear();
    out.clear();
}

uint32_t mdec_read(uint32_t phys) {
    if (phys == 0x1F801820) {
        if (out.empty()) return 0;
        uint32_t v = out.front();
        out.pop_front();
        return v;
    }
    uint32_t s = 0;
    if (out.empty()) s |= 1u << 31;
    if (words_left || !out.empty()) s |= 1u << 29;              // busy
    if ((control & (1u << 30)) && words_left) s |= 1u << 28;   // data-in request
    if ((control & (1u << 29)) && !out.empty()) s |= 1u << 27; // data-out request
    s |= uint32_t(depth()) << 25 | (out_signed() ? 1u << 24 : 0) | (out_bit15() ? 1u << 23 : 0);
    s |= (words_left ? words_left - 1 : 0xFFFF) & 0xFFFF;
    return s;
}

void mdec_write(uint32_t phys, uint32_t v) {
    if (phys == 0x1F801820) return write_command(v);
    if (v & (1u << 31)) mdec_init();
    control = v;
}

void mdec_dma_in(uint32_t addr, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, psx_ram + ((addr + 4 * i) & 0x1FFFFC), 4);
        write_command(w);
    }
}

void mdec_dma_out(uint32_t addr, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w = 0;
        if (!out.empty()) { w = out.front(); out.pop_front(); }
        memcpy(psx_ram + ((addr + 4 * i) & 0x1FFFFC), &w, 4);
    }
}

}  // namespace psx::hw
