# Animation: skeletons, frames, playback, hit spheres

How a model's skeleton and animations are stored, how the scripts play them one frame per
30 Hz tick, how frames and animations are blended, how animations move the entity (root
motion), and how the hit spheres follow the pose. Read from the code (functions named below)
and checked against RAM dumps of the 1:3 replay (`build/re/mid_*.bin`) and a watch trace of it
(`VALOR_WATCH`, `build/anim/watch1.log`). The file side follows the GPL-3 viewer
[Medal-Of-Honor-PSX-File-Viewer](https://github.com/AdrianoDiDio/Medal-Of-Honor-PSX-File-Viewer)
(`docs/README.md`, "BSD Files"; `src/Common/RenderObject.c`); this document describes the same
structures as the game sees them in RAM, after loading. Inferences are marked "(inferred)".

Tools: `tools/recon/anim_ents.py RAMDUMP` lists entities with their animation instance
(current animation and frame, blend counter); `tools/recon/anim_dump.py RAMDUMP MODEL
[--bones] [--anim N --frames]` prints a model's skeleton and its animations' frames from RAM.

## Summary for a reimplementation

- Skeletons are trees of rigid bones; each vertex belongs to one bone (no weights).
- Animations are 30 frames per second, one frame per game tick, no interpolation between ticks.
  Every frame stores a **model-space** rotation (a quaternion) per bone, a root offset, a
  per-frame root movement and a per-frame root turn. Most frames are not stored but
  interpolated from key frames every 2 or 4 frames (slerp at fixed weights ¼, ½, ¾).
- The scripts (enemies.md op 00) play a frame range A..B; a frame range takes B-A+1 ticks.
  The engine never loops: cycles are the scripts replaying the animation.
- Mode 1 cross-fades from the pose on screen to the new animation over 4 ticks (¼ steps);
  mode 2 cuts; mode 0 continues.
- Root movement is applied once per frame shown, turned by the entity's orientation; the root
  turn is added to the angles every tick the animation holds a frame.
- The hit spheres sit at bone joints (or between two), plus an offset turned by the bone. The
  joints are only computed when the model is **drawn**, so the spheres use the pose of the last
  frame drawn: one tick behind for an enemy on screen, frozen for one off screen, and all at
  the feet for one never drawn.

## Where things are in RAM

| What | Where | Notes |
|---|---|---|
| Models (render objects) | `g_models` (`0x8009EC58`), 0x100 bytes each, `g_model_count` | The BSD's render objects with offsets relocated to pointers (`model_relocate`) |
| Animation tables, frames, quaternions, bones, vertices | The level's BSD data, `0x801D0000`..`0x801FFFFF` in 1:3 | Shared by models: every soldier model of a level points at the same animation table (`0x801ED648` in 1:3) |
| Instance (per entity) | `entity+0x100`, allocated with the entity (`0x800BB2D8` for the soldier at `0x800BA31C`) | Below |
| Pose buffers (per entity) | Allocated by `model_instance_init` (`0x8006A96C`) from the entity's pool (`0x8008ED30` for types 0x1402/0x1405) | Three quaternion arrays and the joint array, 8 bytes per bone |
| Skinned vertices | `0x800AB148` onward (cursor `0x8009EABC`), rebuilt for every model drawn | Model space, 8 bytes per vertex |

### The model (render object) fields animation uses

| Offset | Meaning |
|---|---|
| +0x00 | Hash (id) |
| +0x08 | Animation table (0: no skeletal animation) |
| +0x38 | Face table → instance +0x7C |
| +0x84 | Vertex table → instance +0x80: per bone index {+0 vertices, +4 count}, vertices {s16 x, y, z, pad} in the bone's space |
| +0x98 | A second animation table format (16-byte entries, 24-byte frames), used when +0x08 is 0. Code exists (op 00, `entity_move`, root motion); no model of 1:3, 2:1, 2:3, 3:2 uses it |
| +0x9C | Root bone → instance +0x84 |
| +0xA4..+0xAC | Scale x, y, z (16.16; 1.0 in every sampled model), copied to entity +0x270 (inferred: entities.md lists +0x270 as rotation) |
| +0xBE | Number of hit parts → entity +0x36 |
| +0xC0 | Hit part templates (0x48 bytes each, copied to entity +0x304) |
| +0xEC..+0xF8 | Up to four other models' hashes that an animation can swap the mesh with (below); -1 in every sampled model |

### The skeleton

A bone record (20 bytes), reached from the model's root bone, a first-child/next-sibling tree:

| Offset | Type | Meaning |
|---|---|---|
| +0x0 | u16 | Bone index (indexes the quaternions, the vertex table and the joints) |
| +0x2 | 3×s16 | Offset from the parent's joint, in the parent's rotated frame |
| +0x8 | u16 | 0 |
| +0xA | u16 | 0xCDCD (padding) |
| +0xC | ptr | First child (0: none) |
| +0x10 | ptr | Next sibling (0: none) |

The 1:3 soldier (`anim_dump.py mid_14000.bin 25 --bones`): 22 bones. 0 the root (pelvis), 1 hips;
legs 2→3→4 and 5→6→7 (hip at ±18 sideways, then 70 and 80 long: knee, ankle); spine
8→9→10→11 (27 apart), neck/head 12→13; arms from 11: 14→15→16→17 and 18→19→20→21
(collarbone ±7, then 25/26, 52, 41/45: shoulder, elbow, hand). The names are inferred from the
lengths and from where the hit parts sit. The dog (type 0x1405) has 32 bones; the first-person
weapon models 14 to 32.

### Animation table and frames

The table: u16 count, u16 0xCDCD, then `count` pointers to entries (0 for animations this
level doesn't load: the soldier's table has 335 slots, 183 filled in 1:3, 217 in 2:1).

An entry (8 bytes): u8 frame count, u8 bone count, u8 mesh (0: the model's own; n: the model
`+0xEC[n-1]`, looked up when op 00 starts the animation, `0x80057D9C`), u8 0xCD, pointer to
the frames.

A frame (20 bytes):

| Offset | Type | Meaning |
|---|---|---|
| +0x0 | u32 | **Root movement** this frame: x bits 0-9, y bits 10-20, z bits 21-30, each signed (10, 11, 10 bits), model units |
| +0x4 | u32 | **Root offset**: the root joint's position in the model, packed the same way (the pelvis height: y about -130..-150 for a standing soldier, y down) |
| +0x8 | 3×s16 | **Root turn** this frame: pitch, yaw, roll (4096 per turn) |
| +0xE | u8 | Key frames: high nibble = frames to the next key, low nibble = frames back to the previous key (0, 0 on a key) |
| +0xF | u8 | Number of bones (quaternions) |
| +0x10 | ptr | The frame's quaternions (key frames); 0 on frames to interpolate |

The viewer reads +0x0 and +0x8..+0xD as unknowns and +0x4 as "the translation"; which word
does what was read from the code: `anim_root_motion` (`0x8003F1F8`) decodes +0x0 as the
movement and copies +0x4 into instance +0x98, the skeleton's root (`0x80084718`); `entity_move`
adds +0x8.

**Quaternions** are packed two per three words (an odd last one in two words), each component
a signed 12-bit value ×2 (4096 = 1.0), unpacked by `quat_decode` (`0x800847CC`) into
{x, y, z, w} s16. The bit layout is the viewer's (`RenderObjectDecodeQuaternions`), identical
to the code: first quaternion w = word0 bits 20-31, x = word0 bits 4-15, y = word1 bits 0-11,
z = word1 bits 12-15 (high, signed) : word0 bits 16-19 : word0 bits 0-3; second quaternion x =
word1 bits 20-31, y = word2 bits 16-27, w = word2 bits 4-15, z = word2 bits 28-31 (high,
signed) : word2 bits 0-3 : word1 bits 16-19; every component then ×2.

A quaternion is the bone's rotation **in model space**, not relative to its parent: a bone's
joint is its parent's joint plus the parent's rotation applied to the bone's offset, and its
vertices are turned by its own rotation only. The rotation matrix the game builds from {x, y,
z, w} (`0x80013E58`, and the same GTE sequence in `entity_parts_update` and `0x8008DABC`) is the
**transpose** of the textbook one, i.e. it rotates by the conjugate. Checked: rebuilding the 21
joints of the soldier at `0x800BA31C` (mid_14000) from its quaternions and the bone offsets
gives the joints in RAM within 22 units in total with the transposed matrix, 1244 with the
textbook one.

**Key frames.** Only every 2nd or 4th frame stores quaternions; in all models of mid_14000 and
mid_50000 the gaps are 2 (one frame between, at ½) or 4 (three frames, at ¼, ½, ¾), never
anything else. Frame 0 and the last frame are keys. Animations that must be exact (the
first-person Garand's firing, anim 1) key every frame. Quaternion lists are shared between
frames with the same pose (the Garand's anim 1 last frame points at its first frame's data).

Root movement totals over whole animations (`anim_dump.py`, the 1:3 soldier): walk (anim 7)
19 frames, 26 units forward per frame except frame 0 (0), 467 in all; run (14) 537 over 18
frames (29.8 per tick); turns (2, 3, 4) -1030, 2040 (≈180°), 655 yaw units in 204-unit steps
over their first frames, with small sideways steps. Forward is +z in the model.

## The instance (`entity+0x100`)

Set up by `model_instance_init` (`0x8006A96C`) when the entity is made from the model.

| Offset | Type | Meaning |
|---|---|---|
| +0x08 | ptr | Model |
| +0x2C, +0x30, +0x3C | | The model's static vertex copy and count (non-skeletal drawing) |
| +0x3E | s16 | **Blend counter**: 4 at the start of a cross-fade, down to 0 (below) |
| +0x40 | s16 | Frame of the new animation to blend toward if ≥ 0 (always -1: set so by mode 1) |
| +0x5C | s16 | **Current animation** (-1: none) |
| +0x5E | s16 | **Current frame** |
| +0x60 | s16 | Blend target frame: the range's first frame, at least 3 |
| +0x62, +0x64 | s16 | Animation and frame of the pose last computed (a cache: the pose is rebuilt only when they, or the blend counter, change) |
| +0x70 | u16 | Number of animations |
| +0x72 | u16 | Number of bones |
| +0x74 | ptr | Animation table (0: the model has no skeletal animation) |
| +0x78 | ptr | Its entry pointers (+0x74 + 4) |
| +0x7C, +0x80 | ptr | Face table, vertex table (the model's, or another model's for a mesh-swapping animation) |
| +0x84 | ptr | Root bone |
| +0x88 | ptr | **The pose**: the quaternions in use (points at +0x90's buffer, or +0x8C's during the first tick of a cross-fade) |
| +0x8C | ptr | The pose at the start of the cross-fade (a copy) |
| +0x90 | ptr | Buffer the pose is built in |
| +0x94 | ptr | **Joints**: per bone the joint position in model space (3×s16 + pad), written when the model is drawn |
| +0x98 | 3×s16 | Root offset in use (the current frame's +0x4, blended during a cross-fade) |
| +0xA0 | 3×s16 | Root offset at the start of the cross-fade |

## Playback

### Who plays animations

Entities with model scripts run them through `script_run_a` (`0x8003F8F0`): enemy soldiers and
dogs (`enemy_think`), the player's soldier (`player_update`, `player_think_mounted`), the
camera (`0x800473DC`, only with its view flag 0x800), the mounted gun, and others
(`0x80046A88`, `0x8004787C`, `0x80054AB8`, `0x80061A2C`, ...). The current animation and frame
live only in the instance; the scripts are the only thing that changes them (besides resets:
-1 when a script ends, `script_run_a`).

### Op 00: `anim N frames A..B` (`script_op_anim`, `0x80057D9C`)

The instruction is two words: the opcode word's 16-bit immediate holds N (bits 0-11) and the
mode (bits 12-15); the operand word holds A (low half) and B (high half).

1. N's entry must exist (else a fatal error, `0x80089B04(1, 0x100)`).
2. Animation = N, frame = A, +0x60 = max(A, 3).
3. The channel's frame target (context +0x18) = B; B = -1 means the last frame (count - 1);
   B = A gives target -1 (the frame shows for one tick).
4. Mode (only when the script runs normally, not from an event handler: `script_step`'s 5th
   argument 0):
   - **1, cross-fade**: copy the pose on screen (+0x88's quaternions) to +0x8C and the root
     offset +0x98 to +0xA0; blend counter = 4; +0x40 = -1.
   - **2, cut**: blend counter = 0.
   - **0, continue**: a running cross-fade's counter goes down by one.
5. Mesh: the entry's mesh byte selects the model whose faces and vertices are drawn.
6. Frame A's root movement is applied (`anim_root_motion`, below), the channel's wait counter
   is cleared, the player's root-turn accumulators (+0x240..+0x248) are zeroed, and the script
   yields.

Mode counts in the soldier's 1:3 scripts: 389 mode 0, 250 mode 1, 33 mode 2. Hit reactions
(anims 29..39) cut (mode 2); idles, walks, firing poses usually fade in (mode 1) on their
first range and continue (mode 0) on the next ones. All seven first-person weapon animations
are played with mode 2. Long animations are played as several ranges (walk: `anim 7 frames
0..4`, `5..14`, `15..-1`) so the script can act between them; consecutive ranges join without
losing or repeating a frame (below).

### The frame advance (`script_run_a`, every tick)

For each of the two script channels that has a script:
- its wait counter (op 16 `wait N`) > 0: count down, nothing else;
- else, the current frame < the channel's target: **frame + 1**, the blend counter - 1 if > 0,
  and the new frame's root movement is applied; the channel yields;
- else: target = -1 and the script steps until an instruction yields.

So a range A..B shows each frame for exactly one tick: A on the tick the op runs, A+1 the next
tick, ..., B, and on the tick after B the script goes on (and a following `anim N frames B+1..`
puts frame B+1 on that tick): B-A+1 ticks per range, **30 frames per second**, no
sub-tick interpolation, no looping by the engine. Watched (enemy at `0x800BA31C`, every tick of
the replay, `build/anim/watch1.log`): the frame steps by exactly 1 per tick; a mode-1 start sets
the counter to 4 and it goes 3, 2, 1, 0 with the next four frames; the pose cache (+0x62/+0x64)
follows the current animation and frame one tick later.

(Inferred, not seen:) the frame is shared by both channels, so if both had a target ahead it
would advance twice in a tick; the soldier's scripts animate from one channel at a time.

### Root movement (`anim_root_motion`, `0x8003F1F8`)

Only for entities with flags A 0x1000 (soldiers, dogs). For the frame just made current:
1. The root offset (+0x4) goes to the instance's +0x98.
2. The root movement (+0x0) is decoded; moving up (y < 0, y points down) takes the entity off
   the ground (`+0x284` = 0, `0x80075118`).
3. Scaled by the speed scale `+0x224` (when not 1.0), kept at `+0x27C` (3×s16), turned by the
   entity's rotation matrix `+0x340`, and added to `+0x1F0` (16.16): `entity_move` adds that to
   the position in the same tick, then the walker collision applies as for any movement.

So distance covered comes from the animation frame by frame; frames skipped by the scripts
don't move the entity, and a held frame doesn't either.

### Root turn (`entity_move`, `0x8006C83C`, every tick)

While the instance has an animation and a frame ≥ 0, the frame's +0x8 (pitch, yaw, roll) is
added to the angles every tick, **including ticks that hold the frame** (a wait, the end of a
range). Enemies and others: to `+0x228..+0x230`, then each angle is wrapped to 0..4095 and its
fraction dropped (`& 0x0FFF0000`). The player's soldier (type 1): to `+0x240..+0x248`, wrapped to
28 bits (used when its first-person model is drawn, `0x8008D660`; inferred). The turning
animations have their turn on their first frames and 0 on the last ones (`anim_dump.py --anim
3 --frames`: 204 per frame on frames 1..10, 0 from 11), so holding the end doesn't turn.

## The pose (`pose_build`, `0x80085648`)

Built at most once per displayed frame: `pose_update` (`0x80086634`) compares the entity's
`+0x3A0` with the frame counter (`g_frame_count`, `0x8009A5D8`, incremented by `game_frame`),
from the entity's draw (`entity_draw_model`, `0x8006A5B4`) or the camera's (`0x80046DD4`). When
the game runs two or more ticks in a frame, only the last tick's frame is ever posed.

1. No animation (-1): nothing (the cache is set to -1).
2. Not blending (counter 0): if the animation or frame changed, the frame's quaternions
   (`anim_frame_pose`, below) go into +0x90's buffer, +0x88 points at it.
3. Blending (counter c > 0, and a pose existed): the target is the new animation at frame +0x40
   if ≥ 0, else +0x60 (the range's first frame, at least 3); the result is the pose saved at the
   start (+0x8C) interpolated toward the target at **t = (4 - c)/4**: c = 4 shows the saved pose
   as is, 3, 2, 1 show ¼, ½, ¾ of the way, and at 0 the pose is the current frame again. The
   root offset (+0x98) is interpolated linearly from +0xA0 the same way.

So a mode-1 animation started at frame 0 is shown as: the old pose (tick 0), ¼, ½, ¾ toward
its frame 3 (ticks 1-3, while its frames 1..3 play underneath: root movement included), then
its frame 4 exactly. A range started later (A ≥ 3) fades toward frame A and then jumps to A+4.
A fade started during another fade starts from what was on screen. Seen in mid_8000: an enemy
at frame 2 of anim 26 with counter 2 has posed anim 26 frame 3.

### Interpolating quaternions (`anim_frame_pose`, `0x8008492C`, and the fade)

A frame with its own quaternions is decoded. Otherwise the previous key (frame - low nibble)
and next key (frame + high nibble) are decoded and each bone interpolated at t = ½ when the
keys are 2 apart, else t = (frame - previous key)/4. Per bone, with a and b the two
quaternions (4096 = 1.0):

- d = (a·b + 0x800) >> 12 (4D dot product).
- |d - 4096| ≤ 8 (`0x8009EAB0`): no interpolation, one of the two is used as is.
- If d < 0, b is negated (shortest way).
- 4096 - |d| < 140 (`0x8009EAAC`, about 15°): **linear** a + (b - a)·t, rounded, not
  renormalised.
- Else **slerp**: θ = ratan2(√(1 - d²), d) (`SquareRoot12`, `ratan2`: 4096 per turn), weights
  sin(θ - θt)/sin θ for a and sin(θt)/sin θ for b (`rsin`, the products shifted by 17).

The cross-fade uses the same code with the saved pose as a and the target as b.

## Skinning and joints (`skeleton_pose`, `0x80084718`; `skeleton_walk`, `0x80013AE4`)

Done inside the model drawing (`model_draw` `0x8006D2E4` → `model_render` `0x800824D0` →
`0x80082948` → `0x80080DD4` / `0x800814C4` → `0x80084718`), i.e. **only for models drawn**.
From the root bone with an identity rotation and the root offset (+0x98) as position:
- bones with an index beyond the frame's bone count are skipped;
- the bone's matrix: rotation = its own quaternion (transposed form, above); position = the
  parent's position + the parent's rotation × the bone's offset (kept in 2^19 fixed point,
  rounded to integers at the end);
- its vertices are turned and moved by that matrix into the skinned-vertex buffer;
- its first child is walked with this bone's matrix, its next sibling with the parent's;
- the joint (the bone's position) is stored in the instance's joints (+0x94).

The model's placement in the world (entity position and the matrix `+0x340`) is applied after,
when drawing.

### When a model is posed and drawn (`entity_draw_model`, `0x8006A5B4`)

`render_pass` calls every entity's draw handler (`+0x370`, `entities_draw` `0x8006C714`).
Entities with flags A 0x02000000 (soldiers have 0x03000000) that are outside the view
(`entity_in_view`, `0x8008E9DC`: the bounding sphere against the view, inferred) are not posed
once they have been drawn before (`+0x298`, set by `model_draw` when it drew something); with
0x01000000 they are not drawn either. In mid_14000 the soldier at `0x800BA31C` is off screen:
its animation is 7 frame 14 but its pose (+0x62/+0x64) is anim 220 frame 21, last posed 144
frames earlier; the soldier at `0x800BCB30` has never been on screen (+0x298 = 0): posed every
frame but never drawn, its joints are all zero.

## Hit spheres from the pose (`entity_parts_update`, `0x8004CEE4`)

Called by `entity_bounds` (`0x8004D43C`) at the end of every entity's update in the tick (after
think and move, before the collision pass), for entities with hit parts. A part (entity
`+0x304`, 0x48 bytes, from the model's templates at +0xC0):

| Offset | Meaning |
|---|---|
| +0x08 | Kind (combat.md: damage scale, 7 never hit, 5 attack) |
| +0x18 | The instance |
| +0x1C, +0x1E | Bones a and b; 0xFFFF: a fixed point; 0xFFFE: a fixed point raised by the player's eye height (player weapon block `+0x94`) |
| +0x20 | Centre in the world (3×s32), the result |
| +0x34 | Radius (also used for the bounding box) |
| +0x38 | Radius² (the hit test) |
| +0x40 | Offset (3×s16) |

Centre, in model space: **(joint[a] + joint[b]) / 2 + R(a) × offset**, R(a) bone a's rotation
(skipped when the offset is 0); a fixed point is just the offset. Then into the world by the
entity's matrix `+0x340` and position. The bounding box `+0x308..+0x312` is rebuilt from the
centres ± radii. The entity's own sphere (`+0x2E0`) is placed on bone `+0x2EA` the same way
(`0x8008E354` → `0x8008DABC`), or at the local point `+0x2D8` when that is -1. Checked: the 17
part centres of the soldier at `0x800BA31C` (mid_14000) recomputed from its joints, quaternions
and offsets match RAM within one unit.

The 1:3 soldier's parts (radius): 0 head: bone 13 + 15 along it (20); 1, 2 hands: 21, 17 (20);
3, 4 between collarbone and elbow, 10 to the side: 18-20, 14-16 (20); 5, 6 elbows 20, 16 (20);
7 chest 9-10 + 15 (30); 8 belly 9-8 - 10 (35); 9, 10 thighs 5-6, 2-3 (25); 11, 12 shins 6-7, 3-4
(20); 13, 14 knees 6, 3 (20); 15, 16 ankles 7, 4 (20). All kind 1. (Which side is left is not
established.)

**Consequences** (from the order: ticks, then collisions, then drawing):
- An enemy on screen is hit where it was drawn the frame before, moved and turned to where it
  is now: the pose lags the animation by one tick (more when frames are dropped: up to 4 ticks
  per frame).
- An enemy off screen keeps the last pose it was drawn in, carried along with its position and
  heading. Only enemy bullets and explosions can hit it there (the player can't aim at it).
- An enemy never drawn has every joint at 0, so every hit sphere sits at its origin (the feet),
  plus the parts' offsets.
- The player's soldier: three parts are fixed points (1, 2 at eye height, 3 below: radii 30,
  70, 70), unaffected by animation; part 0 sits on bone 22 of the first-person model with kind
  7 (never hit), except during a melee swing (below).

## The player: first-person weapon models

The player's soldier (type 1) has no body: its model is the weapon in hand (one per weapon:
the Garand, the SMG, the shotgun, the grenade in 1:3, model type 1, the hashes being the
viewer's weapon ids), with its own skeleton (arms and gun: 23 bones for the Garand, 32 for the
SMG) and its own table of 7 animations. They run through the same instance, op 00 and pose
code, from the model's scripts (`player_update` runs `script_run_a`/`_b`), and the model is
drawn like any other by the soldier's draw handler (`player_draw`, `0x8007B8F4` →
`entity_draw_model`; placed with `0x8008D660`, which uses `+0x240`, inferred) while the weapon
block's `+0xA4` (the aim distance, combat.md) is 0x190 and its flags 0xC000 are clear (not on
the mounted gun; the scope presumably changes `+0xA4`: inferred). In the dumps its pose is at
most one frame old and its joints are filled.

From the Garand model's scripts (`sst.py --type 1`; names inferred from when they play): anim 0
the idle (looped by script 7), 1 firing (script 8, on the fire event, then back to idle), 2 (at
the start, script 16: raising?), 3 (held, scripts 10/15: lowered?), 4 (scripts 9/11: reload?),
5 (scripts 0/1), 6 the rifle-butt swing (script 13: frames 0..7, then 8..12 with a hit part
switched to kind 5, an attack, then back to kind 7). All mode 2: weapon animations cut, never
fade. Their root movement and turn are 0 in the Garand's animations; the root offset (0, 480,
-127) places the arms relative to the eye (inferred).

## Corrections to other documents

- enemies.md lists `0x80072704`/`0x800730D4` in the enemy think as "(animation)". They are not:
  they update two small effect pools in the enemy's weapon block. `0x80072704` (`+0x3E8`, 5
  entries of 0x24 bytes): puffs that rise 1 unit a tick and fade (colour × 7/8 a tick) for their
  life; drawn as sprites by `0x80072310`. `0x800730D4` (`+0x49C`): particles that fall, slow down
  and lie flat when they reach the floor (`0x80072EEC`); drawn by `0x800729C0`. Smoke and blood
  drops (inferred). Frames are advanced by `script_run_a`.
