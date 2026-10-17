// Controllers in ports 1 and 2 (see pad.cpp).
#pragma once
#include <cstdint>

namespace psx::hw {

uint8_t pad_exchange(int port, int step, uint8_t tx, bool &ack);
void pad_toggle_analog();

}  // namespace psx::hw
