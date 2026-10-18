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
- [x] **High frame rate**: the game logic is a fixed 30 Hz tick, so the last frame is redrawn with
      the camera and moving objects (enemy limbs, doors) interpolated toward the next tick, at the
      display's refresh rate (DESIGN.md, Frame interpolation). No added latency.
- [x] Crosshair (Interface tab; size slider). Hidden in aim mode and on the pause screen
- [x] **Mouse and keyboard controls**: mouse look and movement go straight into the game's
      control roles (a recompiler hook on the role axis function), and keys press the game's
      actions through its own button table. Bindings, sensitivity and invert are in F1 → Controls.
      The mouse turns the view at the display's frame rate instead of the game's 30 Hz.
- [ ] Saves decoupled from memory cards
- [x] Optional sub-pixel vertex precision (PGXP-style, Video tab, off by default): projected vertices are
      drawn where they really project, from the camera-space positions recorded for interpolation

## Later

- Local split-screen multiplayer (second controller in port 2)
- Medal of Honor: Underground
