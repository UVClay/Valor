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

[OpenBIOS](https://github.com/pcsx-redux/nugget) (MIT) is built from source and recompiled like the
game, so the kernel (events, interrupt handler chains, file I/O, memory card patches) runs as real
BIOS code on the emulated hardware and the console boots from its reset vector. A few BIOS routines
are replaced natively (`flushCache`, `psxlongjmp`), and code stubs the BIOS writes into low RAM at run
time are followed by a tiny interpreter.

### Generated code

- CPU state in a struct: 32 GPRs, hi/lo, COP0 status/cause/EPC, GTE registers.
- 2 MB RAM + 1 KB scratchpad as byte arrays; memory access helpers translate KUSEG/KSEG0/KSEG1
  addresses and route `0x1F80xxxx` to I/O handlers.
- One C function per MIPS function; branches become `goto`, delay slots are emitted in order.
- `jal` → direct C call. `jr $ra` → return. Jump tables are resolved statically.
- `jalr` / function pointers / C++ virtual calls → `psx_call(addr)`: lookup in the resident
  overlay (identified by its id word in RAM), the exe, then the BIOS.
- R3000 load delay slots: emitted exactly (the instruction after a load sees the old value) when
  that instruction reads the loaded register; otherwise loads are direct.
- Every `jr $ra` checks it still returns to its caller; if not, the code switched context
  (inline longjmp) and execution continues at `$ra`. Return addresses of `setjmp` calls are
  entry points of their own, since a longjmp can resume there.
- COP2 (GTE) instructions → calls into the runtime GTE implementation.

### Overlays

Each overlay is its own generated module with its own function table. Loading an overlay copies
its image into emulated RAM (for its data) and selects its function table for `call_indirect`.

### Interrupts and timing

The PS1 delivers VBlank, GPU, CD, DMA, timer and SPU interrupts asynchronously. Recompiled code
checks for pending events at function entry and backward branches; an exception is a nested call
into the recompiled BIOS vector, and the handler's `rfe` longjmps back to the interrupted code.
Time is counted in instructions at 2 cycles each (PCSX's default), which sets the game's speed.

### Hardware

Runtime devices: interrupt controller, timers, DMA, CD-ROM (reads the BIN directly, XA-ADPCM),
GPU (software rasterizer reproducing affine texturing, dithering, semi-transparency, mask bit),
MDEC, GTE, SPU (Mednafen's, vendored from Beetle PSX), controller port.

## Verification

Replay recorded controller input in an emulator and in the recompiled build, compare RAM and GPU
command streams per frame (M4).
