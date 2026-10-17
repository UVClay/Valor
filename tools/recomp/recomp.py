#!/usr/bin/env python3
"""Static recompiler driver.

Usage:
  recomp.py <config.toml> analyze          print function discovery stats
  recomp.py <config.toml> gaps [module...] list unclaimed regions between functions
  recomp.py <config.toml> generate <outdir> write C sources for all modules

A config describes one project: either a PS-X EXE plus overlays (a game) or an ELF
(the BIOS). Resident modules (the exe, BIOS ROM/RAM) can be called directly from any
module of the project; overlays share one load address and only one is resident.
"""
import re
import struct
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze import discover, find_gaps  # noqa: E402
from codegen import CodegenError, fn_name, generate_header, generate_module  # noqa: E402
from elf import SHF_EXECINSTR, STT_FUNC, STT_NOTYPE, Elf  # noqa: E402
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


class Project:
    def __init__(self, config_path: Path):
        cfg = tomllib.loads(config_path.read_text())
        self.cfg = cfg
        self.name = cfg["project"]
        self.images = {}    # module name -> Image (resident modules first)
        self.resident = []  # module names
        self.overlays = []  # overlay config dicts
        self.seeds = {}
        self.symbols = {}   # module name -> {addr: name}
        if "elf" in cfg:
            self._load_elf(cfg)
        else:
            self._load_psexe(cfg)

    def _load_elf(self, cfg):
        elf = Elf(ROOT / cfg["elf"])
        self.entry = cfg["entry"]
        for mod in cfg["module"]:
            secs = sorted((elf.section(n) for n in mod["sections"]), key=lambda s: s.addr)
            base, data = secs[0].addr, b""
            for s in secs:
                data += b"\0" * (s.addr - (base + len(data))) + s.data
            name = mod["name"]
            img = self.images[name] = Image(name, base, data)
            self.resident.append(name)
            code = {s.name for s in secs if s.flags & SHF_EXECINSTR}
            syms = {}
            for sym in elf.symbols:
                if sym.section not in code or sym.value % 4 or not img.contains(sym.value):
                    continue
                # NOTYPE symbols are asm labels: only global ones are entry points.
                if sym.type == STT_FUNC or (sym.type == STT_NOTYPE and sym.bind == 1):
                    syms.setdefault(sym.value, sym.name)
            self.symbols[name] = syms
            self.seeds[name] = set(syms) | ({self.entry} if img.contains(self.entry) else set())

    def _load_psexe(self, cfg):
        extracted = ROOT / cfg["extracted"]
        exe_cfg = cfg["exe"]
        raw = (extracted / exe_cfg["file"]).read_bytes()
        if raw[:8] != b"PS-X EXE":
            sys.exit(f"{exe_cfg['file']} is not a PS-X EXE")
        self.entry, _gp, t_addr, t_size = struct.unpack_from("<IIII", raw, 0x10)
        code_end = exe_cfg.get("code_end", t_addr + t_size)
        name = exe_cfg["name"]
        self.images[name] = Image(name, t_addr, raw[0x800:0x800 + code_end - t_addr])
        self.resident.append(name)
        self.seeds[name] = {self.entry, *exe_cfg.get("seeds", [])}
        self.symbols[name] = read_symbols(exe_cfg.get("symbols", []))
        for ov in cfg.get("overlay", []):
            data = (extracted / ov["file"]).read_bytes()
            self.images[ov["name"]] = Image(ov["name"], cfg["overlay_base"], data)
            self.overlays.append(ov)
            self.seeds[ov["name"]] = {ov["entry"], *ov.get("seeds", [])}
            self.symbols[ov["name"]] = read_symbols(ov.get("symbols", []))
        # Library symbols that look like functions are seeds too.
        for mod, syms in self.symbols.items():
            self.seeds[mod] |= {a for a in syms if self.images[mod].contains(a)}

    def analyze(self):
        # setjmp-like functions, by symbol name, whose call sites a longjmp can resume.
        names = set(self.cfg.get("continuations", []))
        conts = {a for syms in self.symbols.values() for a, n in syms.items() if n in names}
        return discover(self.images, self.seeds, self.resident, conts)


