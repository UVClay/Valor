# Enemy and actor types across the game

Every AI-driven actor in the single-player levels (MSN1..MSN9; MSN12, the two-player arenas,
is left out): which kinds exist, where, and how each behaves; how the levels' copies of the
soldier scripts differ; and the event and message plumbing they all share. The soldier itself
is in soldier.md (1:3's scripts) and enemies.md (the VM, brain and weapon); this document
covers the rest and refers to them.

**How it was checked.** A RAM dump of every level (24 of them) was taken by warping there
(`--warp M:L`, the mission number being the MSN number: `--warp 7:1` is MSN7 LVL1, `9:4` is
MSN9 LVL4; mission indices 6 and 8 crash in the loader) and dumping at vblank 10500, shortly
after the level starts, under `build/etypes/` (`M_L.bin`). At that point every model of the
level is loaded with its scripts and every entity record is in the record table, even if few
entities exist yet. New tools:

- `tools/recon/et_survey.py DUMP...`: the typed models (type, script count, start script, a
  hash of the script list) and the records by kind and class, with the models kind-2/6
  records name and the template hashes of kind-3 (soldier) records; `--summary` groups the
  soldier records by body, weapon and part count with their main fields; `--soldiers` lists
  them one by one.
- `tools/recon/et_sst.py DUMP [--type T | --model HASH] [N...]`: a model's scripts without a
  live entity; `--diff A B` matches B's scripts to A's by code with jump targets and script
  numbers masked (then checks that the script numbers referenced map consistently);
  `--grep RE DUMP...`.
- `tools/recon/et_events.py DUMP [--type T]`: every event registration (condition, stored
  action, the scripts that register it).
- Checking in play: `tools/recon/ver_ai.py --watch --vars` gives a `VALOR_WATCH` list for the
  five character slots (type, flags, health, both channels' script and program counter, the
  variables, the position) and the player; `ver_ai.py LOG --dump RAM --pc` prints every script
  change with the distance to the player, and the program counters as `script:word` (the
  counter rests on the word after the instruction that yielded). `ver_hear.py` adds the effect
  voices and explains each detection; `ver_fire.py` measures fire decisions and dodges. A
  replay must run with a copy of the recording's own card (`--memcard` pointing at a copy of
  its `card/` folder): with an empty card the 1:3 part never loads and nothing is logged before
  2:1.

Code was read with `fn.py c`/`show`; the dog's state was checked against the replay dump
`build/re/mid_8000.bin` (1:3, one dog, killed).

**Warped runs** (the per-level behaviours below marked "Watched (warp)"): `--warp M:L` with
the menu's cross presses stopped before the level starts (vblank 8380; the level's first
`player_update` is at 8421..8499), `VALOR_WATCH` from `ver_hear.py --watch`, a RAM dump at
12000 for the script tables, and the player moved with pokes:

- **Teleport at the start**: `--poke-on-call 8007A6B0:...` (the first `player_update`) writes
  the position into all its copies: `+0x20C` (16.16), `+0x258` (last tick's, restored when a
  move is undone), `+0x3BC`, and the integer `+0x354`; writing `+0x20C` alone was undone two
  ticks later in 3:4. In 1:2, 2:3, 3:4, 4:1 this sticks; in 5:1, 7:1 and 7:3 the player ended
  at y 0 and never moved (no floor), and so did every teleport there into a part of the level
  whose TSP chunk wasn't resident (5:1's crawl spaces, 7:1/7:3's assault soldiers, 2:3 and
  5:2 at some points): teleports reach only the chunks loaded around the start. The player's
  address is fixed per level (`[0x8009D654]` in the `build/etypes` dumps).
