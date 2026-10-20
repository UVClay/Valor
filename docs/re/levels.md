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
| +0x544..+0x584 | {pointer, count} sections: 235, 6715, 52894, 158, 7, 1972, 7, 182, 1123 entries in 1:3 (collision geometry?), also the bases for models' offsets |
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
health, +0x62, +0x63 aim error, +0x64 damage, +0x65, +0x66, +0x68 radius (150), +0x6A, +0x6C and
+0x70 ranges (1500, 3000), +0x6E (enemies.md, Spawning). Records differ soldier by soldier
(1:3 has 6 health, aim error 40, damage 7 next to 4, 20 and 4). Pointer fields at +0x38..+0x50
(and +0x60 or +0x74 by activation kind) are offsets within the record, made pointers by
`0x80075120`.

## Spawners

Records become spawner entities; a spawner creates the real entity through `spawn_by_type`
(`0x80061350`, from `0x8006D6F0`), by the type it holds at `+0xFC`:

| Type | Created by | |
|---|---|---|
| 0x1402 | `enemy_spawn` (`0x800563F0`) | Soldier |
| 0x1405 | `0x80054E4C` | The other enemy (5 hit parts; contact damage: a dog?) |
| 0x1770, 0x1778 | `0x8005D678` | |
| 0x1771 | `0x80046B2C` | |
| 0x1772 | `0x8005E2AC` | |
| 0x1774 | `0x800905C0` | |
| 0x1775 | `0x8005E510` | |
| 0x1776 | `0x8005E648` | |
| 0x1777, 0x1779, 0x177A | `0x8005E7B0` | |

`enemy_spawn` refuses while `0x80097FEC` soldiers are alive (`0x80097FE8`).

## Level logic: messages and scripts

Levels are event-driven: entities send each other messages (`Msg_SendMessageDirectly`,
`MsgSys_ProcessPendingMessages`; `send_event` `0x800662CC` from the enemy scripts), and a message
runs a script of the level (`0x80066318`, `0x800663B8`, `0x800664A0` → `level_vm`, `0x800672E0`).

**Scripts** are in the BSD: the header's `+0x59C` points at the script table (`[0x8009D5AC]`
while the level runs; 1:3: 158 scripts), offsets at `+0x8 + 4·id`. A running script's context
holds its program counter (`+0x0`), the entity's handle (`+0x4`) and the script id (`+0x10`).

**The VM** (`level_vm`, `0x800672E0`, ~2200 instructions, with its overflow cases split by the
recompiler into the small functions that follow it): byte opcodes (0..0x17, jump table at
`0x8003CCF0`) over a typed value stack (`0x800AD260`, 8-byte {type, value} entries, top index
at `0x800AD5A8`). Types: 0x0A integer, 0x0B boolean, 0x0E float (PsyQ software floats:
`0x8001D590` int→float, `0x8001D7B0` equal, `0x8001D880` less), 0x0F string (strcmp), 0x10 a
handle; binary operators switch on the pair of operand types.

Operands follow the opcode byte, 2- or 4-byte aligned (`0x8006949C`). Addresses are offsets
in the script.

| Op | Operand | |
|---|---|---|
| 00 | | nop |
| 01 | | pop |
| 02 | source byte 0xF4..0xFF, then by source | push: a literal or a value of an entity (the script's own, the message's sender, ...; jump tables at `0x8003CD50`, `0x8003CD68`) |
| 03 | 0xF0..0xF3, ... | store (the counterpart of push) |
| 04, 08..0B | | == < (and the other comparisons): push a boolean |
| 05, 06, 07 | | && \|\| ! |
| 0C..0F | | arithmetic (int and float) |
| 10 | address (u16) | pop; jump if zero |
| 11 | address (u16) | jump |
| 12 | index (u8) | call built-in |
| 13 | index (u16) | call built-in |
| 14 | | end |
| 15 | | push whether this script hasn't run yet (a once-flag in the script table) |
| 16 | | set the once-flag |
| 17 | byte or word | debug print |

**Built-ins** (`g_level_builtins`, `0x8009D5C0`, 22 functions, filled at level start by
`0x80066110`):

