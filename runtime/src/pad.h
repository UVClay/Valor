// Controller in port 1 (see pad.cpp).
#pragma once
#include <cstdint>

namespace psx::hw {

uint8_t pad_exchange(int step, uint8_t tx, bool &ack);
void pad_toggle_analog();

}  // namespace psx::hw
