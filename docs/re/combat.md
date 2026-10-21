# Combat: firing, bullets, hits

## Firing (`fire_gun`, `0x8007D5C8`)

`fire_gun(soldier, yaw, pitch, fire)`, called from the player update (`0x8007B350`); enemies
fire through the same spawners. With an empty clip it does nothing.

1. **Aim.** Where the shot starts and points (below, Aiming): along the gun's barrel, or from
   the camera when scoped.
2. **Stance.** `inv+0x1C8` (0..9, movement.md) picks, through a jump table at `0x8003D218`,
   whether the shot is aimed at the aim point: stances 0, 1, 7, 8 yes (`projectile_aim_at_point`),
   2..6 and 9 no (the scope or the gun's bones, below). It is not an accuracy setting: nothing
   changes the spread.
3. **The shot**, through a jump table on the weapon (`0x8003D240`):
   - Most guns: one bullet, `bullet_spawn` (`0x80045A78`), type `0x13F6` (weapon 11: `0x13F7`).
   - Shotgun: six pellets, `pellet_spawn` `0x80045F14` (type `0x13F7`, damage 2.0 each; flags B
     `0x24`: swept but **not** nearest-first, below).
   - Bazooka: a rocket, `0x80045CA0` (type `0x13F1`).
   - Weapons 1, 6 and 11 (the SMGs and the BAR) also climb recoil (`inv+0x50` += 1.0, up to
     1.0) and have spread 2^11 (measured: player.md, Weapons in play).
4. **Noise.** `0x8004D8DC(soldier+0x152, 0, 100, 3)` (7 for some weapons): the shot is heard by
   enemies (enemies.md, to come).
5. **Effects.** Muzzle flash (`0x8006DCA0(inv+0x1D4)`), the weapon's sound (`0x80041CE8`;
   louder for weapons 5 and 11), and with ~49% chance `0x80088040` (a shell casing) unless the
   weapon's flag 0x10 is set.
6. **Ammo.** One round off the clip; at 0 the reload starts (`0x8005C97C(soldier, 0x10, 1)`).
7. **Refire.** `inv+0x44/+0x48` = the weapon's delay (cheat 0x10 shortens it).

Thrown and launched weapons go through `throw_weapon` (`0x8007CF4C`): a wind-up
(`inv+0x120/+0x124`), then a projectile entity (`0x80045808`, type 0x5A) at the hand, swept
against the world (`0x8003EF5C`) and tested against every entity (`0x8004CD90`); a hit explodes
at once (`0x8006A76C`, `0x8004D43C`).

## Aiming (the player)

Every tick the player's controls (`player_controls`, `0x8007F300`) end in `aim_update`
(`0x8007E034`), which works out where the crosshair is and steers the gun to it:

1. **The aim point.** A ray from the camera through the crosshair (its screen position
   `inv+0x190`/`+0x192`, the aim distance `inv+0xA4`) is swept against the world in three
   pieces (`world_sweep`); the first wall hit ends it. Every entity it passes (not dead, not
   flagged 0x206 in flags B) is tested against its sphere, and for characters (flags A 0x10400,
   with a box) more exactly (`0x8004B0F0`); the nearest one shortens the ray to it. The end is
   the aim point (`inv+0x1A4..+0x1AC`).
2. **Aim friction.** If the nearest entity on the ray is a character (flags A & 0x50) at least
   1200 units away, `inv` flag 0x100000 is set, and past 2750 units also bit 31: the turning
   and looking speeds are halved, or quartered, while the crosshair is on a distant enemy.
3. **The gun follows the crosshair.** The barrel's far point is projected to the screen and
   the soldier's angular velocity is set from its distance to the crosshair: yaw the full
   difference (half under 9 pixels), pitch 1.25× (1× under 9, 0.5× under 5). The gun converges
   on the crosshair over a few ticks rather than instantly; turning fast, it trails it.
4. **Where shots start** (`inv+0x198`): the camera when the aim point is closer than the
   muzzle (+20 units: flag 0x20000) or in mode 0x200000, else the muzzle.

