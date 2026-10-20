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
- Speed: random 600–669 units per tick (16.16 velocity at bullet+0x228..0x230); the player runs
  at about 35 per tick. At 3000 units a bullet needs about 5 ticks (0.17 s): a moving target can
  leave the line of fire after the trigger was pulled on it.
- Spread: none, except weapon 11 (±2^11 per axis, then ×512, added to the velocity).
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

## Open

- The entity physics that finds the overlap (how far a bullet is swept per tick, the target's
  collision shape): entities.md.
- How a target's handler turns the bullet's damage into health loss (hit zones?).
