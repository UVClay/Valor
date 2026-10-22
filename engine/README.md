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
| `core/src/camera.cpp` | The camera entity in its normal mode: `camera_think` (ease, look points, the eye at the soldier, the shake) and `camera_setup`'s view matrix (movement.md, The camera) |
| `core/src/aim.cpp` | `aim_update` (combat.md, Aiming): the crosshair's ray swept against the world and the entities' spheres and hit parts, the aim friction, the aim point and shot start, the gun's two points and the angular velocity that turns the body toward the crosshair |
| `core/src/level.cpp` | The BSD header fields used so far and the player start (formats.md, levels.md) |
| `core/src/records.cpp` | Entity records and their volumes, the floor zones' record lists, the zone tracking (enter/leave with the original's skips), the chunk streaming (levels.md, Triggers; formats.md, chunk streaming records) |
| `core/src/entities.cpp` | The entity list and handle table, linking, immediate removal with handles freed at the end of the frame, the deferred message queue (entities.md, objects.md) |
| `core/src/session.cpp` | A level in play: each tick walks the list (think, move; the world's lights, the soldier, ladders' thinks), zones and streaming after the soldier's move, the 1/3-second message; each frame's end runs the queue, frees handles, streams |
| `core/src/gte.cpp` | libgte's integer math: `ratan2`, the square roots, `rsin` (the game's tables, read from the executable), the rotation matrices of angles and quaternions |
| `core/src/model.cpp` | The level's models from the BSD (inheritance, the skeletal tables, skinned and static faces, vertices, hit parts), `anim_frame_pose`'s interpolation, the joints, the hit parts' centres, skinning (animation.md) |
| `core/src/anim.cpp` | A model instance's animation state: op 00's start (continue, cross-fade, cut), the frame advance, `pose_build`'s cache and fade |
| `core/src/render_data.cpp` | Presentation data: TSP render tree, faces, vertex colours, animated light colours, TAF TIMs placed in a 1024×512 VRAM, texture pages decoded through a CLUT |
| `core/src/trace.cpp` | Traces in the `VALOR_TRACE_STATE` format (runtime/src/watch.cpp) |
| `sim/` | `valor_sim`: the whole closed-loop engine, ported from `tools/sim/engine*.py`, on the original's memory layout (see The engine below). |
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
| Esc | Pause (Start): the pause menu |

The keys press a fresh card's buttons, which the core reads through its own control
configuration, exactly as the game does. They are the defaults of Godot input actions
(`valor_*`), which OPTIONS > CONTROLS rebinds.

