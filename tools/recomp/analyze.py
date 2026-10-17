"""Function discovery and control-flow analysis."""
from dataclasses import dataclass, field

from mips import RA, Image, Insn

BIOS_VECTORS = (0xA0, 0xB0, 0xC0)
JTBL_WINDOW = 24  # instructions to look back from a `jr` for the table setup


class AnalysisError(Exception):
    pass


@dataclass
class Function:
    image: Image
    start: int
    end: int = 0                                     # exclusive, includes last delay slot
    addrs: set = field(default_factory=set)          # every instruction address in the function
    labels: set = field(default_factory=set)         # branch / jump-table targets
    calls: set = field(default_factory=set)          # jal targets
    call_sites: dict = field(default_factory=dict)   # jal addr -> target
    tail_calls: dict = field(default_factory=dict)   # j addr -> target, or fallthrough addr -> next func
    jump_tables: dict = field(default_factory=dict)  # jr addr -> [targets] (None = unused case)
    bios_jumps: dict = field(default_factory=dict)   # jr addr -> 0xA0/0xB0/0xC0
    dynamic_jumps: set = field(default_factory=set)  # jr addr with unknown target
    pointers: set = field(default_factory=set)       # constants built by lui/addiu|ori into the image


def _const_window(fn_image: Image, addrs: list):
    """Forward constant propagation over a straight-line window.

    Values are ("c", value) for constants, ("ci", value) for constant + unknown index,
    ("ld", table_addr) for a word loaded from constant+index.
    """
    regs = {0: ("c", 0)}
    sltiu_bound = None
    for a in addrs:
        i = fn_image.insn(a)
        op = i.op
        val = None
        if op == "lui":
            val = ("c", i.uimm << 16)
        elif op in ("addiu", "addi") and regs.get(i.rs, (None,))[0] in ("c", "ci"):
            kind, v = regs[i.rs]
            val = (kind, (v + i.imm) & 0xFFFFFFFF)
        elif op == "ori" and regs.get(i.rs, (None,))[0] == "c":
            val = ("c", regs[i.rs][1] | i.uimm)
        elif op == "addu":
            a_, b_ = regs.get(i.rs), regs.get(i.rt)
            if a_ and a_[0] == "c" and a_[1] != 0 and not (b_ and b_[0] == "c"):
                val = ("ci", a_[1])
            elif b_ and b_[0] == "c" and b_[1] != 0 and not (a_ and a_[0] == "c"):
                val = ("ci", b_[1])
            elif a_ and b_ and a_[0] == "c" and b_[0] == "c":
                val = ("c", (a_[1] + b_[1]) & 0xFFFFFFFF)
        elif op == "lw" and regs.get(i.rs, (None,))[0] == "ci":
            val = ("ld", (regs[i.rs][1] + i.imm) & 0xFFFFFFFF)
        elif op in ("sltiu", "slti"):
            sltiu_bound = i.imm
        elif op == "andi" and i.uimm and (i.uimm & (i.uimm + 1)) == 0:
            sltiu_bound = i.uimm + 1  # index masked to a power of two
        w = i.writes()
        if w:
            if val is None:
                regs.pop(w, None)
            else:
                regs[w] = val
    return regs, sltiu_bound


def _linear_back(fn_addrs: set, addr: int, n: int):
    out = []
    a = addr - 4
    while a in fn_addrs and len(out) < n:
        out.append(a)
        a -= 4
    return out[::-1]


def explore(image: Image, start: int, known: set) -> Function:
    """Explore one function from `start`. `known` holds other function starts (tail-call targets)."""
    fn = Function(image, start)
    fn.labels.add(start)
    queue = [start]
    delay_slots = set()
    while queue:
        a = queue.pop()
        if a in delay_slots and a + 4 not in fn.addrs:
            # Jumped straight into a delay slot: it runs as a normal instruction, then falls through.
            queue.append(a + 4)
            continue
        while a not in fn.addrs:
            if a != start and a in known and a not in fn.labels:
                fn.tail_calls[a] = a  # fell through into the next function
                break
            if not image.contains(a):
                raise AnalysisError(f"{start:08X}: runs off image at {a:08X}")
            i = image.insn(a)
            if i.op == "invalid":
                raise AnalysisError(f"{start:08X}: invalid instruction {i.word:08X} at {a:08X}")
            fn.addrs.add(a)
            if i.op == "break":
                break  # trap after a fatal error; nothing meaningful follows
            if not i.has_delay_slot:
                a += 4
                continue

            d = image.insn(a + 4)
            if d.op == "invalid" or d.has_delay_slot:
                raise AnalysisError(f"{start:08X}: bad delay slot at {a + 4:08X}")
            fn.addrs.add(a + 4)
            delay_slots.add(a + 4)
            stop = False
            if i.is_branch:
                fn.labels.add(i.target)
                queue.append(i.target)
                unconditional = (i.op == "beq" and i.rs == i.rt) or (i.op == "bgez" and i.rs == 0)
                stop = unconditional
            elif i.op == "j":
                if i.target in known and i.target != start:
                    fn.tail_calls[a] = i.target
                else:
                    fn.labels.add(i.target)
                    queue.append(i.target)
                stop = True
            elif i.op == "jal":
                fn.calls.add(i.target)
                fn.call_sites[a] = i.target
            elif i.op == "jr":
                stop = True
                if i.rs != RA:
                    _resolve_jr(image, fn, i)
                    for t in filter(None, fn.jump_tables.get(a, [])):
                        fn.labels.add(t)
                        queue.append(t)
            a += 8
            if stop:
                break

    fn.end = max(fn.addrs) + 4
    _collect_pointers(image, fn)
    return fn


