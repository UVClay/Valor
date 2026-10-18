# Recon notes — Medal of Honor (USA), SLUS-00974

Disc image SHA1: `cf503f1dab8d6f9ae25d11b324b7c251651f5fff`

## Executables

| File | Load addr | Size | Contents |
|---|---|---|---|
| `SLUS_009.74` | `0x80010000` (text), entry `0x8001DFD4` | 0x2A000 | Overlay manager, PsyQ libs, `CDIO.CPP` |
| `SHELL.BIN` | `0x80039D90` | 0x55EA8 | Front end / menus |
| `LEVEL.BIN` | `0x80039D90` | 0x90510 | Gameplay (1 player) |
| `LEVEL2P.BIN` | `0x80039D90` | 0x7C620 | Gameplay (split-screen) |

- Overlays are raw images (no header beyond an 8-byte prefix: id word 7/5/6, then 0)
  loaded over the tail of the exe at `0x80039D90`; only one is resident at a time.
- Game code is C++ (`CD.CPP`, `CDIO.CPP`, `objects.cpp`, `tsp.cpp`).
- PsyQ: `sys.c` v1.140 (1998-01-12), `intr.c` v1.75, `bios.c` v1.86.
- Initial stack `0x801FFF00` (SYSTEM.CNF), TCB 4, EVENT 16.

## Overlay manager

Overlay table at `0x800385A8`, 3 entries of 12 bytes `{char *name, char *file, void (*entry)()}`:

| idx | name | file | entry |
|---|---|---|---|
| 0 | `SHELL` | `SHELL.BIN` | `0x8005FC28` |
| 1 | `LEVEL5` | `LEVEL.BIN` | `0x80064374` |
| 2 | `LEVEL5_2P` | `LEVEL2P.BIN` | `0x8005F280` |

Main loop at `0x80037060`: system init → `func_80037318(table[i].file, 0x80039D90)` (load
file) → more init → `jalr table[i].entry` → teardown (root counters `0xF2000000..2`) → repeat.
Overlays return to the exe, so each can be recompiled as an independent module.

## PsyQ libraries

`tools/psyq/fetch_sigs.py` + `tools/psyq/match_sigs.py` (signatures from
lab313ru/psx_psyq_signatures, versions 4.0–4.7, combined; strongest non-ambiguous match wins).

| Binary | Coverage | Notes |
|---|---|---|
| `SLUS_009.74` | 52% (189 objects + 63 single functions, 280 symbols) | Mixed lib versions, mostly 4.1-era objects plus some 4.6 |
| `SHELL.BIN` | 4% | Memory card (LIBMCRD/LIBCARD), MDEC VLC (LIBPRESS), a few LIBGS helpers |
| `LEVEL.BIN` / `LEVEL2P.BIN` | ~0% | No library code: all game code |

Key addresses: `VSync 0x80015DB8`, `ResetGraph 0x80016B60`, `DrawSync 0x80016E38`,
`LoadImage 0x8001704C`, `DrawOTag 0x800172D8`, `PutDrawEnv 0x80017348`, `PutDispEnv 0x80017408`,
`InitGeom 0x8001BAA0`, `SsInit 0x8001582C`.

Not matched yet: `PadInit`/`CdInit`/`SpuInit` (game may use lower-level APIs; CD access goes through libds).

## Player control and movement (LEVEL.BIN)

Pad input:
- The raw pad buffers are at `0x800D0C88`.
- The game copies the stick bytes per port to `0x80093B70 + port*4` in the order rx, ry, lx, ly.
- The analog-mode flag is at `0x80093B78 + port`.
- The controller manager is `0x8004D974`.

Each control role (forward, strafe, ...) is read from a digital button (`0x80075A30`, with its config at
`0x800B75F0`, role×52, port×208) or, in analog mode, from a stick axis:
`0x80075FB4(port, role, ?)` → `0x80075E7C(port, cfg, axis, enabled)`. The analog config is at
`0x800B74B0 + port*160 + role*40`:

| Offset | Default | Meaning |
|---|---|---|
| +0x0 (byte) | role 0: 3, role 1: 0, role 2: 1, role 3: 2 | Stick axis (index into the stick copy) |
| +0xE (byte) | 0x5B | Low threshold |
| +0x14 | 360 | Negative-side scale |
| +0x18 | 199 | Positive-side scale |
| +0x20 | 0x230000 (forward/strafe), 0x3C0000 (look) | Speed, used as `speed>>8` |
| +0x24 | | Invert |

