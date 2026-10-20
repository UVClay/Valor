# Reverse engineering: the game's mechanics

What the game does, system by system, written to be reimplemented from: player state, entities,
combat, enemies, levels. Each document says how its claims were checked.

| Document | Covers |
|---|---|
| [player.md](player.md) | The soldier's inventory, the weapon table, carry-over between levels, cheat flags |
| [combat.md](combat.md) | Firing, bullets (projectile entities), hit tests |
| entities.md (to come) | The entity list, entity layout, think/touch handlers, physics |
| enemies.md (to come) | Perception, states, targeting, accuracy, damage |
| levels.md (to come) | Level data, scripts, objectives |

`names.txt` holds every named address (functions and data), read by the tools.

## Tools

Run inside the container (`./dev ...`).

- `tools/recon/fn.py` disassembles whole functions with labels, named calls, resolved
  addresses and the strings they point to (`show ADDR`), and searches: `callers`, `calls`,
  `uses ADDR` (code building an address), `offset OFF...` (functions using these load/store
  offsets), `grep REGEX` (instructions).
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
