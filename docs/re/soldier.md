# The soldier: behaviour scripts

What the enemy soldier (type 0x1402) does, read from its model's scripts: 218 in 1:3
(`build/re/soldier_1_3.sst`), 312 in 2:1 (`build/re/soldier_2_1.sst`), disassembled by
`tools/recon/sst.py` from RAM dumps of real play. The VM, the brain and the weapon are in
enemies.md; this document is what the scripts do with them.

References are `script:word` in 1:3's numbering (`25:291` is script 25, word 291) unless 2:1 is
named. Claims come from the disassembly; where an opcode handler was needed it was decompiled
(`fn.py c`) and is named. Inferences are marked "(inferred)"; the rest is in Open.

## Reading the disassembly

`sst.py` prints some operands as raw numbers that are really variable ids or packed values:

- **Variable ids in decimal.** The first operand of `set`, `add`, `sub`, `var_op28/29/2A` and the
  destination of `node_eval`, `query` and `lookup3e` is a variable id printed in decimal: `set
  22 = 1` sets variable 0x16 (the fire state), `set 28` is 0x1C (hit_react), `set 18` is 0x12,
  `node_eval.7 v1E 20` writes v14. In `if`, variables print in hex (`v15` is 0x15).
- **`random_chance.XXVV W`** (op 0x1E, `0x8005959C`): VV is the variable, XX a percent chance
  (0: always), W packs `max << 16 | min`. With the chance passed, the variable gets
  `min + rand % (max - min + 1)`; otherwise -1. `random_chance.E 6553600` is `roll = 0..100`
  (101 values, so "roll < 25" is 25/101). Below these print as `roll = rand 0..100`.
- **`query.HHFF A B C`** (op 0x2E, `0x8005A574`): FF the fact, A the destination, and with mask
  0x08 (every soldier query) the last word is variable 0x1F, the target: `query.FF09 0 -1 31`
  is "v0 = distance to the entity in v1F". With mask 0, operand B is an entity type and HH an
  index (`0x8006D600`: the first entity of that type and `+0x35`): `query.8 0 5107 0` finds a
  live entity of type 0x13F3. Facts:

| Fact | Result |
|---|---|
| 1, 3..6, 8, 0xD | The target's `+0xF4` (health), `+0x2D4` (eye height), `+0x290`, `+0x44`, `+0x48`; the target itself; the soldier itself |
| 9, 0xA | Distance to the target; to operand B |
| 0xB | **Sees the target**: within the brain's sight range (`+0xB0`, 1500), within the field of view (brain `+0xB4`), no blocking entity, and a clear world sweep from the eye to the target's eye (halved when the target's flags B 0x800 is set) (`0x8005A0C8`); ranges ×0.75 while the player's weapon-block bit 1 is set |
| 0x10 | The same with the second range (`+0xB2`, 3000) |
| 0xC | \|angle\| from the soldier's heading to the target (4096 per turn) |
| 0xF | `0x80056F60(self, player)`: a test of the player's gun line against a radius (`+0x2E8`, or brain `+0xC6`): **the player aims at it** (inferred) |
| 0x11 | **Hearing**: 1 if a sound is playing that the soldier can hear (below) |

- **`lookup3e.VVDD R`** (op 0x3E → `0x800579F4`), a probe: VV the destination, DD a direction
  in the soldier's frame (1: -x, 2: +x, 4: +z, 8: -z; from the moves that follow 4 is forward, 8
  back, inferred); R + 1 if a sweep of length R hits nothing, else 0 (also 0 over a drop of more
  than 400 with flags B 0x200000). Scripts test `v < R`: "blocked".
- **`compute.0 0 R v1E MASK -1`** (op 0x33 → `0x80040F48`): the nearest **node** within R with a
  kind (`+0xC`) bit in MASK, not v1E, enabled, nearer the soldier than the player, the player
  within 10000 and in front of it (a dot product); index → v0, -1 if none.
- **`node_eval.K N V`** (op 0x34, `0x8005B32C`): K 1..5 node word `+0x0..+0x10` (4 kind, 5
  approach side), 6 its holder (`+0x30`), 7 the cover score: 0x7FFF taken; 0x7FFE the player
  within 600 of it; 0x7FFD the player beyond 3500 (7000 for kind 0x10), nearer the node than the
  soldier, or 90° or more off its facing; else that angle (0..1023).
