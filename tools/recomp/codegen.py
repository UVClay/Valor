"""C code generation from analyzed functions."""
from mips import LOADS, RA, Insn
from analyze import Function

FUNCS_PER_FILE = 64


class CodegenError(Exception):
    pass


def R(n: int) -> str:
    return "0u" if n == 0 else f"c->r[{n}]"


def fn_name(module: str, addr: int) -> str:
    return f"{module}_{addr:08X}"


def _set(rd: int, expr: str) -> str:
    return "" if rd == 0 else f"c->r[{rd}] = {expr};"


def _ea(i: Insn) -> str:
    if i.imm == 0:
        return R(i.rs)
    return f"{R(i.rs)} + 0x{i.imm & 0xFFFFFFFF:X}u" if i.imm > 0 else f"{R(i.rs)} - 0x{-i.imm:X}u"


def simple(i: Insn) -> str:
    """C for an instruction that doesn't transfer control."""
    op, rs, rt, rd, sa = i.op, i.rs, i.rt, i.rd, i.sa
    s32 = lambda n: f"(int32_t){R(n)}"
    if op == "sll":
        return "" if i.word == 0 else _set(rd, f"{R(rt)} << {sa}")
    if op == "srl":
        return _set(rd, f"{R(rt)} >> {sa}")
    if op == "sra":
        return _set(rd, f"(uint32_t)({s32(rt)} >> {sa})")
    if op == "sllv":
        return _set(rd, f"{R(rt)} << ({R(rs)} & 31)")
    if op == "srlv":
        return _set(rd, f"{R(rt)} >> ({R(rs)} & 31)")
    if op == "srav":
        return _set(rd, f"(uint32_t)({s32(rt)} >> ({R(rs)} & 31))")
    if op in ("add", "addu"):
        return _set(rd, f"{R(rs)} + {R(rt)}")
    if op in ("sub", "subu"):
        return _set(rd, f"{R(rs)} - {R(rt)}")
    if op == "and":
        return _set(rd, f"{R(rs)} & {R(rt)}")
    if op == "or":
        return _set(rd, f"{R(rs)} | {R(rt)}")
    if op == "xor":
        return _set(rd, f"{R(rs)} ^ {R(rt)}")
    if op == "nor":
        return _set(rd, f"~({R(rs)} | {R(rt)})")
    if op == "slt":
        return _set(rd, f"{s32(rs)} < {s32(rt)}")
    if op == "sltu":
        return _set(rd, f"{R(rs)} < {R(rt)}")
    if op in ("addi", "addiu"):
        return _set(rt, f"{R(rs)} + 0x{i.imm & 0xFFFFFFFF:X}u")
    if op == "slti":
        return _set(rt, f"{s32(rs)} < {i.imm}")
    if op == "sltiu":
        return _set(rt, f"{R(rs)} < 0x{i.imm & 0xFFFFFFFF:X}u")
    if op == "andi":
        return _set(rt, f"{R(rs)} & 0x{i.uimm:X}u")
    if op == "ori":
        return _set(rt, f"{R(rs)} | 0x{i.uimm:X}u")
    if op == "xori":
        return _set(rt, f"{R(rs)} ^ 0x{i.uimm:X}u")
    if op == "lui":
        return _set(rt, f"0x{i.uimm << 16:X}u")
    if op == "mfhi":
        return _set(rd, "c->hi")
    if op == "mflo":
        return _set(rd, "c->lo")
    if op == "mthi":
        return f"c->hi = {R(rs)};"
    if op == "mtlo":
        return f"c->lo = {R(rs)};"
    if op == "mult":
        return (f"{{ int64_t p_ = (int64_t){s32(rs)} * {s32(rt)}; "
                f"c->lo = (uint32_t)p_; c->hi = (uint32_t)((uint64_t)p_ >> 32); }}")
    if op == "multu":
        return (f"{{ uint64_t p_ = (uint64_t){R(rs)} * {R(rt)}; "
                f"c->lo = (uint32_t)p_; c->hi = (uint32_t)(p_ >> 32); }}")
    if op == "div":
        return f"psx_div(c, {R(rs)}, {R(rt)});"
    if op == "divu":
        return f"psx_divu(c, {R(rs)}, {R(rt)});"
    if op == "lb":
        return _set(rt, f"(uint32_t)(int8_t)psx_r8(c, {_ea(i)})")
    if op == "lbu":
        return _set(rt, f"psx_r8(c, {_ea(i)})")
    if op == "lh":
        return _set(rt, f"(uint32_t)(int16_t)psx_r16(c, {_ea(i)})")
    if op == "lhu":
        return _set(rt, f"psx_r16(c, {_ea(i)})")
    if op == "lw":
        return _set(rt, f"psx_r32(c, {_ea(i)})")
    if op == "lwl":
        return _set(rt, f"psx_lwl(c, {_ea(i)}, {R(rt)})")
    if op == "lwr":
        return _set(rt, f"psx_lwr(c, {_ea(i)}, {R(rt)})")
    if op == "sb":
        return f"psx_w8(c, {_ea(i)}, {R(rt)});"
    if op == "sh":
        return f"psx_w16(c, {_ea(i)}, {R(rt)});"
    if op == "sw":
        return f"psx_w32(c, {_ea(i)}, {R(rt)});"
    if op == "swl":
        return f"psx_swl(c, {_ea(i)}, {R(rt)});"
    if op == "swr":
        return f"psx_swr(c, {_ea(i)}, {R(rt)});"
    if op == "lwc2":
        return f"gte_mtc2(c, {rt}, psx_r32(c, {_ea(i)}));"
    if op == "swc2":
        return f"psx_w32(c, {_ea(i)}, gte_mfc2(c, {rt}));"
    if op == "mfc0":
        return _set(rt, f"psx_mfc0(c, {rd})") or f"psx_mfc0(c, {rd});"
    if op == "mtc0":
        return f"psx_mtc0(c, {rd}, {R(rt)});"
    if op == "rfe":
        return "psx_rfe(c);"
    if op == "mfc2":
        return _set(rt, f"gte_mfc2(c, {rd})") or f"gte_mfc2(c, {rd});"
    if op == "cfc2":
        return _set(rt, f"gte_cfc2(c, {rd})") or f"gte_cfc2(c, {rd});"
    if op == "mtc2":
        return f"gte_mtc2(c, {rd}, {R(rt)});"
    if op == "ctc2":
        return f"gte_ctc2(c, {rd}, {R(rt)});"
    if op == "cop2":
        return f"gte_cmd(c, 0x{i.word & 0x1FFFFFF:X}u);"
    if op == "syscall":
        return f"psx_syscall(c, 0x{i.addr:08X}u);"
    if op == "break":
        return f"psx_break(c, 0x{i.addr:08X}u, 0x{(i.word >> 6) & 0xFFFFF:X}u); return;"
    raise CodegenError(f"{i.addr:08X}: no codegen for {op}")


