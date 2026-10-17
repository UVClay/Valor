// Entry point: power on the console and run the BIOS from its reset vector.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "audio.h"
#include "gpu.h"
#include "hw.h"
#include "input.h"
#include "memcard.h"
#include "runtime.h"

using namespace psx;

// FRAME:BUTTON[:DURATION], button names as on the pad (cross, circle, start, up, l1, ...).
static bool parse_press(const char *arg) {
    static const struct { const char *name; uint16_t bit; } names[] = {
        {"select", hw::BTN_SELECT}, {"l3", hw::BTN_L3}, {"r3", hw::BTN_R3}, {"start", hw::BTN_START},
        {"up", hw::BTN_UP}, {"right", hw::BTN_RIGHT}, {"down", hw::BTN_DOWN}, {"left", hw::BTN_LEFT},
        {"l2", hw::BTN_L2}, {"r2", hw::BTN_R2}, {"l1", hw::BTN_L1}, {"r1", hw::BTN_R1},
        {"triangle", hw::BTN_TRIANGLE}, {"circle", hw::BTN_CIRCLE}, {"cross", hw::BTN_CROSS}, {"square", hw::BTN_SQUARE},
    };
    char button[16] = {};
    unsigned long long frame = 0, duration = 6;
    if (sscanf(arg, "%llu:%15[a-z0-9]:%llu", &frame, button, &duration) < 2) return false;
    for (auto &n : names)
        if (!strcmp(n.name, button)) {
            config.presses.push_back({frame, duration, n.bit});
            return true;
        }
    return false;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s --bios openbios.bin --disc game.cue [--memcard FILE.mcd] [--volume PERCENT] [--headless] [--screenshots DIR] [--screenshot-every N] [--wav FILE]\n"
            "       [--press FRAME:BUTTON[:DURATION]]... [--monkey SEED:FROM_FRAME]\n"
            "       [--trace-io] [--trace-calls] [--trace-cd] [--trace-audio] [--max-cycles N]\n", argv0);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) config.bios = argv[++i];
        else if (!strcmp(argv[i], "--disc") && i + 1 < argc) config.disc = argv[++i];
        else if (!strcmp(argv[i], "--trace-io")) config.trace_io = true;
        else if (!strcmp(argv[i], "--trace-calls")) config.trace_calls = true;
        else if (!strcmp(argv[i], "--trace-cd")) config.trace_cd = true;
        else if (!strcmp(argv[i], "--trace-audio")) config.trace_audio = true;
        else if (!strcmp(argv[i], "--headless")) config.headless = true;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) config.wav = argv[++i];
        else if (!strcmp(argv[i], "--memcard") && i + 1 < argc) config.memcard = argv[++i];
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) config.volume = std::clamp(atoi(argv[++i]), 0, 200);
        else if (!strcmp(argv[i], "--monkey") && i + 1 < argc) {
            unsigned long long seed, from;
            if (sscanf(argv[++i], "%llu:%llu", &seed, &from) != 2) { usage(argv[0]); return 2; }
            config.monkey_seed = seed;
            config.monkey_from = from;
        } else if (!strcmp(argv[i], "--press") && i + 1 < argc) { if (!parse_press(argv[++i])) { usage(argv[0]); return 2; } }
        else if (!strcmp(argv[i], "--screenshots") && i + 1 < argc) config.screenshots = argv[++i];
        else if (!strcmp(argv[i], "--screenshot-every") && i + 1 < argc) config.screenshot_every = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--max-cycles") && i + 1 < argc) config.max_cycles = strtoll(argv[++i], nullptr, 0);
        else { usage(argv[0]); return 2; }
    }
    if (config.bios.empty()) { usage(argv[0]); return 2; }

    static Cpu cpu;
    cpu.tail_link = 1;
    try {
        load_bios(config.bios);
        hw::events_init(&cpu);
        hw::timers_init();
        hw::cdrom_init();
        hw::gpu_init();
        hw::display_init(!config.headless);
        hw::audio_init();
        hw::spu_init();
        hw::sio_init();
        hw::memcard_init();
        hw::mdec_init();
        cop0.sr = 1u << 22;  // BEV: exceptions go to the ROM vector until the BIOS installs its own
        psx_call(&cpu, bios_project.entry);
        log("BIOS reset handler returned");
    } catch (const Halt &h) {
        log("halt: %s", h.what());
        log("  frames=%llu primitives=%llu", (unsigned long long)hw::gpu.frames, (unsigned long long)hw::gpu.primitives);
        log("  cycles=%lld sr=%08X cause=%08X epc=%08X", (long long)cpu.cycles, cop0.sr, cop0.cause, cop0.epc);
        static const char *names[32] = {"zr", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
                                        "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
                                        "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
        for (int i = 0; i < 32; i += 4)
            log("  %s=%08X %s=%08X %s=%08X %s=%08X", names[i], cpu.r[i], names[i + 1], cpu.r[i + 1],
                names[i + 2], cpu.r[i + 2], names[i + 3], cpu.r[i + 3]);
        uint32_t vec[4];
        memcpy(vec, psx_ram + 0x80, sizeof vec);
        log("  vector@80: %08X %08X %08X %08X", vec[0], vec[1], vec[2], vec[3]);
        std::string recent;
        for (uint32_t i = 16; i > 0; i--) {
            char buf[12];
            snprintf(buf, sizeof buf, " %08X", psx_trace_ring[(psx_trace_pos - i) & (PSX_TRACE_SIZE - 1)]);
            recent += buf;
        }
        log("  recent functions (oldest first):%s", recent.c_str());
        return 1;
    }
    return 0;
}
