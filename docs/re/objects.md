# World objects, randomness, tick order

The game's clock and dice, and the things in a level that are not soldiers: doors and other
scripted objects, destructibles, the mounted gun, grenades and explosions. Read from the code
(`fn.py show`/`c`), checked against RAM dumps (`tools/recon/obj_ents.py` lists a dump's
entities) and against replays of `recordings/2026-10-19_22-22-43_m1l3` with `VALOR_WATCH` and
`VALOR_TRACE_DAMAGE` (headless; see "Replays" at the end for a caveat).

## Randomness

### The generator

There is one random number generator, the PsyQ library's `rand` (`0x8001552C`, in the exe, so
the shell and the level overlay share it):

```
seed = seed * 0x41C64E6D + 12345        (32-bit, at 0x800CA2A4)
return (seed >> 16) & 0x7FFF            (0..32767)
```

No other generator exists: the only multiplier 0x41C64E6D in LEVEL.BIN, SHELL.BIN and the exe
is `rand`'s (`fn.py grep`), and nothing else writes `0x800CA2A4` (`fn.py uses`).

**Seeding.** `srand` (`0x8001555C`) has one caller, the shell's start-up (SHELL `0x8005FE04`,
after the pads and CD are set up and before `global.rsc` loads): `srand([0x80039C68])`. That
word is in the exe's data, so it survives overlay loads; the shell adds 1 to it on every change
of a menu button's state (about 40 places in SHELL `0x8005D598`) and on each switch between the
two title scripts (`0x80058930`). So the seed is "how many button edges the player has made in
menus since power-on" (inferred: the counter is never cleared). LEVEL.BIN never reseeds: a
level starts with whatever state the shell left (1:3 in the replay: 0x46AD1C00).

Usages, all of the form `rand() * N >> 15` (uniform 0..N-1) or `rand() % N`:

