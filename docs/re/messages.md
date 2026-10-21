# Level messages: every id, who sends it, what it does

Levels run on messages: numbered events (0..157, the script table's first word, 158 in every
level) that the engine, the trigger volumes, the model scripts and the level scripts send, and
that level scripts handle. levels.md (Level logic) has the machinery: subscriptions, filters,
the queue, the VM. This document is the catalogue, enough to wire the level scripts of a
reimplementation.

Checked by reading the senders in LEVEL.BIN (every caller of `msg_send` `0x80066EA0`,
`send_event` `0x800662CC` and `script_run_message` `0x800664A0`) and by listing every message
of every level from RAM dumps of all 24 single-player levels (warped starts at vblank 10300,
`build/lvl/M_L.bin`): `tools/recon/lvl_msgs.py` lists, per message, the scripts that handle it,
subscribe to it and send it, the record lists (enter, leave, Use) that send it, the objective
pickups and the model scripts' message ops. Meanings of the script-level conventions are read
from the scripts (`lvs.py`); they are marked *inferred* where the effect was not watched in game.
catalogue.md has the level-by-level picture (the script library by role, objectives).

## Where a message comes from, and what its context holds

A handler runs with a context (32 bytes, levels.md): `self` (+0x4) and `self_obj` (+0x10) are
the subscriber, as it was when it subscribed; `sender` (+0x8), `actor` (+0xC) and `sender_obj`
(+0x14) come from the send. The VM reads them as push forms FA/F4 (self, self_obj), FB/F5
(sender, sender_obj) and FC (actor); lvs.py prints those names.

| Source | How | sender (+0x8) | actor (+0xC) | sender_obj (+0x14) |
|---|---|---|---|---|
| A record's player list: enter `+0x38`, leave `+0x3C`, Use `+0x4C` | broadcast (`0x80083DE8`, `0x80083F64`, `record_use_send` `0x800845DC`), the list's messages in order; Use goes by the zone bits of the last tick (levels.md, Triggers) | 0 | the player's soldier | the record (its pair) |
| A record's character list: enter `+0x40`, leave `+0x44` | broadcast (`0x80083E6C`, `0x80083FE8`) | 0 | the character | the record |
| Level script `send(m)` (built-in 2) | broadcast | the script's entity (0 in record context) | 0 | the script's record (0 in entity context) |
| Level script `send_to(e, _, m)` (built-in 12) | broadcast, only to subscriptions made for `e`'s record (subscriber object = `e+0x1C`); `e` = 0: to everyone | as `send` | 0 | as `send` |
| Model script op 0x43 `send_event m` (`0x8005C210`) | broadcast | the entity running it | 0 | 0 |
| Objective pickup taken (`0x800832C0`) | broadcast | the pickup | 0 | 0 |
| Alarm raised (`op_alarm` `0x8005C710`, `alarm_from_level` `0x8005F068`): 0x2B | broadcast, once per entity spawned from the record named by the alarm's AI node (`+0x2C`) | that entity | 0 | 0 |
| Every 20 vblanks (`game_frame`): 0x59 | broadcast | 0 | 0 | 0 |
| Engine, direct (`script_run_message`): 0, 1, 2, 0x1A; model script op 0x48 | runs the handler of one script now, no subscription needed | 0 | 0 | 0 |

A broadcast reaches every subscription of that message whose filter passes (levels.md,
Subscriptions), newest subscription first; the handler is queued and runs after the frame is
drawn (usually the same frame; when the frame ends late, at the end of the next one: levels.md).
A direct run executes at once, with `self`/`self_obj` given by the caller.

## The engine's messages (fixed in the code)

