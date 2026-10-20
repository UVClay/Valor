# Levels: files, the level header, entity records, spawning

Read from the loader and checked against RAM dumps of 1:3 (`recordings/2026-10-19_22-22-43_m1l3`
replayed to vblank 14000).

## Files (`DATA\MSNm\LVLl\`)

| File | Contents |
|---|---|
| `m_l.BSD` | The level: header, entity records, models (with their scripts), AI nodes, collision |
| `m_l0.TAF`, `m_l1.TAF` | Loaded first (`%ld_%ld%d.taf`, the digit from `0x8009A618`): textures/animations? |
| `TSP0\m_l_cN.TSP` | World geometry chunks, drawn by `tsp_draw` (the path is in the header) |
| `Mm_l.VB`, `Mm_lA.VB` | SPU sample banks |

## Loading (`level_load`, `0x80087308`)

The BSD is read (`0x8004A374`); `g_level` (`0x8009CC64`) points at its level header, file
offset 0x800 (the file starts with a 0x800-byte table of 25 offsets, sizes of its later parts;
some of those parts end up elsewhere in RAM). The loader turns the
header's offsets into pointers (adding the section base), relocates every model
(`model_relocate`, `0x80087080`: its data, and its scripts through `0x8003F788`), runs the
section parsers (`0x80086C9C`, `0x80086D18`, `0x80086D8C`, `0x80086E34`, `0x80086EB0`,
`0x80086F24`), creates the world entity (`g_world`, `0x80053E68`) and gives it the entity table
and the node table, sets the fog and background colours, and builds the spawners.

## The level header (`g_level`)