**Options** (`project/shell/options.gd`, from the title and the pause menu; kept by
`shell/settings.gd` in `user://valor_settings.cfg`):
- VIDEO: fullscreen, the window size, and the field of view (vertical, 51.4° the original's). It
  shows the view across the current screen, and says whether it stays inside the logic's view:
  the recording's, 110° across at 21:9, 62.9° vertical (The view test, below). Past it, things at
  the screen's edges count as out of sight: their poses hold and out-of-sight timers run.
- CONTROLS: every action's key or button, the mouse speed, invert Y, and the square stick
  (present/'s `--square-stick` for a round gamepad stick).
- AUDIO: the master volume, and the game's own Sound Effects and Background levels (0..6, 4 by
  default, shell.md). Effects scale the effect voices' buses linearly from 4 (an inference); the
  presentation layer's music player doesn't take the Background level yet.

**The pause menu** (`project/shell/pause.gd`) is `start_handler`'s screen (shell.md):
- PAUSED, the mission objectives ticked when done, and TOTAL ENEMIES NEUTRALIZED (state
  `kills`, `g_stat_kills`) in the original's places on the 512 × 240 screen;
- then CONTINUE, OPTIONS (the port's addition; the game has none there) and QUIT, which asks
  ARE YOU SURE? with NO resuming the game;
- the scene tree pauses, so the ticks and the sound stop. Start resumes after 10 frames, as the
  game's does.

QUIT returns to the front end ("RETURNING TO BASE": exit code 1, no carry), or ends the program
when there's no front end. `--pause-at=TICK [--options[=video|controls|audio]]` opens them for
screenshots.

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

### The whole engine, live (`--engine`)

```sh
build/godot/Godot_v4.7.2-stable_linux.x86_64 --path engine/godot/project -- --level=1:3 --engine
```

With `--engine[=AMMO]` the level runs on the engine (`sim/`), not the typed core:
- the start is built from the disc files (`Engine::load_disc`) with a fresh campaign's save data
  (`game_defaults`), where every weapon of the level starts with AMMO rounds (99 by default);
- the soldiers and dogs spawn from their records and run on their own logic: scripts, brain,
  sensing, movement, firing. They are drawn from the engine's poses through the accessor layer
  and re-skinned every tick;
- the player's input is the keys and mouse above, in the game's layout 1 (the left mouse button
  is Cross: fire; Q is Circle: switch weapons);
- `rand` is PsyQ's on the seed in memory;
- an entity counts as drawn when the original's view test passes (below);
- the idle work while a frame waits for its vblank is the original's (`frame_wait_vblank`), at
  30 Hz with one tick a frame: one idle job, then idle jobs until the second vblank. That's 189
  more, the 1:3 session's median for frames whose work ran past their first vblank (2:1's is 204,
  2:2's 498; a tenth of 1:3's frames ran fewer than 78). The jobs run messages, spawn retries,
  removed handles and chunk streaming;
- dying works as the original's does. The soldier runs its death script (0xF); the camera
  becomes the death camera: a walker at the soldier's place whose eye and look point follow its
  animation's bones (`0x80046EB4`, camera_look_points and camera_place in mode 0x800). When the
  animation ends there is a 50-tick delay and a 15-tick fade, then flag 2. The level then
  restarts in place (`level_restart`): every entity but the soldier, the camera, the world and
  type 8 is removed, and `level_start` runs again on the running state, without reloading files.
  `coretest --live 1:3 --replay`: the player dies at tick 2613, the crouched death animation
  plays (20 frames), and the level restarts at 2701 at full health with its statistics zeroed.
  (The doc's "the camera runs script 0xF" is the soldier: `a0` is still the soldier at the
  call.) Godot draws the death camera's own eye and look point;
- the campaign plays through. A level's end (flags 4 or 8, as the level script's `level_set(3, 1)`
  makes them) runs `level_exit` with `level_end_carry`, then what the shell's CONTINUE does
  (`progress_commit`):
  - `carry_to_next_level`: the slot's next level row gets the carried ammo and health; no carry
    between missions;
  - `campaign_advance`: on through the slots, MSN8 skipped, MSN9's third level as LVL4;
  - the next level is built from the disc on the carried save data (the rand seed and the
    vblank timer kept).

  Godot rebuilds the world, textures and fog for it (`take_level_change`).
  `coretest --live 1:1 --end-every 300` and Godot's `--engine --end-every=60` go through all 24
  levels from 1:1 to 9:4, ending each as its script would; within a mission the ammo carries
  (5:2's 15 rounds reach 5:3);
- chunks stream as the original streams them: entering a streaming record (class 0x4D078F23)
  runs `chunk_stream`, and so do the idle jobs 2 and 3. The record's chunks that aren't loaded
  replace the slots it doesn't list, one read at a time. The slot being read is left out of
  collision until the read ends. A read ends at the second music refill after its request (below). In a
  recorded run from the disc the same logic
  runs, with the reads ending when the CD's did. The slot table matches the original's at the
  dumps of ticks 4675, 5000 and 6943, and so do the slots' parsed headers. The chunk files'
  bytes differ only where the world's animated faces and lights write into them, which the
  engine doesn't model.

### Live play's approximations

Two timings in live play depend on things the engine doesn't run: the CD drive and the CPU's
time. The chunk reads now follow the original's rule; the idle jobs stay a fixed number.

**The chunk read: the music's refills.** The 1:3 recording's timing log watches the loading slot
(`0x800AA184`) through 18 reads: 223 to 388 vblanks each (median 287), and one of 658. The
chunks are 71 to 85 KB (35 to 42 sectors), which a double-speed drive reads in about 16 vblanks,
so the files' sizes don't explain the times, and a fit to size would not help. The read's own
code does:
- `tsp_load_async` (`0x8008C718`) only queues the read on channel 0 of the CD stream table at
  `0x80093B14`. Channel 1 is the music.
- The CD read callback (`0x800493A8`, registered through `0x80019640`) issues channel 0's next
  piece, at most 50 sectors and so the whole chunk, only when the flag at `0x800939F0` is set.
  It ends the read when nothing is left.
- Only the music's refill (`0x80049854`, called from `music_transfer_done` once both 0x5000-byte
  ring halves have gone to the SPU) sets that flag, after it asks for its 20 sectors.

