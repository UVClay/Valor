# The front end (SHELL.BIN): campaign flow, saves, options, passwords, medals

SHELL.BIN is the menu overlay (overlay id 7, loaded at `0x80039D90` like LEVEL.BIN). Everything
it hands to the game lives in `g_game` (`0x800385CC`, 0x780 bytes in the exe's data, so it
survives overlay loads), and that same block is what goes on the memory card.

Checked by reading the code (`fn.py --bin SHELL.BIN`, the tools below) and by headless runs:
menus navigated with `--press`, passwords typed on the Enigma screen, a game saved to a fresh
card (`build/shell/card5.mcd`), RAM dumped before and after, and the save of the 1:3 recording
(`recordings/2026-10-19_22-22-43_m1l3/card/`) decoded. "Inference" marks what isn't checked.

Tools (inside `./dev`):

- `tools/recon/shell_refs.py LO HI [BIN...]`: every load, store or address build in a range,
  including through a base register (`lw v0, 0xD4(a0)` with a0 = g_game). How the g_game fields
  below were found.
- `tools/recon/shell_save.py FILE`: decodes the save block of a card image (.mcd), a single save
  (.mcs) or a RAM dump.
- `tools/recon/shell_password.py WORD [FRAME]`: the `--press` arguments that type a password on
  the Enigma keyboard.
- `tools/recon/shell_sst.py`: the menu screens, their buttons and where each leads, from the
  screen scripts in `DATA\GLOBAL.RSC`.

## Overlays: who runs next

The exe's main loop (`0x80037060`) loads an overlay, calls its entry, and when it returns picks
the next one (read in the disassembly; m2c gets this part wrong):

| Just returned | Next |
|---|---|
| LEVEL or LEVEL2P | SHELL |
| SHELL, `g_game+0x94` (mission) < 11 | LEVEL (one player) |
| SHELL, mission ≥ 11 | LEVEL2P (split screen); `g_game+0x98` = arena map of `g_mp_arena` (`0x80039C74`): 0→3, 1→1, 2→2, 3→5, 4→4, 5→0, 6→6 |

So **MSN12 is the multiplayer arenas**, not single-player bonus levels: the shell's
`level_begin(2)` sets mission 11 (MSN12) and the exe sends that to LEVEL2P, which builds
`data\msn12\lvlN\...` (the strings are in LEVEL2P.BIN). There are no single-player bonus
levels on the disc (DATA has MSN1..5, 7, 9, 12). levels.md's "Mode 2 ... bonus/secret levels"
should read "multiplayer".

The shell ends (entry `shell_main`, `0x8005FC28`, returns) when `g_shell_quit` (`0x8007BFFD`)
is set: by the briefing after its last page (`brief_input`, `0x8003F6EC`) and by the
multiplayer start (`mp_start`, `0x80057FB8`). On the way out `shell_exit` (`0x80060D24`) prints
"Shell game settings after shell quits..." with most g_game fields (their names below come from
that printout).

## The campaign

### Slots

The campaign runs over 8 **slots** (`g_campaign`, `0x80038D90`, 12 bytes each: mission index,
next level, level count), set by `campaign_init` (`0x80060C7C`):

| Slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| Mission (index / dir) | 0 / MSN1 | 1 / MSN2 | 2 / MSN3 | 3 / MSN4 | 4 / MSN5 | 6 / MSN7 | 7 / MSN8 | 8 / MSN9 |
| Levels | 3 | 4 | 4 | 3 | 4 | 3 | 3 | 3 |

Slot 6 (MSN8) isn't on the disc and is skipped everywhere: after slot 5 comes slot 7
(`campaign_advance`), `level_begin` turns slot 6 into 7, and the mission menu's 7th entry is
slot 7. In slot 7 the third level is played as LVL4 (levels.md). Menus number missions 1..7
(`level_unlocked`, `0x80050C38`, maps menu mission 7 to slot 7).

`g_game+0xA8` is the current slot and `+0xAC` its next level; on load they are copied back into
`g_rating_slot`/`g_rating_level` and `g_campaign[slot].level` (`load_apply`, `0x8006EB8C`).

### Starting a level: `level_begin(mode)` (`0x80062108`)

`g_play_mode` (`0x80039D1C`) = mode:

