# Audio: sound banks, voices, 3D sound, music

The game drives the SPU itself: libsnd is only initialised (`SsInit`); every effect is keyed by
the game's own small driver (`0x80041260..0x80042DF8`) through libspu calls, and the music is a
raw ADPCM file streamed from the CD into a ring buffer in SPU RAM, played on voice 0. There is no
XA, no CD-DA and no sequenced (SEQ/SEP) music in the levels.

Read from the code (`fn.py show`/`c`), checked against the disc files with
`tools/recon/snd_taf.py` and `tools/recon/snd_vb.py`, against RAM dumps of 1:3, 2:3 and 3:2
(`build/re/mid_*.bin`, `build/wfix/*/ram.bin`), and by watching the voice table while replaying
the 1:3 session (`VALOR_WATCH` on `g_voices`, "Checked at run time" below). Inferences are marked.

The file side follows [Medal-Of-Honor-PSX-File-Viewer](https://github.com/AdrianoDiDio/Medal-Of-Honor-PSX-File-Viewer)
(GPL-3.0; `src/SoundExplorer/VAB.c`, `src/Common/Sound.c`, `docs/README.md`, "TAF Files"): it
reads the VABs out of the TAF files and plays the VB files as 22050 Hz mono ADPCM. What the game
does with them is below.

## Files

| File | Contents | Used as |
|---|---|---|
| `DATA\MSNm\LVLl\m_l0.TAF`, `m_l1.TAF` | TIM images, then a VAB table: u32 count, then `count` u32 offsets (from the table) to VABs | The level's **sound banks**. `0` German, `1` American (the viewer's reading): the digit is `0x8009A618` (levels.md). Only the soldiers' voice bank differs between the two |
| `DATA\MSNm\LVLl\Mm_l.VB` | Headerless SPU ADPCM, 16-byte blocks, mono, 22050 Hz, 83..441 s | The level's **music**, streamed |
| `DATA\MSNm\LVLl\Mm_lA.VB` | The same length (±0.3%), same first and last seconds | The **ambience-only** mix: streamed instead of the plain file when `g_music_alt` (`0x80038658` = `g_game+0x8C`) is set, i.e. the shell's audio option Background = "ambience only" (below) |
| `MOVIE\*.STR` | MDEC video with interleaved XA audio | Played by the movie player through the CD mixer (`SpuSetCommonCDMix(1)`, CD volume 0x7FFF at init); not part of the level audio |

MSN12 (the two-player arenas, shell.md) has a single bank and only 12:5 has a VB.

### The VAB banks

Every VAB in the TAFs is a standard PsyQ VAB ("pBAV") **with no programs and no tones**
(programs = tones = 0 in every bank of every level: `snd_taf.py` over all TAFs), so the header is
0x20 + 0x800 (the empty program table) + 0x200 (the VAG size table) = 0xA20 bytes, then the body:
the VAGs one after another. The VAG size table is 256 u16: entry 0 is 0, entry n+1 is the size of
VAG n in 8-byte units. A **sound id** is the 0-based VAG index in its bank.

Banks are loaded in TAF order into bank slots 0, 1, 2... (`taf_load_banks`, `0x80086A94`, from
`level_load`), at most 8. What the slots hold (counts of VAGs, 1:3 / all levels):

| Bank | VAGs | Holds | How it's addressed |
|---|---|---|---|
| 0 | 80..83 (56 in MSN12) | The level's effects: impacts, explosions, footsteps, casings, pickups, UI, alarm, objective jingles, weapon draw/holster. The same id means the same thing in every level (70 of 1:3's 80 samples are byte-identical in 1:1; the ids that change per level are the footstep 0x0F, 0x14..0x16, 0x24..0x2A, 0x33..0x36 and the level's extras past 0x3F) | Constant bank 0 in the code |
| 1 | 12 | Weapons: SMG, pistol, rifle, MG fire and handling, grenade, bazooka (table below) | Constant 1, or a weapon table's bank field |
| 2, 3 (, 4) | 2 each | One bank per player weapon model of the level: sound 0 fire, 1 reload | The held weapon model's `+0xC8` (bank -1 in the weapon tables) |
| The 37- or 81-sound one (slot 3, 4 or 5) | 37 (81 in 2:1, 2:2, 3:1) | The soldiers' voices: shouts, pain, death. 11025 Hz recordings (played at pitch 0.5, below). The only bank that differs between the `0` and `1` TAFs (37-sound banks only; the 81-sound disguise-level banks are the same in both) | The soldier models' `+0xC8` (bank -1 in their scripts) |
| 5 in 1:3 | 8 | The other character's (type 0x1405, levels.md) | Its model's `+0xC8` |

The bank numbers come from the models (`+0xC8`, u16, set in the level data), read from the RAM
dumps: in 1:3 the soldier models (0x1402) and the camera say 4, the Garand model 2, the other
player weapon model 3, the 0x1405 model 5; in 2:3 soldiers 4; in 3:2 soldiers 3, matching each
level's voice bank.

### Loading a bank (`bank_load`, `0x800418E4`)

`bank_load(vab, slot)`: checks the magic and that the body fits below SPU address 0x80000
(otherwise error `0x80089B04(5, 0x10)` and nothing loads), uploads the body to the SPU at the
allocator `g_spu_alloc` (`0x800A0130`), copies the 0x200-byte size table to
`0x8009263C + slot·0x200` (pointer at `0x80091108[slot]`), records the body's SPU address at
`0x800910E8[slot]` and the slot as loaded (`0x80091128[slot]` = 1), and advances the allocator.
It also fills a base pitch per sound (`0x8009163C`, 256 rows of 8 slots, u16): the pitch of the
first tone naming that VAG, from a note table at `0x80090FD0` (centre note + 4, capped at 0x1000);
with no tones every base pitch is **0x800**, i.e. 22050 Hz (0x1000 = 44100 Hz).

