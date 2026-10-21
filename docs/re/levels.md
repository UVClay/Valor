# Levels: files, the level header, entity records, spawning

Read from the loader and checked against RAM dumps of 1:3 (`recordings/2026-10-19_22-22-43_m1l3`
replayed to vblank 14000).

## Files (`DATA\MSNm\LVLl\`)

| File | Contents |
|---|---|
| `m_l.BSD` | The level: header, entity records, models (with their scripts), AI nodes, collision |
| `m_l0.TAF`, `m_l1.TAF` | Loaded first (`%ld_%ld%d.taf`, the digit from `0x8009A618`): textures/animations? |
| `TSP0\m_l_cN.TSP` | World geometry chunks, drawn by `tsp_draw` (the path is in the header) |
| `Mm_l.VB`, `Mm_lA.VB` | SPU sample banks |

## Loading (`level_load`, `0x80087308`)

The BSD is read (`0x8004A374`); `g_level` (`0x8009CC64`) points at its level header, file
offset 0x800 (the file starts with a 0x800-byte table of 25 offsets, sizes of its later parts;
some of those parts end up elsewhere in RAM). The loader turns the
header's offsets into pointers (adding the section base), relocates every model
(`model_relocate`, `0x80087080`: its data, and its scripts through `0x8003F788`), runs the
section parsers (`0x80086C9C`, `0x80086D18`, `0x80086D8C`, `0x80086E34`, `0x80086EB0`,
`0x80086F24`), creates the world entity (`g_world`, `0x80053E68`) and gives it the entity table
and the node table, sets the fog and background colours, and builds the spawners.

## The level header (`g_level`)

