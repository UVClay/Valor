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

The BSD is read whole (`0x8004A374`); `g_level` (`0x8009CC64`) points at its level header (in
1:3, 0x6B00 bytes into the file; the file starts with other data). The loader turns the
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

A count, a size and two words, then `count` pairs of {key, record pointer} (1:3: 121 records).
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
| 0x4F292333 | 49 | 0x98 | Soldier spawn (keys 0, 1, 2) |
| 0xB81C10E3 | 33 | 0x6C | |
| 0xBD12927E | 23 | 0x7C | |
| 0x4D078F23 | 10 | 0x38 | |
| 0x166A85FB | 3 | 0x84 | |
| 0x3011B913, 0xD31F988A, 0x8877B646 | 1 each | | |

The soldier record (`enemy_spawn` reads it): +0x5B skill tier, +0x5C, +0x5E, +0x60, +0x61
health, +0x62, +0x63 aim error, +0x64 damage, +0x65, +0x66, +0x68 radius (150), +0x6A, +0x6C and
+0x70 ranges (1500, 3000), +0x6E (enemies.md, Spawning). Two records of the same soldier can
sit side by side with different keys and stats (key 1: 6 health, aim error 40, damage 7; key 2:
4 health, aim error 20, damage 4, a few units away): what selects one is open.

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

## Open

- What the key of an entity pair selects (difficulty? a trigger group?).
- Classes other than the soldier, and the 0x177x types (pickups, doors, alarms, objectives...).
- The level logic run each tick after the collisions (`0x8005F69C`) and objectives
  (`level_end_carry` complains when `g_bPrimaryObjectivesComplete` isn't set).
- The collision sections, the TAF files.
