#include "valor/fixed.h"

namespace valor {

namespace {
const int16_t kSin[4096] = {
#include "sintab.inc"
};
}  // namespace

int32_t sin4096(int32_t a) { return kSin[a & 0xFFF]; }
int32_t cos4096(int32_t a) { return kSin[(a + 1024) & 0xFFF]; }

SinCos sincos_angle(int32_t a) {
    int16_t h = s16(a);
    if (h >= 0) return {sin4096(h), cos4096(h)};
    return {-sin4096(-h), cos4096(-h)};
}

}  // namespace valor