| Offset | Meaning |
|---|---|
| +0x00 | TSP path prefix, e.g. `data\msn1\lvl3\tsp0\1_3_c` (0x80 bytes) |
| +0x80 | Counts (9, 4, 1, ...) |
| +0x98 | Used by the drawing pass (primitive pool size: `2 × [+0x98]`) |
| +0xCC | Fog on (s16); +0xCE..+0xD0 fog colour (far colour), +0xD4 back colour switch |
| +0xDC..+0x320 | 0x14-byte entries {count, pointer, n, 0x02RRGGBB, type}: colour/light sets? |
| +0x53C | The entity table (below) |
| +0x540 | The AI node table: count, then entries (the world object's +0x4/+0x8/+0xC) |
| +0x544..+0x584 | {pointer, count} sections: 235, 6715, 52894, 158, 7, 1972, 7, 182, 1123 entries in 1:3 (model data? the world's collision is in the TSP files), also the bases for models' offsets |
| +0x598, +0x59C, +0x5A0 | Sections parsed by `0x80086F98`, (world), `0x8007540C` |

## Entity records (`+0x53C`)

A count, a size and two words, then `count` pairs of {state, record} (1:3: 121 records, at file
offset 0x2E7C). In the file the records are offsets from the first record and every state is
0. The **pair** (its address) is what messages and scripts call the record's *object*; an
entity spawned from a record keeps it in `+0x1C`.

**The state word** (runtime; checked by reading every writer, and against the 1:3 replay dumps
at vblanks 8000..50000, which hold only 0, 1 and 2):

| Bit | Set | Cleared |
|---|---|---|
| 0 (1) | the record has a live entity: `record_activate` (`0x80052D70`, set before the spawn is tried, so it stays set while the spawn waits in the spawn queue), the spawners (`0x80074BB8`, `0x80074C54`, `0x80074CE8`); a ladder (kind 0) when its entity is made (Triggers) | `entity_remove` (`0x8006B8F0`), whatever the cause |
| 1 (2) | its entity **died**: `entity_remove` of an entity with flags A 0x20000000 (dead), for kinds 2, 3 and 6, unless the record's `+0x30` bit 3 (property 17) is set; and the soldier destructor (`enemy_destroy` `0x80056290`, type 0x1402 only, not the dog's `0x80054D78`) sets it for any dead soldier **whatever `+0x30` bit 3 says** (2:1's r25 has `+0x30` = 0xF and went to 2 when its soldier died) | level start (`records_clear_state` `0x80075684` clears both bits) |

So 0 = not spawned (or culled and free to spawn again), 1 = alive, 2 = killed (3 never lasts:
the removal clears bit 0 as it sets bit 1). `record_activate` tests bit 0 only: a killed
record can be activated again; the scripts decide (the model scripts' op 0x34 reads bit 1).
Every state word of the three levels of the recorded session follows these rules frame by frame
(`tools/sim/level_vm.py`, Level logic below).

Every record starts with:

| Offset | Meaning |
|---|---|
| +0x00 | Class hash |
| +0x04 | Record size |
| +0x08 | 4 |
| +0x0C | Kind (0..6) |
| +0x10 | Position: x, y, z (s16); +0x18..+0x1C angles (s16) |
| +0x20..+0x27 | The record script's variables (`ent[0..7]` in lvs.py) |
| +0x28..+0x36 | The volume (Triggers): +0x28 shape, +0x2A..+0x2E sizes; +0x30 flag bits (properties 14..17), +0x32, +0x34 (the player number on the player start), +0x36 |
| +0x38, +0x3C | Lists sent when the player enters / leaves the volume |
| +0x40, +0x44 | Lists sent when a character enters / leaves |
| +0x48 | Tag list (`type << 16 \| id`, sorted) |
| +0x4C | Use list (the player presses Use inside the volume) |
| +0x50 | A sixth offset, relocated like the lists but read by nothing found (unknown) |
| +0x54 | **Record script** id (level scripts; -1: none) |

The offsets at +0x38..+0x50 are made pointers by `0x80075120` (every record of kind ≥ 1,
`entity_records_register` `0x8007529C`), the template at `+0x60` for kinds 2 and 6
(`0x800751B4`, `0x800751D0`) and `+0x74` for kind 3 (`0x800751EC`). Kind 0 records (classes
0x4D078F23 streaming, 0x3011B913 ladder) have their own short layout: no script, no lists.

1:3's classes:

| Hash | Count | Size | |
|---|---|---|---|
| 0x4F292333 | 49 | 0x98 | Soldier spawn (kind 3) |
| 0xB81C10E3 | 33 | 0x6C | Trigger volume (kind 1) |
| 0xBD12927E | 23 | 0x7C | Pickup (kind 4) |
| 0x4D078F23 | 10 | 0x38 | Streaming (kind 0) |
| 0x166A85FB | 3 | 0x84 | Object (kind 2) |
| 0x3011B913, 0xD31F988A, 0x8877B646 | 1 each | | Ladder (kind 0), object (kind 6), the player start (kind 2) |

**The player start** (class 0x8877B646, kind 2): `player_start_record` (`0x800754CC`) finds the
one whose `+0x34` is the player number (0; two in the MSN12 arenas), and `player_spawn`
(`0x80087294`, from `level_load`) makes the player's soldier for it (`0x80074BB8`). Its record
script is the level's **objective script**, its entity script the player's (13).

The soldier record (`enemy_spawn` reads it): +0x5B skill tier, +0x5C, +0x5E, +0x60, +0x61
health, +0x62, +0x63 aim error, +0x64 damage, +0x65, +0x66, +0x68 (150, the cull delay,
messages.md), +0x6A, +0x6C and +0x70 ranges (1500, 3000), +0x6E (enemies.md, Spawning).
Records differ soldier by soldier (1:3 has 6 health, aim error 40, damage 7 next to 4, 20 and
4). Kind 4's `+0x60` is the pickup hash itself (the table at `0x8009E8E8`).

### Record properties

Push forms 02 F4..FF with a property number read a field of a record (`push_property`,
`0x80065F34`: the object's record if the context has an object, else the entity's `+0x1C`
record), by the record's kind through one reader per kind (`0x8006488C`, `0x80064AE4`,
`0x80064EC8`, `0x800654E8`, `0x800658A0`, `0x80065B24` for kinds 1..6; read from the six
decompiled readers). Types: s = signed (0x0A), u = unsigned word (0x0B). A number a reader
doesn't know, or a kind-0 record, pushes nothing: the VM has already raised the stack top, so
the slot keeps a stale value.

| Prop | All kinds 1..6 | Kind 2 | Kind 3 (soldier) | Kind 4 (pickup) | Kind 5 | Kind 6 |
|---|---|---|---|---|---|---|
| 0..3 | s16 +0x28, +0x2A, +0x2C, +0x2E (shape, sizes) | | | | | |
| 4, 5 | s16 +0x32, +0x34 | | | | | |
| 13 | u32 +0x54 (record script) | | | | | |
| 14..17 | bits 0..3 of +0x30 | | | | | |
| 18 | s16 +0x36 | | | | | |
| 19 | | bit 0 of +0x58 | bit 0 of +0x58 | bit 0 of +0x58 | u32 +0x58 (world anim group) | bit 0 of +0x58 |
| 20 | | s16 +0x5E | s16 +0x68 (cull delay) | s16 +0x5E (lifetime) | | s16 +0x5E |
| 21 | | u8 +0x59 | u8 +0x59 | u8 +0x59 | | u8 +0x59 |
| 22 | | | | u32 +0x60 (pickup hash) | | |
| 23 | | u32 +0x64 (entity script) | u32 +0x78 (entity script) | u32 +0x64 | | u32 +0x64 |
| 24..26 | | u8 +0x5A, +0x5B, +0x5C (health) | u8 +0x5F, +0x60, +0x61 (health) | u8 +0x5A, +0x5B, +0x5C (amount) | | u8 +0x5A, +0x5B, +0x5C |
| 27 | | u32 +0x68 | u32 +0x7C | | | u32 +0x68 |
| 28 | | u8 +0x5D | u8 +0x62 | | | u8 +0x5D |
| 29 | | | u8 +0x5A | | | u32 +0x6C (world anim group) |
| 30..33 | | | u8 +0x63 (aim error), +0x5B (skill), +0x64 (damage), +0x5C | | | |
| 34..37 | | | s16 +0x6A, u8 +0x65, u8 +0x66, u8 +0x5D | | | |
| 38..41 | | | s16 +0x6C (range), s16 +0x6E, u8 +0x5E, s16 +0x70 (range) | | | |

The scripts on the disc read only 19 (kind 5: the world animation group of `world_anim_step`
/ `world_anim_set`) and 29 (kind 6: the same, for breakable glass).

## Spawning: records, models, entity types

A record becomes an entity only when **its record script calls `activate`** (level built-in
4 → `record_activate`, `0x80052D70`): if state bit 0 is clear it sets it and spawns by kind
(`record_spawn`, `0x8007505C`); a spawn that fails (too many soldiers) goes to the spawn queue
(64 entries, `spawn_queue_push` `0x80074EE8`) and is retried every frame. Every spawnable record
on the disc has a record script (all 2 257 records of kinds 2, 3, 4, 6 in the 24 levels but
one kind-4 record), so what activates a record is always a level-script decision; the usual
ones (catalogue.md lists the library):

| Record script | Activates | Re-arms |
|---|---|---|
| 6 (most records) | on message 3 from a zone or script sharing a type-1 tag | on 2 (culled) and 0x13 (removed by a 0x16 cull) |
| 38 | at level start (message 0), and on 3 | after 2 / 0x13 |
| 8, 10 | on 3; 10 also on 7 or 0x25 (a group member removed or hit, type-3 tags) | 8: on 7 |
| 14, 41, 149 | on 3, then again at once each time it is culled (0x13, 2) while enabled (0x14 / 0x15), up to 3 or 5 times | |
| 21 | on 3, only after 0x1D | on 2 |
| 26, 127, 132, 160, 162, 130 | variants: a partner (0xC), with 0x63, as a swap on 0x93, unless 0x96 / 0x9C came first, on alert (0x1B) | |
| 87, 164 | doors: on 3 (type-7 tags) | |
| 78, 107, 115, 126 | demolition charges: on 3 (Use), then a 0x59 countdown sends 0x20 | |

| Kind | Hash (1:3, 2:1, 2:2) | Count | Becomes |
|---|---|---|---|
| 0 | 0x4D078F23, 0x3011B913 | 3..11 | Nothing, never activated; actions by class when the player enters or leaves (Triggers) |
| 1 | 0xB81C10E3 | 26..36 | Nothing (`record_spawn` ignores it): a **trigger volume** (Triggers); 50 of 1059 on the disc have a record script (explosion points, alarms, relays) |
| 2 | 0x166A85FB, 0x8877B646 | 3..15 | An entity of the model named by the template at `+0x60` (`+0x4` its hash; `0x80074C54`); entity script `+0x64` |
| 3 | 0x4F292333 | 10..49 | A soldier: the template at `+0x74` (the weapon hashes, enemies.md); entity script `+0x78` |
| 4 | 0xBD12927E | 5..23 | A pickup (`0x80074CE8` → `pickup_spawn` `0x800833A0`, type 101); entity script `+0x64` (none on the disc) |
| 5 | 0x413B09BF | 0..2 | Nothing: a volume (2:1's: a 260 × 255 × 260 box) with enter/leave lists like kind 1, a Use list and tags (formats.md) |
| 6 | 0xD31F988A | 1..45 | Like kind 2 |

On spawning, the entity gets the record's position and angles (`0x80074AD8`), its model's
behaviour script (+0x94), and message 0 runs its entity script.

**Models** (`[0x8009EC58]`, count `[0x8009EC54]`, 0x100 bytes each, loaded with the level):
+0x0 hash, +0x94 the behaviour script started at spawning (-1: none), +0xB8/+0xBC the scripts
(enemies.md), +0xFC the **entity type**. The type is the model's: `spawn_by_type`
(`0x80061350`, from `0x8006D6F0`) creates the entity by it, then starts its script:

| Type | Constructor | Think | |
|---|---|---|---|
| 0x1402 | `enemy_spawn` (`0x800563F0`) | `enemy_think` | Soldier (7 models in 2:1/2:2, 2 in 1:3) |
| 0x1405 | `0x80054E4C` | `0x80054AB8` | Another character: soldier-like flags, 5 hit parts, contact damage |
| 0x1770 | `0x8005D678` | (none) | **Destructible**: solid, health from the record's `+0x5C`; damaged by bullets, explosions (type 0x31) and melee (`0x8005D32C`); its script gets message 1 on every hit, and its script decides what happens at 0 health (objects.md; e.g. 1:3's script 84: animate, explode, sound, remove); a destroyed one is counted by `0x80039D10` (career stat `+0x1C`: objects destroyed); record `+0x68` set: `0x800838AC` (a drop?) |
| 0x1778 | `0x8005D678` | (none) | The same model kind without collision (scenery) |
| 0x1771 | `0x80046B2C` | `0x80046A88` | Scripted, spinning by variables 3..5, removed when variable 6 < variable 2 (a timed effect?) |
| 0x1772 | `0x8005E2AC` | `mounted_gun_think` | Mounted gun, with a brain (ranges 3000) |
| 0x1774 | `0x800905C0` | `0x8008FDF8` | A large object with a sub-object and two parameter blocks (a vehicle?) |
| 0x1775 | `0x8005E510` | scripts | Scripted, a collision box, nothing collides with it |
| 0x1776 | `0x8005E648` | scripts | Scripted and solid, with hit parts (5 in 2:1: doors?) |
| 0x1777, 0x1779, 0x177A, others | `0x8005E7B0` | (none) | Inert; 0x1779 a box, the rest solid like 0x1770. Bullets fly on through 0x1777 but its touch handler takes their damage (combat.md), and its script gets message 1: 1:3's r49 (kind 6, script 84) was shot at vblank 6501 and removed itself |

Live types seen besides these: 1 the player's soldier, 2 the camera, 3, 0x31 (explosions),
0x4C the ladder (Triggers), 0x65 pickups.

`enemy_spawn` refuses while `0x80097FEC` soldiers are alive (`0x80097FE8`).

## Level logic: messages and scripts

Levels are event-driven: the engine, the trigger volumes, the model scripts and the level
scripts send numbered **messages**, and level scripts handle them. messages.md lists every
message id; this section is the machinery. Read in LEVEL.BIN (`msg_send` `0x80066EA0` and the
functions below, the 22 built-ins, the VM) and checked on RAM dumps of all 24 single-player
levels (`build/lvl/M_L.bin`, warped starts at vblank 10300; `lvs.py --check` passes on all).

### Scripts

The table `[0x8009D5AC]` (`g_level_scripts`, from the header's `+0x59C`): **+0x0 the number of
message ids** (158 in every level: ids ≥ it are refused with "Invalid messageID"), +0x4 the
number of script slots (167), then per slot the script's offset from the table (0: none). A
level holds only the scripts it uses (8..29). **Script ids are global**: an id is the same
bytecode in every level that has it (a library compiled once; catalogue.md lists it by role).
A script:

| | |
|---|---|
| u32 N | Number of handlers |
| N × {u16 message, u16 offset} | The handlers; offsets from the script's start |
| bytes | The script's own variables (byte 0: the once-flag), up to the first handler's code; shared by every record using the script (`local[]`, used by no script on the disc) |
| code | Byte code; jump targets and operand alignment are relative to the script's start |

**Two scripts per record.** `+0x54` is the record script; `+0x64` (kinds 2, 4, 6) or `+0x78`
(kind 3) the entity script. Which runs is decided per run by `script_of` (`0x8006602C`):
with an object (a record pair) in the context, the record's `+0x54`; with only an entity, its
record's `+0x64`/`+0x78`. So a script runs in one of two contexts:

| | Record context | Entity context |
|---|---|---|
| Started by | message 0 at level start (`0x80075210`), messages to subscriptions made in it | message 0 at spawn, 1, 0x1A, messages to subscriptions made in it |
| `self` / `self_obj` | 0 / the record pair | the entity / 0 |
| `ent[i]` (push F0) | the record's byte `+0x20 + i`: kept across spawns | the **entity's** byte `+0x20 + i` (`script_var_addr` `0x80066000`): lost with it |
| Typical work | `activate`, re-arm, objectives (the player start's record script) | `set_flags`, alerts, `remove_entity`, reports |

### Subscriptions, sending, the queue

Each message id has a list of subscription nodes (`[0x8009D5B0][id]`; nodes of 0x24 bytes from
a pool of 1024 at `0x800AD460`): +0x0 the subscriber entity (handle), +0x4 its object, +0x8
the filter, +0xC an entity (handle) and +0x10, +0x14 two values for the filter, +0x18 the
handler's code address, +0x1C the script id, +0x20 next.

**register_msg** (built-in 0, `msg_register` `0x8006692C`) subscribes the running script's
handler number H (an index into its handler table; the node gets that handler's message and
code) with a filter. It pops H, then the filter, then by filter: 0 nothing; 1, 2 a value (→
+0x14); 3, 4 an object (→ +0x10) then an entity (→ +0xC); 5 those two then a value (→ +0x14);
other filters print "Fatal Error: Unhandled FilterType" and do nothing. The subscriber is the
context's entity and object. An identical subscription (same subscriber, filter and values) is
not added twice; one with filter 0 first removes the subscriber's others for that message, and
an existing filter-0 node of the subscriber is turned into the new one. **unregister_msg** (built-in 1, `0x80066C14`) pops
the same way and removes the subscriber's matching nodes (filter 0: all of them for that
message). All of an entity's nodes are dropped when it is removed (`0x800665FC`).

The details an engine needs to get the same lists in the same order (read in `msg_register`
`0x8006692C`, `msg_unregister`, the pool `0x80066894`/`0x80066918`, and reproduced node for node,
addresses included, by `level_vm.py`):

- A node belongs to the subscriber when its entity handle is the subscriber's entity **or** its
  object is the subscriber's object.
- "Identical" compares the filter and its values only (filters 1, 2: `+0x14`; 3, 4: the filter
  entity or the filter object; 5: `+0x14` and the entity or the object), **not the handler**: a
  second handler of the same message with the same filter is silently not subscribed.
- New nodes are put at the **head** of the message's list, and sending walks the list from the
  head, so the newest subscription's context is queued first.
- The pool is a free list (LIFO): `msg_lists_init` (`0x80066790`, level start) chains the 1024
  nodes in address order; a freed node is pushed on the front and is the next one taken.
- Removal (`0x800665FC`) drops every node whose subscriber entity **or filter entity** (`+0xC`)
  is the removed entity; message 0x1A is run first, and only if the entity's record is not of
  kind 0.

**Sending** (`msg_send(id, sender entity, sender object, actor, target)`) walks the id's list
and queues a context for every node whose filter passes and, when a target is given, whose
subscriber object is the target:

| Filter | Passes when | On the disc |
|---|---|---|
| 0 | always | common |
| 1, 4 | the sender object's record (or the sender entity's `+0x1C` record) has `+0x34` (property 5) == +0x14 | never |
| 2 | the sender (entity or object) has a tag whose low byte is +0x14 (`record_has_tag` `0x8005351C`) | never |
| 3 | the sender entity is +0xC, or the sender object is +0x10: **the subscriber itself sent it** (its model script, its own record's lists) | common |
| 5 | the sender's record and the +0xC/+0x10 record share a tag of type +0x14 (`record_tags_match` `0x8005359C`: both sorted lists, tags with `tag >> 16` == type) | most: the **tag groups** (messages.md) |

A queued context (32 bytes, a ring of 128 at `0x800B6460`, read `0x8009D5B8`, write
`0x8009D5BC`; "queue full" drops the message): +0x0 the code address, +0x4 the subscriber
entity, +0x8 the sender entity, +0xC the actor, +0x10 the subscriber object, +0x14 the sender
object, +0x18 the message, +0x1C the script id. Contexts run after the frame is drawn
(`msg_run_all` `0x800663B8`; objects.md), each only if its subscription still exists
(`0x8006A204`): unsubscribing cancels queued messages, and a subscriber entity that is being
removed gets none. **Direct runs** (`script_run_message(entity, object, message)`,
`0x800664A0`) find the handler for the message in the script and run it at once with
sender, actor and sender object 0. What each source puts in the context: messages.md.

More precisely (`msg_enqueue` `0x800670E8`, `0x8006A204`):

- The ring holds 127 contexts: a send is dropped when the write index would reach the read
  index.
- When queuing, a subscriber entity flagged as being removed (flags A 0x80000) gets nothing; a
  sender or actor being removed is stored as 0.
- "Still subscribed" means: the subscriber (its entity if the handle is alive, else its object;
  neither: no) has **any** node for that message, not necessarily the one that queued it; a
  context of message 0 always runs.
- **The queue is not always drained.** After the drawing pass the game runs one message, then
  runs the queue and the spawn retries over and over until the vblank comes (objects.md); a
  frame that ends late leaves the rest, which runs at the end of the next frame after that
  frame's own sends. In the recorded session that happened at the end of 8 of 1:3's 7223 frames,
  97 of 2:1's 8005 and 52 of 2:2's 6448 (the read index at each frame shows how many ran). An
  engine that drains the queue after every tick differs only in those frames, by one frame.

### The VM

`level_vm` (`0x800672E0`): byte opcodes over a stack of 8-byte {type, value} entries
(`g_vm_stack`, `0x800AD260`, top index `0x8009D5A8`). Types: 0x0A signed, 0x0B
unsigned/boolean, 0x0C, 0x0D, 0x0E float (PsyQ software floats), 0x0F string, 0x10
entity/pointer. Binary operators convert int to float when the types differ. Built-ins are
called with (context + 4, script base, script id). `tools/recon/lvs.py RAMDUMP [ID...]`
disassembles the scripts (folded into statements by default; `--check` verifies them).

| Op | Operands | |
|---|---|---|
| 00 | | nop |
| 01 | | pop |
| 02 | value (below) | push |
| 03 | F0..F3 variable | store the top into a variable, pop. Whole byte (selector 9): the value's low byte for types 0x0A/0x0B, the float converted for 0x0E, nothing for other types; a bit (1..8): set if the value is non-zero, else cleared |
| 04..0F | | binary: **T op N**, T the top (pushed last), N the one below; push the result. 04 ==, 05 &&, 06 \|\|, 08 <, 09 >, 0A <=, 0B >=, 0C +, 0D −, 0E ×, 0F ÷ (read from the integer paths: 08 `slt T, N`, 0D `T - N`, 0F `T / N`). So `ent[0] - 1` pushes 1 then `ent[0]` |
| 07 | | ! |
| 10 | u16 (aligned) | pop; jump if zero |
| 11 | u16 (aligned) | jump |
| 12 / 13 | u8 / u16 (aligned) | call built-in |
| 14 | | end |
| 15 | | push 1 if the once-flag (byte 0 of the script's variables) is still 0, else 0 (lvs.py's `first_run`) |
| 16 | | set the once-flag to 1. The flag is the script's, shared by every record and entity running it |
| 17 | type, value | debug print |
| others | | "Invalid Opcode", end the script |

**Values** (push 02):

| First byte | Then | Pushes |
|---|---|---|
| 09 | u8 | that byte |
| 0A..0D | u32 (aligned 4) | an integer of that type |
| 0E | f32 (aligned 4) | a float |
| 0F | length, bytes | a string |
| 10 | 4 bytes (aligned 4) | a pointer to them |
| F0, F1 | selector, index (u8; F1: u16 aligned) | `ent[index]`: the variable byte (record or entity, above) |
| F2, F3 | selector, index | `local[index]`: a byte after the handler table |
| F4..FF | T, property (u8) | T 0x10: the thing itself; otherwise the record property (Record properties). Sources: FA self, FB sender, FC actor (entities; with T 0x10 they push the entity), F4 = FA, F5 = FB, F6 = FC for properties but with T 0x10 they push the object: F4 self_obj, F5 sender_obj, F6 always 0. FD..FF and F7..F9 select no entity (0) |
| others | | end the script |

The selector picks the part of the byte: 9 the whole byte, 1..8 bit 0..7 (store: set if the top
is non-zero). Wide forms: the VM reads a 16-bit index through a pointer made from one byte when
T (not S) is F7..F9 or FD..FF, and then shifts the source by 3 (FD..FF ≡ FA..FC); no script on
the disc uses them.

**Built-ins** (`g_level_builtins`, `0x8009D5C0`, filled at level start by `0x80066110`;
arguments in push order, as lvs.py prints them; the routines are named in messages.md):

| # | Name | Pops | |
|---|---|---|---|
| 0 | register_msg | 2 + by filter | (.., filter, handler index): above |
| 1 | unregister_msg | 2 + by filter | the same |
| 2 | send | 1 | (message): broadcast from this script (sender = its entity and object). Formerly `send_self`: it is not addressed to the script itself |
| 3 | set_flags | 1 | the entity's `+0x40` \|= value (`0x800660F4`); soldiers read it as `s40` (soldier.md) |
| 4 | activate | 2 | (entity, object): `record_activate(object)`; every use on the disc is `activate(self, self_obj)` in record context |
| 5 | remove_entity | 1 | (entity) |
| 6 | explosion | 1 | (kind): sound, particles, damage around the script's entity |
| 7 | entity_test | 3 → 1 | (entity, object, n): the level function table `0x80098630`: 0 the entity is the player (flags A 1), 1 it is dead (flags A 0x20000000). Only 1 is used. (Formerly `level_func`) |
| 8 | show_text | 1 | message-table text (`0x80098188`) on screen (`text_add`) |
| 9 | has_tag | 3 → 1 | (entity, object, value): the record has a tag whose **low byte** is the value |
| 10 | tags_match | 5 → 1 | two records share a tag of a type (filter 5's test) |
| 11 | set_home | 2 | the entity's home (`+0x3A4`) from another entity or a record |
| 12 | send_to | 3 | (target entity, unused, message): broadcast, to subscriptions whose object is the target's record; a target of 0 makes it a plain broadcast |
| 13 | level_set | 2 | (what, value; the value taken as s16): 0 fog near, 1 start the music, 2 primary objectives complete (`g_bPrimaryObjectivesComplete`), 3 end the level (`0x8009C840` \|= 0x24), 4 raise an alarm, or end the one sounding (`alarm_from_level`, `0x8005F068`; audio.md). 5 (used with 1..3 next to texts): **no-op**, there is no case 5 |
| 14..17 | objective_add / remove / complete / is_complete | 2, 1, 1, 1 → 1 | (text, id), (id), (id), (id) |
| 18 | world_anim_step | 1 | step the animated world textures of that group (modes: once, random, loop, reverse) |
| 19 | sound | 7 (→ 1) | (..., mode): pushes a handle when the mode is 3 (audio.md) |
| 20 | sound_stop | 1 | |
| 21 | world_anim_set | 2 | (group, frame) |

**An example**, 1:3's objectives (script 45, lvs.py output, comments added):

```
on msg 0:
    level_set(1, 1)               # music on
    level_set(2, 0)               # primary objectives not complete
    register_msg(0, 1)  # msg 37  # filter 0, handler 1
    ...
    objective_add(223, 14)        # text 223, id 14
    objective_add(224, 15)
    objective_add(225, 16)
on msg 37:                        # the attache case's volume entered
    objective_complete(14)
    unregister_msg(0, 1)  # msg 37
    show_text(226)
    sound(0, 131072, 0, 65536, 53, 0, 0)
    ent[0] = (ent[0] + 1)         # objectives done
...
on msg 8C:                        # the exit volume
    if !(ent[0] < 3) goto L1DE
    level_set(5, 3)               # no-op
    show_text(272)                # not all objectives met
    ...
  L1DE:
    level_set(2, 1)               # primary objectives complete
on msg 8D:
    level_set(3, 1)               # the level ends
```

**Objectives** (`objective_add`, `0x8005372C`: adds at the end, or for a known id resets it to
incomplete with the new text in place; the text from the message table `0x80098188`;
`objective_remove` `0x8005380C` moves the last entry into the removed one's place, and for an
unknown id still drops the last entry (the count can even go to -1); `objective_complete`
`0x800538B0`, `objective_is_complete` `0x8005394C`, 1 for an unknown id): ids at `0x8009892C`,
done flags at `0x800988DC`, texts at `0x80098904`, count at `0x80098620`;
`g_bPrimaryObjectivesComplete` (`0x80098624`) is what `level_end_carry` and the rating check.

`texts_update` (`0x8005F69C`), run every tick after the collisions, ages the on-screen texts:
32-byte entries at `0x80099F4C` (count `0x80099ED0`, at most 50) with a lifetime and a colour
fade, added by `text_add` (`0x8005F28C`).

### Level start

`level_start` (`0x80061E14`), in this order: every entity but type 8 removed; the message
system reset (`0x80066290`, then `0x80066108`: empty lists, the node pool chained afresh, the
queue emptied, the built-in table filled); the spawn queue cleared (`0x80074EA8`); the
objective count set to 0 (the arrays keep stale entries); the player's soldier gets message 0
(its script 13, entity context); every record's state bits cleared; message 0 to every record
of kind 1..6 in table order (`0x80075208`, record context: the objective script, script 38's
start activations...). Nothing resets the records' variable bytes (`+0x20..+0x27`) or the
scripts' own bytes: they come from the BSD as loaded. A restart after a death
(`level_restart` `0x800624AC`, `level_main` `0x800640B4`) runs `level_start` without reloading
the BSD, so they keep what the scripts wrote (read from the code; the session has no death).

### Checked against a reimplementation

`tools/sim/level_vm.py` reimplements this section, the record states and spawning, the
triggers and the objectives from these documents, and compares the original's memory with its
own at every frame of the recorded session's three levels (1:3, 2:1, 2:2; 21 676 frames, one
`VALOR_WATCH` replay and one dump before and one inside each level). It owns the VM and the
built-ins, the node pool, sending and the filters, the queue, the record states and variables,
activation and the spawn queue, the zone bits and the enter/leave/Use lists, kind-0 records,
objectives and `level_set`; it takes as the engine's events the trace's positions and entity
list, the zone under each walker (`+0x390`), the pad (Use), the hits (`VALOR_TRACE_DAMAGE`),
removals (dead, culled, picked up), the 1/3-second counter, alarms and the messages model
scripts send (op 0x43, recognised in the ring), and where each spawn lands. Exact on every
frame: every record's state word and variable bytes, every script's own bytes, every
subscription list (node addresses included), the queued contexts and the ring indices, the
objectives (ids, texts, flags, count, the primary flag), the level-end flags, the spawn queue,
the player's and the character slots' variables, and the variables of every record-spawned
entity in the dumps inside the levels. The only differences are two single frames of zone bits
(below, Triggers). `set_flags` is not compared: it sets event bits (`+0x40`) that the entity's
model script consumes in its next think, so a once-per-frame watch never sees them.

## Objectives by level

The objective script is the player start's record script (ids 43..63, 69, 70, 72, one per
level). It adds the objectives at message 0, completes each on an event message (0x37..0x40,
or a relay), counts completions in `ent[0]` (`ent[1]` in 3:2, `ent[2]` in 3:1) and, on 0x8C
(the exit volume), sets the primary objectives complete if the count reached N; 0x8D (a
smaller volume inside it) ends the level either way. Read from every level's objective script
(`lvs.py`) with the senders found by `lvl_msgs.py --level`; `rN` is the record index (`lvl_records.py`).
Paraphrased objective names (text ids in brackets); "enter" = the player enters the record's
volume, "Use" = Use pressed inside it. catalogue.md has the same per level with more context.

| Level | Objectives → completed by | N |
|---|---|---|
| 1:1 | find the plane (18) → 0x37 enter r52, which adds "enter the town" (20) → 0x39 enter r53; recover the logbook (19) → 0x38 enter r74 (the logbook object) | 3 |
| 1:2 | neutralize the Gestapo (216) → 0x37 from the script itself after 8 × 0x64 (8 soldiers of script 92 dead; a text at 4); destroy the radio (217) → 0x38 from the radio's script 95 when destroyed; locate the hideout (218) → 0x39 enter r57 | 3 |
| 1:3 | attaché case (223) → 0x37 enter r95; weapon cache (224) → 0x38 enter r57; exit the sewer (225) → 0x39 enter r56 | 3 |
| 2:1 | switch tracks (47) → 0x37 from script 99 (kind-5 r82/r84) on 0x5C (Use at a switch); ID blue (45) → pickup 0x2F → 0x38; ID red (46) → pickup 0x30 → 0x39; 0x3C (enter r98): "end of level located" text | 3 |
| 2:2 | gift package (53) → 0x37 Use r48; freight manifest (52) → 0x38 enter r31; ID red (46) → pickup 0x30 → 0x3A; reserve engine (54) → 0x39 from script 98 (its charge exploded); rail tunnel (55) → 0x3B enter r62 | 5 |
| 2:3 | grenades (60) → 0x39 enter any of the pickup records r112..r114; fuel containers (61) → 0x38 after 8 × 0x67 (script 111 destructibles); exit the canyon (62) → 0x37 enter r150 | 3 |
| 2:4 | destroy the railgun (66): four charges placed (0x68 × 4 from script 115, texts per charge) → 0x74 starts their countdown → 0x79 → script 129 explodes and sends 0x37: primary complete at once; 18 timer ticks later the objective completes and 0x38 ends the level (no 0x8C/0x8D) | - |
| 3:1 | promenade (254) → 0x37 enter r126 (or a character entering); bridge (69) → 0x38 enter r125/r148; engineering (68) → 0x39 enter r124; wrench (266) → 0x3A via 0x8E (enter r99, script 136); jam the engine (259) → 0x3B via 0x92 (Use r116 with the wrench: 0x8F) or enter r84; exit through the cargo hold (72): 0x8C is Use at r79 and requires the 5 others (`ent[2]` ≥ 5), then completes it and the primary objectives; 0x8D enter r131 | 5 |
| 3:2 | hatchway (81) → 0x37 enter r152; smuggled weapons (78) → 0x38 Use r131/r151 (once); timetable (79) → 0x3A enter r116/r135 (once); blow up the truck (80) → 0x39 via 0x68 (scripts 97 r117/r119) or from script 98 | 4 |
| 3:3 | engine specs (86) → Use r136/r163/r164 (0x37), completed 3 timer ticks later; hull blueprints (87) → 0x38 enter r191; sea door control (88) → 0x39 via 0x20 (charge script 78 r137) or script 141; board U-4901 (89) → 0x3A enter r192 | 4 |
| 3:4 | radio coordinates (94) → 0x37 Use r58; lock the fins (95) → 0x38 Use r64; fin controls (96) → 0x39 via 0x68 (script 97 r62); blow ballast (97) → 0x3A Use r69..r72; exit hatch (98) → 0x3B enter r56 | 5 |
| 4:1 | crowbar (99) → 0x38 enter r179; Stuka (100) → 0x39 from script 98 (r181) exploding; open the hatch (101) → 0x61 (the hatch door opened: scripts 80, 100) → 0x37, which adds "sublevel 1 entrance" (287) → 0x3A enter r211 | 4 |
| 4:2 | false SOS (105) → 0x38 Use r228; orders (106) → 0x3B enter r223 (adds "gas mask" (108)); key (107) → 0x39 enter r233; gas mask (108) → 0x3A enter r151 (adds it first if needed); 0x3C enter r230: "end of level located" | 4 |
| 4:3 | gas valve 1 (114) → 0x37 Use r108; gas valve 2 (115) → 0x38 Use r107; exit the fort (116) → 0x3A enter r128 | 3 |
| 5:1 | water flow (128) → 0x37 Use r244; enter the plant (129) → 0x3A enter r286; power relays (130) → 6 × 0x68 (script 97 r247..r252; texts 1..5 of 6) → 0x38; main grid (131) → 0x39 Use r253/r284 | 4 |
| 5:2 | generators (141) → 0x37 when the three Use volumes r114..r116 (0x6E, 0x6F, 0x70) are all done; heating tanks (142) → 0x38 after 2 × 0x5C (valves: script 106 on 0x2E); research files (143) → 0x39 after 6 × 0x68 (script 97 r100..r105); D2O report (144) → 0x3A enter r98; elevator (145) → 0x3B enter r106 | 5 |
| 5:3 | heavy water vats (158) → 0x37 after 4 × 0x5C (valves, script 106 on 0x2E); garage (161) → 0x38 enter r159; delivery schedule (160) → 0x39 enter r158; release valve (159) → 0x3A Use r180; truck (162) → 0x3B via 0x68 (script 97 r173/r178); 0x3F enter r182: "end of level located" | 5 |
| 5:4 | both trucks (168) → 0x37 after 2 × 0x68 (script 97); power station (273) → 0x3A from script 146 (r101) exploding; ferry (169) → Use r98 (0x38), completed 12 timer ticks later; 0x39 enter r168: "end of level located" | 3 |
| 7:1 | generator fuel (173) → 0x37 after 4 × 0x68 (script 125, kind 6 r147..r150); mine entrance (174) → 0x38 enter r123 | 2 |
| 7:2 | elevator key (177) → 0x37 enter r113; SS demo squad (178) → 0x38 after 8 × 0x64 (script 150 soldiers dead); elevator (145) → 0x39 enter r77; manuscript (276) → 0x3A enter r112 | 4 |
| 7:3 | curator inventory (30) → 0x37 enter r76; disarm 12 bombs (31) → 0x38 after 12 × 0x67 (Use on each bomb: script 96 on 0x65); 0x3E enter r124: "end of level located" | 2 |
| 9:1 | V1 target list (190) → Use r111/r167 (0x37), completed 5 timer ticks later; launch control (193) → 0x38 via 0x68 (script 97 r112); blast doors (191) → 0x39 Use r105; emergency hatch (279) → 0x3A enter r144 | 4 |
| 9:2 | V2 schematics (204) → 0x37 enter r114; guidance lab (206) → 0x39 enter r99; gyro (207) → 0x3A Use r106; scientist roster (203) → 0x3B enter r112; launch area (237) → 0x3C enter r102 | 5 |
| 9:4 | launch the V2 (214) → Use r5 (0x2C to script 39, which sends 0x37 at once); then a scripted sequence on the 0x59 timer (script 39: 126 ticks → 0x3E; script 72 counts: 0x3 at 10, 0x98 at 44, 0x20 at 53; 0x74 from a model → 0x3F; script 112: 48 ticks after 0x3F sends 0x3D) ends the level with the primary objectives complete (`level_set(2, 1)`, `level_set(3, 1)`); no music until then, no 0x8C/0x8D | - |

## Results: the rating and career totals (SHELL.BIN)

After a level the menus read the statistics (combat.md) and `g_level_end`:

**The rating** (`level_rating`, SHELL `0x8007344C`, "m_pmeval_overall"), 0..2:

| Rating | When |
|---|---|
| 0 | The primary objectives aren't complete (`g_level_end+0xC`), or kills (`0x80039CB8`) are below the level's requirement |
| 1 | Objectives complete and enough kills |
| 2 | ... and more than 75% health at the end (`g_level_end+0x8` / 128.0 > 0.75) |

The kill requirements (SHELL `0x8003BA40`, 4 words per row; 9999: no such level) are per
**campaign slot**: the row is `g_game+0xA8` (copied to `0x80039C88`), the level `0x80039C84`.
The slots, at `0x80038D90` (12 bytes each: mission index, current level, level count; set at
run time), and their requirements:

| Slot | Mission | Levels | Level 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|---|
| 0 | MSN1 | 3 | 21 | 49 | 40 | - |
| 1 | MSN2 | 4 | 5 | 9 | 35 | 17 |
| 2 | MSN3 | 4 | 29 | 24 | 37 | 25 |
| 3 | MSN4 | 3 | 68 | 64 | 51 | - |
| 4 | MSN5 | 4 | 49 | 40 | 46 | 43 |
| 5 | MSN7 | 3 | 49 | 32 | 30 | (51) |
| 6 | (MSN8, not on the disc) | 3 | 45 | 53 | 26 | 26 |
| 7 | MSN9 | 3 | 45 | 53 | (26) | 26 |

When a level starts (SHELL `0x80062108`): slot 6 becomes slot 7, and in slot 7 the third level
becomes the fourth with the third's carry-over row copied to it, since MSN9 has LVL1, LVL2 and
LVL4. MSN6 and MSN8 don't exist. Mode 2 (`0x80039D1C`) plays MSN12 (seven levels, the
two-player arenas, run by LEVEL2P.BIN: mission index 11, the level from `0x80039C74`; shell.md).
There are no single-player bonus levels on the disc.

The **best rating per level** is kept in the level's carry-over row: halfword 13 of
`g_level_ammo[mission][level]` (`g_game+0x18C + mission·152 + level·36 + 26`), -1 until the level
is completed; a new rating replaces it only if higher. A real save had 2 on 1:1..1:3 and 1 on
2:1 and 2:2.

**Career totals** (`career_add`, SHELL `0x80062694`) add the level's counters into `g_game`:
+0x10 shots, +0x14 hits, +0x18 wounds, +0x1C objects destroyed (`0x80039D10`), +0x20 kills, +0x24 head hits,
+0x28 torso hits without part 8, +0x2C part 8 (the groin), +0x30 parts 10/12/14/16, +0x34 parts
2/6, +0x38 parts 9/11/13/15, +0x3C parts 1/5 (legs and arms, one side each), +0x40.. kills per
weapon (from `g_level_end`).

## Triggers

Records have a volume (`+0x10` its centre, `+0x28` the shape, `record_contains`,
`0x80083D30`): 0 a sphere of radius `+0x2A`; 1 a cylinder of radius `+0x2A` from `+0x2E` to
`+0x2C` around the centre's height (1:3's kind-1 records: radius 330..542, ±500); 2 a box of
sizes `+0x2A`, `+0x2C`, `+0x2E` (x, y, z) turned by the record's angles `+0x18..+0x1C` (YXZ)
(`0x80083AB8`).

