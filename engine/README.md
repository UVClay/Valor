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

### Packages (`engine/godot/tools/package.sh`)

`engine/godot/tools/package.sh` makes runnable packages in `build/dist/`, `valor-linux-x86_64/` and
`valor-windows-x86_64/`, with no disc data in them. The player gives their own image on the first
run (each package's README.txt says how).
- **Templates:** the official Godot 4.7.2 export templates, downloaded into `build/godot/` and
  checked against the release's `SHA512-SUMS.txt` (the `.tpz`'s SHA-512 matches; the saved sums
  file equals the release's).
- **Libraries:** both GDExtensions as `template_release`. Linux is built in the dev container and
  links the system libstdc++. Windows is cross-built with MinGW-w64 (`Containerfile.win`,
  `mingw64.cmake`; static libstdc++); both DLLs import only `KERNEL32.dll` and `msvcrt.dll`.
- **Export:** the two presets of `export_presets.cfg`. Each pack is exported again as a `.zip`
  and listed to check that only the project's own files went in.

Numbers:
- Each pack is 40 files (102,178 bytes): scripts, shaders, the scene and Godot's own caches. Linux:
  `valor.x86_64` (the official template, 73.5 MB), two `.so` (2.2 MB, 1.1 MB), `valor.pck`
  (105 KB). Windows: `valor.exe` (109 MB), two `.dll` (3.2 MB, 1.2 MB), `valor.pck`.
- Tested headless from `build/dist/valor-linux-x86_64/` with a fresh user folder and `extracted/`
  hidden: `--disc-image=... --shell-auto=3` plays 1:1, 1:2 and 1:3 through the front end, saves,
  and quits, with no error.
- The Windows package runs under Wine, headless, with the image given as a Windows path
  (`Z:\...\Medal of Honor (USA).cue`): the same three levels, results and save. Every level ends
  at the same place with the same shots as the Linux package's (1:1: 40 shots, 3 hits, a kill).

An exported package doesn't fall back to the source tree's `extracted/` (feature `template`).

## Running

```sh
build/godot/Godot_v4.7.2-stable_linux.x86_64 --path engine/godot/project -- --level=1:3
```

Before the first run, `--headless --import` once registers the extension.

**The game's files come from the user's own disc image, read in place** (`core/include/valor/disc.h`).
Point it at the `.cue` or `.bin` of Medal of Honor (USA) with `--disc-image=PATH`, or choose it in
the front end: the first run asks (YOUR DISC: a path or BROWSE), and OPTIONS > DISC changes it.
The path is kept in the settings (`user://valor_settings.cfg`).
- The reader walks the image's ISO9660 file system on raw 2352-byte Mode 2 sectors once, at
  mount (about a second), and checks for `SLUS_009.74`.
- `read_file` then serves `disc:/...` paths from it: Form 1 sectors' 2048 bytes, the music's
  `.VB` streams among them. A file with Form 2 sectors (the STR movies' video and XA audio) reads
  as its raw sectors, as `tools/extract_disc.py` writes them.
- The loader takes the files' LBAs from the image instead of `manifest.json`.
- Nothing is copied into the project.

`--data=DIR` (an extracted `DATA` folder) and the source tree's `extracted/moh/DATA` still work, for
development. `coretest` takes the image with `VALOR_DISC=PATH`.

Checks:
- `coretest --disc-image IMAGE` compares every file of the image with the extracted copy: all 704
  files are identical (694,669,544 bytes; the 22 STR files raw).
- With `extracted/` hidden behind an empty folder, `VALOR_DISC=... coretest --engine 1:3 --disc`
  gives 7,073 identical ticks, as from the extracted files. The live 1:3 replay dies at 2613 and
  restarts at 2701, and `--live 1:1 --end-every 300` goes through all 24 levels.
- In Godot from the image (`extracted/` hidden), the 1:3 `--replay` frame at tick 372 matches the
  one from the extracted files except 41 pixels of the compass's needle (the frame's
  interpolation).
- The front end plays 1:1 and 1:2 from it with the presentation layer's textures and music.
  Present's library mounts the image too (`ValorAssets.mount_disc`; main.gd passes it in
  `args`).

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
  game's does;