| Mode | Set by | Effect |
|---|---|---|
| 1 | New Game (`0x8005C5D0`, after `new_game`), the title's mission/briefing items (`title_input` `0x80059030`), the mission select (`0x8004FF60`), the post-mission menu (`postmis_input` `0x8006B664`) | slot → `g_rating_slot`, its next level → `g_rating_level` and `g_game+0x98`, its mission → `+0x94`; **if the level has never been completed (best rating -1), `g_cheats &= 3`** |
| 2 | Multiplayer start (`mp_start`) | mission 11, level `g_mp_arena` |
| 3 | `0x8007064C` (mission and level from `0x8007E898`/`0x8007E89C`): the developer level select `m_dev` (inference: its handler; no code builds its address, so it's in a table). **Unreachable**: see below | no slot; results are shown but never recorded |

All modes clear `g_level_end` (13 weapon ammo halfwords to -1, kills to 0, health and the
objectives flag to 0).

A new campaign (`new_game`, `0x80061F00`): codename "noname", lives 5, MP characters 0x7F, MP
arenas 0x1F, career totals, kills per weapon, medals and both cheat words cleared, every mission
block reset (below: ammo rows and best ratings -1, levels locked, first-level health 128.0),
then MSN1 LVL1 unlocked.

### The handover to LEVEL and back

LEVEL reads g_game when it starts (`level_read_parameters`, player.md). When it ends,
`level_exit(reason)` (LEVEL `0x80062298`) stores the reason in `g_params+8` and an **exit code**
in `g_level_exit` (`0x80039C58`), and on a finished level runs `level_end_carry` (player.md):

| LEVEL flags (`g_level_flags`, `0x8009C840`) | Reason | Exit code | Carry |
|---|---|---|---|
| 0x04 (the level-end trigger / `level_set(3)`: \|= 0x24) | 4 | 0 | yes |
| 0x08 | 1 | 3 | yes |
| 0x40/0x80 (the pause menu's Quit sets 0x60) | 2 | 1 | no |
| 0x01 | 3 | 2 | no |
| 0x10 | 4 | 0 | yes |

**Nothing in the retail game sets 0x01, 0x08 or 0x10.** Every access to `0x8009C840` in
LEVEL.BIN (`shell_refs.py` over LEVEL, LEVEL2P and the exe, base registers included) is one of:
`level_set(3)` and the level-end trigger record (`0x80074E10`, class 0xD1BA7D1E) `|= 0x24`;
`player_think_dead` `|= 2` or `|= 0x202`; the pause menu `|= 0x60`; the level loop's own
`|= 0x20`/`0x60`; `level_load_all` (`0x8006240C`) `&= ~4`, `&= ~0x100`; `level_start`
`&= ~2`. The only other path is `level_start` handing `&g_level_flags` to `0x80061158`, an empty
function (`jr ra`): a stripped debug hook, as are the empty `0x8008B3C4` and `0x8008B3CC` the
loop calls before exits 0x01 and 0x10 (and `0x8008B3D4`, which just returns 1 and gates the
whole loop). So in practice LEVEL exits with code 0 (finished, reason 4) or 1 (quit, reason 2);
exit codes 2 and 3 never happen, and 0x100/0x200 are set or cleared but never tested. The shell
also uses the same variable: 4 after a new campaign or default settings, 5 once a level's
results are recorded or a game is loaded.

### Inside LEVEL: the loop, pausing, dying

`level_entry` (`0x80064400`) loops `level_main` (`0x800640B4`) until flag 0x20. Read from the
disassembly (m2c folds the loops wrongly):

```
level_main:
  while !(flags & 0xF1):
    if flags & 0xF9:                       # any end flag
        if flags & 0x0C: level_exit(flags & 8 ? 1 : 4); flags |= 0x20   # finished
        continue
    level_load_all()                       # files, models, TSPs (once per level)
    while !(flags & 0xF5):
        if flags & 2: level_restart()      # died: the same as level_start, screen cleared to black
        else:         level_start()        # 0x80061E14
        while !(flags & 0xF7):             # one game frame per pass
            if start_handler() == 0:       # not paused
                game_frame(); if flags & 0xF5: msg_run_all()
            else: sound_update()
  # out: 0x01 -> level_exit(3), 0x10 -> level_exit(4), 0x40/0x80 -> level_exit(2); flags |= 0x60
```

**Dying restarts the level in place.** When the player's health reaches 0,
`player_think_dead` (`0x80079A50`; movement.md, Death) lets the death camera play; when the
camera's script ends (camera `+0x54` = -1) it starts a fade to black (a type 0x32 fade entity,
`fade_start(1, 0x32, 15, ...)`, `0x8004F748`: 50 ticks' delay, then 15; hud.md) and, once that entity is gone, sets flag 2 (if no
fade entity could be made, at once). Without a death animation, and with the soldier's flags A
0x100000, it sets 0x202 at once, with two white fades (5 and 25 ticks; fades show as cuts, hud.md). Flag 2 ends the frame
loop and the level restarts without reloading any file (`level_restart` `0x800624AC` →
`level_start` `0x80061E14`):

| Restarted | How |
|---|---|
| Every entity but type 8 | removed, then the level's records spawn again; objectives cleared (`g_objective_count` = 0) and re-added by the level script's message 0 |
| Voices 1..23, the music | stopped; the music starts again from the top when the script asks |
| Statistics (`0x80039CA8..0x80039D10`, per-weapon kills `g_weapons[].+0x30`) | zeroed (`stats_reset`, `0x8006192C`) |
| Ammo | every slot's reserve = `g_weapons[w].+0x06`, the amount the level started with (`level_read_parameters` set it from the level's row; nothing changes it during play); clips empty, refilled by the first draw |
| Health | 128.0, or with `g_game+0x74` set (CAPTAINDYE) the level row's health (`player_reset`, `0x80061D08`) |
| The alarm, fog, sky, look-up state | reset (`0x80052C18`, `sky_init`, ...) |
| `inv+0x140` | + 1: a count of starts of this level (nothing reads it) |

What survives a death: `g_game` (nothing in it changes during a level), the pad option flags and
the debug combinations' `inv` flags below (`player_soldier_init` isn't re-run). There is **no
lives limit**: `g_game+0x9C` (5) goes to `g_params+0xC`, which only `level_read_parameters`
reads, to print it. Checked twice: the 1:3 recording dies three times (vblanks 22674, 32930,
44699: health 0, flag 2 set 238, 176, 242 vblanks later, restarted 27 vblanks after that with
health 128.0 and `inv+0x140` + 1; `VALOR_WATCH` on the flags, health and `inv+0x140`, log
`build/sh2/flags.log`); and a 1:3 run with flag 2 poked at the first projectile think
(`--poke-on-call 80044470:8009C840=2`) restarted 95 vblanks later with the shot count back to
0 and the Garand's reserve back from 24 to 32 (`build/sh2/deathB/log.txt`).

