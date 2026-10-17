// Control flow glue for recompiled code: function lookup, exceptions, COP0, native overrides.
#include <algorithm>
#include <csetjmp>
#include <cstdarg>
#include <cstring>
#include <vector>

#include "runtime.h"

extern "C" {
int psx_cache_isolated = 0;
}

namespace psx {

Config config;
Cop0 cop0;

void log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void fatal(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    throw Halt(buf);
}

// ---------------------------------------------------------------------------------------------
// Function lookup

static RecompFn lookup(const RecompModule *m, uint32_t phys) {
    const RecompEntry *end = m->funcs + m->count;
    const RecompEntry *it = std::lower_bound(m->funcs, end, phys,
                                             [](const RecompEntry &e, uint32_t a) { return e.addr < a; });
    return it != end && it->addr == phys ? it->fn : nullptr;
}

// Which overlay is resident, judged by its id word in RAM.
static const GameOverlay *resident_overlay(const RecompProject &p, uint32_t phys) {
    for (uint32_t i = 0; i < p.overlay_count; i++) {
        const GameOverlay &ov = p.overlays[i];
        uint32_t base = ov.base & 0x1FFFFFFF;
        if (phys < base || phys >= base + ov.size) continue;
        uint32_t id;
        memcpy(&id, psx_ram + (base & (PSX_RAM_SIZE - 1)), 4);
        if (id == ov.id) return &ov;
    }
    return nullptr;
}

static RecompFn lookup_project(const RecompProject &p, uint32_t phys) {
    if (const GameOverlay *ov = resident_overlay(p, phys))
        if (RecompFn fn = lookup(ov->module, phys)) return fn;
    for (uint32_t i = 0; i < p.resident_count; i++)
        if (RecompFn fn = lookup(p.resident[i], phys)) return fn;
    return nullptr;
}

RecompFn find_function(uint32_t addr) {
    uint32_t phys = addr & 0x1FFFFFFF;
    if (RecompFn fn = lookup_project(game_project, phys)) return fn;
    return lookup_project(bios_project, phys);
}

// Code the BIOS or a game writes into RAM at run time (vector stubs at 0x80/0xA0/0xB0/0xC0)
// can't be recompiled ahead of time. These stubs are a handful of instructions that load an
// address and jump to it, so follow them here.
static bool follow_stub(Cpu *c, uint32_t addr) {
    uint32_t pc = addr;
    for (int n = 0; n < 16; n++) {
        uint32_t w = psx_r32(c, pc);
        uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, funct = w & 63;
        int32_t imm = (int16_t)w;
        auto set = [&](uint32_t r, uint32_t v) { if (r) c->r[r] = v; };
        if (w == 0) {                                   // nop
        } else if (op == 0x0F) {                        // lui
            set(rt, (w & 0xFFFF) << 16);
        } else if (op == 0x09) {                        // addiu
            set(rt, c->r[rs] + imm);
        } else if (op == 0x0D) {                        // ori
            set(rt, c->r[rs] | (w & 0xFFFF));
        } else if (op == 0x02 || (op == 0 && funct == 0x08)) {  // j / jr
            uint32_t target = op == 0x02 ? ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2) : c->r[rs];
            uint32_t d = psx_r32(c, pc + 4);           // delay slot: same subset
            uint32_t dop = d >> 26, drs = (d >> 21) & 31, drt = (d >> 16) & 31;
            if (dop == 0x09) set(drt, c->r[drs] + (int16_t)d);
            else if (dop == 0x0D) set(drt, c->r[drs] | (d & 0xFFFF));
            else if (dop == 0x0F) set(drt, (d & 0xFFFF) << 16);
            else if (d != 0) fatal("stub at %08X: unsupported delay slot %08X", addr, d);
            if (op == 0 && rs == 31) return true;  // jr $ra: the stub is a function that returns
            psx_call(c, target);
            return true;
        } else {
            return false;
        }
        pc += 4;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Exceptions
//
// Recompiled code can't jump to an arbitrary pc, so an exception (interrupt or syscall) is a
// nested call: set up COP0 like the CPU would, call the vector, and when the handler executes
// its `jr k0; rfe` we longjmp back here and resume the recompiled code where it was.

namespace {
struct ExceptionFrame {
    jmp_buf env;
    uint32_t epc;
    uint32_t target;
};
std::vector<ExceptionFrame *> exception_stack;
}  // namespace

static void take_exception(Cpu *c, uint32_t epc, uint32_t code) {
    cop0.epc = epc;
    cop0.cause = (cop0.cause & ~0x8000007Cu) | (code << 2);
    cop0.sr = (cop0.sr & ~0x3Fu) | ((cop0.sr << 2) & 0x3Fu);  // push KU/IE, disable interrupts
    const uint32_t vector = (cop0.sr & (1u << 22)) ? 0xBFC00180u : 0x80000080u;

    static_assert(std::is_trivially_destructible_v<ExceptionFrame>);
    ExceptionFrame frame;
    frame.epc = epc;
    exception_stack.push_back(&frame);
    if (setjmp(frame.env) == 0) {
        psx_call(c, *(volatile const uint32_t *)&vector);
        fatal("exception handler for %08X returned without rfe", epc);
    }
    exception_stack.pop_back();
    // Interrupt: resume at epc (or epc+4 when the BIOS skips a GTE op it interrupted).
    // Syscall: the BIOS returns to epc+4.
    if (frame.target != epc && frame.target != epc + 4)
        fatal("exception at %08X returned to %08X (context switch?)", epc, frame.target);
}

}  // namespace psx

using namespace psx;

extern "C" {

void psx_call(Cpu *c, uint32_t addr) {
    if (RecompFn fn = find_function(addr)) return fn(c);
    if ((addr & 0x1FFFFFFF) < 0x800000 && follow_stub(c, addr)) return;  // code written to RAM at run time
    fatal("call to unknown function %08X (ra=%08X)", addr, c->r[31]);
}

void psx_bios_call(Cpu *c, uint32_t vector) { psx_call(c, vector); }

void psx_unhandled_jump(Cpu *c, uint32_t pc, uint32_t target) {
    (void)c;
    fatal("jump table at %08X: unexpected target %08X", pc, target);
}

void psx_exception_return(Cpu *c, uint32_t target) {
    (void)c;
    if (exception_stack.empty()) fatal("rfe return to %08X outside an exception", target);
    ExceptionFrame *f = exception_stack.back();
    f->target = target;
    longjmp(f->env, 1);
}

void psx_syscall(Cpu *c, uint32_t pc) { take_exception(c, pc, 8); }

void psx_break(Cpu *c, uint32_t pc, uint32_t code) {
    fatal("break %X at %08X (ra=%08X)", code, pc, c->r[31]);
}

uint32_t psx_mfc0(Cpu *c, int reg) {
    (void)c;
    switch (reg) {
    case 12: return cop0.sr;
    case 13: return cop0.cause;
    case 14: return cop0.epc;
    default: return 0;
    }
}

void psx_mtc0(Cpu *c, int reg, uint32_t v) {
    (void)c;
    switch (reg) {
    case 12:
        cop0.sr = v;
        psx_cache_isolated = (v >> 16) & 1;
        c->next_event = c->cycles;  // re-evaluate pending interrupts
        break;
    case 13: cop0.cause = (cop0.cause & ~0x300u) | (v & 0x300u); break;
    default: break;
    }
}

void psx_rfe(Cpu *c) {
    cop0.sr = (cop0.sr & ~0xFu) | ((cop0.sr >> 2) & 0xFu);
    c->next_event = c->cycles;
}

// Native overrides (see bios/openbios.toml).

void native_flush_cache(Cpu *c) { (void)c; }

// psxlongjmp(jmp_buf, value): restore callee-saved registers and continue at the saved $ra.
// Kernel users (HookEntryInt / SetCustomExitFromException) point $ra at the start of a
// handler, so continuing there is a fresh call. The handler never returns: it ends with
// ReturnFromException.
void native_longjmp(Cpu *c) {
    uint32_t buf = c->r[4];
    static const int regs[] = {31, 29, 30, 16, 17, 18, 19, 20, 21, 22, 23, 28};
    for (int i = 0; i < 12; i++) c->r[regs[i]] = psx_r32(c, buf + 4 * i);
    c->r[2] = c->r[5];
    uint32_t target = c->r[31];
    if (!find_function(target)) fatal("longjmp into the middle of a function (%08X)", target);
    psx_call(c, target);
    fatal("longjmp target %08X returned", target);
}

}  // extern "C"

namespace psx {

void take_interrupt(Cpu *c, uint32_t pc) { take_exception(c, pc, 0); }

}  // namespace psx