- **Events** (op 0x0D/0x0E/0x42, matched each tick by `script_run_b`, `0x8003FBFC`): the mask
  says which entity word must have all the bits: printed `on s38&BITS` for `+0x38`, `s3C`, `s40`.
  The stored instruction runs in the registering channel (a stored `goto` moves its program
  counter, a stored `nop` deletes the record). In `+0x38`: **0x40 hit from in front, 0x20 hit
  from behind** (`damage_from_hit`, `0x8004C324`, by the sign of the soldier's forward vector ·
  (attacker − hit point), `0x8004C108`); 0x400000 arrived, 0x800000/0x1000000 path ends (the
  brain). Bits 0x20..0x100 are cleared each tick while the `+0x48` invulnerability timer runs.

## Channels and entry points

**Channel 0 runs the soldier's mind; channel 1 its current action.** `other_script N` (op 0x08,
`0x800589E8`) and `jump_script` (0x1D) restart **the channel they run in** (`script_start(e, N,
flag, 0x80099EB8)`, the current channel set by `script_step`), not the other one as enemies.md's
table says: the state machine is a chain of `other_script` on channel 0. `start_script` (0x3B)
starts a script on channel 1 unless it already runs, `clear_script` stops channel 1,
`current_script` reads it (-1 none); channel 0 polls it to wait for strafes and throws. When
channel 0 runs off its end, `script_run_a` takes the first script with weight >
`rand × 100 / 32767`: only **202** has one (100), so channel 0 restarts as the guard idle.
Channel 1 just stops. The start script (`model+0x94`) is **200** in 1:3, **294** in 2:1 (dumps).

**Spawn (200).** Registers: hit → 1 (front) / 2 (behind); `s40` 0x10 → 209 (alert), 0x800 →
203, 0x100000 → 76 (path); `s3C&0x8000` → 176 (go home). Sets fire 0, v1D = v1E = -1 (no
node), **v1F = the player** (`query8` type 1, 200:43), v20 = itself, bit 9. Then: an alarm up
(`alarm > -1`; `g_alarm` holds -1 when none in every dump, not 0) → 195; path v22 > 200 → 78;
v22 > 0 → 77; else end → 202.

### Variables

| Var | Use |
|---|---|
| 0x05/06/07 | "hear" range, half, third: 3000/1500/1000 in 1:3 (2250 for some 2:1 soldiers); the scripts' distance thresholds |
| 0x08 | Skill: 33/67/100 (record `+0x5B`) |
| 0x0B | Poll period in ticks (1..4 in the dumps): `wait v0B` between decisions |
| 0x0F | Weapon kind: 0 SMG, 1 rifle, 2 MG, 3 Panzerfaust (enemies.md) |
| 0x10, 0x22 | Path mode and path id (records `+0x5E`, `+0x6E`); v22's range also picks the walk style |
| 0x14, 0x15 | Sight results; also a node's score; v15 the brain's remaining distance to a node |
| 0x16 | Fire state |
| 0x1A, 0x1B | Words for the hit hook `0x800523E4`: v1A 4 plus bits during grenade throws, 1..3 plus 0x80000000 and v1B a part during deaths (meaning: Open) |
| 0x1C | hit_react: 1 hurt (health ≤ 75%), 2 healthy, 4, 8 (down/dying), 0 |
| 0x1D, 0x1E, 0x1F, 0x20 | Previous and current node (-1 none); the target (the player); itself |
| 0x24 | Dodge chance in 1/1001 per decision: 10, 20, 1000 or 100 (record `+0x5C`; 10/20 in the dumps) |
| 0x25 | Fire decision (script 16): 1 with probability v26/101 |
| 0x26 | Fire chance in percent (record `+0x65`): 20, 30 or 50 in the dumps |

Flag words at `+0xEC` (`bitN`): bits 1, 2, 3, 7 are only ever read by the soldier scripts, so they
come from outside (the level?). Their effects: **bit 1** allows grenade throws (35, 111),
**bit 2** makes a stationary soldier (skips moving and dodging, 25:6; only takes kind-0x10
nodes, 91:7; does not react to grenades, 43:0), **bit 3** forces the crouch-and-hide answer to
being aimed at (25:219). Bit 4: no other soldier within 3000 (44:0). Bit 10: path soldier in
combat (78). Bit 20: already alerted once (209:31). Bit 9: alive (set at 200:51, cleared by
every death). Bits 15/16/17 pick a voice group for script 74 (shouts, death cries, alerts; the
sound by mission and level).

## Idle, patrol, detection

