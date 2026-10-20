#!/usr/bin/env python3
"""Function-level disassembly for reverse engineering (run inside ./dev).

  fn.py [--bin FILE] show ADDR...        disassemble the function(s) containing ADDR
  fn.py [--bin FILE] range START END      disassemble an address range (across recompiler splits)
  fn.py [--bin FILE] callers ADDR        functions that call ADDR (jal)
  fn.py [--bin FILE] calls ADDR          functions ADDR calls
  fn.py [--bin FILE] uses ADDR           instructions that build the address ADDR (lui pairs)
  fn.py [--bin FILE] offset OFF [OFF..]  functions using all of these load/store offsets
  fn.py [--bin FILE] grep REGEX           instructions matching REGEX, with their function
  fn.py sortnames                         sort docs/re/names.txt (exe, then overlay addresses)

--bin defaults to LEVEL.BIN; the exe (SLUS_009.74) is always loaded too, for calls into it.
Function starts come from the recompiler's tables (build/cmake/gen/game/*_table.c); names and notes from
docs/re/names.txt ("ADDR name  # note" per line), then the PsyQ library names in build/syms.
  fn.py [--bin FILE] asm ADDR             the function as GNU assembly for m2c
  fn.py [--bin FILE] c ADDR [M2C ARGS]    decompile it with m2c (build/m2c; types from docs/re/context.h)
"""
import re
import struct
import sys
from pathlib import Path

import rabbitizer

ROOT = Path(__file__).resolve().parents[2]
EXTRACTED = ROOT / "extracted/moh"
OVERLAY_BASE = 0x80039D90
TABLES = {"SLUS_009.74": "exe", "LEVEL.BIN": "level", "SHELL.BIN": "shell", "LEVEL2P.BIN": "level2p"}
REGS = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]


class Image:
    def __init__(self, name):
        data = (EXTRACTED / name).read_bytes()
        if data[:8] == b"PS-X EXE":
            self.base = struct.unpack_from("<I", data, 0x18)[0]
            self.data = data[0x800:]
            self.end = OVERLAY_BASE  # the overlay area is someone else's
        else:
            self.base, self.data = OVERLAY_BASE, data
            self.end = OVERLAY_BASE + len(data)
        table = ROOT / f"build/cmake/gen/game/{TABLES[name]}_table.c"
        self.starts = sorted(int(m, 16) | 0x80000000 for m in re.findall(r"\{0x([0-9A-F]+)u,", table.read_text()))

    def has(self, a):
        return self.base <= a < self.end

    def word(self, a):
        return struct.unpack_from("<I", self.data, a - self.base)[0]

    def ins(self, a):
        return rabbitizer.Instruction(self.word(a), vram=a, category=rabbitizer.InstrCategory.R3000GTE)

    def function(self, a):
        """(start, end) of the function containing a."""
        before = [i for i, s in enumerate(self.starts) if s <= a]
        if not before:
            return self.base, self.starts[0] if self.starts else self.end
        i = before[-1]
        end = self.starts[i + 1] if i + 1 < len(self.starts) else self.end
        return self.starts[i], end

    def whole_function(self, a):
        """(start, end) of the function containing a, merged with the pieces the recompiler split
        off it: following pieces that its branches or jumps lead into and nothing calls."""
        if not hasattr(self, "_called"):
            self._called = {self.ins(x).getInstrIndexAsVram() for x in range(self.base, self.end, 4)
                            if self.word(x) >> 26 == 3}
        s, e = self.function(a)
        while e < self.end:
            targets = set()
            for x in range(s, e, 4):
                i = self.ins(x)
                if i.isBranch():
                    targets.add(i.getBranchVramGeneric())
                elif self.word(x) >> 26 == 2:
                    targets.add(i.getInstrIndexAsVram())
            if e in self._called:
                break
            ahead = [t for t in targets if t >= e and t not in self._called and self.has(t)]
            last = self.ins(e - 8)
            falls = not (last.getOpcodeName() in ("j", "jr") or last.isUnconditionalBranch())
            if ahead:
                e = max(self.function(max(ahead))[1], self.function(e)[1] if falls else e)
            elif falls:
                e = self.function(e)[1]
            else:
                break
        return s, e