- **Teleport later**: `--press N:cross` fires once; the bullet's `projectile_think`
  (`0x80044470`) is called through a pointer, so its first call is a timer for pokes (direct
  calls, like `player_crouch_done`, don't reach `--poke-on-call`). The yaw goes into `+0x22C`,
  `+0x268` and `+0x558` (all three, or it eases back), the pitch into `+0x228`, `+0x264`,
  `+0x54C`. The shot's voice follows the player's position (it played at the destination and
  was heard there: 5:3, vblank 9011), so the same call sets flag 2 (unheard, as the silenced
  pistol's) on voices 1..4 (`0x8009115C + 0x34·v`); clearing `g_voice_enabled` doesn't work
  (the sound update re-sets it the next frame).
- **Reaching parts outside the start's chunks**: a teleport only works onto a loaded chunk, and
  zones (so triggers and streaming volumes) are found through the floor under the player, so
  stepping through the streaming volumes by teleports needs a floor at each step. What works
  instead is choosing the chunks: `chunk_stream` (`0x8005439C`, run every few frames from the
  idle job `0x80090A30`) loads, one at a time, each chunk that the **current streaming record**
  (`0x80099070`, set on entering one) names and no slot holds, into a slot holding a chunk it
  doesn't name, about 400 vblanks per chunk. Poking `0x80099070` on the first `player_update`
  with the address of the streaming record nearest the target (`far_stream.py DUMP X Y Z` lists
  them with their chunks, the slots and the current one) streams those chunks while the player
  still stands at the start (the start chunk is evicted, the player stays put); a shot's
  teleport into the target after the loads (9400 in 5:1 and 7:1, chunks loaded at 8480, 8880,
  9266) lands on the floor, and from there entering volumes works normally (7:1: the player
  entered record 3's volume and its chunk 8 streamed in). A teleport on the first
  `player_update` straight into the unloaded part hung for good (no zone, no shot fired). The
  yaw pokes did not turn the player in these runs (he walked along the start facing); turning
  with `--mouse FRAME:200,0` (about 300 yaw units a vblank) did. `far_tp.py` builds the pokes
  (god mode, the streaming record, teleports on either call, the silenced shot).
- God mode (`g_cheats` `0x800386A4` = 0x80) on the first `player_update`; level messages
  started directly by poking a slot's `+0x40` on the same call (seen at the next think).
- New tools: `unr_spawns.py DUMP` (each soldier record: spawn behaviour bits from its script's
  msg-0 `set_flags`, tags, the trigger boxes that spawn it, distance from the start),
  `unr_nodes.py DUMP` (the AI nodes: kind, approach side, position, facing; entries at
  `[g_world+0x388]+0x8`, count `+0xC`, position s16 at `+0x14`, facing s16 at `+0x24`/`+0x28`),
  `unr_slots.py DUMP` (the character slots and both channels' `script:word`),
  `unr_durations.py` (time per script from `ver_ai.py`'s output). `ver_ai.py` prints `-1` for
  the first script of a slot whose list address is logged later in the same tick; `--pc`
  resolves it.

## The survey

Model types found (models are per level, `[0x8009EC58]`, type at `+0xFC`; levels.md):

| Type | What | Levels |
|---|---|---|
| 0x1402 | Soldier: every level; 1..6 body models plus `0xB8534EAA` (loaded everywhere, never named by a record) | all |
| 0x1405 | **Dog** (model `0x34D1136B`, 43 scripts) | 1:3 only (16 dogs) |
| 0x1772 | Mounted gun (model `0xCB2AA887`, 6 scripts) | 1:1, 1:2, 2:3, 4:1, 5:2, 5:4, 7:1, 9:4 |
| 0x1774 | The large scripted object of objects.md (model `0xE500C08C`, 6 scripts; the rocket, inferred) | 9:4 only (objects.md's 2:3 and 4:2 have no such model) |
| 0x1771 | Spinning timed prop (`0xC2E34354`) | 1:1 only |
| 0x1775, 0x1776, 0x1770, 0x1777..0x177A | Scripted boxes, doors, destructibles, scenery (objects.md): no brain | many |

There are no tanks, vehicles with a brain, separate sniper or officer types: snipers, officers
and the rest are soldiers (0x1402) with other body models, weapons and record fields (below).
The only actors with a brain are the soldier, the dog and the mounted gun.

Per level (records of kind 3 are the soldiers and dogs that can appear; each record spawns
once):

| Level | Soldier scripts (start) | Same code as | Soldiers | Dogs | Guns | Bodies |
|---|---|---|---|---|---|---|
| 1:1 | 235 (217) | | 28 | | 3 | 6 |
| 1:2 | 236 (218) | | 52 | | 0 (model loaded) | 6 |
| 1:3 | 218 (200) | 4:3, 9:2 | 33 | 16 | | 1 |
| 2:1 | 312 (294) | | 10 | | | 6 |
| 2:2 | 317 (299) | | 14 | | | 6 |
| 2:3 | 236 (218) | | 40 | | 4 | 2 |
| 2:4 | 210 (192) | 3:2 | 19 | | | 2 |
| 3:1 | 307 (286) | | 37 | | | 2 |
| 3:2 | 210 (192) | 2:4 | 25 | | | 2 |
| 3:3 | 218 (200) | | 49 | | | 2 |
| 3:4 | 213 (192) | | 28 | | | 2 |
| 4:1 | 227 (209) | | 56 | | 12 | 2 |
| 4:2 | 221 (200) | 7:2 | 67 | | | 2 |
| 4:3 | 218 (200) | 1:3, 9:2 | 54 | | | 2 |
| 5:1 | 238 (220) | | 52 | | | 2 |
| 5:2 | 232 (214) | | 42 | | 1 | 4 |
| 5:3 | 232 (214) | | 52 | | | 4 |
| 5:4 | 226 (208) | | 47 | | 2 | 4 |
| 7:1 | 236 (218) | | 49 | | 1 | 6 |
| 7:2 | 221 (200) | 4:2 | 35 | | | 4 |
| 7:3 | 235 (217) | | 31 | | | 6 |
| 9:1 | 227 (209) | | 59 | | | 2 |
| 9:2 | 218 (200) | 1:3, 4:3 | 55 | | | 2 |
| 9:4 | 226 (207) | | 12 | | 1 | 2 |

Within a level all soldier models share one script list (the same `+0xB8` pointer), so every
soldier of a level runs the same scripts. 1:3, 4:3 and 9:2 carry identical sets, as do 2:4/3:2
and 4:2/7:2; the others are 1:3's core (scripts 0..94 everywhere) plus behaviours added for the
level's set pieces, and some leave out 1:3's cover peeking (section Level differences).

### Soldier variants

A kind-3 record's template (`+0x74`: count, then hashes) is {body model, weapon, second part,
grenade}; `enemy_spawn` reads it (enemies.md):

| Entry | Meaning |
|---|---|
| 1 | The body model; the entity type comes from it (0x1402 soldier or 0x1405 dog). A dog's template has only this entry |
| 2 | The weapon: `0x20B78C80` SMG (kind 0), `0x6EB4A258` rifle (1), `0x722419EA` the 0x1407 weapon (2), `0xF63FC614` Panzerfaust (3), matched in `g_enemy_weapons` |
| 3 | A second attachment (head gear), unless `0x4CBF0BA5` (none): `0x7C4E006C`, `0x8A522920`, `0xBE1D2304`, `0xE9FB8048`, `0x8C76FD76`, `0x6B4E1F04` |
| 4 | Present (any value; `0x55A51DAB` on the disc): the soldier carries a **stick grenade** (attachment 2, model `0x61282777`) and gets `+0xEC` bit 1, **grenades allowed** |

So grenade throwers are exactly the soldiers with 4-entry templates: none in missions 1..3,
most soldiers from mission 4 on (soldier.md's bit 1 was "set from outside": it is this).

The rest of the record sets the soldier's numbers (enemies.md, Spawning). Their spread per
level (`et_survey.py --summary`), the main groups:

| Level | Common soldier | Notable others |
|---|---|---|
| 1:1 | Rifle, hp 6, aim 40..60, sight 900..4000 | |
| 1:2, 1:3 | Rifle hp 6 aim 40; SMG hp 6 aim 100 | 1:3: dogs hp 4, sight 1500..2805, hearing 3000..5000 |
| 2:1, 2:2 | SMG hp 6..10 aim 90 | 0x1407 users hp 4 aim 20 fire 50% (2:1); 2:2's 0x1407 users hp 8, aim 10, fire 80%, sight 800, hearing 1200 |
| 2:3, 2:4 | SMG hp 6..12 aim 90..100; rifles aim 40 | Sight 500..4000 spread |
| 3:x | SMG aim 100 or 0x1407 aim 40, hp 6 | 3:2: a body (`0x4A663EB0`) with the 0x1407 weapon and no head gear |
| 4:x | Rifle hp 8 aim 30 fire 45%, SMG hp 8 aim 90, all with grenades | |
| 5:x | Grenadiers as 4:x; 5:2/5:3: `0x699D1029` with the 0x1407 weapon, no gear, no grenade, hp 6 | 5:4: Panzerfaust soldiers (hp 8..9, aim 35, fire 40%) |
| 7:1, 7:3 | Rifle/SMG/Panzerfaust hp 8, sight 3000, hearing 4500..5000 (7:1) | 7:2: `0x11EDCD18` with SMG, **aim error 0**, fire 50..60%, poll every tick, sight 2000..2500, hearing 4000, never on a path |
| 9:x | SMG hp 12 aim 80 fire 50% (9:1, 9:2), up to hp 20 | 9:4: field of view 160..270, hearing 1750..2000 |

(aim = the aim error E, ±E angle units; poll = record `+0x66`, the decision period v0B, 30
meaning 1; hp in health units, the player's rifle doing 2.5.)

## The dog (type 0x1405)

1:3's only non-soldier character. Made by `dog_spawn` (`0x80054E4C`) from a kind-3 record
whose template is just the dog's model; it counts toward the soldier cap (`0x80097FE8`) and has
its own count (`0x80099B20`). Its handlers: think `dog_think` (`0x80054AB8`), hit_part
`0x80054C24`, world_contact `0x80054E14`, removal `0x80054D70`, touch the soldier's
(`0x8005291C`). No weapon block: it hurts by contact.

### Body and numbers

From `dog_spawn`: flags A 0x1300906C, collision mask 0x88204401 (the soldier's), flags B
0x68000, collision radius 125, push radius `+0x2EC` 80 and `+0x3C8` 80 (the soldier: 150, 60,
60), sphere centre 50 above the origin (-50; the soldier -120), health from record `+0x61` (4
for every 1:3 dog), home (`+0x3A4`) the record's position. Brain: sight range record `+0x6C`
(1500..2805), second range `+0x70` (3000..5000; 2800 below 10), field of view `+0x6A` × 11
(990 units, ±87°), head bone 13, walk speed 1.0, no slow-down; nothing else from the soldier's
spawn (no aim error, path mode, dodge or fire chance). Its 32-bone skeleton and part spheres:
animation.md.

Variables set by `dog_spawn` and the start script (42):

| Var | Value | Use |
|---|---|---|
| 0x05, 0x06, 0x07 | 3000, 1500, 600 (script 42 overrides the spawn's range, half, third) | Leash distance; proximity detection 600 |
| 0x0D | 100 | Health percent (updated by the hit handler) |
| 0x13 | | Hit reaction (the soldier's 0x1C): 1 flinch, 4 thrown, 8 dying, 0 |
| 0x16 | The player (`query.8` type 1) | **The target** (the soldier's fire-state variable; the dog has no weapon) |
| 0x17 | Itself | |
| 0x1C | Record `+0x6E` | Path id: non-zero starts the dog on a patrol path |
| 0x1D | Record `+0x5E` | Path mode |
| 0x1F | Record `+0x34` | Leash: 500 or more makes the dog go home when the player is far (1:3: 0 or 501..520) |
| 0x0E | Heading | `dog_think` copies the heading into it before the scripts and back after: scripts can turn the dog by writing it |
| bit 9 | 1 | Alive (cleared by every death) |

`dog_think` each tick: `0x80052AF4` (a witness report when hit, Witnesses below), the event
channel (`script_run_b`), heading → v0E, the scripts (`script_run_a`), v0E → heading, the hit
reaction (v13 → `0x80052084`, blood particles), the smoke and blood particle updates
(animation.md). No ranges update, no weapon.

### Behaviour (scripts 0..42)

**Start (42):** registers hits (front `s38` 0x40 → 27, back 0x20 → 28), makes every hit part
an ordinary part (`hit_part` all → kind 1), sets the variables above; with a path (v1C > 0)
→ **15**, else it runs off the end and channel 0 picks the only weighted script, **14**.

**Waiting:** 14 stands (channel 1: script 0, anim 0 looped); 15 walks its path (channel 1:
script 1: path goal v1C mode v1D, anim 2 looped; at a path end (2: `s38` 0x800000/0x1000000 →
3) it pauses the path (`behaviour 0x2000`), plays anim 2 frames 0..4 and resumes
(`-0x2000`)). Both register: `s40` 0x100 (the group alert from the level, as for soldiers) →
17, a grenade near it (`s3C` 0x80) → 35, a hit comrade seen (`s3C` 0x10000) → 16, and every
tick call **detection (12)**: on 1 → 17.

**Detection (12)** returns 1 when the dog **hears** a sound (query 0x11, any one-shot world
sound within its sight range of it, younger than 60 frames: soldier.md, Hearing); else 0 if
the player is undetectable (flags B 0x400); else 1 if it **sees** the player (query 0xB:
within the sight range, ×0.75 when the player crouches, within ±87° of its head's facing, the
line clear); else 1 if the player is within **600** in any direction. The same rules as the
soldier's 44 with 600 for the soldier's 1000.

**Going to a hit comrade (16):** point goal to the spot (`0x8009817C`), run (anim 3 via
script 6); every tick, sees the player with the long range (query 0x10) → 17; arrived (`s38`
0x400000) → 17.

**Alert (17):** stops, turns toward the player with a turn animation (route + 18: anim 22 or
23 for the two sides, 4 for behind), seeks the player and runs, two barks (sounds 4/5) around
anim 1, → **19**.

**The chase (19)**, every tick after a random 0..1-tick pause:
- channel 1 runs 4: seek the player, run (anim 3 looped), a bark (39: sound 4 or 5, 50%)
  before about half the cycles;
- with a leash (v1F ≥ 500) and the player more than v5 (3000) from the dog's home → **20, go
  home**;
- distance > 475: keep running;
- 370..475 and the player within ±90° of its heading: **21, leap** (start 8: attack part on,
  seek, anim 5 then 6 then two run cycles; after 10 ticks → 22);
- under 370 and within ±90°: 70% **22, bite**, 30% **23, snap twice**; outside ±90°, keep
  running (the seek turns it).

**Bite (22):** channel 1 runs 10: stop, anim 7 with the attack part on (`hit_part` mask 1 →
kind 5), a growl (40: sound 6 or 7); if the player is within 500, `follow` the player (attach
at an offset: it hangs on), anim 9, seek off, anim 4, then seek and two run cycles. Channel 0
waits: while the player is within 475 it lets 10 finish; when the player gets beyond 475 it
cuts the attack and plays anim 8 (player ahead) or 3 (behind); then back to 19. **Snap (23):**
11: anim 10 twice with growls, the attack part on; back to 19 when done or the player is beyond
475.

The attack part is never turned off again until death (only the death scripts set it to kind
8): once a dog has attacked, its mouth part does contact damage whenever it touches the
player. The player takes a dog's contact through `player_hit_part` like a soldier's melee: the
first part of kind 5 marked as touching this tick is the attacker's part, `damage_from_hit`
with its damage (2.0 per hit, combat.md); the player's invulnerability timer after a hit spaces
the bites.

**Go home (20):** point goal to its home, run (5); the player back within v5 of home → 19;
arrived (`s38` 0x400000) → stand facing the player (anim 0), or back to the path (15) if it
has one; detection or the player within v5 of home → 17.

**Grenades (35):** a grenade rolling near (`s3C` 0x80): turn, run at it (seek with slow-down
radius 900); within 400 attach to it (`follow`), anim 24, set the grenade's `+0x3C` 0x2 (picked
up) at frame 3, back to the chase. Gone meanwhile (`s38` 0x10, the seek target removed) → 19.
It never throws it back (a soldier's 0x4 throw is not sent): **the grenade stays in its mouth
until it explodes** (watched, below).

Who is warned (`projectile_world_contact` `0x80043284`, once per grenade: the first tick it
rolls, obj bits 0x400 with 0x4000, which it then clears): every living soldier and dog
(type 0x1402/0x1405, flags A without 0x20080000) within **1200** (`dist2d_approx`) gets `s3C`
0x40 (flee); the nearest of them also gets 0x80 (return it) if it is a **dog** (any distance
within the 1200), or a soldier with fewer than 3 warned and within 1000; with 3 or more warned
the nearest soldier gets 0x20 (smother) instead. So a dog nearest to a rolling grenade always
goes for it.

**The dog survives its own grenade while biting.** (Confirmed on the game itself: the user's DuckStation run of 1:3, 2026-10-21, shows a dog biting the player with the fetched grenade going off in its mouth; the player dies, the dog stands. In normal play the player always dies beside a dog that survives this way, which is why the dog seems to die from every grenade.) Damage between two parts
(`damage_from_hit` `0x8004C1B4`) is refused when either part is kind **8**, and an attacker
with flags B 0x20000 (the dog: 0x68000) has its attack parts (kinds 5, 6, 9) turned to kind 8
by `0x8004A868` once one of them lands. `parts_touch` (`0x8004C990`) reports only the first
touching pair (the first part of the first entity against the first of the second), and the
dog's part 0 is its mouth: the attack part while it leaps or bites, the one next to the
grenade. The blast (newer, earlier in the list) is handled first: the dog's bite "lands" on
the blast (the blast takes 2.0, `VALOR_TRACE_DAMAGE` prints it as the victim), part 0 becomes
kind 8, and from then on every pair the blast meets is that spent part: no damage for all 9
ticks. A dog whose part 0 is an ordinary part (kind 1: running, going home) takes the 4.0 and
dies, thrown (29/30).

**In normal play** (no god mode, no teleport: the player walked from 1:3's start to the first
dog, grenades drawn on the way, throws at the approaching dog; runs b1..b6, c1..c4, a1, a2, a4
in `build/dogfetch/`, below): a dog's bite that lands **on the player** spends its mouth part
too (kind 5 → 8 on the bite's hit, back to 1 when it returns to the chase 19, 5 again on the
next leap), so a dog that has just bitten carries a spent mouth for the rest of the bite (22).
Outcomes:

| Case | Runs | At detonation | Player | Dog |
|---|---|---|---|---|
| Fetched, chased, **biting the player** (22, anim 9) | 5 of 5 (b1, c1..c4) | blast in the dog's mouth ~100 above it, 265..275 from the player; part 0 kind 8 since the bite landed | **killed**: 1000 (within 300) | **unhurt**, all 9 ticks (4.0 → 4.0), no damage event |
| Fetched, player backing away past the leash | 3 of 3 (a1, a2, a4) | the dog had gone home (20) carrying it; part 0 kind 1 | unhurt (5000 away) | **killed** the first tick, 4.0 on part 0, thrown (30) |
| Not fetched (no 35: the warning went elsewhere or the dog's script didn't take it; not traced), dog in the blast | b2, b4, b6 | part 0 kind 1 | unhurt | **killed** (4.0) |
| Not fetched, dog had just bitten and was running off | b3 | part 0 kind 8 at first | killed (1000) | unhurt 7 ticks, then **killed** through part 1 once its mouth left the blast's sphere |

So in normal play the immunity is the rule whenever the dog is biting at detonation, and that
is also when the player, next to it, dies; when the player survives to watch, the dog was not
biting and dies. **God mode** changes how the bite gets spent, not the outcome: with it the
bite never lands on the player (part 0 goes 5 → 1, never 8), and the mouth is spent by landing
on the blast itself (the 2.0 "damage" to the blast in the god-mode runs); without it the
player's bite already spent it. The recomp runs the original's code; a check on a console or
another emulator would settle the user's memory independently.

**Hit (27 front, 28 behind):** stop (24); above 10% health: v13 = 1, a yelp (37: sound 0 or 1),
anim 12 (front) or 14 (behind), back to the chase (19). At 10% or less: killed by an explosion
(type 0x31), a grenade (0x13F3) or a rocket (0x13F1) → **29/30, thrown** (impulse -20 or +20
sideways, -30 up; yelp 38 (sound 2/3), v13 = 4, anim 17 until it lands: on the ground (`s38`
0x2) → 32, anim 19; against a wall (`s3C` 0x2) → 31, anims 18, 20, 21 with thuds (41: sounds
0x19/0x1A, not positional)); otherwise → **34**: v13 = 8, sound 3, anim 15. Scripts 25, 26 and
33 (other hit and death variants, anims 13 and 16) are reachable only from themselves: unused.

**Corpse (36):** hit parts off (kind 8), property 1 = 0, flags A 0x20000, wait 15, 19 blinks
(as the soldier's 15), v0D = -1, wait 60, removed.

Unused: 7 (bark loop), 9 (empty), 13 (a pad-button event). The dog's sounds 0..7 are its own
bank (audio.md: bank 5 in 1:3).

**Timings** (animation.md's frame counts for the dog, `ent_anims.py table 0x1405`; one frame per
tick): alert 17 = turn anim 22/23 (25) + anim 1 (10), 35 ticks, or 10 without a turn; leap (8 on
channel 1) = anim 5 (7) + anim 6 (15) + 2 × anim 3 (12) = 46, cut at 10 by channel 0's move to
22; bite (10) = anim 7 (14), then with the player within 500 anim 9 (24), anim 4 (25) and two run
cycles (24): 87 if nothing cuts it; snap (11) = 2 × anim 10 (23) = 46; hits 27/28 = anim 12 (10) /
anim 14 (15); death 34 = anim 15 (48); corpse 36 = 15 + 19 + 60 = 94.

**Watched** (the replay of the recorded session through 1:3, vblanks 2119..13747: 14 dog lives
in the character slots, every script change, the variables and positions logged each tick,
`tools/recon/ver_ai.py --watch --vars`, and `VALOR_TRACE_DAMAGE=1`):
- **Alert 17 → chase 19** after 35 ticks (turning) or 8..10 (straight ahead), as above.
- **Leap 21** entered 12 times with the player 394..504 away (positions at the end of the tick,
  after the leap's first frame and the player's move; one at 588), never from closer: the
  370..475 band as read. Channel 0 went on to **22 exactly 10 ticks later** every time the leap
  was not cut by a hit (6 of 6).
- **Bite 22 → 19** after 11..12 ticks (4 times: the player already beyond 475, so anim 8 or 3
  and back) or 29..31 (2). No dog ever went from 19 straight to 22 or 23 (the player under
  370): they always arrived running and leapt first, so the 70/30 bite/snap split and the snap
  (23) were not seen.
- **Damage**: the three times a dog hurt the player (2.0 each, vblanks 8483, 11529, 11629) were
  all **during a leap**, 3..5 ticks after it started: the attack part that 8 turns on bites on
  contact while the dog is in the air, before 22 starts.
- **Hits**: 27 (front) back to 19 after 10 ticks, 28 (behind) after 15. The player's rifle took
  2.5 of the dogs' 4 health; the second hit killed (34, the ordinary death: no explosion).
- **Death 34 → corpse 36 after 48 ticks** (all 14), removed (flags A 0x80000) **94 ticks** after
  entering 36.
- **The leash** (vblanks 2966..3088, the player about 3000 from the dog's home): the dog cycled
  19 → 20 (go home) → 15 (path, arrived at once) → 17 (detection) → 19 every 12..25 ticks, a
  loop at the edge of v5; it broke when the player came nearer.
- Dogs' barks (sounds 4/5) are heard by soldiers: seven 1:3 soldiers were alerted by a bark
  196..540 away with the player beyond their sight (soldier.md, Hearing).
- Not seen: the hit-comrade run (16).
- **The grenade fetch (35)**, watched in warped 1:3 runs (`build/dogfetch/`: player teleported
  to (-1047, 453, 4023) on the first `player_update`, where the first dog spawns at once and
  chases; god mode; grenades drawn with three weapon presses; one throw at vblank 9300,
  `VALOR_WATCH` on the character slots, their `+0x38` words and the first projectile after the
  player): the grenade landed and began rolling 699 from the dog (vblank 9328: the dog's `s3C`
  0xC0, the grenade's obj bits 0x434B → 0x0742); the dog entered **35 the next tick** (9330),
  ran at it (anim 3) and started anim 24 at 380 from it (9362); at frame 3 (9368) the grenade
  followed its mouth, its bounce bit cleared (0x340 → 0x140), **fuse back to 90, life 120**;
  **back to the chase (19) 28 ticks after the warning** (9386) with the grenade in its mouth
  (a frame: `build/dogfetch/shots2/frame009410.png`), then leapt (21) and bit (22) as usual,
  carrying it. The fuse ran out at 9546, the blast 180 from the dog mid-bite: the dog took
  nothing (the spent-part rule above; the same in two more throws, charged 10 and 22 ticks,
  blasts at 9562 and 9638, the dog biting each time). Without god mode, a soldier's shot had
  moved the player to the leash's edge: the dog picked the grenade up, chased one tick, then
  **went home (20) carrying it**, and the blast killed it there (4.0 of its 4.0, 9548), thrown
  (30 → 32 → corpse 36). The player, 400..2000 away, was never within the blast's 300-unit kill.

## The mounted gun and its gunner (0x1772)

The gun (objects.md: firing, the player's use) is driven by events on its `+0x3C` from whoever
works it. Its scripts (the same 6 in every level, `et_sst.py --type 1772`):

| Script | State | Events |
|---|---|---|
| 0 | Start: parts normal, v3 = the player, v2 = itself, fire off; `s38` 0x2000 → 5 | |
| 1 | Idle: parts not hittable (kind 7) for 15 ticks then normal | `s3C` 0x200 → 2, 0x4000 → 4 |
| 2 | **Tracking**: seek goal on the player (it turns at the brain's turn rate), parts kind 7 | 0x400 → 1, 0x2000 → 3, 0x800 fire on (v5 = 1), 0x1000 fire off |
| 3 | **Holding**: no turning | 0x400 → 1, 0x200 → 2, 0x800/0x1000 fire |
| 4 | **Manned by the player**: v9 = 1 | 0x400 → 1 |
| 5 | Removed (`s38` 0x2000 from `mounted_gun_think`) | |

The bits come from: the player (`player_update` around `0x8007A478`: 0x4000 when mounting,
0x400 when leaving), and a soldier holding the gun's node (soldier.md, cover: a kind-0x10 node
with a gun within reach; 1:3's 96 sends 0x200 while the player is within 969 units of the
node's facing angle and 0x2000 beyond). The levels that have guns add a gunner's routine: in
1:1 (126 on channel 1, entered from 101/102): hide its own weapon (attachment op, v1A bit
0x08000000), gun to tracking (0x200), fire off, wait 10..18 ticks; then whenever it sees the
player with the long range unreduced by crouching (query 0x12), fire on (0x800) for **1..8
plays of the gunner's anim 114** (2:3's script 113, 1:1's 125; anim 27 at an approach-side-4 node), 14 ticks
each, then off (0x1000) for 10..45 ticks, again; while it doesn't see the player it loops
anim 114 frames 0..4 (5 ticks) and tests again. The gun gone → show the weapon. While a gunner
holds it the gun fires at the player's middle with its own spread (objects.md).

**Watched (warp)** in 2:3 (record 22, a stationary soldier (bit 2, spawn message 0x8000000)
220 units from node 0, a kind-0x10 node by the gun at (-16771, 41, 1646), gun yaw 341):
spawned by trigger 25, idle (220) with the player 1800..2300 away; its `+0x40` poked with
0x100 (the group alert) at vblank 9003: **99 (1:3's 94, moving to the node) at 9006**, run
(107) for 10 ticks, **101 (1:3's 96) and the gun routine 114 at 9026**. With the player 1750
away it never saw him (query 0x12) and kept the 5-tick aim loop for the whole run. With the
player 1050 away in front of the gun: fire on from 9068, the count v15 = 1..8 uniform over
858 bursts (106..134 each), each count one 14-tick play; off 10..45 uniform (17..45 per value
over 1000 holds). Whether the gun's shots hit was not checked (god mode).

## Witnesses: soldiers who see a comrade hit

`0x80052AF4`, at the start of every soldier's and dog's think: if the entity was hit this tick
(`+0x38` 0x20 or 0x40), it records itself as the victim (`0x80098178`), its position
(`0x8009817C`), and the first five other living soldiers and dogs in the entity list as
witnesses (`0x80098164`, count `0x80098184`), replacing any earlier report. Once per frame
(`0x80052C38`, from `entities_update`) the last witness is taken off the list and, both being
valid, tests whether it sees the victim with its long range (`+0xB2`, the sight test of query
0x10); if so it gets `+0x3C` 0x10000. The soldier's idle and patrol scripts (and the dog's)
answer that with 201 (the dog 16): run to where the victim was hit, then alert (209).

So shooting a soldier in view of up to five others (by list order, not distance) pulls those
that can see him to the spot, one candidate per frame. 1:3's 201: point
goal to the spot, a voice (74), anim 215, then **walks** (217: anim 7 with footsteps, not a run);
every tick, the player seen with the long range (query 0x10) → **210, engage** (not 209);
arrived (`s38` 0x400000) → stop, weapon anim (38), → 209. Only the idle and patrol scripts
register 0x10000: a soldier already engaged ignores it, and 2:1's papers soldiers (220/221)
don't register it either (2:1 has it in its idles 296..299 → 295 and in 268 → 254).

**Watched (warp)** in 3:4 (where 201 is 193): the player teleported 2000 units in front of two
idle soldiers (slots 1 and 2, 360 apart), beyond their sight (1500) and hearing, and fired the
pistol: slot 2 hit at vblank 9107; slot 1 entered **193 at 9113** (3 ticks later), played anim
215 (14 ticks), started the walk at 9141 and in that tick saw the player (1996 away, inside the
long range 3000) and went to 25 (through 202, 3:4's 210). In the run before, with the player
770 away, both had seen the player first and were already in 25 when the shots hit: no
witness report was taken.

## Events: what the scripts react to

Events (ops 0x0D, 0x0E and 0x42, all three `sop_event`, `0x80058C78`) store a condition and
one instruction in an event table; `script_run_b` (`0x8003FBFC`) tests them every tick before
the scripts run.

- **Storing.** The condition mask is the instruction's low byte; one word follows per set bit:
  1 a pad test, 2 a button test (the player's model), 4/8 `+0x38` has all / none of the bits,
  0x10/0x20 `+0x3C` all / none, 0x40/0x80 `+0x40` all / none (the scripts use only 4, 0x10,
  0x40). **0x0D and 0x42 are the same instruction**: the record goes to the running channel's
  table; **0x0E** puts it in channel 0's table whatever channel runs it (the action scripts on
  channel 1 use it to redirect channel 0). While the running script is the model's start
  script the record goes to a table in the model instead (`+0xB4`, count `+0xB0`). A record
  with the same condition replaces the old one; storing a `nop` (0x0F) as the instruction
  clears it (`(unregister)` in `et_events.py`).
- **Matching.** Each tick, per channel, the records are tried in order and the first whose
  condition holds runs its instruction, and that channel's other records wait for the next
  tick. A stored `goto` (0x26) moves that channel's program counter (and resets its waits);
  other instructions run through `script_step` (`other_script`, `set`, ...).
- **The words are one-tick pulses.** `entities_update` clears `+0x38`, `+0x3C` and `+0x40` right
  after each entity's think. Everything that sets them (moves and contacts, the brain, the
  collision pass, other entities' thinks, the level's messages at the end of the frame) runs
  between one think and the next, so each pulse is seen exactly once, at the next think.
  Before matching, while `+0x44` counts down, bits 0x600 are cleared; while the `+0x48`
  invulnerability timer counts down, bits 0x1E0 and 0x180000 (the hit directions, so hits
  during it are not reported).

Who sets the bits the soldier and dog scripts test:

| Word | Bit | Set by | Meaning |
|---|---|---|---|
| `+0x38` | 0x2 | world_contact with a floor (`0x80056250`, dog `0x80054E14`); touch on top of a solid object (`0x8005291C`) | Landed |
| | 0x10 | `brain_move_to` | The seek/flee target is gone (null, or flags A 0x180000) |
| | 0x20 / 0x40 | `damage_from_hit` | Hit from behind / in front |
| | 0x400000, 0x800000, 0x1000000 | The brain (enemies.md) | Arrived; path start; path end |
| | 0x80000000 | (entities.md) | Fell |
| `+0x3C` | 0x1 | `enemy_weapon`, when the burst's last shot is fired | **End of a burst** (the fire loops play a weapon anim, 39) |
| | 0x2 | world_contact without floor; touch against a solid object's side | **Bumped into a wall or object** (aborts a move) |
| | 0x20 / 0x40 / 0x80 | A rolling grenade (objects.md) | Smother / flee / return it |
| | 0x100 | `op_alarm`, `alarm_from_level` | An alarm went up |
| | 0x8000 | `entity_set_home` (level built-in 11) via `0x80056AEC` | **New home**: go there (176) |
| | 0x10000 | The witness check | A comrade was hit in view: go to `0x8009817C` (201) |
| | 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000 | Gunners, the player | Mounted gun states (above) |
| `+0x40` | any | Level scripts only (`set_flags`, built-in 3) | Level messages (per level, below) |

## Messages soldiers send

`send_event N` (op 0x43, `0x8005C210` → `send_event`, `0x800662CC` → `msg_send`) broadcasts level
message N with the soldier as sender: every level-script subscription to N whose filter accepts
the sender queues its handler (run at the end of the frame, objects.md). `register_msg`'s last
argument is a handler index in the subscribing script (`0x800694C8`), the message being that
handler's (lvs.py prints it). What the levels do with them (every level dumped):

| Message | Sent by | Subscribers |
|---|---|---|
| 0x1B | Alert (209 and the engage scripts; 72 sends in every level) | Soldier records' own scripts with a tag filter (filter 5: sharing a tag with the sender): `set_flags(0x100)` on themselves → their `s40` 0x100 → alert. **A spotted player alerts the spotter's tagged group**, not everyone. Some subscribers instead `activate` their record (2:2, 2:3, 9:1: reinforcements appear); in 2:x/3:1 others forward it as message 4 |
| 0x4 | 2:x papers scripts (disguise blown) | `set_flags(0x100)` (2:1, 2:2, 3:1): alert |
| 0x1E | 2:x/3:1 suspicion scripts | `set_flags(0x20000)` on the subscriber unless it is the sender |
| 0x33, 0x75 | 2:x/3:1 | Objective and alarm bookkeeping scripts (they re-register other messages) |

## Level differences in the soldier scripts

Compared with `et_sst.py --diff build/etypes/1_3.bin build/etypes/M_L.bin` and read script by
script (the per-level work was split among helper passes; script numbers below are each level's
own). Scripts 0..94 (the hit, death, dodge, fire-loop, patrol and engage core) are 1:3's in
every level, apart from renumbered references; **no level changes a tuning constant** (a
distance, chance or timer) of a shared script. The differences are added behaviours, entered
through new `s40` level messages that the start script registers (each sent by a level script
at the record's spawn, msg 0, unless noted), or through cover-node kinds that are empty in 1:3;
and, in some levels, 1:3's cover-peeking chain left out.

| Level | Added | Left out |
|---|---|---|
| 1:1 | Seated guard (S), path advance (P), low-wall node (L), gun crew (G) | |
| 1:2 | S, L, sentry (T) | |
| 1:3, 4:3, 9:2 | (the reference) | |
| 2:1, 2:2, 3:1 | Alarm runner (A), T, papers and suspicion (soldier.md, 2:1's soldier; DISGUISE) | |
| 2:3 | S, P, G, foxhole ambush (F) | Peeking |
| 2:4, 3:2 | | Peeking |
| 3:3 | A, S | Peeking |
| 3:4 | Hidden ambusher (H) | Peeking |
| 4:1 | F (as its "pop-up shooter" and "jump-down"), G | Peeking |
| 4:2, 7:2 | H | |
| 5:1 | Worker (W), P, crawl-space chaser (C) | |
| 5:2 | W, L, G | |
| 5:3 | W, L, S | |
| 5:4 | S, G | |
| 7:1 | Assault soldier (X), T, G | Peeking |
| 7:3 | X, P, L, G | Peeking |
| 9:1 | P, L | |
| 9:4 | G, technician (R), holdout shooter (unused) | Peeking |

**Peeking left out** (2:3, 2:4, 3:2, 3:3, 3:4, 4:1, 7:1, 7:3, 9:4): 1:3's 98..111 shrink to 1:3's
110 (run to cover) and empty slots. At an ordinary node 95 starts an empty channel-1 script, so
the soldier **stands at the node without firing** until 96 sends it back to engage (the score
above 300, the node taken, the player nearer than 500 or beyond 6000); no grenades from cover,
none of the hit-at-cover scripts (hits take the ordinary path).

**S, seated guard** (`s40` 0x8; 1:1 213..216, 1:2 214..217, 2:3 201..204, 3:3 196..199, 5:3/5:4
204..207): stop, no node; channel 1 loops anim 148 2..4 times, then 50% anim 154 (a
hit-reaction particle pulse at frame 31: smoke, inferred) else 149, 150 ×1..3, 151. Detection
(44) every v0B ticks, the group alert (`s40` 0x100), an alarm or a comrade-hit report → get
up (anim 152, then anim 61 from frame 27) → 17 (stand and engage). Hit: pain, below 10% the
ordinary death, else anim 153 and the get-up → 17. (The pose being seated is inferred from the
animations.) **Watched** in 2:3 (the replay past the end of the recording's match, still the
game's own logic; `ver_ai.py`, vblanks 64313..64579): spawned straight into 201 with 204 on
channel 1, played anim 148 four times (64 ticks) then 149; detection at 64447 with the player
1820 away and nothing hearable (2:3's sight ranges reach 4000) → 202: anim 152 for 27 ticks;
hit at 64511 during the anim-61 tail → 203: anim 153 (16 ticks) + anim 61 frames 27..40 (14) →
17 at 64573, then engaged. Lengths: 148 16, 149 13, 150 16, 151 11, 152 27, 153 16, 154 60.

**P, path advance** (`s40` 0x4000000 → 80/81; 1:1, 2:3, 5:1, 7:3, 9:1, the same scripts): 1:3's
empty 80 becomes a patrol (paths below id 50 walk with script 87 without a path goal, others
84's path walk) that on detection, the group alert, an alarm or a comrade-hit report goes to 81,
a stop-and-go advance: **stand and fire** (87: face the player, fire 1 during anim 27 with no
fire roll, so bursts whenever the player is in the ±30.8° cone; a Panzerfaust plays anim 271
2..4 times, fires an aimed rocket (fire 2, anim 272), reloads (273) and, about half the time,
ends its stand) for 40..70 ticks, then **walk the path** (fire 0, anim 7) for 20..60 ticks, and
again. Overrides: unseen (query 0x10) or the player within 200 → walk; within 500 → keep
firing; after the stand time a non-Panzerfaust soldier walks on only if the player is within
1000 (beyond it, it keeps firing indefinitely). Grenade events just skip to walking; it never
takes cover. Hits while standing: pain and a plain flinch (86: 1:3's 6 without the by-part and
knock-down reactions) → 81; while walking: 1:3's 82 (anim 122) → 81, or 210 for a bit-10
soldier. 81 also registers the **end of a burst** (`s3C` 0x1 → 81:127: stop the fire loop, the
weapon anim 39, back to 81:18), so every burst restarts the stand and its 40..70-tick timer.

**Watched (warp)** in 2:3 (record 21, the path soldier near the start, scripts 80/81/87 as
numbered here): spawned into 80 walking its path; its `+0x40` poked with 0x100 (the group
alert) at vblank 9003 → **81 at 9006**; 87 on channel 1 from 9036 with the player 2530 away and
in view: the stand ended each time on the burst's end, not on the timer, 158 ticks after it
began (9036 → 9352, 9414 → 9730, ...: 316 vblanks every time), then 39 for 26..31 ticks and a new
87; in 8000 ticks it never walked (the player beyond 1000, as read).

**L, low-wall node** (approach side 4: 1:3's empty 112; 1:1 121, 1:2 116, 5:2/5:3 116, 7:3 113,
9:1 121; hits in the 4 scripts before it): take the node, hide (anim 274) 50..90 ticks; stay
hidden if the player aims at it while in body script 8; face the player; a grenade with 6/101
(bit 1, player ≥ 1500: anims 261, 219, 220); else 50% **rise** (275; only if query 0x12, the long
sight test, passes, else v17 += 1 and duck, and 96 leaves the node when v17 > 3): SMG three
volleys of 1..2 anim 277 at fire 1 with 4..6-tick pauses, rifle one fire 2 shot, the 0x1407
weapon 1..4 fire 2 shots (sound 2); or 50% **fire over the top** (280/281/282): SMG fire 1 for
2..8 anim 281, rifle one shot, 0x1407 1..4 shots. Hits: a flinch (anims 281/282, 277/278, 34/35,
249 for the 0x1407 weapon), 0..4 ticks, back to the node.

**G, gun crew** (a kind-0x10 node with a mounted gun: 1:3's empty 113 and 97; 1:1 122..126, 2:3
110..114, 4:1 105..109, 5:2 117..121, 5:4 113..117, 7:1/9:4 105..109, 7:3 114..118): described
under The mounted gun. Hit: flinch (anims 118..121; 36..39 at approach side 4) and remount;
dying or the gun gone: 97 releases it (0x1000 | 0x400), shows the weapon, clears the nodes.

**T, sentry** (`s40` 0x400; 1:2 202..213, 7:1 194..205, as 2:1's 202..213 in soldier.md).
In 1:2's numbering: 202 stands (209 on channel 1: anim 0 1..2 times (+1 with a skill pass), then
anims 209, 210 ×n, 211 or 1, 207 ×n, 208) and every v0B ticks tests 208 (the player within v5
and aiming at it, query 0xF → 204) and 44 (detection → 203); 203 and 204 drop back to 202 when
the player is beyond v5 (3000). Fire (213, from 210/211): anim 240 v3 times, then, unless the
player is within 565 and unseen, fire 1 during anim 241 and anim 242 (sound 5).
**Watched (warp)** in 1:2 (record 81, spawned by trigger 23 at the level start; the sentry
stands on a height 381 above the ground): with the player 1689 away it stayed in 202 (pc
202:23, the `wait v0B`) for the whole run; teleported to 900 away (977 in 3D) at vblank 9003, it
**detected at 9007** and stayed in 203/210 for the rest of the run (400 000 vblanks). Cycle:
anim 240 26 ticks per play, fire state 1 for 15 ticks (anim 241), anim 242 36 ticks. The play
count v3 over 1363 cycles: 1: 101, 2: 121, 3: 369, 4: 348, 5: 211, 6: 213, i.e. 1..4 uniform
plus 2 with the skill pass (skill 67: P(pass) from the counts 0.62..0.67).

**A, alarm runner** (3:3 187..193, as 2:1's 195..199): with no alarm up, an alerted or engaging
soldier runs to a free kind-8 node within 1500 and raises the alarm there (70% per tick at the
node); the alarm then sends every soldier to 188 (50% stand and watch, 50% search) instead of
1:3's empty 195.

Watched in 2:2, where the same scripts are 200..204 (2:1's 195..199; 2:2's soldier list has
five more scripts) and the alert is 308 (2:1's 303): a soldier alerted at vblank 53402 (204:
anim 215, a shout through 315) went to 200 at 53481 with the run (206) on channel 1, reached
the kind-8 node at 53535, played anim 283 (11 ticks) and **raised the alarm** at about 53558
(the 70% branch on the first try), then 308 at 53571. At 53566 a soldier 3238 away entered 201
(the alarm pulse) and, a path soldier, went on to 79 through the search; later alarms (57073,
57158, 57186) sent three soldiers through 201: two to 303 (2:2's stand and watch) and one to
180 (search), the 50/50 as read.

**F, foxhole ambush** (2:3 `s40` 0x1 → 209 and 205..217; 4:1 `s40` 0x1 → 200 and 196..208):
hidden (anim 139, 30..90 ticks, then 50% 140/142 or 143/145 with holds), detection every 4 ticks
→ a skill roll (45): pass → the faster loop, fail → the slower; both face the player (anim 147,
30..90 ticks, anim 134) then **pop up and fire**: aim (anim 135), wait 5..20 (fail) or 10..40
(pass), one (fail) or 2..5 (pass) bursts of anim 135 at fire 1, duck (anim 146, 30..90 ticks); a
bit-1 soldier then throws from the hole 61/101 of the time (anim 146 from frame 8 with the
take/lob/done attachment bits) and waits 120..160 (120..125). Unseen (query 0x10) → hide again.
Hit: below 10% the fall-and-writhe death (1:3's 198), else anims 136 (70%) or 137, 138, 147,
60..180 ticks, back to firing. A second message, `s40` 0x4 (sent on the record's msg 6),
**jumps out**: anims 157, 158, 159 looped until it lands (`s38` 0x2), 160 → 210 (engage).
**Watched (warp)**, the hidden part only, in 4:1 (record 94, spawned by trigger 70 with the
player 3373 away and never detected): channel 0 in 200 polling detection every 4 ticks;
channel 1: 206 (anim 139 = 16 ticks + 30..90) measured 46..106 ticks (503 times), then 207
(268) or 208 (235), 60..120 ticks each (anims 140+142 = 30 ticks, 143+145 the same, plus the
30..90 hold). The pop-up fire was not reached.

**H, hidden ambusher** (`s40` 0x200000, sent at spawn and again on msg 0x98; 3:4 199/206..208,
4:2 = 7:2 207/214..216): stop, hides its weapon (v1A = 1, 0x08000000), channel 1 loops 1:3's
unreferenced 213 (anim 310); a comrade-hit report → run to the spot. Wakes when the player is
within v07 (model `0x11EDCD18`, 7:2's nine SMG soldiers with aim error 0) or, for other models,
when the player is looking at it (sensor 0x71) or within 200, then 60 ticks; gets up (anim 311),
faces the player, anim 237 1..2 times, 50% anim 239 3..5 times → 17. Hit while hidden or
rising: below 10% death, else anim 312 → 17.
**Watched (warp)** in 3:4 (record 5, in the room next to the start): spawned at 8424 into 199
with 206 (anim 310) on channel 1. With the player at the start, 590 away and turned toward it
for 4000 ticks, it never woke (sensor 0x71 needs a clear sweep from the player's eye to its eye:
a wall, presumably). Teleported 150 away at vblank 9003: **207 at 9126**, 61 ticks later (the
60-tick wait); 207 → 17 after 170 ticks (anim 311, 237, the 50% 239s); then 25.

**W, worker** (`s40` 0x80000; 5:1 205..210, 5:2/5:3 208..213): sets bit 3, hides its weapon,
walks its path (mode 2, no walk goal, anim 255 with footsteps); at each end pauses the path and
works (anim 233/235, sound 0x21, then 10..20 cycles of anims 229..232 or 306..309 with 20..70-tick
holds), 30% again, else walks on. Detection is **hearing only** (query 0x11) or the player within
500 when detectable: no sight. Startled (206): face the player, anim 237, 20% a cover search,
anim 238 (sound 0x22), anim 239 2..4 times, 55% anim 303, repeated while the player aims at it,
then 17 (engage, which shows the weapon again). Bit 3 makes it keep cowering when aimed at in
combat (25's bit-3 branch) instead of dodging (inferred to be the reason it is set).
**Watched (warp)**, partly: in 5:3 (two workers spawned with others by trigger 132)
the workers started on their paths (211, about 22 ticks) and the work script (213), and were
startled (209) 26 and 76 ticks into it with the player 2960 and 2135 away, beyond their 500:
by hearing, the soldiers spawned with them having spawned alerted and shouting (alert shouts
id 0xF within range; the workers' detection writes v1, so `ver_hear.py` doesn't list it). In
5:2 record 24 (trigger 25) was startled on its first tick, with fighting going on 3000 away;
209 lasted 100 ticks. A full work cycle was not seen.

**C, crawl-space chaser** (5:1 211..219; `s40` 0x40 with `set_home` to the sender, 0x80 at
spawn, 0x20): if the player is not in a low zone (flags B 0x800) → engage; else runs (anim 14)
to its home point; there, the player within 1000: two back steps and fire 1 over two anim 20;
otherwise goes prone (`to_spawn`, anim 179 with shout 0xF, anim 180 with sound 0x23), property 3
(eye height) = 50, and **crawls at the player** (seek, anim 180); every 2 ticks seen and within
±350 → prone fire (219: bit 1, ≥ 820, a skill pass and 50% a prone grenade (anim 313); else SMG
2..5 rounds of fire 1 over 3 × anim 184, rifle 2..3 fire 2 shots with the bolt; anim 185, wait 10;
the fire roll every 15 ticks). `s40` 0x20 → eye height 188, stand (anim 61), engage. Prone
hits: anims 130..133/112; death anim 103. **Who sends what** (5:1's record script 17 and the
zones, `lvs.py`): 0x80 at spawn; 0x40 with `set_home` on message 0x19, relayed by the
tag-group records 123/124 (scripts 85/86, messages 0x5F/0x60) to its own zone; **0x20 counts
characters, not the player**: the low-zone boxes of its tag group send 0xF/0x10 for every
character entering or leaving them (their `c_enter`/`c_leave` lists), the record counts them
from 128 and sends 0x20 when the count is back to 128, i.e. when it (or any soldier) has
crawled out of those zones. (Earlier text here said "the player out of every low zone": the
player's own zone messages are 0xD/0xE, which only level script 13 handles.) **Watched
(warp)** in 5:1 (record 134, own script 16; reached by streaming, Warped runs): the player
teleported into its spawn box (trigger 135, enter 3, leave 0x60) at 9404, inside low zone 288
(flags B 0x21000 → 0x21800), and walked out of the box: it spawned at 9408 (9900 away, its record
point), got 0x40 from record 267 (script 86: 0x60 → 0x19, `set_home` to that box) and at 9452
ran home (211, anim 14, 40 vblanks); at home with the player 1468 away (beyond the 1000), 212
took the far branch (9492: `to_spawn.13`, anim 179 with shout 0xF, anim 180), then crawled
(9590: 213 with 218 on channel 1, eye height 50, anim 180 with sound 0x23) from 1387 to 736 in
324 vblanks, and at 9914 saw the player within ±350 → 214/219. It is a rifleman: each cycle 2..3
shots (fire 2, sound 4) each followed by the bolt (anim 184 then 183 twice), anim 185, wait 10,
and the fire roll every 15 ticks (219:28) when it fails; it stayed there for the rest of the run
(to vblank 104000) with the player in the low zone. In a second run the player moved on at 10500
(but stayed in low zone 262, stuck on a wall): the chaser lost sight at 10536 (back to 213),
crawled after him and fired again from 609 at 10936. The player leaving every low zone (→ 230,
engage) and the 0x20 stand-up were not reached.

**Low zones and sight** (flags B 0x800, soldier.md, The player's concealment flags). Besides
the halved sweep of query 0xB, the scripts read the flag directly (sensor 0x6B,
`player_low_zone`): in 5:2's kneeling fire (156, 1:3's 148) a player in a low zone skips the
"too close / too far" exits, and C's chase is built on it. **Watched (warp)**, without a
control: in 5:2 the player was teleported into low zone 109 (a box 2140 wide) and flags B went
0x21000 → 0x21800; the worker it spawned (record 24, 1259 away on the same floor, startled and
then fighting) cycled for the rest of the run (1195 times) through kneel (156, 31 ticks) →
**lost sight** (query 0x10 = 0, 158, 41 ticks) → search (183, 246 ticks: 13 walk cycles toward
the player without seeing him) → 218 (4 ticks) → 156, never getting a clear look at a player
standing in the open at 1259. That fits the halved sweep (the sweep aimed at half the
player's eye height hits something low between them) but two control runs outside the zone
failed (no floor at the chosen points), so it is not separated from the geometry.

**X, assault soldier** (`s40` 0x2000000; 7:1 206..217 = 7:3 205..216): idle fidgets, detection
every 2 ticks (a comrade-hit report: walk to the spot, anim 215) → face the player, anims
286/292, shout 0x18, 1..2 bursts at fire 3 (anim 288); with the player within 1500 and 650 clear
behind it advances (anim 290) while 400 ahead is clear, barks, 3..4 bursts at fire 3, then 2..3
more (anim 285 or 293). Then forever by range: beyond 500 strafe-fire (anims 11, 291, 10, 291 at
fire 3, each 1..3 times, until a side probe is blocked); 300..500: 20% bark and bursts (285),
20%/25% strafe either way, 10% a pause (292), 25% bursts (293); under 300: back off (anim 9,
1..3 × anim 15 with 175 clear behind). Fire 3 goes along its facing, which the scripts keep
turned to the player. Hits 207/208: stagger (124/125) and continue; others (and hits by type
0x13F7) drop it into the ordinary pain script 6. No cover.
**Watched (warp)** in 7:1 (record 44, spawned by trigger 103; reached by streaming, Warped
runs): spawned at 9408 when the player was teleported into the trigger 5695 away, it stood in
211 (idle, 229 on channel 1) until the player, walking past it up the ramp, came within 1521 at
9546 → 213: anim 286 (9546), 292 (9578), shout 0x18 and anim 287 (9640), one burst at fire 3
(v16 = 3, anim 288, 9678..9778), 289, 292; no advance, the player being already beyond 1500
behind it. At 9864 (player 3812 away) → 214 with the strafe-fire loop 215 on channel 1 (anims
11, 291, 10, 291, fire 3 throughout): the strafes, facing the player, carried it **toward** him,
3812 → 412 in 1200 vblanks (about 2.8 units a vblank). From 10968 (494) the 300..500 choices
(216: anims 285/28, strafes 11/10, 292, 293), at 12080 (290) one back-off (217, anim 9) and back
to 216; it kept 300..400 from the standing player to the end. Hits not tested (god mode, no
shots). 7:3's copy (205..216, the same code) was not run.

**R, technician** (9:4 225, `s40` 0x200000): never fights: no hit events, weapon hidden, waits
50 ticks, walks its path (anim 334), works at the end (anims 323..333); a second 0x200000 (level
script 34's msg 0x98, from a counter reaching 44 in script 72) makes it leave: walks off (anim
330), health percent -1, drops its grenade attachment bit (op 0x29 0x20000000), removed. (By the
rocket, inferred.) **Correction**: it also leaves **on its own** after one work cycle (225:180 →
202, the same exit as the message: 50% two anims 324 and one or two 329 first), and with path id
5 it walks until the path's end event (0x800000), otherwise five cycles of anim 330, before the
removal. **Who spawns it** (9:4 has no streaming records, so everything is resident): the
records 30..32 have no trigger; their tag group 0x1000E is record 0's (level script 72), which
on message 0x3E starts counting the periodic 0x59 (about 3 a second) and sends **3** (spawn) at
10, 0x98 (leave) at 44 and 0x20 at 53. 0x3E comes from record 35 (the kind-2 object with script
39) after 126 periodic messages, counted from 0x2C, which is the **Use list of record 5** (a
box at (1291, 313, 937)). **Watched (warp)**: the player teleported into record 5 on the first
`player_update` and pressed Use at 8700: the technicians of records 32 and 30 spawned at 11462
and 11468 (two free slots), anim 0 and the 50-tick wait (to 11574/11580), walked their paths
(anim 334) to 11764/11854, worked (332, then 323, 327, 329, 324 or 333), and both turned to
leave (anim 330) at 12136, the 0x98 tick (674 vblanks after the spawn: 34 periodic messages,
19.8 vblanks each); removed at 12328 and 12386. Record 31 had waited for a slot and spawned at
12334 in the first one freed (after its 0x98, so only the work cycle sends it away): path to
12637, work (332, 328, 325, 324, 329) and anim 330 at 13189, removed at 13385. 9:4's 196..206, a holdout shooter on `s40` 0x800000, is registered but no
level script sends that bit: unused.

**The disguise levels** (2:1, 2:2, 3:1; soldier.md, 2:1's soldier). 2:2 = 2:1 plus P (registered
on `s40` 0x4000000, but no 2:2 level script sends it: unused) and fixed voices (voice 1 always
sound 0x2A, voice 2 always 0x2C, where 2:1 picks 0x2A/0x2B and 0x2C/0x2D). 3:1 = 2:1 without
peeking, with four voices for roles 0/2/4 (v25 1..4; voice 4 sound 0x2F), and H (as 3:4's;
`s40` 0x200000 → 293, wake-up 301).

The papers chain, from the level scripts: an inspecting soldier (228) sends message **0x75**;
its record's script then listens for **0x33**, which the soldier sends when the player is
within 500 holding the papers (weapon 7) out (`+0x11E` = 8); the record asks the papers script
(13, message 0x32), which answers by the papers' grade (0 → 0x6B, 1..3 → 0x34..0x36; the grade
rises when papers pickups or a zone send 0x2F/0x30/0x31), and the record sets the verdict on
its soldier: `set_flags` 8 refused, 4 accepted, 1 accepted below skill 40, 2 below skill 80.
Only the records holding that chain (2:1's s42 and s113; 3:1 also s103) can give a verdict: their
soldiers have roles 3 and 4, and only roles 3 and 4 inspect (227 goes to 228 only for v11 ≥ 3).
**Correction** (an earlier line here said an inspection by others times out): soldiers of roles
0..2 never run 228; they ask in 242 and **any papers held out satisfy them** (258: vA = 0, a
gesture and a voice line, then calm in 241), whatever the grade. Watched in the replay of 2:1
(soldier.md, Papers and suspicion: refused at grade 0, accepted at grades 1 and 3, role-0
soldiers calmed by the papers). Message **0x1E** (a
role-1 soldier whose suspicion vA reaches 40, or entering certain zones) makes every other
subscriber's suspicion rise (`s40` 0x20000 → 244: vA += 20..21); **0x1F** (zones) calms them
(`s40` 0x40000 → 245: 5% calm at once, else vA -= 0..20). Record 20's script toggles its
soldier between standing (`s40` 0x10000 → 220) and walking its path (0x8000 → 221). Roles come
from the spawn message: 0x200 role 0, 0x1000 role 1, 0x1000000 role 3, 0x10000000 role 4
(0x2000, 0x4000 and the sentry's 0x400 are not sent in these levels).

**Bit 7** (var 263) marks a soldier that accepted the papers and then sees the player without
them in hand (229 → 230, which sets it, resets the role to 0 and vA to 70 or 40 and drops the
path): it then skips the papers check (242:32), looks around with anim 273 instead of 272, and
when alerted goes straight to combat (19) without the surprise animations (inferred meaning).

The player's `+0x11E` is only ever compared with 8 (2:1 228:71, 242:44, 261:19/53): body script
8 is the player's "use the item in hand" (fire, or hold out the papers; inferred). So the
classifier's classes 3/4 are "seen firing" (split by the soldier's `+0x29C`) and 5 "heard while
firing"; soldier.md's "+0x11E = 8, 3 or 4" reads as the classes.

## Resolved items

- **Events 0x0D/0x0E/0x42 and `send_event`**: above.
- **`+0xEC` bits**: bit 1 from the template (grenades); bit 2 from the level message `s40`
  0x8000000 (registered by the start script as a stored `set 0x102 = 1`); bit 3 set by 5:1..5:3's
  own scripts; bit 7 by 2:x's (level differences). Bits 4, 9, 10, 15..20 are the scripts' own
  (soldier.md).
- **Route results**: `route` (op 0x37, `0x8005BAC4`; 0x3A to a node, `0x8005BDC4`) on the
  target entity's position, `0x80056B00`: the first result variable gets the turn needed
  between the heading and the direction to the target, `a` = |difference| (4096 units per
  turn): **0** when `a` < 601 (53°) or 1500..1600, **1 or 2** for 601..1499 (the two sides,
  by the sign of the cross product), **3** for 1601..2299 (behind); the second gets the signed
  difference (16.16). The scripts play a turn animation by it (1:3's 75: anims 2/4/3; the
  dog's 18: 22/23/4) before moving.
- **v1A/v1B** (the hook `0x800523E4`, from `enemy_think`): v1A is a bit mask over the soldier's
  **attachments** (the list at weapon block `+0x18`: 0 the weapon, 1 the head gear, 2 the
  grenade; 0x144 bytes each, flag 1 attached, 0x20 detached) plus an action bit, v1B its
  argument: 0x10000000 set the attachment's value to v1B; 0x08000000 hide it; 0x04000000 show
  it; 0x20000000 on the grenade: **drop it** (a grenade, `grenade_spawn` model `0x61282777`,
  falling from the hand); 0x02000000 on the grenade: **throw it at the player** (speed
  30 + (d - 700)/16 up to 100, +5 beyond 1300; aimed at the player's middle; upward speed 30,
  35 beyond 2000); 0x01000000: **lob it from cover** (speed 50 + (d - 700)/16 up to 150, along
  the direction to the player); no action bit (the deaths' 1..3 | 0x80000000): **knock the
  attachment off** (the weapon or head gear flies off from the hit part v1B, `0x80051200`).
  So soldier.md's 0x61282777 "helmet" is the grenade.
- **The gait table** (brain `+0xCC` speeds, `+0xD0` bases): written by spawn, `brain_seek`
  (base × the slow-down scale) and `brain_reset_gait`, **read by nothing**: no effect. The
  speed scale that matters is the entity's `+0x224` (read by `anim_root_motion` and
  `entity_move`).
- **Brain `+0xB4`**: the field of view, a half-angle: the sight test (`0x8005A0C8`) takes the
  facing of the head bone (brain `+0xAA`, 13) from two of its points, and the target must be
  within ±`+0xB4` of it (record `+0x6A` × 11: 90 → 990 units, ±87°). So head-turning animations
  move what a soldier sees.
- **`+0x3C8`** (60 for soldiers, 80 for dogs; brain `+0xCA` its base, scaled by the slow-down):
  the character's radius against solid objects' planes in `0x8004B6DC` (the touch push-out).
- **Who reads `+0x38` 0x400000/0x800000/0x1000000**: only script events (1:3 soldier: 94, 176,
  201 for arrival, 79/85/88 for path ends; the dog: 2, 16, 20).
- **The player's weapon-block bit 1** is **crouching** (`player_crouch_done`, `0x800798AC`, sets
  it; movement.md's inv flag 2). Crouched: sight (query 0xB) and the long range (0x10) are
  ×0.75 (0x12 is not), the vertical aim error is halved (enemies.md), and the soldiers' distance
  thresholds v5/v6/v7 become 3/4, 3/8 and 1/4 of the range instead of 1, 1/2 and 1/3
  (`0x80055530`, every think).
- **Behaviour goal 0x2000** (soldier.md Open): `behaviour 0x2000` pauses the path goal
  (clears bit 2 without forgetting the point), `-0x2000` resumes it.
- **Query facts** added: 0x12, the sight test with the long range (`+0xB2`) not reduced by
  crouching (the gunners' and low-wall soldiers' "can I fire" test); 0xE reads `[0x80099EB4]`.
- **1:3's alarm event** goes to the empty 195 once (the bit is a one-tick pulse): channel 0 runs
  off it into the weighted idle 202. Levels with the alarm runner (2:x, 3:1, 3:3) fill it.

## Open

- 9:4's holdout shooter and 2:2's path advance are registered but never triggered by the dumped
  level scripts; 1:1's level scripts 75/89 send 8 and 1 on msgs 0x5B/0x8A to unknown targets.
- The soldier's `+0x29C` (splits the disguise classifier's classes 3 and 4); ops 0x12 and
  `to_spawn.13` in 5:1's crawl.
- What the second-range ("hear") value means to designers: it is a sight range for queries 0x10
  and 0x12 and for the witness check, while hearing uses the first range.
- The 0x1407 weapon's identity (enemies.md calls it a pistol, soldier.md an MG; its burst is 11
  shots 12 ticks apart).
- Mission indices 6 and 8 crash when warped to: whether the shell ever maps to them.
- Not reached in play: the low-wall nodes (L: approach side 4; no soldier took one in the
  runs); the sentry's "aimed at" branch (204); the foxhole's pop-up fire; a worker's full work
  cycle; the dog's 16 (35 watched since, Grenades); 7:3's assault soldiers (7:1's watched); role-4 inspections and
  2:2/3:1's papers (2:1's role-3 chain and role-0 checks watched, soldier.md). Watched only in
  part: workers (W), low-zone sight (above), the crawl chaser (C: no stand-up, no engage on the
  player leaving the low zones), the assault soldier (X: no advance, no hits). Reached since:
  X, C (by streaming the chunks, Warped runs), the technician R (via record 5's Use) and the
  papers chain (the replay).

## Names

```
80052AF4 witness_report             # hit this tick: victim, spot (0x8009817C), up to 5 living soldiers/dogs as witnesses
80052C38 witness_check              # once per frame: last witness sees the victim (range +0xB2) -> +0x3C 0x10000
80098164 g_witnesses                # handles of up to 5 witnesses; +0x14 the victim, +0x18 the spot, +0x20 the count
80098178 g_witness_victim
8009817C g_witness_spot             # sensor 0x6E; soldier script 201 and dog script 16 run here
80098184 g_witness_count            # u8
80055528 enemy_ranges_update        # v5/v6/v7 from the brain's +0xB2, reduced while the player crouches
80099B2C g_range_third_set          # once-flag for 0x800AB930
800AB930 g_range_third              # first soldier's +0xB2 / 3: v7 for every standing-player check
80056250 enemy_world_contact        # floor: +0x38 |= 2; wall: +0x3C |= 2
80056288 enemy_remove               # +0x384: soldier count, record state |= 2 when flags A 0x20000000
8005291C enemy_touch                # push out of solid objects (0x8004B6DC): 1 on top +0x38 |= 2, 2 side +0x3C |= 2
8004B6DC sphere_vs_object_planes    # 1 standing on it, 2 against a side
80054B80 dog_draw
80054C24 dog_hit_part
80054D70 dog_remove
80054E14 dog_world_contact
80099B20 g_dog_count
800523E4 enemy_attachments_op       # v1A/v1B: set, hide, show, drop/throw/lob the grenade, knock off
80051200 attachment_knock_off
80056B00 route_turn_kind            # 0 ahead, 1/2 sides, 3 behind; *out = signed angle
8005BAC4 op_route                   # op 0x37
8005BDC4 op_route_node              # op 0x3A
8005C210 op_send_event              # op 0x43
80058C38 op_event_0d                # -> sop_event
80058C58 op_event_42                # -> sop_event
80056AEC enemy_home_changed         # +0x3C |= 0x8000
8006D788 entity_home_changed        # soldier -> 0x80056AEC; 0x1774 -> 0x80090938
8005A0C8 sight_test                 # head-bone facing, FOV brain +0xB4, range, blockers, world sweep
8005A574 op_query                   # op 0x2E
80069514 lvl_register_msg           # built-in 0
800694C8 script_handler_get         # a script's Nth handler {message, offset}
```
