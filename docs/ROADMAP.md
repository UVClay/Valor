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
- [ ] **Lamp pop-in in widescreen**: the wall lantern at the start of mission 1 level 2 vanishes
      near the left edge in 21:9 while its bracket stays. Ruled out: the world's frustum tests
      (0x8008E5B0 and the tree walk around 0x8008BAD4 project p-vertices through the
      widescreen-scaled GTE; disabling their side tests changes nothing). It's culled elsewhere,
      probably by the object or light code.
- [x] **Widescreen HUD**: in 3D frames, 2D primitives that nobody projected (and aren't
      full-width) keep their 4:3 proportions. The compass and ammo count move to the screen's
      corners; menus and messages stay centered (Interface tab).
- [x] **Strafing**: the movement code adds forward and strafe as vectors. The loss of speed came from
      the game's per-axis stick curve meeting round-gated modern sticks. Round input is now mapped
      to the DualShock's square range (Controls → "Square stick range", see RECON.md).
- [x] **High frame rate**: the game logic is a fixed 30 Hz tick, so the last frame is redrawn with
      the camera and moving objects (enemy limbs, doors) interpolated toward the next tick, at the
      display's refresh rate (DESIGN.md, Frame interpolation). No added latency.
- [x] Crosshair (Interface tab; size slider)
- [ ] Mouse and keyboard controls
- [ ] Saves decoupled from memory cards
- [ ] Optional sub-pixel vertex precision (PGXP-style) to remove the vertex wobble at high resolution

## Later

- Local split-screen multiplayer (second controller in port 2)
- Medal of Honor: Underground