So a chunk is read just after a music refill and finishes at the refill after that. A refill
comes every two ring halves, 2 × 35,840 samples at 22,050 Hz = 3.2508 s. At the PS1's NTSC field
rate of 59.826 Hz (not 59.94) that is 194.48 vblanks, counted from the music's start and stopped
while paused (the pause zeroes the music voice's pitch, so the ring doesn't drain). The
recording bears this out:
- each read ends 0 to 3 vblanks after a refill on that clock, from the level's start;
- 1:3's six reads hold that phase over 7,400 vblanks. With 59.94 Hz's 194.85 it would drift
  14 vblanks, which is what the first fit showed;
- the phase jumps in 2:1 and 2:2 (79.5, 6.5 and 126.1 vblanks) equal the paused time between the
  reads, mod 194.48 (81.6, 7.5, 128.0: the frame log's frames with no tick);
- 17 of the 18 reads take one to two refills (223 to 388 vblanks, against 194.5 to 389);
- the 658 also ends on the clock but waited two refills more (why is not known).

**Live play uses it** (`Engine::refill_after`): the music's start (`music_start`, at the end of
`level_start`) sets the clock. A read starts at the first refill after its request and ends at
the next one. The live engine's vblanks don't run while Godot is paused, so the pause is covered.
A level with no music would have no refills, and the original's reads would never run; the
engine then counts from vblank 0. On 2:1 (`coretest --live 2:1 --aimbot --forward -0x22DD00`) the
first read runs from tick 48 to tick 195: 294 vblanks, the first refill at 194.5, the second at 389.

**The idle jobs (1 + 189 a frame).** `frame_wait_vblank` runs idle jobs until the second vblank,
so their number depends on how long the frame's work took. 189 is the 1:3 recording's median for
frames that ran past their first vblank (2:1's is 204, 2:2's 498; a tenth of 1:3's frames ran
fewer than 78). The jobs are a three-step round robin (`0x800AB8E8`): one message each, a spawn
retry on step 1, the removed handles, a second message and `chunk_stream` on step 2, and
`chunk_stream` on step 3. Even 78 jobs run each step 26 times. So the count changes what
happens only through the round robin's phase at the next frame (the count mod 3), and on frames
whose message queue is longer than the jobs can drain (not measured). A model of the
frame's CPU time, fitted to the recorded counts against what each frame did, could at best guess
the phase. **No better model is worth it:** recorded runs take each frame's count from the
recording, and live play has no original to agree with.

The HUD line adds health, weapon, clip and reserve, and the soldiers alive and down. Testing
options:
- `--replay=build/engine/orig.log` plays the recording's pad and roles instead of the keyboard
  and mouse; the rest stays live;
- `--aimbot` turns toward the nearest living soldier within 1500 units and fires;
- `--skip=T` runs to tick T at once;
- `--shot-tick=T` saves the screenshot at tick T.

```sh
engine/godot/tools/screenshot.sh build/gd/engine_shot_1_3.png --level=1:3 --engine \
    --replay=/work/build/engine/orig.log --aimbot --skip=200 --shot-tick=372 --frames=1000000
```

At tick 371 one of 1:3's first soldiers has been shot dead and the next is in the corridor.
`coretest --live 1:3 [--replay] [--aimbot] [--hold MASK@FROM:TO]...` runs the same loop
headless. With `--replay --aimbot` the player kills the first soldier at tick 370, and by tick
1,200 has fired 42 rounds while the soldiers have shot the player down to 45.0 health.