**Shots** (`projectile_create` → `projectile_aim`, `0x800451B0`), in this order:
- Aimed (the stance flag from `fire_gun`: stances 0, 1, 7, 8, every gun fired normally):
  `projectile_aim_at_point` (`0x80045084`): the start is `inv+0x198` exactly and the angles
  are `angles_from_to(start, inv+0x1A4)` (`0x80044D18`): yaw = `ratan2(dx, dz)`, pitch =
  -`ratan2(dy, SquareRoot0(dx² + dz²))`, with dx, dy, dz the integer parts of the 16.16
  differences (PsyQ's table functions, so a reimplementation needs their tables to match).
  Checked: 416 of 416 player projectiles start exactly at `+0x198` (or one step on, 5, the
  firing tick above) and all 108 unspread rounds have exactly these angles; the spread ones are
  these plus the spread.
- Scoped (`inv` flag 0x100; turning is cut to 1/8; only reached when not aimed): from the
  camera's position along the camera's angles. Checked: 3 scoped rounds start at the camera.
- Otherwise: from the gun, along the line between two points on it (`0x80044E0C`: the model's
  bones `+0xD4`, `+0xD6` with offsets `+0xD8`, `+0xE0`; `0x80044F10`, `0x80044D18` turn the
  line into angles): **along the barrel**, so where the gun points, not where the crosshair is.
  No player shot of the session was fired this way.
- Enemies' shots: angles from the gun to the target point (enemies.md).
- Weapons with flag 2: the start is pulled back toward the shooter by 3/4 of its horizontal
  distance, so the first tick's sweep starts behind the muzzle (no shooting through a wall the
  barrel pokes through).

**Measured** (`VALOR_TRACE_SHOTS=1` on a replay of a 27-minute session, 335 unscoped shots,
`tools/recon/shots.py`): the bullets' lines pass a median 0.8 units (90%: within 0.5° seen from
the camera) from the aim point: where the game's crosshair ray ends is where shots go, the gun's
lag doesn't matter. But that ray is **not the view's centre**: the crosshair rests at screen
line 131 (`inv+0x192`, with the projection centre at 120: `SetGeomOffset(w/2, h/2)`), so the
aim point is a median 2.95° below the view's direction (yaw: centred). The original draws no
crosshair outside aim mode; a crosshair drawn at the screen's centre is ~3° above where the
shots go (30 units at 600, 150 at 3000).

**Turning** (`player_controls`): the turn and look inputs are scaled by 0.5625 (0.375 with
`inv` flag 0x80000), then by the aim friction (½ or ¼) or the scope (⅛), and pitch is clamped
to ±0x2AA (±60°).

## Bullets (`bullet_spawn`, `0x80045A78`)

Bullets are entities, not instant rays:
- **Speed**: a random integer N, 600–669 (`rand() % 70 + 600`; pellets 350–749,
  `rand()·400 >> 15`), kept in `+0x200` (16.16; `entity_move` copies it into the local z
  velocity `+0x220` every tick; `+0x224`, the speed scale, stays 1.0). The player runs at about
  35 per tick. At 3000 units a bullet needs about 5 ticks (0.17 s): a moving target can leave
  the line of fire after the trigger was pulled on it.
- **A tick's move**, exactly: M = `RotMatrixYXZ` of the integer parts of the angles (pitch
  `+0x228`, yaw `+0x22C`; the roll doesn't change the forward column); v = N·16 (the velocity
  >> 12); each axis moves ((M[i][2] · v) >> 12) << 12 in 16.16, i.e. in steps of 1/16 unit.
  No gravity, no slowing down. Checked: every tick of every bullet and pellet in the trace
  (below) moves exactly so, with one speed in the documented range per projectile.
