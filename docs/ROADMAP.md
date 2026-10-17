# Roadmap

## Done: plays like the original

Boots from the disc image through recompiled OpenBIOS; GPU, SPU, CD-ROM, MDEC, GTE, DMA, timers,
DualShock and memory card emulated. Validated with random-input sweeps of every single-player level
and full playthroughs of two missions.

## PC quality of life

- [ ] **Unrestricted resolution**: OpenGL renderer replaying GPU commands at any scale; the software
      rasterizer keeps PS1 VRAM authoritative underneath
- [ ] **Debug overlay** with levels: off / FPS / extended (frame times, pacing, emulation stats)
- [ ] **FOV / widescreen**: GTE projection scaling, game culling widened to match, HUD kept 4:3
- [ ] **Strafing**: in the original, strafing all but cancels forward movement; combine forward and
      strafe input like modern shooters (needs the player movement code decompiled)
- [ ] High frame rate (interpolated rendering on top of the original simulation tick)
- [ ] Mouse and keyboard controls
- [ ] Saves decoupled from memory cards
- [ ] Optional sub-pixel vertex precision (PGXP-style) to remove the vertex wobble at high resolution

## Later

- Local split-screen multiplayer (second controller in port 2)
- Medal of Honor: Underground