The level's records' models are drawn at their records: objects with their static meshes, soldiers
and dogs skinned from poses the core builds every tick (a walk or their first animation, looping:
the scripts that choose animations aren't ported). The characters' textures aren't right yet: their
faces point at VRAM (640, 256), where 1:3's TAF puts the dog's skin, so the soldiers' own textures
reach VRAM some other way (not found yet).

### The front end (`--shell`)

```sh
build/godot/Godot_v4.7.2-stable_linux.x86_64 --path engine/godot/project
```

With no `--level`, `--engine` or `--scene` (or with `--shell`), the project starts in the front
end (`project/shell/shell.gd`), drawn with Godot's own controls but in the original's flow
(shell.md). The rules are the game's, ported from SHELL.BIN into `sim/campaign.cpp` and reached
through `ValorShell`:
- **Title**: CONTINUE (the campaign's next level), NEW GAME (a codename of up to 10 letters,
  then `new_game`: MSN1 LVL1 open), MISSIONS, LOAD, SAVE, PASSWORD, QUIT.
- **Missions**: the campaign's slots (`campaign_init`: MSN1–5, 7, 8, 9; MSN8 isn't on the disc)
  and the levels each has unlocked, with the best rating's stars. Choosing one runs
  `level_begin`, which also handles MSN9's third level being LVL4 and the cheats for a level not
  yet completed.
- **Password**: the 61 passwords of `password_enter`, applied to the campaign (unlocks, cheats);
  unknown and refused words say so.
- **Briefing**: the level's pages from the disc (`data\scr2\mbrief\mM\L.rsc`), each a left and
  a right 8-bit TIM of 256 × 240, shown side by side at 4:3. M is the campaign's slot + 1, not
  the mission's directory (by the pages' text: `m6` is MSN7's Mountain Pass, `m8` MSN9's Buzz
  Bomb Assembly, `m7` a near-copy of `m8` for the absent MSN8). Cross or a click turns the page;
  after the last one (or Circle/Esc) the level starts.
- **The level** runs on the engine from the campaign's save data (`start_engine_game`: the row's
  carried ammo and health, the cheats). Dying fades and restarts in place as before, and the
  presentation layer draws the engine's own fade entities (state `fade`: the black's opacity
  from the latest type 0x32 entity; `fade_start` now also keeps its end colour at `+0x24`, bytes
  only the prims use). At the level's end the engine stops (`is_level_over`) instead of going on
  by itself; the pause menu's QUIT leaves the level.
- **Results**: the level's results (`g_level_end` and the statistics at `0x80039CA8`): the
  objectives, kills, shots, hits and accuracy, hit locations, wounds and health, and
  `level_rating`'s stars. Then the post-mission choices: CONTINUE (only with the primary
  objectives done), REPLAY, SAVE and WAR ROOM. CONTINUE, SAVE and WAR ROOM after a success run
  `progress_commit` once (carry, career, medals, rewards, `campaign_advance`), and the medals it
  awards are listed. The next level is built on the carried save data (`continue_engine`).