- entities.md: `+0x270` is filled from the model's scale (+0xA4..+0xAC; 1.0 everywhere seen),
  not angles (inferred from `model_instance_init`; the matrix build `0x8008D918` takes it as its
  third argument).

## Open

- The second animation format (model +0x98, 24-byte frames) and the models that use it, if any
  level does.
- `entity_in_view` (`0x8008E5B0`): frustum only, or also distance; and how often enemies fight
  off screen with frozen spheres in practice.
- Which channel animates when both run (the double-advance case).
- Left/right of the soldier's limbs, and the names of the first-person animations, by watching
  them (`VALOR_WATCH` on `[g_soldier+100]+5C` while firing, reloading, switching).
- The dog's (0x1405) skeleton and parts, and the mesh-swapping animations (+0xEC models) if a
  level uses them.

## Names

```
80057D9C script_op_anim             # op 00: anim N (mode in bits 12-15) frames A..B; mode 1 fade, 2 cut
8003F1F8 anim_root_motion           # frame's root movement (+0) via +0x340 into +0x1F0; root offset (+4) -> inst+0x98
80085648 pose_build                 # current frame or 4-tick cross-fade into inst+0x88/+0x90
80086634 pose_update                # pose_build once per displayed frame (entity +0x3A0 vs g_frame_count)
8008492C anim_frame_pose            # (inst, anim, frame, out): decode a key frame or slerp between keys
800847CC quat_decode                # (packed, out, count): 2 quaternions per 3 words, 12-bit components x2
80084718 skeleton_pose              # skin a drawn model: root = inst+0x98, then skeleton_walk
80013AE4 skeleton_walk              # (bone, parent matrix, out table, inst): vertices + joints (inst+0x94)
80013E58 quat_matrix                # quaternion -> GTE rotation (transposed form)
8006A96C model_instance_init        # (inst, model, flags, entity): pose buffers, hit parts from model +0xC0
8006A5B4 entity_draw_model          # pose (unless off screen and seen before) and draw
8006C714 entities_draw              # every entity's +0x370 from render_pass
8006D2E4 model_draw                 # place + render; sets +0x298 (drawn once)
800824D0 model_render               # skeletal models -> 0x80082948 -> skinning
8008E9DC entity_in_view             # bounding sphere against the view (inferred)
8008E354 entity_bone_point          # (entity, bone, out, offset): a point on a bone in the world
8008DABC model_bone_point
8005608C enemy_draw                 # soldier's +0x370
8007B8F4 player_draw                # player soldier's +0x370
80046DD4 camera_draw                # camera's +0x370: poses and skins its model
80075118 entity_leave_ground        # +0x284 = 0
80072704 enemy_puffs_update         # weapon block +0x3E8: 5 rising, fading puffs (smoke, inferred)
800730D4 enemy_drops_update         # weapon block +0x49C: falling particles that lie on the floor (blood, inferred)
80072310 enemy_puffs_draw
800729C0 enemy_drops_draw
8009A5D8 g_frame_count              # +1 per game_frame (displayed frame)
8009EAAC g_quat_lerp_limit          # 0x8C: below this from 1.0 the slerp is linear
8009EAB0 g_quat_same_limit          # 8: this close to 1.0, no interpolation
8009EABC g_skin_cursor              # next free skinned vertex (from 0x800AB148)
```
