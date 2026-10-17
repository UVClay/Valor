# Valor

Static recompilation of *Medal of Honor* (PlayStation, 1999) and *Medal of Honor: Underground* to native x86_64.
First goal: play it on PC exactly like it played on the console, with a PlayStation controller.

You need your own copy of the game. No game data is included in this repository.

## Setup

Requires only [Podman](https://podman.io/). Everything else lives in the container.

```sh
# put your BIN/CUE under disc/, then:
./dev python3 tools/extract_disc.py "disc/Medal of Honor (USA)/Medal of Honor (USA).cue" extracted/moh
```

`./dev <cmd>` runs any command in the dev container (`./dev` alone opens a shell).
The image is built automatically on first use.

Supported image: Medal of Honor (USA), SLUS-00974, SHA1 `cf503f1dab8d6f9ae25d11b324b7c251651f5fff`.

See `docs/RECON.md` for reverse-engineering notes.

## License

GPL-2.0-or-later. See `LICENSE`.
