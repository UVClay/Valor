// Controller state as the emulated pad reports it.
#pragma once
#include <cstdint>

namespace psx::hw {

// PlayStation button bits (active low in the pad protocol).
enum PadButton : uint16_t {
    BTN_SELECT = 1 << 0, BTN_L3 = 1 << 1, BTN_R3 = 1 << 2, BTN_START = 1 << 3,
    BTN_UP = 1 << 4, BTN_RIGHT = 1 << 5, BTN_DOWN = 1 << 6, BTN_LEFT = 1 << 7,
    BTN_L2 = 1 << 8, BTN_R2 = 1 << 9, BTN_L1 = 1 << 10, BTN_R1 = 1 << 11,
    BTN_TRIANGLE = 1 << 12, BTN_CIRCLE = 1 << 13, BTN_CROSS = 1 << 14, BTN_SQUARE = 1 << 15,
};

struct PadSticks {
    uint8_t lx, ly, rx, ry;  // 0x00..0xFF, 0x80 centered
};

bool pad_connected(int port);
// Pressed buttons as an active-low mask (0xFFFF = nothing pressed).
uint16_t pad_buttons(int port);
PadSticks pad_sticks(int port);
// Motor speeds requested by the game: small motor on/off, large motor 0..255.
void pad_rumble(int port, uint8_t small, uint8_t large);

}  // namespace psx::hw