| Msg | When | To | Context |
|---|---|---|---|
| 0 | **Start.** At level start (`level_start` → `0x80075210`), for every record of kind 1..6, after the states are cleared (`0x80075684`); for the player's soldier; for every entity when it spawns (`0x80074BB8`, `0x80074C54`, `0x80074CE8`) | the record's script `+0x54` (record context: self 0, self_obj the record); the entity's script `+0x64`/`+0x78` (entity context: self the entity, self_obj 0) | direct |
| 1 | **Hit.** A soldier or character hit by a projectile (types 0x13F0..0x13F9, not flags A 0x4; `enemy_hit_part`), once per hit, also for the hit that kills (the 10% rule's second `apply_damage` of the same hit sends no second one); a destructible damaged (`destructible_touch`, after `apply_damage`; type 0x1777 too) | the entity's script | direct, entity context |
| 2 | **Culled.** An entity with flags A 0x8000 (every soldier; a pickup whose record has `+0x58` bit 0; record-spawned objects of types 0x1770, 0x1776, 0x1777, 0x1778: 240 of the session's 280 culls were objects, 192 of them 2:1's type 0x1777) that has been drawn once and then stayed off screen, not near the player (`0x8006A3BC`), for `+0x294` drawn frames (soldiers: record `+0x68`, 150; pickups `+0x5E`), counted in `+0x290` (`model_draw` → `0x8006A570`), is removed by `entities_draw` during the drawing pass, before the queue runs; first its record gets 2. The test ignores the dead flag: 2:2's r56/r57 (kind 2, type 0x1777, entity script 98) lay dead for about 1100 vblanks, then were culled and their records re-armed. No dead character was culled in the session: all 66 (52 soldiers, 14 dogs) were removed outside the drawing pass with no message 2, which the record states and subscriptions show | the record's script | direct, record context |
| 0x1A | **Removed.** Any entity spawned from a record of kind 1..6 being freed (`0x800665FC`, from the soldier, dog and generic destructors): dead, culled, picked up or `remove_entity`. By then `entity_remove` has updated the record's state bits (levels.md), so the handler sees bit 0 clear. Its subscriptions, and every node whose filter entity it is, are dropped right after | the entity's script | direct, entity context |
| 0x2B | **Alarm** raised (above) | subscribers (scripts 133: filter 3, its own entity) | broadcast |
| 0x2F, 0x30, 0x31, 0x69, 0x6A | **Objective pickup** taken: pickup kinds 15..19 (records of kind 4 whose `+0x60` is 0x25C50BD4, 0x933987E0, 0x96593A51, 0x4A5B62D4, 0xD352336E); then the pickup is removed. On the disc: 2:1 (0x2F, 0x30), 2:2 (0x30), 3:1 (0x2F, 0x30, 0x31) | the player's script 13, the objective scripts | broadcast |
| 0x59 | **Timer**: every 20 vblanks (`g_vblank_timer` ≥ 10: subtract 20), 3 per second, at most one per tick, at the end of the tick after the collisions; a pause resets the counter; the scripts count it for delays | subscribers | broadcast |

Model scripts send (op 0x43, literal operands only on the disc): 0x1B (every level's soldier
set), 4, 0x1E, 0x33, 0x75 (the papers soldiers of 2:1, 2:2, 3:1), 0x74 (9:4). Op 0x48 (run a
handler of another entity directly, `0x8005C698`) appears in no model script. No other code
sends level messages.

## Every message

"Sent by": zones (record lists; e = player enter, l = player leave, u = Use, ce/cl = character
enter/leave), scripts (`s` + id; the script library ids are global, catalogue.md), models (op
0x43), engine. "Handled by": the script ids with a handler. Filters as `f5:N` (shares a tag of
type N with the sender), `f3` (sent by the subscriber itself: its own entity or record), `f0`
(everyone). Levels: where a sender exists (the handlers are in more levels).

| Msg | Meaning | Sent by | Handled by (filter) | Levels |
|---|---|---|---|---|
| 0 | Start (engine) | engine | almost every script | all |
| 1 | Hit (engine) | engine | entity scripts of soldiers and destructibles | all |
| 2 | Culled: the record's entity was removed off screen (engine). Record scripts re-arm their activation (subscribe to 3 again) | engine | record scripts 6, 8, 10, 14, 21, 26, 38, 41, 87, 115, 127, 130, 132, 143, 149, 160, 162, 164 | all |
| 3 | **Activate** the records of a group: record scripts call `activate` (spawn) | zones e (every level), l, u, ce; scripts 32, 72, 91, 93, 97, 119, 121, 122, 123, 126, 128, 133, 151, 163 (a destroyed or collected object spawns its group) | record scripts (f5:1, some f5:5/f5:7) | all |
| 4 | Alert, papers soldiers: `set_flags(0x100)` | models (2:1, 2:2, 3:1); scripts 20, 26, 27, 42, 90, 103, 113 | soldier scripts 20, 27, 42, 90, 103, 113 (f5:6), 26 (f5:6: re-arm C) | 2:1, 2:2, 3:1; 1:1, 5:2, 7:3 (26) |
| 5, 6, 0x1C | Unused: 5 is in two 2:3 enter lists with no handler; 6 and 0x1C have handlers (scripts 1, 19: `set_flags(4)`, `set_flags(0x10)`) and no sender | - | - | - |
| 7 | A member of my reinforcement group was removed: spawn (script 10) or arm activation (8) | scripts 3, 22, 36, 90 on 0x1A | 8, 10 (f5:3) | 1:1, 2:3, others with the scripts |
| 8, 9 | Player enters / leaves an **undetectable** zone: the player's script sets `+0x40` 1 / 2 (flags B 0x400 on / off, soldier.md) | zones e/l (2:3 only) | 13 (f0, checks actor = self); papers soldiers (dead code: their check needs the soldier itself in the zone) | 2:3 |
| 0xA | Stop a world animation loop (script 114: `world_anim_set(p19, 0)`) | zones l, cl | 114 (f5:1) | 2:2, 2:4, 3:2..3:4, others |
| 0xC | Chain: the next record of a pair spawns (script 26) | 26 | 26 (f3) | 1:1, 5:2, 7:3 |
| 0xD, 0xE | Player enters / leaves a **low** zone: counter from 128, `set_flags(4)` on entering, `set_flags(8)` when back to 128 (flags B 0x800, soldier.md) | zones e/l (12 levels) | 13 (f0, actor = self) | 12 levels |
| 0xF, 0x10 | A character enters / leaves a zone: the same counter for soldiers (scripts 16, 17: `set_flags(0x20)` when back to 128) | zones ce/cl | 16, 17 (f5:4) | 5:1 |
| 0x13 | My entity was culled by 0x16: re-arm (subscribe to 3 again: 6, 26, 38, 130, 160) or respawn at once while enabled (14, 41, 149) | `send_to(self, ...)` in every soldier script's 0x16 handler | record scripts 6, 14, 26, 38, 41, 130, 149, 160 (f5:1) | all |
| 0x14, 0x15 | Stop / resume respawning (scripts 14, 41, 149: a record that respawns when culled, until a count, then sends 0x14 itself) | zones e/l; 41, 149 | 14, 41, 149 (f5:5) | 10 levels |
| 0x16 | **Cull** a group: the entity tells its record (0x13) and removes itself | zones e, l, u, ce | soldier scripts (f5:1, f5:5), 122 (f5:8) | most |
| 0x17, 0x18 | Make the entity immune to 0x16 / subject to it again | zones ce (0x17, 3:1) | soldier scripts | 3:1 (0x17) |
| 0x19 | Set home: `set_flags(0x40)`, `set_home(sender)` (the soldier goes to the sender's record) | 85, 86 on 0x5F/0x60 | 16, 17 (f5:4) | 5:1 |
| 0x1A | Removed (engine). Soldier scripts report it: 7 (3, 22, 36, 90), 0x13 (15, 37), 0x64 if dead (92, 150), 0x5C + 0x8B if dead (103), 0x8B if dead (42, 113), 0x1B (83) | engine | soldier scripts | all |
| 0x1B | **Alert** a soldier group: `set_flags(0x100)` (soldier.md: alerted). Every soldier script sends it on 1 (hit); soldiers' model scripts on a shout | models (all levels); most entity scripts on 1 | soldier scripts (f5:6; papers soldiers f3 → 4); 130 (f5:6: spawn when alerted) | all |
| 0x1D | Arm activation (script 21 then waits for 3) | zones e | 21 (f0) | 1:3, 2:1, 2:2, 3:1, 4:2, others |
| 0x1E, 0x1F | Papers soldiers: `set_flags(0x20000)` / `set_flags(0x40000)` unless the sender is the soldier itself | models (0x1E); zones e, l, u | 20, 27, 42, 90, 103, 113 (f5:6) | 2:1, 2:2, 3:1 |
| 0x20 | **Detonate**: explosives of a tag-8 group explode, are removed and report (3, 0x93, 0x20 again: chain reactions, 0x68, 0x39, 0x3A...) | demolition charges 78, 107, 115, 126 (a timer after Use); destroyed objects 32, 33, 97, 98, 101, 123; objective scripts 72 | 32, 97, 98, 101, 123, 125, 139, 141, 146 (f5:8), 52, 112, 148 | 14 levels |
| 0x21..0x24 | Papers soldiers: the player enters / leaves an area tagged 0x51..0x54 (`has_tag`) | **never sent** (no list, script or model on the disc sends them; inferred dead) | 20, 27, 42, 90, 103, 113 | - |
| 0x25 | A papers soldier was hit: tell the soldiers of its areas (0x26..0x29) | 20, 27, 42, 90, 103, 113 on 1 | the same scripts (f3); 10 (f5:3: spawn) | 2:1, 2:2, 3:1 |
| 0x26..0x29 | Area alarm 1..4: the other soldiers of that area send 4. Only reachable after 0x21, so dead in practice | the same scripts on 0x25 | the same scripts | - |
| 0x2B | Alarm raised (engine): the alarm box's script spawns its group (3) | engine | 133 (f3) | 2:2, 3:1, 3:3 |
| 0x2C, 0x2D | 9:4: 0x2C the launch console used (the sequence starts, the objective completes); 0x2D `set_home` to the sender | 0x2C zone u; 0x2D script 112 | 39, 72 | 9:4 |
| 0x2E | **Used** a switch, valve or lever (type 0x1775): `set_flags(2)` (the model turns) and report (0x72, 0x5C, 0x5A, 0x5B) | zones u | 40, 81, 106, 108, 139, 152 (f5:6), 143 | 3:4, 4:3, 5:1..5:3, 9:1 |
| 0x2F, 0x30, 0x31 | Papers pickups (engine): the player's script keeps the highest clearance 1, 2, 3; objective scripts complete "obtain papers" | engine; zone e (2:1, 0x31) | 13, 46, 47, 50, 55 (f0) | 2:1, 2:2, 3:1 |
| 0x32 | A soldier asks for papers: the player's script answers 0x34/0x35/0x36 (clearance 1/2/3) or 0x6B (none) | 42, 103, 113 on 0x33 | 13 (f0) | 2:1, 2:2, 3:1 |
| 0x33 | The soldier's model script starts an inspection: the soldier subscribes to the answers and sends 0x32 | models | 42, 103, 113 (f3) | 2:1, 2:2, 3:1 |
| 0x34, 0x35, 0x36, 0x6B | Papers answer: every soldier waiting for one sets `+0x40` 1, 2, 4 (clearance 1, 2, 3) or 8 (none) (soldier.md: the verdicts) | 13 | 42, 103, 113 (f0, subscribed only during an inspection) | 2:1, 2:2, 3:1 |
| 0x37..0x40 | **Objective events**, per level (levels.md, Objectives by level). One of them only shows "end of level located" in 2:1 and 4:2 (0x3C), 5:3 (0x3F), 5:4 (0x39), 7:3 (0x3E) | zones e, u, ce; the objective script itself; object scripts (39, 99, 129, 141, 146, 95...) | the level's objective script | all |
| 0x59 | Timer (engine) | engine | 29 scripts (f0) | all |
| 0x5A, 0x5B, 0x89, 0x8A, 0x71 | **Open** a door group: door entity scripts `set_flags` (1, 2, 8, 0x10/0x20, 0x40, 0x80: the model's opening, objects.md), sound, report 0x61 (100, 124 toggle and report 0x62 on the second) | zones u, ce, cl; 50, 58, 87, 108, 152, 164 | 57, 74, 75, 80, 88, 89, 100, 104, 124, 138, 140 (f5:7) | 13 levels |
| 0x5C | **Unlock**: door entity scripts (80, 88, 124, 140) start listening to 0x5B/0x5A; door record scripts (87, 164) remember it; also objectives (heating tanks 5:2, vats 5:3) and charges 98, 107 | zones e, u; objective scripts; 87 (on 0x78), 106, 132, 139, 164 | 58, 59, 62, 80, 87, 88, 98, 99, 107, 124, 126, 140, 164 | 18 levels |
| 0x5D, 0x5E | Switch: 0x5D nothing (handler empty); 0x5E stop accepting Use | 143 (0x5E) | 40, 81, 106, 108, 139, 152 | 5:2, 5:3 |
| 0x5F, 0x60 | A character enters a zone: relay to 0x19 (set home) | zones e/l (0x60) | 85, 86 (f5:4) | 5:1 |
| 0x61, 0x62 | A door opened / closed (report from the door entity): the door record stops / resumes the "locked" prompts | door entity scripts | 87, 164 (f5:7), 54 (4:1 objective), 116 | 14 levels |
| 0x63 | **Collected / walked into** an object: remove it, spawn its group (3), 0x93 (91); 128, 151, 166 after a delay; 136 → 0x8E | zones e, u (kind-2 object volumes); 127 | 91, 128, 136, 151, 166 (f5:1) | 16 levels |
| 0x64 | A member of a counted squad died (scripts 92, 150 on 0x1A when dead): objective counters (1:2 Gestapo, 7:2 demo squad) | 92, 150 | 44, 62 | 1:2, 7:2 |
| 0x65 | Use on a bomb (7:3): report 0x93, 0x67, sound | zones u | 96 (f5:8) | 7:3 |
| 0x66 | Explosion at the player (script 13) | **never sent** | 13 | - |
| 0x67 | A target destroyed or disarmed, counted (2:3 fuel containers, 7:3 bombs) | 96, 111 | 48, 63 | 2:3, 7:3 |
| 0x68 | A target destroyed or a charge placed, counted by objective scripts; stops script 114's animation | 97 (exploded), 115 (charge activated), 125 | objective scripts, 114 (f5:8) | 11 levels |
| 0x6C | "This door is locked" text and sound (the player's script) | zone u (2:1) | 13 | 2:1 |
| 0x6D | Toggle `set_flags(0x8000)` / `set_flags(0x10000)` (2:1's script 20, sent to itself at spawn) | 20; zones ce/cl (1:1) | 20 (f5:1) | 2:1, 2:2, 3:1 |
| 0x6E, 0x6F, 0x70 | 5:2 generators 1..3 used | zones u | 58 | 5:2 |
| 0x72 | A switch was used (objective: 3:4 ballast, 5:1) | 40, 108, 139, 152 | 53, 108 (f5:8) | 3:4, 5:1, 9:1 |
| 0x74 | Next stage: 2:4's four placed charges start their countdown (115); 9:4's script 39 sends 0x3F and removes itself. Sent with no handler in 2:2, 5:2, 9:1 | objective scripts 47, 49, 58, 69; model (9:4) | 39, 115 | 2:4, 9:4 |
| 0x75 | The papers soldier's model script: subscribe to 0x33 | models | 42, 113 (f3) | 2:1, 2:2, 3:1 |
| 0x76 | Sent when a 7:2 glass breaks; nothing handles it | 117 | - | 7:2, 7:3 |
| 0x78 | Door record: "am I unlocked?" (sent by itself on activation; replies 0x5C) | 87, 164 | 87, 164 (f5:7) | 13 levels |
| 0x79 | 2:4: the charges' countdown ran out: detonate | 115 | 101, 129 | 2:4 |
| 0x7A..0x88 | Raise alarm 80..94 (`level_set(4, n)`) | zones u (alarm switches); 50 (0x7F) | 131 (f0) | 2:2, 3:1, 3:3 |
| 0x8B | A dead papers soldier: door scripts start listening for 0x8A/0x89 | 42, 103, 113 on 0x1A | 74, 75, 89, 138 (f5:7) | 2:1, 2:2, 3:1 |
| 0x8C | **Exit reached**: if the objective counter is full, `level_set(2, 1)` (primary objectives complete), else "not all objectives met" | zones e (3:1: Use) | objective scripts | 22 levels |
| 0x8D | **End the level**: `level_set(3, 1)` | zones e (inside 0x8C's volume) | objective scripts | 22 levels |
| 0x8E, 0x8F, 0x92 | 3:1: wrench found, the engine used (with or without the wrench), engine jammed | 136; zone u; 50 | 50, 137 | 3:1 |
| 0x93 | Swap: the sender is removed and this record's entity spawns (destroyed or collected objects replaced by their wreck), 0x5C | explosives and collectibles 32, 91, 96..98, 129, 137, 139, 163, 166 | 132 (f5:8) | 19 levels |
| 0x94, 0x95, 0x96 | Looping sound: start (handle kept) / stop / stop for good | zones e/l; 44, 146 | 142, 145, 160 | 1:2, 3:4, 4:2, 5:4 |
| 0x97 | Step a world animation on Use (5:2, 9:4) | zones u | 147 (f3) | 5:2, 9:4 |
| 0x98 | `set_flags(0x200000)` on soldiers of script 34 | 72 (9:4) | 34 (f0) | 9:4 |
| 0x99, 0x9A | Door record prompts: 0x99 (entering the door's own volume) "press action button to open door" while unlocked; 0x9A (Use) "this door is locked" while locked, at most every 12 timer ticks. Script 163 (an object): 0x99 a text, 0x9A remove it, spawn its group (3), 0x93; 137 a text | zones e, u (the record's own lists: f3) | 87, 164 (f3), 137, 163 (f5:1) | 15 levels |
| 0x9B | 2:4: re-activate a charge | zones e | 115 (f5:1) | 2:4 |
| 0x9C | 3:2: cancel a record's activation | zones e, u | 162 (f5:6) | 3:2 |
| 0x9D | Reset the player's low-zone counter | zones e | 13 | 7:3 |

Messages 0xB, 0x11, 0x12, 0x2A, 0x41..0x58, 0x69, 0x6A (pickup kinds 18, 19 are placed in no
level), 0x73, 0x77, 0x90, 0x91 and 0x9E..0x9D (the last id is 0x9D) appear in no script, list
or model script of the 24 levels.

## Conventions a reimplementation needs

- **Tag groups.** Most traffic is "to everyone who shares a tag of type N with the sender"
  (filter 5; a tag is `type << 16 | id`, record `+0x48`). Types on the disc: 1 (spawn groups:
  0x3, 0x16, 0x13; 3256 tags), 3 (reinforcements: 7, 0x25), 4 (character zones: 0xF, 0x10,
  0x19), 5 (respawn gates 0x14/0x15, 0x16), 6 (alert groups 0x1B, 4; switches 0x2E), 7 (doors
  0x5A..0x5C, 0x61, 0x78, 0x89..0x8B), 8 (explosives 0x20, 0x68, 0x93, 0x65, 0x72).
- **Two scripts per record.** The record script (`+0x54`) runs in record context: it decides
  when the record spawns (`activate` on 3, re-arm on 2 or 0x13). The entity script (`+0x64`,
  kind 3 `+0x78`) runs in entity context from the spawn to the removal (0x1A). The player
  start's record script is the level's objective script, its entity script the player's (13).
- **The level ends** by 0x8D (`level_set(3, 1)`), which the exit volume sends whatever the
  objectives: 0x8C, a larger volume around it, only decides whether the primary objectives
  count as complete (the rating, levels.md).

## Checked against a reimplementation

`tools/sim/level_vm.py` (levels.md, Level logic) drives a reimplementation of the level logic
with the engine's own messages as this document lists them (0 at start and spawn, 1 per hit, 2
on a cull, 0x1A on every removal, 0x2B on an alarm, 0x59 from the counter, the objective
pickups, the zones' enter/leave/Use lists from the player's and the soldiers' positions) and
compares the 128-entry queue with the original's at every frame of 1:3, 2:1 and 2:2: every
queued context (code address, subscriber, sender, actor, objects, message, script) and the
ring indices agree on all 21 676 frames. The messages model scripts send (op 0x43: 0x1B, 4,
0x33, 0x75 in the session; 34 contexts) are taken from the ring as the engine's input. Two
things the session showed that weren't in this document: the cull also takes dead objects (2
above), and the queue isn't always drained by the end of a frame (157 frames, levels.md).

## Names

```
8006692C msg_register               # (msg, entity, object, filter, handle, a, b, pc, script): add a subscription (0x24-byte node)
80066C14 msg_unregister             # the same arguments; filter 0 removes all of this entity's/object's for the message
800665BC msg_start                  # (entity, object): script_run_message(entity, object, 0)
800665FC msg_entity_removed         # (entity): message 0x1A to its script, then drops its subscriptions
80066790 msg_lists_init             # one subscription list per message id; the node pool at 0x800AD460 (1024 x 0x24)
80066110 level_scripts_init         # g_level_scripts from the header, the built-in table
8006A204 msg_still_subscribed       # a queued context runs only if its subscription still exists
80069514 bi_register_msg            # level built-in 0
80069714 bi_unregister_msg          # level built-in 1
800698F0 bi_send                    # level built-in 2: broadcast from this script
80069A08 bi_set_flags               # level built-in 3 -> 0x800660F4 (entity +0x40 |=)
80069A74 bi_activate                # level built-in 4 -> record_activate
80069ACC bi_remove_entity           # level built-in 5
80069B14 bi_explosion               # level built-in 6 -> lvl_explosion
80069D5C bi_entity_test             # level built-in 7: the level function table 0x80098630
800536F4 entity_test                # (entity, object, n): 0 is the player (flags A 1), 1 is dead (flags A 0x20000000)
80069FC4 bi_show_text               # level built-in 8
8006A038 bi_has_tag                 # level built-in 9
8006A0C8 bi_tags_match              # level built-in 10
8006A184 bi_set_home                # level built-in 11
80069968 bi_send_to                 # level built-in 12: broadcast, only to the target's record's subscriptions
80069D04 bi_level_set               # level built-in 13
80069DEC bi_objective_add           # level built-in 14
80069E44 bi_objective_remove        # level built-in 15
80069E84 bi_objective_complete      # level built-in 16
80069EC4 bi_objective_is_complete   # level built-in 17
80069F2C bi_world_anim_step         # level built-in 18
80069B88 bi_sound                   # level built-in 19
80069CC4 bi_sound_stop              # level built-in 20
80069F6C bi_world_anim_set          # level built-in 21
8006488C record_prop_kind1          # push_property readers, by record kind (levels.md, Record properties)
80064AE4 record_prop_kind2
80064EC8 record_prop_kind3
800654E8 record_prop_kind4
800658A0 record_prop_kind5
80065B24 record_prop_kind6
80075210 records_start_scripts      # message 0 to every record of kind 1..6
80075684 records_clear_state        # every pair's state &= ~3 at level start
800754CC player_start_record        # the 0x8877B646 record with +0x34 == player number
80087294 player_spawn               # the camera entity and the player's soldier from the player start
80074BB8 record_spawn_type          # (type, pair): an entity of that type for the record (the player, ladders), message 0
80074C54 record_spawn_model         # kinds 2, 3, 6: spawn_from_model, message 0
80074CE8 record_spawn_pickup        # kind 4: pickup_spawn, its model script, message 0
80074E10 record_enter_kind0         # kind-0 records by class: level end, chunk streaming, ladder
800832C0 item_pickup_touch          # pickup kinds 15..19: messages 0x2F, 0x30, 0x31, 0x69, 0x6A
800833A0 pickup_spawn               # (record, kind hash): the type-101 entity from the table at 0x8009E8E8
8008335C pickup_kind_find           # hash -> table index and model hash
8006A570 cull_countdown             # off screen and not near the player: +0x290 -= 1
8009D5B0 g_msg_subscriptions        # per message id, the list of subscription nodes
8009D5B8 g_msg_head                 # queue read index (0..127)
8009D5BC g_msg_tail                 # queue write index
80098630 g_level_functions          # entity_test's table: 0x800539A8 is_player, 0x800539B4 is_dead
```
