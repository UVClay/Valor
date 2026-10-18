// Mouse and keyboard controls (see kbm.cpp).
#pragma once
#include <cstdint>
#include <string>

union SDL_Event;
struct SDL_Window;

namespace psx::hw {

// The bindable controls.
enum KbmControl {
    KC_FORWARD, KC_BACK, KC_LEFT, KC_RIGHT, KC_FIRE, KC_AIM, KC_RELOAD, KC_USE, KC_JUMP, KC_CROUCH,
    KC_WEAPON, KC_CENTER, KC_PAUSE, KC_COUNT
};
const char *kbm_control_label(int control);  // for the settings menu
const char *kbm_control_key(int control);    // settings.ini key ("bind_fire")
// A binding: SDL scancode, or a mouse button / wheel direction (KBM_MOUSE + button, KBM_WHEEL_*).
constexpr int KBM_MOUSE = 1000, KBM_WHEEL_UP = 2000, KBM_WHEEL_DOWN = 2001;
extern int kbm_bindings[KC_COUNT];
std::string kbm_input_name(int input);
int kbm_parse_input(const std::string &name);  // -1: unknown

// Window input (keys, mouse buttons, motion, wheel). True when it was used.
bool kbm_event(const SDL_Event &e);
// Once per presented frame: capture the mouse while playing.
void kbm_update(SDL_Window *window);
// Buttons held for the emulated pad (input.h bits, active high).
uint16_t kbm_buttons();
// Settings menu: the next key or mouse button pressed rebinds `control`.
void kbm_begin_rebind(int control);
void kbm_reset_bindings();
int kbm_rebinding();  // the control waiting for a key, or -1

}  // namespace psx::hw
