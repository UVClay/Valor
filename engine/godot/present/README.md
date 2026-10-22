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
| Effects on 23 voices, first free, no stealing: fire, draw, reload, pickups, the hurt grunt, shell casings | audio.md | clip, reserve, weapon and health changes |
| Music, looping (the ambience-only mix with `--ambience-only`) | audio.md, Music | |

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

## Stubbed

Waiting for the core to report them (each is a state or event the original keeps that
`get_state` doesn't give yet):

- **Hit wedges**: the direction of each hit (`hit_indicator`'s timers).
- **Level messages** (`show_text`) and the rest of the text list; `--test-message=TEXT` shows one.
- **Other entities' sounds**: enemy shots, impacts, explosions, voices, script sounds, so there is
  no positional (`sound_pan`) audio yet; footsteps aren't documented.
- **The music's start** by the level script (`level_set(1, 1)`): it starts with the level.
- **The scope** and the zoom (the weapon hidden while zoomed), the **crosshair's movement** (the
  reticle sits at its rest point), the **muzzle flash**, the weapon model's brightness in dark
  places.
- **Without the engine** (a walk) the weapon is the level's first and plays its animations from
  input (left mouse fire, E reload).
- **PVS culling**: the world is one mesh (main.gd's), drawn whole.

`--test-press=MASK@TICK` presses PS1 buttons for two ticks (0x40 fires), for screenshots.
