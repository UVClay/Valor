// Devices not implemented yet: MDEC, rasterizer, display, input.
#include "gpu.h"
#include "hw.h"
#include "input.h"

namespace psx::hw {

void mdec_init() {}
uint32_t mdec_read(uint32_t phys) { return phys == 0x1F801824 ? 0x80040000u : 0; }  // status: FIFO empty
void mdec_write(uint32_t, uint32_t) {}
void mdec_dma_in(uint32_t, uint32_t) {}
void mdec_dma_out(uint32_t, uint32_t) {}

void gpu_draw(const std::vector<uint32_t> &) { gpu.primitives++; }
void gpu_present() {}

bool pad_connected(int port) { return port == 0; }
uint16_t pad_buttons(int) { return 0xFFFF; }

}  // namespace psx::hw
