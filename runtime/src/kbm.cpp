// Mouse and keyboard controls. In a mission, keys are bound to the game's own actions (fire,
// reload, ...) and pressed as whatever pad button the chosen controller configuration assigns
// them; movement and mouse look go straight into the game's control roles (game.cpp,
// hook_role_axis), so they work with any configuration and keep the game's own turning code
// (weapon sway, aim mode's reticle). Elsewhere (menus, the pause screen) the keyboard drives the
// pad: arrows/WASD, Enter/Space accept, Escape/Backspace back.
#include "kbm.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game.h"
#include "gpu.h"
#include "hw.h"
#include "input.h"
#include "overlay.h"

namespace psx::hw {

namespace {

struct ControlInfo { const char *label, *key; };
const ControlInfo controls[KC_COUNT] = {
    {"Forward", "bind_forward"},
    {"Back", "bind_back"},
    {"Strafe left", "bind_left"},
    {"Strafe right", "bind_right"},
    {"Fire", "bind_fire"},
    {"Aim", "bind_aim"},
    {"Reload", "bind_reload"},
    {"Use", "bind_use"},
    {"Jump", "bind_jump"},
    {"Crouch", "bind_crouch"},
    {"Next weapon", "bind_weapon"},
    {"Center view", "bind_look"},
    {"Pause", "bind_pause"},
};
// The game action each control presses (-1: movement or pause).
const int control_action[KC_COUNT] = {-1, -1, -1, -1, ACT_FIRE, ACT_AIM, ACT_RELOAD, ACT_USE, ACT_JUMP,
                                      ACT_CROUCH, ACT_WEAPON, ACT_CENTER, -1};

bool scancode_down[SDL_SCANCODE_COUNT];
bool mouse_down[8];
// A wheel binding taps its control: held for a few vblanks (the game reads presses per 30 Hz
// tick), then released as long before the next notch.
int wheel_taps[2];
uint64_t tap_until, tap_gap_until;
int tap_control = -1;
double mouse_dx, mouse_dy;  // counts not yet taken by the game
int rebind_control = -1;
bool captured;

// Gameplay: a mission without the pause screen or the settings menu up.
bool playing() { return game_in_level() && !game_paused() && !menu_is_open(); }

bool input_down(int input) {
    if (input >= 0 && input < SDL_SCANCODE_COUNT) return scancode_down[input];
    if (input > KBM_MOUSE && input < KBM_MOUSE + 8) return mouse_down[input - KBM_MOUSE];
    return false;
}

bool scripted_down(int control) {
    for (const auto &k : config.kbm_presses)
        if (k.control == control && gpu.frames >= k.frame && gpu.frames < k.frame + k.duration) return true;
    return false;
}

bool control_down(int control) {
    if (tap_control == control && gpu.frames < tap_until) return true;
    return input_down(kbm_bindings[control]) || scripted_down(control);
}

void update_taps() {
    if (gpu.frames < tap_gap_until) return;
    tap_control = -1;
    for (int w = 0; w < 2; w++) {
        if (!wheel_taps[w]) continue;
        wheel_taps[w]--;
        for (int c = 0; c < KC_COUNT; c++)
            if (kbm_bindings[c] == KBM_WHEEL_UP + w) tap_control = c;
        tap_until = gpu.frames + 4;
        tap_gap_until = gpu.frames + 8;
        return;
    }
}

}  // namespace

const int default_bindings[KC_COUNT] = {
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, KBM_MOUSE + SDL_BUTTON_LEFT,
    KBM_MOUSE + SDL_BUTTON_RIGHT, SDL_SCANCODE_R, SDL_SCANCODE_E, SDL_SCANCODE_SPACE, SDL_SCANCODE_C,
    KBM_WHEEL_DOWN, SDL_SCANCODE_V, SDL_SCANCODE_ESCAPE,
};
int kbm_bindings[KC_COUNT] = {
    SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, KBM_MOUSE + SDL_BUTTON_LEFT,
    KBM_MOUSE + SDL_BUTTON_RIGHT, SDL_SCANCODE_R, SDL_SCANCODE_E, SDL_SCANCODE_SPACE, SDL_SCANCODE_C,
    KBM_WHEEL_DOWN, SDL_SCANCODE_V, SDL_SCANCODE_ESCAPE,
};

void kbm_reset_bindings() { std::copy(std::begin(default_bindings), std::end(default_bindings), kbm_bindings); }

const char *kbm_control_label(int control) { return controls[control].label; }
const char *kbm_control_key(int control) { return controls[control].key; }

std::string kbm_input_name(int input) {
    if (input == KBM_WHEEL_UP) return "Wheel Up";
    if (input == KBM_WHEEL_DOWN) return "Wheel Down";
    if (input > KBM_MOUSE && input < KBM_MOUSE + 8) return "Mouse " + std::to_string(input - KBM_MOUSE);
    if (input > 0 && input < SDL_SCANCODE_COUNT) {
        const char *n = SDL_GetScancodeName(SDL_Scancode(input));
        if (n && *n) return n;
    }
    return "None";
}

int kbm_parse_input(const std::string &name) {
    if (name == "None") return 0;
    if (name == "Wheel Up") return KBM_WHEEL_UP;
    if (name == "Wheel Down") return KBM_WHEEL_DOWN;
    if (name.rfind("Mouse ", 0) == 0) {
        int b = atoi(name.c_str() + 6);
        return b >= 1 && b < 8 ? KBM_MOUSE + b : -1;
    }
    SDL_Scancode s = SDL_GetScancodeFromName(name.c_str());
    return s == SDL_SCANCODE_UNKNOWN ? -1 : int(s);
}

void kbm_begin_rebind(int control) { rebind_control = control; }
int kbm_rebinding() { return rebind_control; }

bool kbm_event(const SDL_Event &e) {
    int input = -1;
    bool down = false;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e.key.scancode >= SDL_SCANCODE_COUNT) return false;
        input = e.key.scancode;
        down = e.type == SDL_EVENT_KEY_DOWN;
        if (rebind_control < 0) scancode_down[input] = down;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button >= 8) return false;
        input = KBM_MOUSE + e.button.button;
        down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (rebind_control < 0) mouse_down[e.button.button] = down && captured;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (e.wheel.y == 0) return false;
        input = e.wheel.y > 0 ? KBM_WHEEL_UP : KBM_WHEEL_DOWN;
        down = true;
        if (rebind_control < 0 && playing()) wheel_taps[input - KBM_WHEEL_UP] = std::min(wheel_taps[input - KBM_WHEEL_UP] + 1, 3);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (!captured) return false;
        mouse_dx += e.motion.xrel;
        mouse_dy += e.motion.yrel;
        return true;
    default:
        return false;
    }
    if (rebind_control >= 0 && down) {
        // Escape cancels; the binding's own key otherwise.
        if (input != SDL_SCANCODE_ESCAPE || rebind_control == KC_PAUSE) kbm_bindings[rebind_control] = input;
        rebind_control = -1;
        return true;
    }
    return captured;
}

