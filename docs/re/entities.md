# Entities

Everything that moves or reacts (the soldier, the camera, enemies, bullets, grenades, doors,
pickups) is an entity: one struct about 0x3D0 bytes long, in linked lists, updated once per
30 Hz tick. Read from the code below; offsets checked against RAM dumps of real play.

## Lists and handles

- `g_entities` (`0x8009D64C`): the list the tick updates, linked through `+0x4`. Other heads
  nearby (`0x8009D644`, `0x8009D648`, `0x8009D678`..`0x8009D69C`) hold sublists of the same
  entities; `0x8009D648` is the world (its `+0xC` is the mask bit entities test to collide with
  the level geometry).
- Handles: 16-bit indices into the table at `0x8009D678` (`table[handle & 0xFFFF]` = entity),
  used for links that must survive an entity's removal (`+0x2C` parent, `+0x30` attach target).
- `g_update_stamp` (`0x8009D61C`): incremented per pass; each entity's `+0x28` takes it when
  updated, so parents can be updated before their children and nobody twice.

## Layout

| Offset | Type | Meaning |
|---|---|---|
| +0x04 | ptr | Next in the list |
| +0x08 | s32 | Type: 1 soldier (player), 2 camera, 0x13F6 bullet, 0x13F7 pellet / weapon-11 bullet, 0x13F1 rocket, 0x1402 (ricochets: metal?), 0x1777 (ignored by bullets), ... |
| +0x0C | u32 | Flags A: 0x1 the player's soldier; 0x8 walks (character collision); 0x20 has gravity; 0x80 (slides on landing); 0x4000 bleeds; 0x20000 no physics; 0x80000 dead/removed; 0x8000000 (ignored by thrown weapons); 0x10000000 slow fall (water/ladder?) |
| +0x10 | u32 | Flags B: 0x1, 0x2/0x4 skipped as a collision target; 0x4 swept collision (bullets); 0x8 calls `+0x378` after the collision pass; 0x10 no floor check; 0x40/0x44 world-collision variants; 0x10000 bullet; 0x80000 nearest-hit-first (bullets); 0x200000 (falls through floors) |
| +0x14 | u32 | Collision mask: collides with entities whose flags A share a bit |
| +0x18 | u32 | Attachment state bits |
| +0x28 | u32 | Update stamp |
| +0x2C | u32 | Parent handle (riding: moved with the parent) |
| +0x30 | u32 | Attach handle; +0x34 (s8) the bone |
| +0x36 | u16 | Number of hit parts (0: the bounding box only) |
| +0x38 | u32 | Contact flags (0x1 on ground, 0x8000 hit something, 0x80000000 fell) |
| +0x100 | ptr | Model/animation instance (+0x5C/+0x5E current animation and frame, +0x8 → model) |
| +0x1F0 | 3×s32 | External push this tick (from a parent), 16.16 |
| +0x1FC/+0x200/+0x204 | s32 | Gravity: acceleration, fall speed, terminal speed |
| +0x208 | s32 | Floor height under the entity |
| +0x20C | 3×s32 | Position, 16.16 (+0x354.. mirrors the integer part) |
| +0x218..+0x220 | 3×s32 | Local velocity (x, vertical, z), turned by the angles into the world; +0x21C vertical: gravity adds 2.0/tick up to 75.0 |
| +0x224 | s32 | Speed scale (1.0: the local velocity as is; bullets: 600-669) |
| +0x228 | 3×s32 | Angles: pitch, yaw, roll (16.16, 4096 per turn); for characters +0x22C is the heading |
| +0x234 | 3×s32 | Angular velocity (added to the angles each tick) |
| +0x240 | 3×s32 | The player's angles from animation root motion (wrapped to 28 bits) |
| +0x258 | 3×s32 | Position at the previous tick (bullets sweep from here) |
| +0x264 | 3×s32 | Angles at the previous tick |
| +0x270 | | Rotation (angles) |
| +0x284 | s32 | On the ground |
| +0x2D8/+0x2E0 | 3×s16 | Collision sphere centre, local and world |
| +0x2E8 | u16 | Collision sphere radius (0: never collides) |
| +0x304 | ptr | Hit parts: `+0x36` entries of 0x48 bytes (below) |
| +0x308..+0x312 | 6×s16 | Bounding box (min xyz, max xyz), rebuilt from the bones each tick |
| +0x314 | s32 | Distance term of the last part hit |
| +0x318 | | Surface the last sweep hit |
| +0x340 | | Rotation matrix |
| +0x364 | 3×s16 | Normal of the last contact |
| +0x36C | fn | **think**(self): once per tick |
| +0x370 | fn | draw? |
| +0x374 | fn | **hit_part**(self, my_part, other, other_part): a precise (per-part) contact; damage by body part |
| +0x378 | fn | **world_contact**(self, flags): after moving, with the contact flags |
| +0x37C | fn | **touch**(self, other): the spheres touch (or a bullet's path crosses the sphere) |
| +0x384 | fn | ? |
| +0x388 | ptr | Type-specific object (the soldier's weapon block, the camera's view, ...) |
| +0x38C | ptr | Brain: goals, perception ranges, aim error (enemies.md); updated by `brain_update` (`0x80040DF4`) |
| +0x3BC | 3×s32 | Position before this tick's move (restored when blocked) |
| +0x3CA | s16 | Weapon index (on bullets and grenades) |

## A frame (`game_frame`, `0x800616E4`)

1. `input_poll` (`0x8004DE3C`) reads the controllers.
2. Paused: nothing else (the poll repeats for the dropped ticks).
3. Otherwise, for each tick due (1 + the vblank-paced ticks dropped, at most 4):
   `g_tick` (`0x8009A5DC`) += 1, `entities_update(g_entities)`, `entities_collide(g_entities)`,
   `0x8005F69C` (level logic?), and another `input_poll` when more ticks follow.
4. `render_pass`, `0x8004137C`, `0x8006B894`.

So everything that happens in a tick (moving, firing, spawning bullets) happens before any
collision of that tick, and a bullet fired during a think is swept and tested in the same tick
if it's added after the shooter in the list.

## The tick (`entities_update`, `0x8006BA28`, entered through `0x8006BA20`)

For each entity, in list order, parents first:
1. `0x8004A828(e)`.
2. Riding: with a parent (`+0x2C`), add the parent's movement this tick (position delta,
   angle delta, and its rotation applied to the offset) to `+0x1F0`/velocity. A dead parent
   (flag 0x80000) or attachment bit 0 detaches it (`0x8006D84C`).
