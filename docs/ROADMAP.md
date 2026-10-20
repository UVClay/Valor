# Roadmap

## Done: plays like the original

Boots from the disc image through recompiled OpenBIOS; GPU, SPU, CD-ROM, MDEC, GTE, DMA, timers,
DualShock and memory card emulated. Validated with random-input sweeps of every single-player level
and full playthroughs of two missions.

## Direction (2026-10-20)

Presentation is frozen: what exists stays, with crash and regression fixes only. The work goes
into understanding the game's mechanics, system by system, written up in `docs/re/` to be
reimplemented from (an eventual engine with identical mechanics and presentation rebuilt
natively from game state). The recomp stays as the reference: recordings replayed through it
check every finding.

1. [x] **Player state**: inventory, weapons, health, movement, pickups, carry-over between
   levels, cheat flags (docs/re/player.md)
2. [x] **Entities**: the struct, lists and handles, the tick, movement, world and entity
   collisions, sphere hitboxes (docs/re/entities.md)
3. [x] **Combat**: firing, bullets, hit tests, damage (flat per weapon, no headshot bonus), the
   player being hit, statistics (docs/re/combat.md)
4. [ ] **Enemies**: done: the script VM (all 74 opcodes) and a disassembler, variables and
   every sensor, spawning from level records, firing and aim error, the brain (seek, flee,
   paths, walking with wall avoidance, points and cover nodes). Left: reading the soldier's
   scripts into plain behaviour, how aim follows the target (docs/re/enemies.md)
5. [ ] **Levels**: done: files, the level header, records and how each kind spawns, models and
   entity types, the level script VM fully (with a disassembler that folds scripts into
   statements), objectives, the rating, campaign slots and career totals. Left: what record
   kinds 0, 1, 5 are, the collision sections, the TAF files (docs/re/levels.md)

Tools: `tools/recon/fn.py c ADDR` decompiles with m2c (clone it into `build/m2c`).

Waiting for the game state they need (presentation, later): a separate HUD layer and then
native HUD elements, a native sniper scope, a PC-style aim mode (the view keeps turning, the
game's aim follows it), moving objects interpolated between ticks.

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

## Main menu redesign

- [ ] **Single-segment mode**: pick any mission and level at will, and keep stats per level:
      the stars earned, enemies killed out of the total (Doom/Quake-style 100% runs), and the
      rest of the game's end-of-level results.

## Known issues (testing tools)

- [x] `--warp` to a level after a mission's first started without ammunition: such a level
  starts with what the previous one ended with (docs/re/player.md, Carry-over). The warp now
  gives every weapon four clips.

## Debugging

- [x] Debug mode: every session recorded, named after its mission, with its log and a RAM dump
      on a crash; Tab marks (README, Recording a play session).
- [ ] Faster memory card loads: the card's protocol is emulated byte by byte at the real speed,
      so reading saves is slow. Answer the BIOS card functions natively, or speed up the SIO
      timing, as fast loading does for the CD.

## Later

- Local split-screen multiplayer (second controller in port 2)
- Medal of Honor: Underground
