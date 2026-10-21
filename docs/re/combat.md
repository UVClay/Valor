# Combat: firing, bullets, hits

## Firing (`fire_gun`, `0x8007D5C8`)

`fire_gun(soldier, yaw, pitch, fire)`, called from the player update (`0x8007B350`); enemies
fire through the same spawners. With an empty clip it does nothing.

1. **Aim.** Where the shot starts and points (below, Aiming): along the gun's barrel, or from
   the camera when scoped.
2. **Stance.** `inv+0x1C8` (0..9) picks an accuracy flag through a jump table at `0x8003D218`.
3. **The shot**, through a jump table on the weapon (`0x8003D240`):
   - Most guns: one bullet, `bullet_spawn` (`0x80045A78`), type `0x13F6` (weapon 11: `0x13F7`).
   - Shotgun: six pellets, `0x80045F14` (type `0x13F7`, damage 2.0 each).
   - Bazooka: a rocket, `0x80045CA0` (type `0x13F1`).
   - Weapon 11 also climbs recoil (`inv+0x50` += 1.0, up to 1.0) and has spread 2^11.
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

**Shots** (`projectile_create` → `0x800451B0`):
- Scoped (`inv` flag 0x100; turning is cut to 1/8): from the camera's position along the
  camera's angles, exactly where the crosshair is.
- Otherwise: from the gun, along the line between two points on it (`0x80044E0C`: the model's
  bones `+0xD4`, `+0xD6` with offsets `+0xD8`, `+0xE0`; `0x80044F10`, `0x80044D18` turn the
  line into angles): **along the barrel**, so where the gun points, not where the crosshair is.
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
- Speed: random 600–669 units per tick (`+0x224`), along the bullet's angles (`+0x228`: pitch,
  yaw); the player runs at about 35 per tick. At 3000 units a bullet needs about 5 ticks (0.17 s): a moving target can
  leave the line of fire after the trigger was pulled on it.
- Spread: none, except weapon 11: ±2^11 ×512 (16.16) added to pitch and yaw, ±16 angle units
  (4096 per turn): ±1.4°.
- Tracer: `0x8004572C(bullet, 230, 180, 70, 40, 20, 10)`.
- Touch handler (`bullet+0x37C`) = `bullet_touch` (`0x80043AD4`).

## Hits (`bullet_touch`, `0x80043AD4`)

Called by the entity physics as `touch(bullet, other)` when they overlap:
1. Ignores the shooter's own group (`0x8006D7D0(obj+0x54, &other)`) and other projectiles
   (types 0x13F0..0x13F9).
2. Exact test against the target: `0x8004B624` (bullet flag 4: returns the hit point and
   normal) or `0x8004B6DC`. A miss lets the bullet fly on.
3. On a hit: the bullet moves to the hit point; explosive rounds (obj+0x38 & 0x100) explode
   (`0x800719C0`); targets with flag 0x4000 bleed (`0x80043140`, particles `0x80073BA8`); type
   0x1402 ricochets (sound); the bullet is removed (`0x8006B8F0`).
4. The target's own touch handler (`other+0x37C`) is called with the bullet: damage is applied
   there (enemies.md, to come).

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
kill's second event on a soldier the first hit had already killed.

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
| `0x80039CD0`..`0x80039D0C` | Hits per part (1..16) | |

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

## Open

- (The fireballs' size, growth and damage per caller: objects.md.)
- Which helmet models carry the hashes 0x8A522920 / 0xAA1714FE (the ones that save a soldier),
  and which levels' soldiers wear them.
- The results screen's medals (SHELL.BIN, `m_medals`); the rating itself is in levels.md.
