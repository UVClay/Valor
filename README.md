# Valor

Static recompilation of *Medal of Honor* (PlayStation, 1999) and *Medal of Honor: Underground* to native x86_64.
First goal: play it on PC exactly like it played on the console, with a PlayStation controller.

You need your own copy of the game. No game data is included in this repository.

## Setup

Requires only [Podman](https://podman.io/). Everything else lives in the container.

```sh
git clone --recursive https://github.com/UVClay/Valor.git
# put your BIN/CUE under disc/, e.g. "disc/Medal of Honor (USA)/Medal of Honor (USA).cue"
./dev python3 tools/extract_disc.py "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" extracted/moh
./dev python3 tools/psyq/fetch_sigs.py
for f in SLUS_009.74 SHELL.BIN LEVEL.BIN LEVEL2P.BIN; do
    ./dev python3 tools/psyq/match_sigs.py extracted/moh/$f --out build/syms/$f.psyq.txt
done
./play
```

`./play` builds everything (first build takes a minute) and runs the game in a window, with
your display, audio and controllers passed into the container.
`./dev <cmd>` runs any command in the dev container (`./dev` alone opens a shell).

Supported image: Medal of Honor (USA), SLUS-00974, SHA1 `cf503f1dab8d6f9ae25d11b324b7c251651f5fff`.

## Controls

Any SDL-supported controller works (DualShock/DualSense map 1:1). Keyboard fallback:

| PlayStation | Keyboard |
|---|---|
| D-pad | Arrow keys |
| Cross / Circle / Square / Triangle | X / C / Z / S |
| L1 / R1 / L2 / R2 | Q / E / 1 / 3 |
| Start / Select | Enter / Backspace |
| Fast-forward (hold) | Tab |

## How it works

The game's MIPS code (exe and its three overlays) and [OpenBIOS](third_party/nugget/openbios)
are statically recompiled to C; the runtime emulates the PlayStation hardware underneath
(GPU, SPU, CD-ROM, MDEC, GTE, DMA, timers, controller port). See `docs/DESIGN.md` and
`docs/RECON.md`.

## License

GPL-2.0-or-later. See `LICENSE`.
