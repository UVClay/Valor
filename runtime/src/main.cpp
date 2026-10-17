// Entry point: power on the console and run the BIOS from its reset vector.
#include <cstdlib>
#include <cstring>
#include <string>

#include "gpu.h"
#include "hw.h"
#include "runtime.h"

using namespace psx;

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s --bios openbios.bin --disc game.cue [--trace-io] [--trace-calls] [--trace-cd] [--max-cycles N]\n", argv0);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) config.bios = argv[++i];
        else if (!strcmp(argv[i], "--disc") && i + 1 < argc) config.disc = argv[++i];
        else if (!strcmp(argv[i], "--trace-io")) config.trace_io = true;
        else if (!strcmp(argv[i], "--trace-calls")) config.trace_calls = true;
        else if (!strcmp(argv[i], "--trace-cd")) config.trace_cd = true;
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
        hw::spu_init();
        hw::sio_init();
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
