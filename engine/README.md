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
| `core/src/player.cpp` | The player's controls for play: run, strafe, turn, pitch, jump, crouch with the eye easing and quartered speed |
| `core/src/level.cpp` | The BSD header fields used so far and the player start (formats.md, levels.md) |
| `core/src/render_data.cpp` | Presentation data: TSP render tree, faces, vertex colours, animated light colours, TAF TIMs placed in a 1024×512 VRAM, texture pages decoded through a CLUT |
| `core/src/trace.cpp` | Traces in the `VALOR_TRACE_STATE` format (runtime/src/watch.cpp) |
| `tools/coretest/` | Steps the core on a replay's inputs and compares every tick with the original |
| `tools/export_moves.py` | Writes those inputs from a replay's trace and watch log (move_sim.py's reader) |
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
| W A S D | Run and strafe at the keyboard speed (34.863 units per tick) |
| Mouse, ← → | Turn and look (the d-pad's 60 units per tick on the arrows) |
| Space | Jump |
| C, Ctrl | Crouch (toggle) |
| Esc | Release the mouse |

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
discarded, the animated lights' colours substituted (each light's first step for now), vertex fog,
and the four semi-transparency modes. It uses the Compatibility renderer, whose output matched the
original's brightness side by side.

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

That is the same result `move_sim.py --free` gets, tick for tick. The inputs are the control
function's outputs, as for move_sim. On 2% of ticks the strafe or forward value was cleared in RAM
before the watch saw it, and it is recovered by trying the keyboard values (move_sim.py's
inference, kept as is).

## What's next

In order, following the closed-loop Python engine in `tools/sim/engine*.py`, which composes the
reference simulators:

1. **Input from the pad.** The control roles (digital ramps, the analog curve, movement.md) so the
   core takes raw pad state, and a recorded tape's pad stream drives it directly. Then the rest of
   `player_update`: aim mode and lean, ladders, the flinch, Use, the eye height 188 after a weapon
   switch, death.
2. **Entities and the tick order** (objects.md, "The frame and the tick"; entities.md): the entity
   list, handles, think, move, contacts; the chunk streaming slots (kind-0 records), which the
   floor search depends on. All chunks are loaded for now.
3. **Level logic** (levels.md, messages.md; `level_vm.py`): records, the level-script VM, triggers
   and zones, the deferred queue, objectives.
4. **Enemies** (enemies.md, soldier.md; `sst_vm.py`, `brain_sim.py`): the behaviour-script VM, the
   brain and movement, animation with root motion.
5. **Combat** (combat.md; `combat_sim.py`): weapons, bullets, hit spheres, damage.
6. **Presentation in Godot**: static and skinned models, animated lights stepped per tick, the sky,
   PVS culling, sounds and music.

Each system gets a coretest mode against the original's trace, as movement has. Once the entity
list exists, the core writes full `VALOR_TRACE_STATE` lines, and `tools/recon/trace_cmp.py`
compares whole traces.
