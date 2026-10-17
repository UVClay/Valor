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
    int scale = 0;           // OpenGL renderer resolution scale (0 = fit the window, 1 = software)
    bool dither = true;      // PS1 dithering and 15-bit color
    std::filesystem::path screenshots;  // save a PNG of the display every `screenshot_every` frames
    uint64_t screenshot_every = 60;
    bool screenshot_window = false;  // capture the window (with overlay) instead of the PS1 image
    int overlay = 0;                 // initial debug overlay level
    std::filesystem::path wav;  // also record audio output here
    int volume = 100;           // output volume, percent
    std::filesystem::path memcard = "saves/memcard1.mcd";  // slot 1 (empty path: no card)
    struct Press { uint64_t frame, duration; uint16_t buttons; };
    std::vector<Press> presses;  // scripted input (--press FRAME:BUTTON[:DURATION])
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

// cpu.cpp
RecompFn find_function(uint32_t addr);
void take_interrupt(Cpu *c, uint32_t pc);

}  // namespace psx
