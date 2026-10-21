# Rebuilding the game: an overview

How the pieces documented in this folder fit together, for an engine that reproduces Medal of
Honor's mechanics exactly and draws everything its own way. Each section points at the document
with the details; nothing here is new, only arranged in the order an engine needs it.

## Units and conventions

| | |
|---|---|
| Distance | World units. The player runs 34.86 units per tick; the eye is 240 above the feet standing, 80 crouched (movement.md) |
| Axes | x and z horizontal, **y down**: floors' normals have y < 0, a jump starts with vertical speed −26 (movement.md, levels.md Collision) |
| Positions | 16.16 fixed point (entity `+0x20C`), integer part mirrored at `+0x354` (entities.md) |
| Angles | 4096 per turn, stored 16.16 (entity `+0x228`: pitch, yaw, roll) |
| Time | The game logic runs in **ticks of 1/30 s**. The console's vblank is 1/60 s; recordings and dumps count vblanks |
| Health, damage | 16.16; weapons do flat damage per hit, a soldier left under 10% of his maximum health dies, and a damaging hit on a soldier's head kills outright unless one of two helmet models is knocked off, 50% of the time (combat.md) |
| Random numbers | One PsyQ `rand` (LCG) shared by gameplay, sound and effects; matching its sequence isn't practical, match the distributions (objects.md) |

## The tick

objects.md, "The frame and the tick", has the full order. In short, every tick:

1. Input is read (a fresh read for each tick).
2. Every entity thinks and moves, in list order: the camera first, then entities spawned from
   records (newest first), the world, the player's soldier, projectiles last. The walk reads each
   entity's `next` before its think: an enemy's bullet moves in the tick it was fired, the
   player's waits at the muzzle until the next tick unless another projectile was alive.
3. Then all contacts and hits are resolved (entities.md, combat.md).
4. On-screen texts update; a periodic message 0x59 goes out about three times a second.

Level-script messages are **queued and run after the frame is drawn**, so their effects land
one tick later; spawn retries and removed entities' handles are handled there too. The original
runs as many ticks per drawn frame as vblanks went by (up to 4), so it keeps 30 ticks per
second of real time; an engine with a fixed 30 Hz tick and the message queue flushed after each
tick reproduces it.

## Loading a level

formats.md, "Loading a level in a new engine", is the checklist: the BSD (level header, models,
entity records, zones, level scripts, AI nodes and paths, lights, sky and fog), the TSP chunks
(render geometry with vertex colours and PVS masks, animated faces, collision), the TAF
(textures into fixed VRAM places, sound banks; the `0`/`1` file differs only in the soldiers'
speech language), the VB music. Chunks stream in and out as the player crosses streaming
volumes (formats.md, levels.md).

## The player

- **Movement and camera**: movement.md. No acceleration or friction; velocity comes straight
  from the input each tick; crouch, jump, steps, falls, ladders, aim-mode lean; the camera a
  tick behind the soldier.
- **Inventory and weapons**: player.md (the weapon table, clips, reserves, carry-over between
  levels), combat.md (firing, bullets as fast projectiles, aiming through the crosshair line,
  aim friction near enemies), player.md "Weapons in play" (refire, clip, reload, raise and lower
  in ticks; spread on the SMGs and the BAR only), objects.md (grenades, rockets).
- **Being hit**: combat.md; death restarts the level in place with unlimited retries (shell.md);
  god mode and other cheats: shell.md.

## Entities

entities.md: the entity struct, the single entity list and handles, think/move/collide, hit
parts (17 on a soldier). Soldiers and dogs share **5 character slots**, so at most 5 are alive at
once; level scripts spawn them (never distance), and one that stays out of view long enough is
removed and its record re-armed (despawning). Animation
(animation.md) moves characters by root motion and places their hit spheres from the pose the
game last drew, so an engine that animates everything every tick is slightly more precise than
the original for enemies off screen.

## Enemies

- enemies.md: the behaviour script VM (74 opcodes), variables and sensors, the brain (seek,
  flee, paths, cover, walking with wall avoidance), firing and aim error.
- soldier.md: the soldier's scripts as behaviour: detection by sight and **hearing** (any
  positional sound under 2 s old in range, the silenced pistol excepted), combat loops, cover,
  grenades, alarms, the papers and suspicion in 2:1.
- enemy_types.md: the only other actors with a brain are the dog (1:3) and the mounted gun;
  each level ships its own soldier script set over a shared core, adding behaviours that level
  messages start (guards, ambushers, gun crews, sentries...); witnesses react to hits they see.

## Level logic

levels.md: records and how each kind spawns, the level script VM and its 22 built-ins, triggers
and zones (enter, leave, Use), objectives, the rating, campaign slots. messages.md: what each
message number means. catalogue.md: every level's contents.

## World objects

objects.md: doors (scripted models), destructibles, the mounted gun, explosions tick by tick (no
falloff, no wall check), grenade flight and bounces, world animations.

## Sound

audio.md: 23 effect voices without stealing, banks from the TAF, linear distance falloff and
panning, what plays when, music streamed from the level's VB on a loop. Soldiers hear through
the same voice table (soldier.md).

## Front end and saves

shell.md: the campaign flow between the menus and the levels, the save (a raw copy of the game
state, no checksum), options, passwords and cheats, medals and unlocks. hud.md: the state the
HUD shows.

## Checking an engine against the original

The recompilation is the reference. A recording (`./play --record`) replays exactly
(`./play --replay`, with or without a display), and `VALOR_WATCH`, `VALOR_TRACE_DAMAGE`,
`VALOR_TRACE_SHOTS` and `--dump-ram` show any value at any tick (README.md, Tools). Feeding the
same inputs to a new engine and comparing positions, health and enemy states tick by tick is
the test of "feels identical": `VALOR_TRACE_STATE=FILE` writes the original's state after every
tick (a JSON line: the player's look and weapon, every entity's position, angles, health,
animation and frame, exact), and `tools/recon/trace_cmp.py` finds the first tick and field where
another engine's trace in the same format departs from it.
