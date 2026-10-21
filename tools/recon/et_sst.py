#!/usr/bin/env python3
"""Model scripts by model (no live entity needed), and script-set comparisons between levels
(docs/re/enemy_types.md).

  et_sst.py RAMDUMP --type 1405 [SCRIPT...]   disassemble the first model of that type
  et_sst.py RAMDUMP --model HASH [SCRIPT...]  ... of the model with that hash
  et_sst.py --diff DUMP_A DUMP_B [--type 1402] match B's scripts to A's by content: per script
                                               of B, the A script with the same code (=), or
                                               the closest one by instruction diff (~N changed)
                                               (--detail N: the changed lines, N lines of new ones)
  et_sst.py --grep REGEX RAMDUMP... [--type T] disassembled lines matching REGEX, per level

Jump targets and script numbers differ between levels, so the diff masks goto/if targets and
script-number operands (other_script, call, jump_script, start_script) before comparing.
"""
import difflib
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import sst  # noqa: E402


def find_model(ram, typ=None, hsh=None):
    n, base = ram.u32(0x8009EC54), ram.u32(0x8009EC58)
    for i in range(n):
        m = base + i * 0x100
        if typ is not None and ram.u32(m + 0xFC) & 0xFFFF == typ and ram.u16(m + 0xBC):
            return m
        if hsh is not None and ram.u32(m) == hsh:
            return m
    return None


def script_words(ram, model):
    lst, count = ram.u32(model + 0xB8), ram.u16(model + 0xBC)
    out = []
    for k in range(count):
        s = lst + 16 * k
        n, code = ram.u16(s + 0xA), ram.u32(s + 0xC)
        out.append((ram.s16(s + 4), [ram.u32(code + 4 * j) for j in range(n)]))
    return out


SCRIPT_OPS = (0x08, 0x1D, 0x38, 0x3B)
JUMP_OPS = (0x07, 0x26)


def masked(words):
    """Words with jump targets and script numbers zeroed (in events' stored instructions too),
    and the script numbers referenced, in order."""
    w = list(words)
    refs = []
    i = 0
    while i < len(w):
        op = w[i] >> 24
        n = max(sst.length(op, w, i), 1)
        k = i
        if op in (0x0D, 0x0E, 0x42):
            k = i + 1 + bin(w[i] & 0xFF).count("1")
            if k >= len(w):
                break
            op = w[k] >> 24
        if op in SCRIPT_OPS and not (w[k] >> 16) & 1:
            refs.append(w[k] & 0xFFFF)
            w[k] &= 0xFFFF0000
        elif op in JUMP_OPS:
            w[k] &= 0xFFFF0000
        elif op == 0x27 and k + 3 < len(w):
            w[k + 3] = 0
        i += n
    return w, refs


def norm_lines(words):
    w, _ = masked(words)
    return [re.sub(r"^\s*\d+: ([0-9a-f]{8} )*[0-9a-f]{8}\s+", "", ln) for ln in sst.disassemble(w)]


def load(path, typ):
    ram = sst.Ram(path)
    m = find_model(ram, typ=typ)
    start = ram.u32(m + 0x94)
    return ram, m, start, script_words(ram, m)


DETAIL = 0


def diff(a, b, typ):
    _, _, sa, A = load(a, typ)
    _, _, sb, B = load(b, typ)
    an = [norm_lines(w) for _, w in A]
    bn = [norm_lines(w) for _, w in B]
    ah = {}
    for i, l in enumerate(an):
        ah.setdefault(hashlib.md5("\n".join(l).encode()).hexdigest(), []).append(i)
    print(f"A {a}: {len(A)} scripts, start {sa}; B {b}: {len(B)} scripts, start {sb}")
    match = {}
    d = 0
    for j, l in enumerate(bn):
        h = hashlib.md5("\n".join(l).encode()).hexdigest()
        if h in ah:
            match[j] = min(ah[h], key=lambda i: abs(i - (j + d)))
            if len(ah[h]) == 1:
                d = match[j] - j
    for j, l in enumerate(bn):
        if j in match:
            ra, rb = masked(A[match[j]][1])[1], masked(B[j][1])[1]
            bad = [(x, y) for x, y in zip(ra, rb) if match.get(y, -1) != x]
            note = f"  refs differ: " + ", ".join(f"A{x}/B{y}" for x, y in bad) if bad else ""
            print(f"  B{j:3} = A{match[j]}{note}")
            continue
        best, bi = 0.0, -1
        for i, la in enumerate(an):
            if abs(len(la) - len(l)) > max(20, len(l)):
                continue
            r = difflib.SequenceMatcher(None, la, l, autojunk=False).ratio()
            if r > best:
                best, bi = r, i
        if bi >= 0 and best > 0.5:
            sm = difflib.SequenceMatcher(None, an[bi], l, autojunk=False)
            ch = sum(max(i2 - i1, j2 - j1) for t, i1, i2, j1, j2 in sm.get_opcodes() if t != "equal")
            print(f"  B{j:3} ~ A{bi} ({ch} lines changed of {len(l)}, ratio {best:.2f})")
            if DETAIL:
                for t, i1, i2, j1, j2 in sm.get_opcodes():
                    if t != "equal":
                        for x in an[bi][i1:i2]:
                            print(f"        - {x}")
                        for x in l[j1:j2]:
                            print(f"        + {x}")
        else:
            print(f"  B{j:3} new ({len(l)} lines)")
            if DETAIL:
                for x in l[:DETAIL]:
                    print(f"        {x}")


def main():
    a = sys.argv[1:]
    typ = 0x1402
    if "--type" in a:
        k = a.index("--type")
        typ = int(a[k + 1], 16)
        del a[k:k + 2]
    global DETAIL
    if "--detail" in a:
        k = a.index("--detail")
        DETAIL = int(a[k + 1])
        del a[k:k + 2]
    if a[0] == "--diff":
        diff(a[1], a[2], typ)
        return
    if a[0] == "--grep":
        rx = re.compile(a[1])
        for p in a[2:]:
            _, _, _, S = load(p, typ)
            for k, (_, w) in enumerate(S):
                for ln in sst.disassemble(w):
                    if rx.search(ln):
                        print(f"{os.path.basename(p)} {k}: {ln.strip()}")
        return
    ram = sst.Ram(a[0])
    if len(a) > 1 and a[1] == "--model":
        m = find_model(ram, hsh=int(a[2], 16))
        rest = a[3:]
    else:
        m = find_model(ram, typ=typ)
        rest = a[1:]
    S = script_words(ram, m)
    print(f"model {ram.u32(m):08X} at {m:08X} type {ram.u32(m + 0xFC) & 0xFFFF:X}: {len(S)} scripts,"
          f" start {(ram.u32(m + 0x94) + 2**31) % 2**32 - 2**31}")
    which = [int(x, 0) for x in rest] or range(len(S))
    for k in which:
        weight, w = S[k]
        print(f"script {k} (weight {weight}, {len(w)} words)")
        print("\n".join(sst.disassemble(w)))


if __name__ == "__main__":
    main()