def _resolve_jr(image: Image, fn: Function, i: Insn):
    window = _linear_back(fn.addrs, i.addr, JTBL_WINDOW)
    regs, bound = _const_window(image, window)
    val = regs.get(i.rs)
    if val and val[0] == "c" and val[1] in BIOS_VECTORS:
        fn.bios_jumps[i.addr] = val[1]
        return
    if val and val[0] == "ld" and image.holder(val[1]):
        table = val[1]
        tables = image.holder(table)  # may live in another module (ROM .rodata for RAM code)
        targets = []
        limit = bound if bound and bound > 0 else 1024
        for n in range(limit):
            ea = table + 4 * n
            if not tables.contains(ea):
                break
            t = tables.word(ea)
            if t == 0 and bound:
                targets.append(None)  # unused case
                continue
            if t & 3 or not (fn.start <= t < image.end) or (bound is None and t - fn.start > 0x10000):
                break
            targets.append(t)
        if any(targets):
            fn.jump_tables[i.addr] = targets
            return
    targets = _guess_hoisted_table(image, fn, i)
    if targets:
        fn.jump_tables[i.addr] = targets
        return
    fn.dynamic_jumps.add(i.addr)


def _guess_hoisted_table(image: Image, fn: Function, jr: Insn):
    """Jump table whose address was computed far from the `jr` (e.g. hoisted out of a loop).

    Looks at every lui/addiu constant between the function start and the jr, and picks the
    data table with the most consecutive entries pointing near the jr.
    """
    best = []
    hi = {}
    for a in range(fn.start, jr.addr, 4):
        i = image.insn(a)
        if i.op == "lui":
            hi[i.rt] = i.uimm << 16
            continue
        if i.op == "addiu" and i.rs in hi:
            table = (hi[i.rs] + i.imm) & 0xFFFFFFFF
            entries = []
            ea = table
            while image.contains(ea) and len(entries) < 1024:
                t = image.word(ea)
                if t & 3 or not (fn.start <= t < jr.addr + 0x4000):
                    break
                entries.append(t)
                ea += 4
            if len(entries) >= 2 and len(entries) > len(best):
                best = entries
        # The lui may sit on another path, so remember it even past later writes:
        # the table's entries validate the guess.
    return best


def _collect_pointers(image: Image, fn: Function):
    """lui/addiu pairs inside the function that point into the image (function pointer candidates)."""
    hi = {0: 0}  # addresses below 0x10000 are built from $zero without a lui
    for a in sorted(fn.addrs):
        i = image.insn(a)
        if a in fn.labels:
            hi = {0: 0}
        if i.op == "lui":
            hi[i.rt] = i.uimm << 16
        elif i.op in ("addiu", "ori") and i.rs in hi:
            v = (hi[i.rs] + i.imm) & 0xFFFFFFFF if i.op == "addiu" else hi[i.rs] | i.uimm
            if image.contains(v) and v % 4 == 0:
                fn.pointers.add(v)
        w = i.writes()
        if w and i.op != "lui":
            hi.pop(w, None)


def looks_like_function_start(image: Image, addr: int) -> bool:
    i = image.insn(addr)
    if not (i.op == "addiu" and i.rs == 29 and i.rt == 29 and i.imm < 0):
        return False
    # A stack adjustment in a delay slot is mid-function.
    return addr - 4 < image.base or not image.insn(addr - 4).has_delay_slot


