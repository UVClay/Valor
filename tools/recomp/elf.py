"""Minimal ELF32 little-endian reader: sections and symbols."""
import struct
from dataclasses import dataclass
from pathlib import Path

STT_NOTYPE, STT_FUNC = 0, 2
SHF_EXECINSTR = 4
SHT_NOBITS = 8


@dataclass
class Section:
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    data: bytes


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    type: int
    bind: int
    section: str


class Elf:
    def __init__(self, path: Path):
        raw = path.read_bytes()
        if raw[:4] != b"\x7fELF" or raw[4] != 1 or raw[5] != 1:
            raise ValueError(f"{path}: not a 32-bit little-endian ELF")
        e_shoff, = struct.unpack_from("<I", raw, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", raw, 0x2E)
        headers = [struct.unpack_from("<IIIIIIIIII", raw, e_shoff + i * e_shentsize) for i in range(e_shnum)]
        strtab = headers[e_shstrndx]
        name_of = lambda off, tab: raw[tab[4] + off:raw.index(b"\0", tab[4] + off)].decode()
        self.sections = []
        for h in headers:
            name = name_of(h[0], strtab)
            data = b"" if h[1] == SHT_NOBITS else raw[h[4]:h[4] + h[5]]
            self.sections.append(Section(name, h[1], h[2], h[3], h[4], h[5], data))
        self.symbols = []
        for idx, h in enumerate(headers):
            if h[1] != 2:  # SHT_SYMTAB
                continue
            link = headers[h[6]]
            for off in range(h[4], h[4] + h[5], 16):
                st_name, value, size, info, _other, shndx = struct.unpack_from("<IIIBBH", raw, off)
                if not st_name or shndx == 0 or shndx >= len(self.sections):
                    continue
                self.symbols.append(Symbol(name_of(st_name, link), value, size, info & 15, info >> 4,
                                           self.sections[shndx].name))

    def section(self, name: str) -> Section:
        for s in self.sections:
            if s.name == name:
                return s
        raise KeyError(name)
