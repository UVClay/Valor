// Devices not implemented yet: rasterizer, display, input.
#include "gpu.h"
#include "hw.h"
#include "input.h"

namespace psx::hw {

void gpu_draw(const std::vector<uint32_t> &) { gpu.primitives++; }
void gpu_present() {}

bool pad_connected(int port) { return port == 0; }
uint16_t pad_buttons(int) { return 0xFFFF; }

}  // namespace psx::hw
