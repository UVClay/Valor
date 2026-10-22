# The player: movement and camera

How the controls become motion and the view, as formulas a new engine can use. Addresses are
for Medal of Honor (USA) with LEVEL.BIN loaded; names in `names.txt` (new ones at the end).

Read from `player_update` (`0x8007A6B0`, the soldier's think), `player_controls`
(`0x8007F300`), `entity_move` (`0x8006C83C`) and the camera's think (`0x800473DC`); checked with
two scripted runs in 1:1 (`--warp 1:1`, the controls below, `VALOR_WATCH` on the fields named
here; `tools/recon/move_trace.py LOG` turns such a log into a per-tick table) and RAM dumps of
the real 1:3 session (`build/re/mid_14000.bin`; `tools/recon/move_cfg.py RAM` prints the
control config and movement state). "Measured" means seen in those traces; "code" means read
only; "inferred" is a guess from the code.

Units: world units, y pointing **down** (up is negative); positions and velocities 16.16; one
tick is 1/30 s; angles 16.16 with 4096 per turn (1 unit = 0.088°).

## Summary for a reimplementation

```
every tick (30 Hz):
  forward = role(FORWARD)           # 0 .. ±34.86 (stick) / ±35.0 (d-pad); ÷4 when fully crouched
  strafe  = role(STRAFE)            # same scale, positive = right
  yaw    += role(TURN)              # up to ±60 units/tick (5.27°), no smoothing
  pitch   = clamp(pitch - role(PITCH)/2, ±682)        # ±59.9°
  if jump pressed and on ground and standing: vy = -26
  if not on ground: vy = min(vy + 2, 75)
  move = rotate_y(yaw) · (strafe, vy, forward)       # set from the input every tick: no
  pos += move                                         # acceleration, no friction, full air control
  walls: only if the whole-unit position changed; push out horizontally (levels.md, Walkers);
         still in a wall after the pushes → the whole move is undone this tick
  floor: on ground and floor within 32 below/any distance above (found up to 32 above) → snap
         airborne and moving down and floor within min(dy+16, 64) below → land, vy = 0
         no floor under the new position → nothing: keep it, height unchanged
camera: eye = feet + 240 (80 crouched, eased 20/tick), at last tick's position; no bob
```

The fast feel comes from this: full speed on the first or second tick of input, a dead stop
the tick it's released, unchanged speed in the air and on slopes, √2 faster on diagonals, steps
taken instantly, and turning that's added straight to the yaw.

## Order of a tick

`entities_update` runs the camera entity (first in the list) before the soldier, so **the view
shows the soldier's position and angles from the end of the previous tick** (measured: the
camera's position each tick equals the soldier's one tick earlier). For the soldier
(`player_update`, then the shared entity steps, entities.md):

1. Controller mode changed (analog on/off, `inv+0x153`): reload the control config
   (`player_pad_config`, below).
2. Below y = +5000 ("Player has fallen too far... Suicide time."): 10000 damage.
3. Dying (contact flag 0x4000, set by `apply_damage`): start the death (below) and skip to 21.
4. Aim mode: `inv` flag 0x20 while action 8 (R2) is held **and on the ground**; the sniper
   rifle (weapon 5) adds the scope (0x100, 0x40) and zoom target 2447; with the option flag
   0x400000 and a weapon whose `g_weapons[w]+0x28` is set, mode 0x200000 with zoom target 475.
5. Stance `inv+0x1C8` from aim mode and weapon (`player_stance`): aiming 0 (pistols, shotgun,
   Garand), 1 (SMGs, weapon 11), 2 (sniper), 5 (bazooka), 6 (grenades, papers); not aiming 7
   (pistols, shotgun, sniper, Garand), 8 (SMGs), 9 (bazooka, grenades, papers).
6. Weapon switch (action 2), slot changes from the scripts, empty grenade slots.
7. Use (action 5), below.
8. Recoil kick update (below), fire requests.
9. Jump.
10. Crouch toggle (action 3) and the eye height easing.
11. On a ladder while crouched: stand up.
12. `player_controls`: movement roles → velocity, look roles → angles, then `aim_update`
    (combat.md) and the view's pitch target.
13. Gravity on (flags A 0x20) unless on a ladder.
14. Moving or not → script variable 2 (the walk animation).
15. Automatic reload of an empty clip.
16. Hit flinch (below).
17. Zoom.
18. The soldier's model scripts (animation).
19. Ladder latch: leaving a ladder zeroes the vertical speed.
20. `inv+0x8C` = on ground.
21. `entity_move`: angles += angular velocity, gravity, the move, walls, the floor; then the
    player's `world_contact` (`0x8007B4B8`: on landing all velocity = 0) and `zones_update`
    (levels.md); then `player_weapon_update` and the bounds.

## Control roles → values

