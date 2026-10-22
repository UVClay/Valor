# The presentation layer

Everything the player sees and hears besides the world and the characters, drawn natively from
the core's state: the HUD, the first-person weapon, the sky, the sounds and music, and the
gamepad. It changes nothing in the rules. Two parts:

| | |
|---|---|
| `engine/godot/present/` | `ValorAssets`, a GDExtension of its own (`libvalor_present`): the level's presentation data read from the extracted disc at run time |
| `engine/godot/project/present/` | The autoload `Present` (`present.gd`) and its parts: `hud.gd`, `view_model.gd`, `sky.gd`, `audio.gd` |

It attaches itself to the main scene (`main.gd`'s `game` and `cam`) once that has loaded a level,
reads the core's state after every tick (`ValorGame.get_state()`), and draws at the display rate.

## Building

```sh
./dev cmake -S engine/godot/present -B build/gdp/cmake -G Ninja
./dev cmake --build build/gdp/cmake -j8
```

The library lands in `engine/godot/project/bin/` next to the game's; `valor_present.gdextension`
registers it (run the project once with `--headless --import` after the first build).

## ValorAssets

| Method | |
|---|---|
| `load(data, mission, level, american)` | The BSD, the TAF (`1` American, `0` German soldiers' voices) and LEVEL.BIN |
| `page_image(tpage, clut)`, `region_image(...)` | A texture page through a CLUT (the HUD's pictures, the font, the moon) |
| `get_sound(bank, sound)`, `get_music(ambience_only)` | The TAF's VAB banks and the level's VB, SPU ADPCM decoded to 16-bit PCM at 22050 Hz (the music sample for sample as `tools/recon/snd_vb.py`) |
| `build_model_mesh(hash, anim, frame)` | A skinned model posed by the core's animation code (the first-person weapons) |
| `bone_point(hash, anim, frame, bone, offset)` | A point on a bone of that pose (the muzzle flash's place) |
| `level_bin(addr, len)`, `level_string(addr)` | LEVEL.BIN's static data by RAM address: the weapon table, the glyph tables, the stick curve, names |
| `stick_roles(rx, ry, lx, ly)` | Stick bytes through the game's own analog curve and a fresh card's configuration (the core's `role_axis`): the four roles as the core takes them |

## What it shows

| | Source | From the core's state |
|---|---|---|
| Health ring, compass, clip and reserve with the slot's ammo picture | hud.md | `health`, the view's yaw, `clip`, `reserve`, `slot`, `weapon` |
| Aim reticle (the bazooka's own shape; none for grenades and papers) | hud.md | `aiming`, `weapon` |
| Pickup messages in the game's font, fading in as `text_add_fade` does | hud.md, player.md | health and reserve changes |
| Fades: the level start's 15 black ticks, death's 65 ticks then black | hud.md | `tick`, `health` |
| First-person weapon at the frame the instance shows, turned by the body's angles against the view, drawn over the world in its own pass | animation.md, movement.md | `weapon_model`, `weapon_anim`, `weapon_frame`, `body_pitch`, `body_yaw` |
| Sky gradient (from the look pitch), 255 twinkling stars, the moon | formats.md, Sky | the view's pitch |
| Every sound: the engine's 23 effect voices (views::voices), placed as `sound_pan` places them (the scaled view matrix, the class's falloff, the near/far split) | audio.md | `get_engine_voices()` |
| Music, looping, from the level script's start (the ambience-only mix with `--ambience-only`) | audio.md, Music | `music` |
| Sparks, smoke and debris (tumbling quads), explosion fireballs and puffs (camera-facing, coloured by size), by the game's draw functions | combat.md (Drawing particles), objects.md, hud.md (Billboards) | `get_engine_sprites()` |
| Muzzle flash: the 31 x 31 sprite at VRAM (1008, 128), 15 units round the weapon's bone point, one of eight turns, the tick after a shot (checked against a recomp RAM dump) | hud.md | the fire |
| Zoom (the weapon hidden, the view narrowed) and the scope: the game's scope model (0x003C8662) under the HUD | hud.md, movement.md | `zoom`, `scoped` |
| Objectives (Tab), with the pause menu's box and tick glyphs | levels.md | `objectives` |
| Fades from the engine's fade entities | hud.md | `fade` |

Wide screens: the compass keeps to the left edge and the ammo to the right, the messages and the
reticle stay in the 4:3 middle. `--crosshair=SIZE` (default 4, 0 for none) adds a crosshair on the
aim line (y 131, not the screen's centre), hidden in aim mode.

## Input

The gamepad's buttons are the PS1's (A Cross, B Circle, X Square, Y Triangle, the shoulders and
triggers L1/R1/L2/R2, Back Select, Start, the sticks' clicks L3/R3, the d-pad). Its sticks go
through the game's own curve, as a DualShock's would: by default a round modern stick is taken
as is (the original's response; its diagonals fall short of full speed, since the game's curve
works per axis). `--square-stick` stretches the round range to the DualShock's square one first;
`--modern-sticks` swaps the sticks' x axes (the left one strafes, the right one turns; a fresh
card has left-x turn, right-x strafe). `main.gd` adds the pad to the keyboard's and mouse's input
(`Present.pad_input()`).

## Also drawn

- Impact slivers and explosion bursts (2D, the screen-space rules of 0x8006F7BC / 0x8006FBF0; the
  bursts' corners approximated), hit puffs and drops on soldiers and dogs (the drops' size not
  traced), the enemies' muzzle flashes (at the muzzle a new enemy bullet left from), the explosion
  light and scorches on the world (`world.gdshaderinc`).
- From the engine when it keeps them: the text list (level messages, pickups, ammo digits), the hit
  wedges, looping sounds (re-triggered voices start again).

## Level changes

The layer follows main.gd's level: when the level (mission, level) or main.gd's world mesh changes
(next_level, start_level from the front end), everything it built goes and it is built again from
the new level's files (`present.gd` `_attach`); `--test-reattach=TICK` does that once for a test.
On exit it frees what it made and removes its audio buses. One leak is left: a stream still
playing when the game quits (the music) keeps its playback, which Godot frees only on a later
audio mix (2 objects: AudioStreamWAV and its playback).

## Frame time (a 1:3 live run, the session's input, 2560 x 1440, `--perf`)

In the container (software OpenGL), so the frame itself (about 40 ms, the rasteriser) isn't a
GPU's; what the CPU spends is:

| Per | What | Average | Worst |
|---|---|---|---|
| tick | main.gd's tick: the engine, the actors' meshes | 1.7..2.4 ms | 25 ms (level streaming, first sights) |
| tick | re-skinning one character (main.gd rebuilds every skinned actor's mesh each tick; 3 at a time here) | 31 µs | 43 µs |
| tick | present/: audio (the voice table) 90 µs, effects 75, PVS 52, view model 36 (2.3 ms when it builds a new pose: cached after), get_state 15, HUD 12 | 0.3 ms | 2.3 ms |
| frame | present/: HUD draw 130 µs, sky draw 83 µs (in sky levels), the rest under 10 | 0.2 ms | 0.25 ms |
| frame | draws: 43..59 draw calls, about 17,000 triangles, 57..190 objects | | |

So the presentation costs about 0.3 ms a tick and 0.2 ms a frame. Fixed in this pass: the PVS
texture is uploaded only when the camera's leaf changes. Left for the main worker (engine/README,
Coordination): skinning on the GPU instead of a new mesh per actor per tick, cheap now (31 µs
each) but it grows with the actors on screen; the 25 ms spikes in main.gd's tick.

## What's left in presentation

- **The pause menu's objectives screen**: Tab shows the list; the game's own pause panels
  (`menu_boxes`) are the shell's (main worker).
- **Weapon brightness** in dark places: its flag (script variable 0x8A) is never set (docs).
- **Footsteps**: not documented (no footstep sound call was found in audio.md).
- **The burst streaks' corners** (`0x8006FBF0`) and **the drops' size** (`0x800729C0`):
  approximated, not traced.
- **The sky**: the stars' colour stepping (`stars_init`'s pointer arithmetic) is inferred; the
  `func_80089174` gradient mode (`g_params` 2 / 3) isn't drawn.
- **The flash and billboards' rotation** use one of eight turns at random, not the game's frame
  counter (`g_update_stamp`).
- **The death camera, the fades' colour ramp** (the game cuts; hud.md) and **the scope's zoom
  easing**: from the engine's state, as they come.
- **Without the engine** (a walk) the weapon is the level's first and plays from input; no sounds
  but the music.
- **The leak at exit** above.
- **Not presentation, noticed**: the engine's live replay of the session drifts from the recording
  within a few hundred ticks (main worker), so recomp side-by-sides need scripted runs.

`--test-press=MASK@TICK[:TICKS][,...]` presses PS1 buttons (0x40 fires, 0x20 switches, 0x2 aims),
`--test-turn=UNITS@TICK:TICKS` turns, for screenshots; `--show-objectives`; `--audio-log`, `--fx-log`
print what plays and what's drawn; `--perf` times the parts and prints them at exit.
