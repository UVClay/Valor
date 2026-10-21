# The Valor engine

Medal of Honor's rules in a native engine. The game logic is an engine-agnostic C++ library with the
original's arithmetic, written from `docs/re/` and the reference reimplementations in `tools/sim/`.
Godot hosts it: rendering, input and the window. The recompilation stays the reference, and every
system ported here is checked tick by tick against a replay of it.

## Layout

| Path | What |
|---|---|
| `core/` | `valor_core`, a static library in plain C++20 with no engine types and no floats in game state: 16.16 positions, 4096-per-turn angles, MoH's axes (y down), the 30 Hz tick. No exceptions, no global state, deterministic. It can also be linked into the recomp's runtime for a live lockstep comparison |
| `core/src/world.cpp` | TSP collision blocks, `floor_find`, walls of a walker (levels.md, Collision) |
| `core/src/movement.cpp` | `entity_move` and `walker_slide` (movement.md) |
| `core/src/controls.cpp` | The pad and the control roles: `role_button`'s ramps, `role_axis`'s curve, actions with their exclusion masks, the speed changes of crouching; a fresh card's configuration (movement.md, Control roles) |
| `core/src/player.cpp` | `player_update` and `player_controls` as they run: aim mode with the pin and lean, the aiming stances' crosshair box, centre view, the three look modes, jump, the crouch easing (and 5:4's lower crouch), ladders, the flinch, zoom, `weapon_draw`'s effects, the death start; then the move. Systems not ported come in as events (`PlayerEvents`) |
| `core/src/level.cpp` | The BSD header fields used so far and the player start (formats.md, levels.md) |
| `core/src/records.cpp` | Entity records and their volumes, the floor zones' record lists, the zone tracking (enter/leave with the original's skips), the chunk streaming (levels.md, Triggers; formats.md, chunk streaming records) |
| `core/src/entities.cpp` | The entity list and handle table, linking, immediate removal with handles freed at the end of the frame, the deferred message queue (entities.md, objects.md) |
| `core/src/session.cpp` | A level in play: each tick walks the list (think, move; the world's lights, the soldier, ladders' thinks), zones and streaming after the soldier's move, the 1/3-second message; each frame's end runs the queue, frees handles, streams |
| `core/src/gte.cpp` | libgte's integer math: `ratan2`, the square roots, `rsin` (the game's tables, read from the executable), the rotation matrices of angles and quaternions |
| `core/src/model.cpp` | The level's models from the BSD (inheritance, the skeletal tables, skinned and static faces, vertices, hit parts), `anim_frame_pose`'s interpolation, the joints, the hit parts' centres, skinning (animation.md) |
| `core/src/anim.cpp` | A model instance's animation state: op 00's start (continue, cross-fade, cut), the frame advance, `pose_build`'s cache and fade |
| `core/src/render_data.cpp` | Presentation data: TSP render tree, faces, vertex colours, animated light colours, TAF TIMs placed in a 1024×512 VRAM, texture pages decoded through a CLUT |
| `core/src/trace.cpp` | Traces in the `VALOR_TRACE_STATE` format (runtime/src/watch.cpp) |
| `tools/coretest/` | Steps the core on a replay's inputs and compares every tick with the original: the movement on the control function's outputs (`coretest.cpp`), the player from the pad (`pad_mode.cpp`), the poses against RAM dumps (`pose_mode.cpp`) |
| `tools/capture.sh` | Replays a recording through the original and keeps the state trace, the input trace (the pad, every role call, the soldier as each update finds it), the watched fields and RAM dumps |
| `tools/export_pad.py` | Writes a capture as coretest's pad input |
| `tools/padcheck.sh`, `padrecheck.sh` | Captures recordings and checks every level start and restart in them; the second reruns existing captures |
| `tools/export_moves.py` | The movement-only input (move_sim.py's reader) |
| `godot/` | The GDExtension (`ValorGame`) and the Godot 4.7 project |
| `godot/tools/screenshot.sh` | Runs the project on a virtual display (Xvfb and software OpenGL in a container) and saves a frame |

## Building

The toolchain is pinned to exact versions:
- Godot 4.7.2-stable, the official Linux x86_64 build, checked against the release's SHA512-SUMS, in
  `build/godot/` (not in git);
- godot-cpp 10.0.0-stable, a submodule at `third_party/godot-cpp` (`git submodule update --init`),
  targeting API 4.7.

```sh
# the core, coretest and the GDExtension, in the dev container (./dev)
./dev cmake -S engine -B build/gd/cmake -G Ninja
./dev cmake --build build/gd/cmake -j8
# the core alone: -DVALOR_GODOT=OFF
```

The extension lands in `engine/godot/project/bin/`. The dev container has no static libstdc++, so
the library links the system one (`GODOTCPP_USE_STATIC_CPP=OFF`).

## Running

```sh
build/godot/Godot_v4.7.2-stable_linux.x86_64 --path engine/godot/project -- --level=1:3
```

Before the first run, `--headless --import` once registers the extension. The project reads the
extracted disc from `extracted/moh/DATA` (`--data=DIR` to change it). Nothing from the disc is
copied into the project.

| Control | |
|---|---|
| W A S D | Run and strafe at the keyboard speed (34.863 units per tick: the analog roles, as the runtime's keyboard gives them) |
| Mouse | Turn and look (the analog roles) |
| ← → | The d-pad: turning that ramps up to 60 units per tick |
| Space | Jump (Triangle) |
| C, Ctrl | Crouch (L2, a toggle) |
| E | Use (Square) |
| Right mouse button | Aim (R2): pinned, the strafe keys lean |
| Middle mouse button | Centre the view (L3) |
| Esc | Release the mouse |

The keys press a fresh card's buttons, which the core reads through its own control
configuration, exactly as the game does.

The core ticks at 30 Hz in `_physics_process` (`physics_ticks_per_second = 30`). The camera is
interpolated between the last two ticks at the display rate. The mouse turns the view at once,
and its yaw reaches the core on the next tick. Other user arguments:
- `--place=X,Y,Z,YAW,PITCH` warps to a pose given in game units;
- `--walk=TICKS` walks forward on its own;
- `--hfov=65.4` gives the original's horizontal field of view (the default is 51.4° vertical, which
  is the same view at 4:3);
- `--screenshot=FILE --frames=N` saves frame N and quits.

`engine/godot/tools/screenshot.sh OUT.png ARGS...` does the same on a virtual display.

The world is drawn as the PS1 drew it: texture × vertex colour × 2, the transparent texel
discarded, vertex fog, and the four semi-transparency modes. The animated lights are stepped by
the core each tick (`lights_update`) and coloured in the shader. Every chunk is drawn. Collision
follows the streaming slots, with instant loads. It uses the Compatibility renderer, whose output matched the
original's brightness side by side.

The level's records' models are drawn at their records: objects with their static meshes, soldiers
and dogs skinned from poses the core builds every tick (a walk or their first animation, looping:
the scripts that choose animations aren't ported). The characters' textures aren't right yet: their
faces point at VRAM (640, 256), where 1:3's TAF puts the dog's skin, so the soldiers' own textures
reach VRAM some other way (not found yet).

## Checking the core against the original

```sh
python3 engine/tools/export_moves.py build/sim/rec2.jsonl build/sim/rec2.log 1:3 \
    --from 1714 --to 20490 --ram build/sim/rec_1720.bin@1720 > build/gd/moves_1_3.txt
build/gd/cmake/coretest build/gd/moves_1_3.txt --free [--trace OUT.jsonl] [-v N]
```

The trace and watch log come from one replay of the recording, with `VALOR_TRACE_STATE` and
`VALOR_WATCH="$(python3 tools/sim/move_sim.py watch)"`, plus a RAM dump per level (move_sim.py's
docstring). Results on the recorded session, free-running (the core carries its own state; only
object touches, which move the player after the move, come from the original):

| Level | Ticks | Exact to the 16.16 bit |
|---|---|---|
| 1:3 (vblanks 1714..20490) | 7,106 | all |
| 2:1 (20510..40730) | 7,416 | all |
| 2:2 (40736..61370) | 5,611 | all |
| Scripted 1:1 run `t2b` | 53,617 | all |
| Scripted 1:1 run `t1` | 647 | 553, exactly as `move_sim.py` |

With `--stream`, the core fills the chunk slots itself: the player's zones, the streaming records,
and the level's initial slots. It no longer takes them from the game. With
`--stream-log build/gd/stream_watch.log`, the reads take as long as the original's did: each one
completes at the vblank the replay's `VALOR_WATCH="cs=80098970:10;cl=800AA184:4"` showed. Then all
three levels are exact on every tick. The slot numbers equal the game's on every tick but one
(the idle job ran a frame later). That includes 2:1 at vblank 38124, where the player outran a
four-second read and dropped to a lower floor. With instant loads (`--stream` alone), 2:1 differs
from that tick on, and only there (formats.md, chunk streaming records).

That is the same result `move_sim.py --free` gets, tick for tick. The inputs are the control
function's outputs, as for move_sim. On 2% of ticks the strafe or forward value was cleared in RAM
before the watch saw it, and it is recovered by trying the keyboard values (move_sim.py's
inference, kept as is).

### From the pad

```sh
engine/tools/capture.sh recordings/2026-10-19_22-22-43_m1l3 build/gd/cap/session 61900 1720 20500 40740
python3 engine/tools/export_pad.py build/gd/cap/session 1720 > build/gd/pad_1720.txt
build/gd/cmake/coretest build/gd/pad_1720.txt --ram build/gd/cap/session/d1720.bin \
    --stream-log build/gd/cap/session/run.log [-v N] [--stick-only]
engine/tools/padcheck.sh -j 4 --stick-only build/cov/tapes/w??   # every start and restart
```

The core runs from a RAM dump a few vblanks into the level, free, on the pad words and every
control-role call of each tick (`VALOR_TRACE_INPUT`, which the runtime logs at `player_update`). It
computes `role_button` itself and checks every value against the original's. It takes `role_axis`
from the trace, since the session's mouse and keyboard add to it in the runtime. With
`--stick-only` (monkey runs, gamepads), it computes `role_axis` from the stick bytes too. Systems
not ported come in as events, from the soldier as the next update finds him:
- the weapon in hand and `weapon_draw`;
- aim_update's friction and body roll;
- a hit's flinch and death;
- touches by soldiers and objects;
- the trigger boxes' flags B;
- the level logic placing the player.

Compared every tick: the position where `entity_move` left him, the view's yaw and pitch, the eye
height, the crouch, aim and scope flags, the flinch, the aim-mode flag B.

| Recording | Segments | Ticks | Position, yaw, pitch exact |
|---|---|---|---|
| The session: 1:3, 2:1, 2:2 | 3 | 22,682 | all (and all 90,242 `role_button` values) |
| Monkey runs (`build/cov/tapes`, `--stick-only`): 1:2, 2:3, 2:4, 3:2, 3:3, 3:4, 4:1, 4:2, 4:3, 5:1, 5:2, 5:3, 5:4, 7:1, 7:2, 7:3, 9:1, 9:2, 9:4 | 91 (level starts and restarts after deaths) | 143,648 | all |

Two ticks' eye height differ, in 5:4, where the player rides a parent (`+0x2C`; riding isn't
ported). The entity list follows the original's for the systems not ported: the core takes their
spawns and removals from the trace. The ladders it makes itself got the handles the original gave
them, in every case.

### Poses

```sh
build/gd/cmake/coretest --pose 1:3 build/simcheck/2026-10-19_22-22-43_m1l3/d1*.bin ...
```

For every soldier and dog in a RAM dump, the core checks three things against RAM:
- the pose it builds from the level file;
- the joints from the pose in RAM;
- the hit parts from the joints.

On all the session's dumps (`tools/sim/check.py`'s, 1:3, 2:1 and 2:2) it gives exactly the counts
`combat_sim.py pose` gives. The misses there are dumps that caught an instance moved on since its
last draw. `combat_sim.py check` follows them tick by tick and finds every pose exact.

## What's next

In order, following the closed-loop Python engine in `tools/sim/engine*.py`, which composes the
reference simulators:

1. **The rest of the player**: the death camera (a camera animation's root motion carries the
   soldier) and the restart in place (`level_start`, shell.md), riding a parent, `aim_update`
   (combat), weapons and their scripts, Use's messages delivered.
2. **Entities**: the contact phase (`entities_collide`: touches, hit parts), spawning by records.
3. **Level logic** (levels.md, messages.md; `level_vm.py`): records, the level-script VM, triggers
   and zones, the deferred queue, objectives.
4. **Enemies** (enemies.md, soldier.md; `sst_vm.py`, `brain_sim.py`): the behaviour-script VM, the
   brain and movement, animation with root motion.
5. **Combat** (combat.md; `combat_sim.py`): weapons, bullets, hit spheres, damage.
6. **Presentation in Godot**: the characters' textures, the sky, PVS culling, sounds and music.

Each system gets a coretest mode against the original's trace, as movement has, and its events
leave the pad mode as the system is ported.
