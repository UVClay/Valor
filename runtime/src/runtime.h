// Runtime-internal declarations shared between runtime source files.
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "psx/recomp.h"
#include "psx/runtime_modules.h"

namespace psx {

// Thrown to unwind out of recompiled code (fatal error or game exit).
struct Halt : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

struct Config {
    std::filesystem::path bios;  // openbios.bin
    std::filesystem::path disc;  // .cue or .bin
    bool trace_io = false;
    bool trace_calls = false;  // log indirect calls and exceptions
    bool trace_cd = false;     // log CD-ROM commands, responses and sectors
    bool trace_audio = false;  // log the audio queue once a second
    int64_t max_cycles = 0;  // stop after this many cycles (0 = run forever)
    bool headless = false;   // no window
    bool hidden = false;     // render with a hidden window (screenshots of the OpenGL renderer)
    bool test_run = false;   // headless or hidden: don't touch the player's memory card or settings
    int scale = 0;           // OpenGL renderer resolution scale (0 = fit the window, 1 = software)
    bool dither = true;      // PS1 dithering and 15-bit color
    bool smooth_scaling = true;  // filter the image when scaling it to the window
    bool fullscreen = false;
    int window_w = 960, window_h = 720;  // initial window size
    double aspect = 4.0 / 3.0;  // output aspect (0 = match the window)
    double fov = 0;             // world horizontal FOV in degrees (0 = original vertical FOV, Hor+)
    // MoH: the world is projected with GTE H = 400 on a 512x240 screen (vertical half-FOV
    // tangent 120/400); the weapon (H = 133) is scaled along with it.
    uint16_t world_h = 400;
    double world_base_tan = 120.0 / 400.0;
    // GTE commands (cop2 instruction addresses) that project HUD elements, which keep their size
    // under FOV/widescreen scaling. MoH: the compass disc, set up by LEVEL.BIN at 0x80060A08.
    std::vector<uint32_t> hud_projections = {0x80060A08};
    std::filesystem::path screenshots;  // save a PNG of the display every `screenshot_every` frames
    uint64_t screenshot_every = 60;
    uint64_t screenshot_from = 0;    // no screenshots before this frame
    bool screenshot_window = false;  // capture the window (with overlay) instead of the PS1 image
    int overlay = 0;                 // initial debug overlay level
    uint64_t menu_screenshot = 0;    // testing: capture the settings menu on this frame
    std::filesystem::path wav;  // also record audio output here
    int volume = 100;           // output volume, percent
    // Map each stick's round range onto the square one a DualShock reports (full deflection
    // reaches both axes' limits on diagonals). MoH shapes each axis separately with a steep
    // curve, so on a round-gated stick angling it off-axis loses most of the speed.
    bool square_sticks = true;
    // Frame interpolation (interp.cpp): the game logic runs at 30 Hz; show frames in between
    // at `frame_rate` per second (0 = the display's refresh rate).
    bool interpolate = true;
    int frame_rate = 0;
    // Keep the 2D HUD's proportions in widescreen (it would otherwise stretch with the image).
    bool widescreen_hud = true;
    bool hud_edges = true;  // ... and move the corner elements to the screen's edges
    // Mouse and keyboard (kbm.cpp): mouse look in missions, Quake-style sensitivity (0.022
    // degrees per count at 1).
    bool mouse_look = true;
    double mouse_sensitivity = 3.0;
    bool invert_mouse = false;
    bool crosshair = false;  // draw a + at the center of the 3D view
    int crosshair_size = 4;  // arm length in original (240-line) pixels
    std::filesystem::path memcard = "saves/memcard1.mcd";  // slot 1 (empty path: no card)
    struct Press { uint64_t frame, duration; uint16_t buttons; };
    std::vector<Press> presses;  // scripted input (--press FRAME:BUTTON[:DURATION])
    struct Stick { uint64_t frame, duration; int axis; uint8_t value; };  // axis: 0 lx 1 ly 2 rx 3 ry
    std::vector<Stick> sticks;   // scripted sticks (--stick FRAME:AXIS=VALUE[:DURATION])
    struct KbmPress { uint64_t frame, duration; int control; };
    std::vector<KbmPress> kbm_presses;  // scripted controls (--kbm FRAME:CONTROL[:DURATION])
    struct MouseMove { uint64_t frame; double dx, dy; bool taken[2]; };
    std::vector<MouseMove> mouse_moves;  // scripted mouse motion (--mouse FRAME:DX,DY)
    uint64_t dump_ram_frame = 0;  // testing: write RAM to dump_ram_path at this frame
    std::string dump_ram_path = "build/ram.bin";
    uint64_t monkey_seed = 0, monkey_from = 0;  // random input from this frame on (--monkey SEED:FRAME)
    // Debugging: write a word into RAM the first time a function is called
    // (--poke-on-call CALL:ADDR=VALUE), e.g. to pick the level the game loads.
    struct Poke { uint32_t call, addr, value; bool done; };
    std::vector<Poke> pokes;
};
extern Config config;

// COP0 system control registers.
struct Cop0 {
    uint32_t sr = 0;
    uint32_t cause = 0;
    uint32_t epc = 0;
};
extern Cop0 cop0;

// memory.cpp
void load_bios(const std::filesystem::path &path);
uint32_t io_read(Cpu *c, uint32_t phys, int size);
void io_write(Cpu *c, uint32_t phys, uint32_t value, int size);

// gte.cpp
void gte_set_projection(double aspect, double hfov_deg, uint16_t world_h, double base_tan);
extern uint64_t gte_projections;
extern double gte_squeeze;  // horizontal squeeze applied to 3D for the output aspect (1 at 4:3)
void gte_trace_frame();  // VALOR_TRACE_GTE_MAT

// cpu.cpp
RecompFn find_function(uint32_t addr);
void take_interrupt(Cpu *c, uint32_t pc);

}  // namespace psx
