# Reverse engineering: the game's mechanics

What the game does, system by system, written to be reimplemented from: player state, entities,
combat, enemies, levels. Each document says how its claims were checked.

| Document | Covers |
|---|---|
| [ENGINE.md](ENGINE.md) | **Start here**: how the documents fit together for rebuilding the game, the units and conventions, the tick |
| [player.md](player.md) | The soldier's inventory, the weapon table, carry-over between levels, cheat flags |
| [combat.md](combat.md) | Firing, bullets (projectile entities), hit tests |
| [entities.md](entities.md) | The entity list and layout, handlers, the tick, movement, collisions, hitboxes |
| [enemies.md](enemies.md) | Scripts (the bytecode VM), variables and sensors, the brain, firing and aim error |
| [soldier.md](soldier.md) | The enemy soldier's behaviour scripts read into plain behaviour: alerting, combat, cover, grenades, alarms, 2:1's disguise |
| [enemy_types.md](enemy_types.md) | Every actor with a brain across the game (soldier, dog, mounted gun), each level's soldier script set and what it adds, witnesses, script events |
| [levels.md](levels.md) | Level files, the level header, entity records, spawners |
| [messages.md](messages.md) | Every level message id: who sends it, who handles it, what it means; the subscription filters and context slots |
| [animation.md](animation.md) | Skeletons, animation frames, playback and blending, root motion, hit spheres from the pose |
| [movement.md](movement.md) | The player's movement and camera: speeds, crouch, jump, steps and falls, turning, aim mode, use, death |
| [formats.md](formats.md) | Every file type on the disc (BSD, TSP, TAF, VB, RSC, TIM, STR) and what a new engine needs to load a level |
| [audio.md](audio.md) | The sound driver and voices, banks, what plays when, 3D volume and panning, music streaming; how soldiers hear |
| [shell.md](shell.md) | The front end: campaign flow between SHELL and LEVEL, the save format, options, passwords and cheats, medals and unlocks, menus |
| [objects.md](objects.md) | The random number generator, the order of a tick, explosions in time, grenades, doors, destructibles, the mounted gun, world animations, the remaining entity types |
| [hud.md](hud.md) | The game state the HUD and screen effects show (health ring, hit wedges, compass, ammo, texts, reticle, scope, shake, fades) |
| [catalogue.md](catalogue.md) | Every level's contents: records, entity types, scripts, objectives, features; and what no document covers yet |

`names.txt` holds every named address (functions and data), read by the tools, with the PsyQ
library names in `build/syms` (from the setup tools) as a fallback.

## Other sources

- [Medal-Of-Honor-PSX-File-Viewer](https://github.com/AdrianoDiDio/Medal-Of-Honor-PSX-File-Viewer)
  (GPL-3.0): readers and documentation for the file formats (TSP, BSD, RSC, TAF, TIM, VB, SST),
  a level viewer with OBJ/PLY export, a model viewer with animations. It covers the files; these
  documents cover what the game does with them. Its code can't be copied into Valor (GPL-2.0);
  formats are described here in our own words. Clone it into `build/ext/` to consult it.

## Tools

Run inside the container (`./dev ...`).

- `tools/recon/fn.py` disassembles whole functions with labels, named calls, resolved
  addresses and the strings they point to (`show ADDR`), and searches: `callers`, `calls`,
  `uses ADDR` (code building an address), `offset OFF...` (functions using these load/store
  offsets), `grep REGEX` (instructions), `sortnames`. `c ADDR` decompiles a function with
  [m2c](https://github.com/matt-kempster/m2c) (clone it into `build/m2c`), typed by `context.h`
  (the entity, brain and weapon structs, prototypes); the recompiler's split-off pieces are
  merged back. GTE instructions come out as `M2C_ERROR` lines: read those in `show`.
- `tools/recon/sst.py RAMDUMP --type 1402` disassembles an entity's model scripts.
- `tools/recon/lvs.py RAMDUMP [ID...]` disassembles the level's scripts, folded into
  statements (`--raw`: one instruction per line).
- `tools/recon/tspcol.py FILE.TSP...` checks TSP chunks' collision blocks; `--obj` exports the
  triangles.
- `VALOR_TRACE_DAMAGE=1` logs every damage event (victim, attacker, damage, health).
- `VALOR_TRACE_STATE=FILE` writes the game's state after every tick as JSON lines (the player's
  look and weapon, every entity's handle, type, flags, position, angles, health, animation and
  frame, all exact); `tools/recon/trace_cmp.py A B` reports where two traces first differ.
- `VALOR_TRACE_SHOTS=1` logs every player bullet with the aim point and the camera;
  `tools/recon/shots.py LOG` measures them.
- `VALOR_WATCH="name=EXPR:LEN;..."` makes the runtime log, every game tick, what changed in
  those bytes (EXPR: hex, `[EXPR]` to follow a pointer, `+HEX`/`-HEX`), e.g.
  `VALOR_WATCH="inv=[8009D654]+3CC:80"`. `VALOR_WATCH_HALF=1` logs signed halfwords.
- `--dump-ram FRAME:PATH` and replays of recordings (`./play --replay DIR`) give RAM at known
  moments of real play to compare against.

## Method

1. Find the data: compare RAM dumps of known situations (natural and warped starts, before and
   after an action) for values the screen shows.
2. Find the code: functions using those offsets or addresses (`fn.py offset`, `uses`, `grep`).
3. Read it, name it (`names.txt`), and write down what it does.
4. Check: watch the fields while scripted input (`--kbm`, `--press`, `--mouse`) exercises them,
   or replay a real session and compare.
