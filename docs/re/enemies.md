# Enemies: scripts, the brain, firing

An enemy soldier (type 0x1402) is an entity (entities.md) whose behaviour is split three ways:

- **Scripts** (bytecode in the soldier's model data, 218 of them for the 1:x soldier) decide:
  react to a hit by body part, die by cause, pick cover, route, raise the alarm, set animation
  and sounds. They read the world through variables and sensors.
- **The brain** (`entity+0x38C`, run by `0x80040DF4` from the entity update) carries out what
  the scripts ask through `behaviour` (op 0x2F): move to a target, follow a path of nodes, aim
  at a target. It holds the perception ranges and the aim error.
- **The weapon** (`enemy_weapon`, `0x8005163C`, from the think) fires bursts at the target
  when the scripts set the fire state (variable 0x16).

Checked against RAM dumps of a real session (1:3, 2:1, 2:2) with `tools/recon/sst.py`.

## The think (`enemy_think`, `0x80055628`)

Each tick: health below 0 clears the attach link; `0x80055528`; the fire state from variable
0x16 goes to the weapon block (+0x11) and `enemy_weapon(enemy, player, state, ...)` runs; then
`0x80055198`, the hit-reaction hook (variables 0x1A/0x1B → `0x800523E4`), `0x80052AF4`, the
second script channel (`script_run_b`, `0x8003FBFC`), variable 0x13 ← vertical velocity, the
first channel (`script_run_a`, `0x8003F8F0`), variable 0x13 → velocity, the hit-part reaction
(variable 0x1C → `0x80052084`), `0x80072704`/`0x800730D4` (animation), `0x800563E8`.

## Variables (`get_var`, `0x8005CBA8`; `set_var`, `0x8005C97C`)

A variable id is 9 bits:

| Id | Meaning |
|---|---|
| 0x00..0x27 | Word variables at `entity+0x4C + 4·id` |
| 0x40 + n | Bit n of flags A (`+0xC`) |
| 0x80 + n | Bit n of flags B (`+0x10`) |
| 0x100 + n | Bit n of the flag words at `+0xEC` (`+0xEC + 4·(n>>5)`) |
| 0x60..0x7F | Sensors, computed when read (jump table at `0x8003C288`): the player's model id, the player's flags B & 0x400, the entity's spawn point (`+0x3A4`), whether the player is in front (dot sign), `+0x29C`, the player's current weapon, the player's `+0x11E`, the fire button, an AI node state, the alarm entity (`0x80097FE0`), the player's flags B & 0x800, the player's weapon-block bit 1, the number of other living soldiers within 3000 (and 1000 vertically), the height difference to the player, the model's id, line of sight to the player (a sweep at eye height, within 600 of the facing), the mission and level, the distance from the spawn point to the player, ... |

Known uses: 0x0C the part last hit, 0x0D health left in percent, 0x0E random rolls, 0x16 fire
state, 0x18 the type of what hit it, 0x13 vertical velocity, 0x1C hit-part reaction.

## Scripts

Each entity runs up to two script channels: contexts of 0x74 bytes at `+0x104`, each with the
current script (`+0x10` → {+0xA length, +0xC code}), the program counter (`+0x14`), wait
counters (`+0x18` animation frame, `+0x1C` ticks) and a call stack (`+0x1E` depth, frames of 16
bytes at `+0x24`, at most 5). `script_run_a` steps channel 0 and 1 until an instruction yields
(waits, animations, end); a finished script is replaced by a random one of the model's list,
weighted (`+0x4` of each entry), which is how idle variations come about.

The model's scripts: `[[entity+0x100]+0x8]+0xB8` (count `+0xBC`), 16 bytes each: `+0x4` weight,
`+0xA` length in words, `+0xC` code.

**Instructions** are 32-bit words: `[opcode:8][mask:8][imm:16]` then operand words; mask bit
n-1 set makes operand n a variable id, else a literal (`script_operand`, `0x8005D148`). The step
(`script_step`, `0x8005D18C`) calls the handler from the table at `0x80099B38` (74 entries of
{handler, opcode, size}); the handler returns the next instruction, and a flag
(`0x80099EC0`) tells the runner to yield. Jump targets are word indices in the current code.

| Op | Name | Effect |
|---|---|---|
| 00 | anim N frames A..B | Play animation N (imm low 12 bits; top 4 bits a mode), yield until frame B (-1: the last) |
| 01 | mask | Set/clear bits in the context's four mask words |
| 02 | sound | Play a sound (id, volume, pitch, spread; scaled by the sound setting) |
| 03/04/05 | gravity / fall / impulse | Gravity; fall and vertical speeds; add to them |
| 06 | nudge | Move the position by an offset |
| 07, 26 | goto I | |
| 08 | other_script N | Switch the other channel to script N |
| 09, 0B | stop | The program counter stays |
| 0C | hit_part | Configure a hit part (kind, damage...): melee attacks (`0x8004C64C`) |
| 0D, 0E, 42 | event | Store an instruction to run when an event matches |
| 0F, 2B | nop | |
| 10/11 | flags_set/clear | Flags A (imm 0) or B |
| 13 | set V = X | |
| 14 | weight | A script's random-pick weight |
| 15 | remove_handle | Remove the entity with that handle |
| 16 | wait N | N ticks |
| 17 | remove_self | |
| 18/19/1A | vec_a/vec_b/bytes | Small tables on the entity (`+0x2A4`, `+0x2AC`, `+0x2A0`) |
| 1D | jump_script N | Replace this channel's script |
| 1E, 21 | random | Random rolls into a variable |
| 1F | property | Set an entity property by number (health, angles, eye height, invulnerability, radius...) |
| 20 | clear_input | |
| 22/23/24/25 | accel / accel_add / velocity / velocity_add | |
| 27 | if A op B goto T else F | op 1 ==, 2 >, 3 <, 4 != |
| 28/29/2A, 2C/2D, 30 | arithmetic | var ops, add, sub, multiply |
| 2E | query | A fact about a target (self, handle, player) into a variable: health, eye height, distance, angle, perception (sight/hearing range, 0.75× when the player's weapon-block bit 1 is set), nodes in range... |
| 2F | behaviour | Brain goals (bits: 1 move to, 2 path through nodes, 4 aim at, 0x800, 0x1000, 0x4000, 0x8000; negative clears) |
| 31 | end | |
| 32, 33, 3E | lookup / compute | Results of helper functions into variables |
| 34 | node_eval | Score an AI node for cover (0x7FFF taken, 0x7FFE player within 600, 0x7FFD bad angle, else the angle) |
| 35/36 | node_write / node_take | Update a node; move to and face it |
| 37, 3A | route | Route to an entity or a node (`0x80056B00`, `0x80057474`) |
| 38/39 | call N / return | |
| 3B/3C/3D | start/clear/current script | |
| 3F/40 | or_flags | Into another entity's `+0x3C`/`+0x38` |
| 43 | send_event | `0x800662CC` |
| 44 | to_spawn | Back to the spawn point and facing |
| 45 | has_anim | |
| 46 | follow | Place relative to another entity |
| 47 | clear_events | |
| 48 | decal | |
| 49 | alarm | If none is up: spawn the alarm, flag every living soldier (+0x3C 0x100), send event 0x2B to the zone's entities |

## Firing (`enemy_weapon`, `0x8005163C`)

The weapon block (`enemy+0x388`): `+0x11` fire state (from variable 0x16), `+0x12` pause
countdown, `+0x13` shots left in the burst, `+0x14` countdown to the next shot. The enemy's
weapon kind indexes the table at `0x80097FF0` (48 bytes each):

| Kind | Type | Reading | Between shots | Shots per burst | Pause |
|---|---|---|---|---|---|
| 0 | 0x1401 | SMG | 4 ticks | 32 | 10-20 ticks |
| 1 | 0x1406 | Rifle | - | 1-2 | 30-60 ticks |
| 2 | 0x1407 | MG? | 12 ticks | 11 | 10-20 ticks |
| 3 | 0x1773 | Panzerfaust (`rocket_spawn`) | - | 1-2 | 90-180 ticks |

A shot is a `bullet_spawn` of type 0x13F4 aimed at the target's position raised by half its
eye height, with the damage of the enemy's weapon model (byte `+0x64` of `[[enemy+0x1C]+0x4]`:
2, 7, 8 or 10 in the dumps). Then **the aim error**: random offsets in ±E are added to the
bullet's x and y velocity (16.16, the bullet moving at 600-669 per tick), E being the brain's
`+0xC8`; when the player's weapon-block bit 1 is set the x offset is ±E/2. E differs per enemy:
20 to 100 in the dumps (±1.8° to ±9°).

The brain's `+0xB0` and `+0xB2` (1500 and 3000 for most soldiers) are the ranges `query` tests
(sight and hearing?).

## Open

- The brain (`0x80040DF4`): turning, moving, how aim is kept on the target, and where E,
  the ranges and the weapon kind come from (level data? difficulty?).
- The player's weapon-block bit 1 (crouching?): smaller detection range, half the x error.
- The sensors 0x60..0x7F one by one, and the variables the scripts use by convention.
- Events (0x0D/0x0E) and `send_event` (`0x800662CC`).
