# Roadmap

## Done: plays like the original

Boots from the disc image through recompiled OpenBIOS; GPU, SPU, CD-ROM, MDEC, GTE, DMA, timers,
DualShock and memory card emulated. Validated with random-input sweeps of every single-player level
and full playthroughs of two missions.

## PC quality of life

- [x] **Unrestricted resolution**: OpenGL renderer replaying GPU commands at any scale; the software
      rasterizer keeps PS1 VRAM authoritative underneath
- [x] **Debug overlay** with levels: off / FPS / extended (frame times, pacing, emulation stats)
- [x] **FOV / widescreen**: `--aspect`, `--fov`; GTE projection scaling, 2D screens stay 4:3
- [ ] Near clipping for models (enemies, props): only the world's polygon test is hooked, so a
      model polygon reaching behind the camera is still dropped (an enemy touching the camera at
      the screen's edge). The models are drawn by six RTPT routines (0x8006E058, 0x8006F7BC,
      0x80072310, 0x800732F8, 0x8008080C, 0x80081CDC)
- [x] **Wide views without holes**: the game's frustum is widened to the actual view, and world
      polygons reaching behind the camera are clipped at a near plane instead of dropped. This
      fixes walls vanishing at the screen edges at wide FOVs (enemies visible through them) and
      the wall lantern pop-in in mission 1 level 2 (DESIGN.md, Wide views).
- [x] **Widescreen HUD**: in 3D frames, 2D primitives that nobody projected (and aren't
      full-width) keep their 4:3 proportions. The compass and ammo count move to the screen's
      corners; menus and messages stay centered (Interface tab).
- [x] **Strafing**: the movement code adds forward and strafe as vectors. The loss of speed came from
      the game's per-axis stick curve meeting round-gated modern sticks. Round input is now mapped
      to the DualShock's square range (Controls → "Square stick range", see RECON.md).
- [x] **High frame rate**: the game logic is a fixed 30 Hz tick; every presented frame runs the
      game's drawing pass again on a copy of the machine, with the camera placed for that moment
      (DESIGN.md, Decoupled rendering). No added latency.
- [ ] Moving objects (enemies, doors) in decoupled frames still move at 30 Hz: blend their
      transforms between ticks in the copy's projections.
- [ ] Pickup messages ("ATTACHE CASE ACQUIRED") garble for a few frames in decoupled rendering.
- [x] Crosshair (Interface tab; size slider). Hidden in aim mode and on the pause screen
- [x] **Mouse and keyboard controls**: mouse look and movement go straight into the game's
      control roles (a recompiler hook on the role axis function), and keys press the game's
      actions through its own button table. Bindings, sensitivity and invert are in F1 → Controls.
      The mouse turns the view at the display's frame rate instead of the game's 30 Hz.
- [x] Fast loading option (CD data reads 8x faster, XA streams untouched), pause in the background,
      F12 screenshots
- [x] Controller options: modern stick aiming (small deadzone, smooth curve, set turn speeds) and
      gyro aiming, both off by default
- [x] **Saves decoupled from memory cards**: saves are single-save files (`.mcs`) in `saves/card1/`.
      The game sees a card assembled from them; after it writes, the card is split back into them,
      and deleted saves go to `deleted/`. The old `memcard1.mcd` is imported once and left untouched
- [x] Optional bilinear texture filtering for 3D (transparent texels carry no weight)
- [x] Optional depth buffer (Video tab, off by default): world triangles are depth-tested by their
      recorded depths instead of relying on the game's sorting; sky, weapon and HUD keep its order
- [x] Optional sub-pixel vertex precision (PGXP-style, Video tab, off by default): projected vertices are
      drawn where they really project, from the camera-space positions recorded for interpolation

## Known issues (testing tools)

- `--warp 1:3` starts the sewer level with only a Garand and no ammunition: the warp skips
  whatever sets up the loadout when the level is reached normally. Only matters for testing.

## Debugging

- [x] Debug mode: every session recorded, named after its mission, with its log and a RAM dump
      on a crash; F9 marks (README, Recording a play session).
- [ ] Faster memory card loads: the card's protocol is emulated byte by byte at the real speed,
      so reading saves is slow. Answer the BIOS card functions natively, or speed up the SIO
      timing, as fast loading does for the CD.

## Later

- Local split-screen multiplayer (second controller in port 2)
- Medal of Honor: Underground
