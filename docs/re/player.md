# Player state: inventory, weapons, carry-over

Addresses are for Medal of Honor (USA), with LEVEL.BIN loaded. Names are in `names.txt`.
Verified against RAM dumps from a real session (`recordings/2026-10-19_22-22-43_m1l3`, natural
1:3, 2:2 and 2:3 starts) and warped starts, and with `VALOR_WATCH` while firing, reloading and
switching weapons.

## The soldier

`g_soldier` (`[0x8009D654]`) is the player's soldier entity; `g_player` (`[0x8009D638]`) is the
camera entity that follows it. Both are entities (see entities.md, to come); the soldier's
weapon state is a block at `soldier+0x3CC`, which is also what `[soldier+0x388]` points to (the
entity's "object" pointer: for the soldier, the weapon block lives inside the entity).

| Offset | Type | Meaning |
|---|---|---|
| soldier+0xF0 | 16.16 | Maximum health? (128.0 at every level start) |
| soldier+0xF4 | 16.16 | Health (128.0 at level start; saved by `level_end_carry`) |
| soldier+0x152 | u8 | Noise group passed to `0x8004D8DC` when firing (alerts enemies) |
| soldier+0x1C8 | s32 | Stance (0..9, a jump table in `fire_gun` picks accuracy from it) |

### Weapon block (`inv = soldier+0x3CC`)

| Offset | Type | Meaning |
|---|---|---|
| inv+0x02, +0x06 | u16 | State flags: 0x80 while the weapon is busy (refire, reload, switch); 0x08 base, 0x02 toggles with movement |
| inv+0x04 | u16 | 0x800: weapon switch requested |
| inv+0x40 | s32 | Refire delay of the current weapon (ticks), copied from `g_weapons[w]+0x04` on switching; negative for thrown weapons |
| inv+0x44 | s32 | Refire delay latched when firing |
| inv+0x48 | s32 | Refire countdown (ticks) |
| inv+0x4C, +0x50 | s32 | Recoil kick (moves and settles during bursts); +0x50 also climbs by 1.0 per shot for weapon 11 |
| inv+0x52 | u16 | 1 on the tick a shot is fired |
| inv+0x54 | u8 | Current slot |
| inv+0x55 | u8 | Current weapon (index into `g_weapons`) |
| inv+0x56 + 2·slot | s16 | Reserve ammo per slot (-1: slot empty) |
| inv+0x5E + 2·slot | s16 | Maximum ammo per slot (from the weapon table) |
| inv+0x66 + 2·slot | s16 | Rounds in the clip per slot. A slot's clip fills the first time it's drawn; an empty clip reloads by itself (~40 vblanks later) |
| inv+0x120 | s32 | Throw wind-up length (ticks: 8, or 15/18 for stick grenades) |
| inv+0x124 | s32 | Throw wind-up counter |
| inv+0x1D4 | | Muzzle flash state (`0x8006DCA0`) |

The level's slots: `g_slot_count` (`0x8009E874`) and `g_slot_weapon` (`0x8009E878`, a byte per
slot, the weapon index). E.g. 1:3 has Garand (12), Thompson (1), shotgun (4), grenades (3).

## Movement (measured)