def load_names():
    names, notes = {}, {}
    # PsyQ library functions identified by signature (build/syms, from the setup tools).
    for path in sorted((ROOT / "build/syms").glob("*.txt")):
        for m in re.finditer(r"^(\w+) = 0x([0-9A-Fa-f]{8});", path.read_text(), re.M):
            names.setdefault(int(m.group(2), 16), m.group(1))
    path = ROOT / "docs/re/names.txt"
    if path.exists():
        for line in path.read_text().splitlines():
            line, _, note = line.partition("#")
            parts = line.split()
            if len(parts) >= 2:
                a = int(parts[0], 16)
                names[a] = parts[1]
                if note.strip():
                    notes[a] = note.strip()
    return names, notes


NAMES, NOTES = load_names()


def name(a):
    return NAMES.get(a, f"func_{a:08X}")


def label(a):
    return NAMES.get(a, f"{a:08X}")


STRING_IMAGES = []


def string_at(a):
    """The printable C string at a (in a loaded binary), or None."""
    for im in STRING_IMAGES:
        if im.base <= a < im.base + len(im.data):
            o = a - im.base
            e = im.data.find(b"\0", o, o + 200)
            s = im.data[o:e]
            if e - o >= 4 and all(32 <= c < 127 or c in (9, 10) for c in s):
                return s.decode().replace("\n", "\\n")
    return None


def disasm(img, start, end):
    branch_targets = set()
    for a in range(start, end, 4):
        i = img.ins(a)
        if i.isBranch():
            branch_targets.add(i.getBranchVramGeneric())
    # Registers holding a known constant (lui, then addiu/ori), forgotten at labels (straight-line
    # knowledge only) and when overwritten.
    val = {}
    out = []
    for a in range(start, end, 4):
        i = img.ins(a)
        op = i.getOpcodeName()
        text = i.disassemble()
        note = ""
        if a in branch_targets:
            val.clear()
        if i.isBranch():
            t = i.getBranchVramGeneric()
            text = re.sub(r"\. \+ 4 \+.*$", f".L{t:08X}", text)
        elif op == "jal":
            t = i.getInstrIndexAsVram()
            text = f"jal         {name(t)}"
        dest = None
        if i.modifiesRd():
            dest = i.rd.value
        elif i.modifiesRt():
            dest = i.rt.value
        if op == "lui":
            val[i.rt.value] = i.getProcessedImmediate() << 16
        elif op in ("addiu", "ori", "lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb") and i.rs.value in val:
            imm = i.getProcessedImmediate()
            v = (val[i.rs.value] | imm) if op == "ori" else (val[i.rs.value] + imm) & 0xFFFFFFFF
            if 0x80000000 <= v < 0x80200000 or 0x1F800000 <= v < 0x1F801000:
                note = f"  ; {label(v)}"
                if op in ("addiu", "ori") and (text_ := string_at(v)):
                    note += f' "{text_}"'
            if op in ("addiu", "ori"):
                val[i.rt.value] = v
                dest = None
        if dest is not None and op != "lui":
            val.pop(dest, None)
        if a in branch_targets:
            out.append(f".L{a:08X}:")
        if a in NOTES:
            out.append(f"    # {NOTES[a]}")
        out.append(f"  {a:08X}: {text}{note}")
    return out


def sym(a):
    return NAMES.get(a, f"D_{a:08X}")


