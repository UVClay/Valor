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
| +0x98 | A second animation table format (16-byte entries, 24-byte frames), used when +0x08 is 0. Code exists (op 00, `entity_move`, root motion); **no model of any level uses it**: 0 in every model of the 24 level dumps (`build/lvl/*.bin`: 1:1..7:3 and 9:1, 9:2, 9:4; `ent_anims.py survey`). A reimplementation can drop it |
| +0x9C | Root bone → instance +0x84 |
| +0xA4..+0xAC | Scale x, y, z (16.16; 1.0 in every sampled model), copied to entity +0x270 (inferred: entities.md lists +0x270 as rotation) |
| +0xBE | Number of hit parts → entity +0x36 |
| +0xC0 | Hit part templates (0x48 bytes each, copied to entity +0x304) |
| +0xEC..+0xF8 | Up to four other models' hashes that an animation can swap the mesh with (below); -1 in every model of the 24 level dumps (0 in the empty model 0), and no animation entry of any model has a non-zero mesh byte: **mesh swapping is unused** |

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
lengths and from where the hit parts sit. Bones 5..7 and 18..21 are the **right** leg and arm
(model +x is the soldier's right; checked by projecting drawn soldiers' parts on screen,
entities.md), 2..4 and 14..17 the left. The dog (type 0x1405, model 0x34D1136B) has 32 bones
and 5 parts; the first-person weapon models 3 to 32 bones.

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

**A point on a bone** (`model_bone_point`, `0x8008DABC(e, inst, bone, out, offset)`): without
joints (instance `+0x84` 0) the entity's integer position. Otherwise the bone's joint (instance
`+0x94`, 8 bytes per bone), plus, with an offset, the bone's quaternion matrix (from `+0x88`)
applied to it (>> 12); then into the world by the entity's matrix `+0x340` and the integer
translation `+0x354` (s16 results). **For the player's soldier (type 1) the camera object's eye
offset (`+0x94`, the halfword) is added to y**: the first-person arms and gun hang from the eye,
so the gun points `aim_update` and `fire_gun` use move with crouching (combat.md).

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
4. Mode (whenever `script_step`'s 5th argument, kept at `0x80099EB0`, is 0, which is always:
   both callers, `script_run_a` and the event matcher `script_run_b` for both channels, pass 0
   (`sw $zero, 0x10($sp)` before `jal script_step` at `0x8003FB68` and `0x8003FF38`), so event
   handlers' animations fade and cut like any other; an earlier version of this document said
   handlers skipped this):
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

**The double advance.** The frame is shared by both channels and each channel compares it with
its own target, so a channel whose range is still running when the other starts a new
animation (frame reset to A) would also step it: two frames per tick until the lower target.
`script_op_anim` sets only the running channel's target. It does not happen in play: over the
replay of 1:3, 2:1 and 2:2 (about 30,000 ticks, the five character slots and the player's
weapon model watched every frame: `tools/recon/ent_double.py`), no frame ever advanced by
more than the ticks run, and no frame boundary had both channels' targets ahead of the frame.
When channel 0 starts an animation, channel 1's target is already -1 (the soldier's scripts
stop channel 1, `clear_script`, before animating from channel 0). A reimplementation that
keeps one frame per entity and lets only the channel that started the range advance it gives
the same result.

### Root movement (`anim_root_motion`, `0x8003F1F8`)

Only for entities with flags A 0x1000 (soldiers, dogs). For the frame just made current:
1. The root offset (+0x4) goes to the instance's +0x98.
2. The root movement (+0x0) is decoded; moving up (y < 0, y points down) takes the entity off
   the ground (`+0x284` = 0, `0x80075118`).
3. Scaled by the speed scale `+0x224` (when not 1.0: each component × scale >> 16, rounding
   down), kept at `+0x27C` (3×s16), turned by the entity's rotation matrix `+0x340`
   (`ApplyRotMatrix`: GTE, >> 12, rounding down), and added to `+0x1F0` as whole units (<< 16):
   `entity_move` adds that to the position in the same tick, then the walker collision applies
   as for any movement.

The matrix `+0x340` is rebuilt once a tick, after the brain (`0x8006A76C`, `RotMatrixYXZ` of the
angles; enemies.md, The brain), so the root movement goes along the heading of the **end of the
previous tick**: a heading the scripts set in the same think, and the frame's root turn (added
later, in `entity_move`), only steer the next frame's movement. The speed scale is the brain's
of the previous tick too, unless the scripts gave a movement goal earlier in the think
(`brain_reset_gait` sets it to 1.0). Every frame change applies its root movement once, both
the advance and op 00 (which also applies frame A's, so restarting an animation at the frame it
shows moves again). Checked exact to the 16.16 bit over 10,851 moving ticks of soldiers and dogs
in 1:3, 2:1 and 2:2 (`tools/sim/brain_sim.py`; `+0x27C` agreed in all 25,034 frame changes).

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
The masking applies to every angle each tick the instance has an animation, so the brain's
fractional turns (enemies.md) are dropped at the next move. Checked with the enemies' moves
(`brain_sim.py`: 442 ticks with a root turn, every heading, pitch and roll exact).

## The pose (`pose_build`, `0x80085648`)

Built at most once per displayed frame: `pose_update` (`0x80086634`) compares the entity's
`+0x3A0` with the frame counter (`g_frame_count`, `0x8009A5D8`, incremented by `game_frame`),
from the entity's draw (`entity_draw_model`, `0x8006A5B4`) or the camera's (`0x80046DD4`). When
the game runs two or more ticks in a frame, only the last tick's frame is ever posed.

1. No animation (-1): nothing (the cache is set to -1).
2. Not blending (counter 0): if the animation or frame differs from the cache (+0x62/+0x64), the
   frame's quaternions (`anim_frame_pose`, below) go into +0x90's buffer, +0x88 points at it,
   and the cache takes the animation and frame. If they match, nothing is rebuilt.
