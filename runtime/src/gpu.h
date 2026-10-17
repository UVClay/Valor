// GPU state shared between the command interface, rasterizer and display.
#pragma once
#include <cstdint>
#include <vector>

namespace psx::hw {

struct GpuState {
    std::vector<uint16_t> vram;  // 1024x512, 15-bit + mask bit
    uint32_t stat = 0x14802000;
    uint32_t read_latch = 0;
    uint32_t texpage = 0, tex_window = 0;
    int area_x1 = 0, area_y1 = 0, area_x2 = 0, area_y2 = 0;
    int offset_x = 0, offset_y = 0;
    bool mask_set = false, mask_check = false;
    bool display_disabled = true;
    int display_x = 0, display_y = 0;
    uint32_t hrange = 0, vrange = 0, display_mode = 0;
    uint32_t odd_field = 0;
    uint64_t frames = 0;
    uint64_t primitives = 0;
};

extern GpuState gpu;

// Rasterizer (renderer.cpp): draw one polygon/line/rectangle command.
void gpu_draw(const std::vector<uint32_t> &cmd);
// Display (display.cpp): show the current frame.
void gpu_present();

}  // namespace psx::hw