With the default config:
- Roles 0/1/2/3 are forward (ly), strafe (rx), pitch (ry) and turn (lx).
- Strafe can also come from the L1/R1 buttons.

Axis response, per axis with no cross-axis coupling:

| Raw value `v` | Index | Output |
|---|---|---|
| `v < 0x5B` | `(0x5B - v) * 360 >> 8` | `-table[idx] * (speed >> 8)` |
| `v >= 0xA6` | `(v - 0x5B) * 199 >> 8` | `+table[idx] * (speed >> 8)` |
| otherwise | | 0 |

`table` is the 128-entry curve at `0x8009E3AC`. It is 0 up to index 36, then rises steeply to 255.

The result is a huge deadzone, about 50% of the travel on the negative side, plus a steep curve. Forward speed by deflection: 100% → 17.4 units per frame, 88% → 7.5, 71% → 3.0, 50% → 0.6.

`0x8007F300` is the per-frame control function, called from `0x8007A6B0`:
- Role 0 → `player+0x200` (negated).
- Role 1 → `player+0x218`.
- Roles 2/3 feed the look accumulators `obj+0x17C/0x188` → angles `obj+0x190/0x192`, clamped by `obj+0xB4/0xB6`.

The global player pointer is `0x8009D638`, with `obj = *(player+0x388)`. Position is 16.16 fixed point at `player+0x20C/0x210/0x214`; the integer part is mirrored at `player+0x354/0x358/0x35C`.

Measured:
- Forward and strafe add as vectors: full forward plus full strafe moves at 24.6, which is √2 × 17.4. Nothing in the movement code cancels forward speed while strafing.
- The "strafing kills forward movement" feel comes from the per-axis curve. On a round stick gate, holding the stick fully deflected 30° off forward gives (0.5, 0.87). That is 7.5 units per frame instead of 17.4.
- A DualShock reports a square range, so its diagonals reach both axes' limits. The runtime reproduces that by mapping round gamepad input onto the square range (`square_sticks`, default on). At 30° the speed is then 17.9.

Actions (fire, reload, ...):
- Button masks per action are at `0x800B7790 + (port*9 + action)*4`, with an optional second mask at `0x800B77E0` (same layout). Masks have the pad's two bytes swapped (Cross is `0x40`, Start `0x800`).
- `0x8007570C(port, action)` tests whether an action is held, and `0x80075778(port, action)` whether it was just pressed. Pad state is at `0x80093B7C + port*4` and the previous frame's at `0x80093B84`.
- Default buttons: 0 fire (Cross), 1 jump (Triangle), 2 next weapon (Circle), 3 crouch (L2), 4 reload (Square), 5 use (Square), 6 Start, 7 L3, 8 aim mode (R2).

Look roles, measured:
- A turn role value is added to the yaw as is, in 16.16 angle units with 4096 units per turn.
- Pitch gets half of the value and is clamped to about ±53°.
- Neither springs back to center.

Other state:
- `0x8009A5E0` is non-zero while the pause screen is up. It is set by the Start handler `0x800625BC`.
- `*(0x8009D654) + 0x10`: bit `0x10` is set while in aim mode, where the game draws its own red reticle.
- The overlay at `0x80039D90` identifies itself by its first word: 7 is SHELL, 5 is LEVEL, 6 is LEVEL2P.

## Frame timing (LEVEL.BIN)

The simulation is a fixed 30 Hz tick:
- `0x80090B90` presents a frame. It waits for at least 2 vblanks since the last present, the vblank count from `VSync(-1)` being stored at `0x80097FCC`.
- It stores the number of extra 30 Hz ticks that elapsed (frames dropped) at `0x8009A5D0`.
- The main loop (around `0x800617E0`) caps that at 3, copies it to `0x8009A5D4`, and runs `0x8004DE3C` once per missed tick to catch up.
- Logic advances in whole 30 Hz steps, so movement is about 35 units per tick at full speed. The emulated hardware itself runs at 60 Hz.

A higher frame rate therefore means rendering interpolated frames between ticks, not running the game logic faster.

## Asset formats (unexplored)

`.RSC` resource archives, `.BSD`/`.TSP` level data, `.TAF` (unknown), `.VB` SPU sample banks,
`.STR` MDEC video (Form 2 sectors), `.TIM` images.

## Open questions

- How overlay index for the next iteration is chosen.
