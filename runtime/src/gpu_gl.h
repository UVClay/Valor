// OpenGL renderer setup and presentation (see gpu_gl.cpp); drawing hooks are in gpu.h.
#pragma once
#include <cstdint>
#include <vector>

namespace psx::hw {

// Needs a current OpenGL 3.3 context.
bool gl_renderer_init(int scale);
// Change the scale at run time (1 switches to the software renderer's output).
void gl_renderer_set_scale(int scale);
int gl_renderer_scale();
// Read back a VRAM rectangle (native coordinates) at full resolution, ARGB, top row first.
std::vector<uint32_t> gl_renderer_read(int x, int y, int w, int h);
// Frame interpolation: redraw the displayed frame for this point in emulated time (vblanks).
void gl_renderer_interpolate(double vblank_time);
// Decoupled rendering (shadow.cpp): before and after a shadow pass draws.
void gl_renderer_shadow(bool begin);
// Blit the displayed VRAM area (native coordinates) into the window's framebuffer rectangle.
void gl_renderer_present(int dst_x, int dst_y, int dst_w, int dst_h, int src_x, int src_y, int src_w, int src_h);

}  // namespace psx::hw
