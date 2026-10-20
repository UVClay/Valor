# Combat: firing, bullets, hits

## Firing (`fire_gun`, `0x8007D5C8`)

`fire_gun(soldier, yaw, pitch, fire)`, called from the player update (`0x8007B350`); enemies
fire through the same spawners. With an empty clip it does nothing.

1. **Aim.** The view's angles, unless the soldier is in aim mode (`soldier+0x100 → +0x84`):
   then the angles and origin come from the reticle (`→ +0x8`: +0xD4/+0xD6 angles, +0xD8/+0xE0
   position).
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

All 17 parts of a soldier are kind 1, so **every hit does the weapon's damage, wherever it
lands**: there are no headshot multipliers in MoH. Measured with `VALOR_TRACE_DAMAGE` over a
replay of 1:3 and 2:1:

| Attacker | Damage | Notes |
|---|---|---|
| Garand (12) | 2.5 | 3 hits for a 1:3 soldier (6.0 health) |
| Thompson (1) | 1.2 | 5 hits |
| Shotgun (4) | 2.0 per pellet | 3 of 6 pellets kill |
| Silenced pistol (9) | 1.5 | 2:1 soldiers have 4.0 health: 3 hits |
| Enemy bullet (type 0x13F4) | 7.0 rifle, 2.0 SMG (1:3); 4.0 (2:1) | The player has 128.0 |
| Type 0x1405 (a dog?) | 2.0 | Contact between parts (the player's parts 1 and 2) |

Hit parts still matter:
- **Part 0 is the helmet**: a hit there knocks it off half the time (a helmet entity flies,
  `0x80051200`, with a sound by helmet model).
- **Statistics** (in the exe's data, so the results screen can read them after the level):
  shots fired `0x80039CA8` (`fire_gun`, `throw_weapon`), shots that hit `0x80039CAC` (once per
  bullet), hits per part group `0x80039CC4`/`CC8`/`CCC` and per part `0x80039CD0`..`0x80039D0C`
  (17 counters), helmet hits `0x80039CC0`.

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

## Open

- What the results screen shows from those counters, and what the part groups are.
