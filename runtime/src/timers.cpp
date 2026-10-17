// Root counters (timers 0-2) at 0x1F801100. Counters are computed from the clock on demand.
#include "hw.h"

namespace psx::hw {

namespace {

struct Timer {
    uint16_t mode = 0;
    uint16_t target = 0;
    int64_t base = 0;      // clock (in source ticks * rate) when the counter was 0
    uint32_t value_at_base = 0;
    bool irq_fired = false;  // one-shot mode latch
    bool next_is_target = false;  // what the scheduled event is for: target or overflow
};

Timer timers[3];
uint64_t hblanks;  // timer 1 hblank source

// Source ticks happen every `rate` CPU cycles (0 means hblank source).
int64_t rate(int n) {
    int src = (timers[n].mode >> 8) & 3;
    switch (n) {
    case 0: return 1;  // dotclock (src 1/3) approximated by the system clock
    case 1: return (src & 1) ? 0 : 1;
    default: return (src & 2) ? 8 : 1;
    }
}

uint64_t ticks(int n) {
    int64_t r = rate(n);
    if (r == 0) return hblanks;
    return uint64_t(now()) / uint64_t(r);
}

uint32_t current(int n) {
    Timer &t = timers[n];
    uint64_t elapsed = ticks(n) - uint64_t(t.base);
    uint32_t limit = (t.mode & 0x08) && t.target ? t.target + 1u : 0x10000u;
    return uint32_t((t.value_at_base + elapsed) % limit);
}

void reschedule(int n) {
    Timer &t = timers[n];
    Event ev = Event(int(Event::Timer0) + n);
    cancel(ev);
    if (!(t.mode & 0x30)) return;  // no IRQ source enabled
    int64_t r = rate(n);
    if (r == 0) return;  // hblank-clocked: checked in timers_hblank_tick
    uint32_t v = current(n);
    uint32_t dist = 0x10000 - v;
    t.next_is_target = (t.mode & 0x10) && t.target > v;
    if (t.next_is_target) dist = t.target - v;
    schedule(ev, int64_t(dist) * r);
}

void reached(int n, bool target) {
    Timer &t = timers[n];
    t.mode |= target ? 0x0800 : 0x1000;
    bool want = target ? (t.mode & 0x10) : (t.mode & 0x20);
    if (want && !(!(t.mode & 0x40) && t.irq_fired)) {
        t.irq_fired = true;
        if (t.mode & 0x80) t.mode ^= 0x0400;  // toggle mode
        else t.mode &= ~0x0400;                // pulse: bit 10 low = IRQ requested
        irq_raise(IRQ_TIMER0 + n);
        if (!(t.mode & 0x80)) t.mode |= 0x0400;
    }
}

}  // namespace

void timers_init() {
    for (auto &t : timers) t = Timer{};
}

void timers_event(int n) {
    Timer &t = timers[n];
    reached(n, t.next_is_target);
    // Schedule from where the counter really is (events run at the next check site, slightly late).
    t.value_at_base = current(n);
    t.base = ticks(n);
    reschedule(n);
}

void timers_hblank_tick(int lines) {
    uint32_t before = current(1);
    hblanks += lines;
    Timer &t = timers[1];
    if (rate(1) != 0) return;
    uint32_t after = current(1);
    if ((t.mode & 0x10) && before < t.target && (after >= t.target || after < before)) reached(1, true);
    if (after < before) reached(1, false);
}

uint32_t timers_read(uint32_t phys) {
    int n = (phys >> 4) & 3;
    if (n > 2) return 0;
    Timer &t = timers[n];
    switch (phys & 0xF) {
    case 0x0: return current(n);
    case 0x4: {
        uint32_t m = t.mode;
        t.mode &= ~0x1800;  // reached flags clear on read
        return m;
    }
    case 0x8: return t.target;
    }
    return 0;
}

void timers_write(uint32_t phys, uint32_t v) {
    int n = (phys >> 4) & 3;
    if (n > 2) return;
    Timer &t = timers[n];
    switch (phys & 0xF) {
    case 0x0:
        t.value_at_base = v & 0xFFFF;
        t.base = ticks(n);
        break;
    case 0x4:
        t.mode = (v & 0x3FF) | 0x0400;  // writing the mode resets the counter
        t.value_at_base = 0;
        t.base = ticks(n);
        t.irq_fired = false;
        break;
    case 0x8:
        t.value_at_base = current(n);
        t.base = ticks(n);
        t.target = v & 0xFFFF;
        break;
    }
    reschedule(n);
}

}  // namespace psx::hw
