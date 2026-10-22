#include "adpcm.h"

#include <algorithm>

namespace valor_present {

Adpcm adpcm_decode(const uint8_t* data, size_t size, bool whole_file) {
    static const int kF0[5] = {0, 60, 115, 98, 122}, kF1[5] = {0, 0, -52, -55, -60};
    Adpcm out;
    int s1 = 0, s2 = 0;
    for (size_t o = 0; o + 16 <= size; o += 16) {
        const uint8_t* b = data + o;
        int shift = b[0] & 0xF, filter = std::min(b[0] >> 4, 4), flags = b[1];
        if (shift > 12) shift = 9;  // reserved values decode as 9 on the hardware
        if (flags & 4 && out.loop_start < 0) out.loop_start = int(out.pcm.size());
        for (int i = 0; i < 28; i++) {
            int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
            int s = int16_t(uint16_t(nib << 12)) >> shift;
            s += (s1 * kF0[filter] + s2 * kF1[filter] + 32) >> 6;
            s = std::clamp(s, -32768, 32767);
            out.pcm.push_back(int16_t(s));
            s2 = s1, s1 = s;
        }
        if (flags & 1) {
            out.loops = flags & 2;
            if (!whole_file) break;
        }
    }
    return out;
}

}  // namespace valor_present