**Idle (202):** registers `s40&0x100` → 209, alarm (`s3C&0x100`) → 195, grenade events (43),
`s3C&0x10000` → 201 (walk to the point `0x8009817C`, then 209); stops; starts 211 on channel 1
(idle fidgets: anim 0 56%, 1/207/208 20%, 209/210/211 20%, 212 5%); then every v0B ticks
calls **detection (44)** and on 1 goes to 209.

**Patrols:** 77 (v22 1..200) and 78 (v22 > 200, which also sets bit 10 and back-and-forth mode)
start 87: brain path goal (v22, mode v10), walk at 1.0; path ids 100..200 walk anim 7 (with
footsteps), others anim 6/255 with a 5% look-around (253/254) each cycle; at a path end (89) stop,
wait 10..30 ticks, look (anim 304), walk on. Detection every v0B ticks → 209. Stationary
soldiers (bit 2) stand in 21/22: random waits of 30..60 ticks and idle anims, with a 5% chance
per cycle of 2..6 blind bursts (fire 3, anim 27); 44 every 2 ticks → 19; after 12 cycles → 202.

**Detection (44)** returns v4 = 1 when the soldier hears a sound (query 0x11, 44:16), or when the player's flags B
0x400 is clear and either the soldier sees the player within 1500 (query 0xB, 44:30) or the
player is within 1000 in any direction (v7, 44:39). No skill roll.

**Hearing** (query 0x11, `0x8005A574` around `0x8005A9C4`) goes through the sound system's voice
table (`g_voices`, `0x8009115C`: the 24 SPU voices, 0x34 bytes each; audio.md). A soldier hears a
voice when it is enabled, a one-shot (`+0xE` = 0), started less than **60 ticks (2 s)** ago,
positioned in the world (`+0x0` bits 0..1 clear), not made by the soldier itself (the voice's
source handle at `+0x4`), and within **brain `+0xB0`** of it (1500 in the dumps; 3000 without a
brain), measured from the sound's position (`+0x2C`) with `dist_approx`. Any such sound counts,
the player's shots and other soldiers' alike, so gunfire draws every soldier within range for two
seconds and alerts spread from soldier to soldier. Checked by reading the code; the voice fields
are from the sound functions that fill them (`0x80041CE8`, `0x80041FC4`, `0x80042250`) and the
per-tick update `0x8004137C` that ages `+0x28`.

**Alert (209):** stop. Bit 2 → 17 (bit 20 set: 210). Bit 20 already set → 210. Else set bit 20
and, with the player within 3000: anim 215, then 10% each one of three shouts (sounds 0xD, 0xE,
0xF); then 40% straight on, 30% anim 205 once or twice, 30% `send_event 0x1B` and 40% anim
204; `send_event 0x1B`; without a shout 61/101 a bark from group 17; → **210**. With the player
beyond 3000 (209:140): weapon-ready anim (216), a 30% shout, a pause of 5..20 ticks, look-around
or sidesteps, then a **skill roll** (script 45, 209:270): fail → back to idle 202, pass → the
near branch (209:98).

## Engaging (210, 19)

**210 picks the range band** (v0 = distance; "near" < 1500, "far" > 3000):

| Weapon | > 3000 | 1500..3000 | < 1500 |
|---|---|---|---|
| SMG | 180: walk at it (seek + walk, anim 7 cycles; fire 1 on 40% of cycles, 187) until < 3000 | path soldier → 19; 50% 178 (walk at it firing as 187, until < 1500 → 19); else 35% 148 (crouch), 65% 27 | 19 |
| Rifle | 179: walk at it without firing (188) until < 3000 → 210 | 35% 148, 65% 27 | 19 |
| MG | 179 | path soldier → 19; 50% 181 (walk without firing until < 1500); else 19 | 19 |
| Panzerfaust | 19 | 19 | 19 |

Before that 210 sends `send_event 0x1B`, sends a bit-2 soldier to 19, calls 91 (a cover
search within 1000, below), and a bit-1 soldier at 1500 or more
throws a grenade with 5/100 chance (roll 1..100 < 6, 210:39) → 35.

**19, engage:** sets v1A (op 0x29 on v1A: Open), calls 196 (empty in 1:3); a rifleman within
1000 → 136 (close quarters); else starts the **fire loop 40** on channel 1 and runs the **combat
loop 25** on channel 0.

### The fire loops (40, 41)

Channel 1, restarted after every strafe. `on s3C&0x1` → call 39 then resume (39: a sound and a
weapon-specific anim: a reload, inferred). Per weapon (40; 41, used from 28, is the same with
small differences):

