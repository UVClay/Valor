# Level catalogue: every level, what it uses, what is not yet explained

One section per single-player level (24: MSN1..MSN5, MSN7, MSN9; MSN6 and MSN8 don't exist and
MSN9 has no LVL3), MSN12's seven split-screen arenas in brief, and at the end the list of
everything a level uses that no document in `docs/re/` explains yet ("Not yet covered").

**How it was made.** Everything below is read from the level files
(`extracted/moh/DATA/MSNm/LVLl/m_l.BSD`) by `tools/recon/cat_lvl.py` (header, records, models,
model scripts, level scripts), `cat_scripts.py` (the level-script library across levels) and
`cat_obj.py` (what completes each objective). The level scripts are decoded from the file with
`lvs.py`'s decoder (the script table is position independent). Checked against the RAM dumps of
all 24 levels in `build/lvl/` (warped starts): every script's code is byte-identical in the
file and in RAM (only the script variables differ). Texts (objectives, messages) come from
LEVEL.BIN's message table (`0x80098188`, one table for all levels, read from any dump);
pickup and weapon identities from LEVEL.BIN's pickup table (`0x8009E8E8`), weapon table
(`0x80093640`) and enemy weapon table (`0x80097FF0`). Claims about meaning that rest on reading
level scripts rather than code are marked *inferred*.

## Reading the tables

### Record classes (BSD `+0x53C`, levels.md)

