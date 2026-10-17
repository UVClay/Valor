// Memory card in slot 1 (see memcard.cpp).
#pragma once
#include <cstdint>

namespace psx::hw {

void memcard_init();
bool memcard_present();
uint8_t memcard_exchange(int step, uint8_t tx, bool &ack);

}  // namespace psx::hw
