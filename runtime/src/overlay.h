// Debug overlay (see overlay.cpp).
#pragma once
#include <cstdint>

union SDL_Event;
struct SDL_Window;

namespace psx::hw {

// Counted during a frame, shown by the extended overlay.
struct OverlayCounters {
    uint32_t primitives, draw_calls, vram_uploads;
};
extern OverlayCounters overlay_counters;

void overlay_init(SDL_Window *window, void *gl_context);
void overlay_event(const SDL_Event &e);
void overlay_cycle();          // F3: off -> basic -> extended
void overlay_frame_begin();    // emulation of a new frame starts (after pacing)
void overlay_frame_emulated(); // ... and is done (the vblank is reached)
void overlay_note_flip();      // the game changed the displayed buffer
void overlay_set_view(int x, int y, int w, int h, bool world);  // where the game image is shown
void overlay_render();         // draw into the window's framebuffer before swapping
bool menu_is_open();
void menu_toggle();            // F1: settings menu (the game pauses while it's open)

// display.cpp: apply changed resolution / fullscreen settings.
void display_apply_settings();
// display.cpp: frames per second presented with interpolation.
double present_rate();

}  // namespace psx::hw