| Offset | Meaning |
|---|---|
| +0x00 | TSP path prefix, e.g. `data\msn1\lvl3\tsp0\1_3_c` (0x80 bytes) |
| +0x80 | Counts (9, 4, 1, ...) |
| +0x98 | Used by the drawing pass (primitive pool size: `2 × [+0x98]`) |
| +0xCC | Fog on (s16); +0xCE..+0xD0 fog colour (far colour), +0xD4 back colour switch |
| +0xDC..+0x320 | 0x14-byte entries {count, pointer, n, 0x02RRGGBB, type}: colour/light sets? |
| +0x53C | The entity table (below) |
| +0x540 | The AI node table: count, then entries (the world object's +0x4/+0x8/+0xC) |
| +0x544..+0x584 | {pointer, count} sections: 235, 6715, 52894, 158, 7, 1972, 7, 182, 1123 entries in 1:3 (model data? the world's collision is in the TSP files), also the bases for models' offsets |
| +0x598, +0x59C, +0x5A0 | Sections parsed by `0x80086F98`, (world), `0x8007540C` |

## Entity records (`+0x53C`)

A count, a size and two words, then `count` pairs of {state, record} (1:3: 121 records, at file
offset 0x2E7C). In the file the records are offsets from the first record and every state is
0: the state is runtime, as the scripts test it (op 0x34: bit 0x2; 1:3 at vblank 14000 showed
0, 1 and 2: not spawned yet, alive, dead?).
Every record starts with:

| Offset | Meaning |
|---|---|
| +0x00 | Class hash |
| +0x04 | Record size |
| +0x08 | 4 |
| +0x0C | Activation kind (0..6; `0x8007529C` registers every record with `0x80075120`, kinds 1, 2, 3 also with `0x800751B4`/`0x800751D0`/`0x800751EC`) |
| +0x10 | Position: x (s16), y (s16), z at +0x14 |

1:3's classes:

| Hash | Count | Size | |
|---|---|---|---|
| 0x4F292333 | 49 | 0x98 | Soldier spawn |
| 0xB81C10E3 | 33 | 0x6C | |
| 0xBD12927E | 23 | 0x7C | |
| 0x4D078F23 | 10 | 0x38 | |
| 0x166A85FB | 3 | 0x84 | |
| 0x3011B913, 0xD31F988A, 0x8877B646 | 1 each | | |

The soldier record (`enemy_spawn` reads it): +0x5B skill tier, +0x5C, +0x5E, +0x60, +0x61
health, +0x62, +0x63 aim error, +0x64 damage, +0x65, +0x66, +0x68 (150), +0x6A, +0x6C and
+0x70 ranges (1500, 3000), +0x6E (enemies.md, Spawning). Records differ soldier by soldier
(1:3 has 6 health, aim error 40, damage 7 next to 4, 20 and 4). Pointer fields at +0x38..+0x50
(and +0x60 or +0x74 by activation kind) are offsets within the record, made pointers by
`0x80075120`.

## Spawning: records, models, entity types

A record becomes an entity when it's **activated** (`record_activate`, `0x80052D70`, level
built-in 4: once per record, bit 0 of its state word): by its kind (`+0xC`, `0x8007505C`):

| Kind | Hash (1:3, 2:1, 2:2) | Count | Becomes |
|---|---|---|---|
| 0 | 0x4D078F23, 0x3011B913 | 3..11 | Nothing (activation does nothing) |
| 1 | 0xB81C10E3 | 26..36 | Nothing; the most common record (positions, a yaw, small numbers: open) |
| 2 | 0x166A85FB, 0x8877B646 | 3..15 | An entity of the model named by the template at `+0x60` (`+0x4` its hash); script id `+0x64` |
| 3 | 0x4F292333 | 10..49 | A soldier: the template at `+0x74` (the weapon hashes, enemies.md); script id `+0x78` |
| 4 | 0xBD12927E | 5..23 | A pickup (`0x80083960`, type 101) |
| 5 | 0x413B09BF | 0..2 | Nothing |
| 6 | 0xD31F988A | 1..45 | Like kind 2 |

**Models** (`[0x8009EC58]`, count `[0x8009EC54]`, 0x100 bytes each, loaded with the level):
+0x0 hash, +0x94 the behaviour script started at spawning (-1: none), +0xB8/+0xBC the scripts
(enemies.md), +0xFC the **entity type**. The type is the model's: `spawn_by_type`
(`0x80061350`, from `0x8006D6F0`) creates the entity by it, then starts its script:

| Type | Constructor | Think | |
|---|---|---|---|
| 0x1402 | `enemy_spawn` (`0x800563F0`) | `enemy_think` | Soldier (7 models in 2:1/2:2, 2 in 1:3) |
| 0x1405 | `0x80054E4C` | `0x80054AB8` | Another character: soldier-like flags, 5 hit parts, contact damage |
| 0x1770 | `0x8005D678` | (none) | **Destructible**: solid, health from the record's `+0x5C`; damaged by bullets, explosions (type 0x31) and melee (`0x8005D32C`); at 0 health its script gets message 1 (e.g. 1:3's script 84: animate, explode, sound, remove) and `0x80039D10` counts it (career stat `+0x1C`: objects destroyed); record `+0x68` set: `0x800838AC` (a drop?) |
| 0x1778 | `0x8005D678` | (none) | The same model kind without collision (scenery) |
| 0x1771 | `0x80046B2C` | `0x80046A88` | Scripted, spinning by variables 3..5, removed when variable 6 < variable 2 (a timed effect?) |
| 0x1772 | `0x8005E2AC` | `mounted_gun_think` | Mounted gun, with a brain (ranges 3000) |
| 0x1774 | `0x800905C0` | `0x8008FDF8` | A large object with a sub-object and two parameter blocks (a vehicle?) |
| 0x1775 | `0x8005E510` | scripts | Scripted, a collision box, nothing collides with it |
| 0x1776 | `0x8005E648` | scripts | Scripted and solid, with hit parts (5 in 2:1: doors?) |
| 0x1777, 0x1779, 0x177A, others | `0x8005E7B0` | (none) | Inert; 0x1779 a box, the rest solid like 0x1770 (bullets ignore 0x1777) |

Live types seen besides these: 1 the player's soldier, 2 the camera, 3, 0x31 (explosions),
0x4C, 0x65 pickups.

`enemy_spawn` refuses while `0x80097FEC` soldiers are alive (`0x80097FE8`).

## Level logic: messages and scripts

Levels are event-driven: entities send each other messages (`Msg_SendMessageDirectly`,
`MsgSys_ProcessPendingMessages`; `send_event` `0x800662CC` from the enemy scripts), and a message
runs a script of the level (`0x80066318`, `0x800663B8`, `0x800664A0` → `level_vm`, `0x800672E0`).

**Scripts** belong to entities: each record names its script (id at `+0x64`, `+0x78` for kind 3),
and a message to an entity runs that script's handler for it (`script_run_message`,
`0x800664A0`). The table: `[0x8009D5AC]` (`g_level_scripts`, from the header's `+0x59C`): a
count, a number of slots, then per slot the script's offset from the table (0: none; 1:3 has
158 scripts in 167 slots). A script:

