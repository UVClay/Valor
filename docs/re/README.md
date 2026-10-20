# Reverse engineering: the game's mechanics

What the game does, system by system, written to be reimplemented from: player state, entities,
combat, enemies, levels. Each document says how its claims were checked.

| Document | Covers |
|---|---|
| [player.md](player.md) | The soldier's inventory, the weapon table, carry-over between levels, cheat flags |
| [combat.md](combat.md) | Firing, bullets (projectile entities), hit tests |
| [entities.md](entities.md) | The entity list and layout, handlers, the tick, movement, collisions, hitboxes |
| [enemies.md](enemies.md) | Scripts (the bytecode VM), variables and sensors, the brain, firing and aim error |
| [levels.md](levels.md) | Level files, the level header, entity records, spawners |

`names.txt` holds every named address (functions and data), read by the tools, with the PsyQ
library names in `build/syms` (from the setup tools) as a fallback.

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
- `VALOR_TRACE_DAMAGE=1` logs every damage event (victim, attacker, damage, health).
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