def branch_cond(i: Insn) -> str:
    s = lambda n: f"(int32_t){R(n)}"
    return {
        "beq": f"{R(i.rs)} == {R(i.rt)}",
        "bne": f"{R(i.rs)} != {R(i.rt)}",
        "blez": f"{s(i.rs)} <= 0",
        "bgtz": f"{s(i.rs)} > 0",
        "bltz": f"{s(i.rs)} < 0",
        "bgez": f"{s(i.rs)} >= 0",
        "bltzal": f"{s(i.rs)} < 0",
        "bgezal": f"{s(i.rs)} >= 0",
    }[i.op]


DELAYED_RESULT = LOADS | {"mfc0", "mfc2", "cfc2"}


def is_terminator(i: Insn) -> bool:
    """Control never continues to the next sequential instruction (after the delay slot)."""
    if i.op in ("j", "jr"):
        return True
    return (i.op == "beq" and i.rs == i.rt) or (i.op == "bgez" and i.rs == 0)


class FunctionWriter:
    def __init__(self, module: str, fn: Function, resolve_call):
        self.module, self.fn, self.img = module, fn, fn.image
        self.resolve_call = resolve_call  # addr -> C function name or None (indirect)
        self.out = []
        self.pending = 0  # instructions since the last TICK

    def line(self, s: str, indent=1):
        if s:
            self.out.append("    " * indent + s)

    def tick(self, extra=0, indent=1):
        n = self.pending + extra
        if n:
            self.line(f"TICK(c, {n});", indent)
        self.pending = 0

    def call(self, target: int) -> str:
        name = self.resolve_call(target)
        return f"{name}(c);" if name else f"psx_call(c, 0x{target:08X}u);"

    def check_hazard(self, i: Insn):
        """Refuse to generate code that would depend on R3000 load-delay behavior."""
        if i.op not in DELAYED_RESULT or not i.rt or (i.op in ("mfc0", "mfc2", "cfc2") and i.rt == 0):
            return
        nxt = i.addr + 4
        if nxt not in self.fn.addrs:
            return
        j = self.img.insn(nxt)
        if i.op in ("lwl", "lwr") and j.op in ("lwl", "lwr"):
            return  # unaligned pairs forward the pending value
        if i.rt in j.reads():
            raise CodegenError(f"{i.addr:08X}: load-delay hazard ({i.op} then {j.op}) in {self.fn.start:08X}")

    def emit(self):
        fn = self.fn
        self.out.append(f"void {fn_name(self.module, fn.start)}(Cpu *c) {{")
        self.line("CHECK_EVENTS(c);")
        labels = set(fn.labels)
        skip = set()
        for a in sorted(fn.addrs):
            if a in skip:
                continue
            if a in labels:
                self.tick()
                self.out.append(f"L_{a:08X}:;")
            i = self.img.insn(a)
            self.check_hazard(i)
            if not i.has_delay_slot:
                self.pending += 1
                self.line(simple(i))
                falls, end = i.op != "break", a + 4
            else:
                d = self.img.insn(a + 4)
                self.check_hazard(d)
                self.emit_control(i, simple(d))
                falls, end = not is_terminator(i), a + 8
                if a + 4 in labels:
                    # Something jumps straight into this delay slot; it's emitted again on its own next.
                    if falls:
                        self.line(f"goto L_{end:08X};")
                        labels.add(end)
                else:
                    skip.add(a + 4)
            if falls and end not in fn.addrs:
                if end not in fn.tail_calls:
                    raise CodegenError(f"{fn.start:08X}: falls off the end at {end:08X}")
                self.tick()
                self.line(f"{self.call(end)} return;")
        self.tick()
        self.out.append("}")
        return "\n".join(self.out)

    def emit_control(self, i: Insn, delay: str):
        fn, a = self.fn, i.addr
        op = i.op
        if i.is_branch:
            cond = branch_cond(i)
            link = op in ("bltzal", "bgezal")
            self.line("{")
            self.line(f"int cond_ = {cond};", 2)
            if link:
                self.line(f"c->r[{RA}] = 0x{a + 8:08X}u;", 2)
            self.tick(2, indent=2)
            self.line(delay, 2)
            if link:
                self.line(f"if (cond_) {self.call(i.target)}", 2)
            else:
                back = i.target <= a
                if back:
                    self.line("if (cond_) { CHECK_EVENTS(c); goto L_%08X; }" % i.target, 2)
                else:
                    self.line(f"if (cond_) goto L_{i.target:08X};", 2)
            self.line("}")
            return
        if op == "j":
            self.tick(2)
            self.line(delay)
            if a in fn.tail_calls:
                self.line(f"{self.call(fn.tail_calls[a])} return;")
            elif i.target <= a:
                self.line(f"CHECK_EVENTS(c); goto L_{i.target:08X};")
            else:
                self.line(f"goto L_{i.target:08X};")
            return
        if op == "jal":
            self.line(f"c->r[{RA}] = 0x{a + 8:08X}u;")
            self.tick(2)
            self.line(delay)
            self.line(self.call(i.target))
            return
        if op == "jalr":
            self.line("{")
            self.line(f"uint32_t t_ = {R(i.rs)};", 2)
            if i.rd:
                self.line(f"c->r[{i.rd}] = 0x{a + 8:08X}u;", 2)
            self.tick(2, indent=2)
            self.line(delay, 2)
            self.line("psx_call(c, t_);", 2)
            self.line("}")
            return
        if op == "jr":
            if i.rs == RA:
                self.tick(2)
                self.line(delay)
                self.line("return;")
                return
            self.line("{")
            self.line(f"uint32_t t_ = {R(i.rs)};", 2)
            self.tick(2, indent=2)
            self.line(delay, 2)
            if a in fn.bios_jumps:
                self.line(f"psx_bios_call(c, 0x{fn.bios_jumps[a]:X}u); return;", 2)
            elif a in fn.jump_tables:
                self.line("switch (t_) {", 2)
                for t in sorted(set(filter(None, fn.jump_tables[a]))):
                    self.line(f"case 0x{t:08X}u: goto L_{t:08X};", 3)
                self.line(f"default: psx_unhandled_jump(c, 0x{a:08X}u, t_); return;", 3)
                self.line("}", 2)
            else:
                self.line("psx_call(c, t_); return;", 2)
            self.line("}")
            return
        raise CodegenError(f"{a:08X}: unexpected control op {op}")