| | |
|---|---|
| u32 N | Number of handlers |
| N × {u16 message, u16 offset} | The handlers; offsets from the script's start |
| bytes | The script's own variables (byte 0: the once-flag), up to the first handler's code |
| code | Byte code; jump targets and operand alignment are relative to the script's start |

Messages are queued (`MsgSys`, 32-byte contexts in a ring of 128 at `0x800B6460`) and run by
`0x80066318`/`0x800663B8`. A context: +0x0 program counter, +0x4 the entity's handle, +0x8 and
+0xC two more handles (the sender?), +0x10 and +0x14 objects (records) for messages without an
entity.

**The VM** (`level_vm`, `0x800672E0`; decompiled with `fn.py c`): byte opcodes over a stack of
8-byte {type, value} entries (`g_vm_stack`, `0x800AD260`, top index `0x8009D5A8`). Types: 0x0A
signed, 0x0B unsigned/boolean, 0x0C, 0x0D, 0x0E float (PsyQ software floats), 0x0F string,
0x10 entity/pointer. Binary operators convert int to float when the types differ.
`tools/recon/lvs.py RAMDUMP [ID...]` disassembles them (folded into statements by default).

| Op | Operands | |
|---|---|---|
| 00 | | nop |
| 01 | | pop |
| 02 | value (below) | push |
| 03 | F0..F3 variable | store the top into a variable, pop |
| 04, 08, 09, 0A, 0B | | A == B, A > B, A < B, A >= B, A <= B (A pushed first): push a boolean |
| 05, 06, 07 | | && \|\| ! |
| 0C..0F | | + - * / |
| 10 | u16 (aligned) | pop; jump if zero |
| 11 | u16 (aligned) | jump |
| 12 / 13 | u8 / u16 (aligned) | call built-in |
| 14 | | end |
| 15 | | push the once-flag (byte 0 of the script's variables) |
| 16 | | set the once-flag |
| 17 | type, value | debug print |

**Values** (push 02):

| First byte | Then | Pushes |
|---|---|---|
| 09 | u8 | that byte |
| 0A..0D | u32 (aligned 4) | an integer of that type |
| 0E | f32 (aligned 4) | a float |
| 0F | length, bytes | a string |
| 10 | 4 bytes (aligned 4) | a pointer to them |
| F0, F1 | selector, index (u8; F1: u16 aligned) | an entity variable: byte `index` of the entity's record + 0x20 |
| F2, F3 | selector, index | a script variable: byte `index` after the handler table |
| F4..FF | kind, property | with kind 0x10 the entity itself, else a property of its record (`push_property`, one reader per record kind, e.g. 0..5 the s16 fields at +0x28..+0x34, 13 the word at +0x54, 14..17 bits of +0x30). FA..FF: the context's entity +0x4 (self), +0x8, +0xC by (S - 0xF4) % 3; F4..F9: the context's objects +0x10, +0x14 |

The selector picks the part of the byte: 9 the whole byte, 1..8 bit 0..7 (store: set if the top
is non-zero). The wide forms (F1, F3, and kinds F7..F9/FD..FF) read their index through a
pointer made from a byte: broken, so unused.

The variables are bytes **in the level record** (from `+0x20`): the record's own fields are
what scripts read and write, and they keep their values when the entity is removed.

**Built-ins** (`g_level_builtins`, `0x8009D5C0`, filled at level start by `0x80066110`; the
arguments in push order):

| # | Name | Pops | |
|---|---|---|---|
| 0 | register_msg | 2 + by filter | (args, filter, handler): subscribe this script's handler; filter 1, 2 take 1 more value, 3, 4 two, 5 three; other filters print "Fatal Error: Unhandled FilterType" and do nothing (1:3's objective script uses filter 0 six times) |
| 1 | unregister_msg | 2 + by filter | the same |
| 2 | send_self | 1 | (message) to this entity |
| 3 | set_flags | 1 | entity `+0x40` \|= value |
| 4 | activate | 2 | (record, ?): once per record (bit 0), spawns its entity by kind |
| 5 | remove_entity | 1 | |
| 6 | explosion | 1 | sound, particles, damage around the entity |
| 7 | level_func | 3 → 1 | calls the level's function table (`0x80098630`) by index |
| 8 | show_text | 1 | message-table text (`0x80098188`) on screen (`text_add`) |
| 9 | has_tag | 3 → 1 | the record's tag list (`+0x48`) holds the value |
| 10 | tags_match | 5 → 1 | two records' tag lists share a tag past a value |
| 11 | set_home | 2 | the entity's home (`+0x3A4`) from another entity or a record |
| 12 | send_to | 3 | (entity, object, message) |
| 13 | level_set | 2 | (what, value): 0 fog near, 1 start the music, 2 primary objectives complete (`g_bPrimaryObjectivesComplete`), 3 level end flags (`0x8009C840` \|= 0x24), 4 `0x8005F068` |
| 14..17 | objective_add / remove / complete / is_complete | 2, 1, 1, 1 → 1 | (text, id), ... |
| 18 | world_anim_step | 1 | step the animated world textures of that group (modes: once, random, loop, reverse) |
| 19 | sound | 7 (→ 1) | (..., mode): pushes a handle when the mode is 3 |
| 20 | sound_stop | 1 | |
| 21 | world_anim_set | 2 | (group, frame) |