Four roles per port: 0 forward, 1 strafe, 2 pitch, 3 turn. Each tick a role's value comes from
its digital buttons (`role_button`, `0x80075A30`); only if that gives 0 and the pad is in
analog mode, from its stick axis (`role_axis`, RECON.md's curve). So the d-pad wins over a
stick.

**Digital** (config `0x800B75F0 + port·0xD0 + role·0x34`): +0x00/+0x04 the negative/positive
button masks, +0x08/+0x0C a modifier mask and whether it must be up or down, +0x1C the
maximum, +0x24 the output, +0x28 the acceleration, +0x2C the current ramp, +0x30 flags.

```
held one way:  ramp = min(ramp + accel, max);  out = clamp(out ± ramp, -max, max)
released:      flags&6 == 2 → out = 0 (both config dumps);  flags&0x18 == 0 → ramp = 0
```

| Role | Max | Accel | Output on successive ticks held | Released |
|---|---|---|---|---|
| Forward, strafe | 35.0 | 32.0 | 32, 35, 35, ... (measured) | 0 at once (measured) |
| Pitch, turn | 60.0 | 2.0 | 2, 6, 12, 20, 30, 42, 56, 60 (measured on turn) | 0 at once |

**Analog**: `table[idx] · (speed >> 8)` (RECON.md): forward/strafe speed 35.0 → at most
255/256 · 35 = **34.863**; pitch/turn 60.0 → **59.77**. Measured both.

The runtime's keyboard movement (`--kbm`, `hook_role_axis`) adds ±34.863 to the stick path,
the same as a full stick.

Signs: forward role negative = forward (`+0x200 = -role`); strafe positive = right; turn
positive = yaw +; pitch negative (stick up) = look up.

Configs seen: forward d-pad up/down; on a fresh card strafe L1/R1 and turn d-pad left/right,
sticks ly forward, rx strafe, lx turn, ry pitch; the 1:3 session's config had the d-pad's
left/right on both strafe and turn (one of them behind a modifier) and lx strafe, rx turn.
`move_cfg.py` prints a dump's.

## Moving

`player_controls`, not aiming:

```
soldier+0x200 (forward speed) = -role(0)
soldier+0x218 (strafe speed)  =  role(1)
```

`entity_move` copies +0x200 into the local velocity's z (`+0x220`; `+0x1FC`, the acceleration
that would ramp it toward `+0x204` = 36.0, is 0 for the player). The local velocity (strafe,
vertical, forward) is cut to 1/16 unit (`>> 12`), turned by the yaw `inv+0x184` (a rotation
matrix of (0, yaw, `+0x230`)) and added to the position, plus any push (`+0x1F0`). The roll
`+0x230` is normally 0; when something tilts the body (4:2 in a monkey run: a roll of -1 unit
for a stretch, with a pitch drift, while the body's own yaw `+0x22C` lagged the view's by 34),
the full `RotMatrixYXZ(0, yaw, roll)` is used and the move shifts by 1/16 unit a tick in places:
modelled, every tick of that run exact.

- **No acceleration or friction**: the speed is the input's every tick (measured: from 0 to
  34.863 on the first tick with the stick, 32 then 35 with the d-pad, to 0 the tick after
  release).
- **Diagonals add as vectors**: forward + strafe at full = 49.30 per tick (measured 49.28).
- **Slopes don't slow**: walking up a slope that rose 6–7 units per tick kept 34.80
  horizontally (measured).
- **Air control is total** (code): the same assignment runs in the air; a running jump kept
  34.80 horizontally (measured).

| | Per tick | Per second | |
|---|---|---|---|
| Run, stick or keyboard | 34.863 | 1046 | measured |
| Run, d-pad | 35.0 (32.0 first tick) | 1050 | measured |
| Run diagonally | 49.30 | 1479 | measured |
| Crouched, stick | 8.716 | 261 | measured (player.md's 7.9 was not full deflection or not settled) |
| Crouched, d-pad | 8.75 | 263 | code |
| Ladder, climbing | 17.43 vertical | 523 | code (half the forward value) |

Partial stick deflection gives the curve's fraction (RECON.md: 88% → 15.0 per tick, 71% → 6.0).
There is no separate walk.

## Crouching

Action 3 (L2) **toggles** (`inv` flag 2):

- Down: flag 2 set at once, the eye target `inv+0x3C` = -80 (-150 when `g_params` +0 is 5 and +4 is 4). The
  camera's eye offset (`camera_obj+0x94`) moves 20 per tick toward it: 8 ticks from -240
  (measured). When it arrives, `player_crouch_done` (`0x800798AC`): the forward and strafe
  roles' maxima and analog speeds are quartered (`role_axis_set_speed`/`role_button_set_speed`
  with mode 4, shift 2), the soldier's eye height `+0x2D4` = 80 and its sphere centre
  `+0x2DA` = 0 (only when not riding a parent). So **the slowdown comes at the end of the
  8-tick crouch**, the speed during the descent is still full.
- Up: pressing crouch again (`player_stand`, `0x80079718`) unless flags B 0x800 is set: the
  player is in a low-ceiling zone, set by the level's trigger boxes (soldier.md, The player's
  concealment flags; enemies' sight checks also treat the player as low there). Speeds restored at once,
  eye height `+0x2D4` = `inv+0x34` (240), sphere `+0x2DA` = -100, target -240; the camera rises
  20 per tick (8 ticks, measured).
