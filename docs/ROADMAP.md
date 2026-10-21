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
3. [x] **Combat**: firing, bullets, hit tests, damage (flat per weapon; a head hit kills unless
   one of two helmet models is knocked off), the player being hit, statistics (docs/re/combat.md)
4. [x] **Enemies**: the script VM (all 74 opcodes) with a disassembler, variables and every
   sensor, spawning, firing and aim error, the brain, sight and hearing, the soldier's scripts
   read into behaviour, the dog and the mounted gun, every level's script set (docs/re/enemies.md,
   soldier.md, enemy_types.md)
5. [x] **Levels**: files and header, records, their state and properties, the level script VM and
   every message (with a disassembler checked on all 24 levels), triggers and zones, objectives,
   the rating, campaign slots, world collision, a catalogue of every level (docs/re/levels.md,
   messages.md, catalogue.md)
6. [x] **Animation**: skeletons, frames and playback, blending, root motion, hit spheres from the
   pose, every soldier and first-person animation (docs/re/animation.md)
7. [x] **Player movement in full**: speeds, crouch, jump, steps and falls, turning, aim mode, the
   camera, use, death (docs/re/movement.md)
8. [x] **World objects**: doors, switches, destructibles, the mounted gun, explosions, grenades and
   rockets, the random number generator and the order of a tick (docs/re/objects.md)
9. [x] **Sound**: the voice driver, banks, what plays when, 3D falloff and panning, music
   (docs/re/audio.md)
10. [x] **Asset formats**: every file type on the disc, what a new engine needs to load a level
    (docs/re/formats.md)
11. [x] **Front end and HUD**: the campaign flow, saves, options, passwords and cheats, death and
    restart, the HUD's state (docs/re/shell.md, hud.md)

docs/re/ENGINE.md ties them together for a rebuild. Five systems are proven complete by
reimplementing them from the documents alone and matching the original tick by tick on the
recorded session (tools/sim/): the player's movement (every tick exact), the enemies' behaviour
scripts and their movement (exact but for unwatched inputs), combat (hits and damage), and the
level logic (every frame but two). What's left is listed in each document's
"Open" section: mostly confirming script-read behaviour in play and a few unread fields.

**The engine** (`engine/`, engine/README.md): Godot 4.7 hosts an engine-agnostic C++ core with
the original's arithmetic, through a GDExtension. Done so far:
- the player from the recorded pad (the control roles, aim, ladders, crouch, the flinch), exact
  on every tick of the session and of 91 coverage segments in 19 levels;
- the level's collision and chunk streaming;
- the entity list, handles and the frame around the tick;
- models and poses, matching the original's RAM;
- a level drawn natively from the disc files, with its records' models posed by the core, that
  you can walk with mouse and keyboard.

Next, following `tools/sim`: the level logic, enemies, combat.

Out of scope for now: the two-player mode (LEVEL2P.BIN), video playback (standard MDEC).

Checking against recordings: replays now hold with or without a display (the widescreen
projection and the near-polygon hook follow the recorded settings, not whether OpenGL is up).
The 2026-10-19 session differed from vblank 61980 (10 s into 2:3, a fast mouse flick): it was
recorded before drawing copies were kept off the sound chip (9629c58), and its copies, turned
ahead by the mouse look, set the voices' volumes for their own camera, which the game reads back.
Recordings now say which: version 1 tapes replay with copies that use the devices as they did
then (needs OpenGL, which draws the copies), and the session replays exactly to its end; new
recordings are version 2. Every version 1 tape on hand predates 9629c58;
one that doesn't replays with `VALOR_REPLAY_OLD_COPIES=0`.

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