With these arities every one of 1:3's 158 scripts folds with an empty stack at each label.

**An example**, 1:3's objectives (script 45):

```
on msg 0:
    level_set(1, 1)               # music on
    level_set(2, 0)               # primary objectives not complete
    ent[1] = 0
    ent[0] = 0
    objective_add(223, 14)        # three objectives: text 223..225, ids 14..16
    objective_add(224, 15)
    objective_add(225, 16)
on msg 37:
    objective_complete(14)
    show_text(226)
    sound(0, 131072, 0, 65536, 53, 0, 0)
    ent[0] = (1 + ent[0])         # objectives done
...
on msg 8C:
    if !(3 > ent[0]) goto L1DE    # all three done:
    ...
  L1DE:
    level_set(2, 1)               # primary objectives complete
```

**Objectives** (`objective_add`, `0x8005372C`: adds, or resets to incomplete; the text from the
message table `0x80098188`; `objective_remove` `0x8005380C`, `objective_complete`
`0x800538B0`, `objective_is_complete` `0x8005394C`, 1 for an unknown id): ids at `0x8009892C`,
done flags at `0x800988DC`, texts at `0x80098904`, count at `0x80098620`;
`g_bPrimaryObjectivesComplete` (`0x80098624`) is what `level_end_carry` and the rating check.

`texts_update` (`0x8005F69C`), run every tick after the collisions, ages the on-screen texts:
32-byte entries at `0x80099F4C` (count `0x80099ED0`, at most 50) with a lifetime and a colour
fade, added by `text_add` (`0x8005F28C`).

## Results: the rating and career totals (SHELL.BIN)

After a level the menus read the statistics (combat.md) and `g_level_end`:

**The rating** (`level_rating`, SHELL `0x8007344C`, "m_pmeval_overall"), 0..2:

| Rating | When |
|---|---|
| 0 | The primary objectives aren't complete (`g_level_end+0xC`), or kills (`0x80039CB8`) are below the level's requirement |
| 1 | Objectives complete and enough kills |
| 2 | ... and more than 75% health at the end (`g_level_end+0x8` / 128.0 > 0.75) |

The kill requirements (SHELL `0x8003BA40`, 4 words per row; 9999: no such level) are per
**campaign slot**: the row is `g_game+0xA8` (copied to `0x80039C88`), the level `0x80039C84`.
The slots, at `0x80038D90` (12 bytes each: mission index, current level, level count; set at
run time), and their requirements:

| Slot | Mission | Levels | Level 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|---|
| 0 | MSN1 | 3 | 21 | 49 | 40 | - |
| 1 | MSN2 | 4 | 5 | 9 | 35 | 17 |
| 2 | MSN3 | 4 | 29 | 24 | 37 | 25 |
| 3 | MSN4 | 3 | 68 | 64 | 51 | - |
| 4 | MSN5 | 4 | 49 | 40 | 46 | 43 |
| 5 | MSN7 | 3 | 49 | 32 | 30 | (51) |
| 6 | (MSN8, not on the disc) | 3 | 45 | 53 | 26 | 26 |
| 7 | MSN9 | 3 | 45 | 53 | (26) | 26 |

When a level starts (SHELL `0x80062108`): slot 6 becomes slot 7, and in slot 7 the third level
becomes the fourth with the third's carry-over row copied to it, since MSN9 has LVL1, LVL2 and
LVL4. MSN6 and MSN8 don't exist. Mode 2 (`0x80039D1C`) plays MSN12 (seven levels, the
bonus/secret levels: mission index 11, the level from `0x80039C74`).