void kbm_update(SDL_Window *window) {
    bool want = config.mouse_look && playing() && !config.hidden &&
                (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
    if (want != captured) {
        SDL_SetWindowRelativeMouseMode(window, want);
        captured = want;
        mouse_dx = mouse_dy = 0;
        if (!want) std::fill(std::begin(mouse_down), std::end(mouse_down), false);
    }
}

uint16_t kbm_buttons() {
    update_taps();
    // Scripted mouse motion arrives like a real mouse's.
    for (auto &m : config.mouse_moves)
        if (!m.taken[0] && gpu.frames >= m.frame) {
            mouse_dx += m.dx;
            mouse_dy += m.dy;
            m.taken[0] = true;
        }
    uint16_t pressed = 0;
    if (playing()) {
        for (int c = 0; c < KC_COUNT; c++) {
            if (!control_down(c)) continue;
            if (c == KC_PAUSE) pressed |= BTN_START;
            else if (control_action[c] >= 0) pressed |= game_action_buttons(GameAction(control_action[c]));
        }
        return pressed;
    }
    // Menus: the keyboard as a pad.
    auto key = [](SDL_Scancode s) { return scancode_down[s]; };
    if (key(SDL_SCANCODE_UP) || control_down(KC_FORWARD)) pressed |= BTN_UP;
    if (key(SDL_SCANCODE_DOWN) || control_down(KC_BACK)) pressed |= BTN_DOWN;
    if (key(SDL_SCANCODE_LEFT) || control_down(KC_LEFT)) pressed |= BTN_LEFT;
    if (key(SDL_SCANCODE_RIGHT) || control_down(KC_RIGHT)) pressed |= BTN_RIGHT;
    if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_KP_ENTER) || key(SDL_SCANCODE_SPACE)) pressed |= BTN_CROSS;
    if (key(SDL_SCANCODE_BACKSPACE)) pressed |= BTN_TRIANGLE;
    if (control_down(KC_PAUSE)) pressed |= game_in_level() ? BTN_START : BTN_TRIANGLE;
    return pressed;
}

}  // namespace psx::hw