| # | Function | |
|---|---|---|
| 0 | `0x80069514` | RegisterMsg: subscribe the entity to a message (filter types) |
| 1 | `0x80069714` | UnregisterMsg |
| 2, 12 | → `0x80066EA0` | send a message |
| 3 | → `0x800660F4` | |
| 4 | → `0x80052D70` | |
| 5 | → `entity_remove` | remove an entity |
| 6 | → `0x80052F14` | an explosion (sound, particles, `0x800719C0`) |
| 7 | → `0x800536F4` | (sets `0x80098630`) |
| 8 | → `0x80053464` | (dynamic lights) |
| 9, 10 | → `0x8005351C`, `0x8005359C` | |
| 11 | → `0x800539C8` | |
| 13 | → `0x80052DC4` | activate the level's music (loads `m_la.vb`; sets `g_bPrimaryObjectivesComplete`) |
| 14..17 | | objective add / remove / complete / is complete |
| 18, 21 | → `0x80054740`, `0x80054990` | |
| 19 | → `0x800533D4` | play a sound |
| 20 | → `0x80053444` | stop a sound? |

**Objectives** are built-ins (stack wrappers at `0x80069DF4`..`0x80069EC4`): `objective_add(id,
text)` (`0x8005372C`: adds, or resets to incomplete; the text from the message table
`0x80098188`), `objective_remove(id)` (`0x8005380C`), `objective_complete(id)` (`0x800538B0`),
`objective_is_complete(id)` (`0x8005394C`, 1 for an unknown id). The list: ids at `0x8009892C`,
done flags at `0x800988DC`, texts at `0x80098904`, count at `0x80098620`;
`g_bPrimaryObjectivesComplete` is `0x80098624` (set by `0x80052DC4`, read by `level_end_carry`
and `0x8006463C`).

`0x8005F69C`, run every tick after the collisions, is the dynamic lights (muzzle flashes): 32-
byte entries at `0x80099F4C` (count `0x80099ED0`) with a lifetime and a colour fade.

## Results: the rating and career totals (SHELL.BIN)

After a level the menus read the statistics (combat.md) and `g_level_end`:

**The rating** (`level_rating`, SHELL `0x8007344C`, "m_pmeval_overall"), 0..2:

| Rating | When |
|---|---|
| 0 | The primary objectives aren't complete (`g_level_end+0xC`), or kills (`0x80039CB8`) are below the level's requirement |
| 1 | Objectives complete and enough kills |
| 2 | ... and more than 75% health at the end (`g_level_end+0x8` / 128.0 > 0.75) |

The kill requirements (SHELL `0x8003BA40`, 4 words per row; the row from `0x80039C88`, the level
from `0x80039C84`; 9999: no such level):

| Row | Level 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| 0 | 21 | 49 | 40 | - |
| 1 | 5 | 9 | 35 | 17 |
| 2 | 29 | 24 | 37 | 25 |
| 3 | 68 | 64 | 51 | - |
| 4 | 49 | 40 | 46 | 43 |
| 5 | 49 | 32 | 30 | 51 |
| 6, 7 | 45 | 53 | 26 | 26 |

(Rows 0..3 match missions 1..4's level counts; how 5, 7 and 9 map to the rest is open.)

The **best rating per level** is kept in the level's carry-over row: halfword 13 of
`g_level_ammo[mission][level]` (`g_game+0x18C + mission·152 + level·36 + 26`), -1 until the level
is completed; a new rating replaces it only if higher. A real save had 2 on 1:1..1:3 and 1 on
2:1 and 2:2.

**Career totals** (`career_add`, SHELL `0x80062694`) add the level's counters into `g_game`:
+0x10 shots, +0x14 hits, +0x18 wounds, +0x1C `0x80039D10`, +0x20 kills, +0x24 head hits,
+0x28 torso hits without part 8, +0x2C part 8 (the groin), +0x30 parts 10/12/14/16, +0x34 parts
2/6, +0x38 parts 9/11/13/15, +0x3C parts 1/5 (legs and arms, one side each), +0x40.. kills per
weapon (from `g_level_end`).

## Open

- The state values of an entity pair, and what activates a record (kinds 0..6).
- Classes other than the soldier, and the 0x177x types (pickups, doors, alarms, objectives...).
- The rest of the level VM's opcodes and its built-ins (a disassembler for the level scripts,
  like `sst.py` for the enemy scripts).
- The collision sections, the TAF files.