| System | Where | Draws |
|---|---|---|
| Soldier scripts | `script_run_a` (`0x8003F8F0`) | Weighted pick of the next script when one ends (idle variety) |
| | op 0x1E (`0x8005959C`) | `rand() % 100` against a percentage (soldier.md's "N/100" rolls) |
| | op 0x21 (`0x8005989C`) | A roll that fires 16 times in 480·N calls, then a value in [lo, hi] |
| | op 0x02 sound (`0x80058420`) | Sound pitch/volume spread |
| | `0x80052084`, `0x80072244`, `0x800727B4` | Hit reactions and animation choices |
| Enemy firing | `enemy_weapon` (`0x8005163C`) | Burst lengths, pauses, aim error (enemies.md) |
| | `0x80055198` (from `enemy_think`) | Aim offsets |
| Bullets | `bullet_spawn`, `pellet_spawn` | Speed 600 + rand·70/32768; weapon 11 and pellet spread |
| Player | `fire_gun` | Shell casing 49%; `player_hit_part`: grunt 30%; `0x80079D10` (from `player_update`): a 50% sound |
| Helmets | `0x80051200` | Knocked off 50%, its flight |
| Mounted gun | `mounted_gun_think` | Spread of each shot, shell casing 49% |
| Grenades | `0x80045808`, `0x80044120` | Spin of the model; bounce sound |
| Explosions | `explosion_spawn` (`0x800719C0`) | Every fireball's and smoke puff's place, spin, size, delay |
| Alarm | `0x8005EA54` | Siren choice and timing |
| Effects | `0x80073BA8`, `0x80070DEC`, `0x800702E0`, `0x800708F0`, `0x8006EAC0`, `0x80088040` (shell casing), `0x8004E354` (decals) | Particles |
| Sound | `0x80041B04`, `0x80041CE8` (play a sound), `0x80041FC4` (voices), `0x80042250` (ambient, from `0x8004137C` after drawing), `0x80042DF8` (explosion sounds), `0x80043140` (impact sounds) | Pitch and volume spread, which sample |
| Drawing | `0x80088368` (from `render_pass`), `0x80088744`, `0x80089174` | Visual jitter |
| 0x1774 objects | `0x8008F5B0`, `0x8008F7E4`, `0x8008FA60` | Their smoke and fire |

**Measured** (`tools/recon/obj_rng.py` on a watch of the seed, `VALOR_WATCH="seed=800CA2A4:4"`,
which logs it once per tick; the script counts the LCG steps between logged values): 1:3 from
its start to vblank 12000 draws 13256 numbers in 933 ticks with a change, 2.8 per tick on
average; most ticks draw 1 to 10, bursts of 27..35 and 70 come with explosions and shotgun
blasts.

So gameplay draws (soldiers' choices, aim error, bullet speed) are interleaved with sound and
drawing draws, which depend on what is on screen and audible. **Matching the original's random
sequence exactly is not practical** short of reproducing every consumer in order; a
reimplementation should match the distributions (given per system in these documents), ideally
with separate streams so that, e.g., a particle effect does not change a soldier's decision.

Two pseudo-random things do not use `rand`: world animation mode 1 steps frames by 23 modulo
the count (World animations), and the alarm entity (`0x8005EDF8`) times itself with `VSync(-1)`.

## The frame and the tick

`game_frame` (`0x800616E4`), called by the level's main loop (`level_entry` `0x80064374` →
`0x800640B4` → `0x800616D4`), once per drawn frame:

1. Streaming: if no CD job is running and a chunk read is under way, its end (`0x8008C798`:
   `tsp_parse`, the slot's instance, `g_chunk_loading` = -1); copy streamed music (`SpuRead`).
2. `0x8008A580`: the deferred frees (`0x8008A4F8`'s list at `0x8009F888`: each entry's count
   down, the block freed at 0; destructors free entity blocks and pose buffers two frames
   late); `0x8008EAC8` (`char_pool_update`, the character slots' and pose buffers' countdowns).
3. `input_poll` (`0x8004DE3C`).
4. Paused (`g_paused`, `0x8009A5E0`): only `input_poll` once per tick due; the 1/3-second
   timer below is set to 0. No entity moves, but the frame is still drawn and waited for
   (step 6): the out-of-sight counters count down and `entities_draw` removes what reaches 0,
   and the idle work runs (1:3: 16 paused frames after tick 6950 counted a pickup out).
5. Otherwise, for each tick due (1 + `g_ticks_dropped`, at most 4):
   1. `g_tick` (`0x8009A5DC`) += 1.
   2. `entities_update(g_entities)`: every entity's think, move, triggers (below).
   3. `entities_collide(g_entities)`: contacts and hits (entities.md).
   4. `texts_update` (`0x8005F69C`).
   5. `input_poll` again if another tick follows (each tick reads fresh input).
   6. The **1/3-second message**: a vblank counter (`0x8009A5C0`, +1 per vblank in the vblank
      handler `0x80090D38`) at 10 or more: subtract 20 and `send_event(0x59)`. Level scripts
      use message 0x59 as a periodic timer (1:3's objective script repeats a sound with it).
6. `render_pass` (`0x800503D4`): draws the frame, then at its end (`0x800504A8` →
   `0x80090B80`) **waits for the vblank, and does the level's slow work while it waits**:
   - once: `frame_idle_job` (`0x80090A30`), a round-robin on `0x800AB8E8`: state 0 runs
     **one** queued message (`msg_run_one`); 1 one message and one queued spawn retry
     (`0x80074F9C`); 2 one message, the handles of removed entities freed (`0x8006DA88`),
     one more message and the world-chunk streaming check (`0x8005439C`, levels.md), and the
     state goes back to 0 (so the states run 0, 1, 2, 0, ...; the code for a state 3, one
     message and the streaming check, is not reached);
   - then, until the vblank count changes, `frame_idle_loop` (`0x80090AEC`), a second
     round-robin on `0x800AB8EC`: 0 **all** queued messages, those queued meanwhile included
     (`msg_run_all`, `0x800663B8`); 1 all spawn retries (`0x80075034`); 2 `frame_idle_job`
     again; 3 nothing; then 0 again.
   - How many of these run is **real time**: a frame that ends just before the vblank gets
     only the one `frame_idle_job` (1:3's slow frames at ticks 2210..2215 freed the removed
     handles only every third frame). A reimplementation that must match the original's
     handle slots replays the recorded sequence (the runtime traces each call under
     `VALOR_TRACE_INPUT`: `idlef TICK` at each frame's `frame_wait_vblank`, then `idle TICK:
     STATE` and `idle2 TICK` per call; paused frames repeat the tick); otherwise running all messages,
     all spawn retries and the handle freeing each frame is the fast-machine behaviour.
   - then the ticks due for the next frame: `g_ticks_dropped` = (vblanks since the last frame
     + parity) / 2 - 1, so a frame that took 2 vblanks runs one tick, 4 vblanks two, and so
     on: the game advances at **30 ticks per second of real time** whatever the frame rate,
     up to 4 ticks per frame.
7. `0x8004137C` (sound upkeep: it calls the ambient-sound player `0x80042250`, inferred),
   `0x8006B894` (empty).

**Consequences** for a reimplementation:
- Within a tick, every entity moves before any collision is tested, and the contact handlers
  (damage, pickups, bullets) run after all moves.
- **Level-script messages are deferred to the end of the frame.** A trigger volume entered,
  a door told to open, an objective completed, a script's `send`: these are queued
  (`send_event` → `0x80066EA0` checks the subscriptions and appends a 32-byte context to the
  ring at `0x800B6460`) and run after the frame is drawn, before the next frame's ticks. The
  effects show up one frame later (on a 30 Hz frame: the next tick). At least one message per
  frame is guaranteed; normally the idle time before the vblank runs them all.
- Two script entry points are immediate: a destructible's hit runs its message 1 at once
  (`script_run_message`, `0x800664A0`, from `destructible_touch`), and a model's spawn
  script.
- **Spawning by records** (`record_activate` → `record_spawn`, `0x8007505C`) is immediate when
  it can be; a refusal (the soldier cap `0x80097FEC`, levels.md) queues the record (ring of 64
  at `0x8009E294`, count `0x8009E288`) and it is retried at the end of every frame until it
  succeeds.
- **Removal** (`entity_remove`, `0x8006B8F0`) is immediate: flag A 0x80000, unlinked from the
  list, its `+0x384` destructor runs; its handle stays in the table (so handles held by others
  resolve to the dead entity, flagged) until the end-of-frame cleanup frees it
  (`0x8006DA88`, which also clears the global pointers that named it).

### The order of entities

`entities_update` walks `g_entities` in list order (parents first, entities.md). The order is
set by how entities are linked:

| Linked by | Where | Who |
|---|---|---|
| `0x8006B834` | **Right after the camera** (the list's head, `g_player` `0x8009D638`) | Everything spawned from records (`0x80074C54`, `0x80074CE8`), explosions, particle effects, the alarm, pickups, shell casings, the level's own entities (`level_load`) |
| `entity_link` (`0x8006B854`) | **At the end** | Projectiles only (`projectile_create`: bullets, pellets, rockets, grenades) |

So a level's list reads (2:1 at vblank 30000, `obj_ents.py`): the camera; the most recently
spawned entity, ..., the oldest; the world entity (type 3, `g_world`); the player's soldier
(type 1, made first); then the projectiles in the order fired. In a tick:

1. The **camera** (type 2, think `0x800473DC`) updates first, from the soldier's position of the
   previous tick (inferred: the view lags the soldier by a tick).
2. Spawned entities, newest first: soldiers (think, scripts, brain, `enemy_weapon`: their
   bullets are appended), doors, destructibles...
3. The **world** (type 3, think `0x80053C00` → `0x80053AC0`): steps the level's colour cycles
   (header `+0xDC`, `[+0xD8]` entries of 0x14 bytes {count, list, index, colour, ticks left}:
   each list entry is a colour whose high byte is how many ticks it shows; the lights of
   flickering lamps, inferred).
4. The **player's soldier** (think `player_update` `0x8007A6B0`: controls, aim; then
   `player_weapon_update` from the entity update: firing and throwing). Its zone test (trigger
   volumes, `zones_update` `0x800845A8`) runs right after its move.
5. **Projectiles**, oldest first, including those fired this tick ahead of the loop: the walk
   reads an entity's `next` before its think, so a bullet appended at the list's end moves in
   its firing tick only when the loop hasn't passed the end yet. An enemy's bullet (enemies come
   before the player) moves its first stretch at once; **the player's stays at the muzzle for its
   firing tick** unless another projectile was already alive (416 of 421 shots in the replay,
   `tools/sim/combat_sim.py`, combat.md). An entity created by a projectile's
   think (an explosion) is linked after the camera, so it already passed: it gets its first
   think next tick, but it is in the list for this tick's collision pass.

Triggers: the player and walkers test their zone's records in `zones_update` right after their
own move (levels.md, Triggers), so enter/leave messages are queued in list order during
`entities_update`.

## Doors and other scripted objects (type 0x1776)

Doors are models of type 0x1776 (`0x8005E648`): solid (flags A 0x03008400, mask 0x88214051,
flags B 0x44030), no hit parts but a collision shape from the model's box (`0x8004A8F8`:
planes at `+0x300`, count `+0x2EE`), and a think that only runs the model's scripts
(`0x8005E61C`: `script_run_b`, `script_run_a`). Everything a door does is in its model's
scripts; the level only sets bits.

**Read from 2:1's door model** (0x7380B389, 5 doors at vblank 30000, `sst.py`):

Script 1 (the idle one) registers events (op 0x42) on the entity's **`+0x40` word**, which the
level built-in `set_flags(N)` (built-in 3) ORs into. An event fires when all its bits are set
(the event store, `0x80058C78`, and `script_run_b`'s test: mask bit 0x40 compares `+0x40`;
bits 0x4/0x8 `+0x38` all/none, 0x10/0x20 `+0x3C` all/none, 0x40/0x80 `+0x40` all/none):

| Bit set | Script | Motion |
|---|---|---|
| 8 | 8 | Yaw +25 per tick (op 0x25 adds to the angles) for 41 ticks: **swings 90°** (1025 units of 4096) in 1.37 s |
| 1 | 9 | Yaw -25 per tick for 41 ticks: swings 90° the other way |
| 128 | 11 | Pitch -25 per tick for 41 ticks: tips 90° (a hatch or flap, inferred) |
| 2 | 4 (v0 = 6) | Local x velocity 6.0 for 60 ticks: **slides 360 units** in 2 s |
| 16 | 6 (v0 = 6) | Local x velocity 6.0 for 150 ticks: slides 900 units in 5 s (a gate) |
| 64 | 5 | Vertical velocity -6.0 (up) for 80 ticks: **rises 480 units** in 2.7 s |

Sliding doors (scripts 4, 6) then wait in script 7 for bit 4 or 32 and slide back (v0 = -6):
**bit 4 slides back 360 (script 4), bit 32 slides back 900 (script 6)**, whichever was used to
open; swinging and rising doors stay open (scripts 8, 9, 11 end in an idle loop). While moving,
flags B bit 7 (variable 0x87) is 1. There is no acceleration (constant speed, then stop).
**Watched** in 2:1 (the replay, `VALOR_WATCH` on the five door entities' angles, position and
scripts, vblanks 20495..40736): four doors opened, each a swing of exactly **1025 units** of
yaw in steps of 25 per tick, over 41 ticks (vblanks 29208..29287 and 29210..29289, a pair
opening together, one +1025 and the other -1025; 31243..31324; 31814..31892), the model's
script pointer moving on at the start; none ever swung back. The sliding scripts were not
triggered in the replay.
Scripts 2 and 3 (yaw ∓307 per tick for 20 ticks, then pitch ∓51 per tick for 20: a spin and a
fall) are not reached from the events; nothing found starts them.

**Every door model is the same.** The 19 type-0x1776 models of 1:1, 2:2, 2:3, 3:1, 4:1, 4:2,
5:1, 5:2, 5:4, 7:2, 9:1, 9:2 (RAM dumps at vblank 10800 of warped starts;
`tools/recon/mv_models.py DUMP --type 1776` lists a level's models of a type and their scripts,
spawned or not) have 12 scripts each, **identical word for word** to 2:1's: the speeds above
hold for every door, hatch and gate in the game; they differ only in mesh and in which bits
their level sets.

**Who closes them** (bits 4 and 32): only toggling level scripts on kind-7-tagged records, read
with `lvs.py` in every dump above. 4:1's script 100 answers message 0x5A (sent by the trigger or
Use record sharing the tag): the first time `set_flags(16)` (open: slide 900 in 5 s), the next
`set_flags(32)` (close: back 900). 3:1's and 7:2's script 124 do the same with 0x5C arming 0x5A,
but close with `set_flags(4)`: script 7 then slides back only **360** of the 900 (as read; not
watched in play). Each branch unregisters 0x5A, so as read each such door opens and closes
once. Other `set_flags(4)` in the levels are on soldiers and the player (low-zone flags), not
doors.

**Who opens them**: the level scripts. 2:1's door script 75 (89 is the mirror, `set_flags(1)`):

```
on msg 0:    register_msg(7, self, obj, 5, 2)    # handler 2 (msg 5B) from records sharing my
             register_msg(7, self, obj, 5, 3)    #   kind-7 tag; handler 3 (msg 8B)
on msg 5B:   unregister_msg(0, 2); set_flags(8); send(97); sound(..., 41, ...)
on msg 8B:   register_msg(7, self, obj, 5, 4)
on msg 8A:   unregister_msg(0, 4); set_flags(8); sound(...); send(97)
```

`register_msg`'s filter 5 (`0x8005359C`) passes when the sender's record and the subscriber's
share a tag of the given kind (tags are `{kind:16, value:16}` in the record's `+0x48` list).
Each door record has one kind-7 tag (0x70083..0x70086), and the trigger records sharing it
send 0x5B or 0x8A:

| Record list | Sent when | 2:1 examples |
|---|---|---|
| `+0x38` / `+0x3C` | The player enters / leaves the volume | |
| `+0x40` / `+0x44` | A character enters / leaves it | Record 22 sends 0x5B when a soldier walks in (soldiers open doors by walking into a volume) |
| `+0x4C` | **The player presses Use (action 5) inside the volume** | Records 24, 86, 93, 96: 0x5B, 0x6C, 0x8A |

The Use list: `player_update` (`0x8007AA10`) calls `0x80084670` when action 5 is pressed,
which sends every `+0x4C` message of every record whose volume the player is in (the zone bits
`+0x394`, levels.md). Mounting a mounted gun (`player_hit_part`) sends them too.

So a door opens once (the handler unregisters itself), at its script's fixed speed, when the
player presses Use in front of it or when a soldier walks into its volume, as each level
wires it.

**Blocking**: a door has no touch handler; characters' touch handlers push them out of solid
shapes (`0x8005291C` for soldiers, `0x8007C030` for the player, both through `0x8004B6DC`: the
character's sphere against the object's planes, turned into the object's frame; result 1 means
standing on it, 2 against a side). The shape turns and moves with the door, and nothing stops
a door: it pushes the player along (inferred: no code makes a door wait).

**`sphere_vs_object_planes`** (`0x8004B6DC(e, object, normal out, force)`), exactly
(`tools/sim/engine_planes.py`): nothing while e is riding (`+0x18` bit 1). Into the object's
frame (`0x8004AEA0`, `0x8004ADC8`): e's sphere centre `+0x2E0`, its feet (centre x, z at its
y) and head (y - `+0x2D6`), and its velocity: `+0x27C..+0x280` plus the integer parts of `+0x218`,
`+0x21C` and `+0x200 + +0x220`, turned by its matrix (the player's (0, view yaw, roll) scaled by
`+0x270`, others `+0x340`; GTE >> 12), or, all zero, this tick's move (`+0x20C - +0x258`). Per
plane i (normal n, distance dist): d1 = (n·centre >> 15) - dist; d2 the same for the feet
(planes with ny < 0) or the head; bit i is *behind* if d1 < 0, *near* if behind, d1 ≤ the
sphere radius `+0x2E8`, or d1 ≤ the velocity's max + (mid + min)/4, and *within* if d2 ≤ the
radius `+0x3C8`. Then per plane, two tests whose masks are (behind & `+0xC`) == `+0x10` and
(near & `+0x14`) == `+0x18`:
- **On top** (plane flags 1, e not moving up, near): y = the object's y + ((dist << 15) - nx·cx -
  nz·cz) / ny (local centre; C division) << 16; vy 0; e rides the object (`+0x18` | 5, `+0x2C`
  = its handle; the object's `+0x18` | 8); on the ground; result | 1 if it was not.
- **Against a side** (flags 2; e moving, or forced; near; d1 ≥ 0; also (within & `+0x14`) ==
  (near & `+0x14`); overlap = `+0x3C8` - d2 > 0, or forced; the local centre y not below the
  box's bottom `+0x2F2`): pushed out by (n·overlap·2 + 0x8000) >> 16 on x and z through the
  object's matrix (unless forced); then, if moving into it (velocity · n < 0) or forced: `+0x38`
  | 0x8000 (flags 8: | 0x40 and, if `+0x48` ≤ 0 and e has flags A & 5, the object's damage
  `+0xF8`); an up-facing side (ny > 0) under e not moving up stops its fall (pushed up by
  (ny·overlap·2 + 0x8000) & ~0xFFFF if in the air; the object's `+0x18` | 0x10); speeds
  `+0x200`, `+0x218`, `+0x220` 0; result | 2; the world normal (object matrix · n) out.

**`enemy_touch`** (`0x8005291C`): result 1: forward speed 0, `+0x38` | 2; result 2: `+0x3C` | 2
(scripts read it as "bumped into something"). With brain `+4` bit 4 the soldier turns along the
side: yaw = ratan2(normal x, z) ∓ 0x400, the sign by (fz·dx − fx·dz) >> 12 (its forward column
against the object's offset, GTE OP) and brain `+8` bits 0x10 / 8; brain `+8` | 0x20. The
player's touch (`0x8007C030`) rebuilds its matrix after any result. `bullet_touch` without a
sweep (grenades) uses it too: a hit takes the side's normal into `+0x364`, and a grenade
(bit 0x100) explodes at once (6, 6, 75), its explosion marked flags B 0x100000 on objects with
flags A 0x10000000.

**`door_spawn`** (`0x8005E648`, type 0x1776), exactly: `entity_create(3, no object, the
model, instance flags 0x157, the model's parts, 10 script channels, 6 planes)`; mask 0x88214051,
flags A 0x03008400, B 0x44030, type from the model; the record's `+0x5E` as both cull counters;
group `0x80099ECB` (+1); think `scripted_think` (`0x8005E61C`: `script_run_b`, then
`script_run_a`), draw `entity_draw_model`, destructor `0x8006A37C`; no hit-part or contact
handler; health from the record's `+0x5C`; the plane box from the model's `+0x44`/`+0x7C`
(`0x8004A8F8`); then the part count set to 0. Its scripts open and close it; the record's
placement and message 0 follow (`record_spawn_model`).

## Destructibles and scenery (0x1770, 0x1777, 0x1778, 0x1779)

`0x8005D678` makes both 0x1770 and 0x1778; `0x8005E7B0` makes 0x1777, 0x1779, 0x177A.

- **0x1770 destructible**: solid (flags A 0x13028400, mask 0x88214051, flags B 0x44011), shape
  from the model's box, health = record `+0x5C` (0..255), placed on the floor if within 55
  units of it. Touch handler `destructible_touch` (`0x8005D32C`) takes damage from projectiles
  (their `+0xF8`), explosions (type 0x31: their part's damage, 4.0 per tick) and the player's
  melee parts (kind 5, if within the part's reach). **Every hit** runs the object's level
  script message 1 at once (not only the fatal one); at 0 health it is flagged dead (flags A
  0x20000000), counted once in `0x80039D10` (objects destroyed, the career stat), and a record
  with `+0x68` set drops something (`0x800838AC`, a pickup). Non-explosion hits play an impact
  sound chosen by the model (wood 0x23, metal 0x25, glass 0x27: inferred from the sound ids).
  1:3's script 84 is typical: `world_anim_step(self.prop29)`, `explosion(3)` (debris
  particles, no damage), a sound, `remove_entity(self)`.
- **0x1777** (glass and bottles: catalogue.md): the same touch handler (2:1 dump: touch = 0x8005D32C); 2:1's ones have 1 health
  (one bullet: `VALOR_TRACE_DAMAGE`), another level's had 20 (five explosion ticks). A bullet
  hitting one damages it (the object's own touch handler) but **flies on**: `bullet_touch`
  stops only on types other than 0x1777, and grenades ignore it too.
- **0x1778 scenery**: the same model kind, flags A 0x13028000, mask 0, no touch: nothing hits
  it; it is drawn and, through its planes, still solid for characters (inferred: `0x8005D238`
  builds its shape).
- **0x1779** is a box, **0x177A** solid like 0x1770 (levels.md).

Per-level counts of model types (a scan of every BSD for model entries with a type at `+0xFC`):
0x1770 7..18 per level, 0x1778 0..6, 0x1777 one per level (a shared model, loaded everywhere;
but six single-player levels, 2:3, 3:4, 4:1, 4:3, 9:1 and 9:4, have no record that spawns one:
catalogue.md), 0x1776 doors in 1:1, 2:1..2:3, 3:1, 4:1, 4:2, 5:1, 5:2, 5:4, 7:2, 9:1, 9:2.

### Plane boxes, exactly (`tools/sim/engine_planes.py`)

- **The box** (`0x8004A8F8(e, min, max, -1)`; destructibles pass the model's `+0x44`/`+0x7C`,
  the generic spawner the record's half sizes): `+0x2EE` = 6 plane records of 0x1C bytes at
  `+0x300` copied from the template at `0x8003A238` (top (0, -32767, 0), bottom, -x, +x, -z, +z;
  +0 flags, +4 the normal, +0xA the distance, +0xC/+0x14 the masks of the planes a hit point
  must be inside: every other plane but the opposite one), each distance = |that side's
  coordinate| - 1; the sphere: radius (largest side + 30) / 2 at the box's centre (`+0x2D8`,
  local; `0x8005D238` alone gives scenery and 0x1779 just this); the box at `+0x2F0`/`+0x2F8`.
- **A segment against it** (`0x8004B0F0`): both ends into the object's frame
  (`RotMatrixYXZ(-angles)` · (p - position), GTE >> 12, low halves), per plane d0 = (n·a >> 15) - d
  and d1 likewise; among planes with flags & 3 crossed from the front (d0 ≥ 0, signs differ),
  the one with the smallest |d0| (not t) whose point a + ((t·(b-a)) >> 12), t = |d0| << 12 /
  (|d0| + |d1|), is inside the planes its masks name (n·p >> 15 - d < 0 for each); the point back
  through the object's matrix and translation, the normal rotated; the plane's flags.
- **Projectiles** (`bullet_touch` with flags B 4, `0x8004B624`: the path `+0x258` → `+0x20C`,
  integers; skipped while attached): on a hit (not type 0x1777) the projectile is moved to the
  point, `+0x364` = the reversed normal, explosive rounds explode, a target with flags B 0x4000
  gives the impact sound and sparks (two draws: count, smoke), a soldier the ricochet sound 12,
  it is removed and the target's own touch runs; on 0x1777 only the target's touch runs and the
  bullet flies on.
- **`destructible_touch`** (`0x8005D32C`, the object touched by another): a projectile from a
  flags-A-4 owner (enemies' rounds) only makes the material sound; the player's melee part
  (its first hit part at kind 5: the player facing the object, i.e. the player's forward column
  (`+0x344`, `+0x350`) · (player − object) on x, z < 0, and (the part's radius `+0x34` + the
  object's `+0x2E8`)² above dx² + dz²) does the part's damage (`+0x10`), a projectile its own
  (`+0xF8`), and so does an
  explosion (type 0x31, no sound); then message 1 to the object's level script at once, and at
  its death (once, flags A 0x100000) the counter `0x80039D10` and a drop (`0x800838AC`) when the
  record's `+0x68` is set; the sound by the model: wood 0x23 (0x69960D1C, 0x4D682091,
  0x5C8D7E20), metal 0x25 (0xD2E7F597, 0xE68BC914, 0xEBF2A379, 0xA1EBF698, 0x4A4B9B8A), glass 0x27
  (0x5151A6DA), volume 0x20000, spread 0x4000, class 1.
- **The generic spawner** (`0x8005E7B0`, types other than the ones with their own): no
  instance, type from the model, group `0x80099ECC`, health from the record's `+0x5C`, position
  and angles (`+0x264`), cull counters 5, draw handler `0x8005E760`; 0x1779: flags A
  0x13028000, B 0x40213, mask 0, a sphere only; others: flags A 0x13028400, B 0x44011, mask
  0x88214051, `destructible_touch`, the box.

## World animations (built-ins 18, 21)

The world's TSP chunks carry animated pieces (per loaded chunk, `0x80099098` + 0x288 × chunk:
up to 32 entries of 0x14 bytes at the start, count `+0x280`, enabled `+0x284`; entry `+0x0` a
definition {`+0x8` mode, `+0xC` group id, `+0x10` frame count}, `+0xC` the frame, `+0x10` the
step). They change only when a level script says so:

- `world_anim_step(group)` (`0x80054740`): every piece of that group, in every loaded chunk
  except the one being streamed, steps by its mode: **0** once (frame += step, stops on the
  last), **1** "random" (frame = (frame + 23) mod count), **2** loop (+1, wraps), **3**
  ping-pong (+step, reverses at the ends).
- `world_anim_set(group, frame)` (`0x80054990`).

Both record the group's frame at `0x80099AB8[group]` (groups below 99) and apply it
(`0x80054498`); the record is what a chunk streamed in later starts from (inferred). Used for
what changes in the level's own geometry: a destructible's script steps its group when it
breaks (the broken window or wall texture).

## The mounted gun (0x1772)

`0x8005E2AC`: solid (flags A 0x03038000, mask 0x411), a brain (sight and hearing 3000), health
from record `+0x5C`, collision radius 100, home angles from the record (`+0x18..+0x1C`).
`mounted_gun_think` (`0x8005DA24`) runs its scripts, then:

- **Manned by the player** (variable 9 set; the player mounts it by pressing Use while touching
  and facing it, combat.md): fire held → variable 5. Yaw: the turn input is added
  ((3 × angular velocity + input) / 2 per tick, or the angular velocity / 16 when the input is
  small), clamped to **±0x294 (±58°)** around the record's yaw. The view's height (the
  soldier's weapon block `+0x28`) follows the vertical input (at most 3.0 per tick) between
  60 below and 110 above the gun's height.
- **Firing** (variable 5): one bullet every **3 ticks** (10 per second), `bullet_spawn` type
  0x13F4 with **damage 5.0**, the angles jittered by pitch -33..+32 and yaw -20..+19 units
  (±2.9°, ±1.8°). The player's: noise 255 (`0x8004D8DC`), a camera shake (5, 3), a shell
  casing 49%, `g_stat_shots` += 1. Unmanned (a soldier's script setting variable 5): aimed at
  the player's soldier's middle (y - eye height / 2), with the same jitter.

## Other types

- **0x1405: the dog** (inferred: low body, sphere centre 50 up, collision radius 80, health
  from the soldier record (4 in 1:3), no weapon; it hurts by contact of its attack parts, 2.0
  per hit (combat.md); only 1:3 has the model). `0x80054E4C` makes it from a kind-3 (soldier)
  record like `enemy_spawn` (counts toward the soldier cap, brain, perception ranges from the
  record); its think (`0x80054AB8`) runs the scripts and the hit-part reaction, no
  `enemy_weapon`. The player's touch handler pushes soldiers and dogs apart (overlap of the
  `+0x2EC` radii, at most 65 per tick, `0x8007C030`). A grenade near it always makes it go for
  the grenade (Grenades).
- **0x1771** (`0x80046B2C`, 1:1 only; `entity_create(3, no object, the model, instance flags
  0x15)`, flags A 0x03008004, B 0x211, the record's cull counter, health and group
  `0x800939D8`, variable 1 = the record's `+0x34`, destructor `0x8006A37C`; its think
  `0x80046A88` runs only the channels, not the event matcher): no collision (mask 0, radius 0), runs its scripts; each
  tick its angular velocity is variables 3, 4, 5, and it removes itself when variable 6 drops
  below variable 2 (a scripted spinning prop with a lifetime). Watched in a 1:1 run: spawned
  after the level start, it travelled in a straight line (about 10 units x, 17 z per tick) until it
  removed itself in its think; its record script (6) got no message 2,
  so it was not culled although its flags A have 0x8000.
- **0x1774** (`0x800905C0`; the model exists only in **9:4**, the V2: catalogue.md; the
  model lists of the 2:3 and 4:2 dumps have none): solid, scripted, with two particle systems of
  40 (smoke and fire). Variable 2 set: each tick it climbs 300 units (vertical velocity -300),
  turns 200 units of yaw and wobbles in pitch and roll (a rocket lifting off; **watched** with
  variable 2 poked to 1 in a 9:4 warped start, the entity at `0x800F3DA0`: from the next tick it
  rose **298.5625** units a tick (not quite 300), yaw +200 a tick, pitch and roll ±64 units in a
  16-tick circle (variable 6 counting 1..16), and it kept rising for the rest of the run, 33,000
  vblanks, never removing itself: what ends it in play is the level's script);
  variables 3 and 5 trigger smoke and fire bursts, the latter with a camera shake (125, 30) and
  noise 250. Its floor test sets `+0x38` bit 2 when it is within 550 above the floor.
- **0x1775: switches, valves, levers** (`0x8005E510`; 3:4, 4:3, 5:1, 5:2, 5:3, 9:1:
  catalogue.md): scripted, a collision box nobody collides with (levels.md). The player
  reaches it through a kind-2 record's Use list: Use sends message 0x2E, and the level script
  answers once with `set_flags(2)` and a report message (5:1: 0x72, and 0x5B). Its model
  (5:1's 0x579B641F, `mv_models.py`) animates on the `+0x40` bits: **2** turns it, pitch +20
  per tick for 100 ticks (2000 units, half a turn: a valve wheel or lever), the next 2 turns it
  back (-20); bit 1 starts it in the turned state, bit 4 in the unturned one. **Watched** with the
  bit poked rather than sent by the level (5:1 warped start, the switch at `0x800E3640`,
  `--poke-on-call 80044470:800E3680=2`, the poke landing at vblank 8566): pitch +20 per tick
  for exactly 100 ticks (0 → 2000), its script moving on at the first and the last tick. The
  Use → message 0x2E → `set_flags(2)` path was not exercised (the player never reached a
  switch).
- **Type 0x31 without parts**: particle bursts (`0x80073BA8`, think `0x80073B08`) share the
  explosion's type but have no collision (mask 0, no parts), so `player_hit_part`'s type test
  never meets them.

## Grenades

Thrown by `throw_weapon` (`0x8007CF4C`, the player, weapons 3 and 10) or the soldier's hit hook
(`0x800523E4`, soldier.md), made by `0x80045808` through `projectile_create`: type 0x13F3,
damage 10.0 (`+0xF8`, a direct hit's), life 150 ticks (`obj+0x3A`), collision sphere radius 76
(`+0x2E8`) and 40 (`+0x2EC`), flags A |= 0x040000A0 (gravity, bounce on landing), flags B |=
0x40 (swept against the world), touch `bullet_touch`, hit part `0x80044014`, world contact
`0x80043284`, think `0x80044470` (the projectile think). Its weapon block (`obj = +0x388`):

| Field | Value | |
|---|---|---|
| obj+0x34 | 3.0 | Extra gravity per tick (10.0 after the first bounce) |
| obj+0x38 | 0x4349 | Behaviour bits: 0x1 extra gravity, 0x2 bounced this tick, 0x8 (cleared on landing), 0x40 fused, 0x100 explodes, 0x200 bounces, 0x400 rolling, 0x4000 warn soldiers once |
| obj+0x3A | 150 | Life in ticks (removed at 0) |
| obj+0x41 | 90 | **Fuse: 90 ticks, 3.0 s** |
| obj+0x42 | 120 | |
| obj+0x68..+0x7C | | Model spin: angles and speeds (yaw 200..849, pitch 10..39 per tick, random); drawing only |
| obj+0x80 | | Last tick's speed |

### The throw (player)

- **Charge**: while fire (action 0) is held, `inv+0x11C` counts ticks held, 0 on the first,
  up to 30 (`player_update` `0x8007AB60`). Releasing it sets the soldier's `+0x3C` bit 2, and the
  throw starts (`player_weapon_update` → `throw_weapon(soldier, ..., fire, charge)`; the watched
  wind-up began on the release tick; that the soldier's scripts turn the bit into the fire
  variable is inferred).
- **Wind-up**: the throw animation (script 0xC for a charge under 12, an underhand lob; 0xE
  otherwise) and `inv+0x120` ticks before the grenade leaves the hand: 8 for grenades; stick
  grenades 18 (charge < 12) or 15. Exactly: the starting tick sets `inv+0x124` = 1 and goes on
  to the count; each call adds 1 to `inv+0x124` and throws when the count **before** the add
  was at least `inv+0x120` (1:3: started at 5884, thrown at 5891), then resets it to 0. The refire delay is the weapon's (-60: 2 s,
  `inv+0x44`/`+0x48`).
- **Launch speed: 55 + 2 × charge units per tick** (55..115), along the throwing arm's line
  (`projectile_aim` from the model's bones `+0xD4`/`+0xD6`, as for bullets: where the soldier
  aims): the speed is split by that pitch into forward speed (`+0x200`, a speed along the
  grenade's yaw) and vertical velocity (`+0x21C`), and the grenade's own pitch is set to 0.
  Without the cheat 0x40 a grenade is taken from the slot.
- **No throwing through walls**: the segment from the grenade back toward the thrower (as long
  as their horizontal distance, max + min/4, along the grenade's forward column) is swept against
  the world; a hit moves the grenade to it, 20 units back toward the thrower (the column × -5
  >> 10). If that segment crosses an entity's sphere, the grenade starts at the far end
  (behind the hand) instead. Below the thrower's floor, it is lifted to 32 above it with at most
  55 downward speed.

### Flight, bounces, rolling

Each tick (the think, then `entity_move`, then the world contact handler if anything was hit):

1. Life -1; fuse -1: at 0, **explode** (below) and remove.
2. Extra gravity: vertical velocity += `obj+0x34` (3.0) unless it bounced this tick; then
   `entity_move` adds the normal 2.0 (entities.md), at most 75 down: **5.0 per tick² in
   flight**, 12.0 after the first bounce.
3. Rolling (bit 0x400) on the ground: speed -= 3.0 per tick, 1.0 once at 25 or less; at 0
   it stops (all velocities 0, rolling cleared). A bounce sound (7 or 8) on bounce ticks.
4. Moving: forward speed along the yaw, the vertical velocity; swept against the world from
   the old position to the new (walkers' sliding does not apply). A wall: the horizontal
   velocities go to 0 and, the grenade having flag A 0x80, its x and z go back to the old position
   plus the wall's normal × 32 / 65536 rounded to whole units (16 units for an axis normal), y
   kept. The floor (`entity_move`): landing with 8 or more
   units of fall this tick reflects the vertical velocity (vy = -|vy|) and reports a ground
   contact; less, it lands (on ground, vy = 0).
5. World contact (`0x80043284`), when something was hit:
   - Not yet rolling: **speed × 2/3**. On a ground contact, if the next tick would reach the
     floor (floor < y + vy + gravity), it **starts rolling**: on the ground, vy 0, pitch 0, at
     least 30.0 forward speed, flags A 0x28 instead of 0x80. Otherwise the bounce keeps
     **a quarter of the vertical velocity** and the extra gravity becomes 10.0.
   - Rolling: speed halved on each contact. The **first** time (bit 0x4000), every living
     soldier and dog within 1200 (the horizontal distance `0x80050658`: |dx| + |dz| -
     min/2) gets `+0x3C` 0x40 (**flee it**); the nearest gets 0x80 (**come and return it**) if
     it is a dog or within 1000, else 0x20 (**smother it**) if three or more were within
     1200 (soldier.md, Grenades); and the grenade's hit part becomes kind 7 (never hit)
     (`0x8004C64C`), so soldiers can walk up to it.
   - A wall while not rolling: speed × 2/3 again. Every contact: the direction is reflected
     about the contact normal (`0x800431EC` turns the reflected vector into pitch and yaw) and
     the spin halved. Exactly: d = (forward column x, 0, forward column z), k = -((d · n) >> 17)
     (n the contact normal `+0x364`, 32767 long), d += (k · n) >> 12 (GTE GPF); yaw =
     ratan2(dx, dz) << 16, pitch = -ratan2(0, ·) = 0, roll 0; each spin speed is negated when
     its angle and the new one have the same sign, and with no spin left the spin angles take
     the new ones. "Speed × 2/3" is `speed - speed / 3` (C division); rolling, `speed -
     (speed >> 1)`. A floor bounce's contact normal is the floor face's (`floor_find`'s out),
     unless a wall was hit in the same move.
   - The bounce sound every bounce while not rolling: sound 7/8 of bank 1 (grenades), else
     `rand · 7 >> 15` of bank 0.

**Measured** (replay, `VALOR_WATCH` on the player's soldier and on one of his grenades, 1:3
vblanks 34151..34207, every value matching the rules above):

| Vblank | Event | Forward speed | Vertical | |
|---|---|---|---|---|
| 34165..34171 | Fire held 2 ticks, released: charge 2 | | | `inv+0x11C` 1, 2 |
| 34171..34185 | Wind-up `inv+0x124` 2..8 | | | |
| (launch) | 55 + 2·2 = 59 at ~12° below level | 57 | 12 | Integers: the GTE result is truncated |
| 34185 | First tick: hit a wall facing +x (normal (32767, 0, 0)) just ahead; yaw reflected (the soldier faced ~3278, the grenade now 794) | 57 × 2/3 × 2/3 = 25.33 | (12 + 3 + 2) / 4 = 4.25 | |
| 34187..34191 | Flight | 25.33 | 6.25, 18.25, 30.25 | +2 (bounce tick), then +12 |
| 34193 | Floor bounce | 16.89 (× 2/3) | -(42.25 / 4) = -10.56 | |
| 34195 | | 16.89 | -8.56 | +2 only (bounced last tick) |
| 34197 | Lands: rolls | 30 halved = 15.0 | 0 | |
| 34199..34207 | Rolling | 14, 13, 12, 11, 10 | 0 | -1 per tick |

The fuse byte counted down by one per tick from 89.

**A throw into open space** (1:1 at the start, facing yaw 1251 with 2200 units clear; grenades
drawn, fire held 10 ticks; `VALOR_WATCH` on `[[8009D654]+4]`, the first projectile after the
soldier in the list, and its object):

| Tick | Event | Forward | Vertical | y (floor 484) |
|---|---|---|---|---|
| release | charge 9 (`inv+0x11C`), lob script 0xC, wind-up 8 | | | |
| +7 | Launched: 55 + 2·9 = **73**, pitch 0 → vy 0 | 73 | 0 | 309 (175 above the floor) |
| +8..+13 | Flight, +5 per tick (3 extra + 2) | 73 | 5, 10, 15, 20, 25, 30 | 314, 324, 339, 359, 384, 414 |
| +14 | Floor: snapped to 484, bounce | 48.67 (× 2/3) | -(30 + 3 + 2)/4 = **-8.75** | 484 |
| +15 | Next tick would reach the floor: **rolls** | 48.67 / 2 = 24.33 | 0 | |
| +16.. | Rolling, -1 per tick (≤ 25) | 23.33, 22.33, ... | 0 | |

Horizontal travel 68.56, -24.94 per tick = 73 along yaw 1251 (110°). The fuse counted 90 → 0
in 90 ticks, life 150 → 60. A second throw after looking up 60.07 units (`inv+0x178`):
forward 72, vertical **-7**: `vy = -(sin(pitch) · speed)` truncated (−6.69 → −7), forward
`cos · speed` truncated (72.69 → 72).

**Its direction is the view's**: `projectile_aim` gives a grenade (type 0x13F3) and the player's
rocket the thrower's body angles (`+0x228` pitch, `+0x22C` yaw, `0x80044EF0`), and in the
grenade stances (6, 9) those are the view's angles: measured, the soldier's `+0x228` equalled
`inv+0x178` (60.07) and the grenade flew along the view yaw. The start is the hand point
(`gun_points` on the arms model's bones); the GTE rotation truncates toward minus infinity.
The replay's throw (~12° down) is the same rule with the view pitched down.

### What soldiers do with them

`0x80044120` (from the projectile think, for type 0x13F3) reads the bits soldiers' scripts set
on the grenade's `+0x3C` (op 0x3F, soldier.md):

| Bit | | Effect on the grenade |
|---|---|---|
| 1 | Kicked (anim 217) | If a soldier is within 80 (`0x8005788C`): the grenade takes its angles, speed 25.0, vertical -33.0 (up), fuse and life +8 ticks |
| 2 | Picked up (anim 218) | Attached to the soldier's bone 17; life 120, **fuse reset to 90**, spin stopped. While attached, `entities_update` puts it on the bone each tick (`model_bone_point`, after the think) and it does not move itself; the think zeroes its velocities |
| 4 | Thrown back (anim 220) | Released along the soldier's facing: d = distance to the player - 700 (at least 0); speed min(d/64 + 30, or + 35 beyond 1300, 65); vertical -25 (-30 beyond 2000); life 120, fuse 90 |
| 8 | Dropped (the soldier was hit) | Detached; falls |

Soldiers' own throws (`0x800523E4`, stick grenade model 0x61282777, fuse 90): aimed at the
player's middle with speed min(d/16 + 30, 100) (+5 beyond 1300) and vertical -30 (-35
beyond 2000), d = distance - 700; from cover, toward the player at min(d/16 + 50, 150); a
dropped grenade (hit while holding it) starts with vertical -1.

### A direct hit

The grenade explodes at once, with a smaller blast (`explosion_spawn(grenade, 6, 6, 75, ...)`,
radius 375) than the fuse's, when (not the thrower's group, not another projectile, not a
0x1777):
- its hit part meets a character's part (`0x80044014`, the hit-part handler, bit 0x100), or
- its sphere meets a solid object's plane shape (`bullet_touch` → `0x8004B6DC`): objects with
  flags A 0x400 in its mask 0x08000451, i.e. destructibles and doors (inferred from the masks;
  not watched).

After its first roll its part is kind 7 and no longer meets characters' parts (above).

## Explosions (`explosion_spawn`, `0x800719C0`)

`explosion_spawn(source, fireballs, puffs, spread, scale, var0, damage, reach, record)`: an
entity of type 0x31 (combat.md), at the source's position (or the record's). It has **one**
hit part, kind 5 (an attack), whose damage is `damage` and radius `spread × reach`; the
entity's sphere has the same radius. The fireballs and smoke puffs are particles in its object
(0x3C bytes each, after a 0x64-byte header) and do not collide.

**The damage**: the ordinary contact pass (entities.md) calls the victim's `hit_part` with the
explosion's part, so every entity whose parts' spheres reach the explosion's sphere takes
`damage` (× its part's kind scale, combat.md) **once per tick** for as long as the part
exists. There is **no falloff** inside the radius and **no line-of-sight test** (walls do not
shield). The part exists while the fireballs grow and hold: **9 ticks** (the tick it is made
and 8 thinks), then the radius and part count go to 0. The player is special
(`player_hit_part`): an explosion whose fireballs are still alive and whose centre is within
**300 units horizontally** (|dx| + |dz| - min/2, height ignored) does **1000**: death (unless
god mode).

Measured with `VALOR_TRACE_DAMAGE` (1:3 replay): the player standing near his own grenades
took 4.0 eight ticks in a row (vblanks 13660..13674, 25418..25432); a grenade within 300 did
1000 (31392); destructibles took 4.0 nine ticks running (63796..63812, another run).

**The callers**:

| Source | Fireballs | Puffs | Spread | Damage | Reach | Radius |
|---|---|---|---|---|---|---|
| Grenade fuse (`0x80044470`) | 8 | 10 | 85 | 4.0 | 5 | 425 |
| Rocket hitting the world (`0x80043284`; projectiles with bit 0x100 and no fuse) | 10 | 12 | 130 | 4.0 | 5 | 650 |
| Explosive round or grenade hitting an entity (`bullet_touch`, `0x80043E38`, `0x80044014`) | 6 | 6 | 75 | 4.0 | 5 | 375 |
| Level `explosion(0)`, `explosion(5)` | 4 | 4 | 60 | 4.0 | 7 | 420 |
| Level `explosion(4)` | 18 | 30 | 800 | 20.0 | 3 | 2400 |
| Level `explosion(7)`, `explosion(8)` | 6 | 10 | 15 | 2.0 | 5 | 75 |

Level `explosion(1)`, `(6)` are sparks and a sound, `(2)` a smoke emitter (`0x80074A60`),
`(3)` debris particles: no damage. Scale is 1.0 for all but `explosion(4)` (4.0) and 7/8
(0.1875); var0 0x1000 (0x5000, 0x200): the first tick's particle burst (`0x80070DEC`).

The smoke emitter of `explosion(2)` (`0x80074A60` → `0x80074898`): `entity_create(0, object
0xB64, no model, 2 hit parts)`, type 0x1644, flags B | 0x200, placed at the script's entity (or
record) and linked at the front; its think (`0x80074A38`) keeps its emitter endless (`+0x504` =
0x7FFF) and updates the particles (`0x8006F7AC`, `0x8006EAC0`: drawing); it is never removed by
itself. `explosion(4)` also makes its explosion's collide mask 0x88004450 and starts a **screen
flash** (`0x8004FE84(1, 1, 2, 2, (192, 192, 192) → (128, 128, 128))`): an entity of type 0
(object 0x64; flags A 0x20000, B 0x203; linked at the front) whose think (`0x8004FC14`) waits
the delay, steps its colour by (end − start) / length per tick for the length (mode 1), holds,
and removes itself: 1 + 2 + 2 + 1 ticks.

So a grenade's 9 ticks × 4.0 = **36 damage** to anyone within 425 (more than 4.0 per tick if
two explosions overlap), and a soldier (6 health in 1:3) dies on the second tick.

**The fireballs** (the open item in combat.md; read from `0x800712A0` and checked tick by tick
on an explosion's object with `VALOR_WATCH`, 1:3 vblank 31390..31430): each starts at a random
point of the cube ±spread around the centre, size 5.0, a random spin (-25..24 per tick) and
angle. Per tick: **size + 20.0 until 64.0** (3 ticks: 25, 45, 64), **hold 5 ticks**, then
**size - 3.0 or 4.0** per tick (random per fireball) to 0 (16..21 ticks); they drift upward
(vertical velocity -1/128 per tick², and -0.5 more per tick once shrinking). All start
together (the delay is 0: `rand() >> 15` is always 0). When the last fireball starts
shrinking, the damage stops. The **smoke puffs**: same placement, start after 8..27 ticks,
grow by 5.0 to 20..29, hold 45 ticks, shrink by 5.0, rise faster (-1/8 per tick²). The
explosion entity is removed when both systems are done (about 2.5 s).

**Drawing them** (`0x80070F24`): each fireball, then each puff, not finished (state 6) and past
its delay, through the billboard function (hud.md, Billboards): a camera-facing square from the
record's `+0xC`/`+0xE` (fireballs (35..134) × 2, puffs 120), centred, scaled by the system's `+0xC
>> 4` (4096 = 1: 1.0 for the fireballs, 2.0 for the puffs), turned by `+0xA` (spinning by `+8` a
tick), so a fireball is a square of half 70..268 units and a puff 240; textured from the header
(fireballs 63 × 63 at VRAM (992, 32), CLUT (992, 255); puffs 31 × 31 at (1000, 0)), additive; its
**colour is its size's whole part** (`+0x22`): the fireballs brighten from 5 to 64 (half the
texel) as they grow and, shrinking, go red (G 10 and B 15 less, none below 0); the puffs 20..29.
The "size" grows no quad: the quads are fixed, the size is their brightness. Drawn natively by
`engine/godot/project/present/effects.gd`; a recomp frame of a 1:3 grenade
(`build/gdp/cmp_explosion.png`) also shows the explosion light tinting the walls, which the native
world doesn't draw yet.

The soldier "within 80" is `0x8005788C(grenade, 0x50, -1)`: the nearest living character by
`dist_approx` (max + (mid + min)/4, 16-bit) less than 1000 apart in y.

**Its first think** (the tick after it is made): the burst of particles (`0x80070DEC`, var0:
an entity of type 0x23 with 5 pairs of sparks, three draws each, removed after 3 thinks),
a camera shake of (3000 - d) / 16 for 10 ticks and a noise of that strength (`0x8004D8DC`)
if the player is within 3000 (d: max + (the two others)/4).

**The light**: the latest explosion (`0x8009D650`) drives one coloured point light
(`0x800AA188`: r, g, b, radius, radius²): (80, 40, 10) and radius 150 at once, then for 8
thinks +(8, 3, 1) and +175 (to (144, 64, 18), 1550), then -(4, 3, 1) and -75 per tick until the
explosion ends (watched: 150, 325, 500, ... 1550, then 1475, 1400 ...).

**How it lights the world** (the world's face drawer `0x80010B60` and its vertex step
`func_800114B0`; the subdivided faces' `func_80012FF8` / `func_800135F8` do the same): the
light's centre is the source entity's integer position (`+0x20E..`, `g_last_explosion`: an
explosion, or a rocket in flight, `rocket_spawn`: (180, 160, 16), radius 500); faces whose
bounding box overlaps the cube centre ± radius (`func_80011AB0`) get, at each vertex within the
radius (d² = the squared distance in whole units, `sqr`), its colour + light × (1 − d²/r²)
(`0x10000 − (d² << 8) / (r² >> 8)`, then `× light >> 16`), each component clamped at 255, for that
drawn frame only. **Scorches**: while `0x800AA160` is set (by `explosion_spawn`; `draw_level`
clears it after drawing the chunks, every drawn frame, and the 9:4 object's think `0x8008FDF8`
clears it too, so a scorch is applied in the one frame drawn after the spawn), the same faces' colours are darkened in the colour table itself, for
good: colour − colour × ((1 − d²/r²) / 8), at least 1, with the spawn's radius 150: a lasting dark
patch round every explosion. (Models aren't lit by it: only the TSP faces' drawer reads it.) Drawn
natively by the world shader (`engine/godot/project/world.gdshaderinc`) from
`present/effects.gd`; a recomp frame of a 1:3 grenade and the native one side by side:
`build/gdp/cmp_explosion_light.png`.

## Replays

A headless replay (`--hidden` without a display: "SDL init failed ... running headless")
diverges from the recorded session after about vblank 3100 (an older log's damage events
differ from there; the player dies in 1:3 at 31392 and the replay stays in 1:3). Two headless
runs agree with each other exactly (the same damage log up to vblank 34207), so watches and
dumps from headless runs are repeatable, but they are not the recorded play past that point.

## Open

- 0x1771 (1:1's prop, model 0xC2E34354): what sets its velocity (its scripts, not read); 0x1774 watched only with its variable poked (above);
  whether 0x1405 is the dog (no dump shows its model).
- The toggling door scripts (open with 16, close with 4: 360 of 900) not watched in play: the
  doors of 3:1, 4:1 and 7:2 are spawned by triggers, not at the level's start, so a warped start
  without a route never makes them.
- The colour cycles: the world drawer `0x80089D68` takes `g_level+0xDC` as a table (to the
  scratchpad, `0x1F8003C8`), so world polygons tagged with a cycle are drawn in its current
  colour (`+0xC`, inferred: flickering lamps); which TSP field picks the cycle wasn't read.

## Names

```
8001555C srand                      # (seed) -> 0x800CA2A4; one caller, the shell's start-up
800CA2A4 g_rand_seed
80039C68 g_menu_input_count         # +1 per menu button edge in the shell; the srand seed
8009A5C0 g_vblank_timer             # +1 per vblank; game_frame sends message 0x59 per 20
80044014 grenade_hit_part           # projectile hit part: damage, explode (bit 0x100)
80044120 grenade_soldier_bits       # +0x3C bits from soldiers: kick, pick up, throw back, drop
80044470 projectile_think           # life, fuse, gravity, rolling, explosion at fuse 0
80043284 projectile_world_contact   # bounce, roll, warn soldiers, reflect; rockets explode
80045808 grenade_spawn              # (thrower, model, bone a, bone b, fuse, speed, target, ...)
80050658 dist2d_approx              # (a, b): |dx| + |dz| - min/2
80053AC0 level_colour_cycles        # the world entity's think: g_level+0xDC colour sequences
8005359C record_tags_match          # filter 5 of register_msg
8005D678 destructible_spawn         # types 0x1770, 0x1778
8005E2AC mounted_gun_spawn
8005E61C scripted_think             # type 0x1776: script_run_b, script_run_a
8005E648 door_spawn                 # type 0x1776
8005EDF8 alarm_think
80046A88 prop1771_think
80046B2C prop1771_spawn
80054AB8 dog_think
80054E4C dog_spawn                  # type 0x1405
80058734 sop_fall                   # op 0x04: +0x200, +0x21C, +0x218
80058C78 sop_event                  # ops 0x0D/0x0E/0x42: store an event
80059B8C sop_turn                   # op 0x25: angles += operands
80066318 msg_run_one
800663B8 msg_run_all
80066EA0 msg_send                   # subscriptions -> the queue
800670E8 msg_enqueue
80070DEC explosion_burst
800712A0 particles_update
800715D4 explosion_think
80073BA8 particles_spawn            # type 0x31 particle entity, no collision
80074EE8 spawn_queue_push
80074F9C spawn_queue_run_one
80075034 spawn_queue_run_all
80084670 use_volumes                # Use pressed: send the +0x4C lists of the player's volumes
8006B834 entity_link_front          # after the camera
8006DA88 handles_free_removed
8008FDF8 obj1774_think
800905C0 obj1774_spawn
80090A30 frame_idle_job             # one message + one round-robin job
80090AEC frame_idle_loop
80090B80 frame_wait_vblank          # idle jobs until the vblank; computes g_ticks_dropped
80090D38 vblank_handler
800AA188 g_explosion_light          # r, g, b, radius, radius^2
8009D650 g_last_explosion
800B6460 g_msg_queue                # 128 x 32 bytes
8009E294 g_spawn_queue              # 64 records; count 0x8009E288
```