def discover(images: dict, seeds: dict, resident: list, continuations=(), max_rounds=20):
    """Discover functions in all images.

    images: name -> Image. resident: modules every module can call into directly
    (the exe, BIOS ROM/RAM); other modules (overlays) only see themselves + resident ones.
    seeds: name -> set of start addresses.
    continuations: addresses of setjmp-like functions. Code right after each call to one can
    be resumed by a longjmp, so the return address becomes an entry point of its own.
    Seeds found speculatively (pointers in data) are dropped silently if they don't
    explore cleanly; failures of real seeds are reported.
    Returns (functions: name -> {start: Function}, errors: list[str]).
    """
    for name, image in images.items():
        image.peers = [images[r] for r in resident if r != name]
    funcs = {name: {} for name in images}
    starts = {name: set(seeds.get(name, ())) for name in images}
    speculative = {name: set() for name in images}
    rejected = {name: set() for name in images}
    errors = []
    for _ in range(max_rounds):
        errors = []
        for name, image in images.items():
            funcs[name] = {}
            bad = set()
            for s in sorted(starts[name]):
                try:
                    funcs[name][s] = explore(image, s, starts[name])
                except AnalysisError as e:
                    if s not in speculative[name]:
                        errors.append(f"{name}: {e}")
                    bad.add(s)
            starts[name] -= bad
            rejected[name] |= bad

        new = {name: set() for name in images}
        new_spec = {name: set() for name in images}
        ends = {name: {f.end for f in fs.values()} for name, fs in funcs.items()}
        for name, fs in funcs.items():
            for fn in fs.values():
                for t in fn.calls | set(fn.tail_calls.values()):
                    for target_img in (name, *resident):
                        if images[target_img].contains(t):
                            new[target_img].add(t)
                            break
                for site, t in fn.call_sites.items():
                    if t in continuations:
                        new[name].add(site + 8)
                for p in fn.pointers:
                    if looks_like_function_start(images[name], p) or p in ends[name]:
                        new_spec[name].add(p)
        for name, image in images.items():
            for target_img, ptr in _data_pointers(images, funcs, name, resident):
                new_spec[target_img].add(ptr)
            for gs, ge in find_gaps(image, funcs[name]):
                first = next((a for a in range(gs, ge, 4) if image.word(a) != 0), None)
                if first is not None:
                    new_spec[name].add(first)  # code right after a function, if it's code at all
                new_spec[name].update(a for a in range(gs, ge, 4) if looks_like_function_start(image, a))

        changed = False
        for name in images:
            spec = new_spec[name] - new[name] - starts[name] - rejected[name]
            speculative[name] |= spec
            added = (new[name] | spec) - starts[name] - rejected[name]
            if added:
                starts[name] |= added
                changed = True
        if not changed:
            break

    # Speculative starts that ended up inside another function are internal labels
    # (e.g. jump-table targets seen in data). Drop them and explore once more.
    dropped = False
    for name, fs in funcs.items():
        inside = set().union(*(f.addrs - {f.start} for f in fs.values())) if fs else set()
        for st in speculative[name] & starts[name] & inside:
            starts[name].discard(st)
            dropped = True
    if dropped:
        for name, image in images.items():
            funcs[name] = {st: explore(image, st, starts[name]) for st in sorted(starts[name])}
    return funcs, errors


def _data_pointers(images, funcs, name, resident):
    """Words outside code in image `name` that point at plausible unclaimed function starts."""
    claimed = {n: set().union(*(f.addrs for f in fs.values())) if fs else set() for n, fs in funcs.items()}
    ends = {n: {f.end for f in fs.values()} for n, fs in funcs.items()}
    targets = (name, *(r for r in resident if r != name))
    for addr, w in images[name].words():
        if addr in claimed[name] or w & 3:
            continue
        for t in targets:
            img = images[t]
            if not img.contains(w) or w in claimed[t]:
                continue
            if looks_like_function_start(img, w) or _after_function_end(img, ends[t], w):
                yield t, w
            break


def _after_function_end(image: Image, ends: set, addr: int) -> bool:
    """addr is the first non-padding word after some function's end."""
    a = addr
    for _ in range(8):  # at most a few words of alignment padding
        if a in ends:
            return True
        if a - 4 < image.base or image.word(a - 4) != 0:
            return False
        a -= 4
    return False


def find_gaps(image: Image, fs: dict):
    """Unclaimed code-looking regions between discovered functions: [(start, end)]."""
    claimed = sorted((f.start, f.end) for f in fs.values())
    gaps = []
    pos = image.base
    for s, e in claimed:
        if s > pos:
            gaps.append((pos, s))
        pos = max(pos, e)
    if pos < image.end:
        gaps.append((pos, image.end))
    return gaps
