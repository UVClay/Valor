// Event scheduler, interrupt controller and the VBlank clock.
#include <limits>

#include "hw.h"

namespace psx::hw {

Cpu *cpu;

static constexpr int64_t NEVER = std::numeric_limits<int64_t>::max();
static int64_t event_time[int(Event::Count)];
static uint32_t i_stat, i_mask;

void schedule_at(Event ev, int64_t when) {
    event_time[int(ev)] = when;
    recheck();
}
void cancel(Event ev) { event_time[int(ev)] = NEVER; }
bool pending(Event ev) { return event_time[int(ev)] != NEVER; }
void recheck() {
    if (cpu) cpu->next_event = cpu->cycles;
}

void irq_raise(int line) {
    i_stat |= 1u << line;
    recheck();
}

uint32_t irq_read(uint32_t phys) { return phys == 0x1F801070 ? i_stat : i_mask; }

void irq_write(uint32_t phys, uint32_t v) {
    if (phys == 0x1F801070) i_stat &= v;  // writing 0 acknowledges
    else i_mask = v & 0x7FF;
    recheck();
}

static void vblank() {
    irq_raise(IRQ_VBLANK);
    gpu_vblank();
    timers_hblank_tick(263);
    schedule(Event::VBlank, CYCLES_PER_FRAME);
}

static void run_due_events(int64_t t) {
    for (bool ran = true; ran;) {
        ran = false;
        for (int e = 0; e < int(Event::Count); e++) {
            if (event_time[e] > t) continue;
            event_time[e] = NEVER;
            ran = true;
            switch (Event(e)) {
            case Event::VBlank: vblank(); break;
            case Event::Timer0: timers_event(0); break;
            case Event::Timer1: timers_event(1); break;
            case Event::Timer2: timers_event(2); break;
            case Event::CdResponse: cdrom_event_response(); break;
            case Event::CdSector: cdrom_event_sector(); break;
            case Event::SioAck: sio_event_ack(); break;
            case Event::Spu: spu_event(); break;
            case Event::Count: break;
            }
        }
    }
}

void events_init(Cpu *c) {
    cpu = c;
    for (auto &t : event_time) t = NEVER;
    schedule(Event::VBlank, CYCLES_PER_FRAME);
}

}  // namespace psx::hw

using namespace psx;
using namespace psx::hw;

extern "C" void psx_events(Cpu *c, uint32_t pc) {
    if (config.max_cycles && c->cycles >= config.max_cycles) fatal("cycle limit reached at %08X", pc);
    run_due_events(now());

    int64_t next = NEVER;
    for (int64_t t : event_time) next = std::min(next, t);
    c->next_event = next == NEVER ? NEVER : (next + CYCLES_PER_INSTRUCTION - 1) / CYCLES_PER_INSTRUCTION;
    if (config.max_cycles) c->next_event = std::min(c->next_event, config.max_cycles);

    // COP0: IEc (bit 0) and IM2 (bit 10, the interrupt controller's line).
    if ((i_stat & i_mask) && (cop0.sr & 0x401) == 0x401) {
        cop0.cause |= 0x400;
        take_interrupt(c, pc);
    }
    if (!(i_stat & i_mask)) cop0.cause &= ~0x400u;
}
