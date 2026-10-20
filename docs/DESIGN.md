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
  camera's screen extents. A hook widens those extents to the actual projection, with 10% extra.
- **Drawing past the edges.** In-between frames turn the view toward the next tick's, and mouse
  look turns it further ahead (Frame interpolation). So the game draws a "reach" beyond the
  view: at least 6° sideways and 4° up and down, and up to three ticks of the recent mouse turn
  (30° / 20°). Both the frustum and the world's polygon test reach that far; the test otherwise
  skips polygons off the screen (by screen outcodes), so a wider frustum alone draws nothing
  more. Polygons the GTE had to clamp aren't added. The frustum stops at 75° from the view's
  center (70° vertically): wider, the game culls the room in front of the camera at some angles
  and the frame shows no world. At wide FOVs that limits the reach (about 20° at 110°).
- **Near clipping.** The GTE can't project points behind the camera, and its divide saturates
  for points nearer than H/2. The world renderer (`0x80010B60`) only subdivides near polygons
  once and drops the ones reaching behind the camera. On the PS1 that gap was always outside the
  narrow view. A wider one shows it at the edges as holes in nearby walls, through which enemies
  could be seen.
  - The game side: a hook on the polygon test (`0x80011214`) lets such polygons through when they
    face the camera, checked in camera space. They are drawn whole: the game's subdivision can't
    place pieces behind the camera (the floor under the player went missing) and fills the
    primitive buffer with them (the weapon, drawn last, disappeared).
  - The OpenGL renderer side: it redraws every world triangle with a vertex that near from the
    recorded camera-space vertices. It clips the triangle at a near plane, projects it exactly
    and textures it perspective-correct (u/w, v/w and 1/w interpolated on screen; everything
    else keeps w = 1 and the PS1's affine mapping). Splitting it instead would add T-junctions.
    Vertices the GTE projected properly keep its integer position, so shared edges line up
    (not ones it clamped far off screen).
  - In-between frames clip these triangles again where their corners are then, since a turn can
    take a cut point behind the camera. Triangles that can cross the near plane in that time
    (closer than H, or more than 55° off the view's axis) are drawn the same way, with their
    affine texturing kept.

### Decoupled rendering

On by default (`--coupled` or `decoupled_rendering = false` falls back to frame interpolation,
below). Frame interpolation can only move what the game drew for its tick's camera, so a fast turn
or a wide view shows what it culled, clipped or never drew. Instead (`shadow.cpp`), every presented
frame runs the game's own drawing pass again:

- **The copy.** When a tick's drawing pass begins (0x800503D4), RAM, the scratchpad, the GTE and
  the CPU are saved. Each presented frame restores that copy, places the camera where it should be
  at that moment (the player's position lerped between the last two ticks, the view turned by the
  mouse look the game hasn't read yet) and rebuilds the view with the game's camera setup, then
  runs the pass. The game itself never sees the copy.
- **Devices.** The pass's ordering table is walked natively at its flip, and its GPU commands go
  to the OpenGL renderer only: GPU registers are stashed, VRAM is never written, and no events or
  interrupts are delivered while it runs. The game's own drawing of the tick is not shown.
- **Primitive pool.** The game stops drawing a frame once its 80000-byte primitive buffer is full.
  A wider or taller view than the original fills it (the near floor and the weapon are drawn
  last, and were cut off), so the copy gets both buffers' space.
- **The view.** The game builds its camera a tick behind the soldier's look, toward a point about
  200 units away in whole units, so its direction steps by up to 0.3 degrees each tick. The copy
  looks along the soldier's own yaw and pitch (`[[0x8009D654]+0x388]+0x184/+0x178`, 16.16 with
  4096 per turn), turned further by the mouse look taken since the copy and not yet taken, and
  builds the view toward a point far along it. What the game's camera adds (its own turns, the
  target's rounding) and the eye's position are blended from what was shown to the latest tick's
  over a tick, so they stay steady when ticks vary in length.
- **The weapon** is drawn with one transform made from the view: for a moved camera it would
  slide across the screen. The copy draws it with the transform from the tick's own drawing pass,
  blended between ticks, so its sway and bob are smooth too.
- **Faults.** A copy that isn't mid-mission isn't drawn, and an error inside one only drops that
  copy (the tick's own frame is shown): the game itself never runs on it.
- **Menus** are not in a level: they are drawn at 4:3, unwidened.
- Moving objects (enemies, doors) still move at the tick rate.

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
- **Re-projection model.** A point moves by the difference between its exact projection now and
  where it was, the same way a near-clipped triangle's vertex moves, so points they share stay
  together. Where the GTE's divide saturated (z < h/2), the difference is taken with the GTE's
  own projection (saturation and clamping) instead.
- **Unprojected points.** Points the CPU computed itself (clipped edges, subdivided polygons)
  are placed in camera space from their primitive's projected vertices: depth is interpolated
  as 1/z, which is linear on screen. They then move like a projected point. A depth far outside
  the vertices' (extrapolated well past them) isn't trusted: the point moves with them on average.
  Without any projected vertex, a point in the world moves as a direction, which the camera's turns
  move exactly; next to the weapon's vertices it stays put with them. Each screen point moves the
  same way in every world primitive that uses it, so neighbours don't crack apart.
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
  catches up, the view doesn't move, so nothing jumps. Pitch stays inside the game's limit. The
  turn is applied to every point in camera space after the interpolated motion, the same way for
  projected, near-clipped and unprojected points; anything else would let shared points drift
  apart as the mouse moves.
  Each tick also checks that the camera really turned by the look the game took. In aim mode the
  mouse moves the game's reticle, and cutscenes ignore it, so there the early look is off. The
  check allows for the game's pitch steps (about 0.3°) and must fail twice in a row: switching
  the look off wrongly snaps the view back by everything shown ahead.
  The view never turns farther from the tick's camera than the game drew (the reach), the
  interpolated turn and the look together. The limit leaves room for the rest of the tick's
  turn, so a steady turn passes into the next tick's view without a jump; only a turn faster
  than the reach per tick (about 600°/s at 110°) still steps, by the excess.
- **Presentation.** Frames are presented on their own clock at the display's refresh rate (or
  `--frame-rate`). Emulation still advances one vblank at a time, in between presents.

## Verification

Replay recorded controller input in an emulator and in the recompiled build, compare RAM and GPU
command streams per frame (M4).