The **best rating per level** is kept in the level's carry-over row: halfword 13 of
`g_level_ammo[mission][level]` (`g_game+0x18C + mission·152 + level·36 + 26`), -1 until the level
is completed; a new rating replaces it only if higher. A real save had 2 on 1:1..1:3 and 1 on
2:1 and 2:2.

**Career totals** (`career_add`, SHELL `0x80062694`) add the level's counters into `g_game`:
+0x10 shots, +0x14 hits, +0x18 wounds, +0x1C objects destroyed (`0x80039D10`), +0x20 kills, +0x24 head hits,
+0x28 torso hits without part 8, +0x2C part 8 (the groin), +0x30 parts 10/12/14/16, +0x34 parts
2/6, +0x38 parts 9/11/13/15, +0x3C parts 1/5 (legs and arms, one side each), +0x40.. kills per
weapon (from `g_level_end`).

## Collision: TSP chunks

The level's world collision is in its **TSP chunks** (`TSP0\m_l_cN.TSP`, loaded by
`0x8008C850` for each name in the header): the TSP header's word 15 (+0x3C) is the offset of a
collision block, which `0x8003EB44` points the chunk's entry at (`0x80090DE8`, 0x28 bytes per
chunk, count `0x80098984`). `tools/recon/tspcol.py` reads it (and exports the triangles as OBJ);
all 220 TSP files on the disc parse with every node reached, every face in a leaf and the block
ending at the end of the file.

| Block | |
|---|---|
| +0x00 | s16 x min, z min, x max, z max: the chunk's rectangle |
| +0x08 | u16 counts: nodes, face indices, vertices, normals, faces |
| +0x12 | Nodes, 8 bytes {s16 a, s16 b, u16 split}: a 2D k-d tree over x/z (root 0). a < 0: a leaf of ~a faces listed from face index b. Else a split at (x min + split) on x when b >= 0 (children a, b), on z when b < 0 (children a, ~b) |
| | Face indices (u16), padded to 4 bytes |
| | Vertices, 8 bytes {s16 x, y, z} |
| | Normals, 8 bytes {s16 x, y, z}, unit length 32767 (y down: floors have y < 0) |
| | Faces (triangles), 10 bytes {u16 v0, v1, v2, normal, s16 d}: the plane is (normal · p) >> 15 + d = 0 |

**Sweeping a segment** (`world_sweep`, `0x8003EF5C`, for everything but walkers): every chunk
(but `0x800AA184`) whose rectangle holds either end or is crossed by the segment is searched
(`0x8003E3F0`): the tree is walked along the segment, nearer half first, up to 8 deferred
halves; in each leaf (`0x8003DC10`) a face is hit where the plane distance changes sign between
the ends (t = |d0| / (|d0| + |d1|), in 1/4096) and the crossing point is inside the triangle
(three edge cross products against the normal). The nearest hit wins: its point, normal and
face (`-1`: nothing hit).

**The floor** (`floor_find`, `0x80060474` → `0x8006028C`, from `entity_move` and spawning):
for each chunk whose rectangle holds the point, the tree is descended to the leaf holding (x, z)
(a split's low side is child a). The leaf's fourth halfword is a **zone id**, kept in the
entity's `+0x390`. Among the leaf's triangles that hold (x, z) (`0x8005FC00`), the height on
each plane (`0x8005FDCC`) is a candidate if it's below the feet or at most 32 units above them
(y points down); the floor is the candidate nearest the feet, or the highest of those below
when none is above. A floor whose normal's vertical part is less than 1.75 times its largest
horizontal part (steeper than about 30°) is flagged steep (`0x80060138`). No floor: -0x7FFF.

Walkers (`walker_slide`, `0x80078F60`) collide with the same chunks through
`0x80078E2C`/`0x80078D1C` (a cylinder: the probe radius, the collision sphere's height).

## Open

- The state values of an entity pair, and what activates a record (kinds 0..6).
- What kinds 0, 1 and 5 are (triggers? cover? sound sources?), and types 0x1405, 0x1771, 0x1774
  in game.
- Record kinds' property numbers (one reader per kind, `0x8006488C`..`0x80065B24`), the message
  numbers, the register filters and what a context's +0x8/+0xC/+0x10/+0x14 are per message.
- The walkers' cylinder test and the floor search in detail; the BSD header's {pointer, count}
  sections (+0x544..+0x584, not the world's collision); the TAF files.