**The pause menu** (`start_handler`, `0x800625BC`, every frame before the game frame). Start
(pad bit 0x800) on port 0, or the controller being pulled out (`0x8009A600`, which adds "INSERT
CONTROLLER 1 AND PRESS START"), pauses: `g_paused` (`0x8009A5E0`) = 1, every SPU voice's volume
and pitch saved and zeroed, the on-screen texts beyond the first two saved away (restored on
resume), a dark translucent box and these texts (`text_add`, screen x, y in 512×240):

| Text | Where | Colour |
|---|---|---|
| PAUSED | 220, 28 | yellow |
| MISSION OBJECTIVES | 148, 48 | white |
| TOTAL ENEMIES NEUTRALIZED + `g_stat_kills` | 100, 68 / 416, 68 | grey / orange |
| One line per objective, 16 lines apart from y 88: a box glyph `$`, with `#` (the tick) over it when done, then the objective's text | 48 / 72 | orange |
| "X" TO ACCEPT "START" TO CONTINUE | 48, 188 | grey |
| CONTINUE, QUIT | 100, 208 / 388, 208 | the selected one red (0x80, 0x40, 0x40), the other grey |

There are only two items: **no options, no restart** in the game. Left or right on the d-pad
(bits 0x8000, 0x2000; or the analog stick in analog mode) toggles the selection (sound 0:0x21);
Cross (0x40) plays 0:0x13 and acts on release. CONTINUE resumes; Start resumes from anywhere
in the menu, but only 10 frames after the pause began (`0x8009CC78/7C`). QUIT shows QUIT / ARE
YOU SURE? / NO (left) YES (right), NO selected (`0x8009A5EC` = 1); Cross on NO resumes the game
(not the pause menu); Cross on YES sets flags 0x60: `level_exit(2)`, exit code 1, no carry, and
the shell opens on `m_title`. While paused the game frame doesn't run (`sound_update` still
does). Checked in a headless 1:3 run (Start, Right, Cross, Left, Cross; screenshots in
`build/sh2/pause/shots/`): the screens above, then "Returning to base". The recording quits 1:3
the same way (exit code 4 → 1 at vblank 27878).

**Debug button combinations** (`player_pad_config`, `0x8007A050`). They are tested only when
the function runs: at spawn (`player_soldier_init`), and when the pad's mode byte
(`0x80093B78[port]`, digital/analog) differs from `inv+0x153`, i.e. when the controller's
ANALOG button is pressed or a different controller is plugged in. Holding at that moment (pad
bits as libetc's PadRead: Up 0x1000, Down 0x4000, Triangle 0x10, Cross 0x40, L2 0x1, R2 0x2,
L1 0x4, R1 0x8):

| Hold | Sets in `inv` word 0 | Effect |
|---|---|---|
| L1 + L2 + R1 + R2 + Triangle + Up (0x101F) | 0x10000000 | Low gravity: 0.125 per tick, terminal 4.6875, jump 6.5 (movement.md) |
| L1 + L2 + R1 + R2 + Cross + Down (0x404F) | 0x20000000 | **The player's damage ×4**: `apply_damage` (`0x8004AC58`) shifts the damage left 2 when the attacker is the player's soldier (flags A 0x1) with this bit (combat.md) |

Neither is cleared by the function or by a death restart; `player_soldier_init` sets word 0 to
0x400000 at the next level. (Other code testing `lui 0x2000` masks reads entity flags A
0x20000000, "dead", not this word.) LEVEL2P has the same 0x101F test in its own pad function
(`0x80076118`), which drops a multiplayer pickup at the player (below, DENNISMODE).

Back in the shell, `shell_init` (`0x8005FE04`) picks the first screen (`g_next_screen`,
`0x8007BA68`, looked up by name in the screen list `0x8007BA6C`):

| Condition | Screen |
|---|---|
| Cold boot (`0x80039C5C` set) | `m_movie_ea` (logos, then the title); defaults loaded (`game_defaults`, `0x80060B0C`) and `new_game` |
| Mode 1, exit 0 or 3, objectives complete (`g_level_end+0xC`), last level of the slot | `m_movie_g<slot+1>`: the mission's ending film, then on |
| Mode 1, exit 0 or 3, otherwise | `m_pmeval` (results) |
| Mode 3, exit 0 or 3 | `m_pmeval` |
| Mode 2 | `m_play` (multiplayer menu); the exit code is restored from `0x80039D20` |
| Anything else (quit, mode 1 with exit 1/2) | `m_title` |

### Results and recording them

`m_pmeval` shows the statistics (combat.md), the favourite weapon, and the rating
(`level_rating`, `0x8007344C`, levels.md), which it stores in `g_new_rating` (`0x8007C004`).
Then `m_postmis` offers SAVE, CONTINUE, REPLAY, WAR ROOM (with yes/no confirmations).

`progress_commit(advance)` (`0x800622D8`) records the level. It does nothing unless
`g_level_exit` is 0 or 3 and the mode isn't 3 (and it sets `g_level_exit` = 5, so it runs once):

1. Last level of the slot: the mission's **done** flag (`block+0x94`) = 1.
2. `carry_to_next_level` (`0x800625BC`): if the slot has a next level, `g_level_end`'s 13 ammo
   halfwords go to that level's row and its health (`g_level_end+8`) to the row's `+0x1C`.
   There is no carry from one mission to the next.