3. Blending (counter c > 0, and a pose existed: cache animation not -1): the target is the new
   animation at frame +0x40 if ≥ 0, else +0x60 (the range's first frame, at least 3); the
   result is the pose saved at the start (+0x8C) interpolated toward the target at
   **t = (4 - c)/4**: c = 4 shows the saved pose as is (+0x88 = +0x8C), 3, 2, 1 show ¼, ½, ¾ of
   the way, and at 0 the pose is the current frame again. The root offset (+0x98) is
   interpolated from +0xA0 the same way, per component: c = 3: A + (T - A) >> 2; c = 2:
   A + (T - A) >> 1; c = 1: T - (T - A) >> 2. **The cache takes the target frame**, not the
   current one, so a fade whose counter reaches 0 on its target frame (a mode-0 range after it
   took one off early) keeps showing the ¾ pose one more frame: the cache matches, nothing is
   rebuilt (182 such draws in the session below).
4. A fade started with no pose yet (cache animation -1): `pose_build` itself sets the blend
   counter to 0 and builds the current frame (the VM only sets it to 4). With +0x88 still 0
   (never posed) the frame is built into +0x90 and the cache is left alone.

Checked tick by tick over the replay of 1:3, 2:1 and 2:2 (`tools/sim/combat_sim.py check`: the
five character slots' instances and pose buffers watched every frame, the pose recomputed from
the animation data in RAM as the previous frame's draw saw the instance): every pose built
matches RAM exactly: 12914 plain frames (key or interpolated), 2511 fade steps, 1021 fade starts
showing the saved pose, 182 kept by the cache, and the cache itself in all 16628.

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

The fixed point, exactly: the root's position is the root offset << 19; a bone's offset (3×s16)
goes into the GTE as offset × 16 and is multiplied by the parent's rotation (4096 = 1) without
a shift, then << 3, so positions are in units of 2^-19; the joint is (2·P + 0x80000) >> 20 per
component (rounded). A bone whose index is beyond the frame's bone count ends the walk of its
whole sibling chain, not only its own subtree. Checked: the joints of every draw in the replay
of 1:3, 2:1 and 2:2 recomputed from the pose in RAM this way match RAM exactly (14651 draws;
`tools/sim/combat_sim.py`).

The model's placement in the world (entity position and the matrix `+0x340`) is applied after,
when drawing.

### When a model is posed and drawn (`entity_draw_model`, `0x8006A5B4`)

`render_pass` calls every entity's draw handler (`+0x370`, `entities_draw` `0x8006C714`).
Entities with flags A 0x02000000 (soldiers have 0x03000000) that are outside the view
(`entity_in_view`, below) are not posed once they have been drawn before (`+0x298`, set by
`model_draw` when it drew something); with 0x01000000 they are not drawn either (and their
out-of-sight counter runs, entities.md). Others are always posed and drawn (the renderer
clips them).

**`entity_in_view` (`0x8008E9DC`)** tests the cube around the entity's collision sphere
(centre `+0x2E0` ± radius `+0x2E8` on each axis; a soldier's sphere is on its pelvis, radius
150) with **`box_in_view` (`0x8008E5B0`, `(min, max)`)**, a frustum test done with the GTE:
the view's rotation and translation (`0x8009C804`, the current camera matrix: rows scaled 2,
1.25, 2) are loaded, then for each of six directions kept in the camera's view block
(`[[g_player+0x388]+0xC8]`: `+0x50`, `+0x6C`, `+0x88`, `+0xA4`, `+0xC0`, `+0xDC`, 3×s32 each,
`+0xDC` = -`+0x50`) the box corner farthest along that direction is picked (per axis max if the
component is ≥ 0, else min) and projected (`rtps`). Outside, and so not in view, if:

| Direction | Test on its corner | Edge |
|---|---|---|
| `+0x50` (forward) | GTE flag bit 18 (SZ3 < 0: behind the eye) | Near |
| `+0x6C` | screen x < 0 | Left |
| `+0x88` | screen x > `[0x8009C828]` = 540 | Right (28 pixels past the 512-wide screen) |
| `+0xA4` | screen y < 0 | Top |
| `+0xC0` | screen y > `[0x8009C82C]` = 240 | Bottom |
| `+0xDC` (backward) | depth SZ3 > view `+0x40` (the far distance, camera object `+0xC4`: 4600 in 1:3, the level's `+0x590`, else 4500) | Far |

Otherwise in view. Each test uses the corner most likely to be inside, so a box is rejected
only when even that corner is outside one edge: no false negatives against a single plane
(the classic "p-vertex" test). The screen is 512 × 240 with the projection centre at (256,
120) and distance 400 (`0x8009A620`..`0x8009A628`): the original view is 65° wide (32.6° left,
35.4° right with the margin) and 51° high. No distance limit other than the far plane.
`0x8005E760` (another draw handler) uses the same test. The directions are written with the
view each frame (`0x8008D0CC`, which Valor widens for its wider views: runtime `game.cpp`). In mid_14000 the soldier at `0x800BA31C` is off screen:
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
| +0x1C, +0x1E | Bones a and b; 0xFFFF: a fixed point; 0xFFFE: a fixed point whose y gets the camera object's `+0x94` added (`[[g_player]+0x388]+0x94`, g_player the camera `0x8009D638`, read at `0x8004CF70`: -240 standing, the player's parts in d1720 put it there) |
| +0x20 | Centre in the world (3×s32), the result |
| +0x34 | Radius (also used for the bounding box) |
| +0x38 | Radius² (the hit test) |
| +0x40 | Offset (3×s16) |

Centre, in model space: **(joint[a] + joint[b]) / 2 + R(a) × offset**, R(a) bone a's rotation
(skipped when the offset is 0 or the instance has no pose pointer +0x88); a fixed point is just
the offset. Then into the world by the entity's matrix `+0x340` and position. Exactly, per
component and in this order:

- R(a) from the pose's quaternion {x, y, z, w} (+0x88, 8 bytes per bone), each element its own
  GTE product >> 12: [[(2x² + 2w²) >> 12 - 4096, (2xy + 2zw) >> 12, (2xz - 2yw) >> 12],
  [(2xy - 2zw) >> 12, (2y² + 2w²) >> 12 - 4096, (2yz + 2xw) >> 12], [(2xz + 2yw) >> 12,
  (2yz - 2xw) >> 12, 2·((z² >> 12) + (w² >> 12)) - 4096]] (the transpose of the textbook matrix);
- the turned offset (R(a) · offset) >> 12, stored as s16;
- plus (joint[a] + joint[b]) >> 1 (s16 joints, an arithmetic shift);
- into the world: (M · c) >> 12 + the integer position (`+0x354`), M = `+0x340`, which is
  `RotMatrixYXZ` of the integer parts of the angles `+0x228..+0x230` (PsyQ's, with its own
  roundings; checked equal in all 34352 frames below).

The entity's own sphere (`+0x2E0`, 3×s16, radius `+0x2E8`) is placed on bone `+0x2EA` the same
way (the joint, no offset; `0x8008E354` → `0x8008DABC`), or, when that is -1, at the local point
`+0x2D8` through the same matrix: the dog's (type 0x1405) is -1 with (0, -50, 0), radius 125;
a soldier's is bone 0, radius 150.

**The bounding box is never rebuilt.** `entity_bounds` sets `+0x308..+0x30C` to -0x7FFF and
`+0x30E..+0x312` to 0x7FFF, then `entity_parts_update` keeps the smaller of the box's minimum and
each centre - radius, the larger of the maximum and centre + radius (`0x8004D328..0x8004D3FC`):
the box stays the whole world. Watched: the five slots' boxes only ever held 0 or ±0x7FFF over
the session. A test against it always passes.

Checked every frame of the replay of 1:3, 2:1 and 2:2 (`tools/sim/combat_sim.py check`: the
five character slots' parts, joints, pose and matrix watched; the parts recomputed from the
joints and pose in RAM and the entity's angles and position): 34310 of 34352 frames give every
centre exactly, and the collision sphere 34316. The others: a soldier pushed back in the
collision pass after its parts were placed (2:2, vblanks 53467..53704, 35 frames: parts 2 to 57 units ahead
of where the push left it; the push is inferred from the error being along the soldier's path
while an object 110 units away was touching it), two frames at the 2:2 start and one in 2:1
(37 units, the same kind), and a never-drawn soldier whose part 3 is one unit off for 5 frames
(unexplained). So a reimplementation places the parts after think and move and doesn't move
them for the collision pass's pushes.

The 1:3 soldier's parts (radius): 0 head: bone 13 + 15 along it (20); 1, 2 hands: 21, 17 (20);
3, 4 between collarbone and elbow, 10 to the side: 18-20, 14-16 (20); 5, 6 elbows 20, 16 (20);
7 chest 9-10 + 15 (30); 8 belly 9-8 - 10 (35); 9, 10 thighs 5-6, 2-3 (25); 11, 12 shins 6-7, 3-4
(20); 13, 14 knees 6, 3 (20); 15, 16 ankles 7, 4 (20). All kind 1. Odd parts (1, 3, 5, 9, 11,
13, 15) are on the soldier's right, even ones (2, 4, 6, 10..16) on its left; the full table is
in entities.md.

**Consequences** (from the order: ticks, then collisions, then drawing):
- An enemy on screen is hit where it was drawn the frame before, moved and turned to where it
  is now: the pose lags the animation by one tick (more when frames are dropped: up to 4 ticks
  per frame).
- A frame drawn without a tick (paused, a menu over the level) rebuilds the joints but places
  no parts: they keep the last tick's, a frame behind (3:1 in a monkey run, paused at an
  animation's last frame: every part from frame 19 while the joints show 20).
- An enemy off screen keeps the last pose it was drawn in, carried along with its position and
  heading. Only enemy bullets and explosions can hit it there (the player can't aim at it).
- An enemy never drawn has every joint at 0, so every hit sphere sits at its origin (the feet),
  plus the parts' offsets. Its collision sphere (on bone 0) too.
- The muzzle an enemy shoots from is placed on its gun's bones (the enemy weapon table's
  `+0x8`/`+0xC`, passed to `bullet_spawn` as bones; inferred from the call), so an off-screen
  enemy's shots also start from the frozen pose.

**How often, in a real session** (`tools/recon/ent_offscreen.py` on the replay of the recorded
session, 1:3, 2:1, 2:2 up to vblank 61980; the five character slots' `+0x298`/`+0x29C` watched
each frame with the damage trace; an enemy "shot" is a decrement of its burst counter, weapon
block `+0x13`, or a rifle's bolt animation 21 after 20):

| Event | Drawn (pose 1 tick old) | Frozen (seen before, not drawn) | Never drawn |
|---|---|---|---|
| Player bullets and pellets hitting a soldier or dog | 294 | 2 | 0 |
| Enemy shots | 84 | 9 (1:3) | 1 (2:1) |

The replay runs with the recording's wider view (21:9, 110° wide), which draws more than the
original. With an estimate of the original 65° × 51° view from the camera's position and
heading (the soldier's pelvis sphere against the edges): every hit on an enemy was inside it,
but 15 of the 84 enemy shots counted as drawn came from soldiers outside it. So in the
original about **one enemy shot in four** (25 of 94) is fired by a soldier whose pose is
frozen or never computed, and **hits on enemies with frozen spheres are rare** (2 of 296: a
soldier in front of the player that the renderer had not drawn for about 40 frames, `+0x29C`
being `model_draw`'s result). A reimplementation that always poses every soldier differs
from the original mainly in where off-screen enemies' bullets start.
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

**The seven first-person animations**, named by watching `[[g_soldier+0x100]+0x5C]` (animation
and frame) with the weapon block (`inv+0x04` switch request, `+0x55` weapon, `+0x66..` clips)
and the shot trace over the replay of 1:3, 2:1 and 2:2 (1,100 animation starts), and by the
camera's movement while each played. Every weapon model has the same seven slots (the 1:3
Garand's scripts: `sst.py --type 1`):

| Anim | Shows | Seen | Garand script |
|---|---|---|---|
| 0 | **Moving**: the weapon swaying while the player walks or runs | Camera moving 12.8 units a frame on average while it plays | 7 (loop; on event 64, inferred "started moving") |
| 1 | **Fire**; grenades: the throw; papers (7): showing them | Every shot (and every clip decrement for the shotgun) starts it | 8, 14 |
| 2 | **Raise** (draw the new weapon) | Right after a switch, on the new weapon | 16 |
| 3 | **Lower** (put the weapon away) | After the switch request (`inv+0x04` 0x800), before the weapon index changes | 10, 15 |
| 4 | **Reload**; also the first draw of a weapon whose clip is empty (the clip fills) | A clip increases | 9, 11 |
| 5 | **Standing idle** | Camera moving 0.15 a frame | 0, 1 (the weighted defaults: whenever no other script runs; event 128, inferred "stopped") |
| 6 | **Melee**: the Garand's rifle-butt swing (frames 8..12 with the attack part, kind 5) | Once in the replay (grenade model) | 12, 13 |

Frame counts (all 24 level dumps; every weapon model has all seven):

| Weapon | Model | Bones | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|---|---|
| 0 .45 pistol, 8 pistol, 9 silenced pistol | 0x6FF70F21, 0x334A2AE5, 0x5FE822FD | 14 | 27 | 12 | 13 | 9 | 29 | 6 | 26 |
| 1 Thompson | 0x6138B27C | 32 | 29 | 21 | 15 | 13 | 51 | 6 | 33 |
| 2 Bazooka | 0x1E57C9E7 | 3 | 14 | 28 | 30 | 16 | 29 | 25 | 23 |
| 3 Grenades | 0x194A049F | 24 | 16 | 29 | 20 | 13 | 13 | 11 | 45 |
| 4 Shotgun | 0x9D04B4CF | 27 | 29 | 30 | 23 | 18 | 32 | 29 | 33 |
| 5 Sniper rifle | 0xFF616D23 | 23 | 29 | 43 | 23 | 17 | 33 | 6 | 30 |
| 6 Sten | 0x9C3E508F | 30 | 29 | 6 | 25 | 13 | 51 | 6 | 33 |
| 7 Papers | 0xBB96E6B3 | 15 | 5 | 49 | 5 | 5 | 5 | 5 | 5 |
| 10 Stick grenades | 0xB8A56418 | 22 | 29 | 49 | 22 | 8 | 8 | 6 | 53 |
| 11 BAR, 12 Garand | 0xA0737F88, 0x4F124773 | 28, 23 | 29 | 21 | 23 | 17 | 36 | 6 | 30 |

(Model hashes are `g_weapons[w]+0x00`.) All are played with mode 2: weapon animations cut,
never fade. Their root movement and turn are 0 in the Garand's animations; the root offset (0,
480, -127) places the arms relative to the eye (inferred). The fire animation's length is not
the refire delay (player.md): a shot restarts anim 1 from frame 0 whenever the refire
countdown allows (the Thompson: every 4 ticks into a 21-frame animation).

## The soldier's animations

Every soldier model of a level shares one table of 335 slots, of which a level loads the ones
its scripts may need (183 to 227). The same index is the same motion in every level (same
frame count in all 24 dumps but for a few one-frame differences, e.g. anim 40: 71 or 72).
`tools/recon/ent_anims.py table 0x1402 build/lvl/*.bin` prints them with frame counts, the
number of levels loading each, root movement and turn totals and the 1:3 / 2:1 scripts that
play them. Frames are ticks (30 a second); movement is in model units, +z forward, +x the
soldier's right; turn in 4096 per turn (+ is to the right). "Shows" comes from the scripts
that play them (soldier.md, 1:3 numbering unless said; "s" = script) and from the motion;
what each shows is inferred from those scripts' context (the script numbers are read; the
names are not seen on screen), "?" where nothing plays it.

| Anim | Frames | Move x, z; turn | Levels | Shows |
|---|---|---|---|---|
| 0 | 6 | - | all | Stand, idle (s211 fidgets 56%; s89 path end) |
| 1 | 14 | - | all | Idle fidget (s211) |
| 2 | 13 | -21, -11; -1030 | all | Turn left 90° on the spot (first step of a route, s75; s206, s209) |
| 3 | 25 | -31, 8; 2040 | all | Turn round 180° (s75, s117, s184) |
| 4 | 15 | 11, -12; 655 | all | Turn right ~60° (s75, s206, s209) |
| 5 | 41 | 0, 237 | all | Slow walk? (2:1 papers scripts 271/281) |
| 6 | 11 | 0, 50 | all | Patrol walk, slow (s87/88: paths outside 100..200) |
| 7 | 19 | 0, 467 | all | Walk cycle (patrols 100..200, walking at the player: s180, s187, s188) |
| 8 | 16 | 0, 105 | all | Step forward (s142) |
| 9 | 24 | 0, -160 | all | Step back (s141) |
| 10 | 18 | 151, 0 | all | Strafe right firing (s144, fire 4) |
| 11 | 17 | -159, 26 | all | Strafe left firing (s143, fire 4) |
| 12 | 21 | 106, 0 | all | Strafe right (s140) |
| 13 | 22 | -110, 0 | all | Strafe left (s139) |
| 14 | 18 | 0, 537 | all | Run cycle (to cover s110, s119, s214) |
| 15 | 18 | 0, -224 | all | Back off a step, facing (s136, s149, s186...) |
| 16 | 9 | - | all | End of the right strafe-fire (s144) |
| 17 | 13 | - | all | End of the left strafe-fire (s143) |
| 18 | 44 | -25, -23 | 21 | Rifle: raise to aim (s23, s38 before the bolt sequence) |
| 19 | 21 | - | 21 | ? (loaded with the rifle anims, played by no script read) |
| 20 | 16 | - | 21 | Rifle: aim and fire (fire 2 during it; s40/41) |
| 21 | 37 | - | 21 | Rifle: work the bolt (s40/41, s39 reload) |
| 22 | 27 | 0, -105 | 21 | Rifle step back (s145) |
| 23 | 31 | 136, 0 | 21 | Rifle step right (s146; s79) |
| 24 | 31 | -105, 0 | 21 | Rifle step left (s147) |
| 25 | 17 | -8, 37 | all | SMG: bring the gun up (s38) |
| 26 | 4 | - | all | SMG: hold aim, not firing (s40/41: fire decision no; s39) |
| 27 | 4 | - | all | SMG: fire burst loop (fire 1/3 during it; s40/41, s22, s206) |
| 28 | 27 | - | all | SMG reload (s39) |
| 29 | 22 | - | all | Head hit (s7) |
| 30 | 22 | - | all | Flinch 1 (s6, player bullet above 75%) |
| 31 | 22 | - | all | Flinch 2 (s6; s35 hit holding a grenade) |
| 32 | 22 | - | all | Flinch 3 (s6) |
| 33 | 19 | 15, -39 | all | Flinch 4 / torso hit (s6, s171, s173) |
| 34 | 20 | - | all | Hit reaction a (s34) |
| 35 | 20 | - | all | Hit reaction b (s34) |
| 36 | 5 | - | all | Short flinch 1 (s6: hit by other than the player's bullet) |
| 37 | 5 | - | all | Short flinch 2 (s6) |
| 38 | 5 | - | all | Short flinch 3 (s6) |
| 39 | 5 | - | all | Short flinch 4 (s6) |
| 40 | 71/72 | -21, 0 | all | Long head-clutch (s7) |
| 41 | 75/76 | -30, 0 | all | Right arm hit (s169) |
| 42 | 91/92 | -12, -150 | all | Left arm hit (s8) |
| 43 | 103 | -8, -22 | all | Right leg hit (s167) |
| 44 | 65 | -35, -108 | all | Left leg hit (s166) |
| 45 | 143 | -17, 34 | all | Groin hit, long fall (s173 → down) |
| 46 | 32 | 0, 19 | all | Knocked down (s167, s171 → down) |
| 47 | 15 | - | all | Lying down (s10, 1..5 plays) |
| 48 | 45 | 0, -1 | all | Get up 1 (s10) |
| 49 | 15 | - | all | Fire from the ground (s10, SMG/MG) |
| 50 | 24 | - | all | Fire from the ground (s10, rifle) |
| 51 | 66 | -1, -17 | all | Get up 2 (s10) |
| 52 | 43 | 35, -259 | all | Death: falls back (s190 head, s194, s3 from behind) |
| 53 | 104 | -320, -74 | all | Death: long fall, spraying (fire 3 for frames 0..97; s190, s194) |
| 54 | 43 | 0, -177 | all | Death: leg (even), s190, s194 |
| 55 | 63 | 0, -534 | all | Death (s190 roll, s191 rocket torso) |
| 56 | 46 | -160, -118 | all | Death: leg (odd), s190 |
| 57 | 53 | 0, -618 | all | Death: thrown back (s190 head/default, s191) |
| 58 | 67/73 | -1, -317 | all | Death (s190 other parts 30%) |
| 59 | 30 | - | all | Kneel down (s148) |
| 60 | 5 | - | all | Kneeling: aim / fire (s156) |
| 61 | 41 | -3, 8 | all | Stand up from kneeling / cover (s104, s106, s114, s149, s150) |
| 62 | 36 | 130, 0 | all | Kneeling shuffle right (s152, s154) |
| 63 | 36 | -124, 0 | all | Kneeling shuffle left (s152, s155) |
| 64 | 16 | - | all | Kneeling rifle shot (s156, fire 2) |
| 65 | 88 | 0, -410 | all | Torso hit, long stagger (s171) |
| 66 | 66 | -1, -186 | all | Hit, staggers back (s172, s174) |
| 67 | 18 | - | all | Kneeling hit (s152) |
| 68 | 20 | -205, 142 | all | Death from kneeling / crouch (s115, s153, s198) |
| 69 | 61 | - | all | ? (no script read) |
| 70 | 65 | 51, -166 | all | Death by rocket, legs (s191) |
| 71 | 67 | - | all | ? (no script read) |
| 72 | 41 | - | all | ? (no script read) |
| 73 | 14 | - | all | Thrown by an explosion, forward (s120) |
| 74 | 10 | 0, -41 | all | Landing after a throw, part 1 (s121) |
| 75 | 6 | - | all | Landing, part 2 (s121) |
| 76 | 28 | 0, 209 | all | Lying after a throw (s121, s125) |
| 77 | 16 | - | all | Thrown by an explosion, backward (s123) |
| 78 | 10 | - | all | Landing on the back, part 1 (s124) |
| 79 | 6 | - | all | Landing on the back, part 2 (s124) |
| 80 | 28 | 79, -207 | all | Lying after a backward throw (s122, s124) |
| 81 | 25 | 0, -165 | all | Rolls over and dies (s122) |
| 82 | 5 | - | 14 | Cover: into hiding (s111) |
| 83 | 16 | -27, 0 | 14 | Cover side 1: lean out (s111) |
| 84 | 16 | - | 14 | Cover side 1: lean, firing (s111, fire 4) |
| 85 | 11 | - | 14 | Cover hit, side 1 (s102) |
| 86 | 11 | 27, 0 | 14 | Cover side 1: back in (s102, s111) |
| 87 | 23 | - | 14 | Cover side 2: lean out (s111) |
| 88 | 16 | - | 14 | Cover side 2: lean, firing (s111) |
| 89 | 10 | - | 14 | Cover hit, side 2 (s109) |
| 90 | 15 | - | 14 | Cover side 2: back in (s109, s111) |
| 91 | 48 | -242, -28 | 14 | Death at cover (s102) |
| 92 | 31 | -20, 0 | 13 | ? (no script read) |
| 93 | 50 | 16, -210 | all | Falls down, hurt (s11, s128, s161, s162, s168, s174) |
| 94 | 76 | - | all | Prone: hit (s162) |
| 95 | 50 | - | all | Prone: hit (s162) |
| 96 | 20 | -57, -126; -1938 | all | Prone: turn round (s10) |
| 97 | 19 | - | all | Prone: lie (s126, s127) |
| 98 | 39 | 1, 138 | all | Crawl forward (s130) |
| 99 | 43 | 1, -97 | all | Prone: aim (s126) |
| 100 | 46 | -24, -104; -1980 | all | Prone: turn round (s130, s131) |
| 101 | 67 | -17, 102 | all | Prone: get up (s127) |
| 102 | 41 | 44, -346 | all | Prone: death (s163) |
| 103 | 22 | -17, 152 | all | Crawling: death (s133) |
| 104 | 43 | 0, -99 | all | Prone: aim, part 2 (s126) |
| 105 | 43 | 0, -99 | all | Prone: fire (s126) |
| 106 | 43 | 0, -97 | all | Prone: after firing (s126) |
| 107 | 28 | 0, -252 | all | Down: death (s129) |
| 108 | 37 | 0, -328 | all | Down: hit, head (s128) |
| 109 | 80 | - | all | Down: hit, long (s128) |
| 110 | 28 | - | all | Down: hit (s128) |
| 111 | 105 | - | all | Down: hit, longest (s128) |
| 112 | 8 | 0, 36 | all | Crawling: hit by the player's bullet (s132) |
| 113 | 26 | 33, -7 | all | Kneel to aim, rifle/Panzerfaust (s157, s164) |
| 114 | 13 | - | all | Kneeling: fire (s160, s164) |
| 115 | 40 | - | all | Kneeling: bolt (s160) |
| 116 | 40 | -32, 6 | all | Kneeling: stand up (s160) |
| 117 | 27 | - | all | Kneeling: hit (s161) |
| 118 | 5 | - | all | Down: short hit 1 (s162) |
| 119 | 5 | - | all | Down: short hit 2 (s162) |
| 120 | 5 | - | all | Down: short hit 3 (s162) |
| 121 | 5 | - | all | Down: short hit 4 (s162) |
| 122 | 18 | 22, 305 | all | Hit while running (s81, s82, s177) |
| 124 | 17 | -159, 26 | all | Hit while strafing left (s137) |
| 125 | 18 | 151, 0 | all | Hit while strafing right (s138) |
| 126 | 6 | - | all | Down: twitch 1 (s128) |
| 127 | 6 | - | all | Down: twitch 2 (s128) |
| 128 | 6 | - | all | Down: twitch 3 (s128) |
| 129 | 6 | - | all | Down: twitch 4 (s128) |
| 130 | 6 | - | all | Crawling: twitch 1 (s132) |
| 131 | 5 | - | all | Crawling: twitch 2 (s132) |
| 132 | 5 | - | all | Crawling: twitch 3 (s132) |
| 133 | 5 | - | all | Crawling: twitch 4 (s132) |
| 134 | 23/26 | - | 2:3 4:1 | Level-specific scripts (2:3 s208/211, 4:1 s199/202) |
| 135 | 4/16 | - | 2:3 4:1 | Level-specific scripts (2:3 s213/214, 4:1 s204/205) |
| 136 | 29/46 | - | 2:3 4:1 | Level-specific scripts (2:3 s208, 4:1 s199) |
| 137 | 46/81 | - | 2:3 4:1 | Level-specific scripts (2:3 s208, 4:1 s199) |
| 138 | 6/39 | - | 2:3 4:1 | Level-specific scripts (2:3 s208, 4:1 s199) |
| 139 | 16 | - | 2:3 4:1 | Level-specific scripts (2:3 s215, 4:1 s206) |
| 140 | 15/16 | - | 2:3 4:1 | Level-specific scripts (2:3 s216, 4:1 s207) |
| 141 | 13 | - | 2:3 4:1 | ? (played by no soldier script of the 24 levels) |
| 142 | 13/14 | - | 2:3 4:1 | Level-specific scripts (2:3 s216, 4:1 s207) |
| 143 | 15/16 | - | 2:3 4:1 | Level-specific scripts (2:3 s217, 4:1 s208) |
| 144 | 13 | - | 2:3 4:1 | ? (played by no soldier script of the 24 levels) |
| 145 | 13/14 | - | 2:3 4:1 | Level-specific scripts (2:3 s217, 4:1 s208) |
| 146 | 16/17 | - | 2:3 4:1 | Level-specific scripts (2:3 s213/214, 4:1 s204/205) |
| 147 | 13/22 | - | 2:3 4:1 | Level-specific scripts (2:3 s208/211, 4:1 s199/202) |
| 148 | 16 | - | 11 | Level-specific scripts (1:1 s216, 1:2 s217) |
| 149 | 13 | - | 11 | Level-specific scripts (1:1 s216, 1:2 s217) |
| 150 | 16 | - | 11 | Level-specific scripts (1:1 s216, 1:2 s217) |
| 151 | 11 | - | 11 | Level-specific scripts (1:1 s216, 1:2 s217) |
| 152 | 27 | - | 11 | Level-specific scripts (1:1 s214, 1:2 s215) |
| 153 | 16 | - | 11 | Level-specific scripts (1:1 s215, 1:2 s216) |
| 154 | 60 | - | 11 | Level-specific scripts (1:1 s216, 1:2 s217) |
| 155 | 41/50 | 0, 236 | 5 | Sentry / unreferenced death: falls (1:3 s198, 2:1 s207) |
| 156 | 31 | - | 5 | Writhing on the ground (s199, 2:1 s212) |
| 161 | 19 | -39, 65 | 23 | Patrol look (s85) |
| 162 | 20 | -15, 107 | 23 | Patrol stop and look (s85, s86) |
| 163 | 42 | 0, 246 | all | Search: look round walking (s206) |
| 164 | 93 | 0, 476 | all | Patrol walk variant (s85) |
| 165 | 93 | 0, 481 | 23 | Patrol walk variant (s85) |
| 166 | 29 | - | all | Look left (s22, s85, s86, s206) |
| 167 | 33 | - | all | Look right (s22, s85, s86, s206) |
| 168 | 5 | - | all | Look ahead (s22, s85, s86) |
| 169 | 41 | -29, -54; -2030 | 19 | Turn round 180° (2:1 s260) |
| 170 | 13 | - | 23 | ? (no script read) |
| 171 | 16 | - | 23 | ? (no script read) |
| 172 | 51 | -127, 8 | 14 | Cover hit, falls (s102) |
| 173 | 26 | 135, 0 | 13 | Cover: flinch (s102, s105, s107) |
| 176 | 13 | - | 13 | ? (no script read) |
| 177 | 13 | - | 13 | ? (no script read) |
| 178 | 51 | - | 7:1 7:3 | ? (played by no soldier script of the 24 levels) |
| 179 | 23 | 4, 161 | 5:1 | Level-specific scripts (5:1 s212) |
| 180 | 26 | 0, 133 | 5:1 | Level-specific scripts (5:1 s212/216) |
| 181 | 30 | - | 5:1 | ? (played by no soldier script of the 24 levels) |
| 182 | 12 | - | 5:1 | Level-specific scripts (5:1 s219) |
| 183 | 6 | - | 5:1 | Level-specific scripts (5:1 s219) |
| 184 | 7 | - | 5:1 | Level-specific scripts (5:1 s219) |
| 185 | 8 | - | 5:1 | Level-specific scripts (5:1 s219) |
| 202 | 48 | 244, 38 | 20 | Death at cover, side 2 (s108, s109) |
| 203 | 24 | - | 5 | ? (2:1 s253: sentry) |
| 204 | 16 | - | all | Alert: point / wave (s39, s209, s215) |
| 205 | 13 | - | all | Alert: look round (s39, s209, s215) |
| 207 | 16 | - | all | Idle fidget (s211) |
| 208 | 12 | - | all | Idle fidget (s211) |
| 209 | 13 | - | all | Idle fidget (s211) |
| 210 | 17 | - | all | Idle fidget (s211) |
| 211 | 12 | - | all | Idle fidget (s211) |
| 212 | 56 | - | all | Idle fidget, long (s211, 5%) |
| 213 | 38 | 0, 228 | all | Run onto a grenade (s114) |
| 214 | 21 | - | all | Lying on a grenade (s114, ×11) |
| 215 | 14 | -6, 0 | all | Startled: weapon up (s209 alert, s23, s206, s207...) |
| 216 | 14 | - | all | Rifle ready pose (s216) |
| 217 | 40 | 0, 18 | 23 | Kick a grenade away (s117) |
| 218 | 35 | - | 23 | Pick up a grenade (s117, s118) |
| 219 | 15 | - | 23 | Grenade: pull the pin (s35, s37, s118) |
| 220 | 54 | - | 23 | Grenade: throw (release at frame 21; s37, s117, s118) |
| 221 | 66 | - | 2:1 2:2 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s232/233, 2:2 s237/238) |
| 222 | 19 | -16, 65 | all | Search: look round 1 (s206) |
| 223 | 21 | -7, 112 | all | Search: look round 2 (s206) |
| 224 | 6 | - | 2:1 2:2 2:3 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s258/260, 2:2 s263/265) |
| 225 | 33 | - | 2:1 2:2 2:3 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s228/258, 2:2 s233/263) |
| 226 | 20 | - | 6 | Level-specific scripts (2:1 papers and suspicion, 2:1 s228/258, 2:2 s233/263) |
| 227 | 36 | - | 2:1 2:2 2:3 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s228/258, 2:2 s233/263) |
| 228 | 18 | 0, -224 | 7 | ? (played by no soldier script of the 24 levels) |
| 229 | 51 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s209/210, 5:2 s212/213) |
| 230 | 41 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s209/210, 5:2 s212/213) |
| 231 | 35 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s209/210, 5:2 s212/213) |
| 232 | 56 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s209/210, 5:2 s212/213) |
| 233 | 40 | 0, 0; 1022 | 5:1 5:2 5:3 | Level-specific scripts (5:1 s209, 5:2 s212) |
| 235 | 45 | 0, 0; -997 | 5:1 5:2 5:3 | Level-specific scripts (5:1 s210, 5:2 s213) |
| 237 | 30 | 0, -22 | 8 | Level-specific scripts (3:1 s301, 3:4 s207) |
| 238 | 25 | -28, -1 | 5:1 5:2 5:3 | Crouch (s25, bit-3 soldiers) |
| 239 | 15 | - | 7 | Crouched hold (s25) |
| 240 | 26 | - | 1:2 7:1 | Level-specific scripts (2:1 sentry, 1:2 s213, 2:1 s213) |
| 241 | 15 | - | 1:2 7:1 | Level-specific scripts (2:1 sentry, 1:2 s213, 2:1 s213) |
| 242 | 36 | - | 1:2 7:1 | Level-specific scripts (2:1 sentry, 1:2 s213, 2:1 s213) |
| 243 | 6 | - | 6 | Stand, scripted idle 1 (s212) |
| 244 | 28 | - | 6 | Scripted idle 2 (s212) |
| 245 | 39 | - | 6 | Scripted idle 3 (s212) |
| 246 | 22 | 0, 43 | 10 | MG: bring up (s23, s38) |
| 247 | 6 | - | 10 | MG: aim (s40/41, s39) |
| 248 | 11 | - | 10 | MG: fire burst (fire 1/3) |
| 249 | 14 | - | 10 | MG: hit (s31) |
| 250 | 31 | 0, -159 | 10 | MG step back (s145) |
| 251 | 24 | -75, 0 | 10 | MG step left (s147) |
| 252 | 24 | 74, 1 | 10 | MG step right (s146) |
| 253 | 30 | 0, 229 | all | Patrol look round 1 (s88) |
| 254 | 30 | 0, 229 | all | Patrol look round 2 (s88) |
| 255 | 37 | 0, 253 | all | Patrol walk, alternative (s88) |
| 256 | 13 | - | 2:1 2:2 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s273/290, 2:2 s278/295) |
| 257 | 5 | - | 2:1 2:2 2:3 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s273/290, 2:2 s278/295) |
| 258 | 10 | - | 2:1 2:2 2:3 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s272/273, 2:2 s277/278) |
| 259 | 14 | - | 2:1 2:2 2:3 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s272/273, 2:2 s277/278) |
| 260 | 49 | - | all | Look round, long (s22, s206, s209) |
| 261 | 45 | - | 15 | Grenade: face the player and wind up (s35) |
| 262 | 12 | -19, -21; -2042 | 22 | Turn round 180°? (7:1/7:3 scripts) |
| 263 | 29 | - | 9 | Throw from cover, side 1 (s98) |
| 264 | 23 | - | 9 | Throw from cover, side 2 (s100) |
| 265 | 39 | 19, -59 | 22 | Rifle-butt strike (s136, s184; attack part) |
| 266 | 17 | 0, -211 | 5 | Panzerfaust: back off (s186) |
| 267 | 29 | 29, -10 | 5 | Panzerfaust: kneel (s158) |
| 268 | 25 | -8, 42 | 5 | Panzerfaust: bring up (s38) |
| 269 | 40 | - | 5 | Panzerfaust: kneeling reload (s164) |
| 270 | 30 | -28, 10 | 5 | Panzerfaust: stand up (s159) |
| 271 | 6 | - | 5 | Panzerfaust: aim (s41) |
| 272 | 15 | - | 5 | Panzerfaust: fire (s41) |
| 273 | 51 | - | 5 | Panzerfaust: reload (s39, s41) |
| 274 | 5 | - | 6 | Level-specific scripts (1:1 s121, 1:2 s116) |
| 275 | 12 | - | 6 | Level-specific scripts (1:1 s121, 1:2 s116) |
| 276 | 6 | - | 6 | ? (played by no soldier script of the 24 levels) |
| 277 | 7 | - | 6 | Level-specific scripts (1:1 s117/118, 1:2 s112/113) |
| 278 | 11 | - | 6 | Level-specific scripts (1:1 s117/118, 1:2 s112/113) |
| 279 | 16 | - | 6 | ? (played by no soldier script of the 24 levels) |
| 280 | 7 | - | 6 | Level-specific scripts (1:1 s121, 1:2 s116) |
| 281 | 7 | - | 6 | Level-specific scripts (1:1 s116/121, 1:2 s111/116) |
| 282 | 13 | - | 6 | Level-specific scripts (1:1 s116/121, 1:2 s111/116) |
| 283 | 11 | - | 5 | Raise the alarm (2:1 s195) |
| 284 | 7 | - | 5 | Alarm: look (2:1 s195) |
| 285 | 41 | - | 16 | Level-specific scripts (7:1 s213/216, 7:3 s212/215) |
| 286 | 16 | 0, 36 | 7:1 7:3 | Level-specific scripts (7:1 s213, 7:3 s212) |
| 287 | 19 | - | 7:1 7:3 | Level-specific scripts (7:1 s213, 7:3 s212) |
| 288 | 25 | - | 7:1 7:3 | Level-specific scripts (7:1 s213, 7:3 s212) |
| 289 | 12 | - | 7:1 7:3 | Level-specific scripts (7:1 s213, 7:3 s212) |
| 290 | 22 | 0, 481 | 7:1 7:3 | Level-specific scripts (7:1 s213, 7:3 s212) |
| 291 | 17 | 0, 276 | 18 | Level-specific scripts (7:1 s215, 7:3 s214) |
| 292 | 31 | - | 3:4 7:1 7:3 | Level-specific scripts (7:1 s213/216, 7:3 s212/215) |
| 293 | 51 | - | 3:4 4:2 7:1 7:3 | Level-specific scripts (7:1 s213/216, 7:3 s212/215) |
| 294 | 53 | 0, -518 | all | Death by rocket, legs (s191) |
| 295 | 46 | 162, -616 | 21 | Death: thrown back hard (shotgun s193, rocket s191) |
| 296 | 73 | 0, 122 | all | Death by pistol (s194) |
| 297 | 32/72 | 93, -291 | all | Death (s190 roll) |
| 298 | 31 | 12, 538 | all | Explosion: thrown, forward roll (s123) |
| 299 | 41 | - | 18 | Rocket: tumble 1 (s191, s192) |
| 300 | 49 | 0, 344 | 18 | Rocket: tumble 2 (s191, s192) |
| 301 | 52 | -297, 0 | 18 | Rocket: tumble 3 (s191, s192) |
| 302 | 49 | -171, -229 | 7 | Rocket death from behind (s192) |
| 303 | 30 | -63, 0 | 5 | Crouch: duck (s25) |
| 304 | 45 | -11, -28; -2046 | all | Look round at a path end (s89) |
| 305 | 12 | 0, 53 | all | Stop walking (s89) |
| 306 | 25 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s210, 5:2 s213) |
| 307 | 30 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s210, 5:2 s213) |
| 308 | 30 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s210, 5:2 s213) |
| 309 | 51 | - | 5:1 5:2 5:3 | Level-specific scripts (5:1 s210, 5:2 s213) |
| 310 | 5/30 | - | 3:1 3:4 4:2 7:2 | Scripted idle loop (s213) |
| 311 | 36/50 | 4, -48; -1977 | 3:1 3:4 4:2 7:2 | Level-specific scripts (3:1 s301, 3:4 s207) |
| 312 | 18 | - | 3:1 3:4 4:2 7:2 | Level-specific scripts (3:1 s302, 3:4 s208) |
| 313 | 56 | - | 5:1 | Level-specific scripts (5:1 s219) |
| 314 | 11 | - | 22 | MG ready pose (s136, s216) |
| 315 | 12 | - | all | SMG ready pose (s216) |
| 316 | 12 | - | all | Weapon-ready, alone (s215) |
| 317 | 6 | - | 2:1 2:2 3:1 9:4 | Level-specific scripts (2:1 papers and suspicion, 2:1 s270, 2:2 s275) |
| 318 | 18 | - | 2:1 2:2 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s228/291, 2:2 s233/296) |
| 319 | 6 | - | 2:1 2:2 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s228, 2:2 s233) |
| 320 | 25 | - | 2:1 2:2 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s228, 2:2 s233) |
| 321 | 25 | - | 2:1 2:2 3:1 | Level-specific scripts (2:1 papers and suspicion, 2:1 s269, 2:2 s274) |
| 322 | 5 | - | 15 | Cover: flinch side 2 (s108) |
| 323 | 25 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 324 | 17 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 325 | 25 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 326 | 18 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 327 | 13 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 328 | 16 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 329 | 31 | 0, -67 | 9:4 | Level-specific scripts (9:4 s225) |
| 330 | 19 | 0, 467 | 9:4 | Level-specific scripts (9:4 s225) |
| 331 | 11 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 332 | 22 | -2, 216 | 9:4 | Level-specific scripts (9:4 s225) |
| 333 | 21 | - | 9:4 | Level-specific scripts (9:4 s225) |
| 334 | 19 | 0, 467 | 9:4 | Level-specific scripts (9:4 s225) |