- Jumping is not possible while crouched (the jump checks flag 2). A ladder stands you up.
- The easing has a quirk for targets not a multiple of 20 away (only -150): within 18 of the
  target while moving crouched, the step is `current - target` (away from it), then ±20.
- Crouching also raises `0x800AA168` to ≥ 300 (standing: ≤ 250; measured 250 → 300 → 250).
  It is a **drawing distance only**: the world drawers (`0x80089D68` in LEVEL.BIN, `0x80010B60`
  in the exe) copy it to the scratchpad and flag the polygon vertices nearer than 4× it (1000
  standing, 1200 crouched; a second flag under 200), which picks the polygons to subdivide
  near the camera (inferred: against texture warping close to the low camera). No gameplay
  effect; the projection stays 400.

**The eye height `+0x2D4`** (the collision capsule's height, below; not the camera's) is set by
(`fn.py grep` for stores to `+0x2D4`, then watched in 1:1):

| Who | Value | When |
|---|---|---|
| `player_soldier_init` | 240 | level start |
| `model_instance_init` | the model's `+0xA0`: **188** for every player arms model | **every weapon switch** (`weapon_draw` swaps the soldier's model) |
| `weapon_draw` | 80 | the switch, while crouched |
| `player_crouch_done` / `player_stand` | 80 / `inv+0x34` (240) | crouch reached / stood up |
| `player_update` | 80 | crouched while riding a parent |
| the model-script op 0x1F (`property`, selector 3) | any | no player script uses it |

Measured: 240 at start; 188 after the first switch; 80 crouched; 240 after standing; 188 again
at the next switch, 80 if switched while crouched. So the player's capsule is 52 units shorter
from his first weapon switch until he next crouches and stands (the 1:3 session's 188). The
camera's height is unaffected (`camera_obj+0x94` stays -240).

## Jumping and falling

Jump: action 1 (Triangle) **pressed** (not held), on the ground, not crouched, not aiming:
`leave_ground` (`+0x284` = 0), vertical speed `+0x21C` = **-26.0** (-6.5 with the debug flag
below).

Gravity (`entity_move`, flags A 0x20, off the ground): vertical speed += **2.0** per tick, at
most **75.0**, applied before the move, so the first tick rises 24 (debug flag: += 0.125, at
most 4.6875).

Measured standing jump on flat ground: rises 24, 22, ..., 2, 0 (apex **156** after 13 ticks),
falls 2, 4, ..., 20, then lands on the 23rd tick (the remaining 46 taken at once by the landing
snap). Falling off a ledge follows the same curve from 0: 1406 units to reach 75 per tick
(37.5 ticks).

Landing (`entity_move`, airborne, after the move): with `dy` this tick's vertical move in whole
units, the player lands if it moved down (`dy ≥ 0`) and the floor is below the feet by at most
`min(dy + 16, 64)`, or the feet are at or below it: feet = floor, vertical speed 0, on ground;
then `world_contact` zeroes the whole velocity (no effect on the next tick, which sets it from
the input again).

**No fall damage** (code): nothing in the landing or the player's `world_contact` hurts; the
only height death is y > +5000 (falling out of the world).

**Ceilings don't stop a jump.** Measured in 1:3 (`tools/recon/mv_column.py` found a flat ceiling
491 above the floor at (-3246, 2596), walked there, jumped): the full 24, 22, ..., 2 rise, apex
156, the same 23-tick arc as in the open, and no contact flag (`+0x38` 0x8000) at all; but that
ceiling was never touched (contact needs it within the radius, 100, of the segment's top, 224
above the feet: 480 above the floor at the apex). Read from the code: a down-facing plane does
count as a wall (levels.md, Walkers), but its push is horizontal only, so a flat ceiling moves him
by little or nothing (the session's 1:3 tick 258: face 187 of `1_3_C2.TSP`, ny 0.98, two pushes of
a few units, matched by the simulator) and the rise goes on; the re-check that could undo the
move looks below the feet. `entity_move`'s one ceiling branch tests the move's whole-unit
vertical delta (`lh 0x42(sp)`) against 0x801, not a normal: **dead for the player**. With the capsule's top 444 above the feet standing (levels.md), a jump pushes it
into ceilings lower than ~600; the camera (eye) can reach 396 above the floor, so a ceiling
lower than that would be seen through (inferred; none tested). A sloped ceiling's horizontal
normal does push the walker sideways like a wall.

## Steps, slopes, edges

On the ground, after each move (`entity_move` with `floor_find`, levels.md): the floor search
takes floors below the feet or at most **32** above them. Then, with `d` = feet - floor
(positive: feet below the floor):

- `d ≥ -32` (floor up to 32 below, or above): feet = floor. **Steps up and down of up to 32
  are taken instantly**, no smoothing (the camera follows one tick later).