def generate_module(module: str, funcs: dict, resolve_call, symbols: dict, header_names):
    """Returns {filename: text} for one module (exe or overlay)."""
    files = {}
    starts = sorted(funcs)
    for chunk in range(0, len(starts), FUNCS_PER_FILE):
        body = [f'#include "{h}"' for h in header_names] + [""]
        for s in starts[chunk:chunk + FUNCS_PER_FILE]:
            if s in symbols:
                body.append(f"/* {symbols[s]} */")
            body.append(FunctionWriter(module, funcs[s], resolve_call).emit())
            body.append("")
        files[f"{module}_{chunk // FUNCS_PER_FILE:03d}.c"] = "\n".join(body)

    table = [f'#include "{h}"' for h in header_names] + ["",
             f"static const RecompEntry {module}_entries[] = {{"]
    table += [f"    {{0x{s:08X}u, {fn_name(module, s)}}}," for s in starts]
    table += ["};", f"const RecompModule {module}_module = {{\"{module}\", {module}_entries, {len(starts)}u}};", ""]
    files[f"{module}_table.c"] = "\n".join(table)
    return files


def generate_header(module: str, funcs: dict) -> str:
    lines = ["#pragma once", '#include "psx/recomp.h"', ""]
    lines += [f"void {fn_name(module, s)}(Cpu *c);" for s in sorted(funcs)]
    lines += [f"extern const RecompModule {module}_module;", ""]
    return "\n".join(lines)
