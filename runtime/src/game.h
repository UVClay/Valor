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

// The game's actions (its per-port action button table, 0x800B7790).
enum GameAction {
    ACT_FIRE, ACT_JUMP, ACT_WEAPON, ACT_CROUCH, ACT_RELOAD, ACT_USE, ACT_PAUSE, ACT_LOOK, ACT_AIM, ACT_COUNT
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
// display.cpp: a gamepad gyroscope reading (radians per second about x, y, z; SDL's axes).
void kbm_gyro(const float rate[3], uint64_t timestamp_ns);
void kbm_pending_look(double &yaw, double &pitch);

}  // namespace psx