- Floor more than 32 below: the soldier leaves the ground and falls.
- No floor at all (-0x7FFF): **nothing happens for the player**: the position is kept, at the
  same height, still on the ground or still falling. `entity_move` would undo the move (and, in
  the air, put the feet back on the last floor `+0x208`) only for entities without flags B
  0x10000, and the player's flags B are 0x21000 in every traced tick (the 1:1 runs and the whole
  session). Checked: 71 such ticks in the session (most in 2:1) and the simulator, keeping the
  position, matched them all. The old text ("the move is undone") was read from the branch
  without the flag test.

The steep flag (floors steeper than ~30°, `floor_in_chunk` `0x80060138`) is **read by nothing**:
it is the fourth halfword of the normal that `floor_find` copies out, and none of its four
callers (`entity_move`, `destructible_spawn`, the pickup drop `0x800833A0`, `obj1774_think`)
reads that halfword back (`fn.py show`; their stack copies are never loaded). Inferred:
how steep a slope can be climbed is set by the 32-unit step rule and the wall push-out of
`walker_slide`, not by an angle; at full run a rise of more than 32 per 34.9 (about 43°) per
tick finds no floor ahead. Observed: walking crouched up a gentle slope in 1:1, the soldier slid sideways every tick and
every fourth tick was pushed back ~25 units and down. **Cause** (the simulator reproduces every
tick of it, `build/sim/t1`, vblanks 10341..10420): not the slope's own triangles, which are
floor-like and never count as walls for the player (levels.md, Walkers), but two steep
triangles beside and ahead of the path (faces 124 and 100 of `1_1_C1.TSP`, 73° and 63°, rising
40 to 200 units above the feet). Crouched, the walker's segment is only 4..64 above the feet and
reaches into their lower edge; each tick 124 pushed him sideways, and every few ticks 100 pushed
him back twice along its normal (a sloped plane's push is scaled by its horizontal part, so it
repeats), then the floor snap put him lower on the slope. The earlier explanation (every
sloped floor triangle acts as a wall when crouched) was wrong.

## Collision shape

The soldier is a walker (flags A 0x8; `walker_slide`, levels.md has the full rules): radius
`+0x2EC` = **100** (soldiers 60) and a vertical segment from `eye - 16` above the feet down to 4
above them (eye = `+0x2D6`: 240 standing, 80 crouched, **188 standing after any weapon switch**,
Crouching). Distances to a plane are taken at the feet for up-facing planes and at the
segment's top for the others, less the radius:

- Only the planes the move goes into, and only when the whole-unit position changed.
- Floors and slopes up to 45° are never walls; steeper triangles, walls and ceilings are.
- Out-pushes are horizontal only, to radius + 4 along the plane's normal, nearest plane at the
  old position first, repeated: he slides along walls (measured: about 32 per tick along a wall
  hit at an angle) and never climbs them; height comes only from the floor.
- If, after the pushes, a plane is still too near, the **whole move is undone** for the tick
  (position and on-ground back to the start; seen 99 times in the session, all matched). The
  re-check gathers its planes around a segment that runs from the feet *down* to eye - 20 below
  them (the first gather's centre minus fp is not applied; inferred to be a slip), so it fires
  mostly against walls reaching below the floor level, e.g. steep banks.

The entity sphere (touching pickups, triggers, enemies) has radius `+0x2E8` = 200 and centre
100 above the feet standing, at the feet crouched.

**Against soldiers and dogs** (the player's touch handler `0x8007C030`, in the contact phase
after every move): when a soldier (0x1402) or dog (0x1405) whose model instance `+0x6C` is 0x80
overlaps him (horizontal distance, as max + min/4 of |dx|, |dz|, below the sum of the two
`+0x2EC` radii), the player gets a push of the overlap (at most 65) away from it in `+0x1F0`,
used by the **next** tick's move, and `+0x200`, `+0x218`, `+0x220` are cleared (no effect on the
next tick, which sets them from the input again). The push can round to 0 while still clearing
the speeds. Against objects (doors, crates) the same handler pushes him out of their planes
directly (objects.md, Blocking), after `entity_move` has stored `+0x3BC`.

## Ladders

