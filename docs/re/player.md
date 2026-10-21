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
| soldier+0x1C8 | s32 | Stance (0..9; in `fire_gun` it picks whether a shot is aimed at the aim point: 0, 1, 7, 8 yes; combat.md) |

### Weapon block (`inv = soldier+0x3CC`)

| Offset | Type | Meaning |
|---|---|---|
| inv+0x02, +0x06 | u16 | State flags: 0x80 while the weapon is busy (refire, reload, switch); 0x08 base, 0x02 toggles with movement |
| inv+0x04 | u16 | 0x800: weapon switch requested |
| inv+0x40 | s32 | Refire delay of the current weapon (ticks), copied from `g_weapons[w]+0x04` on switching; negative for thrown weapons |
| inv+0x44 | s32 | Refire delay latched when firing |
| inv+0x48 | s32 | Refire countdown (ticks) |
| inv+0x4C, +0x50 | s32 | Recoil kick (moves and settles during bursts); +0x50 also climbs by 1.0 per shot for weapons 1, 6, 11 |
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
| Crouched, forward | 8.716 units | a quarter of running, once the 8-tick crouch ends (movement.md) |
| Jump | 24 up at take-off, gravity 2 per tick | 156 units high, 23 ticks (0.77 s) in the air |

On a ladder (levels.md, Triggers: `inv+0x70` the top) the forward input climbs at half its
value instead of walking, with gravity off.

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
| 11 | SMG ROUNDS | 3 | 20 | 999 | 3.5 | BAR (spread and recoil climb, like 1 and 6: below) |
| 12 | RIFLE ROUNDS | 20 | 8 | 999 | 2.5 | M1 Garand |

## Weapons in play

How each weapon fires, reloads and switches, so firing can be rebuilt tick for tick. Read from
`player_update` (`0x8007A6B0`), `player_weapon_update` (`0x8007B350`), `fire_gun`
(`0x8007D5C8`), `bullet_spawn`/`pellet_spawn`/`rocket_spawn`, `projectile_aim` (`0x800451B0`)
and `weapon_reload` (`0x800460F4`); measured with scripted runs (`--warp`, `--kbm fire:N`,
`VALOR_WATCH="sc=[8009D654]+11C:4;inv=[8009D654]+40C:30"`, `VALOR_TRACE_SHOTS=1`) in 1:1,
1:3, 2:2, 2:3, 4:1, 5:1 and 7:1. `tools/recon/mv_weapons.py LOG` turns such a log into shot
gaps, reload and switch lengths; `tools/recon/mv_spread.py LOG` measures each bullet against
the line to the aim point.

### The trigger

Every tick, in `player_update` (guns, not grenades):

```
if refire_count (inv+0x48) > 0:  refire_count -= 1
elif (latched (inv+0x44) > 0 ? fire held : fire pressed):  soldier+0x3C |= 2
```

