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
    int64_t max_cycles = 0;  // stop after this many cycles (0 = run forever)
    bool headless = false;   // no window
    std::filesystem::path screenshots;  // save a PNG of the display every `screenshot_every` frames
    uint64_t screenshot_every = 60;
    std::filesystem::path wav;  // also record audio output here
    struct Press { uint64_t frame, duration; uint16_t buttons; };
    std::vector<Press> presses;  // scripted input (--press FRAME:BUTTON[:DURATION])
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