- **Saves**: one file per save holding what the memory card file holds (0x2000 bytes: `SC`, the
  Shift-JIS title "MOH-CODENAME (n%)", `g_game` at +0x200), named as the card names them
  (`BASLUS-00974` + the codename's first six letters + a number + `.sav`). They live in Godot's
  user folder (`user://valor_saves`, on Linux `~/.local/share/godot/app_userdata/Valor/`), never
  in the recordings' `saves/`. LOAD checks them as `load_screen_update` does.

`coretest --campaign` plays the rules alone. A perfect campaign earns the medals 0xFBF, a poor
one 0x200, the passwords it tries (URLINGAMBE, CAPTAINDYE, an unknown word) do what shell.md
says, and a save loads back. `--shell-auto=N
--end-every=T` plays N levels unattended: `--shell-auto=4 --end-every=60` goes through the
briefings of 1:1, 1:2, 1:3 and 2:1 (three pages each), each level, its results and CONTINUE,
then saves "MOH-TESTER (16%)". `--shell-brief` opens the first briefing (for screenshots).
After the last slot's last level CONTINUE ends the campaign (`campaign_advance` stays put) and
returns to the title; the game shows the mission's ending film there, which isn't played.

The whole campaign through the front end: `--shell-auto=26 --shell-cheats=0xC0 --end-every=3000
--aimbot --walk=100000` (unlimited ammo and no damage, turned on with SMPSMDMILK and URLINGAMBE as
a player would) plays all 24 levels for 3,001 ticks each, 1:1 to 9:4. It shows every briefing
(three pages each) and the results, continues, and ends with "the campaign is complete", the
Bronze Star and a save. There was no error, crash or hang. Each level took 1.0 to 1.6 s of wall
time headless.

With `--aimbot --walk=100000`, `--shell-auto=3 --end-every=2700` plays 1:1, 1:2 and 1:3 for
2,701 ticks each with no error. 1:1: 64 shots, a kill. 1:2: none, its carried ammo is spent.
1:3: none, it walks into the wall ahead of its start. At `--end-every=700`, 1:2 has 6 shots and
a kill.

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
- aim_update's friction (without `--aim-log`) and the body's roll;
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

### Aiming

```sh
build/gd/cmake/coretest build/gd/pad_1720.txt --ram build/gd/cap/session/d1720.bin \
    --stream-log build/gd/cap/session/run.log --aim-log build/gd/cap/session/run.log --projection 21:9,110
```

With `--aim-log`, the pad mode runs the camera and `aim_update` in the core. The capture's
`VALOR_TRACE_AIM` lines supply what isn't ported yet, as each `player_update` finds it:
- the entities' spheres and hit parts;
- the arms model's gun bones as the last draw left them;
- the scripts' animation state.

The watch supplies the recoil (inv `+0x4C`/`+0x50`, from firing) and the camera's shake (`+0xCC`).
Compared each tick: the aim point, shot start, angular velocity, inv `+0x180`/`+0x18C`, the friction
bits and the body's pitch and yaw, plus the view matrix and the soldier's `+0x340` matrix. The
projection is the recording's (the session's settings: 21:9, FOV 110; the monkey runs': 4:3, the
original's).

| Recording | Ticks | aim_update exact |
|---|---|---|
| 1:3 (the session) | 7,179 | 7,160 |
| 2:2 (the session) | 6,654 | 6,083 |
| 2:1 (the session) | 7,854 | 1,517 |
| 1:2, the four segments of `w12` | 7,849 | all |
| 3:1, `mk` and `st` | 4,546 | all |

The misses in 2:1 and 2:2 are the ray against an object's collision box (`0x8004B0F0`, objects
with `+0x2EE` planes), which isn't ported. In 1:3 they're 19 ticks where the eye differs.

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

## The engine (`sim/`)

```sh
build/gd/cmake/coretest --engine 1:3 [--to TICK] [--max N] [--out TRACE.jsonl] [--dump TICK:PATH]
```

`valor_sim` is the C++ port of the closed-loop Python engine (`tools/sim/engine*.py` and the
simulators it composes: `level_vm.py`, `sst_vm.py`, `brain_sim.py`, parts of `combat_sim.py` and
`move_sim.py`). It covers every system the Python engine has:
- the level logic (the level-script VM, subscriptions, the message queue, triggers, spawning,
  objectives) and the vblank wait's idle work;
- the model-script VM with its world reads and effects, and the soldiers' and dogs' brains, moves
  and thinks;
- the player's update, the camera, aim_update;
- firing, bullets, the contact phase and damage, pickups, attachments, objects' boxes;
- the draw pass's poses and out-of-sight counters, and the sound system's voice table.

It runs from the level's seed dump, or from the disc files (below), on the recording's outside inputs (`build/engine/`: the pad
and role values, the random draws by function, the frame structure, the CD's chunk timing, which
models the renderer drew, the idle work). `coretest --engine` compares its state trace with the
original's every tick, as `engine.py cmp` does.

