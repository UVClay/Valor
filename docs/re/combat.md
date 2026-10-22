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
3. **The shot**, through a jump table on the weapon (`0x8003D240`, 13 entries; the damage is
   the weapon table's `+0x2C` unless given):
   - Weapons 0, 5, 8, 9: one bullet, `bullet_spawn` (`0x80045A78`), type `0x13F6`.
   - Weapon 12: the same with damage 2.5 (0x28000) passed directly.
   - Weapons 1, 6, 11 (the SMGs and the BAR): one bullet of type **`0x13F7`** with spread 11
     (below), and the recoil climb `inv+0x50` += 1.0, at most 1.0.
   - Weapon 4 (shotgun): six pellets, `pellet_spawn` `0x80045F14` (type `0x13F7`, damage 2.0
     each; flags B `0x24`: swept but **not** nearest-first, below); its fire sound's volume
     0x20000. Each pellet: speed (rand·400 >> 15) + 350 drawn first, `projectile_create` as for
     bullets (life 15, flags A 0x2000), touch `bullet_touch`, the tracer, then three draws:
     pitch, yaw and roll += ((rand & 0x3FFF) - 0x2000) << 9.
   - Weapon 2 (bazooka): a rocket, `0x80045CA0` (type `0x13F1`).
   - Weapons 3, 7, 10: nothing here (thrown: `throw_weapon`; 7 has its own case in
     `player_weapon_update`).
   Every bullet call is `bullet_spawn(soldier, boneA, boneB, 0, 0, offA, offB, damage, spread,
   aimed, type, 0x2000, 0x20, 0x08000450)`; the bones are the weapon table's `+0xC`/`+0xE` as
   `player_weapon_update` passes them, replaced by the body model's `+0xD4`/`+0xD6` and offsets
   `+0xD8`/`+0xE0` when the instance's `+0x84` is set. The shots counter `0x80039CA8` += 1
   before the shot.
4. **Rumble** (`0x8004D8DC(port = inv+0x152, 0, 100, 3 (7 for the rocket and pellets), 0)`,
   not enemy hearing): by the vibration setting `0x80093A04` (0 off: nothing; 1 the strength
   halved; 2 three quarters; 3 full) it writes the port's two bytes at `0x80093BE8 + 2·port`
   (0, strength), `0x80093BFC + 4·port` = the duration, `0x80093BF4 + 4·port` = (1 << 0) - 1.
5. **Effects**, in this order: the projectile's `+0x3CA` = the weapon; unless `inv` word 0 has
   0x200040, the muzzle flash (`0x8006DCA0`: `inv+0x1D6` = 1); the weapon's sound if its
   `+0x14` ≥ 0: `sound_play(bank +0x16 (< 0: the body model's +0xC8), sound, soldier, 0x17FFF
   (0x20000 for weapons 4, 5, 11), 0, pitch 1.0, pitch spread 0x1800, 0, 0, class 0,
   flags (weapon flags >> 3) & 2)`; then, unless weapon 7 or weapon flag 0x10, one `rand()`:
   `rand·100 >> 15 ≥ 51` → a shell casing `casing_spawn(soldier, soldier's position, pitch
   1.25)` (below).
6. **Ammo.** The clip `inv+0x66 + 2·slot` -= 1; at 0 variable 0x10 = 1 (the reload).
7. **Refire.** `inv+0x44` = `inv+0x40` (the delay set by the weapon), `inv+0x48` = its
   absolute value (cheat 0x10 shortens it). Then `ammo_hud_update` (drawing only).

### The projectile (`projectile_create`, `0x8004532C`), in its order

`bullet_spawn` draws the speed first (`rand() % 70 + 600`, << 16) and calls
`projectile_create(owner, type, -1, speed, boneA, boneB, 0, 0x20, damage, life 15, 0, 0, offA,
offB, aimed, flagsA 0x2000, flagsB 0x20, mask)`:
1. `entity_create(0 (no instance), object 0x90, no model, 2 parts)`. The part count `+0x36`
   stays 0 (only `model_instance_init` sets it), so a projectile has no hit parts of its own.
2. Handlers: think `projectile_think` (`0x80044470`), draw `0x80044B0C`, hit_part `0x80043E38`,
   world_contact `projectile_world_contact` (`0x80043284`), destructor `0x80044BE4`.
3. Type; flags A = 0x2000 | 0x108 (| the owner's flag 4); mask; flags B |= 0x200 | 0x20.
4. Damage `+0xF8` (×4 for the player with weapon block word 0 bit 0x20000000).
5. `+0x30` (attached) none, `+0x34` = -1; object: `+0x38` = 0, `+0x54` = the owner's handle,
   `+0x3A` = life, `+0x2C`/`+0x30` = 0, `+0x42` = 0, `+0x34` = 1.0.
6. Velocity 0; `+0x200` = the speed; `projectile_aim` (Aiming, below).
7. `+0x284` = 0 (not on the ground), object `+0x58`, `+0x44` = 0; the matrix (`0x8006A76C`);
   `+0x258` = the position.
8. Weapons with flag 2 (the player's): the start pulled back along the projectile's forward
   column by d - d/4, d = the larger of |dx|, |dz| (projectile minus shooter, integer) plus a
   quarter of the smaller.
9. The parts' `+0x3C` cleared; the parts box (`0x8004CDB0`: `+0x308..+0x312` = min/max of the
   parts' centres ∓ radius `+0x34`, from ±0x7FFF; empty here); linked **at the end** of the list
   (`entity_link`); object `+0x84..+0x8C` = the position.

Then `bullet_spawn`: touch = `bullet_touch`, object `+0x38` = 0 (0xA40 for the player with
cheat 0x20), the tracer (`0x8004572C`: object `+0x38` |= 0xA0, two `SetLineG2` primitives at
object +4 and +0x18 with the colours (230, 180, 70) and (40, 20, 10), `+0x258` = the position),
flags B |= 0x90004, and with a spread n: three draws, pitch/yaw/roll += ((rand & (2^(n+1) - 1))
- 2^n) << 9.

**`projectile_think`** (bullets): object `+0x3A` -= 1, at 0 removed; object `+0x40` (the
world-only countdown from `projectile_aim_at_point`) -= 1 and at 0 the mask is restored from
object `+0x3C`; the parts box; object `+0x68..+0x70` += `+0x74..+0x7C` (0 for bullets); bits of
object `+0x38` for grenades and rolling or smoking projectiles (0x40 fuse, 0x400 rolling, 0x1/0x2
gravity, 0x10 trail); finally object `+0x80` = `+0x200`.

**Moving** (`entity_move`'s general case, for anything that is not the player or a walker): the
previous position and angles saved (`+0x258`, `+0x264`), angles += angular velocity, `+0x220` =
`+0x200`, gravity for flag A 0x20 off the ground, then the local velocity >> 12 (as s16) turned
by `RotMatrixYXZ` of the integer angles (GTE, >> 12), << 12, added with the push `+0x1F0`. If the
mask meets the world's flags A (`g_world` entity `+0xC`) and the entity has flag A 8 and flags
B 0x44 (not 1): `world_sweep(new position, old position)` (levels.md). On a hit: when the
normal's y ≥ 0x801 and the vertical move was downward with `+0x21C` > 0, `+0x21C` = 0 and y back
to `+0x3C0`; result |= 2, `+0x218` = `+0x220` = 0, `+0x364..+0x36A` = the normal (its fourth
halfword the face's d), contact `+0x38` |= 0x8000, and without flag A 0x80 the position = the
hit point (integer, << 16); the face record (point, normal, the three vertices: 0x28 bytes)
goes to `+0x318`. Then the floor (`floor_find` for flag A 8 or on the ground: `+0x208`, zone
`+0x390`), the push cleared, `+0x3BC` = the position.

**The world contact is deferred.** `projectile_world_contact(p, result)` for a swept projectile
(flags B 4) only sets flags B 0x8 and keeps the result in object `+0x5C`; `entities_collide`
calls it again (result 0) after the projectile's entity tests, so a bullet hitting a soldier and
a wall in the same tick hits the soldier. The second call clears 0x8, takes the kept result and,
with flags B 0x20:
1. A bullet hole: `0x8004E614(+0x318)` refuses when the point is within 16 units (each axis,
   exclusive) of the last one (`0x800AB918`) or the normal is zero; else the next of 100 decal
   slots (`0x80093C58` index, slots of 0x80 at `0x80093C5C`; its polygon from `0x8004E354`),
   the last point updated (4 halfwords), and the projectile moved to the face record's point.
   **Bullet holes are fitted to their face**, and that moves the record's point:
   `0x8004E354` draws the hole's half size s = rand·4 >> 15 + 5 (5..8) and, by the normal's
   largest component (y: `0x8004DE80` on x and z; else x: `0x8004E01C` on y and z; else z:
   `0x8004E1B8` on x and y), keeps the square inside the face's bounding box (the record's three
   vertices, `+0x10`, `+0x18`, `+0x20`) on those two axes: the half size a = s, or half the
   narrower of the two extents when one is under 2s; per axis, with the box [lo, hi] and the
   point p: if p + s > hi, p = lo + a when also p − s < lo, else p −= (p + s − hi); otherwise if
   p − s < lo, p += lo − (p − s). So the sparks (2. below) start at the fitted point, up to s
   units from where the bullet hit (1:3 tick 1658, 2:1 tick 769, 5:4 tick 2248).
2. Unless object `+0x38` 0x200: sparks `particles_spawn(position, 1, rand·3 >> 15 + 2, 30, 25,
   15, 100, 0x3F4, 0xE0, 0x3E0, 0xFE, 15, 1.0, 1, rand·4 >> 15 + 6)` (the two draws first).
3. With result & 3 (or `+0x38` 0x200): object `+0x38` |= 2 and the bounce/roll handling for
   grenades (bits 4, 8, 0x200, 0x400, 0x800).
4. Unless object `+0x38` 0x40: explosive rounds (0x100) explode; others `impact_sound`
   (`0x80043140`: a ricochet `sound_play(0, rand·7 >> 15, p, 0x30000, 0, 1.0, 0x7FFF, 0, 0, class 3,
   0)` and the impact effect, entities.md); the yaw += ((rand & 0x3FF) - 0x200) << 16 + 0x08000000
   (wrapped above 0x10000000); removed.

**Particles** (`particles_spawn`, `0x80073BA8`, type 0x31, flags A 0x01028000, flags B |= 0x203,
`+0x290`/`+0x294` = 180, linked at the front): two systems in the object (+0xC sparks, +0x38
smoke), particles of 0x34 bytes from object +0x64 {+0 state 1 live / 2 dead, +1 delay, +2 life,
+4 position, +0x10 velocity, +0x1C rotation, +0x24 rotation speed (150, 100, 200), +0x2C
colour}. Each spark draws, in order: life (rand·10 >> 15) + 25; x, y, z = position + (-spread +
rand·2·spread >> 15) << 16; velocity x, y, z = (-a3 + rand·2·a4 >> 15) << 16; three rotations
rand >> 3; with a colour spread, colour += rand·spread >> 15; flag +3 = rand·100 >> 15 ≥ 51;
+0x30 and +0x32 = rand·7 >> 15 + 5. Each smoke puff: delay = its index, life (rand·2 >> 15) +
20, at the position, velocity y = ((-(rand·2) >> 15) - 2) << 16. The system: gravity 2.0 up to
75.0 (smoke: -4 up to 150.0), drag 2.0 (x, z). The think (`0x80073B08`) steps each live
particle (`0x80073960`: life down, dead at 0; after its delay it moves, gravity while |vy| <
the limit, drag towards 0 on x and z, rotation += speed) and removes the entity once both
systems had no live particle. So its life is its longest particle's.

**The impact effect** (`impact_effect_spawn`, `0x800702E0`, type 0x23, from `impact_sound` on
walls: `n` 8, life 4, colours (0xF0, 0x48, 0) to (0x40, 0x10, 0)): its object holds a count (+0),
a life (+1), a template (+2 width 1, +4 height 1, +6/+8 VRAM (0x3FF, 0): one texel, +0xA/+0xC CLUT
(0x3E0, 0xFD), +0xE mode 4) and particles of 0x24 bytes from +0x10 (+4 a start point, +0x10 an end
point, 16.16; +0x1C the start colour, +0x20 the end colour, halved over the last 3 ticks by the
think `0x800701DC`). **Drawn** (`0x8006F7BC`) as one `POLY_GT3` per particle: both points
projected, the start widened across the streak by ±(dy, −dx)/16 of the screen vector start→end (in
the 512 × 240 pixels), so a sliver from the start (start colour) to a point at the end (end colour),
semi-transparent with ABR (mode − 1) & 3 = 3: B + F/4. The **explosion bursts** (`0x800708F0`,
type 0x23, the explosion's first think): 10 particles of two kinds (+0 1 or 2), each start point
±8 round the centre and an end point along a random direction, drawn by `0x8006FBF0` as `POLY_GT4`
streaks textured 31 × 31 (kind 1 VRAM (1008, 32), kind 2 (1008, 64), CLUTs (1008, 252) and
(1008, 253)), B + F. Natively: `present/effects.gd` (the bursts' corners approximated).

**Hit reactions on soldiers and dogs** (`hit_reaction`, the scripts' reaction variables): with
blood on, **drops** (object `+0x49C`, 15 of 0x74 bytes: they fall, stop on the floor (flag 8) and
fade), drawn by `0x800729C0` as textured quads (VRAM (992, 0), 32 × 32, CLUT 0x3EFE, opaque; their
size not followed); with blood off, **puffs** (`+0x3E8`, 5 of 0x24 bytes: +8 an angle, +0x10 the
place, +0x1C a scale 2.0, +0x20 a grey 0x3C fading by an eighth a tick, 60 ticks), drawn by
`0x80072310` as camera-facing quads of ±15 × scale in view space (×2 across on screen: 15 × s
units across, 12 × s up), VRAM (1008, 0), 32 × 32, CLUT 0x3EFF, additive (tpage 0x2F). (names.txt
called them the enemies' gun smoke, inferred; they are the hit puffs.) **The enemies' muzzle
flashes**: the same sprite as the player's (hud.md), at `object+0xB68` (set up in `enemy_spawn`
with the enemy weapon table's vertex), drawn by `enemy_draw` the frame after `enemy_weapon` sets
its counter: at the muzzle, where the shot leaves (natively: at the new bullet's previous
position).

**Drawing particles** (`0x800732F8`, while the system's done flag, object `+0`, is clear): each
live particle past its delay is a `POLY_FT4` of corners (±`+0x30`, ±`+0x32`), scaled by the
system's `+0x8` (16.16: 1.0 for impacts, 1.5 for the rocket's) and turned by `RotMatrixYXZ` of its
three angles (`+0x1C..+0x20`, stepping each tick: the quads tumble), at its position (`0x8008D9D8`,
then `RTPT`); a triangle (the fourth corner on the third) when its `+0x3` flag is set; textured
from the header (`+0x18` VRAM x, `+0x1A` y, `+0x1C`/`+0x1E` the CLUT, `+0x20`/`+0x22` the size in
texels: the sparks 15 × 15 at (1012, 224), the debris one texel at (1023, 63), the smoke 31 × 31 at
(1000, 0)); coloured by `+0x2C` (128 = the texel as is); semi-transparent when `+0x26` is non-zero.

**The shell casing** (`casing_spawn`, `0x80088040`): at most 5 pending (`0x8009EC60`);
`entity_create(1 (an instance, no model), object 0xC)`, type 0x66, flags A 0x07020000, flags
B 0x213, think `casing_think` (`0x80087FA0`), destructor `0x8006A318`; the given position;
object +0 = (rand & 15) + 8 ticks, +4 the owner's handle, +8 the pitch; linked at the front.
Each tick +0 -= 1; at 0 `sound_play(0, (rand & 1) | 0x10, casing, 0xCCCC, 0, pitch, 0x199B, ...)`,
the count -1, removed.

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
   flagged 0x206 in flags B) is tested against its sphere, and for characters and objects (flags A 0x10400) more exactly: objects with plane shapes by the
   segment-box test (`0x8004B0F0`, objects.md, from the camera to the aim point), the rest by their parts; the nearest one shortens the ray to it. The end is
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
- Scoped (`inv` flag 0x100; turning is cut to 1/8; only reached when not aimed): the start is
  the camera entity's position (`copy_camera_pos` `0x80045160`) and the angles are
  `angles_from_to(start, the camera object's first three words)` (`0x80045184`: the camera's
  look point, 16.16). Checked: 3 scoped rounds start at the camera; the engine's scoped shots
  match the original's in 2:2 (ticks 6115..6323).
- Otherwise: from the gun, along the line between two points on it (`0x80044E0C`: the model's
  bones `+0xD4`, `+0xD6` with offsets `+0xD8`, `+0xE0`; `0x80044F10`, `0x80044D18` turn the
  line into angles): **along the barrel**, so where the gun points, not where the crosshair is.
  No player shot of the session was fired this way.
- Enemies' shots: angles from the gun to the target point (enemies.md).
- **Exactly, the shots not aimed at the aim point** (`projectile_aim` without the flag;
  `gun_points` `0x80044E0C`, `0x80044F10`): with an attachment given (enemies pass their weapon
  attachment), the two points are vertices +8 and +0xC of the attachment's model (instance
  `+0x2C`, the model's `+0x88` vertex array, 8 bytes each) through `RotTrans` with the
  attachment's drawn matrix while it hangs on a bone (+8 ≠ 0, flag 1), else with RotMatrixYXZ of
  its angles scaled by its scale >> 4 and moved to its integer position (`0x8008E38C`); without
  one, `model_bone_point` of the shooter's two bones (with offsets). The start = the second
  point; with a target point (`+0x228`/`+0x22C` from `angles_from_to(start, target)`, roll 0),
  else from the first point to the second (along the gun). Types 0x13F3 and 0x13F1 aim their own
  way; the player's scope copies the camera.
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

**Turning** (`player_controls`): with the scope (`inv` flag 0x100) the turn and look inputs
are divided by 8, nothing else. In the aiming stances 0 and 1 they are scaled by 0.5625
((v >> 1) + (v >> 4); 0.375, (v >> 1) - (v >> 3), with `inv` flag 0x80000), then by the aim
friction (>> 1, or >> 2 with bit 31); in every other stance only by the aim friction. The
pitch then moves by **half** the scaled look (`pitch - (look >> 1)`, an arithmetic shift) and
is clamped to ±0x2AA (±60°).

**The aiming stances 0 and 1 move a crosshair first** (`player_controls`): on entering one, the
integrators `inv+0x188` (turn) and `inv+0x17C` (look), the friction bits and both angular
velocities are cleared. Holding Center view (action 7) eases both integrators to 3/4
(`center_view`, `0x8007F084`) and places the crosshair from them. Otherwise the turn input
(scaled as above) adds /8 to `+0x188`; the crosshair's x = 256 + (`+0x188` << 8) / M3 (M3 the
turn role's maximum, `0x800B75F0` + role·0x34 + 0x1C, 0x230000 for the pad); while |that
offset| ≤ the box half-width `inv+0xB4` only the crosshair moves; beyond it the crosshair stays
on the box edge, the integrator is set back to the edge (box·M3 >> 8, rounded toward zero for
negatives: -(box·M3)/256) and the input turns the look yaw `inv+0x184` and the body yaw `+0x22C`
directly. The look input likewise: +/8 into `+0x17C`, y = 131 + `+0x17C`·120 / M2 (M2 =
0x3C0000), box `inv+0xB6`; past the edge the integrator = ±box·M2 / 120 (C division) and half the
input lowers `inv+0x178` and the body pitch `+0x228` (clamped ±0x2AA, the body pitch corrected
by the overshoot), and `view_pitch_target`. The look input is the button role, else the stick
role, else (an analog pad with the port's "pitch from the move stick" option, `0x8009E398`)
`role_axis_alt` (`0x80076008`: the look role's curve applied to the move stick). Then
`aim_update`, and nothing else of the normal look handling runs.

### `aim_update`, exactly (`0x8007E034`)

1. Out of aim (`inv+0xA4` ≥ 0x191 without flag 0x200000): angular velocities 0, done. With
   flag 0x800000: the velocities follow the look angles (`+0x234` = look pitch - 4·`+0x17C`
   (stances 0, 1) + `+0x180` - pitch; `+0x238` = look yaw + 8·`+0x188` (stances 0, 1) +
   `+0x18C` - yaw), done.
2. The ray: the transposed view matrix (camera object `+0x100`) with its middle column ×64/25
   (C division), applied (GTE, >> 12) to ((`+0x190` - 256) << 6 - tx, (`+0x192` - (`+0x4E` +
   120)) << 6 - ty, (`inv+0xA4` << 6) - tz), each >> 2 as s16. The end E = C + (P << 16 - C) >> 1
   from the camera's position C (with any |P| > 0x4000: C + ((P >> 2) << 16 - C >> 2) << 1).
3. Swept in three pieces, nearest first: (C + (E-C)/8 → C), (C + (E-C)/2 → C + (E-C)/8),
   (E → C + (E-C)/2), each `world_sweep(a, b)` with integer points; the first hit is the aim
   point, else E's integer part. Flags 0x100000 and bit 31 cleared.
4. Entities: d = aim - C (integers), L = cheap length (max + (mid + min)/4), u =
   `VectorNormal(d)`, |d|² from the GTE. For every entity but the soldier, not dead (flags A
   0x20000000) and not flagged 0x206 in flags B: `segment_sphere(C, u, centre, radius)`; for
   flags A 0x10400 also the parts (`segment_parts`, |d|²) or, with plane shapes, the box test
   `0x8004B0F0`. Passing ones: their distance dd = cheap(C - position); the smallest below L wins,
   and a character (flags A 0x50) at dd ≥ 1200 sets 0x100000, ≥ 2750 also bit 31. A winner moves
   the aim point to C + d·dd / (cheap(d) + 1) (C division). The aim point → `inv+0x1A4` (<< 16).
5. The gun's two points: near = `model_bone_point(bone +0xD4, offset +0xD8)`, far = bone +0xD6,
   offset +0xE0 (body model; the player's eye offset applies, animation.md). With t5 = the
   horizontal cheap distance (|a| + |b| - min/2, of the differences >> 2) from C to the aim point
   and a3 the same to the far point: if t5 < a3 + 20.0, shots start at the camera (`inv+0x198` =
   C, flag 0x20000) and the gun is steered on the screen: the far point projected (RTPS with the
   view matrix, H = `inv+0xA4`, offset (256, 120)); yaw velocity = (`+0x190` - sx) << 16 (halved
   under 9 pixels); pitch velocity from sy - `+0x192`: ×1.25 from 9, ×1 from 5, else ½.
   Otherwise shots start at the far point (the camera with flag 0x200000), 0x20000 cleared, and
   the gun is steered by angles: g = normal(far - near), a = normal(aim - near); yaw difference
   = ratan2(gz, gx) - ratan2(az, ax), pitch difference = ratan2(gy, √(gx² + gz²)) - ratan2(ay,
   √(ax² + az²)), each wrapped to ±0x800 and scaled by the 3D cheap distance s to the aim point
   (16.16): ≤ 37.5: /4, ≤ 100: /2, ≤ 200: ×3/4, under 8 then /4. Yaw velocity = the yaw
   difference; the pitch target = pitch + difference (clamped ±0x2AA), velocity = ((7·target +
   pitch) >> 3) - pitch.
6. `inv+0x180` = pitch + velocity - (look pitch - 4·`+0x17C` in stances 0, 1), `inv+0x18C` = yaw
   + velocity - (look yaw + 8·`+0x188` in stances 0, 1).

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

**Spent attack parts.** (Confirmed on the game itself: the user's DuckStation run of 1:3, 2026-10-21, shows a dog biting the player with the fetched grenade going off in its mouth; the player dies, the dog stands. In normal play the player always dies beside a dog that survives this way, which is why the dog seems to die from every grenade.) Damage between two hit parts is refused when either part is kind 8
(`damage_from_hit`). An attack part that lands a hit, on an entity with flags B 0x20000 (the
dog), is turned to kind 8 by `0x8004A868`, and `parts_touch` (`0x8004C990`) reports only the
first touching pair. So a dog mid-bite that touches an explosion bites the blast first (the
damage trace shows the blast as the victim, 2.0), its mouth part is spent, and the blast does
it no damage for all its ticks: watched with a fetched grenade going off in a biting dog's
mouth (enemy_types.md, the dog's Grenades). A dog not attacking dies from the 4.0.
In normal play (no god mode, the player walking to the dog; enemy_types.md, Grenades) a
dog's bite that lands on the player spends the part too, and the dog was unhurt in all 5 runs
where it was biting with the fetched grenade at detonation, while the player beside it took the
1000 within 300; dogs not attacking died (5 runs). A spent part protects only while it is the
first part inside the blast: one dog that ran off was killed through another part after 7 ticks.

Hits from the same type as the victim (soldier on soldier), or from a projectile whose shooter
had flags A 0x4, skip the head and 10% rules and the statistics (the first branch of
`enemy_hit_part`).

All 17 parts of a soldier are kind 1, so every hit does the weapon's damage wherever it
lands, **except the head: a hit on part 0 kills** (below). (An earlier version of this
document said there was no headshot effect; the replay's damage trace and `enemy_hit_part`
show otherwise.)

### The hit handlers, exactly

- **Who applies the damage.** In the collision pass a bullet's own `hit_part(bullet, -1, victim,
  part)` (`0x80043E38`) calls `damage_from_hit(bullet, -1, victim, part)`, which does nothing
  for a part < 0 (returns 0) — the call still removes the bullet (1) unless the victim is its
  shooter (`handle_same(object+0x54)`), a projectile (0x13F0..0x13F9) or type 0x25; a soldier
  victim (type 0x1402) also plays `sound_play(0, (rand >> 15) + 12, bullet, 0x20000, 0, 1.0,
  0x4CCD)` (the draw is always 0: sound 12). The damage comes from the victim's
  `hit_part(victim, part, bullet, -1)`.
- **`damage_from_hit(v, part, a, apart)`**: part < 0: 0. A projectile (apart < 0): damage =
  `a+0xF8`, scaled by the victim part's kind; the side bit into `v+0x38`: 0x40 if the
  victim's forward column (`+0x344`, `+0x34A`, `+0x350`) · (the projectile's start `+0x258`
  (integer) - the part's centre) > 0 (from the front), else 0x20; `apply_damage` if non-zero.
  Between parts: refused (0) when either part is kind 8 unless one side is type 0x1B; with
  dy = (v.y - a.y) >> 16 and "front" the same dot with the attacker part's centre: an attack
  part (5, 6, 9) sets the attacker's `+0x38` 0x200000 and the victim's 0x40/0x20 by the side
  (kind 6 adds 0x100000, kind 9 0x80000), then spends the attacker's attack parts (flags B
  0x20000 only, above); an ordinary victim part sets 0x400 (front) or 0x200; with an attack
  part also 0x80 for dy > 0 and 0x100 for dy < 0. Then `apply_damage` if non-zero; returns 1.
- **`apply_damage(v, d, a)`**: the hit statistic is counted here once per projectile for victims
  with flags A 0x4440 (not the player): attacker not the player, its flags B without 0x100000,
  its `+0x3CA` (weapon) ≠ -1 → `0x80039CAC` and `inv+0x148` += 1, attacker flags B |= 0x100000.
  A death (health ≤ 0 and not already dead, not the player) with flags B 0x8000 credits a kill
  (`stat_kill(a, 1)`: `inv+0x142` and `0x80039CB8` += 1, both reset when `+0x142` reaches
  0x8000; the attacker's weapon `+0x3CA` (< 13), else the player's current weapon, counts in its
  table entry's `+0x30`). The player as victim: the wound wedge (`hit_indicator`) and
  `0x80039CB0`/`inv+0x144` += 1.
- **The dog's `hit_part`** (`0x80054C24`): `damage_from_hit`, then variable 0xD = health·100 /
  max unless it is -1, variable 0x18 = the attacker's type, 0xC = the part; a player's melee part
  (kind 5) that hit is turned back to kind 1 with sound 0x22. Returns 1.
- **`enemy_hit_part`** order: a projectile from itself: 0. Same type, or a projectile from an
  owner with flags A 4: damage, variables 0x18/0xC, and dead or negative health → variable 0xD
  = 0, health 0. Otherwise damage, 0x18/0xC; if the health changed: the hit statistic as in
  `apply_damage`, then by part: 0 the head (below), 7 or (1, 2 by an explosion) torso and
  explosion counters `0x80039CC4`/`0x80039CE8`, 1 and 2 `0x80039CC8` with `0x80039CE0`/`CE4`,
  3/4 `0x80039CC4` + `CD0`/`CD4`, 5/6 `0x80039CC8` + `CD8`/`CDC`, 8 `0x80039CC4` + `CEC`, 9..16
  `0x80039CCC` + `CF0..D0C`; a player's melee part back to kind 1 (sound 0x22). Then the 10%
  rule; finally, hit by a projectile not from a flags-A-4 owner, the soldier's level script gets
  message 1 at once (`script_run_message(soldier, 0, 1)`).
- **Touching.** `enemy_touch` (`0x8005291C`, soldiers and dogs) and the player's touch
  (`0x8007C030`) answer only objects with plane shapes (`+0x2EE`, `sphere_vs_object_planes`
  `0x8004B6DC`), except that the player's touch also pushes the player away from a soldier or
  dog whose instance `+0x6C` is 0x80: with dx, dz the integer offsets (character - player) and
  d = max + min/4 of their absolute values, overlap = the two `+0x2EC` radii - d; if positive
  (at most 65), n = `VectorNormalSS(dx, 0, dz)`, the player's push `+0x1F0`/`+0x1F8` +=
  ((-n·overlap) >> 12) << 16 and its speeds `+0x200`, `+0x218`, `+0x220` = 0.

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
  (`0x80046C50(camera, 20, 10)`: camera object `+0xCC`/`+0xCE`), a grunt 30% of the time
  (`rand·100 >> 15 < 30`; then 0x1B for ≥ 51 else 0x1C, pitch 0.5, spread 0x1000), a hit sound
  (by a soldier 0x22 with spread 0x3333 and the soldier's attack part back to kind 1; else 0xC or
  0xD by a third draw, spread 0x7FFF; pitch 1.0, volume 0x20000), and the rumble
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
