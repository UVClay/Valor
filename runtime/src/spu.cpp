// SPU registers at 0x1F801C00 and sound RAM. Voice playback and mixing come later.
#include <vector>

#include "hw.h"

namespace psx::hw {

namespace {

uint16_t regs[0x200];            // 0x1F801C00..0x1F801FFF
std::vector<uint8_t> ram(0x80000);
uint32_t transfer_addr;          // byte address in sound RAM

uint16_t &reg(uint32_t phys) { return regs[(phys - 0x1F801C00) >> 1]; }

constexpr uint32_t SPUCNT = 0x1F801DAA, SPUSTAT = 0x1F801DAE, TRANSFER_ADDR = 0x1F801DA6,
                   TRANSFER_FIFO = 0x1F801DA8, KON = 0x1F801D88, KOFF = 0x1F801D8C, ENDX = 0x1F801D9C;

void write_ram(uint16_t v) {
    ram[transfer_addr & 0x7FFFE] = v & 0xFF;
    ram[(transfer_addr + 1) & 0x7FFFF] = v >> 8;
    transfer_addr = (transfer_addr + 2) & 0x7FFFF;
}

}  // namespace

void spu_init() {
    std::fill(std::begin(regs), std::end(regs), 0);
    std::fill(ram.begin(), ram.end(), 0);
}

uint16_t spu_read(uint32_t phys) {
    if (phys == SPUSTAT) {
        uint16_t cnt = reg(SPUCNT);
        uint16_t s = cnt & 0x3F;
        uint16_t mode = (cnt >> 4) & 3;  // transfer mode
        if (mode == 2) s |= 0x0100 | 0x0080;  // DMA write request
        if (mode == 3) s |= 0x0200 | 0x0080;  // DMA read request
        return s;
    }
    if (phys == TRANSFER_ADDR) return uint16_t(transfer_addr >> 3);
    return reg(phys);
}

void spu_write(uint32_t phys, uint16_t v) {
    if (phys < 0x1F801C00 || phys >= 0x1F802000) return;
    reg(phys) = v;
    if (phys == TRANSFER_ADDR) transfer_addr = uint32_t(v) << 3;
    else if (phys == TRANSFER_FIFO) write_ram(v);
    else if (phys == KON || phys == KON + 2) {
        // Keyed-on voices clear their ENDX bit.
        uint32_t bits = phys == KON ? v : uint32_t(v) << 16;
        uint32_t endx = reg(ENDX) | (uint32_t(reg(ENDX + 2)) << 16);
        endx &= ~bits;
        reg(ENDX) = endx & 0xFFFF;
        reg(ENDX + 2) = endx >> 16;
    }
}

void spu_dma_write(uint32_t addr, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, psx_ram + ((addr + 4 * i) & 0x1FFFFC), 4);
        write_ram(w & 0xFFFF);
        write_ram(w >> 16);
    }
}

void spu_dma_read(uint32_t addr, uint32_t words) {
    for (uint32_t i = 0; i < words * 4; i++) {
        psx_ram[(addr + i) & (PSX_RAM_SIZE - 1)] = ram[transfer_addr];
        transfer_addr = (transfer_addr + 1) & 0x7FFFF;
    }
}

}  // namespace psx::hw