| Level | Ticks | Identical to the original | Identical to the Python engine |
|---|---|---|---|
| 1:3 | 7,296 | 7,073 (the other 223: impact-spark positions, one hit face at 1658 and 3454) | all |
| 2:1 | 8,368 | 8,308 (all but one shot's type 31, 60 ticks from 769) | all |
| 2:2 | 7,036 | 7,036 (all of it) | to tick 6,114 (Python's earlier trace) |
| 3:1 (coverage tape) | 4,525 | 3,016 (everything to 1,454) | |
| 5:4 (coverage tape) | 10,339 | 679 (everything to 680) | |
| 9:4 (coverage tape) | 9,901 | 650 (everything to 651) | |

Synced to the Python engine at 874c759; the left column is the Python engine's own at that commit.
The right column compares with the Python engine's latest traces (`build/engine/eng_*.jsonl`).
Each run parts from it only after both have parted from the original. The trace now also
carries the on-screen text list and the hit wedges' timers (`texts`, `hud`), which agree
wherever the entities do. Since eb3c158:
- the SPU's key status comes from the recording (`keys` lines, `SpuGetAllKeysStatus`). A
  soldier's script reads it (variable 0x68), so 2:2 runs to 3,479 (was 810);
- the level scripts' sounds (built-in 19) are played;
- the node search (op 33 compute 0), op 34 `node_eval` and `brain_node_target` for the node
  goal are ported;
- the text list (`texts.cpp`: `text_add`, `texts_update`, `show_text`, the pickups' messages,
  the ammo digits, `ammo_hud_init` at level load, `texts_clear` at level entry) and
  `hit_indicator` with the wedges' countdown;
- the alarm (`alarm.cpp`: op 49 and `level_set(4)`, `alarm_spawn`, `alarm_think` on the vblank
  counter, `alarm_destroy`), and looping sounds restarting (`sound_restart`);
- the recording's chunk-slot changes apply to the tick drawn at their vblank (they happen in the
  idle work before the drawing pass), so 2:2 runs to 5,359;
- the recorded runs allocate on the seed's own heap too, with the original's refusals when it is
  full (2:2's explosion(5) at tick 5379 makes nothing); pickups and destructibles take
  `model_instance_init`'s block (flags 0x157), attachments' models theirs, and the characters'
  destructors release their attachments' and parts' instances two frames late. 2:1 runs to 8,308;
- level explosions 0, 5, 7 and 8, and witnesses picked by flag variable 0x109 and tested within
  the victim's long range: 2:2 runs to 6,114;
- scoped shots from the camera toward its look point: 2:2 is identical throughout;
- the muzzle flashes' countdown and the player's held-model brightness (enemy_draw, player_draw),
  the scorch flag cleared each drawn frame;
- other recordings: `ENG_INPUTS=DIR coretest --engine M:L` reads DIR's orig.log, orig.jsonl and
  seed/ (the dump at the level's first tick's vblank + 1), and its frames, timing and drawn logs
  when they're kept apart. A level's trace lines start 4 vblanks before its mark (the mark can
  come a vblank after the first tick). The coverage tapes in `build/engine/in_m3l1`, `in_m5l4`
  and `in_m9l4` give the rows above, the Python engine's own first runs.

### Why it keeps the original's memory layout

The engine's state is an 8 MB image laid out as the original's RAM (`sim/include/valor/sim/common.h`,
`Mem`). Entities, their objects, brains, the script channels and event tables, the level's records
and scripts live at the addresses the game used, and the code reads and writes them at the
documented offsets (`m.s32(e + 0x20C)`), line for line as the Python does.

That's deliberate:
- **The rules carry over unchanged.** `docs/re` and `tools/sim` state the rules against that
  layout. A port that kept the layout is a transliteration, checkable line by line. A port to new
  types would mean re-deriving each rule's data flow first.
- **Two engines that must agree byte for byte.** The C++ and Python engines can be dumped at any
  tick and compared word for word (`--dump`). That's how this port was checked, and it makes every
  later Python fix a small, verifiable diff to apply here.
- **Values hide in the layout.** Scripts store addresses in variables, event records are copied
  instructions, handles and pointers are compared, and pose buffers are shared. A typed model
  would have to reproduce all of that anyway to stay exact.

### From the disc files (`sim/src/load.cpp`)

```sh
build/gd/cmake/coretest --build 1:3 --against build/engine/seed/d1713.bin [--show N] [--out BUILT.bin]
build/gd/cmake/coretest --engine 1:3 --disc
```

`Engine::load_disc` builds the level's starting state from the disc files and the save data the
shell hands over (`g_game`, 0x780 bytes). It doesn't need a RAM dump. It runs the original's
sequence in order, on an exact port of the exe's heap (`Heap`: InitHeap, malloc, calloc, free),
so every block lands where the original put it:
- level_entry: level_read_parameters and the pools;
- level_load: the TAF's sound banks, the BSD read and relocated, models inherited, weapons
  matched to slots, the world entity with the TSP files' CD entries (LBAs from
  `extracted/moh/manifest.json`), records, zones, nodes, paths, the message lists, the camera and
  the player's soldier with its controller configuration;
- level_start: player_reset with the exact heap's weapon swap, the scripts' start messages, the
  sky and stars on PsyQ's real `rand`, the first chunks loaded (tsp_parse, collision headers),
  music_start, the fade.

`coretest --build` compares the result with a dump region by region: the exe, the overlay, the
exe's bss, and the heap block by block, matched by allocation serial. The reference is `d1713`,
the original's RAM during level_start. It was captured mid-`music_start`, between the two SPU
transfers, so the music ring and the fade differ by construction. For 1:3:
- all 26 of the original's heap blocks are built at their addresses and sizes;
- 15 blocks match word for word, among them the BSD (162,308 words), the primitive pool, the CD
  entries, the first chunk slot and the music entry;
- the other blocks differ only in bytes the game never wrote: past each chunk file's end, block
  tails, and the old contents of a reused pose buffer. Drawing primitives and the HUD also
  differ.

What's left in the static regions is outside the logic. The exe has 630 differing words (PsyQ's
CD, pad, GPU and SPU state). The overlay has 10,714 (the music ring mid-transfer, HUD and draw
primitives, display environments). The exe's bss has 239 (libspu, libgpu).