- the game frame still runs paused, as the original's does (`g_paused`, objects.md): every 30 Hz
  frame `Engine::live_paused_frame` draws (the out-of-sight counters run down and can remove
  what they count out), does the idle work (messages, spawn retries, handles freed) and holds the
  1/3-second timer at 0, with no tick. The music's refill clock stands still, as the paused
  music voice does. `--pause-at=TICK --resume-after=N` resumes after N paused frames (headless:
  90 paused frames at 1:1's tick 200, then on); `coretest --live M:L --pause TICK:N` the same.

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
  `coretest --live 1:3 --replay`: the player dies at tick 2121 (2613 before the soldiers threw
  grenades and got auto aim's larger hit parts), the crouched death animation plays (var 2 =
  30), and the level restarts at 2209 at
  full health with its statistics zeroed.
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

### Live play over a long session

What the recordings can't show, checked live:
- **The heap over a long session.** `coretest --live M:L --heap-every N` prints the exe heap's
  free and used bytes, its largest free block and fragments, and the handle slots. Over 40,000
  ticks (22 minutes) of 7:1 with 17 deaths, every restart gives the heap back to the same
  129,344 free bytes; the handle slots' high-water mark climbs about 7 a restart (the removed
  entities' handles stay taken until the idle work frees them), reaches 256, and the handle
  allocator (`Engine::handle_alloc`) then takes free slots from 0. 1:1, 2:2, 3:3, 5:4 and 9:4
  stay flat over the same run.
- **Saving and loading between levels.** `coretest --campaign` saves after every level of a
  perfect and a poor campaign, loads the file into a fresh campaign (a power cycle) and checks that
  CONTINUE starts the same level on the same save data as playing straight on: the same at every
  level. (The game saves only between levels; the level is then built from the disc on that save
  data. The rand seed and the timer, carried when playing on, start afresh, as after a boot.)
- **Pausing**: above (the pause menu).

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
the next one. Paused frames hold the clock (and a read waiting on it), so the pause is covered.
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

**GPU skinning.** The engine's characters are skinned by Godot on the GPU. The game's skinning is
rigid (each vertex follows one bone: `R_b v + joint_b`, `skin_vertices`), so a character's model
becomes one mesh in its bones' own space, each vertex bound to its bone at weight 1
(`build_skin_mesh`, cached per model). A `Skeleton3D` of one parentless bone per model bone and a
`Skin` of identity binds come with it (`setup_skeleton`). Each tick `pose_skeleton` sets every bone
from the engine's pose: the quaternion's matrix in Godot's axes and the joint. Before, main.gd
rebuilt every character's mesh every tick. `--cpu-skinning` keeps that path.
- `--skin-check` compares the two at `--shot-tick`, corner for corner. At 1:3's tick 372 the three
  soldiers (762, 546 and 762 corners) differ by at most 1.71 game units: the fixed-point path
  truncates each axis by up to one unit, √3 = 1.73.
- `--tick-log=MS` prints the ticks slower than MS, split into the engine, the actors' update and
  the rest. In the 1:3 replay the engine's tick takes about 0.1 ms. Over 7,000 headless ticks only
  the soldiers' first appearance passes 2 ms (3.2 ms: their meshes and skeletons built). With
  software rendering at 1280×720 the worst tick was 6.5 ms (6.4 on the CPU path).
- The 25 ms spikes present/README measured (2560×1440, `--perf`, the tick less its parts) didn't
  show up here.

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

| Recording | Restarts | From its seed dump | From the disc files |
|---|---|---|---|
| 1:3 (session) | 0 | 7,296 of 7,296 | 7,297 of 7,297 |
| 2:1 (session) | 0 | 8,368 of 8,368 | 8,369 of 8,369 |
| 2:2 (session) | 0 | 7,036 of 7,036 | 7,037 of 7,037 |
| 1:1 (coverage tape) | 4 | 10,614 of 10,614 | 10,619 of 10,619 |
| 3:1 (coverage tape) | 0 | 4,525 of 4,525 | 4,526 of 4,526 |
| 3:3 (coverage tape) | 2 | 10,428 of 10,428 | 10,429 of 10,429 |
| 3:4 (coverage tape) | 3 | 10,692 of 10,692 | 10,693 of 10,693 |
| 4:1 (coverage tape) | 2 | 7,250 of 7,250 | 7,251 of 7,251 |
| 4:3 (coverage tape) | 3 | 10,247 of 10,247 | 10,248 of 10,248 |
| 5:1 (coverage tape) | 2 | 10,679 of 10,679 | 10,680 of 10,680 |
| 5:4 (coverage tape) | 10 | 10,339 of 10,339 | 10,340 of 10,340 |
| 7:1 (coverage tape) | 6 | 10,505 of 10,505 | 10,510 of 10,510 |
| 7:3 (coverage tape) | 7 | 10,443 of 10,443 | 10,444 of 10,444 |
| 9:4 (coverage tape) | 5 | 9,901 of 9,901 | 9,902 of 9,902 |

All of it, every one, from the seed and from the disc (a disc run also compares tick 1, which the
seed's dump already holds). Synced to the Python engine at 3337d69, which is whole on the same
fourteen. The restarts are the engine's own: after a death the level restarts in place
(`level_restart`: every entity but the soldier, the camera, the world and type 8 removed, their
handles kept until the idle work frees them, and `level_start` built as from the disc), where the
Python engine reseeds from the dump after each restart (`seed/r*.bin`). Every respawned entity is
checked this way:
- soldiers and dogs with `model_instance_init`'s flags and hit parts (0x85 / 0x5, 0x285 / 0x205
  with auto aim, which doubles the parts' radii);
- the mounted gun (`mounted.cpp`) and 9:4's V2 (`obj1774_spawn`, `0x800905C0`: its object of two
  particle systems, the 0x3CC-byte block it takes from the heap).

**The regression test**, `engine/tools/suite.py`, runs every recording (the session's three and
every tape `build/engine/in_m*l*`) from its seed and from the disc, in parallel (about 30 seconds
for all 24), prints the table, and fails when any run falls below the counts it has reached
(`engine/tools/suite_expect.txt`; `--update` raises them). The other tapes, first runs in the
Python engine too, part where it does (1:2 at 2045, 3:2 at 2632, 7:2 at 8304, ...).

The disc-built starts needed:
- tick 0's recorded draws from the level's load on (`level_load` too, not only `level_start`: the
  soldiers' first script picks in 5:4 and 9:4), less the shell's own, whose code sits at the same
  addresses: the draws from the dump's vblank less the load's vblanks (`0x8009A5C0`);
- the load's entities as its own logic left them (2:2's soldier: an event the level script
  raised at the start, which the engine's adopt had cleared);
- the start's spawns on the exact heap (2:2's soldier's 0x140-byte brain);
- the dump before the first tick (below), and the frames' timer counted from its vblank.
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
- crouched sight queries read the view object's first word; the recording's own projection
  scales the GTE (the tapes ran at 4:3 and the original FOV); soldiers throw, lob and drop their
  grenades (0x800523E4); a bullet hole fitted to its face moves the impact point (the decal's
  fitters; the face record carries the face's vertices and d). That closes the last session gaps
  (1:3 tick 1658, 2:1 tick 769);
- the mounted gun worked by its gunner, rockets (the bazooka's and the Panzerfaust's), the
  queued key-ons kept in RAM (`0x80090FC8`), queries with no target reading address 0's
  fields, a seed's sound update run when its dump came before it;
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

From the disc: **whole**, 7,297 of 7,297 ticks (the extra one is the disc run's first tick). RAM
differs from the seed run by a constant 35,996 words at ticks 1000..1003: the unbuilt library and
HUD state above, which nothing in the logic reads.

The save data, `rand`'s seed and the timer come from the last dump before the level's first tick
(`d1713`; `d20494` and `d40734` for 2:1 and 2:2, captured again from the session's replay:
`valor-gd --replay ... --dump-ram V:PATH`, identical to the old dumps where they overlap). 2:2's
first tick ran over two vblanks, so its dump is two before the seed's. Without such a dump the
seed itself stands in, a tick late.

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
4. Keep syncing the Python engine's commits as small diffs (each with the Python engine's numbers).

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
- **Frame time** (present/README.md, Frame time): in a 1:3 live run the presentation costs about
  0.3 ms a tick and 0.2 ms a frame. main.gd's tick averages 1.7..2.4 ms with spikes to 25 ms;
  `update_engine_actors` builds a new mesh for every skinned actor every tick (31 µs each, 3
  actors here): skinning on the GPU (bone matrices to a skinned mesh, or the pose as a texture)
  would take that off the tick as more characters show.
- **Level changes**: present/ rebuilds itself when `game.get_level_info()`'s level or `main.world`
  changes; no signal is needed, but if main.gd ever reuses one level's `world` node for another
  level with the same numbers, emit something and present/ will listen.
