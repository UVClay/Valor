# Recompilation design

Target 1: the game runs natively on x86_64 and plays exactly like it does in an accurate
emulator, with a real PlayStation controller. Modern features come later.

## Layers

```
 recompiled game code (exe + 3 overlays)     ← generated C, one function per MIPS function
 recompiled PsyQ libraries (part of the exe) ← same generator, unmodified
 ─────────────────────────────────────────── memory-mapped I/O + BIOS calls
 runtime: HLE BIOS, interrupts/events, DMA,  ← hand-written C++
          GPU, SPU, CD-ROM, MDEC, pads, timers, GTE
 platform: SDL3 (window, audio, gamepad), file I/O
```

### Why recompile the libraries instead of replacing them

The PsyQ libraries are recompiled like everything else, and the runtime emulates the hardware
they talk to (GPU command port, SPU registers, CD controller, DMA, timers). The alternative,
replacing libgpu/libspu/libcd functions with native versions, means re-implementing hundreds of
functions with subtly different behavior. Emulating the hardware under unmodified libraries gives
the same result as an emulator would, so it's the most faithful option and the least code.

The GPU command stream (GP0 packets from `DrawOTag`) is also the natural interception point for
the high-resolution renderer later.

Native replacements are still used where they help: busy-wait loops (`VSync`, `DrawSync`,
CD polling) so we don't spin, and hot libc routines.

### BIOS

HLE: BIOS calls (`A0`/`B0`/`C0` tables via `syscall`/`jal 0xA0` etc.) are implemented natively,
including events, callbacks and the exception handler chain, as in emulator HLE BIOSes.

### Generated code

- CPU state in a struct: 32 GPRs, hi/lo, COP0 status/cause/EPC, GTE registers.
- 2 MB RAM + 1 KB scratchpad as byte arrays; memory access helpers translate KUSEG/KSEG0/KSEG1
  addresses and route `0x1F80xxxx` to I/O handlers.
- One C function per MIPS function; branches become `goto`, delay slots are emitted in order.
- `jal` → direct C call. `jr $ra` → return. Jump tables are resolved statically.
- `jalr` / function pointers / C++ virtual calls → `call_indirect(addr)`: lookup in the exe's
  table, then in the table of the currently loaded overlay.
- R3000 load delay slots: emitted exactly (the instruction after a load sees the old value) when
  that instruction reads the loaded register; otherwise loads are direct.
- COP2 (GTE) instructions → calls into the runtime GTE implementation.

### Overlays

Each overlay is its own generated module with its own function table. Loading an overlay copies
its image into emulated RAM (for its data) and selects its function table for `call_indirect`.

### Interrupts

The PS1 delivers VBlank, GPU, CD, DMA, timer and SPU interrupts asynchronously. Recompiled code
checks for pending interrupts at safe points (function entry and backward branches) and runs the
BIOS exception path there. Timing is driven by a cycle estimate per basic block.

## Verification

Replay recorded controller input in an emulator and in the recompiled build, compare RAM and GPU
command streams per frame (M4).
