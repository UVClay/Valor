// Entry point: power on the console and run the BIOS from its reset vector.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "audio.h"
#include "gpu.h"
#include "hw.h"
#include "input.h"
#include "kbm.h"
#include "memcard.h"
#include "settings.h"
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
    fprintf(stderr, "usage: %s --bios openbios.bin --disc game.cue [--scale N] [--no-dither] [--subpixel] [--depth-buffer] [--stick-aim] [--aspect W:H|window] [--fov DEGREES] [--memcard FILE.mcd|FOLDER] [--memcard-import FILE.mcd] [--volume PERCENT] [--overlay 0-2] [--crosshair SIZE] [--frame-rate FPS] [--no-interpolate] [--window WxH] [--headless] [--hidden]\n"
            "       [--screenshots DIR] [--screenshot-every N] [--screenshot-from FRAME] [--screenshot-window] [--wav FILE]\n"
            "       [--press FRAME:BUTTON[:DURATION]]... [--monkey SEED:FROM_FRAME]\n"
            "       [--kbm FRAME:CONTROL[:DURATION]]... [--mouse FRAME:DX,DY]...\n"
            "       [--stick FRAME:lx|ly|rx|ry=VALUE[:DURATION]]... [--dump-ram FRAME[:FILE]]\n"
            "       [--poke-on-call CALLHEX:ADDRHEX=VALUE]...\n"
            "       [--trace-io] [--trace-calls] [--trace-cd] [--trace-audio] [--max-cycles N]\n", argv0);
}

int main(int argc, char **argv) {
    settings_load();  // command-line flags below override saved settings
    bool memcard_given = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) config.bios = argv[++i];
        else if (!strcmp(argv[i], "--disc") && i + 1 < argc) config.disc = argv[++i];
        else if (!strcmp(argv[i], "--trace-io")) config.trace_io = true;
        else if (!strcmp(argv[i], "--trace-calls")) config.trace_calls = true;
        else if (!strcmp(argv[i], "--trace-cd")) config.trace_cd = true;
        else if (!strcmp(argv[i], "--trace-audio")) config.trace_audio = true;
        else if (!strcmp(argv[i], "--headless")) config.headless = true;
        else if (!strcmp(argv[i], "--hidden")) config.hidden = true;
        else if (!strcmp(argv[i], "--screenshot-window")) config.screenshot_window = true;
        else if (!strcmp(argv[i], "--screenshot-from") && i + 1 < argc) config.screenshot_from = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--menu-screenshot") && i + 1 < argc) config.menu_screenshot = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--overlay") && i + 1 < argc) config.overlay = std::clamp(atoi(argv[++i]), 0, 2);
        else if (!strcmp(argv[i], "--frame-rate") && i + 1 < argc) config.frame_rate = std::clamp(atoi(argv[++i]), 0, 1000);
        else if (!strcmp(argv[i], "--no-interpolate")) config.interpolate = false;
        else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &config.window_w, &config.window_h) != 2) { usage(argv[0]); return 2; }
        }
        else if (!strcmp(argv[i], "--crosshair") && i + 1 < argc) { config.crosshair = true; config.crosshair_size = std::clamp(atoi(argv[++i]), 1, 20); }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) config.scale = std::clamp(atoi(argv[++i]), 0, 16);
        else if (!strcmp(argv[i], "--no-dither")) config.dither = false;
        else if (!strcmp(argv[i], "--subpixel")) config.subpixel = true;
        else if (!strcmp(argv[i], "--depth-buffer")) config.depth_buffer = true;
        else if (!strcmp(argv[i], "--stick-aim")) config.stick_aim = true;
        else if (!strcmp(argv[i], "--fov") && i + 1 < argc) config.fov = std::clamp(atof(argv[++i]), 0.0, 150.0);
        else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) {
            if (!parse_aspect(argv[++i], config.aspect)) { usage(argv[0]); return 2; }
        }        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) config.wav = argv[++i];
        else if (!strcmp(argv[i], "--memcard") && i + 1 < argc) { config.memcard = argv[++i]; memcard_given = true; }
        else if (!strcmp(argv[i], "--memcard-import") && i + 1 < argc) config.memcard_import = argv[++i];
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) config.volume = std::clamp(atoi(argv[++i]), 0, 200);
        else if (!strcmp(argv[i], "--monkey") && i + 1 < argc) {
            unsigned long long seed, from;
            if (sscanf(argv[++i], "%llu:%llu", &seed, &from) != 2) { usage(argv[0]); return 2; }
            config.monkey_seed = seed;
            config.monkey_from = from;
        } else if (!strcmp(argv[i], "--poke-on-call") && i + 1 < argc) {
            unsigned call, addr, value;
            if (sscanf(argv[++i], "%x:%x=%u", &call, &addr, &value) != 3) { usage(argv[0]); return 2; }
            config.pokes.push_back({call, addr, value, false});
        } else if (!strcmp(argv[i], "--stick") && i + 1 < argc) {
            unsigned long long frame, duration = 6;
            char axis[4] = {};
            unsigned value;
            if (sscanf(argv[++i], "%llu:%2[a-z]=%u:%llu", &frame, axis, &value, &duration) < 3) { usage(argv[0]); return 2; }
            static const char *names[] = {"lx", "ly", "rx", "ry"};
            int a = -1;
            for (int k = 0; k < 4; k++) if (!strcmp(axis, names[k])) a = k;
            if (a < 0 || value > 255) { usage(argv[0]); return 2; }
            config.sticks.push_back({frame, duration, a, uint8_t(value)});
        } else if (!strcmp(argv[i], "--kbm") && i + 1 < argc) {
            unsigned long long frame, duration = 6;
            char name[32] = {};
            if (sscanf(argv[++i], "%llu:%31[a-z]:%llu", &frame, name, &duration) < 2) { usage(argv[0]); return 2; }
            int control = -1;
            for (int k = 0; k < hw::KC_COUNT; k++)
                if (!strcmp(hw::kbm_control_key(k) + 5, name)) control = k;
            if (control < 0) { usage(argv[0]); return 2; }
            config.kbm_presses.push_back({frame, duration, control});
        } else if (!strcmp(argv[i], "--mouse") && i + 1 < argc) {
            unsigned long long frame;
            double dx, dy;
            if (sscanf(argv[++i], "%llu:%lf,%lf", &frame, &dx, &dy) != 3) { usage(argv[0]); return 2; }
            config.mouse_moves.push_back({frame, dx, dy, {false, false}});
        } else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc) {
            char *end;
            config.dump_ram_frame = strtoull(argv[++i], &end, 0);
            if (*end == ':') config.dump_ram_path = end + 1;
        } else if (!strcmp(argv[i], "--press") && i + 1 < argc) { if (!parse_press(argv[++i])) { usage(argv[0]); return 2; } }
        else if (!strcmp(argv[i], "--screenshots") && i + 1 < argc) config.screenshots = argv[++i];
        else if (!strcmp(argv[i], "--screenshot-every") && i + 1 < argc) config.screenshot_every = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--max-cycles") && i + 1 < argc) config.max_cycles = strtoll(argv[++i], nullptr, 0);
        else { usage(argv[0]); return 2; }
    }
    if (config.bios.empty()) { usage(argv[0]); return 2; }
    // Test runs (headless/hidden) never touch the player's memory card or settings.
    config.test_run = config.headless || config.hidden;
    if (config.test_run && !memcard_given) config.memcard = "build/test-memcard.mcd";

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
        hw::memcard_flush(true);
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