def cmd_analyze(proj: Project, *_):
    funcs, errors = proj.analyze()
    for e in errors:
        print("error:", e)
    for name, fs in funcs.items():
        img = proj.images[name]
        code = sum(len(f.addrs) * 4 for f in fs.values())
        jt = sum(len(f.jump_tables) for f in fs.values())
        bios = sum(len(f.bios_jumps) for f in fs.values())
        dyn = sum(len(f.dynamic_jumps) for f in fs.values())
        gaps = [(s, e) for s, e in find_gaps(img, fs) if e - s >= 16]
        gap_bytes = sum(e - s for s, e in gaps)
        print(f"{name:8} {len(fs):5} functions, {code:#8x} code bytes of {len(img.data):#8x}, "
              f"{jt} jump tables, {bios} BIOS jumps, {dyn} dynamic jr, {len(gaps)} gaps ({gap_bytes:#x} bytes)")


def cmd_gaps(proj: Project, *names):
    funcs, _ = proj.analyze()
    for name in names or funcs:
        img = proj.images[name]
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


def generate_registry(proj: Project) -> str:
    p = proj.name
    lines = ['#include "psx/runtime_modules.h"']
    lines += [f'#include "{m}.h"' for m in proj.images]
    lines += ["", f"static const RecompModule *const {p}_resident[] = {{"]
    lines += [f"    &{m}_module," for m in proj.resident]
    lines += ["};"]
    if proj.overlays:
        lines += [f"static const GameOverlay {p}_overlays[] = {{"]
        for ov in proj.overlays:
            size = len(proj.images[ov["name"]].data)
            lines.append(f'    {{&{ov["name"]}_module, "{ov["file"]}", 0x{proj.cfg["overlay_base"]:08X}u, '
                         f'0x{size:X}u, {ov["id"]}u}},')
        lines += ["};"]
    ovs = f"{p}_overlays" if proj.overlays else "0"
    lines += ["", f'const RecompProject {p}_project = {{"{p}", 0x{proj.entry:08X}u, {p}_resident, '
              f"{len(proj.resident)}u, {ovs}, {len(proj.overlays)}u}};", ""]
    return "\n".join(lines)


def cmd_generate(proj: Project, outdir):
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    funcs, errors = proj.analyze()
    if errors:
        for e in errors:
            print("error:", e, file=sys.stderr)
        sys.exit(1)
    written = set()
    for module, fs in funcs.items():
        def resolve(target, module=module, fs=fs):
            if target in fs:
                return fn_name(module, target)
            for r in proj.resident:
                if target in funcs[r]:
                    return fn_name(r, target)
            return None

        headers = [f"{r}.h" for r in proj.resident]
        if module not in proj.resident:
            headers.append(f"{module}.h")
        write_if_changed(out / f"{module}.h", generate_header(module, fs))
        written.add(f"{module}.h")
        try:
            syms = proj.symbols.get(module, {})
            by_name = {n: a for a, n in syms.items()}
            dynamic = [(by_name[n], by_name[n] + 4 * words)
                       for n, words in proj.cfg.get("dynamic_code", {}).items() if n in by_name]
            files = generate_module(module, fs, resolve, syms, headers,
                                    proj.cfg.get("overrides", {}), dynamic)
        except CodegenError as e:
            sys.exit(f"{module}: {e}")
        for name, text in files.items():
            write_if_changed(out / name, text)
            written.add(name)
    write_if_changed(out / f"{proj.name}_modules.c", generate_registry(proj))
    written.add(f"{proj.name}_modules.c")
    for stale in out.glob("*.[ch]"):
        if stale.name not in written:
            stale.unlink()
    write_if_changed(out / "sources.txt", "\n".join(sorted(n for n in written if n.endswith(".c"))) + "\n")
    print(f"generated {len(written)} files in {out}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    proj = Project(Path(sys.argv[1]))
    {"analyze": cmd_analyze, "gaps": cmd_gaps, "generate": cmd_generate}[sys.argv[2]](proj, *sys.argv[3:])


if __name__ == "__main__":
    main()