3. `think(e)` (`+0x36C`).
4. Attached (`+0x30`): position = the target's bone `+0x34` (`0x8008DABC`).
5. Otherwise, unless flag 0x20000: `entity_move` (`0x8006C83C`), then `world_contact(e, flags)`
   (`+0x378`) if anything was hit; player and walkers also `0x800845A8`.
6. Brain (`+0x38C`, `brain_update`), `0x8006A76C(e)`, `player_weapon_update` for the player's soldier
   (flag 1), the bounding box (`entity_bounds`, `0x8004D43C`), and the camera's view for type 2
   (`camera_setup`).

## Moving (`entity_move`, `0x8006C83C`)

1. Angles += angular velocity. The current animation frame's root rotation is added (to
   `+0x240` for the player, to the angles for others).
2. Gravity: fall speed += acceleration up to the terminal speed; with flag 0x20 and off the
   ground, vertical velocity += 2.0 per tick, at most 75.0 (slower with flag 0x10000000).
3. The local velocity (scaled by `+0x224`), turned by the entity's angles (player: its yaw), is
   added to the position, plus the push from a parent (`+0x1F0`).
4. Against the level (if the entity's mask includes the world's bit): walkers (flag 0x8) slide
   (`0x80078F60`); everything else sweeps a line from the old position (`0x8003EF5C`: hit point,
   normal, surface at `+0x318`) and stops there.
5. The floor (`0x80060474`, height in `0x800B7460`, -0x7FFF: none): landing, stepping (up to
   0x120 units), falling (flag 0x200000 entities through holes).
6. Returns contact flags: 0x1 ground, 0x2 wall, 0x80 fell.

## Collisions between entities (`entities_collide`, `0x8006C08C`; from the main loop `0x800616E4`)

For each entity A (not dead, not flagged 0x2/0x4 in flags B) and each B whose flags A share a
bit with A's mask:

- **Boxes** (A without flag B 0x4): spheres touch (`0x8004CB18`) → `touch(A, B)` and
  `touch(B, A)`; then the parts (`0x8004C990`) → `hit_part(A, a, B, b)` and `hit_part(B, b, A, a)`.
  Each pair once (B runs over the rest of the list).
- **Swept** (flag B 0x4: bullets): the segment from A's previous position to its current one is
  tested against every B's sphere (`0x8004CD90`, with the distance along it) and parts
  (`0x8004C968`). Without flag 0x80000 every crossing is handled as found. With it (bullets),
  the crossings are collected (`[0x8009D620]`, 12-byte entries {entity, distance, part, done},
  up to 159) and handled nearest first: `touch(A, B)`, `hit_part(A, -1, B, part)` and
  `hit_part(B, part, A, -1)`; if none of them returns non-zero (the bullet goes on), the next
  nearest. A bullet's whole path is tested each tick: nothing is skipped however fast it flies.
- Afterwards, with flag B 0x8: `world_contact(A, 0)`.

## Shapes: spheres

- **Each entity**: a sphere, centre `+0x2E0`, radius `+0x2E8` (`entity_bounds` places it from
  the model each tick). Two entities touch when the distance between centres is less than the
  sum of the radii (`spheres_touch`, `0x8004CB18`).
- **A segment against an entity** (`segment_sphere`, `0x8004CBC8`): the distance from the
  segment's line to the centre (GTE outer product with the unit direction) against the radius;
  returns an approximate distance to the centre (max + (mid + min)/4) to sort hits by.
- **Hit parts** (`+0x304`, `+0x36` of them, 0x48 bytes each): +0x0 hit this tick, +0x8 kind
  (7: never hit), +0x20 centre (3×s32, world), +0x38 radius². A segment hits a part
  (`segment_parts`, `0x8004C76C`) if it starts inside it, or the part is ahead (dot > 0), within
  this tick's length, and the line passes within the radius. The first part hit is returned
  (that index goes to `hit_part`), every part hit is flagged.

So a bullet hits a target when its path this tick passes through one of the spheres on the
target's bones: hit zones are those parts.

## Open

- The main loop's order (`0x800616E4`): think/move for all, then collisions; when bullets fired
  this tick first move.
- How the parts follow the bones (`0x8004CEE4`), and what each part index is on a soldier.
- The other lists and what puts entities on them (spawning, activation by distance/triggers).
