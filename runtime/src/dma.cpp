// DMA controller at 0x1F801080. Transfers complete immediately.
#include "hw.h"

namespace psx::hw {

uint32_t gpu_packet_addr;

namespace {

enum Channel { MDEC_IN, MDEC_OUT, GPU, CDROM, SPU, PIO, OTC };

struct Chan {
    uint32_t madr = 0, bcr = 0, chcr = 0;
};

Chan chans[7];
uint32_t dpcr = 0x07654321, dicr = 0;

uint32_t ram_word(uint32_t addr) {
    uint32_t v;
    memcpy(&v, psx_ram + (addr & 0x1FFFFC), 4);
    return v;
}

void update_master_flag() {
    bool enabled = (dicr >> 16) & (dicr >> 24) & 0x7F;
    bool master = (dicr & 0x8000) || ((dicr & 0x800000) && enabled);
    bool was = dicr & 0x80000000u;
    dicr = (dicr & 0x7FFFFFFFu) | (master ? 0x80000000u : 0);
    if (master && !was) irq_raise(IRQ_DMA);
}

void finish(int n) {
    chans[n].chcr &= ~0x11000000u;
    if (dicr & (1u << (16 + n))) dicr |= 1u << (24 + n);
    uint32_t before = dicr;
    update_master_flag();
    if (config.trace_cd && n == 3) log("dma3: done, dicr %08X -> %08X", before, dicr);
}

// GPU linked list (ordering table): headers are {next:24, words:8}.
void gpu_linked_list(uint32_t addr) {
    for (int guard = 0; guard < 0x100000; guard++) {
        uint32_t header = ram_word(addr);
        uint32_t words = header >> 24;
        gpu_packet_addr = addr;  // for GPU tracing
        for (uint32_t i = 1; i <= words; i++) gpu_gp0(ram_word(addr + 4 * i));
        if (header & 0x800000) return;  // end marker 0xFFFFFF
        addr = header & 0x1FFFFC;
    }
    fatal("GPU DMA linked list doesn't terminate");
}

void run(int n) {
    Chan &ch = chans[n];
    uint32_t sync = (ch.chcr >> 9) & 3;
    bool from_ram = ch.chcr & 1;
    int step = (ch.chcr & 2) ? -4 : 4;
    uint32_t addr = ch.madr & 0x1FFFFC;
    uint32_t words = sync == 0 ? ((ch.bcr & 0xFFFF) ? ch.bcr & 0xFFFF : 0x10000)
                               : (ch.bcr & 0xFFFF) * (ch.bcr >> 16);

    if (n == GPU && sync == 2) {
        gpu_linked_list(addr);
    } else if (n == OTC) {
        // Clear an ordering table backwards: each entry points to the previous one.
        for (uint32_t i = 0; i < words; i++, addr -= 4) {
            uint32_t v = i == words - 1 ? 0x00FFFFFF : ((addr - 4) & 0x1FFFFF);
            memcpy(psx_ram + addr, &v, 4);
        }
    } else if (n == GPU) {
        for (uint32_t i = 0; i < words; i++, addr += step) {
            if (from_ram) gpu_gp0(ram_word(addr));
            else { uint32_t v = gpu_read_data(); memcpy(psx_ram + (addr & 0x1FFFFC), &v, 4); }
        }
    } else if (n == CDROM) {
        cdrom_dma_read(addr, words);
    } else if (n == SPU) {
        if (from_ram) spu_dma_write(addr, words);
        else spu_dma_read(addr, words);
    } else if (n == MDEC_IN) {
        mdec_dma_in(addr, words);
    } else if (n == MDEC_OUT) {
        mdec_dma_out(addr, words);
    } else {
        log("dma: channel %d not implemented (chcr %08X)", n, ch.chcr);
    }
    if (sync == 1) ch.madr = (ch.madr + words * 4) & 0xFFFFFF;  // block mode advances MADR
    if (sync == 1) ch.bcr &= 0xFFFF;
    finish(n);
}

}  // namespace

uint32_t dma_read(uint32_t phys) {
    if (phys == 0x1F8010F0) return dpcr;
    if (phys == 0x1F8010F4) return dicr;
    int n = (phys >> 4) & 7;
    switch (phys & 0xF) {
    case 0x0: return chans[n].madr;
    case 0x4: return chans[n].bcr;
    case 0x8: return chans[n].chcr;
    }
    return 0;
}

void dma_write(uint32_t phys, uint32_t value, int size) {
    // Sub-word writes only touch the bytes they cover (libetc writes DICR's halves separately).
    uint32_t shift = 8 * (phys & 3);
    uint32_t mask = (size == 4 ? 0xFFFFFFFFu : ((1u << (8 * size)) - 1)) << shift;
    uint32_t v = value << shift;
    phys &= ~3u;
    if (phys == 0x1F8010F4) {
        if (config.trace_cd) log("dma: dicr write %08X mask %08X (was %08X)", v, mask, dicr);
        // Bits 24-30 acknowledge on write of 1; bits 0-5, 15-23 are plain R/W.
        uint32_t rw = 0x00FF803Fu & mask;
        dicr = (dicr & ~rw & ~(v & 0x7F000000u & mask)) | (v & rw);
        dicr &= ~0x80000000u;
        update_master_flag();
        return;
    }
    v = (dma_read(phys) & ~mask) | (v & mask);
    if (phys == 0x1F8010F0) { dpcr = v; return; }
    int n = (phys >> 4) & 7;
    Chan &ch = chans[n];
    switch (phys & 0xF) {
    case 0x0: ch.madr = v & 0xFFFFFF; break;
    case 0x4: ch.bcr = v; break;
    case 0x8:
        ch.chcr = n == OTC ? (v & 0x51000000u) | 2 : v;
        {
            bool start = ch.chcr & 0x01000000u;
            bool manual = ((ch.chcr >> 9) & 3) == 0;
            bool enabled = (dpcr >> (4 * n + 3)) & 1;
            if (start && enabled && (!manual || (ch.chcr & 0x10000000u) || n == OTC)) run(n);
        }
        break;
    }
}

}  // namespace psx::hw
