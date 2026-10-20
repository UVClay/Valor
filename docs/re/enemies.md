# Enemies: scripts, the brain, firing

An enemy soldier (type 0x1402) is an entity (entities.md) whose behaviour is split three ways:

- **Scripts** (bytecode in the soldier's model data, 218 of them for the 1:x soldier) decide:
  react to a hit by body part, die by cause, pick cover, route, raise the alarm, set animation
  and sounds. They read the world through variables and sensors.
- **The brain** (`entity+0x38C`, run by `0x80040DF4` from the entity update) carries out what
  the scripts ask through `behaviour` (op 0x2F): seek or flee a target, follow a path, walk,
  go to a point or a cover node. It holds the perception ranges and the aim error.
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
| 0x60..0x7F | Sensors, computed when read (below) |

**Sensors** (`get_var(id, entity)`, `0x8005CBA8`; "the player" is the player's soldier):

| Id | Value |
|---|---|
| 0x60 | The player's model id |
| 0x61 | The player's flags B & 0x400 |
| 0x62 | A pointer to the entity's home (`+0x3A4`: position, then angles) |
| 0x63 | 1 if the player is behind the entity (dot of its forward vector and the offset < 0) |
| 0x64 | The entity's `+0x29C` |
| 0x65 | The player's current weapon (`inv+0x55`) |
| 0x66 | The player's `+0x11E` |
| 0x67 | The player holds fire (action 0) |
| 0x68 | The entity's node (`+0xFC`, 1..23) is usable: not taken (`+0xE`), enabled (`0x800910D0`), and not flagged |
| 0x69 | The alarm entity (`g_alarm`, `0x80097FE0`), 0 if none |
| 0x6B | The player's flags B & 0x800 |
| 0x6C | The player's weapon-block bit 1 |
| 0x6D | The number of other living soldiers within 3000 units (and 1000 vertically) |
| 0x6E | A pointer to `0x8009817C` |
| 0x6F | Height above the player (entity y - player y; y points down) |
| 0x70 | The entity's own model id |
| 0x71 | **The player is looking at it**: within ±600 (52.7°) of the player's heading (`angle_to`, `0x8003F858`), and a sweep from the player's eye to the entity's eye hits nothing (`world_sweep`, `0x8003EF5C`, -1 for no hit) |
| 0x72 | `g_params` (mission and level) |
| 0x73 | `0x800939F8` |
| 0x74 | Distance from the entity's home to the player |
| other | 0 |

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
| 2F | behaviour | Brain goals (1 seek, 2 path, 4 walk, 0x800 node, 0x1000 point, 0x4000 flee, 0x8000 none; negative clears): The brain |
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
countdown, `+0x13` shots left in the burst, `+0x14` countdown to the next shot.

| Fire state | |
|---|---|
| 0 | Hold fire: the countdowns run down |
| 1 | Bursts, each shot only while the target is within ±350 (30.8°) of the enemy's heading (`angle_to`); outside it the burst goes on without shots |
| 2 | One aimed shot now, then state 0 |
| 3 | Bursts fired straight along the enemy's own facing, not at the target (suppressing fire) |
| 4 | Bursts aimed at the target whatever the angle |

A burst: when the pause has run out, shots = weapon `+0x18` + rand × `+0x1C`, then a shot
every `+0x10` + rand × `+0x14` ticks; after the burst, a pause of `+0x20` + rand × `+0x24`.
Every aimed shot is aimed afresh at where the target is now (no lead, no tracking lag): the
only inaccuracy is the aim error. The enemy's
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
bullet's pitch and yaw (angle units, 4096 per turn), E being the brain's `+0xC8`; when the
player's weapon-block bit 1 is set the pitch offset is ±E/2. E differs per enemy, set by the
level (Spawning): 20 to 100 in the dumps, ±1.8° to ±8.8°.

The brain's `+0xB0` and `+0xB2` (1500 and 3000 for most soldiers) are the ranges `query` tests
(sight and hearing?).

## Spawning (`enemy_spawn`, `0x800563F0`)

A soldier is made from a spawn record in the level data (`[a0+0x4]`), so health, accuracy and
ranges are the level designer's, per enemy; the game's difficulty parameter (`g_params+0xB`,
from `g_game+0x110`) is read by nothing in LEVEL.BIN. At most `0x80097FEC` soldiers are alive at
once (`0x80097FE8` counts them), and none while `0x800AB8C8` is 4 or more.

| Record | Becomes |
|---|---|
| +0x5B | Variable 8: 33, 67 or 100 (by value 0, 1, 2) |
| +0x5C | Variable 0x24: 10, 20, 1000 or 100 (by value 0, 1, 2, else) |
| +0x5E | Variable 0x10 |
| +0x61 | Health and maximum health (6.0 in 1:3, 4.0 in 2:1) |
| +0x63 | The aim error E (brain `+0xC8`) |
| +0x65 | Variable 0x26 |
| +0x66 | Variable 0x0B (30 → 1) |
| +0x6A | Brain `+0xB6` |
| +0x6C | Brain `+0xB0`: a range (1500 in the dumps) |
| +0x6E | Variable 0x22 |
| +0x70 | Brain `+0xB2`: a range (3000; 2800 if under 10), and variables 5, 6, 7 = it, half, a third |
| +0x68 | Entity `+0x290` and `+0x294` (150) |
| +0x74 | → a sub-record: count, then hashes: +0x8 the weapon (matched in `g_enemy_weapons`, its index into variable 0xF), +0xC a second model part (unless 0x4CBF0BA5), and with 4 entries variable 0x101 = 1 and a third part 0x61282777 |

The handlers: think `enemy_think`, `+0x370` `0x8005608C`, hit_part `enemy_hit_part`,
world_contact `0x80056250`, touch `0x8005291C`, `+0x384` `0x80056288`.

## The brain (`brain_update`, `0x80040DF4`)

The brain (0x124 bytes, `brain_alloc`) is the soldier's legs: the scripts set goals with
`behaviour` (op 0x2F, `op_behaviour`, `0x8005AC00`), the brain turns the heading (`+0x22C`) and
sets the speed scale (`+0x224`) every tick; the distance covered comes from the animation (its
root motion), not from the brain. Decompiled with m2c (`fn.py c`); fields in `context.h`.

### Goals (`+0x4`) and how scripts set them

`behaviour GOAL A B C [D]`: operand 1 the goal, 2..4 its parameters, 5 the instruction's own
immediate. A new movement goal first drops the others (`goals &= ~0x5C03`), frees the cover node
the soldier held and resets the gait.

| Goal | Parameters | Each tick (`brain_update`) |
|---|---|---|
| 0x1 seek | target entity (`+0x10`), slow-down radius (`+0xC2`), turn rate (`+0xD4`, default 0x100) | `brain_move_to`: steer toward the target |
| 0x4000 flee | target entity, `+0x50` | `brain_move_to` with the forward vector reversed: steer to put the target behind |
| 0x2 path | path id, mode (`+0xBA`: 0 once, 1 backwards, 2 back and forth; -1 keep) | `brain_follow_path`: starts at the path's nearest point |
| 0x4 walk | speed (16.16, `+0x18`), gait (`+0xC0`), backwards (`+0xD8`) | Walk the heading, turning along walls (`brain_steer`); also used during the other goals |
| 0x1000 point | position pointer (`+0x40`), variable, arrive radius (`+0xA8`), slow-down radius | `brain_goto_point` |
| 0x800 node | AI node (`+0x3C`), variable, arrive radius, slow-down radius | `brain_goto_node`, via `brain_node_target` |
| 0x8000 | | Every goal off |
| -bit | | That goal off (seek, path, walk, node, point and flee also reset the gait) |

**Seeking** (`brain_seek`, `0x80040180`, shared by every goal): the GTE outer product of the
forward vector (rotation matrix column `+0x344`/`+0x350`) and the offset to the goal gives the
side it's on. Beyond a dead zone (|cross| > 0x20, after scaling the offset down by 2, or by 4
past 2000 units) the heading turns toward it by the turn rate, or by less to land exactly on it;
inside the dead zone, a dot product says whether the goal is behind (turn) or ahead (no turn).
The heading wraps to ±half a turn. Turn rates: seek `+0xD4` (0x100 = 22.5° per tick, 675°/s),
point and node 0x100, path 0xC0 (16.9° per tick).

**Arriving**: with a slow-down radius (`+0xC2`) and the goal inside it, the speed scale is
`distance / radius` (so the soldier slows to a stop), and so are the probe radius and `+0x3C8`;
otherwise speed 1.0. Distances everywhere are the cheap `max + (mid + min) / 4`.

**Point and node goals** write the remaining distance into the given script variable every
tick; within the arrive radius they write 0 and set entity `+0x38` bit 0x400000.

**Nodes** (the world's node table, `[[g_world+0x388]+0x8]`, 0x34 bytes each: +0x0 position,
+0xC kind, +0x10 approach side (1, 2; 4 none), +0x24/+0x28 facing x/z, +0x30 the handle of the
soldier holding it): the soldier stands a little behind the node along its facing (and 40 units
to one side); a node with an approach side is reached through a waypoint 100 units to that side
first (`+0xAC`), unless the soldier is already within 250 units. The soldier claims the node
(`+0xB6`) until its next movement goal.

**Paths** (`g_paths`, `0x8009E290`, from the BSD header's `+0x5A0` by `paths_parse`): {id, count,
points}, points of 0x14 bytes starting with s16 x, y, z. The current point (`+0xBC`) advances
(by `+0xBE`, ±1) when the soldier moved past it this tick, is within 76 units, or has it behind
(the dot of the path's direction and the offset). At the ends, by mode: once, stop and set
`+0x38` bit 0x1000000; backwards, at 0 set 0x800000 and wrap; back and forth, turn round
(0x800000 at the start, 0x1000000 at the end). Every point reached sets 0x400000.

**Walls** (`walker_slide`, `0x80078F60`, the walkers' move against the level): while the walk
goal is on, the walls within the probe radius are sorted by the angle of their normal to the
heading into ahead, left and right; the nearest of each is kept at brain `+0xEC..+0x120` with
state bits 4, 8, 0x10 (`+0x8`). `brain_steer` then sets the heading along the wall: the angle of
its normal (`ratan2`) ±90°, away from the side it's turning to.

### Set up at spawning (`enemy_spawn`)

Sight and hearing ranges from the record (`+0x6C`, `+0x70`), aim error (`+0x63`), `+0xB4` =
record `+0x6A` × 11, path mode 2 (back and forth), no slow-down, gait speeds 100, walk speed
1.0, probe radius 60, collision radius 150 (the record's `+0x68`, 150, goes to entity
`+0x290`/`+0x294`), no node held.

## Open

- Who reads entity `+0x38` bits 0x400000/0x800000/0x1000000 (arrived, path ends): events?
- The gait table (`+0xCC`/`+0xD0`) and what `+0xB4`, `+0x3C8` do.
- The player's weapon-block bit 1 (crouching?): smaller detection range, half the x error.
- The variables the scripts use by convention; what the player's flags B 0x400/0x800 and
  `+0x11E` are.
- Events (0x0D/0x0E) and `send_event` (`0x800662CC`).