def asm(img, start, end):
    """The function as GNU assembly for m2c: hi/lo pairs as %hi/%lo of named symbols, jump
    tables as .rodata (straight-line pairing, as in disasm)."""
    lui_sym, lo_sym, jtbls = {}, {}, {}
    hi = {}
    labels = set()
    for a in range(start, end, 4):
        i = img.ins(a)
        op = i.getOpcodeName()
        if i.isBranch():
            labels.add(i.getBranchVramGeneric())
        elif op == "j" and start <= i.getInstrIndexAsVram() < end:
            labels.add(i.getInstrIndexAsVram())
        if op == "lui":
            hi[i.rt.value] = (a, i.getProcessedImmediate() << 16)
            continue
        if op in ("addiu", "lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb", "lwc2", "swc2") and i.rs.value in hi:
            la, base = hi[i.rs.value]
            v = (base + i.getProcessedImmediate()) & 0xFFFFFFFF
            # A jump table: a word table of addresses inside this function, read then jumped to.
            if op == "lw" and any(img.ins(x).getOpcodeName() == "jr" for x in range(a + 4, a + 12, 4)) \
                    and img.has(v) and start <= img.word(v) < end:
                targets = []
                t = v
                while img.has(t) and start <= img.word(t) < end:
                    targets.append(img.word(t))
                    t += 4
                jtbls[v] = targets
                labels.update(targets)
                name_ = f"jtbl_{v:08X}"
            else:
                name_ = sym(v)
            if la not in lui_sym:
                lui_sym[la] = (name_, v)
            first, fv = lui_sym[la]
            lo_sym[a] = first if fv == v else f"{first} + 0x{v - fv:X}" if v > fv else f"{first} - 0x{fv - v:X}"
        dest = i.rd.value if i.modifiesRd() else i.rt.value if i.modifiesRt() else None
        if dest is not None and op != "lui":
            hi.pop(dest, None)
    out = [".set noat", ".set noreorder", ".section .text", f"glabel {name(start)}"]
    for a in range(start, end, 4):
        i = img.ins(a)
        op = i.getOpcodeName()
        text = i.disassemble()
        if i.isBranch():
            text = re.sub(r"\. \+ 4 \+.*$", f".L{i.getBranchVramGeneric():08X}", text)
        elif op == "jal":
            text = f"jal         {name(i.getInstrIndexAsVram())}"
        elif op == "j":
            t = i.getInstrIndexAsVram()
            text = f"j           .L{t:08X}" if start <= t < end else f"j           {name(t)}"
        elif a in lui_sym:
            text = re.sub(r"0x[0-9A-Fa-f]+$", f"%hi({lui_sym[a][0]})", text)
        elif a in lo_sym:
            if op == "addiu":
                text = re.sub(r"-?0x[0-9A-Fa-f]+$|-?\d+$", f"%lo({lo_sym[a]})", text)
            else:
                text = re.sub(r"-?(0x[0-9A-Fa-f]+|\d+)?\(", f"%lo({lo_sym[a]})(", text, count=1)
        if a in labels and a != start:
            out.append(f".L{a:08X}:")
        out.append(f"/* {a:08X} */ {text}")
    for t, targets in jtbls.items():
        out += [".section .rodata", f"glabel jtbl_{t:08X}"] + [f".word .L{x:08X}" for x in targets]
    return out


def sort_names():
    path = ROOT / "docs/re/names.txt"
    lines = path.read_text().splitlines()
    head = []
    for line in lines:
        if not line.startswith("#"):
            break
        head.append(line)
    entries = {}
    for line in lines:
        if line and not line.startswith("#"):
            entries[int(line.split()[0], 16)] = line
    exe = [entries[a] for a in sorted(entries) if a < OVERLAY_BASE]
    ovl = [entries[a] for a in sorted(entries) if a >= OVERLAY_BASE]
    out = head + ["", "# --- SLUS_009.74 (exe) ---"] + exe + ["", "# --- LEVEL.BIN ---"] + ovl
    path.write_text("\n".join(out) + "\n")


