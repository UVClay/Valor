/* Interface between recompiled code and the runtime. Included by every generated file. */
#pragma once
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Cpu {
    uint32_t r[32];
    uint32_t hi, lo;
    int64_t cycles;      /* instructions executed (scaled to CPU cycles by the runtime) */
    int64_t next_event;  /* when `cycles` reaches this, psx_events() must run */
    /* A tail call passes its caller's return address on, so `jr $ra` in the tail-called
       function can tell a normal return from a context switch. */
    uint32_t tail_ra;
    uint32_t tail_link;  /* $ra when the tail call was made: a stale handoff won't match */
} Cpu;

/* Return address the current function must return to (see Cpu.tail_ra). */
static inline uint32_t psx_entry_ra(Cpu *c) {
    uint32_t ra = c->r[31] == c->tail_link ? c->tail_ra : c->r[31];
    c->tail_link = 1; /* never a valid $ra (unaligned) */
    return ra;
}
#define ENTRY_RA(c) psx_entry_ra(c)
#define TAIL(c, ra) ((c)->tail_ra = (ra), (c)->tail_link = (c)->r[31])

typedef void (*RecompFn)(Cpu *c);

typedef struct RecompEntry {
    uint32_t addr;
    RecompFn fn;
} RecompEntry;

typedef struct RecompModule {
    const char *name;
    const RecompEntry *funcs;  /* sorted by addr */
    uint32_t count;
} RecompModule;

#define PSX_RAM_SIZE 0x200000
extern uint8_t psx_ram[PSX_RAM_SIZE];
extern uint8_t psx_scratch[0x400];
/* COP0 SR.IsC: while the cache is isolated, stores don't reach memory. */
extern int psx_cache_isolated;

/* Slow paths: scratchpad, I/O, BIOS ROM, mirrors. */
uint32_t psx_read_slow(Cpu *c, uint32_t addr, int size);
void psx_write_slow(Cpu *c, uint32_t addr, uint32_t value, int size);

static inline uint32_t psx_phys(uint32_t a) { return a & 0x1FFFFFFF; }

static inline uint32_t psx_r32(Cpu *c, uint32_t a) {
    uint32_t p = psx_phys(a), v;
    if (p < 0x800000) { memcpy(&v, psx_ram + (p & (PSX_RAM_SIZE - 1)), 4); return v; }
    return psx_read_slow(c, a, 4);
}
static inline uint32_t psx_r16(Cpu *c, uint32_t a) {
    uint32_t p = psx_phys(a);
    uint16_t v;
    if (p < 0x800000) { memcpy(&v, psx_ram + (p & (PSX_RAM_SIZE - 1)), 2); return v; }
    return psx_read_slow(c, a, 2);
}
static inline uint32_t psx_r8(Cpu *c, uint32_t a) {
    uint32_t p = psx_phys(a);
    if (p < 0x800000) return psx_ram[p & (PSX_RAM_SIZE - 1)];
    return psx_read_slow(c, a, 1);
}
static inline void psx_w32(Cpu *c, uint32_t a, uint32_t v) {
    uint32_t p = psx_phys(a);
    if (psx_cache_isolated) return;
    if (p < 0x800000) { memcpy(psx_ram + (p & (PSX_RAM_SIZE - 1)), &v, 4); return; }
    psx_write_slow(c, a, v, 4);
}
static inline void psx_w16(Cpu *c, uint32_t a, uint32_t v) {
    uint32_t p = psx_phys(a);
    if (psx_cache_isolated) return;
    uint16_t h = (uint16_t)v;
    if (p < 0x800000) { memcpy(psx_ram + (p & (PSX_RAM_SIZE - 1)), &h, 2); return; }
    psx_write_slow(c, a, v & 0xFFFF, 2);
}
static inline void psx_w8(Cpu *c, uint32_t a, uint32_t v) {
    uint32_t p = psx_phys(a);
    if (psx_cache_isolated) return;
    if (p < 0x800000) { psx_ram[p & (PSX_RAM_SIZE - 1)] = (uint8_t)v; return; }
    psx_write_slow(c, a, v & 0xFF, 1);
}

