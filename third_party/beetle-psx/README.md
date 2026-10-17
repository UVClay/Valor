# Beetle PSX SPU (vendored)

`psx/spu.c` and `psx/spu.h` are unmodified copies from
[libretro/beetle-psx-libretro](https://github.com/libretro/beetle-psx-libretro) at commit
`9568c66b7386fa928b433f29e3daf7b8ee0f0031` (`mednafen/psx/`). They are Mednafen's SPU emulation,
licensed GPL-2.0-or-later (see the file headers).

The headers in this directory (`mednafen-types.h`, `state.h`, `state_helpers.h`, `libretro.h`,
`psx/irq.h`, `psx/cdc.h`) are small shims written for Valor so the SPU builds outside Mednafen;
they connect it to Valor's interrupt controller and CD-ROM audio. The SPU's save-state code is
compiled out.

To update: replace the two files from a newer commit and update the hash above.
