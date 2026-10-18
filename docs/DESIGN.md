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
- Hooks (`[hooks.<module>]` in the game config): the function's body is generated as
  `<fn>_body` and its name calls a native `hook(c, body)`, which runs the original and can
  adjust arguments or results. PC features use them to change the game's behavior at a precise
  point. Example: mouse and keyboard input is added to the control role values
  (`runtime/src/game.cpp`).

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

### Wide views: culling and near clipping

The game was built for a 4:3 view of about 65° (`gte_set_projection` scales projections for
widescreen and FOV). Two things it relies on break with a wider view:

- **Frustum.** `0x8008D0CC` builds the frustum planes used to cull rooms and objects from the
  camera's screen extents. A hook widens those extents to the actual projection, with 10% extra
  for interpolated frames turning toward the next tick.
- **Near clipping.** The GTE can't project points behind the camera, and its divide saturates
  for points nearer than H/2. The world renderer (`0x80010B60`) only subdivides near polygons
  once and drops the ones reaching behind the camera. On the PS1 that gap was always outside the
  narrow view. A wider one shows it at the edges as holes in nearby walls, through which enemies
  could be seen.
  - The game side: a hook on the polygon test (`0x80011214`) lets such polygons through when they
    face the camera, checked in camera space.
  - The OpenGL renderer side: it redraws every world triangle with a vertex that near from the
    recorded camera-space vertices. It clips the triangle at a near plane, splits it until each
    piece's depth range is small (texture mapping stays affine, so this keeps it close to
    perspective) and projects it exactly. Vertices the GTE projected properly keep its integer
    position, so shared edges line up.

### Frame interpolation

The game logic runs at a fixed 30 Hz (RECON.md, Frame timing). For higher frame rates
(`interp.cpp`, `gpu_gl.cpp`):

- **Recording projections.** Every GTE projection is recorded with its camera-space position,
  keyed by the screen coordinate it produced. The per-tick camera is the transform that the most
  world vertices used.
- **Recording draws.** The OpenGL renderer records each tick's drawing as a list. It matches every
  vertex to its projection by that coordinate. When different points share a coordinate, it picks
  the candidate projected nearest, in order, to the rest of the primitive.
- **Re-projecting.** Frames between vblanks redraw the displayed list with every matched vertex
  re-projected through a camera between this tick's and the next one's: slerped rotation, lerped
  position. The game projects tick N+1 before tick N is displayed, so the next camera is known in
  time and nothing is delayed.
- **Moving objects.** Enemies are drawn with one GTE transform per limb. Each transform is matched
  to the next tick's transform from the same code, with the same mesh (a hash of its first model
  vertices), nearest in the world. Its world transform is then blended, and those vertices are
  re-projected from their model-space input.
- **What stays put.** The weapon (projected with another H), the HUD (never projected) and the
  compass (a HUD projection) are left in place. Camera cuts larger than 90° or 1500 units are not
  interpolated.
- **Re-projection model.** Re-projection models the GTE exactly: the divide saturates for
  z < h/2, IR saturates, and SXY is clamped. Points near the camera therefore move the way the
  hardware would draw them.
- **Unprojected points.** Points the CPU computed itself (clipped edges) move with their
  primitive's projected vertices, the same way in every primitive that shares them, so neighbours
  don't crack apart.
- **Widescreen HUD.** The same matching also finds the HUD. In a 3D frame, a primitive with no
  projected vertex that isn't full-width is HUD if it is a sprite or line, or a polygon from
  outside the game's primitive pool (where the frame's world packets are). It is drawn squeezed
  back to 4:3 proportions. Elements are primitives touching on screen. A primitive takes the
  anchor of the previous frame's element it falls in; packet addresses shift as the number of
  world primitives changes, so anchors are not keyed by packet. Elements in a top corner near the
  side (compass, ammo) are anchored to that screen edge; the rest are anchored to the center.
- **Mouse look.** The game applies the mouse turn it reads one tick later. Interpolating it would
  lag behind the mouse by up to two ticks. Instead, each tick records the turn the game took
  (`kbm.cpp`), and the displayed camera is rotated further by three parts: the rest of the current
  tick's turn, all of the next tick's turn, and motion the game hasn't read yet. When the game
  catches up, the view doesn't move, so nothing jumps. Pitch stays inside the game's limit.
- **Presentation.** Frames are presented on their own clock at the display's refresh rate (or
  `--frame-rate`). Emulation still advances one vblank at a time, in between presents.

## Verification

Replay recorded controller input in an emulator and in the recompiled build, compare RAM and GPU
command streams per frame (M4).