`coretest --engine --disc` runs the engine from that built state. It takes three things from the
recording, as it takes the rest of its inputs:
- the pad's analog mode at the first read: `player_update` then reconfigures the controls, as the
  original does on tick 1;
- the vblanks the load took: the vblank timer's 20-vblank message counts them;
- `rand`'s seed at entry: the dump's seed, stepped back over the build's own draws.

From the disc it matches the seed-dump run exactly: **7,073 of 7,296 ticks identical** (at
d337b74: 5,632, as the seed run then). RAM differs from the seed run by a constant 35,996 words at ticks
1000..1003: the unbuilt library and HUD state above, which nothing in the logic reads.

2:1 from the disc matches its seed run: 8,308 of 8,368. Before the seed runs used the game's heap,
only the disc run agreed at the impact of tick 6280; now both do. 2:2 has no dump from before its tick 1, so
the timer and `rand`'s seed come a tick late from the seed: 0 identical (6,113 from the seed dump),
first difference at tick 2 for both.

### The view test

Whether an entity is drawn matters to the logic, not only to the picture. It decides whether a
soldier's pose is rebuilt off screen, and whether an out-of-sight counter runs down to the
entity's removal (and its record's message 2). Live play uses the original's test
(`entity_in_view`, `sim/src/anim.cpp`):
- `frustum_build` (`0x8008D0CC`) is ported: the six directions are built from the camera's eye,
  look and side points, the view rectangle and the projection distance;
- `box_in_view` (`0x8008E5B0`) takes the corner of the collision sphere's cube farthest along each
  direction and projects it through the camera's view matrix;
- an entity is out of view when that corner is behind the eye, left of x 0 or right of x 540,
  above y 0 or below y 240, or past the far distance.

The directions computed this way equal the ones in the original's view block at all seven 1:3
dumps (21 of 21 entities' results).

The projection is the recording's: 21:9 with a 110° horizontal field of view, the rectangle
widened as Valor's runtime widens it (1.1 / the projection's scale). This doesn't depend on
Godot's camera. The tradeoff: if a player widens Godot's field of view past the recording's,
an entity near the screen's edge can be on screen in Godot but out of the engine's view. Its
pose then holds, and if it's a soldier or an object its out-of-sight counter runs, as it would
for the original's narrower view.

The recordings can't check the test: their drawn flags came through Valor's runtime, which ran
the view test against its own presentation camera (interpolated, with its mouse-turn reach).
In the recorded runs the engine still takes the drawn flags from the recording. It also counts
how often its own test agrees: 1:3 25,905 agree, 19,123 differ.

### The game's heap at run time

