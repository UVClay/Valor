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
`./play --warp 3:2` makes New Game start in mission 3, level 2 (for testing).
`./dev <cmd>` runs any command in the dev container (`./dev` alone opens a shell).

Supported image: Medal of Honor (USA), SLUS-00974, SHA1 `cf503f1dab8d6f9ae25d11b324b7c251651f5fff`.

## Controls

Any SDL-supported controller works (DualShock/DualSense map 1:1).

Mouse and keyboard: in a mission the mouse looks around (captured while playing) and keys are
bound to the game's actions, whichever controller configuration is selected in its options.
Rebind them in F1 → Controls.

| Action | Default |
|---|---|
| Move / strafe | W A S D |
| Fire / aim mode | Left / right mouse button |
| Reload / use | R / E |
| Jump / crouch | Space / C |
| Next weapon | Mouse wheel down |
| Center view (the game's L3) | V |
| Pause | Esc |
| Menus | Arrows or WASD, Enter or Space accepts, Backspace goes back |

| Runtime | Key |
|---|---|
| Settings menu | F1 |
| Screenshot (into `screenshots/`) | F12 |
| Debug overlay: off / FPS / extended stats | F3 |
| Analog mode on/off (the DualShock's Analog button) | F7 |
| Fast-forward (hold) | Tab |
| Volume down / up | - / = (or `./play --volume 60`) |

## Saves

Saves are kept as single-save files (`.mcs`, readable by other PlayStation tools) in
`saves/card1/`. The game sees an ordinary memory card made of them. A save the game deletes is
moved to `saves/card1/deleted/`. A previous `saves/memcard1.mcd` is imported the first time and
left as it was. `--memcard FILE.mcd` uses a card image instead.

## Settings

F1 opens the settings menu (the game pauses). Changes are saved to `saves/settings.ini` and can
also be given on the command line:

- **Resolution scale:** `--scale N`.
- **Aspect ratio:** `--aspect 16:9`.
- **Horizontal FOV:** `--fov DEG`. Wide views are fully supported: the game's culling is widened
  and polygons reaching behind the camera are clipped, so nearby walls don't vanish at the edges.
- **High frame rate:** on by default; `--no-interpolate` turns it off. The game runs at 30 Hz, and
  every frame in between is drawn again by the game's own drawing code with the camera where it
  should be at that moment (`--coupled` moves the tick's frame instead). It presents at the
  display's refresh rate or at `--frame-rate N`.
- **Square stick range:** maps modern round-gated sticks to the DualShock's range.
- **Sub-pixel precision:** `--subpixel` (off by default) removes the PS1's vertex wobble.
- **Fast loading:** `--fast-loading` (off by default) reads game data 8x faster; a level loads in
  about 2 seconds instead of 10. Music and movies stream as usual.
- **Texture filtering:** `--texture-filter` (off by default) smooths 3D textures (bilinear); the HUD
  and 2D screens stay sharp.
- **Depth buffer:** `--depth-buffer` (off by default) hides what's behind walls by real depth
  instead of the game's polygon sorting, which can show things through nearby walls.
- **Widescreen HUD:** keeps the HUD's proportions, with the compass and ammo count in the corners.
- **Crosshair:** `--crosshair SIZE` (hidden in aim mode and on the pause screen).
- **Mouse:** sensitivity (Quake scale: 0.022 degrees per count at 1), invert, key bindings. The
  view follows the mouse at the display's frame rate, not the game's 30 Hz.
- **Controller:** modern stick aiming (`--stick-aim`): a small deadzone and smooth curve instead of
  the game's, with turn speeds. Gyro aiming (DualSense and other controllers with a gyro): always,
  or only in aim mode.
- **Pause in the background** (on by default) and **volume and debug overlay.**

## Testing

- `./dev tools/sweep.sh [MINUTES] [SEED]` plays every level with random input, headless and
  unthrottled, and reports any crash (logs in `build/sweep/`).
- `./dev tools/monkey.sh [SEEDS] [MINUTES]` runs several random-input sessions from New Game.
- `tools/warp.sh MISSION LEVEL [args]` starts a level directly (headless runs, scripting).

## Recording a play session

`./play --record recordings/NAME` (with `--warp` too, if you like) records what you play: the
controls as the game read them, the moment of every frame shown, and the settings and memory card
you started with. `./play --replay recordings/NAME --video build/NAME.mkv` plays it back
(hidden, much faster than real time) into a video of every frame: `.mkv` is lossless,
`.mp4` or `.webm` smaller (AV1). `--video-range FROM:TO` keeps only those vblanks. The video
needs ffmpeg on the host. Don't change settings (F1) while recording: the replay uses the ones
it started with. The replay checks it is still running the same game and says so if not.

## How it works

The game's MIPS code (exe and its three overlays) and [OpenBIOS](third_party/nugget/openbios)
are statically recompiled to C; the runtime emulates the PlayStation hardware underneath
(GPU, SPU, CD-ROM, MDEC, GTE, DMA, timers, controller port). See `docs/DESIGN.md` and
`docs/RECON.md`.

## License

GPL-2.0-or-later. See `LICENSE`.
