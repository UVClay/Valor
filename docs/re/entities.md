# Entities

Everything that moves or reacts (the soldier, the camera, enemies, bullets, grenades, doors,
pickups) is an entity: one struct about 0x3D0 bytes long, in linked lists, updated once per
30 Hz tick. Read from the code below; offsets checked against RAM dumps of real play.

## Lists and handles

There is **one list**: `g_entities` (`0x8009D64C`), linked through `+0x4`, which the tick,
the collision pass and the draw pass all walk. Everything else is the handle table and a few
global pointers. (An earlier version of this document took the words around `0x8009D644` for
other list heads; they are not: checked below.)

- **Linking**: `entity_link_front` (`0x8006B834`) inserts right after the camera (`g_player`,
  `0x8009D638`, the list's head); `entity_link` (`0x8006B854`) appends (projectiles only);
  `entity_unlink` (`0x8006B89C`) removes. The order this gives is in objects.md.
- **Global entity pointers** (`0x8009D628`..`0x8009D66C`): `g_player` (the camera, `0x8009D638`),
  `g_world` (`0x8009D648`, the level as an entity: its `+0xC` is the mask bit entities test to
  collide with the level geometry), `g_entities`, `g_last_explosion` (`0x8009D650`),
  `g_soldier` (`0x8009D654`), the last dog spawned (`0x8009D644`, written by `dog_spawn` only),
  and `0x8009D628`/`62C`/`630`/`634`/`63C`/`640`/`658`/`660..66C`, which no code writes
  (leftovers: `handles_free_removed` clears them like the others). Checked with `fn.py uses`
  on each address (no gp-relative access: gp is `0x80035D1C`).
- `g_update_stamp` (`0x8009D61C`): incremented per pass; each entity's `+0x28` takes it when
  updated, so parents can be updated before their children and nobody twice.

### Handles

A handle is a 32-bit word **(serial << 16) | slot**. Every entity gets one when it is created
(`handle_alloc`, `0x8006D930`, from `entity_create`) and keeps it at its own `+0x0`.

| Address | What |
|---|---|
| `0x8009D678` | `g_handles[256]`: slot → entity pointer (0: free) |
| `0x8009DA78` | `[256]` the serial each slot was given |
| `0x8009DE78` | `[256]` removed flags (set by `entity_remove` through `0x8006DA50`) |
| `0x8009D670` | The serial counter: +1 per handle made, never reset within a level |
| `0x8009D674` | Slots in use (high-water mark): new handles take the next slot while it is below 256, then the first free slot from 0 |

- **Resolving** (inline everywhere): `h ? g_handles[h & 0xFFFF] : 0`. This does **not** check
  the serial: a stale handle whose slot was reused resolves to the new entity.
- **Checking** (`handle_dead`, `0x8006D8AC`, used by the tick for parents, sounds, scripts,
  the player's mounted gun...): dead if the handle is 0, if the slot's serial differs from the
  handle's, or if the entity's first word is `0xDEDEDEDE`. That last test never fires in this
  game: no code writes 0xDEDEDEDE and it is in no RAM dump (7 dumps searched) nor in the exe;
  it is a debug build's freed-memory fill. `handle_same` (`0x8006D7D0`): a dead handle equals
  only 0, else resolve and compare.
- **Freeing** (`handles_free_removed`, `0x8006DA88`, one of the end-of-frame jobs, objects.md):
  every slot flagged removed is cleared (pointer, serial, flag) and the globals above that
  named its entity are zeroed; the high-water mark drops over trailing free slots.

So between `entity_remove` and the end of the frame a handle still resolves to the removed
entity (flagged A 0x80000) and passes the serial check. Its memory is already back with its
allocator by then (below), but neither allocator reuses or clears it within the frame for
characters; for a reimplementation: treat a handle as dead from the moment the entity is
removed, and never let a slot be reused while handles to it may exist (the serial protects
only `handle_dead` callers).

## Creating, activating and removing entities

### Memory

`entity_create` (`0x8006B5B8`, `(flags, object size, model hash, ..., model)`) allocates one
block: the 0x3CC-byte entity, then its type object (`+0x388`, the given size), the animation
instance (`+0x100`, 0xB0 bytes, when flags & 5), the hit parts (`+0x304`, 0x48 bytes each, the
model's `+0xBE` count), a list at `+0x300` (`+0x2EE` entries of 0x1C, unknown) and two blocks
of 0x3C × n for the script channels (`+0x124`, `+0x198`). Then the handle, defaults
(`0x8006B350`: health 100.0, scale 1.0, `+0x290`/`+0x294` 1, every handler 0, animation -1,
both script channels empty, on the ground), and the instance from the model.

Exactly (`entity_create(flags, object size, model hash, instance flags, parts, channels, list,
model)`): with a model hash (≠ -1) the model is found (`model_find`), and refused if missing;
flags & 5 gives an animation instance (0xB0 bytes) **with or without a model** (the shell
casing has one and no model: its instance fields read -1/0 from the defaults); flags & 2 takes
the part count from the argument, otherwise (with a model) from the model's `+0xBE`. The block
is allocated from the character pool for model types 0x1402/0x1405, else from the heap; the
handle, `+0x390` = 0xFFFF, `+0x394` = 0, `+0x398` = 0xFFFF, `+0x39C` = 0, the pointers in the
order object, instance, parts, list (`+0x2EE` its count), the two channels' tables; the
defaults (`0x8006B350`, which clears `+0x36`, the part count); then `model_instance_init`
when there is a model. **Only `model_instance_init` sets `+0x36`** (from the model), so a
model-less entity created with parts (projectiles: 2) has the memory but a count of 0.
`model_instance_init` (`0x8006A96C`) also copies the model's `+0xA4..+0xAC` into the scale
`+0x270..+0x278` and the **word** at model `+0xA0` into the eye height `+0x2D4` (16.16: the
height is the model's halfword at `+0xA2`; reading `+0xA0` as a halfword gives 0).

- **Characters** (model type 0x1402 soldier, 0x1405 dog) come from a **pool of 5 fixed slots**
  of 0x2814 bytes at `0x800B7B08` (`char_alloc`, `0x8008EEF0`): the entities are always at
  `0x800B7B08 + i × 0x2814`. A slot's last two words are a release countdown (`+0x280C`: -67
  in use) and a free flag (`+0x2810`). A destructor releases a slot with countdown 2
  (`char_release`, `0x8008EE60`); `char_pool_update` (`0x8008EAC8`, at the start of every
  frame) counts it down and frees it at 0: a dead character's memory stays intact for two
  frames. A second pool of 5 × 0x1308 at `0x800C4378` holds their pose buffers (`+0x1300` a
  countdown, -67 in use, `+0x1304` free): `model_instance_init` takes the first free one
  (`0x8008ED30`, zeroed) for a 0x1402/0x1405 model with bones, **not** the one of the same
  index as the character slot; the destructors give it back with countdown 2 (`0x8006B234` →
  `0x8008ECA0`) and `char_pool_update` frees it at 0, per index after the character slot.
  `0x800AB8D0` counts the pose buffers in use (+1 taken, -1 freed) and `0x800AB8C8` is set to
  max(`0x800AB8CC`, `0x800AB8D0`) whenever either pool changes (`0x800AB8CC` is only ever
  lowered, by a release with countdown 0, which no caller uses: so `0x800AB8C8` is the pose
  buffers in use). **`enemy_spawn` refuses at 4 or more** (levels.md): a corpse holds its pose
  buffer until removed, so two corpses and two live characters block a fifth. So **at most 5
  soldiers and dogs exist at once**, whatever the level (`0x80097FEC`, the soldier cap, is 5 in
  every dump; levels.md). Checked in the four `mid_*` dumps: soldiers at slot addresses only,
  the slot states matching the live ones.
- Everything else comes from the general heap (`0x80036DA0`, freed by `0x80036E44`).

### Spawning and activation

Nothing is activated by distance. A level's entities come from:
1. `level_load`: the camera, the player's soldier, the world, ladders and the level's fixed
   objects.
2. **Records**, only when a level script runs built-in 4 `activate` (`record_activate`,
   `0x80052D70`; its only caller is the VM's built-in table): once per record (state bit 0),
   `record_spawn` (`0x8007505C`) makes the entity by the record's kind (levels.md), links it
   after the camera, sets its `+0x1C` to the record's {state, record} pair and sends its script
   message 0. Level scripts call `activate` from trigger volumes' messages (the player entering
   a box), from other entities' deaths, and at the level's start: activation is by trigger.
3. A spawn that fails (the 5 character slots full, or the soldier cap) is queued (64 entries)
   and retried at the end of every frame until it succeeds (objects.md).
4. Effects made by code: bullets, explosions, particles, casings, the alarm, helmets, the
   impact effect (type 0x23, below).

**Record state** (the word before each record pointer, levels.md): bit 0 = has a live entity
(set by `activate`, cleared by `entity_remove`), bit 1 = destroyed (set by `entity_remove` for
kinds 2, 3, 6 when the entity had flags A 0x20000000, dead, unless the record's `+0x30` has bit
3; and by the soldier's destructor when it died). `0x80075684` clears both bits for every
record. A record whose bit 0 was cleared can be activated again.

### Removal

`entity_remove` (`0x8006B8F0`) is immediate: flags A 0x80000, record state as above, sounds
following it dropped (`0x80042524`), unlinked, removed flag for its handle, then its
destructor (`+0x384`): for a soldier `0x80056290` (the soldier count -1, a mounted gun it held
released, its brain freed, the slot released). Who removes:
- Projectiles, effects and pickups themselves (on hit, at the end of their life, when taken).
- Scripts: op 0x17 `remove_self` (a soldier's corpse script 15, about 3 s after the death
  animation: soldier.md), op 0x15 `remove_handle`, level built-in `remove` (`0x80052DA4`).
- **Despawning out of sight** (below).

### Despawning out of sight (`+0x290`)

Entities with flags A **0x8000** (every soldier, flags 0x1300903C; dogs; pickups whose record
`+0x58` has bit 0, catalogue.md) have an out-of-sight counter:

- `+0x294`: its start value (soldiers: record `+0x68`, 150 in every 1:3 / 2:1 dump; pickups:
  record `+0x5E`); `+0x290`: the counter; `+0x298`: has been drawn once; `+0x29C`: drawn this
  frame.
- Each **drawn frame** (not tick), in the draw pass: drawn (`model_draw` got something on
  screen, or `0x8005E760`'s view test passed) → `+0x298` = 1 and `+0x290` = `+0x294`. Not drawn
  and seen before → `0x8006A570`: unless `0x8006A3BC` says to keep it, `+0x290` -= 1.
- `0x8006A3BC(e, camera)` keeps it while its distance to the camera (`max + (mid + min)/4` of
  the axis differences) is at most the camera's far distance (camera object `+0xC4`: the
  level's `+0x590`, else 4500; 4600 in 1:3). **Exception**, to make room: an entity with flags B
  0x40000 (soldiers have it) when the soldiers alive ≥ cap - 1, a spawn is queued, and it is
  3501 or more away **behind** the camera (its offset from the camera, whole units, · the
  forward column `+0x344`, `+0x34A`, `+0x350` of the entity the camera follows, camera object
  `+0xC`, as a 32-bit GTE sum < 0): `+0x298` and `+0x290` set to 1 and the counter not kept, so
  it reaches 0 this frame. Pickups have flags B 0x40000 too (1:3, tick 5825: two pickups made
  at 5823 behind the camera while a soldier waited, gone before ever being drawn).
- `0x8006A570` runs for every entity out of view with flags A 0x01000000, drawn before or not
  (`entity_draw_model`): a never-drawn entity counts down too, harmlessly, as `entities_draw`
  needs `+0x298`.
- `entities_draw` (`0x8006C714`), after each entity's draw: flags A 0x8000, `+0x290` ≤ 0 and
  `+0x298` set → its record's script gets **message 2** (`script_run_message`, at once) and the
  entity is removed.

So a soldier (alive or a corpse) that has been seen is removed after 150 displayed frames (5 s
at 30 fps, 10 s at 15) **beyond the far plane** and out of view; never while within it. One
never seen is never despawned this way. Removal clears the record's bit 0, and activation
scripts that re-subscribe on message 2 (catalogue.md: scripts 6/38) can activate the record
again when their trigger fires: the soldier comes back fresh. In the dumps: the 1:3 soldier at
`0x800BA31C` (mid_14000) is off screen with `+0x290` = 23, the one at `0x800BCB30` never drawn
with `+0x290` = -170 (counting, but `+0x298` = 0 keeps it). Watched in the replay: see
"Despawns in the replay" below.

### The impact effect (type 0x23)

`0x800702E0` (from `0x800705EC` ← `impact_sound`, `0x80043140`, where a bullet hits): a
heap entity with a 0x130-byte object holding up to 8 particles of 0x24 bytes. Each starts
within ±8 units of the hit point with a random speed: the given direction turned by random
angles of ±512 (±45°). Its think (`0x800701DC`) counts the life down (object byte 1); over the
last 3 ticks every particle's two colours are halved each tick; at 0 it removes itself. No
collision (mask 0, flags B 0x203), drawn by `0x8006F7BC`. Presentation only (sparks or dust,
inferred).

## Layout

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | u32 | Its own handle (below) |
| +0x04 | ptr | Next in the list |
| +0x08 | s32 | Type: 1 soldier (player), 2 camera, 0x13F6 bullet, 0x13F7 pellet / weapon-11 bullet, 0x13F1 rocket, 0x1402 (ricochets: metal?), 0x1777 (ignored by bullets), ... |
| +0x0C | u32 | Flags A: 0x1 the player's soldier; 0x8 walks (character collision); 0x20 has gravity; 0x80 (slides on landing); 0x4000 bleeds; 0x20000 no physics; 0x80000 dead/removed; 0x8000000 (ignored by thrown weapons); 0x10000000 slow fall (water/ladder?) |
| +0x10 | u32 | Flags B: 0x1, 0x2/0x4 skipped as a collision target; 0x4 swept collision (bullets); 0x8 calls `+0x378` after the collision pass; 0x10 no floor check; 0x40/0x44 world-collision variants; 0x10000 bullet; 0x80000 nearest-hit-first (bullets); 0x200000 (falls through floors) |
| +0x14 | u32 | Collision mask: collides with entities whose flags A share a bit |
| +0x18 | u32 | Attachment state bits |
| +0x1C | ptr | The record's {state, record} pair it was spawned from (0: none); `[+0x1C]+4` is the record |
| +0x28 | u32 | Update stamp |
| +0x2C | u32 | Parent handle (riding: moved with the parent) |
| +0x30 | u32 | Attach handle; +0x34 (s8) the bone |
| +0x36 | u16 | Number of hit parts (0: the bounding box only) |
| +0x38 | u32 | Contact flags (0x1 on ground, 0x8000 hit something, 0x80000000 fell); also the hit-direction and brain event bits scripts wait on (soldier.md: 0x40 hit from in front, 0x20 from behind) |
| +0x3C, +0x40 | u32 | Event words set by others (grenade reactions, level messages: soldier.md, objects.md) |
| +0x48 | s32 | Invulnerability timer (damage ignored while it runs, combat.md) |
| +0xEC | u32[] | Script flag words (variables 0x100 + n, enemies.md) |
| +0xF0, +0xF4 | s32 | Maximum and current health, 16.16 (100.0 by default) |
| +0xF8 | s32 | Damage it does (bullets, explosions), 16.16 |
| +0x100 | ptr | Model/animation instance (+0x5C/+0x5E current animation and frame, +0x8 → model; animation.md) |
| +0x104, +0x178 | 2×0x74 | Script channels 0 and 1 (enemies.md): +0x10 script, +0x14 program counter, +0x18 frame target, +0x1C wait; +0x124/+0x198 a 0x3C × n block each (unknown) |
| +0x1F0 | 3×s32 | External push this tick (from a parent), 16.16 |
| +0x1FC/+0x200/+0x204 | s32 | Forward acceleration, speed and maximum (enemies.md, The brain; the player's control function writes `+0x200`, movement.md) |
| +0x208 | s32 | Floor height under the entity |
| +0x20C | 3×s32 | Position, 16.16 (+0x354.. mirrors the integer part) |
| +0x218..+0x220 | 3×s32 | Local velocity (x, vertical, z), turned by the angles into the world; +0x21C vertical: gravity adds 2.0/tick up to 75.0 |
| +0x224 | s32 | Speed scale (1.0: the local velocity as is; bullets: 600-669) |
| +0x228 | 3×s32 | Angles: pitch, yaw, roll (16.16, 4096 per turn); for characters +0x22C is the heading |
| +0x234 | 3×s32 | Angular velocity (added to the angles each tick) |
| +0x240 | 3×s32 | The player's angles from animation root motion (wrapped to 28 bits) |
| +0x258 | 3×s32 | Position at the previous tick (bullets sweep from here) |
| +0x264 | 3×s32 | Angles at the previous tick |
| +0x270 | 3×s32 | Scale, from the model (+0xA4..+0xAC; 1.0 everywhere seen; animation.md) |
| +0x284 | s32 | On the ground |
| +0x290, +0x294 | s32 | Out-of-sight counter and its start value (despawning, below) |
| +0x298, +0x29C | s32 | Drawn at least once; drawn this frame (`model_draw`'s result) |
| +0x2D4 | s32 | Eye height (240.0 for the player) |
| +0x2D8/+0x2E0 | 3×s16 | Collision sphere centre, local and world |
| +0x2E8 | u16 | Collision sphere radius (0: never collides; 150 for a soldier) |
| +0x2EA | s16 | Bone the sphere sits on (-1: the fixed point `+0x2D8`; soldiers: 0, the pelvis) |
| +0x2EE, +0x300 | | Count and list of 0x1C-byte entries allocated with the entity (unknown) |
| +0x304 | ptr | Hit parts: `+0x36` entries of 0x48 bytes (below) |
| +0x308..+0x312 | 6×s16 | Bounding box (min xyz, max xyz), rebuilt from the bones each tick |
| +0x314 | s32 | Distance term of the last part hit |
| +0x318 | | Surface the last sweep hit |
| +0x340 | | Rotation matrix |
| +0x354 | 3×s32 | Integer part of the position (x, y, z) |
| +0x364 | 3×s16 | Normal of the last contact |
| +0x36C | fn | **think**(self): once per tick |
| +0x370 | fn | draw? |
| +0x374 | fn | **hit_part**(self, my_part, other, other_part): a precise (per-part) contact; damage by body part |
| +0x378 | fn | **world_contact**(self, flags): after moving, with the contact flags |
| +0x37C | fn | **touch**(self, other): the spheres touch (or a bullet's path crosses the sphere) |
| +0x384 | fn | ? |
| +0x388 | ptr | Type-specific object (the soldier's weapon block, the camera's view, ...) |
| +0x38C | ptr | Brain: goals, perception ranges, aim error (enemies.md); updated by `brain_update` (`0x80040DF4`) |
| +0x390..+0x39C | | 0xFFFF, 0, 0xFFFF, 0 at creation (unknown; `+0x390` is the floor zone id, `floor_find`) |
| +0x3A0 | u32 | `g_frame_count` when last posed (animation.md) |
| +0x3A4 | | Home: position, then angles (enemies.md, variable 0x62) |
| +0x3BC | 3×s32 | Position before this tick's move (restored when blocked) |
| +0x3CA | s16 | Weapon index (on bullets and grenades) |

## A frame (`game_frame`, `0x800616E4`)

1. `input_poll` (`0x8004DE3C`) reads the controllers.
2. Paused: nothing else (the poll repeats for the dropped ticks).
3. Otherwise, for each tick due (1 + the vblank-paced ticks dropped, at most 4):
   `g_tick` (`0x8009A5DC`) += 1, `entities_update(g_entities)`, `entities_collide(g_entities)`,
   `texts_update` (`0x8005F69C`, the on-screen texts), and another `input_poll` when more ticks follow.
4. `render_pass`, `0x8004137C`, `0x8006B894`.

**Which ticks share a frame** matters (the draw pass between frames poses models, moves
attachments, counts sight): the state trace's per-tick vblank is **not** the frame, since the
second tick of a frame can be stamped with the next vblank (1:3 ticks 617 and 618: vblanks 2941
and 2942, one frame: the frame counter `0x8009A5D8` went 611 → 612 while `g_tick` went 616 →
618). The frames come from a watch of the frame and tick counters
(`VALOR_WATCH="fc=8009A5D8:8"`, one line per frame; `build/engine/frames.log`).

So everything that happens in a tick (moving, firing, spawning bullets) happens before any
collision of that tick, and a bullet fired during a think is swept and tested in the same tick
if it's added after the shooter in the list.

## The tick (`entities_update`, `0x8006BA28`, entered through `0x8006BA20`)

For each entity, in list order, parents first:
1. `0x8004A828(e)`.
2. Riding: with a parent (`+0x2C`), add the parent's movement this tick (position delta,
   angle delta, and its rotation applied to the offset) to `+0x1F0`/velocity. A dead parent
   (flag 0x80000) or attachment bit 0 detaches it (`0x8006D84C`).
3. `think(e)` (`+0x36C`). `entities_update` reads the entity's `next` **before** this call,
   so an entity linked at the end during the think of the last one is not updated in that tick.
   If the think removed the entity, nothing else runs for it. Then the event words `+0x38`,
   `+0x3C`, `+0x40` are cleared: what was set during the think (and before it, in the last
   tick's collisions) is gone, what `entity_move`, the collisions and later thinks set this tick
   stays for the next think.
4. Attached (`+0x30`): a stale handle or a removed target (flag 0x80000) clears it; else
   position = the target's bone `+0x34` (`0x8008DABC`, whole units << 16) and no `entity_move`.
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
   (`0x80078F60`; a move still inside a wall afterwards is undone for the player only, an enemy
   stays where the slide left it: levels.md Collision, enemies.md); everything else sweeps a line from the old position (`0x8003EF5C`: hit point,
   normal, surface at `+0x318`) and stops there.
5. The floor (`0x80060474`, height in `0x800B7460`, -0x7FFF: none): landing, stepping (up to
   0x120 units), falling (flag 0x200000 entities through holes). Exactly, for anything but a
   walker (only on the ground or with flag A 8; not attached or flags B 0x10): no floor: an
   object with flags A & 7 (unless flags B 0x10000) goes back to `+0x3BC` (and to `+0x208`'s y
   if it was in the air), on the ground, `+0x38` |= 0x80008000. On the ground: y - floor in
   -32..256 or above it keeps it on the floor; lower, flags B 0x200000 more than 57 below stops
   it dead (velocities 0, back, `+0x38` | 0x80000000), anything else steps off (off the ground,
   `+0x38` | 1). In the air with gravity (flag A 0x20): it reaches the floor if not above it by
   more than this tick's fall (whole units) + 16, at most 64; then the player lands moving down
   only; a bouncer (flag A 0x80) falling 8 or more a tick bounces (on the floor, vy = -|vy|, not
   on the ground, the floor face's normal into `+0x364` unless the move hit a wall; contact
   1); anything else lands (on the ground, y = floor, vy and `+0x1F4` 0, `+0x38` & ~1, contact
   1). A swept wall hit (flags B 0x44): contact 2, `+0x218`/`+0x220` 0, the normal in `+0x364`,
   `+0x38` | 0x8000; a non-bouncer stops at the hit point; a bouncer's x, z go back to the old
   ones plus the normal × 32 / 65536 rounded to whole units; with an up-facing normal (y ≥
   0x801), falling and moving down, vy 0 and y back.
6. Returns contact flags: 0x1 ground, 0x2 wall, 0x80 fell.

For every entity, `entity_move` first copies the forward speed `+0x200` into the local
velocity's z `+0x220` (the player's included; the flinch reads it the next tick).

The contact word `+0x38` for walkers and the player (the next think reads it, then it is
cleared): `walker_slide`'s result 2 (free after pushing, or nothing hit after a gather) sets
0x8000 and makes `entity_move` return contact 2 (so a soldier's `walker_world_contact` sets
`+0x3C` bit 2: 1:3 tick 6404); a slide that left walls (its return > 0) sets 0x8000, zeroes `+0x218`/`+0x220` and
keeps the move's delta (integer, before the slide) in `+0x364..+0x368`; result 1 (still inside)
undoes the move for the player (position = `+0x3BC`, `+0x284` restored).

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

Exactly: the outer loop reads A's `next` first; A is skipped when removed or flagged 2 in flags
B. A box pair needs B not removed, B's flags B without 2 and 4, A's mask & B's flags A, and
`spheres_touch` ((rA + rB)² - |cA - cB|² > 0, the centre differences as s16); after the two
touches `parts_touch` marks **every** touching pair of parts (part +0 = 1, `+0x314` = the
overlap r² - d², on both) but reports the first (A's lowest part, then B's); after the two
`hit_part` calls a removed A stops its loop. A swept A tests **every** entity from the list's
head (not only those after it), itself excepted, with the same conditions; its direction u =
`VectorNormalSS` of the integer path (`+0x20C` - `+0x258`, >> 16 as s16), or the forward column
of its matrix for a path of no length; |path|² from the GTE. Without flag B 0x80000 each
crossing is handled at once (`touch(A, B)`, then `segment_parts` and the two `hit_part`
calls with -1 for A's part), stopping when A is removed. With it the list `g_hit_list` gets
{entity, key (s16), part (-1 for an entity without parts), done}; the nearest is handled, and
while all three calls return 0 the next is the smallest key ≥ the last one among the entries
not done and not that entity. `segment_parts` marks every crossed part (+0 = 1; the target's `+0x314` = |d|² - r² for a
start inside the part, else r²) and reports the lowest-numbered.

## Shapes: spheres

- **Each entity**: a sphere, centre `+0x2E0`, radius `+0x2E8` (`entity_bounds` places it from
  the model each tick: on bone `+0x2EA`, or with -1 at the local point `+0x2D8` through the
  entity's matrix `+0x340` and translation `+0x354`; **the player's** (flags A 1) through a
  matrix of its yaw and roll only (no pitch), scaled by `+0x270` >> 4, at its integer position
  (`0x8008D918`): its local point (0, -100, 0) stays above its feet whatever the pitch). With
  parts the box `+0x308..+0x312` starts at -0x7FFF / 0x7FFF before `entity_parts_update`. Two entities touch when the distance between centres is less than the
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

### The soldier's hit parts

All 22-bone soldier models of the 24 level dumps (`build/lvl/*.bin`, `ent_anims.py survey`)
have 17 parts, all kind 1, with the same bones (animation.md gives radii and offsets). Left and
right were found by projecting the parts of drawn soldiers through the view matrix
(`0x8009C804`) in `mid_30000` (a soldier with its back to the camera: bones 5..7 and 18..21 on
the screen's right) and `mid_8000` (one facing the camera: the same bones on the screen's
left). So the soldier's right is model +x (forward +z, y down).

| Part | Bones | Body part | combat.md statistic | Hit reaction (soldier.md, script 5) |
|---|---|---|---|---|
| 0 | 13 (+15 along it) | Head | Head | Instant kill (combat.md), helmet |
| 1 | 21 | Right hand | Arm | 169 (anim 41) |
| 2 | 17 | Left hand | Arm | 8 (anim 42) |
| 3 | 18-20 (10 aside) | Right upper arm | Torso | 171 |
| 4 | 14-16 (10 aside) | Left upper arm | Torso | 171 |
| 5 | 20 | Right elbow | Arm | 169 |
| 6 | 16 | Left elbow | Arm | 8 |
| 7 | 9-10 (+15) | Chest | Torso | 171 |
| 8 | 9-8 (-10) | Belly and groin | Torso | 173 |
| 9 | 5-6 | Right thigh | Leg | 167 |
| 10 | 2-3 | Left thigh | Leg | 166 |
| 11 | 6-7 | Right shin | Leg | 167 |
| 12 | 3-4 | Left shin | Leg | 166 |
| 13 | 6 | Right knee | Leg | 167 |
| 14 | 3 | Left knee | Leg | 166 |
| 15 | 7 | Right ankle | Leg | 167 |
| 16 | 4 | Left ankle | Leg | 166 |

Odd parts from 9 are the right leg, even the left. The dog (type 0x1405, model 0x34D1136B in
every level that has one) has 32 bones and 5 parts.

## Despawns in the replay

Watched over the replay of the recorded session (1:3, 2:1, 2:2: up to vblank 61980, where the
replay stops matching the recording), every removal of a character-pool entity
(`tools/recon/ent_despawn.py`: flags A, `+0x290`, health and `+0x298` of the 5 slots each
frame):

| Level | Corpses removed (script 15) | Live soldiers despawned (counter 150 → 0) | Live soldiers removed otherwise |
|---|---|---|---|
| 1:3 | 42 | 1 | 0 |
| 2:1 | 10 | 3 | 0 |
| 2:2 | 14 | 3 | 3 (two at once at full counter: a level script) |

Each despawn counted down 150 consecutive drawn frames (e.g. 1:3 slot 1: 150 → 0 over 151
watched frames, removed on the frame it reached 0). No corpse lasted long enough to be
despawned (the corpse script removes it first), and the make-room rule never fired (no jump of
the counter to 1). So in normal play soldiers the player walked away from do vanish: 7 in three
levels.

## Open

- The `+0x300` list (`+0x2EE` entries of 0x1C bytes) and the channels' `+0x124`/`+0x198`
  blocks: allocated by `entity_create` from its arguments, users not traced.
- `+0x390..+0x39C` beyond the floor zone id.
- Which level scripts re-activate a despawned record in practice (catalogue.md names 6/38),
  and whether any soldier was despawned and brought back in a real session.