- **SMG:** fire 0; roll the fire decision (16: `roll < v26`, so 20..50 out of 101); yes → fire 1
  for 1..4 plays of anim 27; no → anim 26 at fire 0; repeat (40:31..57). Fire 1 bursts only
  while the player is within ±30.8° of the heading (enemies.md), and the seek goal turns the
  soldier toward v1F (40:13).
- **Rifle:** anim 20, wait 6 (8 in 41); then each tick roll the decision; yes → sound 4, **fire 2**
  (one aimed shot), anim 20, fire 0, sound 5, anim 21 (the bolt), and again (40:63..93).
- **MG:** anim 247 (3 frames), wait 6; decision yes → fire 1 during anim 248, else wait 6 again.
- **Panzerfaust:** no loop in 40 (40:6 jumps past the end). In 41: anim 271 2..6 times; fire 2
  (aimed rocket, sound 9), or **fire 3** (straight ahead) when the player's flags B 0x400 is
  set; v9 = 9991 while anim 272 plays; wait 10; anim 273 (reload); again.

### The combat loop (25)

Each pass: grenade events (43), hit events by weapon (24: SMG → 1/2, MG → 29/30, rifle →
32/33), then:

1. **Crouched player (18):** player flags B 0x800 set (the player crouches: the sight test halves
   the eye height for it, inferred), the soldier at most 100 "above" (sensor 0x6F) and the
   player within ±600 of its heading → 148 (kneel and fight, below).
2. `wait v0B`.
3. Not bit 2, not a Panzerfaust: **alone** (bit 4, no living soldier within 3000): once, 3% →
   215 (shout, then a fixed run of strafe-fire moves), else 5% → 208 (shout, weapon-ready, back
   to 19) (25:16..60).
4. **Bazooka:** the player holds weapon 2: the first time 50% → 214 (shout; beyond 1500 kneel
   → 151; else 30% and 25% a strafe either way at fire 1, 25% back off firing (anim 15 3..5
   times), 20% flee for 2..4 run cycles; → 19 or 151); every pass after a first 214 → 214 again;
   after a first "no" never (bits 18/19, 25:61..91).
5. **Lost sight** (query 0x10 at 3000 fails, 25:93): stationary → 20; path soldier → 79; sensor
   0x6F < 300 → **175 (search)**; else 184 (back off: melee if within 350 at 70%, flee while
   facing, 182).
6. v0 = distance. Bit 1 and v0 ≥ 1500: 5% → **35 (throw a grenade)**.
7. **Close (< 1000):** Panzerfaust → 184 (too close); bit 3 → step 9; else a **failed skill
   roll** gives a 2/1001 chance per pass of 207 (shout, then 50% strafe-fire 2..4 steps, cover
   search; → 19); then the dodge roll of step 9 **without the aimed-at test** and with roll =
   rand 0..100 (25:197), so all six moves of 135 are possible.
8. **Far (1000..3000):** MG → step 9; beyond 1500 → **27/28 (long range loop)**.
9. **Dodge:** only while **the player aims at it** (query 0xF = 1; 25:210): roll = rand 0..64
   (SMG) or 0..66; v0 = rand 0..1000; v0 ≤ v24 → stop the fire loop, **135 (dodge)**, re-register
   hits, restart 40. So a dodge happens on (v24 + 1)/1001 of the passes while aimed at: 1.1% or
   2.1% per pass with the dumps' values, i.e. about every 1.6..6 s at v0B 1..4. Bit-3 soldiers
   instead: 50% the dodge roll, 50% crouch (anim 238, anim 239 2..6 times, 55% anim 303, cover
   search 3000) while still aimed at.
10. Otherwise **cover search** (91, within 1000) and loop.

### Dodges (135; moves 139..147)

135 turns to the player, stops walking, and picks by the roll the caller left (0..64 SMG,
0..66 rifle/MG at mid range, 0..100 close) and the distance. Each move first probes 175 units
its way; blocked, it tries the opposite move; both blocked, nothing.

| SMG roll | ≤ 1500 | > 1500 |
|---|---|---|
| 0..15 / 16..31 | Strafe 139 (anim 13, probe 1) / 140 (anim 12, probe 2) | The same |
| 32..47 / 48..63 | Strafe-fire 143 (anim 11 twice) / 144 (anim 10 twice) at **fire 4**, only with the player within ±90° (`queryC` ≤ 1024) | The same |
| 64..79 | Step 142 (anim 8, probe 4) | Step 141 (anim 9, probe 8) |
| 80..95 / 96..100 | Step 141 | Step 142 / script 148 (kneel) |

