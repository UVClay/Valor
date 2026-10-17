// Physical memory map outside main RAM: scratchpad, I/O ports, BIOS ROM, cache control.
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

#include "hw.h"
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
    uint32_t v;
    if (phys >= 0x1F801040 && phys < 0x1F801050) v = hw::sio_read(phys, size);
    else if (phys == 0x1F801070 || phys == 0x1F801074) v = hw::irq_read(phys);
    else if (phys >= 0x1F801080 && phys < 0x1F801100) v = hw::dma_read(phys & ~3u) >> (8 * (phys & 3));
    else if (phys >= 0x1F801100 && phys < 0x1F801130) v = hw::timers_read(phys);
    else if (phys >= 0x1F801800 && phys < 0x1F801804) {
        v = hw::cdrom_read(phys);
        for (int i = 1; i < size; i++) v |= uint32_t(hw::cdrom_read(phys)) << (8 * i);  // FIFO reads
    } else if (phys == 0x1F801810 || phys == 0x1F801814) v = hw::gpu_read(phys);
    else if (phys == 0x1F801820 || phys == 0x1F801824) v = hw::mdec_read(phys);
    else if (phys >= 0x1F801C00 && phys < 0x1F802000) {
        v = hw::spu_read(phys & ~1u);
        if (size == 4) v |= uint32_t(hw::spu_read((phys & ~1u) + 2)) << 16;
    } else {
        v = 0;
        memcpy(&v, io_mem + (phys - 0x1F801000), size);  // memory control, SIO1, expansion 2
    }
    if (size == 1) v &= 0xFF;
    else if (size == 2) v &= 0xFFFF;
    trace_once("R", phys, size, v);
    return v;
}

void io_write(Cpu *c, uint32_t phys, uint32_t value, int size) {
    (void)c;
    trace_once("W", phys, size, value);
    if (phys >= 0x1F801040 && phys < 0x1F801050) hw::sio_write(phys, value, size);
    else if (phys == 0x1F801070 || phys == 0x1F801074) hw::irq_write(phys, value);
    else if (phys >= 0x1F801080 && phys < 0x1F801100) hw::dma_write(phys & ~3u, value);
    else if (phys >= 0x1F801100 && phys < 0x1F801130) hw::timers_write(phys, value);
    else if (phys >= 0x1F801800 && phys < 0x1F801804) hw::cdrom_write(phys, value & 0xFF);
    else if (phys == 0x1F801810 || phys == 0x1F801814) hw::gpu_write(phys, value);
    else if (phys == 0x1F801820 || phys == 0x1F801824) hw::mdec_write(phys, value);
    else if (phys >= 0x1F801C00 && phys < 0x1F802000) {
        hw::spu_write(phys & ~1u, value & 0xFFFF);
        if (size == 4) hw::spu_write((phys & ~1u) + 2, value >> 16);
    } else {
        memcpy(io_mem + (phys - 0x1F801000), &value, size);
    }
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
    if (p >= 0x1F000000 && p < 0x1F800000) return 0xFFFFFFFFu >> (32 - 8 * size);  // expansion 1: empty
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
    if (p >= 0x1F000000 && p < 0x1F800000) return;  // expansion 1: empty
    fatal("write%d %08X to unmapped %08X", size * 8, value, addr);
}


}  // extern "C"