levels.md (Triggers) has the ladder entity. While its think sets `inv+0x70` (the player is
within the radius and below the top), `ladder_climb` (`0x8007F1E0`) replaces the forward
movement: vertical speed = half the forward role (forward input: up at 17.43 per tick), forward
speed 0, gravity off; backward input goes down, at most to the floor; at the floor (or with no
input) the normal movement runs, so backing off the bottom walks away. Strafing still works.
Above the top the think stops setting `inv+0x70`: gravity comes back, the vertical speed is
zeroed (`player_update`'s latch `inv+0x74`), and forward input walks on. Being on a ladder
stands a crouched player up.

Exactly (`player_controls` calls it when `inv+0x70` is non-zero, before the forward role; when
it returns 1 the forward role is not applied): `+0x21C` = 0, `+0x200` = 0, flags A &= ~0x20;
v = the forward role's button value >> 1, or if that is 0 and the port is analog its stick value
>> 1 (the role as `player_controls` reads it, halved before the fallback). v < 0 (up):
`entity_leave_ground` (`+0x284` = 0), `+0x21C` = v, `inv` flags | 4, returns 1. v > 0 and y
(`+0x210`) < the floor (`+0x208`): `+0x21C` = min(v, floor - y), flags | 8, returns 1. Otherwise
`+0x21C` = 0 and 0.

## Aim mode (R2) and leaning

While aim mode is on (`inv` flag 0x20; only starts on the ground):

- The soldier is **pinned**: its position is saved in `inv+0x90..0x98` when aim starts and
  every tick the push puts it back there; gravity off, no floor check, forward speed 0.
- **Strafe leans**: `lean = (2·strafe + 3·lean) / 4` per tick (`inv+0x9C`), and the body sits
  `lean` units to the side of the pinned spot: at most 69.7, about 90% there in 8 ticks, back
  the same way on release (measured).
- In stances 0/1 (aiming a gun) the look roles move the **crosshair** instead of the view:
  `inv+0x188 += turn/8`, screen x = 256 + `inv+0x188`·256/60.0; `inv+0x17C += pitch/8`,
  screen y = 131 + `inv+0x17C`·120/60.0. Past the box (±64 px and ±30 lines; ±208/±86 with the
  option flag 0x400000) the view turns instead. Turning is scaled by 0.5625 (0.375 with option
  flag 0x80000, set on a fresh card) and pitch by half that (measured 22.41 and 11.21 units per
  tick at full stick with 0.375). The forward stick drives pitch when the pitch stick is idle
  (`0x8009E398`). Changing stance recentres the crosshair (256, 131; bazooka 270, 115).
- The scope (sniper, `inv` flag 0x100): turn and pitch input ÷ 8. Zoom: the projection distance
  `inv+0xA4` (normally 400) grows toward 2447 (6.1×; mode 0x200000: 475) by a step that starts
  at 3 and grows by 2 per tick up to 65 (about 47 ticks to full sniper zoom); leaving aim mode
  resets it to 400 at once.

## Turning and looking (not aiming)

In the walking stances (7–9) the turn role is added to the view yaw `inv+0x184` **as is**: 60
units per tick at most (59.77 with a stick), 5.27° per tick, 158° per second; measured. (The
0.5625 scaling in combat.md applies only to the aiming stances.) Aim friction (combat.md:
`inv` 0x100000 halves, bit 31 quarters) still applies; the scope divides by 8. `inv+0x188`
keeps a smoothed turn rate (`(3·old + turn)/4`, decaying when the input is under 1.0).

The soldier's body angles (`+0x228`, `+0x22C`) follow the view through `aim_update` (combat.md)
in stances 7 and 8, so the gun trails a fast turn; in the other stances they are set to the
view's angles directly. Movement always uses the view yaw `inv+0x184`.

Pitch `inv+0x178` depends on the look mode `0x8009E3A0[port]` (an option, set by the pad
layout code `0x80076240`; 1 in both dumps). Modes 0 and 2 were measured in 1:1 by poking it
(`--poke-on-call 80044470:8009E3A0=M`, the first projectile's think, after firing once), with
the right stick held up (`--stick F:ry=0:60`):

| Mode | Pitch |
|---|---|
| 0 | Look spring: `pitch = (3·pitch - role)/4`, i.e. follows the stick's position and returns when released (at most ±60 units: 5°). Measured: 14.94, 26.15, 34.55, ... → 59.75 held; × 3/4 per tick back to 0 when released. L3 toggles a lock (`inv` flag 0x10): **the pitch freezes and the stick is ignored** until L3 again (measured). While aiming the pitch is free (+11.21 per tick, the aiming scale); on leaving aim mode it springs back × 3/4 per tick |
| 1 (default) | `pitch -= role/2` (29.9 per tick at full stick, measured), clamped to ±682 (±59.9°). Center view (action 7, L3) **while held**: `pitch = (7·pitch + 1)/8` while above 160 units (14°), then `(3·pitch + 1)/4` (measured) |
| 2 | As mode 1 while aiming; otherwise **the stick does nothing** and the pitch eases back to level as center view does (measured: 168.09 → 147.08 (7/8) → 110.31 (3/4) → ... → 0 after leaving aim; a held stick outside aim mode left it at 0). L3 still sets flag 0x10 (no visible effect) |

No pitch scaling applies outside aiming but the aim friction and the scope.

## The camera

The camera is its own entity (`g_player`, type 2, think `camera_think` `0x800473DC`), updated
before the soldier. In its normal mode (`camera_obj+0x9C` = 0x80):

- **Eye**: the soldier's x and z, and y = feet + `camera_obj+0x94` (the eye offset: -240
  standing, -80 crouched, eased 20 per tick). **No view bob**: on flat ground the camera's
  height never changed while running (measured); steps move it at once.
- **Look point**: 200 units ahead along the yaw `inv+0x184`, at height
  `eye + 2 - 200·tan(pitch)` (`view_pitch_target` `0x8007BADC` writes it; `camera_ease` copies
  it, its shift `camera_obj+0x10` being 0). So at rest the view looks 0.57° down. A second point
  200 to the side at eye height fixes the roll at 0.
- **View matrix** (`view_lookat` `0x80048EEC`): from the eye toward the look point; its vertical
  row is scaled by 5/8 (the PS1 screen's aspect). Projection distance `inv+0xA4` = 400 (also the
  GTE H for the world), zooming as above.
- Both points use the soldier's state at the camera's update: **one tick behind** the soldier.
- **Shake** (`camera_shake` `0x80046C50(camera, amplitude, count)`, from explosions and hits):
  each tick while count > 0, the camera's y += amplitude, amplitude = -amplitude/2,
  count -= 1.
- The crosshair rests 11 lines below the centre (screen line 131 of 240, combat.md), so the aim
  point is ~3° below the view direction.

**Exactly** (reimplemented in `tools/sim/engine_camera.py`; the view matrix matched the
original's bit for bit, since `aim_update` reads it):
- `camera_think`, normal mode: `camera_ease` (`0x80046C64`) moves each current point toward its
  target by (target - current) >> `obj+0x10` (0 here: a copy): the eye offset pair `+0x48..+0x50`
  ← `+0x54..+0x5C`, the look offset `+0x18..+0x20` ← `+0x24..+0x2C`, the side offset
  `+0x78..+0x80` ← `+0x84..+0x8C`. Then `camera_look_points` (`0x800479D8`, modes 0x480):
  `obj+0xBC` = the soldier's look yaw `inv+0x184`; the look offset's (x, z) and the side offset's
  (x, z), integer parts, turned by `RotMatrixY(obj+0xBE)` (`RotTransSV`, saturated): look point
  `obj+0..+8` = soldier + (turned look x, z) << 16 with y = soldier y + `obj+0x1C`; side point
  `obj+0x6C..+0x74` = soldier + (turned side x, z), y = soldier y + `obj+0x94` << 16. Then
  `camera_place` (`0x800482A4`, mode 0x80): the camera's position = the soldier's, y +
  `obj+0x94` << 16. Then the shake (`obj+0xCC` amplitude, `+0xCE` count) adds to the camera's
  y and both points' y.
- `camera_setup` (`0x8004913C`, from `entities_update` for type 2): look point, eye (the
  camera's integer position) into `obj+0xE4`/`+0xF0` (words), then `view_lookat`
  (`0x80048EEC`): f = `VectorNormal(look - eye)`, s = `VectorNormal(side - eye)`, up =
  `OuterProduct12(f, s)`, right = `OuterProduct12(up, f)`; the rows (s16) right·2,
  (up·5 / 8, a C division)·2, f·2 at `obj+0x100`; the translation `obj+0x114` =
  `ApplyMatrixLV(rows, -eye)` (PsyQ's, in two 15-bit halves). The frustum after it is drawing
  only.
- The look point's height target (`view_pitch_target`, `0x8007BADC`, from `player_update`):
  `obj+0x28` = (eye offset `obj+0x98` + C division(-d·rsin(p), rcos(p))) << 16, with p =
  `inv+0x17A` (the look pitch's integer part) and d = |x| + |z| - min/2 of the look offset's
  `+0x26`, `+0x2E`.

### Recoil kick (`inv+0x4C`, `+0x50`)

Kick is not a view movement: `aim_update` raises the crosshair ray's screen point by
`inv+0x4C` pixels (its integer part), so shots climb while the view stays. Per tick
(`player_update`):

```
kick += climb;  kick = min(kick, 15.0)
kick  = kick > 0 ? kick - 0.5 : (kick < -0.125 ? kick + 0.125 : 0)
climb = max(climb - 0.125, 0)
```

Weapons 1, 6 and 11 (the SMGs and the BAR) set `climb` (+1.0 per shot, at most 1.0,
`fire_gun`'s case for them), so only they kick (measured on 1: 14.5 pixels after 20 rounds held;
player.md, Weapons in play).

### Being hit

`player_hit_part` (combat.md) starts a flinch: `inv+0x154` = 8 ticks of knockback
`inv+0x80..+0x88` = 6 units per tick along the attacker's facing (48 in all), plus the push
less a quarter of the soldier's velocity (local components subtracted as world ones: inferred
to be meant as a 3/4 slowdown): x the strafe speed `+0x218` as `player_controls` just set it,
y the vertical speed (a climb's when the soldier is leaving a ladder this tick: the latch that
zeroes it comes after), z last tick's local forward speed `+0x220`; every other tick some angle
offsets (`+0x24C..+0x254`) halve. Checked on monkey runs of 2:4 and 3:2 (every flinch tick
exact, 225 and 8, with analog strafing into walls and a hit on a ladder).
Crouching cancels the flinch. Also a camera shake.

Just before the flinch, `player_update` keeps a 4-tick hit-part window in `inv+0x38`: while it
counts down every hit part is set to kind 8 with radius and damage 0 (`0x8004C64C(soldier,
0xFFFF, 8, 0, 0)`), at 0 back to the defaults (`-1, -1, -1`); it is started (4, and the parts
set) when the soldier's channel-0 script's class has bit 8 and its animation is at frame 0.

## Use

Action 5 (Square, the same button as reload by default), pressed (`player_use`,
`0x80084670`): every level record whose volume the player is standing in (the zone bits
`+0x394`, levels.md) sends its "use" messages (record template `+0x4C`: a count, then message
numbers, `send_event(message, 0, record, player)`). Then, unless the current weapon has flag
1, reload (`0x800460F4`); if no reload was possible, script variable 0x10 = 1. Mounting a
mounted gun is a touch + Use (combat.md).

## Death

When health reaches 0 (`apply_damage` sets contact flag 0x4000), the next `player_update`
(`player_death_start`, `0x80079D10`):

- The soldier's think becomes `player_think_dead` (`0x80079A50`); its hit, touch and contact
  handlers are cleared; aim, scope and zoom flags cleared; it stops moving on its own (flags A
  0x20000, no physics).
- If the camera has an animation: the camera entity runs script 0xF (the death camera) and
  becomes a physical walker at the soldier's place (flags A 0x102A, gravity), mode 0x800 (its
  eye and look point follow the animation's bones), easing shift 3; its variable 2 picks the
  death animation: 30 if crouched, 20 if killed by an explosion (type 0x31), otherwise 50%
  10–12, 50% 0–2. The soldier copies the camera's position and angles every tick (`player_think_dead`; the camera is first in the entity list, so it is the camera's position after this tick's move: every dead tick of a 1:1 death matched it exactly; the soldier's own `+0x3BC` is stale meanwhile).
- The death camera (camera mode 0x800, `camera_think`): while its model is set it runs its own
  scripts (event matcher, then the channels) and `0x80046EB4` takes its targets from the
  animation: the joint of bone `model+0xD4` (`co+0x54..`), and that bone's pose rotation (the
  quaternion, current else next else first buffer) applied to the model's offsets `+0xD8` and
  `+0xE0` plus the joint: the look target (`co+0x24..`) and side target (`co+0x84..`); the eased
  points are then put through the camera entity's own matrix (`+0x340`, `+0x354`): the look point
  (`co+0..`, its y also in `co+0x98`), the side point (`co+0x6C..`), and the eye (`co+0x3C..`,
  from the eased joint, its height `co+0x94`; kept at least 85 above the camera's floor
  `+0x208`), which `camera_setup` uses in modes 0x1800 instead of the entity's position. Without
  a model the camera goes back to mode 0x80. Its touch (`0x800476C4`) is the object planes, its
  world contact (`0x800476E8`) stops it on landing (forward speed 0, `+0x38` | 2).
- `player_think_dead` then copies the camera's `+0x284` and position/angles (`+0x20C..+0x26C`)
  each tick, sets `inv` 0x8000 when the soldier's channel-0 script is gone, runs the soldier's
  scripts (a negative `+0x288` resets it and turns the soldier half round).
- When the camera script ends (its variable 2 reads −1): a 50-tick delay, then a 15-tick fade (which the game shows as
  a cut; hud.md, Fades), and the level is lost (`0x8009C840` |= 2). Without a camera
  animation: only the delay and fade.
- What follows (the level restarts in place, no file reload, unlimited retries) is in
  [shell.md, Inside LEVEL: the loop, pausing, dying](shell.md#inside-level-the-loop-pausing-dying).

## Debug and options seen on the way

`player_pad_config` (`0x8007A050`), run when the pad's mode changes (a new mode is applied
after 30 ticks of `inv+0x134` with the velocity held at 0):

- Holding Up + L1 + L2 + R1 + R2 + Triangle at that moment sets `inv` 0x10000000: low gravity
  (0.125 per tick, terminal 4.6875, jump 6.5). Down + the shoulders + Cross sets 0x20000000 (×4
  damage, combat.md).
- Options from `g_game` (+0xE8/+0xEC/+0xDC.. for port 0): the slow aim turning (0x80000), the
  big crosshair box (0x400000), the button layout.

## Checked against a reimplementation

`tools/sim/move_sim.py` steps the player's movement as written here and in levels.md
(Collision): the move (yaw rotation with PsyQ's `rcossin_tbl`, which is round(4096·sin), and the
velocity cut to 1/16 unit), gravity, jump, the floor search, steps and falls, landing, walls,
ladders, aim-mode pinning and lean, the hit flinch and the soldier push. It reads the level's
TSP chunks from the extracted disc, in the slots the game had loaded, and takes as inputs what
the control function produced each tick (`+0x200`, `+0x218`, the jump's -26, the eye height
`+0x2D6`, the look yaw) from a `VALOR_WATCH` log plus a RAM dump per level; it compares its
position with `+0x3BC` (where `entity_move` left the soldier) from `VALOR_TRACE_STATE`. Run (the
watch spec is in the script):

```
move_sim.py check TRACE.jsonl WATCH.log 1:3 --from 1714 --to 20490 --ram build/sim/rec_1720.bin [--free]
```

Results (`--free`: the simulator's own state carried from tick to tick, only object touches
taken from the game; every tick of each range):

| Run | Ticks | Exact |
|---|---|---|
| Session 1:3 (vblanks 1714..20490) | 7,106 | all |
| Session 2:1 (20510..40730) | 7,416 | all, 446 of them on ladders |
| Session 2:2 (40736..61370) | 5,611 | all, 310 aiming |
| Scripted 1:1 (`build/sim/t1`, `t2b`): run, diagonal, d-pad, turn, jump, crouch, crouched slope, the basin's walls | 980 | all |

Exercised and exact among them: 2,000 wall-push ticks, 99 moves undone in a wall, 244
landings, 21 walk-offs, 71 ticks with no floor, 3,900 steps up or down, jumps, ladders, the
flinch, aiming. "Exact" is to the 16.16 bit.

What the inputs needed: `+0x218` (and `+0x200`, after a soldier touch) read 0 at the end of a
tick that cleared them, so on about 2% of ticks the strafe/forward value was recovered by trying
the keyboard values (0, ±34.863, ±8.716 crouched) and keeping the one that matches; and a
tick's watch state counts only if the frame was drawn before the next tick (the game runs 2 ticks
in a frame now and then). Not simulated: objects' touch handlers (doors, crates: they move the
player after `+0x3BC` is stored, so they never enter the comparison), riding a parent, death.