The bit becomes the fire variable (variable 1) and `player_weapon_update` calls `fire_gun` **in
the same tick** (measured: the press tick's bullet is logged on that tick), **unless the
soldier's weapon script is busy**: its script number (`soldier+0x11E`, the one that also sets
the weapon block's busy flag 0x800000) is 2, 9 (reload), 10 (lower), 13 or 16 (raise). Firing
sets `latched = refire_count = |weapon +0x04|` (an s16; the halfword at +0x06 is something
else). "Pressed" is the fire button's mask (`0x800B7790`, port × 9 + action) down now and not
at the previous poll (`0x80093B7C`, `0x80093B84`); the game polls again for each extra tick of
a frame, so only a frame's first tick can see a press. So:

- **Every gun with a positive delay is automatic when held**, the Garand and pistols too: one
  shot every **delay + 1 ticks** (measured for all of 1, 4, 5, 6, 8, 9, 11, 12: 5, 26, 51, 3,
  13, 13, 4, 21 ticks). The first shot of a weapon needs a press (`latched` starts at 0).
- A negative delay (bazooka -15) latches a negative value: **one press per shot** (measured:
  holding fire through the reload fired one rocket).
- A press while a reload, draw or other busy script runs is lost. A hold fires as soon as the
  script ends if the weapon has fired before (`latched` > 0); if not, it needs a new press
  (measured: fire held from inside the Thompson's first reload never fired).

Checked tick by tick over the replay of 1:3, 2:1 and 2:2 (`tools/sim/combat_sim.py check`,
stepping this rule from the refire state the watch shows at the end of the previous frame,
with the frame's pad and the soldier's script number): a shot exactly when predicted in 16975
of 16977 ticks; the other 2 are in frames of several ticks where the reload script ended
inside the frame, where the watch can't show at which tick. 220 shots in all (Garand, Thompson,
shotgun, silenced pistol, sniper rifle).

### Reload

`weapon_reload` moves the rounds **at once** (clip = min(clip size, clip + reserve), the reserve
drops by as much), then starts the soldier's script 9, the reload animation. Firing is blocked
until it ends: the script's frame count (`soldier+0x11C` when it starts) + 1 ticks. It runs:

- by itself when the clip is empty and the refire countdown has run out (measured: the tick the
  countdown reaches 0, e.g. 20 ticks after the Garand's 8th round; every one in the session);
- on Use (action 5) unless the weapon has flag 1, if the clip isn't full (`g_weapons[w]+0x0A`)
  and there is reserve, and no weapon script runs (checked: every Use press of the session);
- when a slot is drawn with an empty clip (the clip "fills the first time it's drawn").

A weapon switch during a reload cuts the animation (the rounds already moved; measured, weapon 6).

### Switching

Action 2 (`weapon_holster` `0x80080380`): the soldier's script 10 lowers the weapon (its frame
count + 2 ticks), the weapon changes, then script 16 raises the new one (frame count ticks) or,
if its clip is empty, script 9 reloads it. Measured lengths in ticks:

| # | Weapon | Ticks between shots (held) | Clip | Reload | Lower | Raise | Spread | Notes |
|---|---|---|---|---|---|---|---|---|
| 0 | .45 pistol (3:x) | 13 (code) | 8 | - | - | - | none | not measured (3:2 didn't start warped) |
| 1 | SMG (Thompson; MP40 in German levels) | **5** | 32 | **51** | 14 | - | ±16 | recoil climb |
| 2 | Bazooka | one per press; 15 then the reload | 1 | **29** | 17 | - | none | rocket below |
| 3 | Grenade | one per release; 60 between throws | 1 | - | 14 | 19 | - | objects.md, Grenades |
| 4 | Shotgun | **26** | 8 | **32** | 19 | 22 | 6 pellets ±64 | all 8 shells in one reload |
| 5 | Sniper rifle | **51** | 5 | **33** | 18 | - | none | scope below |
| 6 | SMG (4:x, 7:x) | **3** | 20 | **51** | 14 | - | ±16 | recoil climb |
| 7 | Papers | - | - | - | 6 | 4 | - | |
| 8 | Pistol | **13** | 7 | **29** | 10 | 12 | none | |
| 9 | Silenced pistol | **13** | 7 | **29** | 10 | 12 | none | |
| 10 | Stick grenade | one per release; 60 | 1 | - | 9 | 21 | - | wind-up 18 or 15 |
| 11 | BAR | **4** | 20 | **36** | 18 | - | ±16 | recoil climb |
| 12 | M1 Garand | **21** | 8 | **36** | 18 | 22 | none | |

Bold: measured. Lower and raise are the scripts' frame counts as logged (+2 and +0 ticks), "-"
where the run went to a reload instead. Spread in angle units (4096 per turn): ±16 = ±1.4°,
±64 = ±5.6°.

### Where shots go

`fire_gun` passes `projectile_aim` a flag from the stance (`inv+0x1C8`, movement.md):

| Stance | Weapons | Start | Direction |
|---|---|---|---|
| 0, 1, 7, 8 | guns aimed or not (sniper unscoped) | `inv+0x198` (the muzzle, or the camera when the aim point is nearer) | **toward the aim point** `inv+0x1A4` (the crosshair ray's end, combat.md) |
| 2 (scope, `inv` 0x100) | sniper | the camera's position | toward the camera's look point: **the view's centre** (0.57° down), not the crosshair line |
| 5, 9 (bazooka), 6, 9 (grenades) | | the gun/hand bone point (`gun_points`) | the soldier's body angles `+0x228`, which in these stances are the view's angles |

Measured: 8 Garand rounds 0.2 units from the line to the aim point (rounding only); a scoped
shot left from the camera at the view yaw, 0.62° below the view (the look point's 0.57° plus
rounding). Exactly, over the replay of 1:3, 2:1 and 2:2 (`tools/sim/combat_sim.py check`, the
weapon block watched each frame): every aimed projectile starts at `inv+0x198` (416), every
unspread round's angles are `angles_from_to(inv+0x198, inv+0x1A4)` to the angle unit (108,
combat.md has the formula), and 3 scoped rounds start at the camera. Aimed at the aim point, a bullet's collision mask loses bits 0x08000200 until
`distance / speed + 1` ticks have passed (`0x80045084` stores the full mask in `obj+0x3C`, the
countdown in `obj+0x40`; `projectile_think` restores it).

**Spread** is added after aiming, uniform and independent per axis: pitch, yaw and roll each
`+= (rand() & (2·2^n - 1)) - 2^n` units of 1/512 angle unit, n = 11 for weapons 1, 6, 11
(±16 units), none for the others; each shotgun pellet n = 13 (±64). **Nothing changes it**:
not moving, crouching, aiming or the stance (measured on the Thompson, 47 rounds standing,
running and crouched: every deviation within ±16, mean 0, the same spread in all three).

**Recoil climb** (weapons 1, 6, 11, measured on 1): each shot adds 1.0 to the climb
(`inv+0x50`, at most 1.0); the kick (`inv+0x4C`) lifts the crosshair ray by its integer part in
pixels (movement.md, Recoil kick). Held, the Thompson nets about +1.25 pixels per shot (each
5-tick cycle adds 1 + 0.875 + 0.75 + 0.625 + 0.5 and settles 5 × 0.5): measured 14.5 pixels
after 20 rounds, near the 15.0 cap; it settles 0.5 per tick after release.

**Bullets**: speed 600..669 per tick (rand % 70), pellets 350..749 (rand·400 >> 15),
`bullet_spawn` damage the table's +0x2C (weapon 12 passes 2.5 itself, pellets 2.0 each, the
rocket 30.0). They move by `+0x200` along their angles each tick, in 1/16-unit steps, for 15
ticks (combat.md, Bullets). **The player's projectiles don't move in the tick they're fired**
(objects.md says they do): the entity loop has already read the soldier's `next`, null when the
soldier is last, so the new projectile is first moved the next tick and sits at `+0x198` for
one tick (416 of 421 in the replay; the other 5 had an entity after the soldier).

### The scope (sniper, aiming)

Aim (R2) with weapon 5 sets `inv` 0x20, 0x40 and 0x100. The projection distance `inv+0xA4`
grows from 400 by a step of 3, the step growing by 2 per tick up to 65 (measured: 403, 408,
415, ... then +65 per tick), **until it is at or past 2447: it stops at 2463** after 46 ticks
(6.2×). Turning and pitch input are divided by 8 while scoped. Releasing aim puts it back to
400 at once.

### The bazooka's rocket

`rocket_spawn` (`0x80045CA0`): type 0x13F1, model 0x9EED43D5, **200 units per tick** along the
view's direction from the gun, **no gravity** (flags A lack 0x20), life **30 ticks** (about 6000
units), damage 30.0 to what it hits, a smoke trail (particles) and the explosion light at once.
Measured (7:1): +200 in z per tick, x and y unchanged, 15 ticks to a wall 3000 away. On the
world it explodes (10 fireballs, radius 650, objects.md), on an entity (6, radius 375), and at
the end of its life it is removed **without exploding** (`projectile_think`: the life runs out
before any fuse; the rocket has no fuse bit). Then the 15-tick countdown and the 29-tick reload:
44 ticks before the next press can fire.