From the disc the engine keeps using the exe's allocator (`GameHeap`, load.h) after the start:
- entities, brains and projectiles' models are allocated through it;
- model swaps allocate pose buffers exactly as `model_instance_init` does;
- the destructors' frees are emulated: brains are freed at once, while entity blocks and pose
  buffers are freed two frames late (`0x8008A4F8`, counted down at each frame's start by
  `0x8008A580`).

`coretest --engine 1:3 --disc --heap TICK:DUMP` compares the heap block by block with the
original's RAM. All eight dumps of the session match on every block's address, size and
allocation serial (ticks 207, 4675, 5000, 5300, 5600, 5823, 6402, 6943: 32 to 62 blocks each).
It took one fix: a pickup's entity block has room for its model's hit parts, which it then
drops. With the instance blocks of pickups, destructibles and attachments
(332ca96's sync), the 21 RAM dumps of 1:3 in the session's capture
(`build/simcheck/2026-10-19_22-22-43_m1l3`, ticks 53 to 6993) all match on every block, 29 to 51
blocks each.

### Clean types for the host (`sim/include/valor/sim/views.h`)

The host doesn't see offsets. The accessor layer is thin read-only views over `Mem`: `Entity`,
`Instance` (the pose: bone quaternions and joints for skinning), `Player` (the inventory and the
view), with the entity list, the soldier and the camera. Each structure's fields are one table
(name, type, offset, what it is) that generates the accessors, so the names have a single source
(entities.md, animation.md, movement.md). The engine's own state trace is read through them
(`Engine::state_line`), and the trace numbers above are unchanged by it, so the views are checked
every run. The Godot extension uses them to place and pose the actors. Input goes in as it does
in the recordings: the pad words and the control roles' values per tick.

## What's next

1. **Live play**: the chunk reads on the music's refill clock (above); the shell's options
   (controls, audio) and the pause menu; the war room's medal case.
3. **Presentation**: the first-person weapon, pickups and effects (tracers, explosions' particle
   systems), sounds and music from the engine's voice table, the HUD.
4. **2:2 from the disc**: a dump from before its tick 1 (the capture's earlier vblank), so its
   timer and seed come in on time.
5. Keep syncing the Python engine's commits as small diffs (each with the Python engine's numbers).

## Coordination

Notes between the workers on `engine/godot/`:

- **The presentation layer** (`godot/present/`, the autoload `Present`; its README) draws the HUD,
  the first-person weapon, the sky, the particle effects and the sound from `ValorGame.get_state()`
  and the read-only views. It reaches into `main.gd` in three places, so changes there should keep
  them working:
  - **It sets `main.cam.fov` every frame** for the zoom (the projection distance: `fov` from
    `main.cam.fov`'s first value, narrowed by 400 / `zoom`). Camera code shouldn't set the fov
    after `_ready`, or the two will fight; if the camera wants to own the zoom, take it from
    `get_state().zoom` there and tell present/ to stop.
  - It reads `main.prev_yaw`, `cur_yaw`, `prev_pitch`, `cur_pitch` (the view the ticks left, before
    the mouse's unsent turn) to place the weapon, and `main.cam` to place billboards.
  - `main.gd` adds `Present.pad_input()` (the gamepad) to its own input each tick.
- **The explosion light** (objects.md, Explosions: the latest explosion's coloured point light,
  `0x800AA188`) tints the world in the original (a recomp frame of a grenade in 1:3 shows the
  tunnel orange). The world shader (`world.gdshaderinc`) has no point light yet; present/ can feed
  one (position, colour, radius from the engine) once the shader takes it.
- **The PVS** (present/world_pvs.gd, world.gdshaderinc's `pvs_vis`): present/ replaces
  `main.world.mesh` with a copy carrying each vertex's leaf id in CUSTOM0, and re-applies
  `main.apply_materials(world)` on it; it redoes that whenever `main.world.mesh` changes. It relies
  on `ValorGame.build_world_mesh` taking every chunk's faces in RenderChunk::parse's order into the
  five surfaces by blend; if the world mesh changes shape (per-chunk meshes for streaming, say), it
  finds the faces don't match and turns the PVS off. If you'd rather own it, the cleaner place is
  `build_world_mesh` adding the leaf id itself (ValorAssets.pvs_load has the walk); tell present/
  and it will only fill the texture.