3. `career_add` (levels.md): career totals, kills per weapon, the best rating (kept if higher),
   `g_game+0xA0` = 1 (career has data).
4. `medals_award` (`0x800628A4`) and `mission_rewards` (`0x8006294C`), below.
5. advance = 1: `campaign_advance` (`0x80062498`): the slot's next level + 1, past the last level
   on to the next slot (5 → 7; after slot 7 it stays on slot 7's last level), `g_game+0xAC`
   updated, and that level unlocked. advance = 0: only unlocks the next level (the next slot's
   first level after a slot's last).

Who calls it (`postmis_input`, by the action code in `0x8007E85C`; which action is which button
is inferred from what each does): action 12 (CONTINUE) with advance 1 unless already recorded
(`0x8007E858`), then `level_begin(1)`; action 9 (WAR ROOM) with advance 1 when the objectives
are complete; action 6 (REPLAY) with advance 0 when the objectives are complete, then a slot's
first level goes through its briefing film `m_movie_b<slot+1>`, the others straight to
`m_brief`; action 2 (SAVE) goes to the codename/save screens. The briefing's last page quits
the shell into LEVEL.

## The save (memory card)

### The file

Written by `card_write_save` (`0x80045790`) from the request block `g_save_request`
(`0x8007A30C`, prepared by `save_prepare`, `0x80065764`), checked on a card written by the game
in a headless run and on the recording's save:

| Item | Value |
|---|---|
| File name | `BASLUS-00974` + the codename's first 6 characters, space-padded + a number (`%s%d`): 1 + the highest number among this codename's files already on the card, 0 if none. E.g. `BASLUS-00974TESTER0` |
| Size | One block (0x2000 bytes); at most 0x1E00 bytes of data accepted |
| +0x000 | Standard header: `SC`, icon flag 0x11/0x12/0x13 (1, 2 or 3 icon frames; the game's has 3), block count 1 |
| +0x004 | Title, 64 bytes of Shift-JIS converted from ASCII (`0x800462CC`): `MOH-<codename padded to 10> (<n>%)`, e.g. "ＭＯＨ−ＴＥＳＴＥＲ　　　　　（０％）" |
| +0x060 | Icon CLUT (32 bytes), +0x080/+0x100/+0x180 the icon frames (128 bytes each), from the save screen's resources |
| +0x200 | **`g_game`, 0x780 bytes**, a straight copy |
| +0x980.. | Whatever was in the allocated buffer (not cleared: the game's own file has junk there) |

n% = the number of levels with a best rating ≥ 0, over the 8 slots × 4 rows, × 100 / 24,
capped at 99; when saving from the post-mission menu, the level just played counts too if it
isn't rated yet (`0x8007E859`). (The recording's save, MSN1 LVL1-2 done: "(８％)".)

There is **no checksum**. Loading (`load_screen_update`, `0x8006E380`, through
`card_read_file`, `0x80045610`) accepts a file whose data starts with "MOH" and has "MOH" at
+0x77C (compared case-insensitively) and whose level (`+0x98`) is below 8; then it copies the
0x780 bytes over `g_game`, applies the sound settings (`0x8004DC40`), and `load_apply`
(`0x8006EB8C`) restores the slot and level (above) and sets `g_level_exit` = 5.

### `g_game` (0x780 bytes; the save's data)

Defaults from `game_defaults` (memset 0, then the values below) and `new_game`.

| Offset | Type | Meaning | Default |
|---|---|---|---|
| +0x000 | char[4] | "MOH" (checked on load) | |
| +0x004 | char[11] | Codename (≤ 10 letters, typed on the Enigma keyboard, `codename_input` `0x80075F20`) | "noname" |
| +0x00F | u8 | game_player_mode (printed; no writer found) | 0 |
| +0x010..+0x03C | s32 ×12 | Career totals (`career_add`, levels.md): shots, hits, wounds, objects destroyed, kills, head, torso, groin, ... | 0 |
| +0x040..+0x070 | s32 ×13 | Career kills per weapon (the weapon table's order) | 0 |
| +0x074 | s32 | CAPTAINDYE entered: **health carries over** (every level start and restart takes the level row's +0x1C instead of 128.0, `player_reset` `0x80061D08`); counts toward the DWI medal | 0 |
| +0x078 | u32 | Multiplayer characters available, bit per uniform entry (28) | 0x7F |
| +0x07C | u32 | Multiplayer arenas available, bit per arena entry (7) | 0x1F |
| +0x080 | s32 | Music ("background") volume → `g_params+9` | 4 |
| +0x084 | s32 | 0 stereo, 1 mono (`g_params+0xF` = stereo) | 0 |
| +0x088 | s32 | Sound effects volume (`g_params+0xA` = × 42) | 4 |
| +0x08C | s32 | Background: 0 music and ambience, else ambience only (`g_music_alt`, `0x80038658`: LEVEL then streams `Mm_lA.VB`, the track without its music, instead of `Mm_l.VB`; audio.md) | 0 |
| +0x094 | s32 | Mission index (0-based) | 0 |
| +0x098 | s32 | Level (0-based) | 0 |
| +0x09C | s32 | Lives (`g_params+0xC`, player.md's "life"); only printed: deaths are unlimited | 5 |
| +0x0A0 | s32 | Career has data (set by `career_add`; the evaluation screen) | 0 |
| +0x0A4 | s32 | Evaluation screen state (a random 0/1) | 0 |
| +0x0A8 | s32 | Campaign slot | 0 |
| +0x0AC | s32 | The slot's next level | 0 |
| +0x0B0, +0x0B4, +0x0B8 | s32 | Video options camera, shake, flash (`m_video`; not on the options menu, and nothing in LEVEL reads them) | 0 |
| +0x0D0 | u32 | Medals (below) | 0 |
| +0x0D4 | u32 | Cheats unlocked (bits 0..7, below) | 0 |
| +0x0D8 | u32 | Cheats on (`g_cheats`, player.md) | 0 |
| +0x0DC.. | s32 ×5 | Player 1: invert Y, auto aim, vibration, auto-centre (`m_ctrl_i_autoc`), "zoom" (`m_ctrl_zoom`) | 0, 1, 1, 0, 0 |
| +0x0F0.. | s32 ×5 | Player 2, the same | 0, 1, 1, 0, 0 |
| +0x104 | s32 | Player being configured | 0 |
| +0x108, +0x10C | s32 | Controller configuration of players 1, 2 (`m_config_i_1..5`; LEVEL prints "Using ANALOGn") | 1 |
| +0x110 | s8 | **Difficulty** (→ `g_params+0xB`) | 0, never changed |
| +0x112 | u8 | "perfect" (→ `g_params+0xD`; no writer found) | 0 |
| +0x113, +0x128 | u8 | Players 1, 2 DualShock | 1 |
| +0x114.., +0x129.. | u8 | Pad calibration of players 1, 2 (`m_calib`): per stick centre x,y, upper-left x,y, lower-right x,y | |
| +0x18C + m·0x98 | | **Mission blocks**, m = mission index 0..9 (10 blocks, to +0x77C) | |
| +0x77C | char[4] | "MOH" (checked on load) | |

A mission block (0x98 bytes): four level rows of 0x24 bytes, then two words.

| Offset | Type | Meaning |
|---|---|---|
| row + 0x00 | s16 ×13 | Ammo the level starts with, per weapon (-1: not carried; player.md's `g_level_ammo`) |
| row + 0x1A | s16 | Best rating 0..2, -1 never completed |
| row + 0x1C | 16.16 | Health the level starts with (128.0 in every mission's first row) |
| row + 0x20 | s32 | Level unlocked (the mission select offers it) |
| +0x90 | s32 | Mission's film unlocked in the gallery (mission complete, or a password) |
| +0x94 | s32 | Mission complete |

`shell_save.py` prints all of this for a card or RAM dump.

## Options

The options room (`m_option`) has PASSWORD, SECRET CODES, CONTROLLER, AUDIO, CREDITS (seen in a
headless run; the script also has `m_option_video`, which isn't shown). Menu items write their
values through pointers set up when the screen is built (item `+0x84` → the g_game field), so
the fields above are the options' storage:

- **Audio** (`audio_menu_init`, `0x8003D9D8`): Sound Effects (+0x88), Background (+0x80),
  Stereo/Mono (+0x84), Background "music and ambience" / "ambience only" (+0x8C).
- **Controller** (`ctrl_menu_init`, `0x80043DF0`): player select, configuration 1..5, invert Y,
  auto aim, zoom, auto-centre, calibration, vibration. In LEVEL (`0x8007A050`): invert Y and the
  configuration pick the stick setup (`0x80076240`); auto aim (`g_params+0x12`) makes
  `enemy_spawn` create soldiers with flags 0x285 instead of 0x85; field +0xEC ("zoom" by the
  menu's order, inference) switches the look limits between 0xD0/0x56 (and flag 0x400000) and
  0x40/0x1E; field +0xE8 (auto-centre, inference) clear sets flag 0x80000; vibration is read by
  `controller_manager`.
- **Difficulty: there is none.** Nothing in SHELL, LEVEL, LEVEL2P or the exe writes
  `g_game+0x110` (only `shell_exit` prints it and `level_read_parameters` copies it); no screen
  has a difficulty item. With combat.md/enemies.md (nothing reads `g_params+0xB`), difficulty
  is a dead field, always 0.

## Passwords and cheats

### Entering

Options > PASSWORD opens the Enigma keyboard (`m_enigma`):

```
Q W E R T Z U I O
A S D F G H J K L
<- P Y X C V B N M SEND
```

The d-pad moves, cross types, SEND submits (the same keyboard takes the codename). The word goes
to `password_enter` (`0x80048658`) → `password_find` (`0x800486A8`): a `strcmp` against the
table `g_passwords` (`0x8007B680`, 61 entries of 16 bytes: handler, 12-byte word), and the
handler runs with the entry's index. Two entries have conditions: CAPTAINDYE is refused once any
level has a rating, URLINGAMBE only works after SMPSMDMILK (`0x8007BA62`). Checked: typing
INVASION in a headless run set exactly `g_game+0x21C` (MSN1 block +0x90) to 1.

| # | Words | Handler | Effect |
|---|---|---|---|
| 0-7 | DENNISMODE, SPRECHEN, BRADALARM, TRACERON, ICOSIDODEC, GOBLUE, BADCOPSHOW, MOSTMEDALS | `0x80048830` | cheat bit # unlocked (`+0xD4`) |
| 8-15 | INVASION, BIGGRETA, DASBOOT, STUKA, KOMET, TWOSIXTWO, MISSLEAGE, VICTORYDAY | `0x80048850` | slot #-8's mission film unlocked in the gallery (block +0x90) |
| 16-19 | COOLCHICK, AJRULES, DWIGALLERY, DWIMOHTEAM | `0x800487B4` | `0x8007B67F` = 1 (the Enigma screen then leaves for `m_dwiemp`) and `0x8007E874` = 1, 2, 0, 3: a slide show of photo pages, `data\scr3\<set>\<a>\<b>.rsc` (TIMs `l`, `r` and one more, `dwi_page_load` `0x8006D050`); the page counter `0x8007E870` starts at 1, page n is a = (n−1)/10 + 1, b = n mod 10 (10 for 0). DWIGALLERY (0): `mdwiemp`, pages 1..17 (folders 1 and 2). DWIMOHTEAM (3): `mdwiteam`, pages 1..21. COOLCHICK (1): the one page `mdwiemp\15\1`; AJRULES (2): `mdwiemp\15\2`. Past the last page (`dwi_input` `0x8006D3C4`) it returns to `m_title` and resets the code to 0. (The pictures are DreamWorks Interactive staff, by the folder names: inference) |
| 20-27 | RETTUNG, ZERSTOREN, BOOTSINKT, SENFGAS, SCHWERES, SICHERUNG, EINSICKERN, GESAMTHEIT | `0x8004889C` | slot #-20's first level unlocked (row 0 +0x20). RETTUNG's slot 0 is open anyway; EINSICKERN (slot 6) does nothing |
| 28-55 | GUNTHER, JIMMY, ROBBIE, MANON, ULF, HELMUT, HEINRICH, WECANDOIT, EVILKURT, INTHENAVY, TIREDJOHAN, DASBOOTY, MANLYMAN, SCARYKARL, HOODDOWN, ROCKETMAN, ARTCRITIC, COOLSHADES, HOODUP, BIGFATMAN, FINESTHOUR, HERRZOMBIE, BEACHBALL, WOOFWOOF, IKEANDKEN, NUTCRACKER, SSPIELBERG, PAYBACK | `0x800488F4` | multiplayer character #-28 (`+0x78` bit) |
| 56-57 | MACOOCOO, SPYSCHOOL | `0x80048918` | multiplayer arena 5, 6 (`+0x7C` bit) |
| 58 | SMPSMDMILK | `0x8004893C` | arms URLINGAMBE |
| 59 | URLINGAMBE | `0x8004893C` | runs handlers 0..57 with their indices: everything above |
| 60 | CAPTAINDYE | `0x80048A00` | `g_game+0x74` = 1 (fresh games only) |

### The cheats (`+0xD4` unlocked, `+0xD8` on)

Options > SECRET CODES (`m_secret`, `secret_menu_init` `0x80067B38`, `secret_menu_input`
`0x80067C74`) shows one item per unlocked bit (item # = bit #) and toggles it in `+0xD8`. A cheat
is unlocked by its password, or by play: two stars (rating 2) on every level of slot s unlocks
bit s (slots 0..5; slot 7 → bit 6), and the "all seven mission medals" medal unlocks bit 7
(`mission_all_two_stars` `0x80062B48`, `medals_award`). `level_begin` keeps only bits 0 and 1 on
when starting a level that has never been completed.

| Bit | Password | Unlocked by | Effect (where read) |
|---|---|---|---|
| 0x01 | DENNISMODE | MSN1 all two stars | Multiplayer only (LEVEL2P, `mp_drop_spawn` `0x8008175C`): a dying player normally drops a **PLAYER AMMO** pickup holding their ammo (model 0x4EFA45A0); with the cheat it drops a random **power-up** instead (below). Nothing in LEVEL reads the bit |
| 0x02 | SPRECHEN | MSN2 | `level_entry` sets `g_taf_variant` (`0x8009A618`) = 1: the level loads `m_l1.TAF`, the other language's sound set (formats.md) |
| 0x04 | BRADALARM | MSN3 | **Silly alarms**: `alarm_spawn` (`0x8005EA54`, the alarm entity of audio.md) replaces the alarm's sound with one of 25 at random (`0x8003C2E0`): bank 0 sounds 0x0C, 0x0D (flesh hits), 0x0F (footstep), 0x10, 0x11 (casings), 0x12, 0x13, 0x19, 0x1A (falls), 0x1B, 0x1C (grunts), 0x1D, 0x1E, 0x1F (health), 0x20 (ammo), 0x21, 0x22, 0x23, 0x2C (the real bell), 0x32 (message beep), or (entries 20..24) the soldier voice bank's 0x00, 0x01, 0x04, 0x05, 0x1F (bank from model 0xB8534EAA's `+0xC8`). Half the time the pitch is also randomised: 0x2000 + rand·0x2EE0/32768 (0.125..0.31) for voice-bank sounds, 0x19, 0x1A, 0x1F, and 0x2C in mission 2 or a mission's first level (`g_params` +0/+4 = 2/1, 1-based as in audio.md's alarm pitches), else 0x4000 + rand·0x4E20/32768 (0.25..0.56); otherwise the caller's pitch, without the per-mission pitch the normal alarm gets |
| 0x08 | TRACERON | MSN4 | `level_entry` sets `0x800AA174` = 1: `tsp_draw` draws the world with `0x80089D68`, red wireframe (seen in a headless run with the bit poked) |
| 0x10 | ICOSIDODEC | MSN5 | Faster refire (player.md) |
| 0x20 | GOBLUE | MSN7 | Larger player bullets (player.md) |
| 0x40 | BADCOPSHOW | MSN9 | Unlimited ammo: every slot starts (and restarts) with its reserve at the weapon's maximum (`player_reset_inventory` `0x80061A2C`), reloads don't take from the reserve (`0x800460F4`), thrown weapons don't use ammo (player.md) |
| 0x80 | MOSTMEDALS | medal 0x800 | The player takes no damage (player.md, combat.md) |

**DENNISMODE's power-ups.** When a multiplayer player dies (`0x80074A2C`, contact flag 0x4000),
LEVEL2P spawns a pickup at the body. Its template comes from a table of 14 in LEVEL2P's data
(`0x8009AF38`, 0x34 bytes each: model hash, ..., a 32-byte name, a colour). Without the cheat
it's entry 0, PLAYER AMMO, loaded with the dead player's reserve + clip per slot (`+0x24`
halfwords, slots up to 4). With the cheat, `rand() & 31` picks:

| Name | Model | Chance /32 | Name | Model | Chance /32 |
|---|---|---|---|---|---|
| 4X FIRE RATE | 0xE4F2812F | 2 | AIR WALKING | 0x30C394AB | 3 |
| ACHILLES MODE | 0x0BD11E86 | 2 | SWAP AMMO | 0x5EDAF160 | 1 |
| REFLECTING SHOTS | 0xD7D56DF3 | 3 | FROZEN | 0xB669339C | 2 |
| 4X DAMAGE | 0x82292D75 | 3 | SLOW | 0x03929C69 | 3 |
| HALF DAMAGE | 0x7995EE22 | 2 | REVERSE CONTROLS | 0x98CD33D8 | 2 |
| FORCE | 0x745C9F92 | 2 | RANDOM CONTROLS | 0x839771D3 | 3 |
| BLINK | 0xC56C576D | 3 | (13: PLAYER AMMO, empty) | 0x4EFA45A0 | 1 |

with a second hash picked by which of the two players died (0x7FC2236C or 0xE6CB72D6, both in
every MSN12 BSD).
What each power-up does when picked up is LEVEL2P code that wasn't followed (out of scope: the
two-player game).

## Medals (`g_game+0xD0`)

`medals_check(arg)` (`0x80062C6C`) returns the medals newly earned; `medals_award` ORs them in.
For each slot s (not 6), with the mission complete (or completing now) and at least
(levels − 1) of its levels at two stars:

| Bit | Medal (screen item) | Condition |
|---|---|---|
| 1 << s (bits 0-5, 7) | `m_medals_1..7`, one per mission | the above |
| 0x200 | `m_medals_brz` | every mission complete (7) and MSN9 complete (or its last level being finished now) |
| 0x400 | `m_medals_sil` | ... and 4 or more mission medals |
| 0x800 | `m_medals_moh` | ... and all 7 mission medals → also unlocks cheat bit 7; gallery: `m_movie_moh_medal` |
| 0x100 | `m_medals_dwi` | ... and every level of every mission at two stars, or CAPTAINDYE entered → also unlocks multiplayer characters 21..27; gallery: `m_movie_mov_medal` |

The medal case (`m_medals`, `0x80069EE4`) shows items 0..10 for bits 0-5, 7-11; with 0x800 it
hides the bronze (and with 0x400 likewise, by the code's shape: inference).

Completing a mission (`mission_rewards`) also unlocks its gallery film and multiplayer
characters/arenas:

| Slot completed | Characters (bits of +0x78) | Arenas (+0x7C) |
|---|---|---|
| 1 (MSN2) | 7, 8 | |
| 2 (MSN3) | 9-12 | |
| 3 (MSN4) | 13 | 5 |
| 4 (MSN5) | 14, 15 | |
| 5 (MSN7) | 16-19 | |
| 7 (MSN9) | 20 | 6 |

The gallery (`gallery_init` `0x8004E664`, `gallery_input` `0x8004E9D4`) offers film i (0..7,
`m_movie_g<i+1>`) when slot i's mission block +0x90 is set (film 6 also when MSN7's is, or
MSN9's third row is unlocked), film 8 with medal 0x800 and film 9 with medal 0x100.

## Screens and texts

The menus are data: each screen is a script `global\script\m*.sst` in `DATA\GLOBAL.RSC`, loaded
by `sst_load` (`0x80049E90`). RSC: name[0x40], count, ?, then count × {name[0x40], index, size,
offset, 0}. A script is a stream of tagged records: 1 screen (name), 2 button (name, the screen
it leads to), 3 sprite (a TIM), 10/11 3D models and their animations (the title's clerks), 8
widgets, and others; the sizes are in `shell_sst.py`. A button's target is followed unless the
screen's code handler (set by the screen's init function, e.g. `title_input`) decides otherwise.
The graph `shell_sst.py` prints, in short:

- `m_movie_ea` → `m_movie_dwi` → `m_movie_moh` → `m_title`; `m_movie_b1..8` (briefing films) →
  `m_brief`; `m_movie_g1..8` and the two medal films → `m_gallery`.
- `m_title`: `n_mission` (continue the campaign, inference: not shown on a fresh game), new game
  → `m_pname`, war records → `m_war`, briefing (the slot's briefing film), multiplayer →
  `m_play`, gallery → `m_gallery`, options → `m_option`. Left idle it plays `m_movie_demo`:
  the title's input reader (`title_read`, `0x8005A430`) counts `0x8007BBD0` down once per
  frame from 960 (reset to 960 by any input) and at 0 returns event 10.
- **`m_dev` is unreachable.** `title_input` (`0x80059030`, called with the reader's event) sets
  `0x8007BBB8` = 1 on event 5, and with that set the next title item chosen (unless the demo
  timer fired) goes to `m_dev` instead of its own target
  (`data\scr2\mdev.rsc`, init `0x8006FCC4`, a mission/level picker whose handler is
  presumably `dev_level_start`, mode 3). But `title_read` only returns 0, 2, 3, 4 and 10, and
  nothing else writes `0x8007BBB8` (cleared by the title's init `0x80058CC8`): event 5 and the
  developer menu are left over from development. Mode 3 therefore never happens in the retail
  game.
- `m_war`: mission log → `m_mission`, personal records → `m_commend` (→ `m_medals`, `m_eval`),
  load → `m_load`, save → `m_save`, new game → `m_pname`.
- `m_play`: start, game type/weapons/arena → `m_multopt`, map room, uniforms → `m_uniform`.
- `m_option`: `m_enigma`, `m_secret`, `m_ctrl` (→ `m_config`, `m_calib`), `m_audio`,
  (`m_video`), `m_movie_credit`.
- `m_pmeval` → `m_postmis` (save → `m_save`; continue, replay, war room → `m_title`).

**Texts.** Almost all menu text is pre-drawn in TIMs: button labels and pages are sprites in the
screen scripts, the briefing is three pages of TIMs per level
(`data\scr2\mbrief\m<M>\<L>.rsc`: `mbrief\mM\lL\{1,2,3}{l,r,s}.tim`), and the slide shows are
`data\scr3\mslide\mM\N.rsc`. The only text drawn from strings is numbers (the statistics) and the
codename. In a level, the objectives and messages come from the level's message table (levels.md).

## Open questions

- What each DENNISMODE power-up does in LEVEL2P (out of scope here).
- The third TIM of a DWI gallery page and how the pages advance (inference: Cross, like the
  slide shows).
- The soldier's flags A 0x100000 that gives the instant death restart with a white flash: what
  sets it wasn't looked for.

## Names

For names.txt (the exe's data addresses go to its section; the rest per binary):

```
# --- SLUS_009.74 (exe) ---
800385D0 g_game_codename            # g_game+4, char[11]
8003869C g_medals                   # g_game+0xD0: bits per mission (1<<slot), 0x100 DWI, 0x200 bronze, 0x400 silver, 0x800 MoH
800386A0 g_cheats_unlocked          # g_game+0xD4: the secret-codes menu shows these bits
80039C58 g_level_exit               # LEVEL's exit code: 0/3 finished, 1 quit, 2 ?; shell: 4 new game, 5 recorded/loaded
80039C5C g_cold_boot
80039C74 g_mp_arena                 # multiplayer arena (menu order); the exe maps it to the MSN12 level

# --- SHELL.BIN ---
80045610 card_read_file
80045790 card_write_save            # SC header, title, icon, then the data at +0x200
800462CC title_to_sjis              # (inference) ASCII save title -> Shift-JIS
800486A8 password_find              # strcmp over g_passwords; CAPTAINDYE/URLINGAMBE conditions
80048658 password_enter
80048830 pw_cheat                   # +0xD4 |= 1 << index
80048850 pw_gallery                 # slot (index-8) mission block +0x90 = 1
800487B4 pw_dwi_gallery
8004889C pw_mission                 # slot (index-20) first level unlocked
800488F4 pw_mp_character            # +0x78 |= 1 << (index-28)
80048918 pw_mp_arena                # +0x7C |= 1 << (index-51)
8004893C pw_all                     # SMPSMDMILK arms, URLINGAMBE runs handlers 0..57
80048A00 pw_captaindye              # g_game+0x74 = 1
80049E90 sst_load                   # screen script records -> screens, buttons, sprites
8004E664 gallery_init
8004E9D4 gallery_input
8004FF60 mission_select_input
80050C38 level_unlocked             # (menu mission 1..7, level 1..): row +0x20
80050CAC level_best_rating          # (menu mission, level): best rating + 1
80057FB8 mp_start                   # level_begin(2), quits the shell
80059030 title_input
8003D9D8 audio_menu_init
8003F6EC brief_input                # last page: g_shell_quit
80043DF0 ctrl_menu_init
8005FC28 shell_main                 # the overlay's entry
8005FE04 shell_init                 # picks the first screen from the play mode and the level exit code
80060B0C game_defaults              # g_game cleared, options' defaults
80060C7C campaign_init              # g_campaign: 8 slots
80060D24 shell_exit                 # prints "Shell game settings after shell quits"
80061F00 new_game                   # progress reset, MSN1 LVL1 unlocked
800622D8 progress_commit            # (advance): record the level's results, unlock/advance
80062498 campaign_advance
800625BC carry_to_next_level        # g_level_end ammo + health -> the slot's next level row
800628A4 medals_award
8006294C mission_rewards            # two-star cheat unlock, gallery, MP characters/arenas
80062B48 mission_all_two_stars
80062C6C medals_check
80065764 save_prepare               # file name, title, percentage
80066830 video_menu_init
80067B38 secret_menu_init
80067C74 secret_menu_input          # toggles g_cheats bits
80069EE4 medals_screen_init
8006B664 postmis_input              # save / continue / replay / war room
8006E380 load_screen_update         # validates "MOH" at 0 and 0x77C, copies 0x780 bytes to g_game
8006EB8C load_apply
8007064C dev_level_start            # (inference) level_begin(3)
80075F20 codename_input
8007A30C g_save_request             # 0x94 bytes: name, data, size, title, icon pointers
8007B680 g_passwords                # 61 x {handler, char[12]}
8007BA68 g_next_screen
8007BA6C g_screens                  # screen list, linked through +0x80
8007BFFD g_shell_quit
8007C004 g_new_rating               # the level's rating, from level_rating

# --- LEVEL.BIN ---
80062298 level_exit                 # (reason): g_level_exit, level_end_carry when finished
```