Each floor **zone** (the k-d leaf's id, Collision) lists the records whose volumes reach into
it (`[0x8009EC5C]`: per zone a pointer to a byte holding **255 minus the count**, then from +2
the u16 indices into the level's record table, sorted). Every tick, the player and the walkers
test their position against their zone's records (`zones_update`, `0x800845A8` →
`zones_mask` `0x80084400`, called by `entities_update` right after `entity_move` for entities
with flags A & 5, and doing anything only with flags A & 0x51): the zone in `+0x390`, a bit per
entry of the zone's list in `+0x394` (bit k = the k-th index), last tick's zone and bits in
`+0x398` and `+0x39C`; every bit that changes is an **enter** or a **leave** (`zones_events`
`0x800840E0`). The position is the integer part (`+0x20E`, `+0x212`, `+0x216`) after the move.
Which records are tested: for the player, kind-0 records and records with an enter, leave or
Use list; for a character, records of kind 1..6 with a character enter or leave list.

When the zone is the same as last tick, every changed bit is reported (leaves first, then
enters). When it changed, `zones_events` walks the old and the new zone's sorted lists
together and reports a change only for a record **in both lists**; a record in one list only
is skipped while both lists still have entries, and only the tail of the longer list is
reported (leaves for the old list's tail, enters for the new one's). So stepping into a volume
at the very tick the floor zone changes, when that record is not in the old zone's list and
sorts before the end of it, sends no enter at all (the bit is set, so no later tick sends it
either); likewise a leave. Read in the disassembly (`0x800842B4..0x80084344`); 2:1 has five such
skipped changes, none with a subscriber, so the session can't tell them apart.

| | Enter | Leave |
|---|---|---|
| The player (flags A 1) | send each message of the list at `+0x38` (a count, then the messages: `send_event(message, 0, record, entity)`) | the list at `+0x3C` |
| Characters (flags A 0x50) | `+0x40` | `+0x44` |
| The player pressing Use (`player_use` `0x80084670`, from `player_update` on action 5 pressed, and from the mounted gun's touch → `record_use_send`) | the list at `+0x4C` of every record of kind 1..6 whose bit is set in `+0x394`: the bits of the last zone update, since the think that reads the pad runs before the move | |
| Kind-0 records (the player; `record_enter_kind0` `0x80074E10`, `0x80074DC4`) | by class: 0x4D078F23 **streams the world** (`0x80054378` → `0x8005439C`: the record says which TSP chunk to load into which slot, replacing the one there), 0x3011B913 a **ladder** (`record_spawn_type(0x4C)`: an entity of type 0x4C, below, only while the record's bit 0 is clear), 0xD1BA7D1E would end the level (`0x8009C840` \|= 0x24) and 0x271BD574 does nothing, but no record on the disc has either class | 0x3011B913: the ladder is **held** (below); nothing is spawned |

**Ladders**: the ladder entity (type 0x4C, `ladder_spawn` `0x80061244`) holds the record's
radius (`+0x2A`) and top (its y less `+0x2C`). While the player is within the radius and
below the top, its think (`0x800611D0`) sets the player's `inv+0x70` to the top, and the
controls climb (`0x8007F1E0`): gravity off, half the forward input as vertical speed, up to
the top or down to the floor. Leaving a ladder's volume does not remove it:
`0x80074DC4` calls `0x80061518(pair)`, which **holds** that ladder in `0x8009A5B8`, first
releasing the one held before if it is another (`0x8006149C`: removes the entity whose `+0x1C`
is the held record, if that record's bit 0 is set, which clears the bit). `ladder_spawn` forgets
the held ladder if it is the one being made, else releases it. So a ladder stays up until the
player has left it and then enters (or leaves) another; re-entering a ladder whose entity
stays makes nothing. Checked on the session: 1:3's one ladder r1 keeps state 1 from its first
climb to the level's end; in 2:1 r1's bit drops at vblank 23190, the frame r6's ladder is made.

**Two frames differ** in the session, each one tick: the player's bit for 2:1's streaming record r85
at vblank 29305 (inside it at his traced position, outside for the game) and a soldier's bit for
2:2's r75 at 53679 (outside at his traced position, inside for the game until a tick later;
r75 has no character leave list).
The trace's position is the one at the end of the tick; what moves an entity after
`zones_update` in those two ticks (the world-contact handler, root motion) was not found.
Nothing downstream changed.

Only records with lists (or kind 0) are tested. The messages these lists send, and what they
hold in the handler's context (the record as sender object, the entering entity as actor):
messages.md.

## Collision: TSP chunks

The level's world collision is in its **TSP chunks** (`TSP0\m_l_cN.TSP`, loaded by
`0x8008C850` for each name in the header): the TSP header's word 15 (+0x3C) is the offset of a
collision block, which `0x8003EB44` points the chunk's entry at (`0x80090DE8`, 0x28 bytes per
chunk, count `0x80098984`). `tools/recon/tspcol.py` reads it (and exports the triangles as OBJ);
all 220 TSP files on the disc parse with every node reached, every face in a leaf and the block
ending at the end of the file.

| Block | |
|---|---|
| +0x00 | s16 x min, z min, x max, z max: the chunk's rectangle |
| +0x08 | u16 counts: nodes, face indices, vertices, normals, faces |
| +0x12 | Nodes, 8 bytes {s16 a, s16 b, u16 split}: a 2D k-d tree over x/z (root 0). a < 0: a leaf of ~a faces listed from face index b. Else a split at (x min + split) on x when b >= 0 (children a, b), on z when b < 0 (children a, ~b) |
| | Face indices (u16), padded to 4 bytes |
| | Vertices, 8 bytes {s16 x, y, z} |
| | Normals, 8 bytes {s16 x, y, z}, unit length 32767 (y down: floors have y < 0) |
| | Faces (triangles), 10 bytes {u16 v0, v1, v2, normal, s16 d}: the plane is (normal · p) >> 15 + d = 0 |

**Sweeping a segment** (`world_sweep`, `0x8003EF5C`, for everything but walkers): every chunk
(but `0x800AA184`) whose rectangle holds either end or is crossed by the segment is searched
(`0x8003E3F0`): the tree is walked along the segment, nearer half first, up to 8 deferred
halves; in each leaf (`0x8003DC10`) a face is hit where the plane distance changes sign between
the ends (t = |d0| / (|d0| + |d1|), in 1/4096) and the crossing point is inside the triangle
(three edge cross products against the normal). The nearest hit wins: its point, normal and
face (`-1`: nothing hit).

**The floor** (`floor_find`, `0x80060474` → `0x8006028C`, from `entity_move` and spawning).
Every rule below is reproduced by `tools/sim/move_sim.py` (`floor_find`), which matched the
original on every tick of the recorded session's three levels (movement.md, Checked against a
reimplementation); integer coordinates are the 16.16 position's whole parts.

