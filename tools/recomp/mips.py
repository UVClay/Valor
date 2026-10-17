"""MIPS R3000A (+ GTE) instruction decoding."""
from dataclasses import dataclass
import struct

REG_NAMES = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
             "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
             "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
             "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
RA, SP, ZERO = 31, 29, 0

PRIMARY = {
    0x02: "j", 0x03: "jal", 0x04: "beq", 0x05: "bne", 0x06: "blez", 0x07: "bgtz",
    0x08: "addi", 0x09: "addiu", 0x0A: "slti", 0x0B: "sltiu", 0x0C: "andi", 0x0D: "ori",
    0x0E: "xori", 0x0F: "lui",
    0x20: "lb", 0x21: "lh", 0x22: "lwl", 0x23: "lw", 0x24: "lbu", 0x25: "lhu", 0x26: "lwr",
    0x28: "sb", 0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2E: "swr",
    0x32: "lwc2", 0x3A: "swc2",
}
SPECIAL = {
    0x00: "sll", 0x02: "srl", 0x03: "sra", 0x04: "sllv", 0x06: "srlv", 0x07: "srav",
    0x08: "jr", 0x09: "jalr", 0x0C: "syscall", 0x0D: "break",
    0x10: "mfhi", 0x11: "mthi", 0x12: "mflo", 0x13: "mtlo",
    0x18: "mult", 0x19: "multu", 0x1A: "div", 0x1B: "divu",
    0x20: "add", 0x21: "addu", 0x22: "sub", 0x23: "subu",
    0x24: "and", 0x25: "or", 0x26: "xor", 0x27: "nor", 0x2A: "slt", 0x2B: "sltu",
}
REGIMM = {0x00: "bltz", 0x01: "bgez", 0x10: "bltzal", 0x11: "bgezal"}

BRANCHES = {"beq", "bne", "blez", "bgtz", "bltz", "bgez", "bltzal", "bgezal"}
LOADS = {"lb", "lh", "lw", "lbu", "lhu", "lwl", "lwr"}
STORES = {"sb", "sh", "sw", "swl", "swr"}


@dataclass(frozen=True)
class Insn:
    addr: int
    word: int
    op: str          # mnemonic, or "invalid"
    rs: int
    rt: int
    rd: int
    sa: int
    imm: int         # sign-extended 16-bit immediate
    uimm: int        # zero-extended 16-bit immediate
    target: int      # branch/jump target, if any

    @property
    def is_branch(self):
        return self.op in BRANCHES

    @property
    def has_delay_slot(self):
        return self.op in BRANCHES or self.op in ("j", "jal", "jr", "jalr")

    def writes(self):
        """GPR written by this instruction (None if none or $zero)."""
        op = self.op
        if op in ("addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lui") or op in LOADS \
                or op in ("mfc0", "mfc2", "cfc2"):
            r = self.rt
        elif op in ("jal", "bltzal", "bgezal"):
            r = RA
        elif op in SPECIAL.values() and op not in ("jr", "syscall", "break", "mthi", "mtlo",
                                                    "mult", "multu", "div", "divu"):
            r = self.rd
        else:
            return None
        return r or None

    def reads(self):
        """Set of GPRs read by this instruction."""
        op = self.op
        if op in ("sll", "srl", "sra"):
            regs = {self.rt}
        elif op in ("jr", "jalr", "mthi", "mtlo") or op in ("blez", "bgtz", "bltz", "bgez", "bltzal", "bgezal"):
            regs = {self.rs}
        elif op in ("mfhi", "mflo", "j", "jal", "lui", "syscall", "break", "mfc0", "mfc2", "cfc2",
                    "rfe", "cop2", "invalid"):
            regs = set()
        elif op in ("mtc0", "mtc2", "ctc2"):
            regs = {self.rt}
        elif op in ("addi", "addiu", "slti", "sltiu", "andi", "ori", "xori", "lwc2", "swc2") \
                or op in ("lb", "lh", "lw", "lbu", "lhu"):
            regs = {self.rs}
        else:  # R-type ALU, stores, beq/bne, lwl/lwr (merge with old rt)
            regs = {self.rs, self.rt}
        regs.discard(0)
        return regs


def decode(word: int, addr: int) -> Insn:
    opc = word >> 26
    rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
    sa, funct = (word >> 6) & 31, word & 63
    uimm = word & 0xFFFF
    imm = uimm - 0x10000 if uimm & 0x8000 else uimm
    target = 0
    op = "invalid"
    if opc == 0:
        op = SPECIAL.get(funct, "invalid")
    elif opc == 1:
        op = REGIMM.get(rt, "invalid")
    elif opc == 0x10:  # COP0
        if rs == 0:
            op = "mfc0"
        elif rs == 4:
            op = "mtc0"
        elif rs == 0x10 and funct == 0x10:
            op = "rfe"
    elif opc == 0x12:  # COP2 (GTE)
        if rs & 0x10:
            op = "cop2"
        else:
            op = {0: "mfc2", 2: "cfc2", 4: "mtc2", 6: "ctc2"}.get(rs, "invalid")
    else:
        op = PRIMARY.get(opc, "invalid")

    if op in BRANCHES:
        target = (addr + 4 + (imm << 2)) & 0xFFFFFFFF
    elif op in ("j", "jal"):
        target = ((addr + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
    return Insn(addr, word, op, rs, rt, rd, sa, imm, uimm, target)


class Image:
    """A contiguous block of MIPS memory (an exe's text+data, or an overlay)."""

    def __init__(self, name: str, base: int, data: bytes):
        self.name, self.base, self.data = name, base, data
        self.end = base + len(data)

    def contains(self, addr: int) -> bool:
        return self.base <= addr < self.end

    def word(self, addr: int) -> int:
        return struct.unpack_from("<I", self.data, addr - self.base)[0]

    def insn(self, addr: int) -> Insn:
        return decode(self.word(addr), addr)

    def words(self):
        for off in range(0, len(self.data) & ~3, 4):
            yield self.base + off, struct.unpack_from("<I", self.data, off)[0]