## Items and the papers (pickup kinds 15..19)

`item_pickup_touch` (`0x800832C0`) handles pickup table entries 15..19 (`0x8009E8E8`): touching
one sends a level message and removes it: 15 → 0x2F, 16 → 0x30, 17 → 0x31, 18 → 0x69,
19 → 0x6A (read from the code; levels: 2:1 has 15, 16; 2:2 16; 3:1 15, 16, 17; catalogue.md).

The player's level script (13, the player start record's; read in 1:1's with `lvs.py`) turns
them into a **clearance level**, `ent[1]`: 0x2F → 1, 0x30 → 2, 0x31 → 3, each unregistering the
lower ones (so the highest taken wins). A papers check (message 0x32, sent by a checking soldier,
soldier.md) is answered with 0x34, 0x35 or 0x36 for clearance 1, 2, 3, or 0x6B with none. The
same script counts low-ceiling zones (0xD/0xE from 128, `set_flags` 4/8: the player's flags B
0x800, movement.md) and undetectable zones (8/9 → `set_flags` 1/2), makes an explosion at the
player on 0x66, and shows "THIS DOOR IS LOCKED" on 0x6C.

## Pickups (entity type 101)

Pickups are touched (`touch`, `+0x37C`) by the player; not by dead or special entities (flags A
0x20180000).

- **Health** (`health_pickup_touch`, `0x80082E90`): ignored at full health (128.0); otherwise
  health += the pack's amount, at most 128.0, with a message (`0x8005F3E8`; the texts at
  `0x8009E864`..`0x8009E86C` by kind) and a sound (0x1F, 0x1E). The amounts, set when the pickup
  is made (`0x80083058`): kind 0 → 13.0, kind 1 → 32.0, kind 2 → 64.0.
- **Ammo** (`ammo_pickup_touch`, `0x800830BC`): the pickup's kind (object `+0x0`) indexes the
  table at `0x8009E8E8` (20 bytes each; `+0xA` the weapon). If one of the level's slots holds that
  weapon and its reserve is below the maximum: the ammo's name as a message (`g_weapons[w]+0x3C`,
  e.g. "RIFLE ROUNDS"), sound 0x20, `0x8004301C(player, slot, amount)` with the amount from the
  level record's byte `+0x5C`, and the pickup is removed. With a full reserve it stays.

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
