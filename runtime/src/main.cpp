// Entry point: power on the console and run the BIOS from its reset vector.
#include <cstdlib>
#include <cstring>

#include "runtime.h"

using namespace psx;

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s --bios openbios.bin --disc game.cue [--trace-io] [--max-cycles N]\n", argv0);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) config.bios = argv[++i];
        else if (!strcmp(argv[i], "--disc") && i + 1 < argc) config.disc = argv[++i];
        else if (!strcmp(argv[i], "--trace-io")) config.trace_io = true;
        else if (!strcmp(argv[i], "--max-cycles") && i + 1 < argc) config.max_cycles = strtoll(argv[++i], nullptr, 0);
        else { usage(argv[0]); return 2; }
    }
    if (config.bios.empty()) { usage(argv[0]); return 2; }

    static Cpu cpu;
    try {
        load_bios(config.bios);
        cop0.sr = 1u << 22;  // BEV: exceptions go to the ROM vector until the BIOS installs its own
        psx_call(&cpu, bios_project.entry);
        log("BIOS reset handler returned");
    } catch (const Halt &h) {
        log("halt: %s", h.what());
        log("  cycles=%lld sp=%08X ra=%08X", (long long)cpu.cycles, cpu.r[29], cpu.r[31]);
        return 1;
    }
    return 0;
}
