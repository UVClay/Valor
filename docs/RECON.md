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

## Asset formats (unexplored)

`.RSC` resource archives, `.BSD`/`.TSP` level data, `.TAF` (unknown), `.VB` SPU sample banks,
`.STR` MDEC video (Form 2 sectors), `.TIM` images.

## Open questions

- Simulation tick model (fixed vs. variable frame delta).
- PsyQ library function identification (signature matching).
- How overlay index for the next iteration is chosen.
