// Input recording and replay (--record DIR, --replay DIR). A recording keeps what the game read
// from the controls (pad, keyboard movement, mouse look) per vblank, every presented frame's moment
// and the mouse look it showed early, plus the settings and memory card it started with. Replaying
// it runs the same game and draws the same frames, so a play session's glitches can be
// reproduced and captured frame by frame.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "input.h"

namespace psx {

// main.cpp: before the hardware starts. Recording copies the settings in effect and the memory
// card; replaying loads them (and the recorded --poke-on-call list) into config.
void tape_record(const std::filesystem::path &dir);
// Debug mode: record this session to a new folder in recordings/ (named when it closes).
void tape_session_start();
void tape_replay(const std::filesystem::path &dir);
bool tape_recording();
bool tape_replaying();
// Replaying a recording made before drawing copies were kept off the devices (tape version 1,
// before 2026-10-20): its copies, as then, use every device and run while the tick is at most 4
// vblanks old. The drawing pass sets the sound voices' volumes for its camera, and the game reads
// them back, so a copy turned by mouse look changed the game: the replay must do the same (with
// OpenGL: a replay without a display draws no copies).
bool tape_copies_touch_devices();

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
// F9: mark this moment in the recording (a replay announces it).
void tape_mark();
int tape_marks();  // marks made so far
// On exit: finish the recording. A debug session is renamed after the first mission played
// (deleted if none was, unless it crashed); a crash also keeps the RAM. Returns the folder
// ("" if none is kept).
std::string tape_close(bool crashed);

}  // namespace psx
