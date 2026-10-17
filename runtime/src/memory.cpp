// Physical memory map outside main RAM: scratchpad, I/O ports, BIOS ROM, cache control.
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

#include "runtime.h"

extern "C" {
uint8_t psx_ram[PSX_RAM_SIZE];
uint8_t psx_scratch[0x400];
}

namespace psx {

static uint8_t io_mem[0x2000];  // plain storage for I/O registers without device behavior yet
static std::vector<uint8_t> bios_rom(0x80000);

void load_bios(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) fatal("cannot open BIOS image %s", path.c_str());
    f.read(reinterpret_cast<char *>(bios_rom.data()), bios_rom.size());
    log("BIOS: %s (%zd bytes)", path.c_str(), f.gcount());
}

static void trace_once(const char *kind, uint32_t phys, int size, uint32_t value) {
    static std::set<uint32_t> seen;
    if (!config.trace_io) return;
    if (seen.insert(phys | (kind[0] == 'W' ? 0x80000000u : 0)).second)
        log("io: first %s%d %08X = %08X", kind, size * 8, phys, value);
}

uint32_t io_read(Cpu *c, uint32_t phys, int size) {
    (void)c;
    uint32_t off = phys - 0x1F801000, v = 0;
    memcpy(&v, io_mem + off, size);
    switch (phys) {
    case 0x1F801814:  // GPUSTAT: ready for commands, VRAM transfer and DMA
        v = 0x1C000000;
        break;
    }
    trace_once("R", phys, size, v);
    return v;
}

void io_write(Cpu *c, uint32_t phys, uint32_t value, int size) {
    (void)c;
    trace_once("W", phys, size, value);
    memcpy(io_mem + (phys - 0x1F801000), &value, size);
}

}  // namespace psx

using namespace psx;

extern "C" {

uint32_t psx_read_slow(Cpu *c, uint32_t addr, int size) {
    uint32_t p = psx_phys(addr), v = 0;
    if (p >= 0x1F800000 && p < 0x1F800400) {
        memcpy(&v, psx_scratch + (p & 0x3FF), size);
        return v;
    }
    if (p >= 0x1F801000 && p < 0x1F803000) return io_read(c, p, size);
    if (p >= 0x1FC00000 && p < 0x1FC80000) {
        memcpy(&v, bios_rom.data() + (p - 0x1FC00000), size);
        return v;
    }
    if (p == 0x1FFE0130) return 0;                     // cache control
    fatal("read%d from unmapped %08X", size * 8, addr);
}

void psx_write_slow(Cpu *c, uint32_t addr, uint32_t value, int size) {
    uint32_t p = psx_phys(addr);
    if (p >= 0x1F800000 && p < 0x1F800400) {
        memcpy(psx_scratch + (p & 0x3FF), &value, size);
        return;
    }
    if (p >= 0x1F801000 && p < 0x1F803000) return io_write(c, p, value, size);
    if (p == 0x1FFE0130) return;  // cache control
    fatal("write%d %08X to unmapped %08X", size * 8, value, addr);
}

void psx_events(Cpu *c, uint32_t pc) {
    if (config.max_cycles && c->cycles >= config.max_cycles) fatal("cycle limit reached at %08X", pc);
    c->next_event = c->cycles + 1000000;
}

}  // extern "C"
