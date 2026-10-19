// Input recording and replay (--record DIR, --replay DIR). A recording keeps what the game read
// from the controls (pad, keyboard movement, mouse look) per vblank, every presented frame's moment
// and the mouse look it showed early, plus the settings and memory card it started with. Replaying
// it runs the same game and draws the same frames, so a play session's glitches can be
// reproduced and captured frame by frame.
#pragma once
#include <cstdint>
#include <filesystem>

#include "input.h"

namespace psx {

// main.cpp: before the hardware starts. Recording copies the settings in effect and the memory
// card; replaying loads them (and the recorded --poke-on-call list) into config.
void tape_record(const std::filesystem::path &dir);
void tape_replay(const std::filesystem::path &dir);
bool tape_recording();
bool tape_replaying();

// What the game reads: recorded (live value passed through) or replayed (live value ignored).
uint16_t tape_pad_buttons(uint16_t live);
hw::PadSticks tape_pad_sticks(hw::PadSticks live);
void tape_movement(double &forward, double &strafe);
double tape_turn(double live);
double tape_pitch(double live);
// The mouse look an interpolated frame shows ahead of the game.
void tape_pending_look(double &yaw, double &pitch);
// The analog button (F7) was pressed.
void tape_analog_toggled();

// display.cpp, once per vblank after it is emulated: checks the replay still runs the same game.
// Replaying, `present` is called for each frame presented after this vblank (its moment, in
// vblanks); false when the recording has ended.
void tape_vblank();
bool tape_replay_presents(void (*present)(double vblank_time));
// Recording, a frame was presented at this moment (in vblanks), in a window this size.
void tape_presented(double vblank_time);
void tape_window(int w, int h);
// Finish the file (on exit).
void tape_close();

}  // namespace psx