- **Life**: 15 ticks (`projectile_create`'s countdown `obj+0x3A`), then removed: ~9500 units.
- **Spread**: none, except weapons 1, 6 and 11: `(rand() & (2^12 - 1)) - 2^11`, << 9, added to
  pitch, yaw and roll (16.16): ±16 angle units (4096 per turn), ±1.4°, in steps of 1/128; the
  shotgun's pellets ±64 (±5.6°) the same way. Measured (below): Thompson 46 rounds -15.4..+15.9,
  pellets 267 within -64..+63.8; nothing else adds anything (108 unspread rounds exact).
- Tracer: `0x8004572C(bullet, 230, 180, 70, 40, 20, 10)`.
- Touch handler (`bullet+0x37C`) = `bullet_touch` (`0x80043AD4`); hit_part `0x80043E38`.

**The firing tick.** `entities_update` reads each entity's `next` before calling its think
(`0x8006BA7C`), and projectiles are appended at the end of the list. So a projectile only
moves in the tick it was fired if some entity follows its shooter in the list. The player's
soldier is usually the last entity: **the player's bullet stays at the muzzle for the firing
tick and makes its first move in the next one** (measured: 416 of 421 player projectiles were
at their start point after the firing tick; the 5 that had moved all had an entity after the
soldier during the update, 2 of which were gone by the tick's end). It is still tested in
that tick's collision pass, with a path of no length (it hits only a part its start lies
inside: 2 shots in the session, at a soldier within reach). An enemy's bullet moves in its
firing tick (the enemies come before the projectiles). (objects.md says every projectile moves
in its firing tick: not the player's.)

## Hits (`entities_collide` for swept projectiles, `bullet_touch`, `0x80043AD4`)

In the collision pass after all moves (entities.md), each projectile (flags B 0x4) tests its
path this tick, from `+0x258` (where it was when its update began) to where it is now (cut
short at a wall by `entity_move`'s world sweep), against every entity that is not removed
(flags A 0x80000), not flagged 0x2/0x4 in flags B, and shares a bit of the projectile's mask
(`0x08000450` for the player's guns: soldiers have 0x10, dogs 0x40; the bits 0x08000200 are off
for the aimed shot's first `distance / speed + 1` ticks, player.md):

1. **The line against the entity's sphere** (`segment_sphere`, `0x8004CBC8`): d = sphere centre
   (`+0x2E0`) - path start (integer parts), u = the path's direction (`VectorNormalSS`, 4096
   long; the forward column of the matrix for a path of no length); the GTE's outer product
   d × u >> 12 (each saturated to s16), squared, must be ≤ radius² (`+0x2E8`). The infinite
   line, not the segment: no length or direction test here. It also gives the sort key: the
   largest |d| component + (the other two) / 4.
2. **The path against the hit parts** (`segment_parts`, `0x8004C76C`), only for an entity whose
   sphere passed: for each part in order (kind 7 skipped), d = centre - start (s16),
   a = |d|² - radius² (`+0x38`); a hit if a ≤ 0 (the start is inside), or else if d · u > 0,
   |path|² ≥ a, and |d × u >> 12|² ≤ radius². **The part reported is the lowest-numbered one
   crossed, not the nearest**: a path through the chest and the head is a head hit (part 0),
   one through a hand (1, 2) and the chest (7) a hand hit.
3. **Which entity**: with flags B 0x80000 (bullets, rockets) the entities whose parts were
   crossed are handled nearest first by the sort key (entities without parts by the sphere
   alone): `touch(bullet, e)`, `hit_part(bullet, -1, e, part)`, `hit_part(e, part, bullet, -1)`;
   if all three return 0 the bullet goes on to the next nearest. Pellets (no 0x80000) take the
   entities in list order instead and handle each as found; a pellet removed by its first hit
   stops there.

For a character: `bullet_touch` returns 0 (its exact test `0x8004B624` needs plane shapes,
`+0x2EE`, which characters don't have), the bullet's hit_part (`0x80043E38`) removes the bullet
and returns 1 unless the entity is its shooter's (`handle_same(obj+0x54)`), another projectile
(0x13F0..0x13F9) or type 0x25, and the victim's hit_part applies the damage (below). Explosive
rounds (obj+0x38 & 0x100) explode there (`explosion_spawn(.., 6, 6, 75, ..)`). An object with
plane shapes is hit through `bullet_touch` (moved to the hit point, removed; type 0x1402
rings); type 0x1777 is damaged by its own touch handler and the bullet flies on.

A dead soldier (flags A 0x20000000) is still in the pass: its body stops bullets and takes
0-damage events until it is removed.

**Measured** (`tools/sim/combat_sim.py check` on the replay of 1:3, 2:1, 2:2; every projectile
in the state trace, each character's parts recomputed per tick from the pose in RAM; a
vanished projectile's last move tried for every speed in range):

| | Ticks or projectiles | Exact | Within (depends on the unknown speed) | Fails |
|---|---|---|---|---|
| Player bullet ticks that flew on: no part crossed | 296 | 296 | | 0 |
| Player bullets' end: which entity (or none) | 173 | 165 | 6 | 2 |
| Player bullets' part (the hit counters, below) | 92 | 82 | 9 | 1 |
| Pellet ticks that flew on | 619 | 618 | | 1 |
| Pellets' end: which entity | 274 | 237 | 36 | 1 |
| Enemy bullets' end (the player's parts approximated) | 48 | 47 | 1 | 0 |

The fails are all near misses or unexplained single cases: a silenced-pistol round predicted
to cross a soldier's chest by 2 units ended without damage, another predicted 16 units inside
an upper arm too (2:1; something not a character in front, a door or object, whose spheres
aren't watched, is the likely cause), a Garand round counted on a hand whose sphere the path
missed by 1.6 units (it was predicted on the chest), and a pellet. Our floating-point world test
(the level's TSP collision triangles) once stopped a round 20 units short of the soldier it hit;
the game's own sweep (`world_sweep`, 0x8003EF5C) was not reimplemented.

## Damage

`enemy_hit_part(enemy, part, other, other_part)` (`0x80055864`, the soldier's `hit_part`) and
the player's (`0x8007C1D0`) handle a hit:

1. `damage_from_hit(victim, part, attacker, attacker_part)` (`0x8004C1B4`): the damage is the
   bullet's (`+0xF8`, the weapon's damage) or, for a contact between parts, the attacking
   part's `+0x10` (part kinds 5, 6 and 9: an attack). It is scaled by the kind of the victim's
   part (`0x8003A2E0`): kinds 1, 5-9 ×1; 2 ×0.75; 3 ×0.5; 4 ×0.25; 10 ×2; 11 ×4; 12 kills. The
   direction it came from (front/back, `0x8004C108`) goes to the victim's `+0x38` flags for the
   reaction.
2. `apply_damage(victim, damage, attacker, part)` (`0x8004AC58`): nothing while the victim's
   `+0x48` timer runs (invulnerable); ×4 when the player's weapon block word 0 has 0x20000000
   (a cheat); health (`+0xF4`) -= damage; at 0 or below the victim dies (flags A 0x20000000,
   `+0x38` 0x4000), credited to the attacker (`0x80080240`) for enemies with flags B 0x8000. Hits
   on the player also run `0x80060FC4` and `0x80080304` (the damage feedback).
3. The enemy reacts (variables 0x18: what hit it, 0xC: the part, 0xD: health left as a fraction
   of +0xF0, for the scripts, enemies.md), and is marked as shot at by the attacker
   (`0x8004AC58`'s caller passes it on).
4. **Below 10%: killed.** Unless variable 0xD is -1, `enemy_hit_part` then sets it to
   health × 100 / maximum (`+0xF0`; s32, truncated) and, if that is under 10 and the soldier
   isn't dead, `apply_damage(soldier, its remaining health)`: a second event for the rest. It
   matters when a hit leaves a sliver: the Thompson's 1.2 is 0x13333, so five of them leave
   1/65536 of a 6.0 soldier (the damage trace prints 0.000, not dead), and the rule kills him
   (3 times in the session, each exactly so).

Hits from the same type as the victim (soldier on soldier), or from a projectile whose shooter
had flags A 0x4, skip the head and 10% rules and the statistics (the first branch of
`enemy_hit_part`).

All 17 parts of a soldier are kind 1, so every hit does the weapon's damage wherever it
lands, **except the head: a hit on part 0 kills** (below). (An earlier version of this
document said there was no headshot effect; the replay's damage trace and `enemy_hit_part`
show otherwise.)

### Head hits (`enemy_hit_part`, `0x80055864`, part 0)

After the ordinary damage (`damage_from_hit`), if the hit lowered the soldier's health and the
part is 0:
1. With the helmet on (the helmet slot's flags at weapon block `+0x160`: bit 0 set,
   bits 1..4 clear) and `rand` < 50%: the helmet is knocked off (a helmet entity flies,
   `0x80051200`; the slot's flags get bit 1) and, by the helmet's model hash: **0x8A522920 or
   0xAA1714FE: the soldier survives** (sound 0x23: the steel helmet rings, inferred); any other
   helmet: `apply_damage(soldier, its remaining health)`, killed (sound 0x25).
2. Otherwise (no helmet, or the other 50%): `apply_damage(soldier, its remaining health)`:
   **killed**.
3. The head-hit statistic `0x80039CC0` +1 (not for explosions).

So a head shot kills outright unless it knocks off one of two helmet models (50% while the
helmet is on); the first hit's own damage is applied first, then the rest of the health as a
second damage event from the same bullet. Dogs (type 0x1405) use their own handler.

### Damage measured

`VALOR_TRACE_DAMAGE=1` over the replay of the recorded session (1:3, 2:1, 2:2: 51 soldiers and
14 dogs killed; 2:3 left out, the replay stops matching the recording there). Every value is
the weapon's damage (×1, kind 1), apart from the head kills' second events:

| Attacker | Damage per hit | Notes |
|---|---|---|
| Garand (12) | 2.5 | 3 body hits for a 6.0 soldier, 2 for a 4.0 dog |
| Thompson (1) | 1.2 | 5 body hits for 6.0 |
| Shotgun (4) | 2.0 per pellet | 3 pellets kill a 6.0 soldier |
| Silenced pistol (9) | 1.5 | |
| Sniper rifle (5) | 5.0 | 2 body hits for 6.0 or 8.0 |
| Head kill (second event) | the remaining health | 26 of the 51 soldier kills: Garand 2, Thompson 2, shotgun 4, silenced pistol 13, sniper rifle 5 |
| Enemy bullet (type 0x13F4) | 1:3: 7.0 and 2.0; 2:1: 4.0, 8.0, 10.0; 2:2: 5.0, 6.0 | The soldier record's `+0x64` per soldier (enemies.md); the player has 128.0 |
| Dog (type 0x1405) | 2.0 | Contact between parts (the player's parts 1 and 2) |
| Explosion (type 0x31) | 4.0 per tick | On objects (type 0x1777) in 2:2 |

Soldiers' starting health (first hit's "before"): 1:3 6.0 (all 28), dogs 4.0; 2:1 4.0, 6.0,
8.0 and 10.0; 2:2 6.0 and 8.0: per record, not per level. Events of 0.0 damage are the head
kill's second event on a soldier the first hit had already killed; events of a weapon's damage
with health 0 → 0 are hits on a body (a dead soldier or dog stays in the collision pass).

Checked event by event (`tools/sim/combat_sim.py check`, 365 events of the three levels): the
arithmetic (health - damage, clamped at 0) in all 365; the player's projectiles' damage = the
weapon table's `+0x2C` (2.0 for pellets) in 199 soldier, 71 dog and 2 object events; 23 head
kills and 3 below-10% kills give exactly the second event; no other hit has one (117); in every
frame the head counter (`0x80039CC0`) equals the number of head kills (122 frames): no helmet
saved anyone in this session. Dog bites 2.0 (3), explosions 4.0 a tick (56), enemy bullets the
values in the table (34).

Hit parts also matter for:
- **The helmet** (above).
- **Statistics** live in the exe's data (from `0x80039CA8`), so the results screen can read
  them after the level; the player's weapon block keeps its own copies:

| Address | Counts | Player copy |
|---|---|---|
| `0x80039CA8` | Shots fired (`fire_gun`, `throw_weapon`) | |
| `0x80039CAC` | Shots that hit (once per bullet) | `inv+0x148` |
| `0x80039CB0` | Times the player was wounded (`0x80080304`) | `inv+0x144` |
| `0x80039CB4` | Melee hits? (`0x8008034C`, attack parts 5/6/9) | `inv+0x14C` |
| `0x80039CB8` | Kills (`0x80080240`); also per weapon at `g_weapons[w]+0x30`, carried to the next level with the ammo | `inv+0x142` |
| `0x80039CC0` | Head hits (part 0, the helmet) | |
| `0x80039CC4` | Torso hits (parts 3, 4, 7, 8) | |
| `0x80039CC8` | Arm hits (parts 1, 2, 5, 6) | |
| `0x80039CCC` | Leg hits (parts 9..16) | |
| `0x80039CD0`..`0x80039D0C` | Hits per part, **in this order**: 3, 4, 5, 6, 1, 2, 7, 8, 9..16 (`enemy_hit_part`'s switch; an explosion's hit on parts 0, 1, 2 counts as torso and part 7) | |

These count only hits that lowered the soldier's health (a body's don't), on soldiers (dogs have
their own handler; their hits count in `0x80039CAC` only). The check above uses them, watched
every frame, as the part each hit landed on. (An earlier version of this table had the per-part
counters in part order.)

  The groups follow the parts' places on a 1:3 soldier (part 0 at the top, 1/2/5/6 about 50
  units to the sides, 9..16 the lower 120 units, 7 and 8 the two big spheres in the middle).

## The player being hit (`player_hit_part`, `0x8007C1D0`)

- Touching the **mounted gun** (type 0x1772) while facing it and pressing Use (action 5) mounts
  it: weapon-block flag 0x4000, the gun's handle at `inv+0x1B0`, the player's think becomes
  `0x8007A32C`; the gun (`mounted_gun_think`, `0x8005DA24`) then turns and fires with the
  player's controls.
- A soldier or dog touching with an attack part (kind 5) set does melee damage.
- An explosion (type 0x31) within 300 units sets its part's damage to 1000: death.
- Otherwise `damage_from_hit` as for enemies, **unless cheat bit 0x80** (god mode). There is no
  armour: health (128.0) takes the damage as is.
- When health dropped: collision radius 200, the attacker's type in `inv+0x1C0`, a view kick
  away from the attacker (`inv+0x80..+0x88`), a flinch timer (`inv+0x154` = 8), a camera shake
  (`0x80046C50`), a grunt 30% of the time, a hit sound, and noise for the enemies
  (`0x8004D8DC(.., 1, 255, 2)`).

**Enemy fire, measured** (the same check, 48 enemy bullets visible in the state trace): 40 are
aimed at the player's position as the previous tick left him, raised by 120 (half the eye
height), from one step behind the bullet's first position (an enemy's bullet moves in its
firing tick), plus an error within ±100 angle units (|pitch| median 25, max 88; |yaw| median
26, max 84: enemies.md's E, 20..100); the other 8 are the death spray (soldier animation 53,
fire 3 along the falling gun, soldier.md). Their ends against the player's parts (1, 2 at eye
height, 3 below, from the template offsets through the soldier's matrix; part 0, kind 7, never)
agree in 47 of 48. The damage is the shooter's record value (the table above).

## Explosions (`explosion_spawn`, `0x800719C0`)

Grenades, rockets, destructibles and the level built-in `explosion` all make an explosion
entity (type 0x31): flags A 0x01228000, collision mask 0x88004451 (it touches characters and
objects), `hit_part` = `damage_from_hit`, so it does its damage through the ordinary contact
pass (entities.md) like a bullet's part, with the damage in its variable 0 (the caller's).
Its hit parts are N fireballs (N the caller's), each placed at a random offset within ±spread
of the centre (kind 3), and they damage whatever they touch while the explosion lasts; when
its life runs out the parts and the radius go to 0. On its first tick it makes the
particles (`0x80070DEC`), shakes the camera if the player is within 3000 units (strength
(3000 - distance) / 16, `0x80046C50`) and makes a noise of that strength that enemies hear
(`0x8004D8DC`). A destructible (`0x1770`) is damaged by explosions as by bullets.

## Checked against the original (`tools/sim/combat_sim.py`)

`combat_sim.py check TRACE LOG` reimplements this document (with animation.md's poses and hit
spheres and player.md's trigger) and steps it against a replay of the recorded session made
with `VALOR_TRACE_STATE`, `VALOR_TRACE_SHOTS`, `VALOR_TRACE_DAMAGE` and a watch of the five
character slots, their pose buffers, the pad, the weapon block and the hit counters (the
script's docstring has the command). The random draws are taken from the outcome and checked
against their ranges: the speed from the first move (or every speed tried when the projectile
vanished on its first move), the spread from the angles, the helmet roll from the events.
Agreement (exact / within a documented range / fails):

| Behaviour | n | Exact | Within | Fails |
|---|---|---|---|---|
| Poses from the animation data, joints, the pose cache (animation.md) | 47907 | 47907 | | 0 |
| Hit part centres, collision sphere, entity matrix, every frame of the five slots (animation.md) | 103056 | 102978 | | 78 (pushes in the collision pass, level starts, 5 off by one) |
| Trigger: a shot exactly when the refire countdown is 0 and fire pressed (held after a shot), not during a weapon script (player.md) | 16977 ticks | 16975 | 2 | 0 |
| Reloads: on Use / on an empty clip when the countdown runs out; the rounds move at once | 82 / 7 / 7 | all | | 0 |
| Start = `inv+0x198`; unspread angles = `angles_from_to(start, aim point)`; scoped from the camera | 416 / 108 / 3 | all | | 0 |
| Spread ±16 (Thompson) / ±64 (pellets), steps of 1/128 | 46 / 267 | | all | 0 |
| The firing tick: still at the start unless an entity followed the soldier | 421 | 416 | 3 | 2 |
| Flight: one speed in range, 1/16 steps along the angles, 15-tick life | 517 | 274 | 243 | 0 |
| Hits: which entity, which part, nothing crossed on the ticks between (Hits, above) | 1632 | 1563 | 62 | 7 |
| Damage: values, clamping, head and 10% second events, the head counter | 878 | 844 | 34 | 0 |
| Enemy aim within ±100 of the player's mid-height, or the death spray | 48 | | 48 | 0 |

"Within" is a random draw taken from the outcome (a speed, a spread) or an outcome that depends
on a draw the trace doesn't show (a projectile that vanished on its first move: every speed
tried). The 2 firing-tick fails had an entity after the soldier during the update that was gone
by the end of the tick (inferred: the state trace lists only the survivors).

## Open

- `VectorNormalSS` (the path's direction) is approximated in `combat_sim.py` by rounding; it only
  matters on a sphere's edge, and three of the remaining hit fails are within 2 units of one.
- The game's world sweep for projectiles (`world_sweep`, `0x8003EF5C`) isn't reimplemented: the
  check cuts paths with a floating-point test on the TSP collision triangles, which once
  stopped a round the game let through.
- What stops a projectile that isn't a character (doors 0x1778, objects with plane shapes,
  `+0x2EE`) wasn't checked: their spheres weren't watched.
- No helmet saved a soldier in the session, so the 50% roll and the two saving hashes are from
  the code only.
- (The fireballs' size, growth and damage per caller: objects.md.)
- Which helmet models carry the hashes 0x8A522920 / 0xAA1714FE (the ones that save a soldier),
  and which levels' soldiers wear them.
- The results screen's medals (SHELL.BIN, `m_medals`); the rating itself is in levels.md.