## Open

- A ceiling lower than the eye (396 above the floor at a jump's apex): whether the view passes
  through it (no such place was looked for).

## New names

```
80046C50 camera_shake               # (camera, amplitude, count): y += amp, amp = -amp/2 per tick
80046C64 camera_ease                # camera_obj current points toward targets, >> obj+0x10
800473DC camera_think               # the camera entity's think: ease, look points, place, shake
800479D8 camera_look_points         # look point 200 ahead along inv+0x184; death mode: from bones
800482A4 camera_place               # mode 0x80: eye = soldier x/z, y + camera_obj+0x94
80048EEC view_lookat                # view matrix from eye to look point (y row x5/8)
800460F4 weapon_reload              # (soldier): clip from reserve; 0 if nothing to do
80075118 leave_ground               # +0x284 = 0
800758B4 role_button_set_speed      # (port, role, n, mode): mode 4 max >> n, 2 restore
800759EC role_button_max            # (port, role): digital config +0x1C
80075D1C role_axis_set_speed        # (port, role, n, mode): analog speed, as role_button_set_speed
80076008 role_axis_alt              # (port, role_axis_of, role_cfg, ?): pitch from the forward stick
800760AC look_mode                  # (port): 0x8009E3A0[port], 0 spring, 1 free, 2 free when aiming
80079718 player_stand               # crouch off: speeds, eye 240, sphere -100, eye target -240
800798AC player_crouch_done         # crouch reached: speeds / 4, eye 80, sphere 0
80079A50 player_think_dead          # soldier follows the death camera; fade, level lost
80079D10 player_death_start         # contact 0x4000: death camera script 0xF, think swap
8007A050 player_pad_config          # pad mode change: role config, option flags, debug combos
8007B4B8 player_world_contact       # landing: velocity = 0
8007BA20 player_stance              # inv+0x1C8 from aim mode and weapon (0..9)
8007BADC view_pitch_target          # camera look point height = eye + 2 - 200 tan(pitch)
80084670 player_use                 # Use: zone records' use messages
800845DC record_use_send            # send a record's +0x4C message list
8009E398 g_aim_pitch_from_move      # per port: forward stick drives pitch when aiming
8009E3A0 g_look_mode                # per port, see look_mode
8009E3AC g_axis_curve               # 128-byte stick response curve
800B74B0 g_role_axes                # analog role config, port*0xA0 + role*0x28
800B75F0 g_role_buttons             # digital role config, port*0xD0 + role*0x34
```

## New names (2)

```
80045084 projectile_aim_at_point    # start inv+0x198, angles to the aim point inv+0x1A4; mask off until there
80044EF0 copy_shooter_angles        # projectile angles = shooter's +0x228 (grenades, player rockets)
80045160 copy_camera_pos            # scoped shot start = the camera entity's position
80045184 angles_to_camera_look      # scoped shot angles toward the camera's look point
80059718 sop_property               # model-script op 0x1F: set a field by selector (3: eye height +0x2D4)
80089D68 tsp_draw_chunk             # world drawing; 4 x 0x800AA168 = the near-subdivision distance
80076094 look_mode_set              # (port, mode) -> g_look_mode
8006A8FC model_by_index             # [0x8009EC58] + i * 0x100
8007848C walls_gather_leaf          # triangles of a leaf near the walker -> planes
80078350 plane_crosses_capsule      # walker plane test before the push
800787C0 walls_gather_chunk
800AA168 g_subdiv_dist              # 250 standing, 300 crouched; x4 = polygons subdivided nearer
8003D218 fire_stance_table          # fire_gun: stance -> aim at the crosshair point (0,1,7,8) or not
8003D240 fire_weapon_table          # fire_gun: weapon -> bullet / SMG bullet+climb / rocket / pellets
```