Scripted keyboard input in 1:1 (`build/re/move.sh`-style: `--kbm`, `VALOR_WATCH` on the
soldier's position `+0x20C`, ground `+0x284`), 30 Hz ticks, y pointing down:

| | Per tick | |
|---|---|---|
| Running forward | 34.8 units | ~1044 units/s (RECON.md: 17.4 per 60 Hz frame) |
| Crouched, forward | 7.9 units | 23% of running |
| Jump | 24 up at take-off, gravity 2 per tick | 156 units high, 23 ticks (0.77 s) in the air |

The player's soldier is set up by `player_soldier_init` (`0x8007BE04`): health and maximum
128.0, eye height 240 (`+0x2D4`), pitch limits `inv+0xB4` 0xD0 and `inv+0xB6` 0x56, `inv+0x8`
the "life" parameter (`g_params+0xC`).

## Weapon table (`g_weapons`, `0x80093640`, 13 × 0x44 bytes, static data in LEVEL.BIN)

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | u32 | Hash (model/sound id) |
| +0x04 | s16 | Refire delay in ticks (30 Hz); negative: thrown/launched (`throw_weapon`) |
| +0x06 | s16 | Ammo carried, used for carry-over (-1: not carried) |
| +0x08 | s16 | Maximum ammo (999 or 99) |
| +0x0A | s16 | Clip size |
| +0x14 | s16 | Firing sound (-1: none) |
| +0x16 | s16 | Sound variant (-1: from the soldier) |
| +0x2A | u16 | Flags: 0x10 no shell casing; bit 1 of (>>3) passed to the sound |
| +0x2C | 16.16 | Damage per bullet |
| (a word in the entry) | char* | Ammo name ("RIFLE ROUNDS", ...) |

| # | Ammo name | Refire | Clip | Max | Damage | Weapon (by the data) |
|---|---|---|---|---|---|---|
| 0 | PISTOL ROUNDS | 12 | 8 | 999 | 1.5 | .45 pistol |
| 1 | SMG ROUNDS | 4 | 32 | 999 | 1.2 | Thompson |
| 2 | BAZOOKA SHELL | -15 | 1 | 99 | 30 | Bazooka |
| 3 | GRENADES | -60 | 1 | 99 | 10 | Grenades |
| 4 | SHOTGUN SHELLS | 25 | 8 | 99 | 2 ×6 pellets | Shotgun |
| 5 | RIFLE ROUNDS | 50 | 5 | 999 | 5 | Sniper rifle |
| 6 | SMG ROUNDS | 2 | 20 | 999 | 1.2 | SMG (Sten?) |
| 7 | — | -45 | 0 | 0 | 0 | Papers (2:x) |
| 8 | PISTOL ROUNDS | 12 | 7 | 999 | 1.5 | Pistol |
| 9 | PISTOL ROUNDS | 12 | 7 | 999 | 1.5 | Silenced pistol (2:x) |
| 10 | STIELHANDGRANATE STICK GRENADES | -60 | 1 | 99 | 10 | Stick grenades |
| 11 | SMG ROUNDS | 3 | 20 | 999 | 3.5 | BAR? (the only weapon with spread and recoil climb) |
| 12 | RIFLE ROUNDS | 20 | 8 | 999 | 2.5 | M1 Garand |

## Carry-over between levels

`g_game` (`0x800385CC`) lives in the exe's data, so it survives overlay loads; it holds the
mission and level (+0x94, +0x98) and, at +0x18C (`g_level_ammo`, `0x80038758`), the ammo each
level starts with: `[mission][level]` rows of 13 halfwords (one per weapon, -1: not carried),
152 bytes per mission and 36 per level.

- `level_end_carry` (`0x8006210C`), at the end of a level: each slot's reserve + clip goes to
  `g_weapons[slot_weapon].+0x06`, then the table's ammo (+0x06) and +0x30 go to `g_level_end`
  (`0x80038D4C`, +0x10 + 4·weapon), with health (`soldier+0xF4`) at +0x08. It also counts
  `0x800988DC` entries and complains if the objectives weren't flagged complete ("Screwed by
  bloody designers!").
- Something outside LEVEL.BIN (the exe or SHELL) copies that into the next level's row.
- `level_read_parameters` (`0x8004A634`), at level start (called from `level_entry`): the
  current level's row goes into `g_weapons[].+0x06`, `g_level_end` is cleared, and the
  "PARAMETER DATA READ" block (mode, mission, level, music, sound, stereo, difficulty, life,
  game, perfect) is printed.

A mission's first level has an empty row and hands out its own loadout. A later level reached
without playing the one before it (`--warp`) has an empty row too, so its weapons had no ammo:
`./play --warp` now fills the row (four clips per weapon).

## Cheat flags (`0x800386A4`, = `g_game+0xD8`)

| Bit | Effect |
|---|---|
| 0x10 | Faster refire (delay scaled by `0x80039D88`) |
| 0x20 | Larger player bullets (bullet object +0x38 = 0xA40) |
| 0x40 | Thrown weapons don't use ammo |
| 0x80 | The player takes no damage |
