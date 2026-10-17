// Emulated PlayStation hardware shared state: clock, event scheduler, interrupt controller.
#pragma once
#include <cstdint>

#include "runtime.h"

namespace psx::hw {

constexpr int64_t CPU_HZ = 33868800;
// Recompiled code counts instructions; PCSX's default of 2 cycles per instruction is what
// the "feels like PCSX" target is calibrated against.
constexpr int64_t CYCLES_PER_INSTRUCTION = 2;
constexpr int64_t CYCLES_PER_FRAME = 566107;  // NTSC: 263 lines at 59.826 Hz
constexpr int64_t CYCLES_PER_LINE = CYCLES_PER_FRAME / 263;

extern Cpu *cpu;
void events_init(Cpu *c);

inline int64_t now() { return cpu->cycles * CYCLES_PER_INSTRUCTION; }

enum class Event { VBlank, Timer0, Timer1, Timer2, CdResponse, CdSector, SioAck, Count };

// Run `ev` at absolute cycle `when` (replaces a pending one of the same kind).
void schedule_at(Event ev, int64_t when);
inline void schedule(Event ev, int64_t delay) { schedule_at(ev, now() + delay); }
void cancel(Event ev);
bool pending(Event ev);
// Make psx_events run at the next check site (interrupt state changed).
void recheck();

// Interrupt lines (I_STAT bits).
enum Irq { IRQ_VBLANK = 0, IRQ_GPU = 1, IRQ_CDROM = 2, IRQ_DMA = 3, IRQ_TIMER0 = 4, IRQ_TIMER1 = 5,
           IRQ_TIMER2 = 6, IRQ_SIO0 = 7, IRQ_SIO = 8, IRQ_SPU = 9, IRQ_PIO = 10 };
void irq_raise(int line);

// Device entry points: register accesses (phys address, size in bytes) and event handlers.
uint32_t irq_read(uint32_t phys);
void irq_write(uint32_t phys, uint32_t v);

void timers_init();
uint32_t timers_read(uint32_t phys);
void timers_write(uint32_t phys, uint32_t v);
void timers_event(int n);
void timers_hblank_tick(int lines);

uint32_t dma_read(uint32_t phys);
void dma_write(uint32_t phys, uint32_t v);

void cdrom_init();
uint8_t cdrom_read(uint32_t phys);
void cdrom_write(uint32_t phys, uint8_t v);
void cdrom_event_response();
void cdrom_event_sector();
// DMA3: copy `words` words of the sector data FIFO into RAM.
void cdrom_dma_read(uint32_t addr, uint32_t words);

void gpu_init();
uint32_t gpu_read(uint32_t phys);
void gpu_write(uint32_t phys, uint32_t v);
void gpu_gp0(uint32_t word);
uint32_t gpu_read_data();
void gpu_vblank();

void spu_init();
uint16_t spu_read(uint32_t phys);
void spu_write(uint32_t phys, uint16_t v);
void spu_dma_write(uint32_t addr, uint32_t words);
void spu_dma_read(uint32_t addr, uint32_t words);

void sio_init();
uint32_t sio_read(uint32_t phys, int size);
void sio_write(uint32_t phys, uint32_t v, int size);
void sio_event_ack();

void mdec_init();
uint32_t mdec_read(uint32_t phys);
void mdec_write(uint32_t phys, uint32_t v);
void mdec_dma_in(uint32_t addr, uint32_t words);
void mdec_dma_out(uint32_t addr, uint32_t words);

}  // namespace psx::hw