namespace psx {

using namespace hw;

void kbm_movement(double &forward, double &strafe) {
    forward = strafe = 0;
    if (!playing()) return;
    forward = double(control_down(KC_FORWARD)) - double(control_down(KC_BACK));
    strafe = double(control_down(KC_RIGHT)) - double(control_down(KC_LEFT));
}

static double tick_yaw, tick_pitch;

// Mouse counts to degrees: Quake's 0.022 degrees per count at sensitivity 1.
static double turn_degrees(double counts) { return counts * 0.022 * config.mouse_sensitivity; }
static double pitch_degrees(double counts) {
    return -counts * 0.022 * config.mouse_sensitivity * (config.invert_mouse ? -1 : 1);
}

static double gyro_yaw, gyro_pitch;  // degrees not taken yet

void kbm_gyro(const float rate[3], uint64_t timestamp_ns) {
    static uint64_t last;
    double dt = last && timestamp_ns > last ? std::min(double(timestamp_ns - last) * 1e-9, 0.05) : 0;
    last = timestamp_ns;
    bool on = config.gyro == 1 || (config.gyro == 2 && game_aiming());
    if (!on || !playing() || dt == 0) return;
    // Counter-clockwise positive: about y (up) is turning left, about x (right) tilting up.
    // A small deadzone keeps a resting controller's drift from turning the view.
    constexpr double DEADZONE = 0.01;  // radians per second
    auto rate_deg = [](float r) { return std::abs(r) < DEADZONE ? 0.0 : double(r) * 180 / M_PI; };
    gyro_yaw -= rate_deg(rate[1]) * dt * config.gyro_sensitivity;
    gyro_pitch += rate_deg(rate[0]) * dt * config.gyro_sensitivity * (config.invert_mouse ? -1 : 1);
}

double kbm_take_turn() {
    double counts = mouse_dx;
    mouse_dx = 0;
    double d = playing() ? turn_degrees(counts) + gyro_yaw : 0;
    gyro_yaw = 0;
    tick_yaw += d;
    return d;
}

double kbm_take_pitch() {
    double counts = mouse_dy;
    mouse_dy = 0;
    double d = playing() ? pitch_degrees(counts) + gyro_pitch : 0;
    gyro_pitch = 0;
    tick_pitch += d;
    return d;
}

void kbm_tick_look(double &yaw, double &pitch) {
    yaw = tick_yaw;
    pitch = tick_pitch;
    tick_yaw = tick_pitch = 0;
}

void kbm_pending_look(double &yaw, double &pitch) {
    bool on = playing();
    yaw = on ? (config.mouse_look ? turn_degrees(mouse_dx) : 0) + gyro_yaw : 0;
    pitch = on ? (config.mouse_look ? pitch_degrees(mouse_dy) : 0) + gyro_pitch : 0;
}

}  // namespace psx