/* Unaligned word access (little-endian semantics). */
static inline uint32_t psx_lwl(Cpu *c, uint32_t a, uint32_t old) {
    uint32_t w = psx_r32(c, a & ~3u), sh = (a & 3) * 8;
    return (old & (0x00FFFFFFu >> sh)) | (w << (24 - sh));
}
static inline uint32_t psx_lwr(Cpu *c, uint32_t a, uint32_t old) {
    uint32_t w = psx_r32(c, a & ~3u), sh = (a & 3) * 8;
    return (old & ~(0xFFFFFFFFu >> sh)) | (w >> sh);
}
static inline void psx_swl(Cpu *c, uint32_t a, uint32_t v) {
    uint32_t w = psx_r32(c, a & ~3u), sh = (a & 3) * 8;
    psx_w32(c, a & ~3u, (w & ~(0xFFFFFFFFu >> (24 - sh))) | (v >> (24 - sh)));
}
static inline void psx_swr(Cpu *c, uint32_t a, uint32_t v) {
    uint32_t w = psx_r32(c, a & ~3u), sh = (a & 3) * 8;
    psx_w32(c, a & ~3u, (w & ~(0xFFFFFFFFu << sh)) | (v << sh));
}

/* Division with R3000A results for divide-by-zero and overflow. */
static inline void psx_div(Cpu *c, uint32_t rs, uint32_t rt) {
    int32_t n = (int32_t)rs, d = (int32_t)rt;
    if (d == 0) { c->hi = rs; c->lo = n >= 0 ? 0xFFFFFFFFu : 1u; }
    else if (rs == 0x80000000u && d == -1) { c->hi = 0; c->lo = 0x80000000u; }
    else { c->lo = (uint32_t)(n / d); c->hi = (uint32_t)(n % d); }
}
static inline void psx_divu(Cpu *c, uint32_t rs, uint32_t rt) {
    if (rt == 0) { c->hi = rs; c->lo = 0xFFFFFFFFu; }
    else { c->lo = rs / rt; c->hi = rs % rt; }
}

/* Control flow out of generated code. */
void psx_call(Cpu *c, uint32_t addr);                      /* indirect call / tail call */
void psx_unhandled_jump(Cpu *c, uint32_t pc, uint32_t target);
void psx_events(Cpu *c, uint32_t pc);                       /* timers, DMA, interrupts at pc */
void psx_exception_return(Cpu *c, uint32_t target);         /* jr + rfe: back to the interrupted code */
void psx_jump_ra(Cpu *c);                                   /* longjmp-style jr $ra into a saved context */
void psx_run_ram_code(Cpu *c, uint32_t start, uint32_t end); /* interpret code patched at run time */
void psx_syscall(Cpu *c, uint32_t pc);
void psx_break(Cpu *c, uint32_t pc, uint32_t code);
void psx_bios_call(Cpu *c, uint32_t vector);                /* A0/B0/C0; function number in t1 */

/* COP0 */
uint32_t psx_mfc0(Cpu *c, int reg);
void psx_mtc0(Cpu *c, int reg, uint32_t v);
void psx_rfe(Cpu *c);

/* COP2 (GTE) */
uint32_t gte_mfc2(Cpu *c, int reg);
uint32_t gte_cfc2(Cpu *c, int reg);
void gte_mtc2(Cpu *c, int reg, uint32_t v);
void gte_ctc2(Cpu *c, int reg, uint32_t v);
void gte_cmd(Cpu *c, uint32_t op, uint32_t pc);  /* pc: address of the cop2 instruction */

/* Ring buffer of recently entered functions, printed when the runtime halts. */
#define PSX_TRACE_SIZE 64
extern uint32_t psx_trace_ring[PSX_TRACE_SIZE];
extern uint32_t psx_trace_pos;
#define ENTER(addr) (psx_trace_ring[psx_trace_pos++ & (PSX_TRACE_SIZE - 1)] = (addr))

#define TICK(c, n) ((c)->cycles += (n))
#define CHECK_EVENTS(c, pc) do { if ((c)->cycles >= (c)->next_event) psx_events(c, pc); } while (0)

#ifdef __cplusplus
}
#endif
