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
extern uint32_t gpu_packet_addr;  // RAM address of the ordering-table packet being drawn (tracing)

// A decoded primitive vertex: position (drawing offset applied), color, texture coordinate.
struct Vertex {
    int x, y;
    int r, g, b;
    int u, v;
};

// Per-primitive drawing state.
struct DrawState {
    bool textured, raw, semi, shaded, dither;
    int semi_mode;
    int tex_depth;  // 0 = 4-bit, 1 = 8-bit, 2 = 15-bit
    int tex_x, tex_y, clut_x, clut_y;
};

// GPU-accelerated renderer (gpu_gl.cpp). It replays what the software rasterizer draws into a
// higher-resolution framebuffer for display; the software rasterizer keeps PS1 VRAM exact.
namespace hwgl {
bool active();
void triangle(const DrawState &s, const Vertex &a, const Vertex &b, const Vertex &c);
void line(const DrawState &s, const Vertex &a, const Vertex &b);
void rect(const DrawState &s, const Vertex &base, int w, int h);  // base.u/v = top-left texel
void fill(int x, int y, int w, int h, uint32_t color);
void copy(int sx, int sy, int dx, int dy, int w, int h);
void upload(int x, int y, int w, int h);        // CPU wrote this VRAM rectangle
void vram_changed(int x, int y, int w, int h);  // the software rasterizer drew here
}  // namespace hwgl

// Rasterizer (renderer.cpp): draw one polygon/line/rectangle command.
void gpu_draw(const std::vector<uint32_t> &cmd);
// Display (display.cpp): show the current frame.
void gpu_present();

}  // namespace psx::hw
