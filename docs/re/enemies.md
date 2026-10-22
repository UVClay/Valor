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
0x16 goes to the weapon block (+0x11), `enemy_weapon(enemy, player, state, ...)` runs, and the
block's +0x11 goes back into variable 0x16 (so a fire state 2 reads 0 once its shot is fired);
then `0x80055198`, the hit-reaction hook (variables 0x1A/0x1B → `0x800523E4`), `0x80052AF4`
(witnesses), the **event matcher** of both channels (`script_run_b`, `0x8003FBFC`), variable
0x13 ← **the heading** (`+0x22C`), **both channels' scripts** (`script_run_a`, `0x8003F8F0`),
heading ← variable 0x13, the hit-part reaction (variable 0x1C → `0x80052084`),
`0x80072704`/`0x800730D4` (smoke and blood particles, animation.md), `0x800563E8`. The dog's
think (`dog_think`, `0x80054AB8`) is the same around the scripts with variable 0x0E as the
heading and 0x13 as the hit reaction.

Exactly, in order: health's integer part ≤ 0 → health 0 and, if its holder (`+0x30`) holds it,
the holder's `+0x30` cleared; **the ranges** (`0x80055528`: v5 = brain `+0xB2`, v6 = it / 2, v7 =
the first soldier's +0xB2 / 3, kept once in `0x800AB930` (flag `0x80099B2C`); while the player
crouches v5 = 3/4, v6 = 3/8, v7 = 1/4 of it, each as s16); `enemy_weapon`; the attachments
(`0x80055198`, enemy_types.md, Attachments); v1A ≠ 0 → `enemy_attachments_op(v1A, v1B)`;
witnesses; the scripts around v13; v1C ≠ 0 → the hit reaction (`0x80052084` with the part v0xC:
by the blood setting `0x80093A05`, 0 turning reactions 1, 4, 8 into 2, 1 turning 4, 8 into 1;
1: drops (`0x800727B4`, up to n of 15 slots of 0x74 at block +0x49C, each 4 draws), 2: a puff
(`0x80072244`: the first free of 5 slots of 0x24 at block +0x3E8: 60 ticks, two draws), then
v1C = 0); block +4 = puffs update (a live puff rises 1.0 a tick, turns, fades by 1/8, until its
60 ticks end), block +0 = drops update (falling to the soldier's floor `+0x208`, lying there;
the landing draws one `rand`). The blood setting is 0 in the recorded session: only puffs.
The dog's think is witnesses, the scripts around v0E, v13 → the hit reaction, and the same two
updates. (Earlier versions of this document had run_b as "the
second channel" and 0x13 as the vertical velocity: `fn.py c 0x80055628` shows
`set_var(e, 0x13, e->angles.y)` and `e->angles.y = get_var(0x13, e)`; the VM interpreter below
matches every tick's variable 0x13 that way.)

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
| 0x68 | The entity's last sound is still playing: its voice (`+0xFC`, 1..23) is enabled, a one-shot, positional and younger than 120 frames (audio.md) |
| 0x69 | The alarm entity (`g_alarm`, `0x80097FE0`), 0 if none |
| 0x6B | The player's flags B & 0x800 |
| 0x6C | The player's weapon-block bit 1 (**crouched**, `player_crouch_done`) |
| 0x6D | The number of other living soldiers within 3000 units (and 1000 vertically) |
| 0x6E | A pointer to `0x8009817C` |
| 0x6F | Height above the player (entity y - player y; y points down) |
| 0x70 | The entity's own model id |
| 0x71 | **The player is looking at it**: 1 when within ±600 (52.7°) of the player's heading (`angle_to(player, entity)`, `0x8003F858`) and a sweep from the player's eye (y − eye height) to the entity's eye hits nothing (`world_sweep`, `0x8003EF5C`: its -1, shifted right 31 logically); else 0. A hit's face record is written into the entity's `+0x318` |
| 0x72 | `g_params` (mission and level) |
| 0x73 | `0x800939F8` |
| 0x74 | Distance from the entity's home to the player |
| other | 0 |

Known uses: 0x0C the part last hit, 0x0D health left in percent, 0x0E random rolls, 0x16 fire
state, 0x18 the type of what hit it, 0x13 the heading (16.16, 4096 per turn: the think copies
it in and out around the scripts, so writing it turns the soldier), 0x1C hit-part reaction.

`get_var` on 0x40..0x5F, 0x80..0x9F and 0x100.. reads one bit (0 or 1); `set_var` sets the bit
for any non-zero value. Only `+0xEC` itself holds bits 0x100..0x11F: a higher id indexes
`+0xF0`, the maximum health. `set_var` on a sensor does nothing, except 0x6A with a brain
(0: save the position in brain `+0xE0`; else restore it). Sensors 0x6A and 0x75..0x7F read 0.

## Scripts

Each entity runs up to two script channels (soldier.md: channel 0 decides, channel 1 runs the
current action). Everything below was read from the handlers (`fn.py c`, `fn.py show` where
m2c's casts are unreliable) and then checked with a reference interpreter,
`tools/sim/sst_vm.py`: it steps every soldier and dog of a RAM dump through one tick from these
rules and compares channels, call stacks, event tables, variables, flags and the animation
instance with the next tick's dump of the same replay (Checking, below).

**The model's scripts**: `[[entity+0x100]+0x8]+0xB8` (count `+0xBC`), 16 bytes each: `+0x0`
class bits (every soldier and dog script has class 1), `+0x4` random-pick weight, `+0xA` length
in words, `+0xC` code. Model `+0x94` is the start script; `+0xB4` (count `+0xB0`) the model's
own event table (Events).

**A channel** is a context of 0x74 bytes (channel 0 at `+0x104`, channel 1 at `+0x178`):

| Offset | |
|---|---|
| +0x00..+0x0C | Four class masks (op 01; starting a script, below) |
| +0x10 | The script's entry (0: the channel is idle) |
| +0x14 | Program counter (an address) |
| +0x18 | s16 frame target: the channel waits while the animation frame is below it (-1: none) |
| +0x1A | s16 script index (-1: none); channel 1's (`entity+0x192`) is what op 0x3D reads |
| +0x1C | s16 tick wait |
| +0x1E | s16 call depth, 0..5 |
| +0x20 | Its event table: `entity+0x1EC` records of 0x3C bytes (40 for the 1:3 soldier, 20 for the dog) |
| +0x24 | Five call frames of 16 bytes: return address, the channel script's code, the called script's code, its length (u16) |

Jump targets (goto, if) are word indices from the start of the code being run: the called
script's while the depth is above 0, else the channel's script. The end of the code is the
called script's length likewise.

### A tick: `script_run_b` then `script_run_a`

1. **Events** (`script_run_b`): while `+0x44` counts down, clear `+0x38` bits 0x600 and count
   it down; while `+0x48` counts down, clear `+0x38` bits 0x1E0 and 0x180000 and count it down.
   Then for channel 0, then 1, if it runs a script: try its event records in order; the first
   whose condition holds runs its stored instruction (Events) and ends that channel's search.
   Quirk: once a record has matched (in either channel), a later empty record also ends the
   search, so channel 1's records past an empty one are not tried after a channel-0 match.
2. **Steps** (`script_run_a`), channel 0 then 1:
   - Idle: channel 0 starts script 0 (no step this tick); channel 1 stays idle.
   - Tick wait above 0: count it down; done for this tick.
   - Animation frame (instance `+0x5E`, **shared by both channels**) below the frame target:
     the frame **goes up by one** (this is where animations advance: animation.md), the blend
     counter (instance `+0x3E`) down by one if above 0, the root motion is applied
     (`anim_root_motion`, `0x8003F1F8`); done for this tick.
   - Otherwise the frame target becomes -1 and instructions run until one yields. Before each,
     a program counter at or past the end of the code (or 0, from op 0x31) **ends the channel**:
     its script, index, and the instance's animation (-1) and frame (0) are cleared; channel 0
     then starts the first script whose weight is above `rand() × 100 / 32767` (only the
     guard idle 202 has a weight in 1:3), or script 0, and goes on running it this tick;
     channel 1 stays idle. A called script that runs off its end ends the channel the same
     way: there is no implicit return.
   - The ops that yield: 00 (anim), 0x16 (wait), 0x17 (remove self). Nothing else ends a tick,
     so a script that loops without them hangs the game (ops 09 and 0B return their own
     address without yielding: they would hang; no soldier or dog script uses them).

So `wait N` resumes on the Nth tick after it (`wait 0` and `wait 1` both on the next), and
`anim N frames A..B` shows frame A on its own tick and resumes the script on the tick after
frame B is reached: B-A+1 ticks (B = A: one tick).

### Starting a script (`script_start`, `0x8003F454`)

`script_start(entity, n, force, channel)`. Op 0x1D (jump_script) forces; ops 08 (restart),
0x3B (start on channel 1) and a channel's random pick don't. An unforced start happens when the
script's class bits meet the channel's mask `+0x4` (and then with a cross-fade from the current
pose: blend counter 4, instance `+0x40` = -1, the pose copied as for anim mode 1), or meet mask
`+0x0`, or meet neither `+0x8` nor `+0xC` while the channel is idle; otherwise nothing happens
and the op goes on to the next instruction. Op 01 sets these masks; a start resets them to 0,
-1 (every class), 0, 0. No soldier or dog script uses op 01 and all their scripts are class 1,
so every start succeeds, with the cross-fade (checked: the blend counter matches every tick).

A start sets the script, its index and the program counter to its first word; frame target -1,
tick wait 0, depth 0; gravity `+0x1FC` = 0; `+0x28C` = `+0x288`. On channel 0 it also resets
the melee hit part (`0x8004C64C(e, -1, -1, -1, -1)`) and **reloads the channel's event table
from the model's** (the records the start script registered, Events), zeroing the condition word
of the rest; on channel 1 it zeroes the condition word of every record. Ops 08 and 0x1D then
continue at the new script's first word in the same tick (if the start happened).

### Events (ops 0x0D, 0x0E, 0x42: `sop_event`, `0x80058C78`)

A record (0x3C bytes): `+0x0` condition mask (the instruction's low byte; 0: empty), `+0x4` the
instruction's operand mask byte, then one word per condition bit at fixed places: `+0x8` (bit 1,
a pad test), `+0xC` (2, a button), `+0x10`/`+0x14` (4/8: `+0x38` has all / none of these
bits), `+0x18`/`+0x1C` (0x10/0x20: `+0x3C`), `+0x20`/`+0x24` (0x40/0x80: `+0x40`), and the
stored instruction at `+0x28`, copied as it is (its operand mask is applied when it runs, so a
variable operand is read then). In the instruction the words follow the opcode word in bit
order, then the stored instruction, whose length comes from a table by opcode (`0x80090EA0`).

The record goes to the running channel's table (0x0D, 0x42) or to channel 0's (0x0E); while the
channel runs the model's start script, to the model's table instead (shared by every entity of
the model, copied into channel 0 at each start, above). The place: a record with the same
condition and words, else the first empty one, else nothing is stored. A stored `nop` (0x0F)
empties it.

When a record matches (A tick, step 1), a stored `goto` (0x26) sets the program counter to the
target in the **channel's own script** (not a called one), with frame target -1, tick wait 0 and
depth 0; a stored `call` (0x38) does nothing (but counts as the match); anything else runs once
through `script_step` (`restart`, `set`, `wait`...: a stored `restart` replaces the script and
runs nothing of it until the step; a stored `wait` only sets the tick wait).

**Instructions** are 32-bit words: `[opcode:8][mask:8][imm:16]` then operand words; mask bit
n-1 set makes operand n a variable id, else a literal (`script_operand`, `0x8005D148`). The
operands are numbered by the handler, not by position: in `if`, B is operand 3. The step
(`script_step`, `0x8005D18C`) calls the handler from the table at `0x80099B38` (74 entries of
{handler, opcode, size}; the size is not used by the step); the handler returns the next
instruction's address, and a flag (`0x80099EC0`) tells the runner to yield.

| Op | Name | Effect |
|---|---|---|
| 00 | anim N frames A..B | Animation N (imm low 12 bits; top 4 bits a mode), frame A, yield; the channel then waits for frame B (-1: the last) (animation.md, Playback) |
| 01 | mask | imm 0: clear the operand's bits in the four class masks; 1..4: set them in mask imm-1 and clear them in the others |
| 02 | sound | id = imm low byte, then five **whole words** through `script_operand` (pitch, pitch spread, volume, volume spread, bank; e.g. pitch 0x8000, spread 0x3333), the flags in the imm high byte (3: on voice 0 path, 0x20 loop); the voice into `+0xFC` (`op_sound`, `0x80058420`). An earlier version said the words' low bytes: wrong (sst_vm.py still masks them) |
| 03 | gravity | `+0x1FC` = operand |
| 04 | fall | `+0x200` (unless imm bit 2), `+0x21C` (unless bit 3), `+0x218` (unless bit 4); imm bit 1: two operands only |
| 05 | impulse | Only with imm bit 1 (else a 4-word nop): `+0x21C` += B, `+0x200` += A (at most `+0x204` when set) |
| 06 | nudge | Only with imm bit 1: x += A (−A with imm bit 0 when `+0x288` < 0), y += B |
| 07, 26 | goto I | |
| 08 | restart N | Start script N on the channel running it, unforced (`0x800589E8`; the channel index is `0x80099EB8`). (sst.py prints it as `other_script`) |
| 09, 0B | | Return their own address without yielding: would hang |
| 0A | turn_half | Heading += half a turn; `+0x28C` = `+0x288` (overwritten from variable 0x13 after the scripts, see 0x24) |
| 0C | hit_part | Configure a hit part (kind, damage...): melee attacks (`0x8004C64C`) |
| 0D, 0E, 42 | event | Store an event record (Events) |
| 0F | nop | One word. 2B is a three-word nop |
| 10/11 | flags_set/clear | Flags A (imm 0) or B |
| 12 | leave_ground | Leave the ground (`0x8005908C`; soldiers, dogs, the player's body; catalogue.md) |
| 13 | set V = X | V the immediate, X a full word |
| 14 | weight | Script A's random-pick weight = imm (in the model: every entity of it) |
| 15 | spawn | Spawn an entity of kind imm low byte without a record (`0x80061168`: 1, or 0x4C a ladder), start its start script forced and run its channels at once, link it at the front of the list (`0x8006D574`) |
| 16 | wait N | Tick wait = N - 1, yield |
| 17 | remove_self | Yield |
| 18/19/1A | vec_a/vec_b/bytes | Small tables on the entity: three s16 at `+0x2A4`/`+0x2AC` + 16 × imm, three bytes at `+0x2A0` |
| 1B | | `0x8008DAA0` on the tables of 18/19/1A (`0x8006D544`) |
| 1C | model_variant | Render-instance variant (`0x800594AC`; every character and weapon model) |
| 1D | jump_script N | Start script N on this channel, forced |
| 1E | random | V (imm low byte) = min + rand() % (max - min + 1), min and max the operand's s16 halves, if the chance passes, else -1. Chance C = imm high byte: `rand() % 100` is drawn first in every case and passes when C = 0 or it is ≤ C, so (C+1)/100 |
| 21 | random_timer | V (imm low byte) = A + rand() % (B - A + 1) when `rand() % (C × 480)` < 16 (C = imm high byte), else -1 |
| 1F | property | Set an entity property by number (health, angles, eye height, invulnerability, radius...) |
| 20 | clear_input | Clears the two words at `0x80093B84` (pad tests) |
| 22/23 | spin / spin_add | Angular velocity (`+0x234`, x y z) = / += the operands (entities.md: added to the angles by the move) |
| 24/25 | angles / angles_add | Angles (`+0x228`) = / +=. For a soldier the heading part is overwritten from variable 0x13 after the scripts (the dog's 0x0E): scripts turn with variable 0x13 and the ops that take a variable operand for the result (0x36, 0x44, 0x46, which the scripts give 0x13) |
| 27 | if A op B goto T else F | op (imm) 1 ==, 2 >, 3 <, 4 !=; A operand 1, B operand **3** (mask bit 2), T and F the low and high halves of the third word; another op returns its own address (hangs) |
| 28/29/2A | xor / or / and | V ^= / \|= / &= X, through get_var/set_var (bit variables too) |
| 2C/2D/30 | add / sub / mul | The raw word at `+0x4C + 4·V` += / -= / ×= X (no bit or sensor handling) |
| 2E | query | A fact about a target into a variable (soldier.md, Reading the disassembly). The target: operand 4 if non-zero; else with B (operand 2) not -1 the first live entity of type B in group H (imm high byte, `+0x35`; `entity_find_type_group`), or with H = 0xFF the nearest one (`0x80057768`); B = H = -1: the entity itself. Facts 1, 3..6 fields, 8 the target, 9 distance, 0xA distance to the entity in B, 0xB/0x10/0x12 sight (0x12 with `+0xB2` and no crouch reduction), 0xC \|angle\|, 0xD itself, 0xE its current animation, 0xF aimed at, 0x11 hearing. No target found (0): nothing is checked, facts 1, 3..6 read those offsets of address 0 (low RAM), 9 is the distance to the 16.16 point at address 0x20C, 8 gives 0 |
| 2F | behaviour | Brain goals (1 seek, 2 path, 4 walk, 0x800 node, 0x1000 point, 0x4000 flee, 0x8000 none; negative clears): The brain |
| 31 | end | Returns address 0: the channel ends |
| 32 | nearest | V = the nearest live entity of type X within 1000 vertically (`0x80057768`), 0 if none |
| 33, 3E | compute / probe | Results of helper functions into variables (soldier.md). Compute 0, the node search (`0x80040F48(e, range, exclude, mask)`): the nearest node (max + (mid + min)/4 from the soldier, within \|range\|) that is not `exclude`, free (its holder's handle dead; any with exclude -1), of a kind in mask (`+0xC`; mask -1 any), nearer the soldier than the player, the player within 10000 of it and in front of it ((player − node) · direction, y left out, >> 12 ≥ 101); mask 8 waives the nearer and in-front tests; -1 if none |
| 34 | node_eval | Read an AI node (`0x8005B32C`; operand 1 the node, 2 the variable, the fact in the instruction's low byte): 1..5 its words `+0..+0x10`, 6 the holder (the entity of `+0x30`), 7 the cover score; -1 for an index past the node count. The score: 0x7FFF if the node's `+4` is set and its link `+0x2C` (≥ 0) names an entry of the world object's table `+0x14` (8 bytes each) with bit 2; else from the soldier to the node's point (the brain's node point `+0xA0` when its claimed node `+0xB6` is this one, else the node's `+0x14`): with the node's `+4` ≥ 500, 0x7FFE if the soldier is 201 or more off in height or in front of the node (offset · direction `+0x24..+0x28` >> 12 ≥ 101); 0x7FFE if the player is within 600 of the point (`dist_approx`); otherwise \|wrap(ratan2(player − point) − ratan2(direction))\| when the player is within 3500 (7000 for kind 0x10), farther than the soldier, and that angle under 0x400, else 0x7FFD |
| 35/36 | node_write / node_take | Update a node; take it (`0x8005B8EC`, operands node, variable, how; for a node in the table only): how 0 or `how & 1` puts x and z on the brain's node point (`+0xA0`, `+0xA4`, the spot `brain_node_target` chose), how 0 or `how & 2` the heading from the node's `+0x1E (<< 16)`, otherwise `how & 4` from its direction (`ratan2(+0x24, +0x28) << 16`), into the yaw `+0x22C` and the variable (neither: both untouched); the table is the world object's `+8` (0x34-byte nodes), count `+0xC`; a node with `+0x4` set sets the radius `+0x2E8` and `+0x3C8` to half the brain's (`+0xC6`, `+0xCA`); flags B \|= 0x80. Watched in 1:2 (a monkey run): soldiers stepping into cover jump onto the node point in one tick |
| 37, 3A | route | The turn toward an entity or a node: a turn kind (0 ahead, 1/2 the sides, 3 behind) and the signed angle into two variables (`0x80056B00`; enemy_types.md) |
| 38 | call N | Depth + 1 (past 5: an error, depth stays 5, no call); the frame {return address, channel script's code, N's code, N's length}; continue at N's first word |
| 39 | return | Depth - 1 and continue at the frame's return address; at depth 0 nothing |
| 3B | start_script N | On channel 1, unforced, unless channel 1's index equals the raw immediate |
| 3C | clear_script | Channel 1 idle: index -1, frame target -1, no script (its wait and depth stay) |
| 3D | current_script | V = channel 1's index (-1 none) |
| 3F/40 | or_flags | Into another entity's `+0x3C`/`+0x38` |
| 41 | instance_6c | Instance `+0x6C` (`0x8005C1B4`; soldiers and dogs; with 0x1C the corpse's blink) |
| 43 | send_event | Broadcast level message N with the soldier as sender (`0x800662CC` → `msg_send`; 0x1B alerts the sender's tagged group: enemy_types.md, Messages) |
| 44 | to_spawn | imm high byte 0: back to the spawn point and its facing, 1: the point only, 2: the facing only; the facing also into variable imm low byte |
| 45 | has_anim | Raw word V = 1 if the model has animation X |
| 46 | follow | Place relative to another entity (the heading into a variable): `0x8005C398`, target W1 & 0xFFFF, distance imm, sideways offset W2 & 0xFFFF, variable (W2 >> 16) & 0xFFF, mode W2 >> 28. Mode 0: u = VectorNormalSS(target - self, y 0), yaw = ratan2(ux, uz) into the variable and `+0x22C`; mode 1: u = the target's forward column, yaw its `+0x22C`; other modes nothing. Then x, z = (offset ≠ 0: the target's matrix and translation applied to (offset, 0, 0); else the target's position) - u·distance >> 12 |
| 47 | clear_events | Zero the condition word of every record of the running channel's table |
| 48 | run_message | Run a level-script message on an entity at once (`0x8005C698` → `script_run_message`); used by no model on the disc (catalogue.md) |
| 49 | alarm | If none is up: spawn the alarm, flag every living soldier (+0x3C 0x100), send event 0x2B to the zone's entities |

Query fact 0xF, **The player's gun points at it** (`0x80056F38` → `0x80056F60(entity, player)`): the gun line: the body model's bone `+0xD6`, its offset `+0xE0` turned by the bone's quaternion matrix from its joint (p1), and p1 + (0, 0, 100) (p2), both through the soldier's matrix and integer translation, + the camera's eye offset on y (without joints: the weapon table's two bones); u = VectorNormalSS(p2 - p1); the entity's sphere centre minus the **player's sphere centre** (`+0x2E0`, not p1) crossed with u (GTE OP, sf=1, the offset d on the matrix diagonal and u in IR: d × u, each component >> 12 rounding down, so the order matters on the edge: 1:3 tick 857 is 22571 against 22500) must be within the entity's radius `+0x2E8` (0: the brain's `+0xC6`). Direction is not tested: behind counts too

Some of the effect ops, exactly (op table at `0x80099B80`: {handler, op, length in words} from
op 6):
- **0C hit_part** (`0x80058AB4`): operands mask (1), kind (2; word bit 2: -1, bit 1: operand),
  radius (3; bits 8/4), damage (4; bits 0x20/0x10, << 16) into `0x8004C64C(e, mask, kind,
  radius, damage)`: every part whose `+4` meets the mask gets kind `+8` (> 0: the value; < 0:
  its default `+0xC`), damage `+0x10` (< 0: `+0x14`) and radius `+0x34` with its square `+0x38`
  (< 0: `+0x30`). For the player (type 1) an attack kind (5, 6, 9) counts a melee statistic,
  any other clears flags A 0x10000000. `script_start` on channel 0 calls it with (-1, -1, -1,
  -1): every part back to its defaults.
- **17 remove_self** (`0x8005926C`): `entity_remove`, `0x80099EC0` = 1, and the script yields.
- **1C model_variant** (`0x800594AC`): the instance's `+0x68` (which mesh is drawn).
- **1F property** (`sop_property`, `0x80059718`): value (1), type (2), group (3: the imm high
  byte), target (4); no target: the entity itself, or with type ≠ -1 and group ≠ -1 the first of
  that type and group. Selector (imm low byte): 1 health `+0xF4`; 2 the scale `+0x270..+0x278`;
  3 the eye height `+0x2D4`; 4 the cull counters `+0x290`/`+0x294` = value >> 16; 5 `+0x44` and
  6 `+0x48` (>> 16); 19 the radius `+0x2E8` and 20 the probe radius `+0x2EC` (>> 16).
- **43 send_event** (`0x8005C210` → `send_event` → `msg_send`): level message N with the entity
  as sender, no object, no actor.

### Checking (`tools/sim/sst_vm.py`)

`sst_vm.py --pairs V...` takes RAM dumps one game tick apart (`--dump-ram V` and `V+1` of the
replay, where the trace's tick at vblank V is the step between them), steps every live soldier
and dog of the first through `script_run_b` and `script_run_a` on a copy of RAM, and compares
with the second. Effects outside the VM (animation poses, sounds, brain goals, hit parts,
messages) are recorded as calls. Values from the world are taken as follows: distances (query 9,
0xA), sensors 0x60..0x66, 0x68..0x70, 0x72..0x74 and entity lookups are computed (positions:
the next dump's for entities earlier in the list, which have already moved, the first dump's for
the rest); sight, hearing, aimed-at, angles, probes, node searches and route results are the
written variable's value in the next dump, and when the variable is written again later in the
tick the candidates (0/1, 0 or R+1, ±3 around an atan2 estimate, -1) are searched; `rand()` is
the real generator's stream between the two dumps' seeds (`0x800CA2A4`, objects.md), tried from
each place. The fire state is the next dump's weapon block `+0x11`, which `enemy_weapon` writes
before the scripts. Not compared, being written after the scripts: the brain's distance
variable, the hit-reaction variable (0x1C, the dog's 0x13) and the hit handlers' 0x0C, 0x0D and
0x18 (`enemy_hit_part`, in the collision pass: a corpse shot again changes them with no script
running). A cross-fade started when the instance has no pose yet (pose cache `+0x62` = -1)
shows a blend counter of 0 in the next dump: `pose_build` zeroes it at the draw (animation.md,
The pose), not the VM; an entity not drawn that frame (off screen: its cache still -1 after it)
keeps the counter, which then counts down with its frames (2:2, vblank 45937: 2 → 1).

Result over 51 tick pairs of the recorded replay (30 in 1:3 from vblank 2163 to 15915, 21 in
2:1 from 20702 to 33550; 7 more pairs were dropped as not one tick apart): **95 of 95**
soldier and dog thinks agree in everything compared, idle, patrol, alert, engage, the fire
loops, dodges, hit reactions, deaths and corpses. The 30 opcodes the scripts ran agree
every time: 00 (42 thinks), 02, 04, 08 (26), 0C, 0D (27), 13, 16, 1C, 1E (34), 22, 26, 27 (60),
29, 2C, 2D, 2E (36), 2F, 33, 37, 38/39 (39), 3B, 3C, 3D, 3E, 41, 42, 43, 47. Computed distances
agreed with the next dump 16 of 16 times; 4 thinks matched the real `rand()` stream exactly,
one of them only with one extra draw skipped in the middle of the think (10894: not explained;
the random value it lands on is unconstrained, so it could be anywhere before 209:101); 13 needed
the candidate search (all for a find-node result of -1, or a roll later overwritten). The other
opcodes (01, 03, 05, 06, 09..0B, 0E, 0F, 10..12, 14, 15, 17..1B, 1D, 1F..21, 23..25, 28, 2A,
2B, 30..32, 34..36, 3A, 3F, 40, 44..46, 48, 49) did not run in these ticks: their rows above come
from the handlers only. Disagreements on the way were all rules
missing from the interpreter, now written above: a start or op 47 clears only each record's
condition word (the stale words stay), the hit handlers' variables, the pose-less cross-fade, and
sensor 0x68 (2:1's 288/289 loop on it while the soldier's own shout plays; `sst.py` labels it
`node_usable`).

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
| 2 | 0x1407 | Pistol (fires the pistol sound, audio.md) | 12 ticks | 11 | 10-20 ticks |
| 3 | 0x1773 | Panzerfaust (`rocket_spawn`) | - | 1-2 | 90-180 ticks |

A shot is a `bullet_spawn` of type 0x13F4 aimed at the target's position raised by half its
eye height, with the damage of the enemy's weapon model (byte `+0x64` of `[[enemy+0x1C]+0x4]`:
2, 7, 8 or 10 in the dumps). Then **the aim error**: random offsets in ±E are added to the
bullet's pitch and yaw (angle units, 4096 per turn), E being the brain's `+0xC8`; when the
player's weapon-block bit 1 is set (the player crouches) the pitch offset is ±E/2. E differs per enemy, set by the
level (Spawning): 20 to 100 in the dumps, ±1.8° to ±8.8°.

**Exactly** (`tools/sim/engine_combat.py`, `enemy_weapon`): kind = variable 0xF & 0xFF, entry
W = `0x80097FF0` + 0x30·kind (+4 type, +8/+0xC the two vertices of the weapon model the shot
runs between, +0x10/+0x14 between shots, +0x18/+0x1C rounds, +0x20/+0x24 pause, +0x28 sound,
+0x2C bank (-1: the body model's `+0xC8`)); each "+ rand × r" is `(rand·r) >> 15`, drawn only
when r ≠ 0. State 0: `+0x14` -= 1 (not below 0); with no rounds left `+0x12` -= 1. State 2:
one shot at the player's middle (x, y - eye/2, z), then the error: crouched, pitch += (-E >> 1) +
(rand·E >> 15) (E = 0: E >> 1); else pitch += (rand·2E >> 15) - E (E = 0: -E); yaw likewise
(always the full ±E); the muzzle flash (`+0xB68+2` = 1), `+0x11` = 0; **no sound**. States 1,
3, 4: pause 0 → new burst (pause, rounds, `+0x14` = 0); else `+0x14` running → -1; else no
rounds → pause -1; else `+0x14` = between, and the shot unless state 1 with |angle_to| ≥ 0x15E:
state 3 along the gun (from vertex +8 to vertex +0xC), 1 and 4 at the player's middle with the
error (crouched: pitch ± 20 and yaw ± 90 fixed, `((rand·40) >> 15) - 20`, `((rand·180) >> 15) -
90`; else ±E both); then the flash, the sound (class 3 for rockets) and rounds -1, at 0 `+0x3C`
|= 1. Finally `+0x10` = `+0x11`. The bullet: `bullet_spawn(enemy, W+8, W+0xC, target or none,
the weapon attachment (weapon block +0x1C), 0, 0, damage, 0, 0, 0x13F4, flags A 0x800, 0, mask
0x08000401)`: its start and direction come from the weapon model's two vertices through the
attachment's drawn matrix (combat.md, Shots not aimed at the aim point), then `0x800514E8`
pulls the start back along the bullet's forward column by d - d/4 (d = max + min/4 of the
horizontal offsets from the shooter) and sets object `+0x38` 0x2000. Rockets (type 0x1773):
not reached in 1:3 (speed 130 (state 2) or 150, life doubled).

The brain's `+0xB0` and `+0xB2` (1500 and 3000 for most soldiers) are the ranges `query` tests
(sight, and the second, long sight range of queries 0x10/0x12 and the witness check; hearing uses `+0xB0`).

## Spawning (`enemy_spawn`, `0x800563F0`)

A soldier is made from a spawn record in the level data (`[a0+0x4]`), so health, accuracy and
ranges are the level designer's, per enemy; the game's difficulty parameter (`g_params+0xB`,
from `g_game+0x110`) is read by nothing in LEVEL.BIN. At most `0x80097FEC` soldiers are alive at
once (`0x80097FE8` counts them), and none while `0x800AB8C8` is 4 or more. The tests in
order: with a cap above 0, no record data or the count at the cap refuses; then `0x800AB8C8` ≥ 4
(the characters' pose buffers in use, entities.md, Memory); then a cap of 0. `dog_spawn`
(`0x80054E4C`) tests only a cap of 0, no record data, or the count at the cap. The destructors
(`enemy_destroy` `0x80056290`, the dog's `0x80054D78`) lower the count, never below 0, and give
back the pose buffer. A refused record goes to the spawn queue (levels.md); 1:3's soldier r46
at tick 5824 was refused with 4 alive under a cap of 5 by the pose-buffer test, and spawned at
5841 once a corpse's buffer was freed.

| Record | Becomes |
|---|---|
| +0x5B | Variable 8: 33, 67 or 100 (by value 0, 1, 2) |
| +0x5C | Variable 0x24: 10, 20, 1000 or 100 (by value 0, 1, 2, else) |
| +0x5E | Variable 0x10 |
| +0x61 | Health and maximum health (6.0 in 1:3, 4.0 in 2:1) |
| +0x63 | The aim error E (brain `+0xC8`) |
| +0x65 | Variable 0x26 |
| +0x66 | Variable 0x0B (30 → 1) |
| +0x6A | Brain `+0xB4` = it × 11: the field of view, a half-angle around the head bone's facing (90 → ±87°) |
| +0x6C | Brain `+0xB0`: a range (1500 in the dumps) |
| +0x6E | Variable 0x22 |
| +0x70 | Brain `+0xB2`: a range (3000; 2800 if under 10), and variables 5, 6, 7 = it, half, a third |
| +0x68 | Entity `+0x290` and `+0x294` (150) |
| +0x74 | → a sub-record: count, then hashes: +0x4 the body model, +0x8 the weapon (matched in `g_enemy_weapons`, its index into variable 0xF), +0xC the head gear (unless 0x4CBF0BA5), and with 4 entries the stick grenade (attachment model 0x61282777) and variable 0x101 = 1 (grenades allowed) |

The handlers: think `enemy_think`, `+0x370` `0x8005608C`, hit_part `enemy_hit_part`,
world_contact `0x80056250`, touch `0x8005291C`, `+0x384` `0x80056288`.

## The brain (`brain_update`, `0x80040DF4`)

The brain (0x124 bytes, `brain_alloc`) is the soldier's legs: the scripts set goals with
`behaviour` (op 0x2F, `op_behaviour`, `0x8005AC00`), the brain turns the heading (`+0x22C`) and
sets the speed scale (`+0x224`) every tick; the distance covered comes from the animation (its
root motion), not from the brain. Decompiled with m2c (`fn.py c`), the GTE parts read in
`fn.py show`; fields in `context.h`. Everything below was checked tick by tick with a
reimplementation (Checking, at the end of this section).

### Order within a tick

For a soldier or dog (`entities_update`, entities.md):

1. **The think** (`enemy_think` / `dog_think`): the scripts may set goals (`behaviour`), the
   heading (variable 0x13, the dog's 0x0E), and start or advance the animation frame, which
   applies the frame's **root movement** (`anim_root_motion`, animation.md) into the push
   `+0x1F0`, turned by the rotation matrix `+0x340` **as it was at the end of the previous tick**
   and scaled by the speed scale `+0x224` the brain left then (or 1.0 if a movement goal was
   given earlier in this think: `brain_reset_gait`). `+0x38` is cleared after the think.
2. **The move** (`entity_move`, below): angular velocity, the frame's root turn, the local
   velocity, the push, the walls (`walker_slide`, which also sorts the walls for the brain), the
   floor. Then `world_contact` (`+0x378`) if anything was hit.
3. **The brain**, unless the entity is dead (flags A 0x80000; a corpse keeps its brain pointer
   but the brain is no longer run).
4. The rotation matrix `+0x340` is rebuilt from the angles (`0x8006A76C`: `RotMatrixYXZ`, the
   position as translation, the scale `+0x270` >> 4, i.e. unscaled at 1.0). So the brain's
   forward vector (the matrix's third column, `+0x344`/`+0x350`) is the heading of the end of
   the previous tick, not the one the scripts and the root turn gave this tick.

### Goals (`+0x4`) and how scripts set them

`behaviour GOAL A B C [D]`: operand 1 the goal, 2..4 its parameters, 5 the instruction's own
immediate. Seek, path, point, node and flee first drop the other movement goals
(`goals &= ~0x5C03`, `brain_clear_moves`), free the node the soldier held
(`brain_release_node`) and reset the gait (`brain_reset_gait`: speed scale 1.0, gait 0, no
slow-down, not backwards, the walker radius `+0x2EC`, collision radius `+0x2E8` and `+0x3C8`
back to the brain's `+0xC4`, `+0xC6`, `+0xCA`, the gait speed `+0xCC` to its base `+0xD0`).
**Walk is added to the others**: it only ORs bit 4 in (and resets the gait only when walk is
then the only goal).

| Goal | Parameters | Each tick (`brain_update`) |
|---|---|---|
| 0x1 seek | target entity (`+0x10`), slow-down radius (`+0xC2`), turn rate (`+0xD4`; 0 or less gives 0x100) | `brain_move_to`: steer toward the target |
| 0x4000 flee | target entity, `+0x50` (read by nothing) | `brain_move_to` with the forward vector reversed, turn rate `+0xD4` (the last seek's) |
| 0x2 path | path id (`path_find` → index `+0xB8`), mode (`+0xBA`) | `brain_follow_path`, from the point nearest the soldier (`path_nearest_point`, 3D cheap distance), step 1 (-1 in mode 1) |
| 0x4 walk | speed (16.16, `+0x18` and its base `+0xDC`), gait (`+0xC0`), backwards (`+0xD8`); brain `+0x0` = -10 | Turn along walls (`brain_steer`, when no other movement goal is on); the walls are sorted by `walker_slide` while it is on |
| 0x1000 point | position pointer (`+0x40`: s16 x at +0, z at +4; e.g. the witness spot `0x8009817C`), variable (`+0x80`), arrive radius (`+0xA8`), slow-down radius (operand 5) | `brain_goto_point` |
| 0x800 node | AI node (`+0x3C`), variable (`+0x7C`), arrive radius, slow-down radius (operand 5) | `brain_goto_node`; the standing point and side waypoint are set once, by `brain_node_target` |
| 0x8000 | | Every goal off, node freed, gait reset |

Negative operands: -1 seek off, -2 path off, -0x800 node off (and freed), -0x1000 point off,
-0x4000 flee off, each with a gait reset; -4 walk off and -0x400 bit 0x400 off, without; 0x2000
pauses the path (bit 2 off, the point kept) and -0x2000 resumes it (bit 2 on). Others do
nothing. Path modes: **0 loops forward** (after the last point, `+0x38` 0x1000000 and back to
point 0), **1 loops backward** (before point 0, 0x800000 and on to the last), 2 back and forth
(0x800000 at the start, 0x1000000 at the end, turning round); -1 gives mode 2 without resetting
the step. (Earlier versions of this document had mode 0 stop at the end, -1 keep the mode,
every negative goal reset the gait, and walk drop the other goals: `fn.py c 0x8005AC00` and
`0x80040A0C`.)

### Steering (`brain_seek`, `0x80040180`, shared by every goal)

Called with the forward vector F (`+0x344`, 0, `+0x350`: 4096 = 1), the offset D to the goal (the
goal's integer x, z less the soldier's, s16), the turn rate R and the distance (the cheap
`max(|dx|, |dz|) + min / 4`):

1. Slow-down: with a slow-down radius `+0xC2` and the distance ≤ it, the speed scale `+0x224`
   and walk speed `+0x18` = distance × 65536 / radius (truncated), the walker radius `+0x2EC` =
   `+0xC4` × that >> 16, `+0x3C8` = `+0xCA` × that >> 16, the current gait speed `+0xCC[gait]` =
   `+0xD0[gait]` × that >> 16; beyond the radius: 1.0, `+0x18` = `+0xDC`, `+0x2EC` = `+0xC4`,
   `+0x2E8` = `+0xC6`, the gait speed its base. Without a radius nothing changes.
2. C = (Dx·Fz − Dz·Fx) >> 12 (the GTE outer product, the y row), then >> 1, or >> 2 when the
   distance is 2001 or more. C is the offset's sideways component in world units, halved; it is
   **not an angle**.
3. C < -0x20: turn by -R, or by C when C > -R (state bit 2); C > 0x20: turn by R, or by C when
   C < R (state bit 1). So a soldier closing in turns by half the sideways offset each tick and
   approaches the goal's direction gradually, never in one step. Within ±0x20: if the dot
   product D·F is negative (the goal behind) turn by +R when C ≥ 0 (bit 1), else -R (bit 2);
   otherwise no turn.
4. With the walk goal on, `brain_steer` (Walls) runs here, before the turn is applied.
5. If a turn bit is still set, heading += turn × 65536, wrapped to ±0x800 (the fraction is
   kept; `entity_move`'s root turn masks to 0..4095 and drops it).

Turn rates: seek `+0xD4` (0x100 = 22.5° per tick, 675°/s), point and node 0x100, path 0xC0
(16.9° per tick). A target that is gone or dead (flags A 0x180000) sets `+0x38` 0x10 and no
steering. The target's position is the one it has when the brain runs: the player's soldier
moves after the enemies in the list, so a soldier steers toward where the player was at the end
of the previous tick.

**Point and node goals** write the distance into their script variable every tick; within the
arrive radius they write 0 and set `+0x38` 0x400000. Distances everywhere are the cheap
`max + (mid + min) / 4`.

**Nodes** (the world's node table, `[[g_world+0x388]+0x8]`, 0x34 bytes each: +0x0 position,
+0xC kind, +0x10 approach side (1, 2; 4 none), +0x24/+0x28 facing x/z, +0x30 the handle of the
soldier holding it): the soldier stands a little behind the node along its facing (facing × 5 >>
10) and 40 units to one side (`+0xA0`); a node with an approach side is reached through a
waypoint 100 units to that side first (`+0xAC`, `+0x8C` = 1), unless the soldier is already
within 250 units; a node of kind 8 is stood on exactly (read from the code, not met in the
replay). While heading for the waypoint the soldier steers to it, gets no arrive pulse, and drops
the waypoint (`+0x8C` = 0) once within the arrive radius of it or past it (the dot of the node's
facing and the soldier's offset from the waypoint is negative). The soldier claims the node
(`+0xB6`) until its next movement goal.

**Paths** (`g_paths`, `0x8009E290`, from the BSD header's `+0x5A0` by `paths_parse`): {id, count,
points}, points of 0x14 bytes starting with s16 x, y, z. Each tick, while the current point
(`+0xBC`) is reached it advances by the step (`+0xBE`) and is tested again (several in one tick
are possible): reached means the distance moved this tick (from `+0x258`) is at least the
distance to it, or that distance is under 76, or the point is behind, i.e. (point − previous
point)·(point − soldier) ≤ 0, the previous point being current − step, taken as 1 below 0 and
count − 2 past the end. Each advance sets `+0x38` 0x400000 (or the end bits, above). Then the
soldier steers for the point at 0xC0. A mode other than 0..2 would loop for ever.

### Walls (`walker_slide`, `0x80078F60`, and `brain_steer`, `0x8003FFA4`)

`walker_slide` (the move against the level, levels.md Collision) clears state bits 4, 8, 0x10
(`+0x8`) of any brain. With the walk goal on it then gathers the walls differently: radius the
larger of the current gait speed `+0xCC[gait]` (100, or less while slowing down) and the walker
radius `+0x2EC`, the segment from eye − 16 below to the feet as usual, but **without** keeping
only the walls the move goes into; it resets the gait speed to its base. Each gathered triangle
steeper than 45° (|ny| < 0x5A80) whose normal N is not turned toward the forward vector F
(F backwards when `+0xD8`; (F·N) >> 12 < 0x2EE0, i.e. cos < 0.37) is sorted by its side,
S = (Nx·Fz − Nz·Fx) >> 12: |S| ≤ 0x5000 ahead (bit 4, slot `+0xF4`), S > 0x5000 bit 8 (slot
`+0x104`), S < -0x5000 bit 0x10 (slot `+0x114`). Each slot keeps the nearest by
|N·(x, y − eye + 16, z) + d| (unshifted, so mostly the normal's dot with the position): the plane
(N, d), that measure (+8) and S (+12); `+0xEC` the forward vector. Only then are the walls the
walker is pushed out of picked as in the plain gather (radius `+0x2EC`, the move going into
them, crossing the segment).

`brain_steer` (from `brain_seek` with the walk goal on, or alone when walk is the only movement
goal), with W the angle of a slot's normal (`ratan2(Nx, Nz)`): when both side slots are set the
farther (bigger measure) is ignored, the 0x10 one on a tie. With a wall ahead: angular velocity
y = 0, then if bit 0x10 is set the heading becomes W(ahead) + 0x400 (unless the seek turns with
bit 2) and turn bit 1 is cleared; else with bit 8, W(ahead) − 0x400 (unless bit 1) and bit 2
cleared; else by the ahead wall's S: S > 0 gives W − 0x400 and clears bit 2, otherwise W + 0x400
and clears bit 1. Without a wall ahead: bit 8 → W(bit-8 wall) − 0x400 (unless bit 1), bit 2
cleared; else bit 0x10 → W(bit-0x10 wall) + 0x400 (unless bit 2), bit 1 cleared. The heading is
set outright (no fraction), along the wall; a turn bit left set still adds the seek's turn on
top. (Bit 1 turns the heading up, bit 2 down.)

### The physical move (`entity_move`, `0x8006C83C`, for soldiers and dogs)

1. `+0x258` = position, `+0x264` = angles; angles += angular velocity `+0x234`; with an
   animation and frame ≥ 0 the frame's root turn is added and each angle masked to 0..4095,
   fraction dropped (animation.md).
2. `+0x200` += `+0x1FC` up to `+0x204` when `+0x1FC` ≠ 0, and `+0x220` = `+0x200` (so `+0x200`
   is the forward speed op 04 sets, not a fall speed); off the ground with flags A 0x20,
   `+0x21C` += 2.0 up to 75.0.
3. The local velocity (`+0x218..+0x220`; × the speed scale via the high halves when it is not
   1.0) >> 12, turned by `RotMatrixYXZ` of the new angles (sf = 1), << 12, plus the push
   `+0x1F0`, is added to the position.
4. `walker_slide` (flags A 8): as for the player (levels.md, move_sim.py), but a walker left
   inside a wall is **not** put back (only the player's branch undoes the move); with walls in
   the final gather, `+0x218` and `+0x220` are zeroed and `+0x38` 0x8000 set.
5. The floor (`floor_find` at the position; d = feet − floor in whole units, y down): no floor →
   back to `+0x3BC` (and to the old floor height if it was in the air), on the ground; on the
   ground with d ≥ -32 (the feet at most 32 above the floor, or anywhere below it) → stand on it,
   else off the ground (`+0x38` 1); in the air with flags A 0x20 → land when d ≥ 0 or -d ≤
   dy + 16 (at most 64), dy the vertical move >> 4.
6. Push cleared, `+0x3BC` = the position.

### Checking (`tools/sim/brain_sim.py`)

`brain_sim.py watch` reads a `VALOR_WATCH` log of the five character slots (entity +0x0..+0x40,
the variables, +0x1F0..+0x3D0, the instance at `+0xFBC`, and every brain as a fixed window at the
address the game allocated it: a pointer watch re-bases silently and loses the brain's constant
words) plus the witness spot, `VALOR_TRACE_STATE` (the player's position, the list order) and a
RAM dump per level (animations, paths, nodes, the seed). For each soldier and dog and each pair
of ticks drawn one after the other it takes the state at T and the script decisions of T+1 (the
heading variable, the animation frame, the goals and parameters), steps the think's root
movement, the move, the brain and the matrix as above, and compares the position (16.16, with
`+0x3BC`), the heading, pitch and roll, the matrix, the speed scale, walker radius and `+0x3C8`,
the brain's state bits, wall slots, path point and step, side-waypoint flag, gait speeds, the
distance variable and the `+0x38` pulses. `brain_sim.py pairs --dir build/vm` does the same
from full RAM dumps one tick apart.

Result over the replay (1:3, 2:1, 2:2; 69,640 entity ticks, 32,002 of them alive: 10,836
soldier and 3,461 dog ticks in 1:3, 7,801 in 2:1, 9,904 in 2:2): the position is exact to the
16.16 bit in 69,638 and the heading in 69,634. Exact in every tick they occurred: seek turns at
the full rate (1,456 ticks), by the cross product (1,789) and round for a goal behind (59), the
slow-down (25), path advances in modes 0, 1 and 2 (44), arrivals (9), `brain_steer` along walls
and the wall slots (1,106), wall pushes (429), root movement (10,851) and root turn (442),
floor steps (2,347), landings (10), ticks with no floor found (1,470, nearly all corpses, held
in place). Not met in the replay, so from the code only: the side waypoint of a node (52 node
ticks, none with one), walking backwards (`+0xD8`), kind-8 nodes, the 0x2000/-0x2000 path pause;
flee ran for one tick. 76 ticks needed a script
decision the next state doesn't show directly, found by trying it: a goal given again with the
same parameters (walk speed, path restarted at the nearest point), a gait reset in the think,
an animation restarted at the frame it showed. The 9 misses are script side: two placements by
op 0x46 (`follow`, which sets the position relative to another entity during the think: a dog
put next to the player, its fraction the player's; a soldier next to a grenade), 6 ticks seeking
a grenade (type 0x13F3, not in the watch) and one `property` change of the radii. The 51 dump
pairs (`build/vm`) agree in every field.

### Set up at spawning (`enemy_spawn`)

Sight and hearing ranges from the record (`+0x6C`, `+0x70`), aim error (`+0x63`), `+0xB4` (field of view) =
record `+0x6A` × 11, path mode 2 (back and forth), no slow-down, gait speeds 100, walk speed
1.0, probe radius 60, collision radius 150 (the record's `+0x68`, 150, goes to entity
`+0x290`/`+0x294`), no node held.

## Open

- The variables the scripts use by convention beyond soldier.md's table; what the player's
  flags B 0x400/0x800 come from is in soldier.md (level zones).
- The 0x1407 weapon (kind 2): pistol sound, MG-like animations.

Resolved (enemy_types.md): who reads `+0x38` 0x400000/0x800000/0x1000000 (script events only;
the words are one-tick pulses cleared after every think); the gait table (`+0xCC`/`+0xD0`: the
current gait speed is the brain's wall-gather radius, Walls), `+0xB4` (field of view), `+0x3C8` (radius against solid objects); the player's
weapon-block bit 1 (crouched: ranges ×0.75, v5..v7 reduced, half pitch error); events 0x0D/0x0E/
0x42 and `send_event`.
