// What the runtime knows about Medal of Honor's state (see docs/RECON.md), and native hooks
// into its code (games/moh/recomp.toml [hooks]).
#pragma once
#include <cstdint>

namespace psx {

// LEVEL.BIN (a mission) is the resident overlay.
bool game_in_level();
// The pause screen is up.
bool game_paused();
// Aim mode (R2 by default): the game draws its own reticle.
bool game_aiming();
// The screen line (0..239) the game aims through: shots go where a crosshair here points. It rests
// at 131, below the view's centre (120).
int game_crosshair_line();

// The game's actions (its per-port action button table, 0x800B7790).
enum GameAction {
    ACT_FIRE, ACT_JUMP, ACT_WEAPON, ACT_CROUCH, ACT_RELOAD, ACT_USE, ACT_PAUSE, ACT_CENTER, ACT_AIM, ACT_COUNT
};
// The pad buttons (input.h bits) assigned to an action in the current controller configuration.
uint16_t game_action_buttons(GameAction a);

// kbm.cpp: keyboard movement (-1..1, forward and right positive) and mouse look since the last
// call (degrees, up and right positive; taking it resets it).
void kbm_movement(double &forward, double &strafe);
double kbm_take_turn();
double kbm_take_pitch();
// Mouse look taken by the game since the last call (degrees), for a new tick (interp.cpp), and
// mouse motion it hasn't taken yet.
void kbm_tick_look(double &yaw, double &pitch);
void kbm_pending_look(double &yaw, double &pitch);
// All the mouse look the game has taken so far (degrees; shadow.cpp).
void kbm_look_taken(double &yaw, double &pitch);
// How far mouse look may turn the view ahead of what the game drew (degrees, from its recent
// speed), sideways and up or down.
void kbm_look_reach(double &yaw, double &pitch);
// How far beyond its view the game drew this tick (degrees; game.cpp): in-between frames turn
// the view at most that far from the tick's camera (interp.cpp).
void game_drawn_reach(double &yaw, double &pitch);
// display.cpp: a gamepad gyroscope reading (radians per second about x, y, z; SDL's axes).
void kbm_gyro(const float rate[3], uint64_t timestamp_ns);

// watch.cpp: VALOR_TRACE_INPUT logs a control role's value (src 0 buttons, 1 stick and mouse).
void trace_role(uint32_t port, uint32_t role, int src, int32_t value);

}  // namespace psx