SPU RAM (`sound_init` `0x80041260` and `music_ring_init` `0x8008A6A8`; `sound_reset`
`0x8004131C` before each level puts the allocator back to 0xB010):

| SPU address | |
|---|---|
| 0x00000..0x0100F | Reserved (capture buffers) |
| 0x01010..0x0B010 | Music ring: two halves of 0x5000 bytes |
| 0x0B010.. | Bank 0, then bank 1, ... packed (1:3: bank 0 at 0xB010, bank 5 ends at 0x7BA20; `snd_taf.py` prints each bank's range) |

The game never sets reverb (no game code calls `SpuSetReverb*`/`SsUtSetReverb*`), so effects and
music are dry. Master volume 0x3FFF both sides.

## Voices

24 SPU voices. **Voice 0 is the music**; voices 1..23 play effects. Their state is `g_voices`
(`0x8009115C`, 24 × 0x34 bytes), with a status byte per voice in `g_voice_enabled`
(`0x800910D0`) refreshed every frame from `SpuGetAllKeysStatus` (0 off, 1 on, 3 keyed off but the
release still sounding). **`voice_start` itself sets the status to 1 at once** (before the
key-on is even sent), so a sound started during a tick is "playing" for every later think of
that tick: hearing (query 0x11) and `+0x68` see it immediately. A one-shot reads 1 while it
plays, 3 for one update once it ends, then 0 (the voice free again from 3).

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | u32 | Flags: bit 0 not positional (no entity, or the entity's flags A 0x40000000), bit 1 not heard by enemies |
| +0x04 | u32 | Handle of the entity the sound follows (0: none) |
| +0x08 | s32 | Sound id |
| +0x0C | s16 | Bank |
| +0x0E | u8 | Loop: restarted when it ends |
| +0x0F | u8 | Fade-in steps (always 0 in the code's calls) |
| +0x10 | u8 | Fade-in step count |
| +0x11 | u8 | Fade-out length (0xFF after a release) |
| +0x12 | u8 | Fade-out frames left |
| +0x13 | u8 | Distance class (0..3, the falloff below) |
| +0x14 | s32 | Volume: argument << 7, at most 0xFF0000 (so its high half, +0x16, is 0..255) |
| +0x18 | s32 | Volume argument as given (kept to restart loops) |
| +0x1C | s32 | Volume spread |
| +0x20 | s32 | Pitch (16.16, 0x10000 = the sample's own rate) |
| +0x24 | s32 | Pitch spread (16.16) |
| +0x28 | u16 | Age in frames |
| +0x2C | 3×s16 | Source position (copied from the entity's `+0x20E/+0x212/+0x216`) |

### Playing a sound (`sound_play`, `0x80041CE8`)

`sound_play(bank, sound, entity, volume, volume_spread, pitch, pitch_spread, fade_in, loop,
distance_class, flags)` → the voice, or -1.

1. Refused (-1) if the bank is ≥ 8 or not loaded, or the sound is negative.
2. **A free voice** (`voice_alloc`, `0x80041AA0`): the first of voices 1..23 that isn't looping
   and whose status is 0 or 3. None free: the sound is **dropped** (-1). There is no priority and
   no stealing; one-shots free their voice by ending, or after 351 frames (below).
3. The entry is filled (table above). Volume: `v = min(volume << 7, 0xFF0000)`; with a volume
   spread k, `v += v·k − rand() % (2·v·k)`. Volume 0x10000 is "half" (high half 128),
   0x20000 and more full (255).
4. Positional (an entity without flags A 0x40000000): the position is taken from the entity and
   the left/right volumes from `sound_pan` (below); if both come out 0 they become 1 (the voice
   still plays, inaudibly). Not positional: position 0, flag bit 0, and both sides
   `((v >> 10) · S) >> 8`, S being the effects volume setting (`g_params+0xA`, below).
5. **Key-on** (`voice_start`, `0x80041B04`): skipped for samples shorter than 13 blocks of 8
   bytes (empty slots). Volume L/R; pitch `p = base · pitch >> 16`, then with a spread
   `p += (p·s >> 16) − rand() % (p·s >> 15)` (uniform within ±p·s/65536); start address = the
   bank's address + the sizes of VAGs 0..sound−1; ADSR (AR 0, DR 0, SR 0, RR 3, SL 15, attack and
   sustain linear, release exponential: an instant attack, held at full, a short release); the
   key-on bit is queued in `0x80090FC8` and sent by the next `sound_update`.

Other entry points: `sound_play_voice0` (`0x80041FC4`, the same on voice 0, used once: 9:4's
level start plays bank 0 sound 0x51 there); `sound_restart` (`0x80042250`, the same on a given
voice, for loops); `voice_stop` (`0x800425C4`: volume 0, pitch 0, key off, entry cleared);
`voice_release` (`0x80042664`: key off at the next update); `sound_fade_entity` (`0x8004249C`,
`(entity, frames)`: every voice following that entity fades out over that many frames, used
with 10 when the player draws a weapon, cutting the reload sound); `sound_stop_all`
(`0x800412E0`).

### Every frame (`sound_update`, `0x8004137C`)

Called once per drawn frame from `game_frame` after the draw (not once per tick: with several
ticks per frame it still runs once), and from the level loop while loading.

1. Read the key status of all voices (`SpuGetAllKeysStatus`, `0x80015A9C`, into
   `0x800910D0`: 0 off, 1 on, 3 keyed off and releasing); send the queued key-ons. When the SPU
   finishes a sample is real time (sample length, pitch, the release envelope, and a voice can
   end and be retaken within one frame, so the status bytes alone don't show every end): an
   engine that must reuse voices exactly as the original takes these statuses from a recording
   (the runtime logs them under `VALOR_TRACE_INPUT`: `keys TICK: 24 bytes`). Scripts read them
   through variable 0x68 (my last sound still playing), so a soldier's pacing follows them (2:2
   tick 138).
2. For each voice 1..23 that is on:
   - age += 1; at **351 frames** a non-looping voice is stopped (`voice_stop`), whatever it is.
   - **Follow the entity**: while its entity lives (not flags A 0x80000) the source position is
     copied from it every frame; once it's gone the handle is dropped and the position stays.
   - **Fading out** (+0x12 > 0): both volumes `−= vol >> (n/7 + 3)` (n = frames left), n −= 1,
     released at 0.
   - **Positional voices are re-panned every 4th frame** (a counter `0x80090FCC` cycling 0..3,
     on 3): `sound_pan` again with the current listener, written to the SPU. Non-positional
     voices keep their volume.
3. For each voice that ended: looping → `sound_restart` with the stored arguments (the loop is
   a re-trigger, not an ADPCM loop point); otherwise the entry is cleared.
4. Voices keyed off by fades are released together (`SpuSetKey(0, mask)`).

### 3D: distance and panning (`sound_pan`, `0x80042AA4`)

The **listener** is the camera (`g_player`, `0x8009D638`): its view object (`+0x388`) holds the
attached entity's handle at `+0xC` (the player's soldier) and the view matrix at `+0x100`. The
vector from the attached entity's position (the camera's own if none) to the source is turned by
that matrix with the GTE (`sound_listener_vec`, `0x800427F4`, `mvmva`, rotation only, >> 12):
x right, y vertical, z forward. That matrix is **not a pure rotation**: its rows have lengths 2.0,
1.25 and 2.0 (×4096) in every dump (it is the scaled view matrix), so the listener-space vector
is `(2·right, 1.25·up, 2·forward)` of the world offset, and every distance below is in those
units (a world distance D straight ahead is 2D).

1. **Distance** (approximate): `d = max + (sum of the other two) / 4` of `|x|, |y|, |z|`.
2. **Gain** (`sound_falloff`, `0x80042998`), d at least 256:

   `g = max(0, 4096 − (d − 256)·4096 / R)` then `g = g · S >> 7`

   | Class (+0x13) | R | Silent from d = | World units ahead (d/2) | Used by |
   |---|---|---|---|---|
   | 0 | 6912 | 7168 | 3584 | Most sounds |
   | 1 | 7936 | 8192 | 4096 | Destructibles breaking |
   | 2 | 8960 | 9216 | 4608 | The mounted gun |
   | 3 | 9984 | 10240 | 5120 | Explosions, impacts on walls, enemy rockets |

   Linear from full at d ≤ 256 (128 world units ahead) to silence; S is the effects setting.
3. **Panning**, from x and z: if z > 400 (more than 200 units in front), x is divided by
   (|z| − 400), so anything in front is practically centred; otherwise x is used as is. Then
   with `a = |x|`:
   - a > 1024: hard left (x < 0) or right (x > 0): that side g, the other 0.
   - else: near side `g·(0x800 + 2a) >> 12`, far side `g·(0x800 − 2a) >> 12` (centred: g/2
     each; fully to one side at 512 world units of lateral offset).
4. **Mono** (`g_params+0xF` = 0, the shell's stereo option off): both sides get their average.
5. Each side `· (v >> 16) >> 12`, then `<< 6`, to the SPU. (At full gain, S = 168 and volume 255,
   a hard-panned side reaches 0x5380, past the SPU's 15-bit volume range; read from the
   arithmetic, not heard.)

So a full-volume sound (255) centred at the listener with the default setting gives about 0x29C0
per side; the same sound not positional gives 0x29D6: positional sounds are no louder than flat
ones at point blank.

### Settings

| | Where | Values |
|---|---|---|
| Effects volume S | `g_params+0xA` (`0x800939FE`) = the shell's setting (`g_game+0x88`, 0..6) × 42 | 0..252, default 168 (4) in every dump |
| Music volume | `0x8003864C` (also `g_params+0x9`), 0..6 | Voice 0 volume 0, 0x800, 0x1000, 0x2000, 0x3000, 0x3800, 0x3FFF (`music_voice_on`, `0x8008AA38`); default 4 = 0x3000 |
| Stereo | `g_params+0xF` (`0x80093A03`) = (`g_game+0x84` == 0) | 1 stereo (every dump), 0 mono |
| Ambience only | `0x80038658` (`g_game+0x8C`, saved) | 0 music and ambience (every dump), else `Mm_lA.VB` |

The script op `sound` (below) has its own scale for its stereo-fixed mode: S 0 → silent, 1 →
half, 2 → as is, 3 → double; it compares S with 0..3, so with S = 42·setting only setting 0
(silent) and the "as is" case apply in practice (inferred: probably a leftover of a 0..3 scale).

## What plays when

Arguments as `bank:sound, volume, pitch ±spread, class`; volume 0x10000 = 128/255, pitch
0x10000 = 1.0. All from the call sites (`tools/recon/snd_calls.py` lists them with their
constant arguments).

### Weapons

The player's and the enemies' weapons each carry their sounds:

| Field | Player weapons (`g_weapons`, `0x80093640`) | Enemy weapons (`g_enemy_weapons`) |
|---|---|---|
| Fire | +0x14 sound, +0x16 bank (−1: the held weapon model's `+0xC8`) | +0x28 sound, +0x2C bank |
| Draw | +0x18/+0x1A | |
| Holster | +0x1C/+0x1E | |
| Reload | +0x20/+0x22 | |

| Weapon | Fire | Draw | Holster | Reload | Flags |
|---|---|---|---|---|---|
| 0 .45 pistol | 1:2 | 0:20 | 0:20 | 1:3 | |
| 1 Thompson | 1:0 | 0:21 | 0:21 | 1:1 | |
| 2 Bazooka | 1:9 | 0:22 | 0:22 | 1:10 | |
| 3 Grenade | 1:6 | 0:23 | 0:23 | 0:23 | |
| 4 Shotgun, 5 sniper rifle, 12 Garand | own:0 | 0:24 | 0:24 | own:1 | |
| 6 SMG, 11 BAR | own:0 | 0:21 | 0:21 | own:1 | |
| 7 Papers | 0:33 | — | — | — | |
| 8 pistol | own:0 | 0:20 | 0:20 | own:1 | |
| 9 silenced pistol | own:0 | 0:20 | 0:20 | own:1 | not heard (flag 0x10) |
| 10 stick grenade | 1:6 | 0:23 | 0:23 | 0:23 | |

("own" = the bank of the weapon model: 2 or 3, its 2-sound bank.) Enemy weapons: SMG 1:0,
rifle 1:4, kind 2 1:2 (the player's pistol sound: enemies.md's "MG?" is more likely a pistol), Panzerfaust 1:9 (the soldiers' scripts add the rifle bolt 1:5 and others
through the script op, below).

| When | Sound | Code |
|---|---|---|
| Player fires | fire sound, 0x17FFF (191/255; 0x20000 for weapons 5 and 11), pitch 1.0 ±9%, class 0, flags bit 1 = weapon flag 0x10 | `fire_gun` |
| Player draws a weapon | draw sound, 0x10000, pitch 0.5 ±20%, flags bit 1 as for firing; also fades the soldier's sounds over 10 frames | `weapon_draw` `0x8007DD4C` |
| Player holsters | holster sound, same arguments | `weapon_holster` `0x80080380` |
| Player reloads | reload sound, 0x20000, pitch 0.5 | `0x800460F4` (with script 9) |
| Ammo pickup adds to a weapon | its reload sound, 0x10000, pitch 0.5 | `ammo_add` `0x8004301C` |
| Throwing a grenade | fire sound, 0x20000, ±9% | `throw_weapon` |
| Shell casing lands | 8..23 frames after a shot (49% of shots; at most 5 pending): 0:0x10 or 0x11, 0xCCCC, pitch 1.25 (0.8 for the mounted gun) ±10% | `casing_spawn` `0x80088040`, `casing_think` `0x80087FA0` |
| Enemy shot | fire sound, 0x10000, ±9%, class 3 for rockets else 0 | `enemy_weapon` |
| Mounted gun shot | 1:11, 0x20000, ±9%, class 2 | `mounted_gun_think` |

### Hits, impacts, explosions

| When | Sound | Code |
|---|---|---|
| Bullet hits a wall (target flag 0x4000) | 0:0..6 at random, 0x30000, pitch 1.0 ±50%, class 3, with sparks | `impact_sound` `0x80043140` |
| Bullet hits a soldier | 0:0x0C, 0x20000, ±30% (the code adds `rand() >> 15`, always 0, so 0x0D is never picked) | `bullet_touch`, `0x80043E38` |
| Shot in the head: helmet models 0x8A522920, 0xAA1714FE | 0:0x23; any other head: 0:0x25 and the soldier dies | `enemy_hit_part` |
| Hit part kind 5 | 0:0x22, 0x20000, ±20% | `enemy_hit_part`, `0x80054C24` |
| Helmet bouncing on the floor | the part table's sound (`0x800980B0`, 0x3C per part: +0x24 sound, +0x28 random range, +0x2C bank), volume halving each bounce | `0x80055198` |
| Player hurt | 30%: grunt 0:0x1B or 0x1C, 0x20000, pitch 0.5 ±6%; and 0:0x0C/0x0D (0:0x22 when hit by a soldier's melee) | `player_hit_part` |
| Projectile bounces | 0:0..6 (grenade 0x13F3: 1:7 or 1:8, 0x40000) | `0x80043284`, `0x80044470` |
| Rocket or grenade explodes | 0:9 or 0:10 at random, 2 or 3 layers at once, 0x40000, ±25%, class 3 | `explosion_sound` `0x80042DF8` |
| Level built-in 6 `explosion(kind)` | kinds 0, 4, 7: `explosion_sound`; kind 1: 50% 0:0..3, 0xCCCC, pitch 0.5 ±12% | `0x80052F14` |
| Destructible breaks | by model hash: 0:0x23, 0x25 or 0x27, 0x20000, ±25%, class 1 | `destructible_touch` |

### Pickups, interface, alarm

| When | Sound | Code |
|---|---|---|
| Health pickup | 0:0x1F (kind 0) or 0x1E, 0x20000 | `health_pickup_touch` |
| Ammo pickup | 0:0x20, 0x10000 | `ammo_pickup_touch` |
| Pause menu opens | 0:0x13, 0x20000, pitch 0.5 | `start_handler` |
| Pause menu item changed | 0:0x21, 0x10000 | `start_handler` |
| Paused | every voice (music too) frozen: volume and pitch saved, set to 0, restored on resume | `start_handler` |
| An alarm starts | 0:0x13 once, then the alarm loop (below) | `alarm_spawn` `0x8005EA54` |

**The alarm** is an entity (type 0x20, a 0x2C-byte object, not positional, think `alarm_think` `0x8005EDF8`) that re-triggers one
sound: when its voice has finished it waits `delay + rand() % random` vblanks (`VSync(-1)`) and
plays it again. Raised by the soldiers' op 0x49 (`op_alarm`, `0x8005C710`: alarm id, sound,
pitch, pitch spread, volume, volume spread, bank −1 = the model's) or by `level_set(4, id)`
(`alarm_from_level`, `0x8005F068`: 0:0x2C, delay 10, volume 0x20000, the pitch is then set by mission
whatever the caller gave: 1.0 in missions 3 (but 0.5 in 3:1), 4, 8 and 9, 0.5 in 2 and 5, the
caller's otherwise). Removed after 1801 thinks, when the record tied to its alarm node is in
state 2 (inferred: the alarm box destroyed or switched off), or by a second `level_set(4, ...)`
while it sounds (that sets its counter to 1801). On raising: `g_alarm` (`0x80097FE0`) = the node, the
entity moves to the front of the list, every living soldier but the raiser gets `+0x3C` bit 0x100, and
every entity of the node's record is sent message 0x2B. Its voice is free again when its SPU status is
not 1 (key on); the next play is then due at once (first sound) or after the delay. Destroying it stops
its voice and clears `g_alarm`. (Cheat 4 replaces the bell by one of 25 random sounds.)

### Scripts

**Model scripts, op 02 `sound`** (`op_sound`, `0x80058420`; enemies.md): operands sound (the
immediate's low byte), pitch, pitch spread, volume, volume spread, bank (−1: the model's
`+0xC8`); immediate high byte: 0x20 loop, 0x01/0x02 stereo-fixed (bank 0, not positional,
volume/4 on both sides, or a random 0..volume/4 per side with 0x02, scaled by S as above). The
voice goes into the entity's `+0xFC`; enemy variable 0x68 reads it: 1 while that voice is on,
not looping, positional and younger than 120 frames.

What the soldier scripts play (1:3, `sst.py` on `build/re/mid_14000.bin`):

| Sound | Bank | Pitch | Volume | |
|---|---|---|---|---|
| 0x0F | 0 | 0.4..1.5 ±20% | 0x6666..0x18000 | Footsteps (in the walk/run animations; the sample is per level, not per surface) |
| 0x19, 0x1A | 0 | 0.5..1.0 ±20% | 0x8000..0x20000 | Body falls / landings (inferred from the scripts using them) |
| 0x04, 0x05, 0x09, 0x0A | 1 | 1.0 | 0x10000..0x20000 | Rifle shot and bolt, rocket and its reload |
| 0x00, 0x01, 0x03..0x05, 0x07..0x0A, 0x0D, 0x0E, 0x14..0x16 | −1 (voice bank) | 0.5 ±10..20% | 0x20000 | Voices: shouts (0x0D, 0x0E, alerting, soldier.md), pain (0, 1), deaths |

Pitch 0.5 on the voice bank is what plays its 11025 Hz recordings at their own speed (base
pitch 0x800 × 0.5 = 0x400). The player's soldier model plays only 0:0x21; no call or player-model script
playing a footstep for the player was found (inferred: the player is silent when walking).

**Footsteps** are model-script sounds only: no code calls `sound_play` with 0x0F (the immediates
0xF passed in `enemy_spawn` and `player_death_start` are other arguments). The soldier's (and
dog's) movement scripts play animations in frame ranges and put a step between them: the walk
loop (anim 7: frames 0..4, `call 53`, 5..14, `call 53`, 15..end, repeat; 25 calls of 53 in 2:2's
soldier model) and the strafes and turns call one-line scripts (46..50, 53: `sound 0x0F` with
their own volume and pitch, e.g. 53: pitch 0.5 ± 0x3333, volume 0xC000, bank 0, then return);
scripted moves (7, 8: anims 40, 42) put `sound 0x0F` inline at their foot-fall frames. So a step
is heard at the animation frame the script waits for, positional on the soldier (op 02 plays on
its entity, follows it), one voice each, through the normal voice rules above (the player hears
it through `sound_pan`; soldiers through hearing, query 0x11). The sample is the level's bank-0
0x0F (one recording per level). The player plays none (above): the engine's voices match the
original's in the 1:3, 2:1 and 2:2 sessions with that rule.

**Level scripts, built-in 19 `sound`** (`0x80069B94` → `lvl_sound` `0x800533D4`): 7 values,
in push order `volume_spread, volume, pitch_spread, pitch, sound, bank + 1, mode` (bank 0 when
given 0). Played on the script's entity; mode 3 = loop, and the voice is pushed as a handle.
Exactly: `sound_play(bank, sound, the context's entity, volume, volume_spread, pitch,
pitch_spread, 0, mode == 3, 0, 0)`.
Built-in 20 `sound_stop(handle)` (`0x80069CC4` → `0x80053444`) stops that voice at once. 1:3's
calls: 0:53 and 0:54 (objective jingles), 0:50 (the message beep, with on-screen texts), 0:49 at
1.0 and 0.5 together (a scripted explosion), 0:39 (a destructible, script 84), 0:40 (with a
text). No level among 1:3, 2:3, 3:2 uses mode 3.

## Music

1. **Start**: `level_set(1, 1)` (levels.md; 1:3's objective script runs it on msg 0). While
   the level is starting (`0x8009A60C` set by the level start, `0x80061E14`) it only records the
   request (`0x8009A608`); the start function plays it at its end. A second start prints
   "Tried to reactivate music a second time" and does nothing (`0x8009CC74`).
2. **The file** `\data\msnM\lvlL\mM_L.vb` (or `mM_La.vb`, above) is opened and read from the CD
   20 sectors (40960 bytes) at a time into a RAM buffer (`music_start`, `0x8008A838`).
3. **The ring**: the first 2 × 0x5000 bytes go to SPU 0x1010 and 0x6010; the last block of the
   second half gets the ADPCM flags end+repeat (byte 3 at +0x4FF1) and the loop address is the
   first half, so voice 0 cycles through the ring. An SPU IRQ placed 0x20 bytes into a half
   (`music_irq`, `0x8008A7B8`) tells `game_frame` to copy the next 0x5000 bytes into the half
   just played, and moves the IRQ to the other half. 0x5000 bytes = 1.63 s of sound.
4. **Voice 0**: start at the ring, pitch 0x800 (22050 Hz), the music volume on both sides
   (mono, centred), the same ADSR as effects (`music_voice_on`, `0x8008AA38`).
5. **End of file**: "Replaying audio from start" (`music_transfer_done`, `0x8008A6D8`,
   `music_rewind` `0x8008A9E4`): the read position goes back to the file's first sector; the
   track **loops** forever (with whatever silence its ends hold).
6. **Stop**: `music_stop` (`0x8008AB48`, key off voice 0, CD callbacks off) at every level
   (re)start and when leaving the level (`0x80062298`).

There is **no dynamic music**: one track per level, started by the level script, never switched
or faded by combat; the only other control is the pause freeze. Ambience comes from the track
and from sound emitters (the alarm, looping script sounds), not from separate ambient streams.

### The two mixes

`Mm_lA.VB` is the plain track **with the music taken out**: the audio menu's Background
choice "music and ambience" / "ambience only" (shell.md) writes `g_game+0x8C`, which is
`0x80038658`; `audio_menu_init` (SHELL `0x8003D9D8`) points its four widgets' value pointers
(`+0x84`) at `0x80038654` (effects), `0x8003864C` (background volume), `0x80038650` (stereo) and
`0x80038658`, in the menu's order. LEVEL reads it in `level_set` (`0x80052E4C`) and the level
start (`0x80062050`) to pick the file.

Checked on the files (`tools/recon/sh2_audio.py mix M L`): projecting the plain track onto the
`A` track gives a coefficient of 1.00 (1:1, 1:3, 2:1, 5:1; 0.99 for 9:1, 0.93 for 3:2), i.e. the
plain track is the `A` track unchanged **plus** a layer uncorrelated with it (the music); the `A` track is always
quieter (whole-track RMS 1850 against 2351 in 1:1, 922 against 1939 in 5:1), the two are
identical in places (1:3's last 10 s: difference RMS 78, correlation 1.00) and differ through
the middle (correlation 0.4..0.9 per 10 s). In 3:3 and 7:1 the `A` file is 0.6 s longer and the samples don't
line up, so the test needs an offset there (not done). Not listened to.

## What the samples are

Measured on the disc files with `tools/recon/sh2_audio.py bank FILE.TAF BANK [OUT]` (length,
RMS, spectral centroid, zero crossings, an 8-slice envelope; optional WAVs under build/) and
`sh2_audio.py same` (byte comparison between two banks), then tied to the code and scripts that
play each id. No speech recognition was available (no whisper/vosk), and nothing was listened
to: names below that rest only on the sound's shape are marked "inferred".

### Empty slots

Both bank 0 and the voice banks are **fixed id spaces with per-level content**: a level's bank
holds a real sample only for the ids it needs and a 96-byte stub (12 units of 8 bytes, silence)
elsewhere. `voice_start` skips anything shorter than 13 units, so **playing a stub does
nothing**. Some calls do hit stubs: 1:3's level scripts play 0:0x31 (decimal 49, the scripted
explosion layer) and 0:0x28 (40, with message 243), both stubs in 1:3; 2:1's bank 0 has no 0x09,
so half of its `explosion_sound` layers are silent; in 1:3 the soldiers' group-15 shouts 0x14,
0x16, 0x17 are stubs (only 0x15 sounds). A new engine should treat a missing sample as silence,
not as an error.

### Bank 0

Across all 31 levels (`*1.TAF`; the `0` TAFs have the same bank 0): ids 0x00..0x23 are the same
sample wherever present (except 0x09 in 2:2/3:1, 0x0F, 0x22 and 0x23 noted below); 0x24 and up
are per-level, mostly for level scripts. 0x08, 0x0E, 0x39, 0x3A, 0x47 are stubs everywhere.

| Id | Length | What (how known) |
|---|---|---|
| 0x00..0x06 | 0.30..0.60 s | Bullet impacts on walls, one at random (`impact_sound`: `rand()·7 >> 15`, so 0..6) |
| 0x07 | 0.27 s | Unknown: bright and short (centroid 4.8 kHz), in every level; no code or 1:3 script plays it (inferred: a ricochet or metal hit, for some object's script) |
| 0x09, 0x0A | 1.79, 1.61 s | Explosions (`explosion_sound`). 2:2 and 3:1 carry a shorter 0x09 (0.83 s) |
| 0x0B | 1.86 s | A third explosion-like sound (low, centroid 1.9 kHz), played at half volume and half pitch (a 3.7 s rumble) by `0x80089174` when its countdown `0x8009EC90` reaches 1 (a screen effect; hud.md) |
| 0x0C, 0x0D | 0.33, 0.31 s | Bullet hitting a body (`bullet_touch`; 0x0D also in `player_hit_part`) |
| 0x0F | 0.20..0.31 s | Footstep; five different recordings by level (inferred: the level's main surface) |
| 0x10, 0x11 | 0.56, 0.47 s | Shell casings landing (`casing_think`) |
| 0x12 | 0.62 s | Unplayed by code; the same spectrum as the casings (centroid 5.7 kHz, zero crossings 8.2k/s like 0x10) (inferred: a third casing, e.g. a shotgun shell) |
| 0x13 | 0.08 s | Short chime: the pause menu opening, an alarm starting |
| 0x14..0x18 | 0.17..0.30 s | Weapon draw/holster: 0x14 pistols, 0x15 SMGs, 0x16 bazooka, 0x17 grenades, 0x18 rifles and shotgun (the weapon table) |
| 0x19, 0x1A | 0.50, 0.47 s | Body falls (soldier scripts, bank 0; inferred, above) |
| 0x1B, 0x1C | 0.27, 0.18 s | The player's grunts when hurt |
| 0x1D | 0.39 s | Played at run time (seen in the replay) from a model script; which one wasn't found |
| 0x1E, 0x1F | 0.68, 0.43 s | Health pickups (0x1F the small one) |
| 0x20 | 0.12 s | Ammo pickup |
| 0x21 | 0.11 s | Pause menu item changed (low click, centroid 0.9 kHz) |
| 0x22 | 0.22 s | Melee / hit part kind 5 (MSN12 has its own) |
| 0x23, 0x25, 0x27 | 0.40, 0.23, 0.46..1.21 s | Destructibles by model (objects.md); 0x23 and 0x25 also the helmet / head shot sounds (`enemy_hit_part`). 0x27 differs per level (1:3's script 84 plays it as the break) |
| 0x24, 0x26, 0x28..0x38 | 0.2..2.5 s | Per level, played by the level scripts (`sound(...)`, decimal ids in `lvs.py` output): 0x29 (41) a door in 2:1, 0x2A (42) doors in 2:2, 0x2D (45) and 0x38 (56) used with `activate` in 2:2/3:2, 0x31 (49) the scripted explosion (two layers, pitch 1.0 and 0.5), 0x33 (51) and 0x37 (55) script events, 0x35/0x36 (53, 54) objective jingles |
| 0x32 | 0.22 s | The message beep (with on-screen texts); in every level |
| 0x51 | 1.30 s | 9:4's level start (on voice 0) |
| 0x52 | 0.46 s | 2:2's objective (`objective_complete(27)` with text 57) |
| 0x3B..0x50 | | One to three levels each: those levels' own script sounds |

### The voice bank (37 sounds)

Over the 21 levels with a 37-sound bank: ids 0x00..0x06, 0x1A, 0x1B and 0x1F are **the same
bytes in the German and American TAFs** (`0` and `1`) wherever present (but for a few that
exist in only one); 0x07..0x10, 0x12, 0x14..0x17, 0x1C..0x1E, 0x22, 0x23 differ between the two.
That splits the bank into non-verbal sounds and speech. 0x11, 0x13, 0x18, 0x19, 0x20, 0x21 and
0x24 are stubs in every level. Use from 1:3's soldier
scripts (`sst.py`, soldier.md for the script numbers), 11025 Hz:

| Id | Length (1:3) | Language | Use |
|---|---|---|---|
| 0x00, 0x01 | 0.23, 0.30 s | same | Pain grunts: 45% on a hit (scripts 4, 152, 128) |
| 0x02 | 0.30 s | same | Not played in 1:3 (inferred: a third grunt) |
| 0x03 | stub in 1:3 | same | 5% when thrown by an explosion (120, 123), and the sentry's death (198) |
| 0x04..0x06 | 0.58, 0.64 s, stub | same | **Death cries**: voice group bit 16 (script 74 via 153, 190, 191) |
| 0x07..0x0C | 0.4..1.5 s (0x0B, 0x0C stubs) | speech | **Barks**: voice group bit 17 (script 74 via 23, 206) |
| 0x0D..0x0F | 0.43..0.61 s (0x0E stub) | speech | **Alert shouts** (209: 10% each when first alerted near the player) |
| 0x10, 0x12, 0x1C | 0.4..1.2 s | speech | Not played by 1:3's soldier scripts (inferred: lines for other levels' scripts, e.g. the disguise levels) |
| 0x14..0x17 | 0x15 0.76 s, others stubs | speech | **Combat shouts**: voice group bit 15 (script 74 via 201, 207, 208: going for an alarm, sentry/strafe shouts) |
| 0x1A, 0x1B | 0.71, 0.56 s | same | Not voices: quiet noise (centroid 2.8 kHz, peak 9000). 0x1A is the **helmet hitting the floor**: the part table's entry 0 (`0x800980B0`: sound 0x1A, range 1, bank −1 = the helmet model's bank) via `0x80055198`, whose `rand()·range >> 15` is always 0, so 0x1B is never picked |
| 0x1E | 0.64 s German, stub American | speech | Script 114 (running onto a grenade, soldier.md): a German-only line |
| 0x1D | | speech | Present in 3 levels only; not mapped |
| 0x1F | | same | Present in 5 levels; soldier.md's alarm runner "shout 0x1F" (2:x's script 201); being the same in both languages it is a non-verbal call (inferred) |
| 0x23 | | speech | Script 38 (reload), for kind-2 weapons with bit 3 |

Which of the group's ids a soldier picks depends on script 74's `params` and variable 0x73
(per mission and level) and a roll; a new engine can take "group → ids" per level from that
script. The 81-sound banks of the disguise levels (2:1, 2:2, 3:1) share 0x00..0x05 and 0x1A/0x1B
with the others and are the same in both languages; their extra ids serve the papers dialogue
(soldier.md's voices v25 1..6), not mapped here.

## Checked at run time

Replaying the session (1:3 from vblank ~2000, 2:1 by 30000, 2:2 by 50000, to 95000) with
`VALOR_WATCH` on every voice's sound id and bank (`g_voices+8`, 8 bytes per voice;
`build/snd/w60000.log`), counting each (bank, sound) a voice was given:

| | Seen (count) |
|---|---|
| Bank 0 | 0x0F footsteps (416), 0x00..0x06 impacts (48..70 each), 0x0C/0x0D hits (73, 80), 0x10/0x11 casings (71, 77), 0x09/0x0A explosions, 0x15, 0x17, 0x18 (draw/holster), 0x19, 0x1A, 0x1B/0x1C (player grunts), 0x1D, 0x21 (48), 0x35 (objective jingle, 8), one each of 0x13, 0x20, 0x22, 0x25 |
| Bank 1 | 0x00 SMG (188), 0x04/0x05 rifle shot and bolt (94, 106), 0x01, 0x06..0x08 (grenades) |
| Bank 2 | 0x00 (100), 0x01 (42): the held weapon's shot and reload |
| Bank 4 | 1:3's voice bank and 2:2's: rare (1..3 each of 0x00, 0x01, 0x07..0x0A, 0x0D, 0x0F, 0x1A, 0x1C, 0x1D, 0x20) |
| Bank 5 | 1:3's 0x1405 bank: 0x04 (58), 0x05 (42), 0x00, 0x03, 0x06, 0x07 (its own weapon and voice set, inferred) |

Voice 0's entry never changed (the music doesn't go through the table); no voice had the loop
byte set. Everything seen fits the call sites above; nothing was played from a bank or id the
code doesn't account for.

## Open questions

- Bank 0 ids 0x07, 0x12, 0x1D and the per-level 0x24+ sounds not seen in the five dumped levels'
  scripts: which model or level script plays them. Running `sst.py` over every entity type of
  more levels' dumps would settle most.
- Voice bank: 0x02, 0x10, 0x12, 0x1C, 0x1D, the 81-sound disguise banks' extra ids, and which
  id of each group a given mission/level picks (script 74's `params`/variable 0x73 branches).
  Names here come from where the scripts play them, not from listening; a listen (or offline
  speech recognition) would give the actual words.
- The `A` mix check in 3:3 and 7:1 (files offset by 0.6 s).
- The pan reading (front sources nearly centred) is from the code; whether that matches the feel
  should be checked by ear in the emulator.

## Tools

- `tools/recon/snd_taf.py FILE.TAF [-v]`: the banks of a TAF, their SPU ranges, per-sound sizes.
- `tools/recon/snd_vb.py stats FILE [FILE2]` / `wav FILE OUT [RATE]`: decode SPU ADPCM; compare
  two tracks.
- `tools/recon/snd_calls.py [ADDR...]`: the calls of `sound_play` with their constant arguments.
- `tools/recon/sh2_audio.py mix M L | bank FILE.TAF BANK [OUTDIR] | same A.TAF B.TAF BA BB`
  (host python with numpy): the two music mixes compared; per-sample measurements of a bank and
  WAVs (put them under build/, never in the repo); which samples two banks share.

## Names

```
80041260 sound_init                 # SsInit, master/CD volume, banks forgotten, SPU allocator 0x1010, music ring
800412E0 sound_stop_all             # voice_stop on 1..23 (level_load)
8004131C sound_reset                # stop all, forget the banks, allocator back to 0xB010
8004137C sound_update               # per frame: key-ons, ages (351), follow entities, fades, re-pan every 4th, loops
800418B8 bank_forget
800418E4 bank_load                  # (vab, slot): body to SPU, size table, base pitches
80041AA0 voice_alloc                # first of 1..23 not looping and off/releasing; -1 none
80041B04 voice_start                # volume, pitch with spread, start address, ADSR, queue key-on
80041CE8 sound_play                 # (bank, sound, entity, vol, vol_spread, pitch, pitch_spread, fade_in, loop, class, flags) -> voice
80041FC4 sound_play_voice0
80042250 sound_restart              # sound_play on a given voice (loops)
8004249C sound_fade_entity          # (entity, frames)
800425C4 voice_stop
80042664 voice_release
8004270C bank_tone_pitch
80042790 banks_clear
800427F4 sound_listener_vec         # source - listener, through the camera matrix (scaled 2, 1.25, 2)
80042998 sound_falloff              # (distance, class) -> gain 0..4096 x effects volume >> 7
80042AA4 sound_pan                  # (voice, &left, &right, class)
80042DF8 explosion_sound            # 0:9/10, two or three layers
80043140 impact_sound               # 0:0..6 and sparks
8004301C ammo_add                   # (soldier, slot, amount); plays the weapon's reload sound
80052F14 lvl_explosion              # level built-in 6
800533D4 lvl_sound                  # level built-in 19 -> sound_play
80053444 lvl_sound_stop             # level built-in 20 -> voice_stop
80058420 op_sound                   # model script op 02
8005C710 op_alarm                   # model script op 0x49
8005EA54 alarm_spawn
8005EDF8 alarm_think
8005F068 alarm_from_level           # level_set(4, id)
8007DD4C weapon_draw
80080380 weapon_holster
80086A94 taf_load_banks             # (vab table, first slot)
80087FA0 casing_think
80088040 casing_spawn
8008A6A8 music_ring_init
8008A6D8 music_transfer_done        # rewinds at the end of the file
8008A7B8 music_irq
8008A838 music_start                # (path)
8008A9E4 music_rewind
8008AA38 music_voice_on
8008AB48 music_stop
80038658 g_music_alt                # g_game+0x8C: 1 = Background "ambience only": play mM_La.vb (the track without music)
8003864C g_music_volume             # 0..6
80090FC8 g_keyon_pending
80090FCC g_pan_phase                # 0..3, positional voices re-panned on 3
800910E8 g_bank_spu                 # 8 SPU addresses; +0x20 size-table pointers, +0x40 loaded flags
8009163C g_bank_pitch               # base pitch per (sound, slot), 256 x 8 u16
8009263C g_bank_sizes               # 8 x 0x200: the VAB size tables
8009A608 g_music_requested
8009A60C g_level_starting
8009CC74 g_music_started
800A0128 g_music_irq_flag
800A012C g_music_left               # bytes of the file still to play
800A0130 g_spu_alloc
800A0134 g_music_ring               # two SPU addresses, +0x1C the half to refill
```
