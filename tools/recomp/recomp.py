#!/usr/bin/env python3
"""Static recompiler driver.

Usage:
  recomp.py <config.toml> analyze          print function discovery stats
  recomp.py <config.toml> gaps [image...]  list unclaimed regions between functions
  recomp.py <config.toml> generate <outdir> write C sources for all modules
"""
import re
import struct
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze import discover, find_gaps  # noqa: E402
from codegen import CodegenError, fn_name, generate_header, generate_module  # noqa: E402
from mips import Image  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SYMBOL_LINE = re.compile(r"^(\w+)\s*=\s*0x([0-9A-Fa-f]+);")


def read_symbols(paths):
    syms = {}
    for p in paths:
        path = ROOT / p
        if not path.exists():
            print(f"warning: symbol file {p} missing (run tools/psyq/match_sigs.py)", file=sys.stderr)
            continue
        for line in path.read_text().splitlines():
            if m := SYMBOL_LINE.match(line):
                syms[int(m.group(2), 16)] = m.group(1)
    return syms


class Game:
    def __init__(self, config_path: Path):
        cfg = tomllib.loads(config_path.read_text())
        self.cfg = cfg
        extracted = ROOT / cfg["extracted"]
        exe_cfg = cfg["exe"]
        raw = (extracted / exe_cfg["file"]).read_bytes()
        if raw[:8] != b"PS-X EXE":
            sys.exit(f"{exe_cfg['file']} is not a PS-X EXE")
        self.entry, _gp, t_addr, t_size = struct.unpack_from("<IIII", raw, 0x10)
        self.exe_raw = raw
        code_end = exe_cfg.get("code_end", t_addr + t_size)
        self.images = {exe_cfg["name"]: Image(exe_cfg["name"], t_addr, raw[0x800:0x800 + code_end - t_addr])}
        self.seeds = {exe_cfg["name"]: {self.entry, *exe_cfg.get("seeds", [])}}
        self.symbols = {exe_cfg["name"]: read_symbols(exe_cfg.get("symbols", []))}
        for ov in cfg.get("overlay", []):
            data = (extracted / ov["file"]).read_bytes()
            self.images[ov["name"]] = Image(ov["name"], cfg["overlay_base"], data)
            self.seeds[ov["name"]] = {ov["entry"], *ov.get("seeds", [])}
            self.symbols[ov["name"]] = read_symbols(ov.get("symbols", []))

        # Library symbols that look like functions are seeds too.
        for name, syms in self.symbols.items():
            img = self.images[name]
            for addr in syms:
                if img.contains(addr):
                    self.seeds[name].add(addr)

    def analyze(self):
        return discover(self.images, self.seeds)


def cmd_analyze(game: Game, *_):
    funcs, errors = game.analyze()
    for e in errors:
        print("error:", e)
    for name, fs in funcs.items():
        img = game.images[name]
        code = sum(len(f.addrs) * 4 for f in fs.values())
        jt = sum(len(f.jump_tables) for f in fs.values())
        bios = sum(len(f.bios_jumps) for f in fs.values())
        dyn = sum(len(f.dynamic_jumps) for f in fs.values())
        gaps = [(s, e) for s, e in find_gaps(img, fs) if e - s >= 16]
        gap_bytes = sum(e - s for s, e in gaps)
        print(f"{name:8} {len(fs):5} functions, {code:#8x} code bytes of {len(img.data):#8x}, "
              f"{jt} jump tables, {bios} BIOS jumps, {dyn} dynamic jr, {len(gaps)} gaps ({gap_bytes:#x} bytes)")


def cmd_gaps(game: Game, *names):
    funcs, _ = game.analyze()
    for name in names or funcs:
        img = game.images[name]
        print(f"== {name}")
        for s, e in find_gaps(img, funcs[name]):
            if e - s < 16:
                continue
            ws = [img.word(a) for a in range(s, e, 4)]
            prologues = sum(1 for w in ws if w >> 16 == 0x27BD and w & 0x8000)
            returns = ws.count(0x03E00008)
            zero = 100 * ws.count(0) // len(ws)
            print(f"  {s:08X}-{e:08X} {e - s:#7x}  prologues {prologues:4}  jr_ra {returns:4}  zero {zero:3}%")


def write_if_changed(path: Path, text: str):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def generate_registry(game: Game, exe: str) -> str:
    ovs = game.cfg.get("overlay", [])
    lines = ['#include "psx/runtime_modules.h"', f'#include "{exe}.h"']
    lines += [f'#include "{ov["name"]}.h"' for ov in ovs]
    lines += ["", f"const RecompModule *const game_exe_module = &{exe}_module;",
              f"const uint32_t game_exe_entry = 0x{game.entry:08X}u;",
              "const GameOverlay game_overlays[] = {"]
    for ov in ovs:
        size = len(game.images[ov["name"]].data)
        lines.append(f'    {{&{ov["name"]}_module, "{ov["file"]}", 0x{game.cfg["overlay_base"]:08X}u, '
                     f'0x{size:X}u, {ov["id"]}u}},')
    lines += ["};", f"const uint32_t game_overlay_count = {len(ovs)}u;", ""]
    return "\n".join(lines)


def cmd_generate(game: Game, outdir):
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    funcs, errors = game.analyze()
    if errors:
        for e in errors:
            print("error:", e, file=sys.stderr)
        sys.exit(1)
    exe = next(iter(funcs))
    written = set()
    for module, fs in funcs.items():
        def resolve(target, module=module, fs=fs):
            if target in fs:
                return fn_name(module, target)
            if target in funcs[exe]:
                return fn_name(exe, target)
            return None

        headers = [f"{exe}.h"] if module == exe else [f"{exe}.h", f"{module}.h"]
        write_if_changed(out / f"{module}.h", generate_header(module, fs))
        written.add(f"{module}.h")
        try:
            files = generate_module(module, fs, resolve, game.symbols.get(module, {}), headers)
        except CodegenError as e:
            sys.exit(f"{module}: {e}")
        for name, text in files.items():
            write_if_changed(out / name, text)
            written.add(name)
    for stale in out.glob("*.[ch]"):
        if stale.name not in written:
            stale.unlink()
    write_if_changed(out / "game_modules.c", generate_registry(game, exe))
    written.add("game_modules.c")
    (out / "sources.txt").write_text("\n".join(sorted(n for n in written if n.endswith(".c"))) + "\n")
    print(f"generated {len(written)} files in {out}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    game = Game(Path(sys.argv[1]))
    {"analyze": cmd_analyze, "gaps": cmd_gaps, "generate": cmd_generate}[sys.argv[2]](game, *sys.argv[3:])


if __name__ == "__main__":
    main()