| Class | Kind | What | Doc | Levels |
|---|---|---|---|---|
| 0x4F292333 | 3 | Soldier / dog spawn | levels.md, enemies.md | all SP |
| 0xB81C10E3 | 1 | Trigger volume (enter/leave/Use lists) | levels.md | all SP, 12:5, 12:7 |
| 0xBD12927E | 4 | Pickup | levels.md, player.md | all |
| 0x166A85FB | 2 | Object of a model (door, destructible, prop...) | levels.md | all SP |
| 0xD31F988A | 6 | Object of a model, like kind 2 (almost always type 0x1777) | levels.md | 17 SP |
| 0x413B09BF | 5 | Volume (Use zones, tags) | formats.md | 13 SP |
| 0x4D078F23 | 0 | Chunk streaming volume | formats.md | 22 SP (not 2:4, 9:4) |
| 0x3011B913 | 0 | Ladder volume | levels.md, movement.md | 20 SP (not 1:1, 2:4, 5:4, 7:2), all MSN12 |
| 0x8877B646 | 2 | **The player start** (one per SP level, two in MSN12): no template; +0x64 is script 13 (the player's), +0x54 the level's objective script; `0x800754D4` looks it up by its `+0x34` (the player number, *inferred*: no direct caller found) | **this doc only** | all |
| 0xFD092133 | 0 | MSN12 only (4..5 per arena); no code in LEVEL.BIN names it (read by LEVEL2P, *inferred*: spawn points) | UNDOCUMENTED | MSN12 |
| 0xD1BA7D1E | 0 | Level end (code in `0x80074E10`) | levels.md | **none on the disc** |

Records per level (counts by class; `cat_lvl.py`):

| Level | ladder | stream | trigger k1 | object k2 | player | soldier k3 | pickup k4 | volume k5 | object k6 | total |
|---|---|---|---|---|---|---|---|---|---|---|
| 1:1 | - | 10 | 37 | 20 | 1 | 28 | 11 | - | 2 | 109 |
| 1:2 | 2 | 12 | 39 | 13 | 1 | 52 | 23 | - | 8 | 150 |
| 1:3 | 1 | 10 | 33 | 3 | 1 | 49 | 23 | - | 1 | 121 |
| 2:1 | 2 | 4 | 26 | 5 | 1 | 10 | 8 | 2 | 47 | 105 |
| 2:2 | 1 | 2 | 36 | 17 | 1 | 14 | 5 | 1 | 4 | 81 |
| 2:3 | 9 | 10 | 51 | 30 | 1 | 40 | 24 | - | - | 165 |
| 2:4 | - | - | 17 | 19 | 1 | 19 | 3 | 4 | - | 63 |
| 3:1 | 2 | 6 | 68 | 20 | 1 | 37 | 17 | - | 8 | 159 |
| 3:2 | 10 | 1 | 36 | 16 | 1 | 25 | 20 | 2 | 47 | 158 |
| 3:3 | 2 | 8 | 40 | 22 | 1 | 49 | 20 | 2 | 55 | 199 |
| 3:4 | 5 | 4 | 23 | 8 | 1 | 28 | 9 | 1 | - | 79 |
| 4:1 | 7 | 26 | 68 | 18 | 1 | 56 | 43 | - | - | 219 |
| 4:2 | 10 | 16 | 67 | 52 | 1 | 67 | 43 | - | 2 | 258 |
| 4:3 | 4 | 10 | 39 | 2 | 1 | 54 | 21 | - | - | 131 |
| 5:1 | 3 | 15 | 94 | 45 | 1 | 52 | 27 | 12 | 53 | 302 |
| 5:2 | 2 | 8 | 39 | 19 | 1 | 42 | 16 | 15 | 2 | 144 |
| 5:3 | 4 | 8 | 48 | 42 | 1 | 52 | 22 | 1 | 8 | 186 |
| 5:4 | - | 6 | 47 | 55 | 1 | 47 | 11 | 4 | 1 | 172 |
| 7:1 | 5 | 15 | 47 | 17 | 1 | 49 | 28 | 4 | 4 | 170 |
| 7:2 | - | 6 | 33 | 12 | 1 | 35 | 18 | - | 18 | 123 |
| 7:3 | 2 | 11 | 39 | 25 | 1 | 31 | 17 | - | 23 | 149 |
| 9:1 | 8 | 6 | 44 | 27 | 1 | 59 | 24 | 2 | - | 171 |
| 9:2 | 2 | 10 | 48 | 23 | 1 | 55 | 19 | - | 2 | 160 |
| 9:4 | 1 | - | 40 | 11 | 1 | 12 | 13 | 1 | - | 79 |

(object k2 excludes the player start.) Record lists used (all SP levels): kind 1: player enter
968, player leave 105, character enter 54 / leave 55, Use 27, tags 970; kind 2: player enter
40 (objects collected by walking into them), Use 109, character enter 3; kind 4: player enter
12 (pickups that also send a message), kind 5: Use 6, enter 3, leave 7. The `+0x50` list is set
on one record in the whole disc (3:3 record 161, kind 2, {3}) and nothing reads it (the zone
code `0x80084400` and its callers never load `+0x50`). Kind-0 records have no lists:
`entity_records_register` (`0x8007529C`) relocates `+0x38..+0x50` for kinds 1..6 only.

### Entity types spawned by records

| Type | What | Doc |
|---|---|---|
| 0x1402 | Soldier | enemies.md, soldier.md (1:3's and 2:1's script sets only, below) |
| 0x1405 | Dog (1:3 only) | objects.md (inferred there) |
| 0x1770 | Destructible | objects.md |
| 0x1771 | Spinning prop (1:1 only, one) | objects.md (inferred) |
| 0x1772 | Mounted gun | objects.md, combat.md |
| 0x1774 | The V2 rocket (9:4 only) | objects.md (inferred: "rocket lifting off") |
| 0x1775 | **Switch / valve / lever**: Use sends message 0x2E, the level sets `+0x40` bits 1/2/4 that its model scripts animate | objects.md has the constructor only; the role is **inferred here** |
| 0x1776 | Door (and other moving parts: hatch, gate, lift) | objects.md (2:1's door model only) |
| 0x1777 | Breakable glass/bottles: one hit, `world_anim_step` of the record's property 29, debris, removed (scripts 84, 117, 119) | objects.md (bullets pass) |
| 0x1778 | Scenery; also the **placed demolition charge** (below) | objects.md |
| 0x1779 | Box; in 1:2, 3:4, 4:2, 5:4 a looping-sound emitter (scripts 142, 145) | levels.md (one line) |
| 0x177A | Inert solid (4:1, one) | levels.md (one line) |
| 0x65 | Pickups (kind 4): health 0..2, ammo 3..14, **items 15..19** | player.md (health, ammo); items: **UNDOCUMENTED** before this doc |

Types seen live in the 24 dumps besides these: 1, 2, 3 (player, camera, world), 0x65, and
**0x23** (9:1: think `0x800701DC`, draw `0x8006F7BC`; made by `0x800702E0` ← `0x800705EC` ←
`impact_sound` `0x80043140`: an impact effect, *inferred*; no doc lists it). 0x2C (alarm,
audio.md), 0x31 (explosions), 0x4C (ladder) and projectiles are covered elsewhere.

**Item pickups** (pickup table entries 15..19, touch `0x800832C0`): taking one sends a level
message and removes it: 15 → 0x2F, 16 → 0x30, 17 → 0x31, 18 → 0x69, 19 → 0x6A (read from the
code). On the disc: 2:1 (15, 16), 2:2 (16), 3:1 (15, 16, 17), the papers levels. The player's
script 13 turns 0x2F/0x30/0x31 into a clearance level 1/2/3 (the Ausweis colours blue, red,
yellow by the objective texts); 2:3's record 73 is a pickup whose hash is 0 (no table entry:
`0x8008335C` leaves the index unset; a data bug, behaviour undefined).

### Weapons by level (the type-1 models, `level_load`; player.md's numbering)

| Levels | Weapons |
|---|---|
| 1:1 | Garand (12), grenades (3) |
| 1:2 | Garand, Thompson (1), grenades |
| 1:3 | Garand, Thompson, shotgun (4), grenades |
| 2:1 | silenced pistol (9), papers (7) |
| 2:2 | papers, silenced pistol, **sniper rifle (5)** |
| 2:3, 2:4 | silenced pistol, sniper rifle, Thompson, stick grenades (10) |
| 3:1 | silenced pistol, Thompson, papers |
| 3:2..3:4 | .45 pistol (0), Thompson, sniper rifle, stick grenades |
| 4:1..4:3 | Garand, SMG (6), shotgun, grenades |
| 5:1..5:4 | pistol (8), shotgun, BAR (11) |
| 7:1..7:3 | Garand, SMG, bazooka (2), stick grenades |
| 9:1, 9:2, 9:4 | shotgun, BAR, bazooka, grenades |

### The level-script library

Script ids are **global**: the same id is the same code in every level that has it (all 134
ids used on the disc have exactly one body across the 31 levels; `cat_scripts.py`). A level
holds only the ids it uses (8..29 per SP level, 2 per MSN12 arena), in a table of 167 slots;
the table's first word (158 everywhere) is the number of **message ids**, not of scripts
(`msg_filter_dispatch` rejects ids ≥ it). Each record has two script references: `+0x54`, run
for the record itself (before and between spawns: activation), and `+0x64` (kinds 2, 4, 6) or
`+0x78` (kind 3), run for its entity. Roles, from reading them:

| Ids | Used by | Role (*inferred* from the scripts) |
|---|---|---|
| 6, 38 | `+0x54` of almost every spawnable record | Activation: on message 3 from the record's tag group `activate()`; 38 also activates at level start; message 2 or 0x13 re-arms it |
| 8, 10, 14, 21, 26, 41, 127, 130, 143, 149, 160, 162 | `+0x54` of soldiers, objects | Activation variants: by other messages (7, 0x1B, 0x1D, 0x25, 0x2E), with a gate (0x14/0x15), every N re-arms (41, 149), activating a partner (26, 127) |
| 13 | the player start (`+0x64`) | The player: low zones (0xD/0xE, counted from 128, `set_flags` 4/8), undetectable zones (8/9 → `set_flags` 1/2), papers clearance (0x2F..0x31 → 1..3; 0x32 asks → 0x34/0x35/0x36 or 0x6B), 0x66 an explosion at the player, 0x6C "THIS DOOR IS LOCKED" (soldier.md, concealment) |
| 43..63, 69, 70, 72 | `+0x54` of the player start | The level's objectives (one per level, below) |
| 1, 3, 4, 5, 15, 16, 17, 18, 19, 22, 24, 25, 30, 31, 34, 36, 37, 73, 82, 83, 92, 150 | soldiers (`+0x78`) | Soldier groups: on start `set_flags` (0x8, 0x10, 0x200000, 0x2000000...), hit (1) → 0x1B to the group → `set_flags(0x100)` (alert), 0x16/0x17/0x18 hand-off between group members, 0x1A (removed) → a count message (92, 150: 0x64 when dead) |
| 20, 27, 42, 90, 103, 113 | soldiers in 2:1, 2:2, 3:1 | Papers checks: area tags 81..84 tracked by messages 0x21..0x24, the check chain 0x75 → 0x33 → 0x32 (to the player) → 0x34/0x35/0x36/0x6B → `set_flags` 1/2/4/8 (soldier.md's verdicts) |
| 75, 74, 80, 88, 89, 100, 104, 124, 138, 140 | doors (0x1776, `+0x64`) | Door: open on a message from its tag-7 group (0x5A, 0x5B, 0x71, 0x89, 0x8A) with `set_flags` 1/2/8 (the model's swing/slide, objects.md), sound, 0x61 |
| 87, 164 | `+0x54` of doors | Door controller: on activation (3) opens its door (0x5A, 0x5B) and sends 0x78; 0x5C marks it unlocked (a key), after which 0x78 forwards 0x5C; 0x99 shows "PRESS ACTION BUTTON TO OPEN DOOR", 0x9A "THIS DOOR IS LOCKED" unless unlocked (again after 12 timer messages); 0x61 (sent by the door when it opens) and 0x62 turn the prompts off and on |
| 11, 33, 93, 95, 111, 121, 122, 133 | destructibles (0x1770) | On hit (1): if destroyed (`level_func(.., 1)`), explosion (0, 3, 5, 7 or 8), remove, a message (3, 0x20, 0x38, 0x67...) |
| 84, 117, 119 | 0x1777 | Glass: `world_anim_step(p29)`, debris, remove |
| 78, 107, 115, 126 | `+0x54` of 0x1778 (the charge model) | **Demolition charge**: activated by Use (the model appears), then after 11 (14) timer messages (3.7 s, 4.7 s) sends 0x20 to its group |
| 32, 97, 98, 101, 123, 125, 129, 139, 141, 146, 148, 155, 156, 158 | charge targets | On 0x20: explosion(5) (or 4: 2400-radius), remove, `world_anim_step`, report (0x68, 0x93, 0x3F...) |
| 40, 81, 106, 108, 139, 152 | switches (0x1775) | On 0x2E (Use): `set_flags(2)` (the model turns), report (0x5C, 0x72...) |
| 91, 128, 136, 151, 163, 166 | objects with an enter volume | Collectibles: on 0x63 (player walks in) remove, activate the group (3), 0x93 |
| 99, 114, 141, 147, 148 | kind-5 volumes, objects | World animations: `world_anim_step/set` of the record's property 19 on Use or on the timer |
| 131 | kind 1 (2:2, 3:1, 3:3) | Alarms: messages 0x7A..0x88 → `level_set(4, 80..94)` |
| 142, 145, 160 | 0x1779 | Looping sounds: 0x94 start (mode 3, handle kept), 0x95/0x96 stop |
| 9 | kind-2 records | `explosion(2)` (a smoke emitter) on activation |
| 39, 112, 155, 156, 158 | 9:4 | The V2 launch (below) |
| 85, 86, 116, 137 | single levels | Small relays (0x5F/0x60 → 0x19; a 12-tick sound; texts) |

**Messages** the engine itself sends (everything else is a level convention between scripts):

| Message | Sent by | Doc |
|---|---|---|
| 0 | Every record with a script of kind 1..6 at level start (`0x80075210` → `0x800665BC`), and every entity when spawned | levels.md (spawn only) |
| 1 | A soldier hit by a projectile (types 0x13F0..0x13F9, `enemy_hit_part` → `script_run_message`), a destructible hit (`destructible_touch`) | objects.md (destructible only) |
| 2 | **Despawn**: `entities_draw` removes an entity flagged A 0x8000 whose off-screen counter `+0x290` ran out, and runs message 2 for its record | **UNDOCUMENTED** (below) |
| 0x1A | An entity removed (`0x800665FC` from the soldier, dog and model destructors), which also drops all its subscriptions | UNDOCUMENTED |
| 0x2B | Alarm raised, to the alarm zone's entities (`op_alarm`, `alarm_from_level`) | audio.md |
| 0x2F, 0x30, 0x31, 0x69, 0x6A | Item pickups 15..19 | this doc |
| 0x59 | Every 1/3 s (`game_frame`) | objects.md |
| 4, 0x1B, 0x1E, 0x33, 0x75 | Soldier model scripts, op 0x43 | soldier.md (as numbers) |
| 0x74 | The 0x1774 model's script, op 0x43 | - |
| anything | Record lists (enter/leave/Use), `send`/`send_to` | levels.md |

## Summary

| Level | TSP | Soldiers (dogs) | Weapons of soldiers | Destr. 0x1770 | Glass 0x1777 | Doors | MGs | Switches | Ladders | Stream | Low zones | Use lists | Soldier scripts |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1:1 | 9 | 28 | rifle | 13 | 2 | 3 | 3 | - | - | 10 | 1 | 6 | 235 |
| 1:2 | 10 | 52 | rifle, SMG | 12 | 8 | - | - | - | 2 | 12 | - | - | 236 |
| 1:3 | 9 | 33 (16) | rifle, SMG | - | 1 | - | - | - | 1 | 10 | - | - | 218 (soldier.md) |
| 2:1 | 6 | 10 | SMG, pistol | - | 47 | 5 | - | - | 2 | 4 | 2 | 7 | 312 (soldier.md) |
| 2:2 | 5 | 14 | SMG, rifle, pistol | 5 | 6 | 2 | - | - | 1 | 2 | - | 10 | 317 |
| 2:3 | 8 | 40 | SMG, rifle | 19 | - | 3 | 4 | - | 9 | 10 | 8 (+1 undetectable) | 4 | 236 |
| 2:4 | 4 | 19 | rifle, SMG, pistol | - | 6 | - | - | - | - | - | - | 4 | 210 |
| 3:1 | 7 | 37 | SMG, pistol | 14 | 8 | 4 | - | - | 2 | 6 | - | 17 | 307 |
| 3:2 | 5 | 25 | rifle, pistol, SMG | 12 | 47 | - | - | - | 10 | 1 | - | 4 | 210 |
| 3:3 | 8 | 49 | pistol, SMG, rifle | 21 | 55 | - | - | - | 2 | 8 | 1 | 7 | 218 |
| 3:4 | 6 | 28 | pistol, SMG | - | - | - | - | 1 | 5 | 4 | - | 7 | 213 |
| 4:1 | 15 | 56 | rifle, SMG | - | - | 2 | 12 | - | 7 | 26 | 15 | 3 | 227 |
| 4:2 | 11 | 67 | rifle, SMG | 49 | 2 | 2 | - | - | 10 | 16 | 2 | 3 | 221 |
| 4:3 | 9 | 54 | rifle, SMG | - | - | - | - | 2 | 4 | 10 | 2 | 2 | 218 (= 1:3's) |
| 5:1 | 11 | 52 | SMG, rifle, pistol | 34 | 53 | 1 | - | 2 | 3 | 15 | 12 | 11 | 238 |
| 5:2 | 8 | 42 | SMG, pistol, rifle | 8 | 2 | 2 | 1 | 2 | 2 | 8 | 2 | 12 | 232 |
| 5:3 | 8 | 52 | SMG, pistol | 35 | 8 | - | - | 5 | 4 | 8 | - | 6 | 232 |
| 5:4 | 6 | 47 | rifle, Panzerfaust | 45 | 1 | 1 | 2 | - | - | 6 | - | 8 | 226 |
| 7:1 | 10 | 49 | SMG, rifle, Panzerfaust | 12 | 4 | - | 1 | - | 5 | 15 | - | 4 | 236 |
| 7:2 | 7 | 35 | rifle, SMG, Panzerfaust | 11 | 18 | 1 | - | - | - | 6 | - | 1 | 221 |
| 7:3 | 10 | 31 | SMG, Panzerfaust | 25 | 23 | - | - | - | 2 | 11 | 4 | 12 | 235 |
| 9:1 | 7 | 59 | SMG | 19 | - | 6 | - | 1 | 8 | 6 | - | 9 | 227 |
| 9:2 | 9 | 55 | SMG | 22 | 2 | 1 | - | - | 2 | 10 | 1 | 2 | 218 (= 1:3's) |
| 9:4 | 4 | 12 | SMG, Panzerfaust | 9 | - | - | 1 | - | 1 | - | 1 | 3 | 226 |

"Soldiers" counts kind-3 records (a record can spawn again, below). Grenade throwers (4-entry
templates, enemies.md) are most soldiers from MSN4 on: 4:1 53, 4:2 67, 4:3 47, 5:1 51, 5:2 25,
5:3 40, 5:4 45, 7:1 48, 7:2 35, 7:3 11, 9:1 57, 9:2 55, 9:4 7; none before. "Low zones" are
kind-1 boxes whose player-enter list sends 0xD (soldier.md's 0x800 "low zone"); 2:3 has the
only undetectable zone (8/9, flag 0x400) on the disc. The soldier script set is one per level
(all soldier models of a level inherit it): 20 distinct sets; 1:3's is also 4:3's and 9:2's.

## The levels

Settings are taken from the objective texts (the briefings are pictures, shell.md). "→" gives
what completes an objective, traced by `cat_obj.py` and read in the scripts. Every level ends
the same way unless noted: a kind-1 volume sends 0x8C (primary objectives complete if the
level's counter is full, else "WARNING! NOT ALL OBJECTIVES MET!" and the no-op `level_set(5, 3)`)
and another sends 0x8D (`level_set(3, 1)`: the level ends); 2:4 and 9:4 end from timers instead.
Every level starts the music (`level_set(1, 1)`).

### 1:1 (plane crash, logbook, town entrance)

- Files: 9 TSP chunks, 13 lights, fog 5700, sky and moon, view 7000; 41 models.
- Spawned: soldiers (3 models, all rifles), 13 destructibles, 2 glass, 3 doors (scripts 75,
  89), 3 mounted guns, the only **0x1771** spinning prop (model with 2 scripts).
- Level scripts: 24 (objective script 43).
- Objectives: find plane (player enters a volume) → adds "enter town" (enter a volume);
  recover logbook (enter a kind-2 object's volume).
- Features: 6 Use lists (doors), 4 character lists (soldiers open doors), one low zone,
  10 streaming volumes, explosions 0/3/5.

### 1:2 (Gestapo, radio transmitter, Maquis hideout)

- Files: 10 TSP, 13 lights, fog 4600, sky and moon; 34 models.
- Spawned: 52 soldiers (6 models, rifles and 2 SMGs), 12 destructibles, 8 glass, a box
  (0x1779, a looping sound, script 142).
- Level scripts: 15 (objective 44).
- Objectives: **neutralize the Gestapo** → 8 soldiers of script 92 removed while dead (each
  sends 0x64; "half" text at 4); **radio transmitter** → destructible script 95 destroyed
  (explosion 7); Maquis hideout → enter a volume.
- Features: 2 ladders, 12 streaming volumes, `set_flags(0x400)` on a soldier (the sentry,
  soldier.md), `sound_stop`.

### 1:3 (sewers: G3 attaché case, weapon cache, exit)

- Files: 9 TSP, 17 lights, fog 3400, no sky, view 4600; 26 models.
- Spawned: 33 soldiers, **16 dogs** (0x1405, model with 43 scripts; the only dogs), one glass,
  3 scenery.
- Level scripts: 10 (objective 45). The reference level of levels.md and soldier.md.
- Objectives: attaché case → enter a kind-2 object's volume; weapon cache, sewer exit → volumes.
- Features: one ladder, 10 streaming volumes, 5 pickups with enter lists.

### 2:1 (rail yard: switch tracks, Ausweis blue and red)

- Files: 6 TSP, 9 lights; 33 models.
- Spawned: 10 soldiers (SMGs, 2 pistols), 5 doors, 47 glass (0x1777, kind 6).
- Level scripts: 15 (objective 46).
- Objectives: **switch tracks** → Use on one of two kind-5 volumes (script 99: world animation,
  0x37); **Ausweis blau/rot** → item pickups 15/16 (0x2F/0x30 → 0x38/0x39).
- Features: **papers** (weapon 7) and the papers-checking soldiers (scripts 20, 42, 90:
  `has_tag` on area tags 81..84; soldier.md's 2:1 soldier); **alarms** in the soldier model
  (op 0x49); 2 low zones; 7 Use lists; 2 kind-5 volumes; "END OF LEVEL LOCATED".

### 2:2 (train: manifest, gift package, reserve engine, rail tunnel)

- Files: 5 TSP, 17 lights, sky; 40 models.
- Spawned: 14 soldiers (SMG, rifle, pistol), 5 destructibles, 6 glass, 2 doors (script 74),
  6 scenery (one the charge).
- Level scripts: 28 (objective 47), the most of any level.
- Objectives: gift package → Use on a volume; freight
  manifest → enter an object's volume; **reserve engine** → demolition charge (script 126
  needs the package first, 98 explodes it); Ausweis rot → item pickup 16; rail tunnel → volume.
- Features: papers and papers soldiers (20, 42, 90, 113), **sniper rifle**, level alarms
  (script 131: 15 alarm ids 80..94) and soldier alarms (op 0x49), `world_anim_set`, ladder,
  10 Use lists, `explosion(8)` and `(2)`.

### 2:3 (canyon: grenades, fuel containers, exit to the railgun)

- Files: 8 TSP, 19 lights, sky and moon; 36 models.
- Spawned: 40 soldiers (SMG, rifle), 19 destructibles, 4 mounted guns, 3 doors, a box.
- Level scripts: 21 (objective 48).
- Objectives: procure grenades → walk into one of 3 pickups with enter lists; **fuel
  containers** → 8 destructibles of script 111 (each sends 0x67); exit canyon → volume.
- Features: **9 ladders**; **8 low zones and the disc's only undetectable zone**
  (soldier.md); low zones also have character lists (0xF/0x10 with tag kind 4); sniper rifle;
  the pickup with hash 0 (record 73).

### 2:4 (railgun Greta)

- Files: 4 TSP (no streaming), 9 lights, sky and moon; 32 models.
- Spawned: 19 soldiers, 6 glass, 6 scenery (charges), 7 boxes (0x1779).
- Level scripts: 13 (objective 49).
- Objectives: one, **destroy Greta**: charges ("CHARGE 1..4 SET", script 115 on Use, the
  targets 97/101/129); the fourth 0x68 → 0x74; `explosion(4)` (radius 2400) twice from script
  129, which then sends 0x37: primary objectives complete, and 17 timer messages (5.7 s) later
  the objective completes and 0x38 ends the level (no 0x8C/0x8D volumes; read from script 49,
  not played).
- Features: 4 kind-5 volumes, `world_anim_set`, sniper rifle, no ladders.

### 3:1 (ship: promenade, bridge, engineering, wrench, jam engine, cargo hold)

- Files: 7 TSP, 9 lights, sky; 40 models.
- Spawned: 37 soldiers (SMG, pistol), 14 destructibles, 8 glass, 4 doors (3 models), a switch.
- Level scripts: 23 (objective 50), six objectives.
- Objectives: promenade, bridge, engineering → volumes (one also a character list); find
  wrench → walk into an object (0x63 → 0x8E); jam engine → Use on an object (0x8F → 0x92) or a
  volume; exit through the cargo hold → Use on an object sends 0x8C, which here completes the
  objective and the primaries.
- Features: **papers** with all three item pickups (15, 16, 17: clearance 1..3), papers
  soldiers (27, 42, 103, 113), locked doors with their own texts (script 164: "PRESS ACTION
  BUTTON TO OPEN CARGO DOOR", "THE CARGO DOOR IS LOCKED"), level and soldier alarms, 17 Use
  lists (the most).

### 3:2 (smuggled weapons, timetable, truck, hatchway)

- Files: 5 TSP, 9 lights, sky and moon; 39 models.
- Spawned: 25 soldiers, 12 destructibles, 47 glass, 2 scenery.
- Level scripts: 21 (objective 51).
- Objectives: hatchway, timetable → volumes; smuggled weapons → Use on a volume; **truck** →
  demolition charge (78 → 97: 0x68).
- Features: **10 ladders**, sniper rifle, one streaming volume, 2 kind-5 volumes.

### 3:3 (U-boat pen: engine specs, hull blueprints, sea door control, board U 4901)

- Files: 8 TSP, 13 lights; 40 models.
- Spawned: 49 soldiers, 21 destructibles, 55 glass.
- Level scripts: 24 (objective 52).
- Objectives: engine specs → 2 timer messages after Use on one of three objects (0x37); hull
  blueprints → volume; **sea door control** → charge (0x20 → 0x39, also script 141's world
  animation); board U 4901 → volume.
- Features: level and soldier alarms, a low zone, the only `+0x50` list.

### 3:4 (inside U 4901: radio, fins, ballast, exit hatch)

- Files: 6 TSP, 9 lights; 39 models.
- Spawned: 28 soldiers (pistol, SMG), 6 scenery, a box, a switch (0x1775, script 139: Use then
  it explodes on 0x20).
- Level scripts: 14 (objective 53).
- Objectives: radio coordinates → Use on a volume; lock fins → Use on an object; **fin
  controls** → charge (script 107, 14 timer messages); blow ballast → Use on one of 4 objects;
  exit hatch → volume.
- Features: 5 ladders, 7 Use lists, a looping sound box.

### 4:1 (fort exterior: crowbar, Stuka, hatch, sublevel)

- Files: **15 TSP** (the most), 17 lights, sky and moon; 33 models.
- Spawned: 56 soldiers (rifle, SMG; 53 grenade throwers), **12 mounted guns** (the most), 2
  doors (80, 100), 3 scenery, the only 0x177A.
- Level scripts: 18 (objective 54).
- Objectives: crowbar → object volume; **Stuka** → charge (script 98); hatch → a door opened
  by Use (0x61 → 0x37), which adds "find entrance to sublevel 1" (volume).
- Features: **26 streaming volumes**, **15 low zones**, 7 ladders, 13 character lists.

### 4:2 (fort: false SOS, Müller's orders, key, gas mask)

- Files: 11 TSP, 9 lights, view distance 0 (→ 4500); 35 models.
- Spawned: 67 soldiers (the most), **49 destructibles**, 2 doors, 2 glass, a box.
- Level scripts: 16 (objective 55).
- Objectives: send false SOS → Use on a volume; orders, gas mask → object volumes (each adds
  the gas mask objective again); key → object volume (or item 15, which the level doesn't
  have).
- Features: 10 ladders, 16 streaming volumes, looping sound box (142).

### 4:3 (fort interior: gas valves, exit)

- Files: 9 TSP, 9 lights; 33 models.
- Spawned: 54 soldiers, 2 switches (the valves, script 81). No destructibles.
- Level scripts: 8 (the fewest; objective 56).
- Objectives: gas valve 1, 2 → Use on the valve objects; exit fort → volume.
- Features: soldier scripts identical to 1:3's set; 2 low zones.

### 5:1 (hydro plant: water flow, power relays, power grid)

- Files: 11 TSP, **no lights**; 28 models.
- Spawned: 52 soldiers, 34 destructibles, **53 glass**, 2 switches, a door, 8 scenery.
- Level scripts: **29** (objective 57).
- Objectives: water flow → Use on an object; **power relays** → six charge targets (0x68,
  counted; at 6 → 0x38); power grid → Use (volume or object); enter plant → volume.
- Features: **12 kind-5 volumes**, 12 low zones with the only soldier low-zone scripts (16,
  17: 0xF/0x10 counted, `set_flags` 0x40/0x20, `set_home`), 12 character lists.

### 5:2 (generators, heating tanks, research files, D2O report, elevator)

- Files: 8 TSP, **25 lights** (the most); 32 models.
- Spawned: 42 soldiers, 8 destructibles, a mounted gun, 2 switches, 2 doors, 6 scenery.
- Level scripts: 22 (objective 58).
- Objectives: **generators** → Use on 3 kind-5 volumes (0x6E/0x6F/0x70, counted); heating
  tanks → 2 switches (106, 0x5C); **research files** → 6 charge targets (0x68); D2O report →
  object volume; elevator → volume.
- Features: **15 kind-5 volumes**, 12 Use lists.

### 5:3 (heavy water vats, valve, delivery schedule, garage, truck)

- Files: 8 TSP, 9 lights; 32 models.
- Spawned: 52 soldiers, 35 destructibles, **5 switches**, 8 glass.
- Level scripts: 21 (objective 59).
- Objectives: **vats** → 4 switches by Use (script 106, counted with texts 1..3 of 4);
  release valve → Use on an object; schedule → object volume; garage → volume; **truck** →
  charge.

### 5:4 (trucks, power station, ferry)

- Files: 6 TSP, sky and moon; 42 models.
- Spawned: 47 soldiers (rifles, 5 Panzerfaust), 45 destructibles (9 models), 2 mounted guns,
  a door, a box.
- Level scripts: 25 (objective 60).
- Objectives: **both trucks** → 2 charge targets; **power station** → charge (script 146);
  **ferry** → 11 timer messages (3.7 s) after Use on an object (0x38).
- Features: first Panzerfaust soldiers; `sound_stop`.

### 7:1 (generator fuel, mine entrance)

- Files: 10 TSP, **no lights**, sky and moon; 36 models.
- Spawned: 49 soldiers (SMG, rifle, 9 Panzerfaust), 12 destructibles, a mounted gun.
- Level scripts: 16 (objective 61).
- Objectives: **generator fuel** → 4 charge targets (script 125, "1..3 OF 4" texts);
  mine entrance → volume.
- Features: 4 kind-5 volumes, 5 ladders, soldiers with `set_flags(0x2000000)` (script 73).

### 7:2 (SS demolition squad, Dahood manuscript, elevator key, elevator)

- Files: 7 TSP; 40 models.
- Spawned: 35 soldiers, 11 destructibles, 18 glass, a door.
- Level scripts: 10 (objective 62).
- Objectives: **8 SS demo squad members** → 8 soldiers of script 150 removed while dead (0x64;
  "half" text); manuscript, key → object volumes; elevator → volume.
- Features: soldier scripts identical to 4:2's set.

### 7:3 (museum: curator inventory, 12 bombs)

- Files: 10 TSP; 41 models.
- Spawned: 31 soldiers, 25 destructibles, 23 glass.
- Level scripts: 15 (objective 63).
- Objectives: inventory → object volume; **12 bombs** → Use on 12 objects (script 96: 0x65 →
  0x67, counted with texts "1..11 OF 12").
- Features: 12 Use lists, 4 low zones. The message table also has "BONUS SAVE ART OBJECTS"
  and "n PERCENT OF ARTWORK LOST" texts that no level script shows (cut content, *inferred*).

### 9:1 (V1 site: target list, blast doors, launch control, emergency hatch)

- Files: 7 TSP, 6 lights, sky and moon; 37 models.
- Spawned: 59 soldiers (all SMG), 19 destructibles, **6 doors**, a switch.
- Level scripts: 24 (objective 69).
- Objectives: target list → 4 timer messages after Use (0x37); blast doors → Use on a volume;
  **launch control** → charge (0x68); emergency hatch → volume.
- Features: 8 ladders, 9 Use lists. The texts for "destroy all buzz bombs" exist but no script
  adds that objective.

### 9:2 (V2 site: roster, schematics, guidance lab, gyro, launch area)

- Files: 9 TSP, no lights; 37 models.
- Spawned: 55 soldiers (all SMG), 22 destructibles (10 models), a door.
- Level scripts: 18 (objective 70).
- Objectives: all volumes or Use on a volume: schematics, guidance lab, gyro (Use), roster
  (object volume), launch area.
- Features: soldier scripts identical to 1:3's.

### 9:4 (the V2 launch)

- Files: 4 TSP (no streaming), sky and moon; 33 models.
- Spawned: 12 soldiers, 9 destructibles, a mounted gun, **the V2 (0x1774)**.
- Level scripts: 16 (objective 72; ten placeholder handlers).
- Objective: **launch the sabotaged V2** → Use on a volume (0x2C to the rocket's script 39:
  music restarts with `level_set(1, 1)` (a no-op the second time: "Tried to reactivate
  music"), then `set_flags` 2 at tick 1, 4 at tick 92, 1 at tick 126 → 0x3E; the 0x1774 model
  script sends 0x74). Script 112 then explodes (`explosion(4)`) and **48 timer messages (16 s)
  later sends 0x3D: objectives complete and level end together** (no 0x8C/0x8D volumes).
- Features: the only level without `level_set(2, 0)` at start.

## MSN12: the split-screen arenas (LEVEL2P.BIN)

Seven levels, 4 TSP chunks each, 23 models, 71..132 records: ladders (1..6), two player
starts (0x8877B646), 4..5 records of class **0xFD092133** (kind 0, UNDOCUMENTED; spawn points,
*inferred*), 62..119 pickups whose hashes (20..55 per arena) are not in LEVEL.BIN's pickup
table (LEVEL2P has its own; not read), and in 12:5 and 12:7 two low-zone boxes. Two scripts
each: 13 (the player) and 38 (activation). No soldiers, objectives or doors. 6 destructible
models are present but no record spawns them.

## Not yet covered

What a level uses that no document in `docs/re/` explains (or explains only by inference),
with where it appears. Highest impact first.

1. **Despawn and respawn of record entities.** `entities_draw` (`0x8006C714`): an entity
   flagged A 0x8000 (soldiers, by their flags 0x1300903C; pickups with record `+0x58` bit 0,
   whose `+0x290/+0x294` come from `+0x5E`; soldiers' `+0x294` is record `+0x68`, 150) has an
   out-of-sight counter: `model_draw` (`0x8006D2E4`) or `0x8005E760` resets `+0x290` to
   `+0x294` while it is in view, and `0x8006A570` decrements it once per drawn frame while it is
   out of view and farther from the camera than the far plane (`0x8006A3BC`); with the soldier
   cap nearly reached and spawns queued, one with flags B 0x40000 behind the camera and 3501+
   away is culled at once. Once drawn (`+0x298`) and at 0, the record gets **message 2** and the
   entity is removed. `entity_remove` clears the record's state bit 0, so activation scripts
   6/38, which re-subscribe on message 2, **can spawn it again** when their trigger fires again.
   Read from the code only; not watched in play.
2. **Record state bits**: bit 0 "has a live entity" (set by `record_activate`, cleared by
   `entity_remove`); bit 1 "destroyed": set by `entity_remove` for kinds 2, 3, 6 when the entity
   had flags A 0x20000000, unless the record's `+0x30` has bit 3 (and by the soldier destructor
   when dead); `0x80075684` clears both for every record. levels.md says 0/1/2 "not spawned,
   alive, dead".
3. **The message conventions** between level scripts (3 activate, 2/0x13 re-arm, 0x1B alert,
   0x16..0x18 group hand-off, 0x20 detonate, 0x2E switch Use, 0x37..0x3F objectives, 0x5A..0x5C,
   0x61/0x62, 0x89..0x8B doors, 0x63 collect, 0x64 kill count, 0x68 target destroyed, 0x7A..0x88
   alarms, 0x8C/0x8D level end, 0x93, 0x94..0x96 sounds, 0x99/0x9A locked doors): read from the
   scripts here, *inferred*; levels.md lists them as open. A reimplementation that runs the
   scripts needs only the engine-sent ones (table above), of which 2 and 0x1A are undocumented.
4. **register_msg filters 2, 3, 4** (`msg_filter_dispatch` `0x80066EA8`): 2 the sender is a
   given entity, 3 the sender record's `+0x34` equals a value (used ~100 times: "my own
   record"), 4 a given record; and the **tag kinds** (tags `{kind:16, value:16}`, filter 5):
   kinds 1 (activation groups), 3, 4 (low zones for soldiers), 5, 6 (soldier groups, switches),
   7 (doors), 8 (charges and their targets), and area tags 81..84 (papers). Only filter 5 and
   kind 7 are described (objects.md).
5. **Soldier script sets other than 1:3's and 2:1's**: 20 distinct sets of 210..317 scripts
   (table above); soldier.md reads 1:3 (= 4:3, 9:2) and 2:1. 18 sets unread, among them the
   grenade throwers (MSN4 on) and Panzerfaust soldiers (5:4, 7:x, 9:4).
6. **Soldier `+0x40` bits set by level scripts that soldier.md doesn't list**: 0x20, 0x40, 0x80
   (5:1's low-zone soldiers), 0x4000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x200000,
   0x2000000 (7:1, 7:3), 0x4000000, 0x8000000 (→ sets `+0xEC` bit 2 "stationary" in every
   model, *inferred* from the event's stored `set`), and 0x800000 registered by the model only.
7. **Type 0x1775 (switches, valves)**: model scripts animate on `+0x40` bits 1/2/4; Use reaches
   them through a kind-2 record's Use list (0x2E). 3:4, 4:3, 5:1, 5:2, 5:3, 9:1.
8. **Item pickups 15..19** (`0x800832C0`) and the **papers clearance chain** through the
   player's script 13 (0x2F..0x32 → 0x34..0x36/0x6B); soldier.md has only the soldier side.
9. **`level_set(5, x)`**: called 1..5 times per level (not in 9:4) (x = 1, 2, 3; e.g. with "WARNING! NOT ALL
   OBJECTIVES MET!") but `level_set` (`0x80052DC4`) handles only 0..4: a no-op in this build.
   **`level_func`**: only index 1 (entity flags A 0x20000000, "destroyed") is used; index 0
   (flags A bit 0) never; the table has 2 entries.
10. **Model-script ops** missing from enemies.md's table: 0x12 (`0x8005908C`: leave the ground,
    soldiers, dogs, the player's body), 0x1C (`0x800594AC`: render-instance variant, all
    characters and weapon models), 0x41 (`0x8005C1B4`: instance `+0x6C`, soldiers and dogs).
11. **Entity type 0x23** (impact effect, 9:1 dump; `0x800702E0`).
12. **Class 0xFD092133** (MSN12) and MSN12's pickup hashes (LEVEL2P's tables); class 0x271BD574
    is tested by the kind-0 enter code (`0x80074E10`, does nothing) and appears on no record.
13. **Sky/stars/moon drawing** (13 levels have sky, 11 a moon; formats.md lists it as unread)
    and levels without lights (5:1, 7:1, 9:2, 12:6: no light substitution).
14. **Placed charges and timed sequences** are level-script data (scripts 78/107/115/126 and
    their targets, the V2 timeline): no new engine code, but nothing documents that the charge
    model appearing on Use and the 3.7 s delay are script-driven.
15. Record `+0x50` list (one on the disc, unread); 2:3's pickup with hash 0.

## Corrections

- levels.md, Level logic: the script table's first word is the **number of message ids**
  (158 in every level), not of scripts: 1:3 has 10 scripts in 167 slots. (Now in lvs.py's
  docstring too.)
- levels.md, the VM: for 0D (−) and 0F (/) the result is **top − next** and **top / next**
  (the value pushed last first), so `(1 - ent[0])` in earlier lvs.py output was `ent[0] - 1`;
  the comparisons 08..0B match the table (`level_vm`, `fn.py c 800672E0`). lvs.py has since been
  changed to print TOP op NEXT; levels.md's table and its example still use the old order.
- lvs.py (before its current version) decoded **the last script of a level as empty** (no next
  offset to bound it); `cat_lvl.py` bounds it by the next header section and trims after the
  last reachable `end` (e.g. 1:3's script 116, 2:2's 166).
- levels.md, Spawning: kinds with relocated templates are 2 (`0x800751B4`), 6 (`0x800751D0`)
  and 3 (`0x800751EC`), not 1, 2, 3 (jump table `0x8003CF10`); kind 4's `+0x60` is the pickup's
  hash itself (not relocated); kind 0 gets no lists.
- levels.md, Triggers: leaving a ladder volume calls `0x80061518` (the current ladder record);
  0x8877B646 in `0x80074DC4` is a range bound of the class test, not a spawn — 0x8877B646 is the
  player start record's class. And class 0xD1BA7D1E (level end) is on no record on the disc:
  levels end through `level_set(3, 1)`.
- levels.md, register_msg: filter 0 takes no extra value (as lvs.py now has it); levels.md's
  table says "filter 1, 2 take 1 more value, 3, 4 two, 5 three" without filter 0.
- enemies.md / sst.py: op 0x48 is not "decal": its handler `0x8005C698` runs a **level-script
  message on an entity at once** (`script_run_message(entity, 0, message)`); no model on the
  disc uses it.
- soldier.md, concealment: 2:3 has **eight** 0xD/0xE boxes (records 70..72, 90, 91, 107..109)
  and one 8/9 box (64); the low-zone boxes also carry character lists 0xF/0x10.
- objects.md, Other types: "0x1774 (2:3, 4:2, 9:4, 12:7)": only 9:4's BSD has a model of type
  0x1774 (`+0xFC` of every model of every BSD), and one record spawns it. "0x1777 one per level"
  holds for the model, but 6 SP levels spawn none (2:3, 3:4, 4:1, 4:3, 9:1, 9:4).

## Tools

- `tools/recon/cat_lvl.py --ram DUMP BSD...`: the per-level report (header, records by class,
  spawned types, pickups, weapons, model scripts and their ops, record lists, zones, built-ins
  with their argument values, objectives, texts, handled messages); `--scripts BSD` the level
  scripts folded; `--check DUMP2 BSD` compares the file's script table with a dump's.
- `tools/recon/cat_scripts.py BSD...`: the script library (one line per id: levels, bodies,
  handlers, built-ins, users); `--show ID` its code.
- `tools/recon/cat_obj.py --ram DUMP BSD...`: each objective and what sends its message.