def main():
    args = sys.argv[1:]
    if args == ["sortnames"]:
        return sort_names()
    binname = "LEVEL.BIN"
    if args[:1] == ["--bin"]:
        binname, args = args[1], args[2:]
    img = Image(binname)
    exe = Image("SLUS_009.74")
    images = [img] if binname == "SLUS_009.74" else [img, exe]
    STRING_IMAGES.extend(images)
    cmd, rest = args[0], args[1:]

    def find(a):
        for im in images:
            if im.has(a):
                return im
        sys.exit(f"{a:08X} is in none of the loaded binaries")

    if cmd == "range":
        a, b = int(rest[0], 16), int(rest[1], 16)
        print("\n".join(disasm(find(a), a, b)))
        return
    if cmd == "grep":
        pat = re.compile(rest[0])
        for im in images:
            for a in range(im.base, im.end, 4):
                t = im.ins(a).disassemble()
                if pat.search(t):
                    f = im.function(a)[0]
                    print(f"{a:08X} in {name(f)}: {t}")
        return
    if cmd in ("asm", "c"):
        a = int(rest[0], 16)
        im = find(a)
        s, e = im.whole_function(a)
        text = "\n".join(asm(im, s, e)) + "\n"
        if cmd == "asm":
            print(text, end="")
            return
        import subprocess
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".s") as f:
            f.write(text)
            f.flush()
            ctx = ROOT / "docs/re/context.h"
            cmd_ = [sys.executable, str(ROOT / "build/m2c/m2c.py"), "-t", "mips-gcc-c", "--valid-syntax"]
            if ctx.exists():
                cmd_ += ["--context", str(ctx)]
            sys.exit(subprocess.run(cmd_ + rest[1:] + [f.name]).returncode)
    if cmd == "show":
        for r in rest:
            a = int(r, 16)
            im = find(a)
            s, e = im.function(a)
            print(f"{name(s)}:  # {s:08X}..{e:08X}" + (f"  {NOTES[s]}" if s in NOTES else ""))
            print("\n".join(disasm(im, s, e)))
            print()
    elif cmd in ("callers", "calls", "uses", "offset"):
        hits = {}
        for im in images:
            for a in range(im.base, im.end, 4):
                i = im.ins(a)
                op = i.getOpcodeName()
                if cmd == "callers" and op == "jal" and i.getInstrIndexAsVram() == int(rest[0], 16):
                    hits.setdefault(im.function(a)[0], []).append(a)
                elif cmd == "offset" and op in ("lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb"):
                    imm = i.getProcessedImmediate()
                    hits.setdefault(im.function(a)[0], set()).add(imm)
        if cmd == "calls":
            a = int(rest[0], 16)
            im = find(a)
            s, e = im.function(a)
            seen = []
            for x in range(s, e, 4):
                i = im.ins(x)
                if i.getOpcodeName() == "jal" and i.getInstrIndexAsVram() not in seen:
                    seen.append(i.getInstrIndexAsVram())
            for t in seen:
                print(f"{t:08X} {name(t)}")
        elif cmd == "callers":
            for f, sites in sorted(hits.items()):
                print(f"{f:08X} {name(f)}: " + " ".join(f"{x:08X}" for x in sites))
        elif cmd == "offset":
            want = {int(o, 16) for o in rest}
            for f, offs in sorted(hits.items()):
                if want <= offs:
                    print(f"{f:08X} {name(f)}")
        elif cmd == "uses":
            target = int(rest[0], 16)
            for im in images:
                hi = {}
                for a in range(im.base, im.end, 4):
                    i = im.ins(a)
                    op = i.getOpcodeName()
                    if op == "lui":
                        hi[i.rt.value] = (a, i.getProcessedImmediate() << 16)
                    elif op in ("addiu", "ori", "lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb") and i.rs.value in hi:
                        if a - hi[i.rs.value][0] < 0x100:
                            imm = i.getProcessedImmediate()
                            v = (hi[i.rs.value][1] | imm) if op == "ori" else (hi[i.rs.value][1] + imm) & 0xFFFFFFFF
                            if v == target:
                                f = im.function(a)[0]
                                print(f"{a:08X} in {name(f)}: {i.disassemble()}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