- **Chunks**: the loaded slots in slot order (`0x80090DE8`, skipping the one being streamed,
  `0x800AA184`) whose rectangle holds (x, z), bounds included.
- **Leaf** (`floor_leaf`, `0x80060034`): from the root, a split on x goes to child b (and the
  cell's x min becomes x min + split) when x >= x min + split, else to child a; a split on z goes
  to child a when z < z min + split, else to ~b (z min moves up). Split offsets are relative to
  the current cell, not the chunk. The leaf's fourth halfword is a **zone id**, kept in the
  entity's `+0x390`.
- **Triangle holds (x, z)** (`0x8005FC00`): its normal's y must be <= 0 (facing up or vertical);
  then a box test that is **not symmetric**: x below v0's x, or above the larger of v1's and
  v2's x, rejects (v0 alone is taken as the minimum); z outside the three vertices' true min and
  max rejects; then GTE NCLIP of (v0, v1, P), (v1, v2, P), (v2, v0, P) on (x, z) pairs must all be
  <= 0.
- **Height** (`0x8005FDCC`): `y = -(nx·x + nz·z + (d << 15)) / ny`, a C division (towards
  zero), kept as s16; a plane with |ny| <= 0x100 gives v0's y instead.
- **Choice in a leaf** (`floor_pick`, `0x8005FEA0`), faces in leaf order: a height is a
  candidate if it is below the feet (y < h) or at most 32 above them (h + 32 >= y). The first
  candidate is taken; after that, if the best so far is below the feet, an "above" candidate
  always replaces it and a "below" one only if higher (smaller y); if the best so far is above,
  any candidate nearer the feet (|h - y|) replaces it.
- **Choice between chunks** (`floor_search`): the first chunk's result is taken; a later chunk's
  (if any) replaces it by the same nearer/below logic, except that the game compares the
  whole-unit heights with the feet's **16.16** y (`lw 4(s2)`), a unit bug that only matters where
  two loaded chunks overlap.
- A floor whose normal's vertical part is less than 1.75 times its largest horizontal part
  (steeper than about 30°) is flagged steep (`0x80060138`); nothing reads the flag
  (movement.md). No floor: -0x7FFF.

**Walkers** (`walker_slide`, `0x80078F60`: the player and characters, flags A 0x8). Read from
the code and reproduced exactly by `move_sim.py` (`walker_slide`) on 2,700 wall-contact ticks of
the recorded session and two scripted 1:1 runs. With r = `+0x2EC` (100 for the player, 60 for
soldiers), e = `+0x2D6` (the eye height: 240, 188, 80) and fp = e - 16:

1. **Only if the whole-unit position changed**: the move's delta `(new - old)` in whole units
   (old = `+0x3BC`, where the last `entity_move` ended) is part of every test below; a plane is
   kept only when the delta goes into it (`delta · normal < 0`, GTE MVMVA, 32-bit). Standing
   still (or moving less than a unit) gathers nothing.
2. **Gather** (`walls_gather` `0x80078E2C` → `walls_gather_chunk` `0x800787C0` →
   `walls_gather_leaf` `0x8007848C`), around a vertical segment from c = (x, y - fp, z) down to
   y - 4 (half-height h = e - 20): every loaded chunk whose rectangle holds (x, z), or whose
   rectangle's nearest edge point is within r (`0x80078AA4`; for a coordinate inside the
   rectangle's range it takes the nearer edge, not the coordinate itself). In a chunk, the tree is
   walked to every leaf whose cell comes within r of (x, z) (both sides of a split when the split
   is within r, the far side kept on a stack of at most 8, dropped beyond that). In a leaf, a
   triangle is kept when (`0x800776C0`, `plane_crosses_capsule` `0x80078350`):
   - its box comes within r of (x, z) (x again judged against v0's x as the minimum and the larger
     of v1/v2 as the maximum) and its y range reaches into [c.y, c.y + h];
   - and it is **not an up-facing floor-like triangle** (ny < 0 and |ny| >= max(|nx|, |nz|), i.e.
     floors and slopes up to 45°, are never walls for the player);
   - a wall-like triangle (|ny| < max(|nx|, |nz|)) counts if one of its vertices strictly inside
     the segment's height range is within r horizontally, else by a box test (`0x8007711C`): seen
     along its main horizontal axis (x-y when |nx| < |nz|, else z-y), the corner of the 2r × h box
     around (c, c.y + h/2) facing each edge must be on the inner side of all three edges (NCLIP);
     a down-facing (ceiling-like) triangle by the same idea seen from above, with the 2r square
     around (x, z);
   - the plane crosses the segment, or the segment's top is in front and the point r towards the
     plane (x ∓ r, z ∓ r by the normal's signs) is behind or crosses it;
   - not already gathered from this chunk; at most 32.
3. **Push** (`wall_first_hit` `0x80078D1C`, `wall_push` `0x80078C4C`): the signed distance of a
   plane (`0x80078BCC`, 16.16) is taken at the feet for up-facing planes and at fp above the feet
   for the others, less an offset. Among the planes now nearer than r, the one **nearest at the
   old position** (offset 0; the most negative wins) pushes: `v = dist(r + 4) >> 8`,
   `x -= (nx >> 7)·v`, `z -= (nz >> 7)·v`: out to r + 4 along the normal's horizontal part, scaled
   by the full 3D distance, so a sloped plane needs several pushes. This repeats up to n + 1 times
   (n planes gathered); planes stay in the list and can push again.
4. **Re-check**: if anything pushed, the planes are gathered again with the new delta, but
   **centred on the feet** (c.y = y, the segment running from the feet down to e - 20 below them:
   apparently a slip for the centre), and if one of them is still nearer than r the result is 1,
   else 2. Nothing pushed: 2 (gathered but clear) or 0 (nothing gathered).

`entity_move` then, for the player: result 1 → the **whole move is undone** (position = old,
on-ground restored), the soldier stops dead for that tick; result 2 → contact flag 0x8000; any
plane in the re-check → `+0x218` and `+0x220` (the strafe and forward speeds) cleared and the
delta stored at `+0x364`. Its "ceiling" branch needs a whole-unit vertical delta of 0x801 or more
(`lh 0x42(sp)` is the delta, not a normal) and never runs. While the brain's walk goal is on, the
same gathering also sorts the walls ahead/left/right for steering (enemies.md).

## Open

- Types 0x1405, 0x1771, 0x1774 in game (kind 5 and the streaming records: formats.md).
- Record `+0x50` (a sixth relocated list offset, read by nothing found); the record fields
  behind properties 21, 24, 25, 27, 28 and soldiers' 33..37, 40.
- `0x8006A3BC`, the "near the player" test that stops the cull countdown (message 2).
- The Use button: action 5's mask in the action table (`0x800B7790`) is 0x80, but in the
  recorded session's pad word (`0x80093B7C`) every Use the queue proves shows as 0x08, and 0x80
  never appears (`level_vm.py` reads Use from 0x08).
- Effects of most `set_flags` bits on soldiers (`s40`, soldier.md) and on doors and switches
  (their model scripts).
- The BSD header's +0x544..+0x588 sections and the TAF files (formats.md).