Notes for a reimplementation:
- A script can name an animation its level did not load (script 25's anim 238/239/303 are in
  every level's script but loaded only in 5:1..5:3 and a few others): op 00 then stops the game
  with a fatal error (`0x80089B04(1, 0x100)`), so those branches are never taken where the
  animation is missing (they depend on flags the level sets), and `has_anim` (op 0x45) exists
  to test it.
- Anims 134..154, 178..185, 221..237, 256..259, 274..293, 306..313, 317..334 belong to
  level-specific scripts (sentries, papers, 9:4's scripted scene...): numbered here for
  completeness, read from the scripts of the levels named only where given.

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
- entities.md: the bounding box `+0x308..+0x312` is not rebuilt from the bones each tick: it
  is always ±0x7FFF (Hit spheres, above). And the entity's sphere sits on bone `+0x2EA` or, at
  -1 (dogs), on the local point `+0x2D8`.

## Open

- The names in the soldier table are read from script context, not seen on screen; a viewer
  (the GPL viewer plays them) would confirm them, and name the "?" rows (19, 69, 71, 72, 92,
  170, 171, 176, 177 and the level-specific ones never played).
- The enemy muzzle's bones (enemy weapon table `+0x8`/`+0xC`), read from the call only.
- Why `model_draw` returned 0 for a soldier in front of the player for ~40 frames in 1:3
  (the two hits on frozen spheres above): the renderer's own culling, not traced.

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
8008E9DC entity_in_view             # cube around the collision sphere through box_in_view
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

## New names (2)

```
8008E5B0 box_in_view                # (min, max): box corners against the 6 view edges with the GTE; far = view +0x40
8009C828 g_view_right_edge          # 540: entity_in_view's right screen limit (28 past the 512-wide screen)
8009C82C g_view_bottom_edge         # 240
8006D930 handle_alloc               # (out, entity): (serial << 16) | slot into g_handles; entity +0x0 = handle
8006D8AC handle_dead                # (&handle): 0, stale serial, or entity word 0xDEDEDEDE (never written)
8006D7D0 handle_same                # (&handle, &entity)
8006DA50 handle_mark_removed        # removed flag for the entity's slot (0x8009DE78)
8006D888 handle_make                # (_, slot, serial) -> serial << 16 | slot
8009DA78 g_handle_serials           # [256]
8009DE78 g_handle_removed           # [256]
8009D670 g_handle_serial            # serial counter
8009D674 g_handle_count             # slots in use (high-water mark)
8006B5B8 entity_create              # (flags, object size, model hash, ..., model): one block, handle, defaults, instance
8006B350 entity_defaults
8008EEF0 char_alloc                 # one of 5 character slots (0x2814 bytes at 0x800B7B08), types 0x1402/0x1405
8008EE60 char_release               # (slot, frames): freed after that many frames
8008EAC8 char_pool_update           # per frame: release countdowns of both character pools
800B7B08 g_char_pool                # 5 x 0x2814: soldiers and dogs live here
800C4378 g_char_pose_pool           # 5 x 0x1308: their pose buffers
8006A3BC offscreen_keep             # (e, camera): within the far distance; cap rule: behind and 3501+ away -> despawn now
8005E760 draw_in_view_counter       # a draw handler: entity_in_view, else countdown
80056290 enemy_destroy              # soldier +0x384: count, mounted gun, brain, slot release
800702E0 impact_effect_spawn        # type 0x23: up to 8 particles at a hit point
800701DC impact_effect_think        # life countdown, colours halved over the last 3 ticks
8006F7BC impact_effect_draw
```
