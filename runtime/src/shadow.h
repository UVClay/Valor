// Decoupled rendering (see shadow.cpp).
#pragma once

namespace psx {

// Draw the latest tick's frame again by running the game's drawing pass on a copy of the machine.
// False when there's nothing to draw yet. x, y: the VRAM position of the frame it drew.
bool shadow_render(double vblank_time, int &x, int &y);
// A shadow pass is running (devices and interrupts must not be touched).
bool shadow_active();
// Shadow passes are drawing the frames shown (the game reached its drawing pass lately).
bool shadow_fresh();

}  // namespace psx