Rifle and MG: within 1000 the rifle goes to 136; else 0..32 a step 146, 33..65 147 (anims 23/24,
probes 2/1), the rest 145 (anim 22, probe 8); far, 75 and up: the MG 146, else beyond 1500 script
148. The MG's moves use anims 250/252/251 at fire 1.

Each move runs on channel 1 while 135 waits for it; before the step it calls 134: SMG/MG roll the
fire decision for fire 1, the rifle a fire 2 shot if not already firing. `s3C&0x2` aborts a move
(meaning: Open); `s3C&0x1` reloads (39). Then fire 0 and back to 25.

### Long range (27, 28), close quarters (136, 184), kneeling (148..156)

28 is 25's loop for 1500..3000 with fire loop 41: a rifleman first kneels with 50% (157 → 160:
v26 → aim 10..30 ticks, fire 2, bolt), a Panzerfaust kneels (158/159/164: aim 10..20, fire 2
or 3, reload; needs 850 clear ahead); within 1500 → 19, unseen → 175 (Panzerfaust 70%).

Rifle within 1000 (136): 1..3 back steps (anim 15) if clear; within 350 a **rifle-butt** (anim
265 with `hit_part.15 8 5 150 0`, op 0x0C's melee part); else 80% one fire 2 shot, 20% a ready
pose; cover search without limit (93) → 19. 184 (Panzerfaust close, or the player well above):
within 350 a 70% butt strike, then back off facing for 100 ticks and flee (182) until > 1500.

148 kneels (anim 59) and shuffles (154/155) or holds (151/156: v26 → fire 1 during anim 60, the
rifle fire 2 every 15..45 ticks); the player standing within 1500 → 149 (stand, step back, 19);
beyond 3000 or unseen → 150 → 175.

## Cover (90..96, 110, 111)

**Search** (91/92/93: within 1000/3000/unlimited; Panzerfaust never): `compute` with mask 0x11
(0x10 for bit 2). **Accept** (90): kind 0x10 if the score ≤ 1200 (i.e. any valid angle); other
kinds if the score ≤ 300 (the player within ±26° of the node's facing) and the player 500..6000
away. Then v1D = old node, v1E = new, → 94.

**Moving (94)**, every tick: score and kind of v1E; kind 0x10: invalid → 210, player < 300 → 17
(stand and engage), > 7000 → 175; other kinds: score > 300 → 175, player < 500 → 17, > 6000 → 175;
more than 3000 left → 175; arrived (v15 = 0 or `s38&0x400000`) → 95. Channel 1 (110): route,
a first step (75: anim 2/4/3 from the route's v2, inferred left/right/back), brain node goal
(v1E, distance in v15), walk 1.0, claim the node (`node_write.6 v1E v20`), run cycle anim 14
with footsteps. 110 rolls the fire decision but sets fire 1 and fire 0 back to back with no
yield (110:41..43): **soldiers never fire while running to cover**.

**At the node (95, 96):** kind 0x10 → 113 (empty: the soldier just stands); approach side 4 →
112 (empty); else **111, peeking**:

1. Stop, claim, `node_take` (move to and face it), anim 82, **hide 15..50 ticks**.
2. Pick the side from the approach side (1: one side, anims 83/84/86; 2: the other, 87/88/90;
   3: 50/50).
3. **1..4 peeks:** lean out; a bit-1 soldier throws from cover with 20% per peek (v12 = 3/2 →
   98/100: anim 263/264 with the grenade hook bits, back to 95); else SMG/MG **fire 4** during
   four plays of the lean anim (84/88), rifle one fire 2 shot; fire 0, back in, wait 3.
4. Back to 1.

96 (channel 0) holds the node: grenades → 114/116/117 after leaving it; leave (136 → 210) when
the score goes above 300, someone else holds it, the player is nearer than 500 or beyond 6000.
At a **kind 0x10 node** it looks for a mounted gun (type 0x1772, `query8`), leaves without one or
with the player within 300, and sets the gun's `+0x3C` 0x200 while the player is within 969 of
the node's facing angle, 0x2000 beyond (the gunner working the gun, inferred).

Hit at cover (102/108/109): death anims 91/202 below 10%, else a flinch and back to 95; the
third hit at 108 → 210.

## Search (175, 204..206)

175 (unseen): path soldier → 79, stationary → 20; walks **straight at the player's current
position** (188: seek v1F + walk, anim 7, counting cycles; the soldier always knows where the
player is). Seen again (query 0x10) → wait 4 → 210. Within 1500: 20% → 205 (stop and watch with
206 until seen → 209) else keep walking; each pass a cover search within 3000. After 13 walk
cycles → 204: stand in place with 206.

206, the **search stance**: random look-arounds (anims 222/163, 167, 166, 260 with 20..30-tick
holds, 30% a bark), sidesteps where clear, and **blind suppressing bursts** when 1600 ahead is
clear: SMG 2..4 volleys of 5..15 × anim 27 at **fire 3**, rifle 1..4 shots at fire 3, MG 2..4
anim 248 at fire 3. 204/205 run detection meanwhile; the player seen → 209.

## Grenades

**Throwing** (bit-1 soldiers, from 25, 28, 210 and cover peeks): 35 routes, takes a first step,
faces the player, anim 261; sets v1A = 4 with op 0x29 bit 0x4000000; anim 261 to the end, then
37 on channel 1: anim 219, **anim 220** with bit 0x2000000 at frame 21, then 0x8000000; waits 20
ticks, then for 37 to end; then the fire loop 40, wait 50, → 209. The bits read as: take a
grenade, release, done; 0x20000000 (36) drop it; 0x1000000 the release from cover (98/100)
(inferred: the hook `0x800523E4` makes the grenade). Hit from the front while holding it
(35:69): dying, or in part 0, 2 or 6 → drop it (36) and 12 (fleeing it via 9 → 116); else flinch
(anim 31) and throw anyway. From behind (35:96) the health test is inverted (`> 10`): a healthy
soldier drops it, a dying one throws (kept as found).

**Reacting** (43, registered by most combat scripts unless bit 2): `s3C` bits set on the soldier
(by the grenade, inferred): 0x40 → **116 flee**: flee goal from the grenade (type 0x13F3) and walk
until it is gone or 2000 away → 210. 0x80 → **117 return it**: run at it; 50%: stop within 160,
anim 217 (a kick: grenade `+0x3C` |= 1 at frame 17); 50%: stop within 100, anim 218 (pick up: |=
2), turn to the player (118), anim 220 (throw: |= 4); hit meanwhile: |= 8 (dropped) and 12/13.
0x20 → **114**: run to it, within 550 attach to it (`follow`) and play anim 213 then 214 eleven
times (inferred: smothering it); a forward probe failing on the way → 116.

## Being hit

Hit events (front 0x40 / back 0x20) go to 1/2 (SMG; 29/30 MG, 32/33 rifle, the same apart from
the anims). All first stop everything (script 0: channel 1 cleared, fire 0, every goal off).

- **Health < 10%** (variable 0x0D): front → 189, back → 3 (death, below).
- Else **6**: pain (4: hit_react 2 above 75% health, else 1; 45% a grunt, sound 0 or 1). Hit by
  the player's bullet (type 0x13F6): above 75% one of four flinches (anims 30..33, 25% each);
  at 75% or below **by part** (5). Hit by anything else: anims 36..39 (25% each).
- **By part** (5): head (0) → 7 (anim 29 or a long head-clutch, anim 40); 1, 5 (arms) → 169
  (anim 41); 2, 6 → 8 (anim 42); 3, 4, 7 (torso) → 171: anim 65 (long), anim 33, or **knocked
  down** (anim 46 → 10); 8 (groin) → 173: 15/128 a long fall (anim 45 → 10), else anim 33;
  odd legs 9..15 → 167: 50% fall (anim 46 → 10), else anim 43; even legs 10..16 → 166 (anim 44).
- Then 165: route, first step, cover search 3000 and a dodge (14: both branches of its skill roll
  are identical, 14:18..27), → 19 within 1500, else 27.

Several part scripts roll into `roll` but test v0 (7:4, 128:123, 128:158, 132:26, 162:81,
171:13, 174:15), so the branch depends on whatever v0 last held (a distance, usually): a script
bug to keep for faithfulness.

**Down (10):** lies for 1..5 plays of anim 47 (hits: 162), then: with 500 clear behind 33% crawl
(96 → 130: anim 98 4..8 times), 33% **fire from the ground** (49/50: SMG/MG fire 1, rifle
fire 2; 50% each time to get up), 33% get up (anims 48, 51 → 19). Hits while down (128, 132,
162): dying → 129/133/163; else by part to the prone variants (126, 130).

## Dying (189..194, 120..125, 3)

189: hit_react 8; then by what killed it:

| Cause | Script | Anims |
|---|---|---|
| Player holds the shotgun (4) and is within 550 | 193 | 295 (thrown back) |
| Player holds pistol 0, 8 or silenced 9 | 194 | 52, 53, 54, 296 (25% each) |
| Player bullet (0x13F6), by part: head | 190 case 8 | 50% 52, 50% 57 |
| arms/torso 3..6 | case 9 | 53 (fire 3 during it, not riflemen) |
| odd / even legs | cases 10, 11 | 56 / 54 |
| other parts | case 7 | 30% 58, else 57 |
| Rocket (0x13F1) or 0x13F3 | 191 | By part (head 57/295, torso 55, legs 70/294); parts 1, 2, 7, 8: 10%/20% tumbles (299, 300/301), 50% thrown (120), 20% anim 55 |
| Explosion (0x31) | 120/123 | Thrown (`fall.3` impulse), anim 73/77 until `s3C&0x2`, then 121/122/124/125 (landing) |
| Attacker type 1 | case 1 | 57 |
| Else, by roll (16% steps) | cases 1..6 | 57, 57, 53 (fire 3), 54, 56, else 30% 297 / 70% 55 |

From behind (3): the player's bazooka → 192; rocket/0x13F3 50% → 191 else 123; explosion → 123;
else anim 52. **Death spray:** in cases 3 and 9 and two leg cases of 191 an SMG/MG soldier sets
fire 3 for the death animation (frames 0..97 of anim 53): bursts along its facing as it falls (a
Panzerfaust soldier from frame 21 unless killed by an explosive; riflemen never).
Death cries: bit 16 + script 74. Every death ends in **15, the corpse**: events and goals off,
wait 15, 19 ticks of ops 0x1C/0x41 (a blink, inferred), health_pct = -1, wait 60, removed: about
3 s after the death animation.

## Randomness and skill

| Roll | Probability | Where |
|---|---|---|
| Fire decision | v26/101 per decision (20..50%) | 16, every fire loop |
| Dodge when aimed at | (v24 + 1)/1001 per pass | 25:291, 28:129 |
| Grenade (bit 1) | 5/100 per pass at ≥ 1500 | 25:139, 28:33, 210:37 |
| Alone reaction | 3% / 5%, once | 25:39, 25:54 |
| Bazooka reaction | 50% once | 25:84 |
| Throw from cover | 20% per peek (bit 1) | 111:89 |

**Skill** (variable 8) is used by 1:3's soldier only through script 45: v4 = 1 with
probability skill/101. It decides (a) whether a soldier alerted from beyond 3000 engages or goes
back to idle (209:270), and (b) that a soldier failing it gets the rare 207 reposition at close
range (25:183). Its use in 14 has no effect. Aim error, health and fire chance come from other
record fields (enemies.md).

## Timings

30 ticks per second. Decisions every v0B ticks (1..4); rifle shot attempts 6..8 ticks apart
after the bolt; kneeling rifle 10..30 ticks of aim, Panzerfaust 10..20 + 10; search looks
20..30; cover 15..50 hidden; grenade throw 20 + 50 around the animation; corpse ~96. Burst and
move lengths are animation lengths (Open) within the weapon table's limits (enemies.md).

## 2:1's soldier

Scripts 0..194 are identical to 1:3's (169 byte for byte; the rest differ only in script
numbers). The stubs are filled and ~100 scripts are inserted: 1:3's 195/196/197 become 196/198/199,
198 → 207, 200 → 294 (start), 201..217 → 295..311 (202 → 296, still the only weighted one).

- **The alarm** (195..199). With no alarm up, an alerted soldier (199, from 303 = 1:3's 209) and
  one entering 19, 27 or 210 (198; skipped when v12 = 8045 and the player aims at it) looks for a
  free node of kind 8 within 1500 (`compute` mask 8), takes it as v1E and runs to it (201: route, node goal,
  walk, anim 14, one shout 0x1F). Each tick: an alarm up already → 196; the node taken → 175.
  At the node: `node_take`, then each tick 70% anim 283 and **`alarm`** (op 0x49, 195:73) → 303,
  30% anim 284 (a look) and re-check. Hit on the way → 197 (v12 = 8045: never tries again).
  **On an alarm** (`s3C&0x100` → 196 in every idle/patrol script): wait 3, 50% → 298 (stand and
  watch with blind bursts, 1:3's 204), 50% → 175 (search). In 1:3 the same event goes to the
  empty 195 (Open).
- **The sentry** (202..213, on `s40&0x400` from the level): detection → 203, the player aiming at
  it within 3000 → 204; both face the player and fire 1 during anim 241 after every 1..4 (+2
  with a skill pass) or 1..2 plays of anim 240, unless the player is within 565 and unseen;
  beyond 3000 → 202. Death: 207 (1:3's unreferenced 198): falls (155), writhes (156) until
  `s38&0x2`, dies.
- **Papers and suspicion** (214..293, entered on `s40` 0x200, 0x1000, 0x2000, 0x1000000,
  0x10000000 → 214..218: v11 a role 0..4, v25 a voice 1..6, vA a starting suspicion, 40 with
  skill > 40). The soldier stands or walks its path (220/221) and every tick classifies the
  player (261): seen with the papers (weapon 7) 1; seen with another weapon 2; seen with player
  `+0x11E` = 8, 3 or 4; a sound heard (query 0x11) 5. Class 2 with the player aiming at it blows
  the disguise with probability skill/200 per check, else vA rises by skill-shaped rolls; classes
  3..5 raise vA (5: blown at once for weapons 1, 2, 3, 6, 10). vA picks the behaviour (262):
  < 10 idle, < 40 watch (235), < 70 approach and ask (242), < 90 inspect close (243), ≥ 90 →
  246: shout, `send_event 4`, strafe, → 303 (combat). An inspection (228) waits for level
  messages `s40` 1/2/4/8 (papers verdicts, inferred): 4 accepted, 8 refused, 1 accepted below
  skill 40, 2 below 80.

Health is 4.0 in 2:1 (6.0 in 1:3) and some soldiers have range 2250; the behaviour scripts
otherwise match.

## The player's concealment flags

Two of the player's flags B bits that the soldiers test come from **level volumes**, through the
player's model script and a level script:

- The player's body script 2 (its model's script list, started by `player_update`) registers
  events on its `+0x40` word: 1 → variable 0x8A = 1, 2 → 0x8A = 0, 4 → 0x8B = 1, 8 → 0x8B = 0.
  Variables 0x80 + n are bit n of flags B, so these set and clear **0x400** and **0x800**.
- Level script 13 (the same in every level dumped: 1:3, 2:3, 3:2) handles the player's messages
  and calls `set_flags` (built-in 3: `+0x40` |= value): message 8 → 1, 9 → 2; message 0xD counts
  up and sets 4, message 0xE counts down and sets 8 when the count is back to zero (it starts at
  128; message 0x9D resets it and sets 8).
- The messages come from kind-1 trigger boxes (class 0xB81C10E3, shape 2) whose player enter and
  leave lists hold 0xD/0xE or 8/9. 2:3 has nine 0xD/0xE boxes and one 8/9 box; a mission 1
  level (the replay at vblank 30000) two 0xD/0xE boxes; 1:3 and 3:2 none.

So **0x800 means "in a low zone"** (overlapping zones counted): the player can't stand up there
(`player_stand`, movement.md) and sight checks against the player use half the sweep (query
0xB), so these are crawl spaces under something low. **0x400 means "undetectable"**: soldiers' detection
(44) skips sight and proximity (hearing still counts) and Panzerfaust soldiers fire straight
ahead (fire 3) instead of aiming. Neither changed in a replay of the recorded session up to
vblank 96,000 (it never entered such a zone); found by reading the scripts (`lvs.py`,
`sst.py`) and the records' lists (`+0x38`/`+0x3C`, u32 count then u32 messages, read by
`0x80083DE8`). What the zones are in the world (shadows, foliage) is not checked.

The player's `+0x11E` (sensor 0x66) is its **body script number** (channel 0's current script,
set by `script_start`; -1 when none). In 1:3 the player's model has 17 scripts; 13 is the jump
(it sets variable 0x13, the vertical velocity, to 400 then 200 over its frames). The replay
showed 0, 7, 13, 8, 10, 9, 16, 12, 1, 15 and 14.

## Open

- Which of the player's actions its body scripts 3, 4 and 8 are in 2:1 (`+0x11E`, above).
- Bits 1, 2, 3, 7 of `+0xEC`: who sets them (the level?), and v22 > 200 paths.
- `s3C` 0x1 (reload?), 0x2 (abort; explosions' landing), 0x8000, 0x10000; `s40` bits from level
  messages; `send_event` 0x1B, 0x4, 0x1E, 0x33, 0x75.
- v1A/v1B and op 0x29 (the hit hook `0x800523E4`), hit_react's effect (`0x80052084`), ops
  0x1C/0x41 on corpses, `fall`/`accel`'s operands, the behaviour goal bit 0x2000.
- `route` (0x37/0x3A) results in v2/v3 and v23 (165:8).
- 1:3's alarm event goes to an empty script: does the soldier then loop 195 → 202 while the
  bit stays set?
- Animation lengths, which bound burst lengths and every "anim" timing above.
