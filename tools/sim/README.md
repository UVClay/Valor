# Reference reimplementations

Each script here rebuilds one of the game's systems from the documents in `docs/re/` alone and
steps it against the original, tick by tick. Agreement means the documents are complete enough
to rebuild that system exactly; every disagreement found so far was a gap or an error in a
document, now fixed there. They are test tools, written for clarity, not speed.

| Script | System | Checked against the recorded session (1:3, 2:1, 2:2) |
|---|---|---|
| `move_sim.py` | The player's movement: input to velocity, turning, crouch, jump, gravity, floor search, steps, falls, walls, ladders, aim lean | Every tick exact to the 16.16 bit, free-running (20,000+ ticks, plus scripted runs) |
| `sst_vm.py` | The behaviour-script VM of soldiers and dogs (74 opcodes, channels, events, waits) | 95 of 95 thinks over 51 tick pairs agree with the next tick in every variable and channel |
| `brain_sim.py` | Enemies' brain and move: seek turns, paths, points, wall steering, root motion, floor | Positions exact in 69,638 of 69,640 enemy ticks (the misses are script inputs not watched) |
| `level_vm.py` | Level logic: the level-script VM and built-ins, subscriptions and filters, the deferred queue, record state and activation, zones and triggers, objectives | Every frame exact (21,676) in records, variables, subscriptions, the queue, objectives and level end, but two single frames of zone bits |
| `combat_sim.py` | Poses and hit spheres, the trigger, reloads, shots, spread, bullet flight, hits, damage | Poses exact; hit spheres 99.9 %; which entity and part each bullet hits 96 % exact, 4 % within a sphere's edge |

## Checking a recording: `check.py`

    python3 tools/sim/check.py RECORDING [--levels M:L,...] [--only move,sst,brain,combat,level] [--jobs N]

replays the recording (in the dev container, `chk_*`), gathers everything below, runs all five
per level and prints one table: system, level, compared, exact, within (a documented range or
rounding), fail, verdict. The levels are the ones the replay plays (a new one wherever the tick
counter starts again, named from RAM), so a replay that parted from its recording is still a
valid check. The verdict compares with `baseline.json`: the recording's own results when it has
an entry (`--save-baseline` writes one), else the recorded session's agreement for that system.
It exits 1 on any FAIL. Everything is kept in `build/simcheck/NAME/` (reports in `out/`); a second
run only runs the simulators (`--replay-again` replays too). Needs a runtime that takes
`--dump-ram` more than once and `--stop-at` (`--bin`, default `build/cmake/valor`).

The recorded session, to 2:3's start (version 1 recordings part from the recording headless in
2:3): about 4 minutes the first time, 20 s after:

    python3 tools/sim/check.py recordings/2026-10-19_22-22-43_m1l3 --levels 1:3,2:1,2:2

Its results are the table above, but for the script VM: `check.py` picks its own 20 tick pairs
per level (106 thinks), one of which (2:2) still differs: an animation instance's blend field.

## How they are fed

The original's side comes from a replay of a recording (`tools/recon/run.sh NAME SECONDS LOG
--replay recordings/...`), with:
- `VALOR_TRACE_STATE=FILE`: every entity's state after every tick (runtime/src/watch.cpp);
- `VALOR_WATCH`: the fields a system reads, logged when they change (docs/re/README.md, Tools);
- `VALOR_TRACE_SHOTS`, `VALOR_TRACE_DAMAGE`: shots and damage events;
- a RAM dump per level (`--dump-ram`) to seed what the watches only log as changes.

The inputs a system takes from outside (the control function's output for the player, the
scripts' decisions for the brain, random draws) come from those logs; the system's own rules
are what the comparison checks. Each script's docstring gives its command line.

## Using them for a new engine

They are the shortest exact statement of each rule, next to the prose in `docs/re/`. A new
engine's version of a system can be checked the same way: feed it the same inputs, write the
same state, compare (`tools/recon/trace_cmp.py` for traces in the `VALOR_TRACE_STATE` format).
