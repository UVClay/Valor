#!/usr/bin/env python3
"""Find PsyQ library objects in PS1 executables by signature.

Usage:
  match_sigs.py <binary> --score               score each downloaded PsyQ version
  match_sigs.py <binary> [--out symbols.txt]   match all versions, keep the strongest
                                               non-overlapping matches, write symbols

Games often mix library versions, so the default mode combines every version: at each
address the match with the most fixed (non-wildcard) bytes wins.
Output symbols are `name = 0xADDR; // LIB:OBJ ver` lines (splat/linker style).
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recon"))
from psxdis import load  # noqa: E402

SIGS = Path(__file__).resolve().parents[2] / "build" / "psyq_sigs"
MIN_BYTES = 24       # ignore tiny object signatures; they match all over the place
MIN_FUNC_BYTES = 48  # per-function fallback needs more evidence
LOCAL_LABEL = re.compile(r"^(loc|text|data|rdata|sdata|bss)_[0-9A-F]+$")


def compile_sig(tokens):
    tokens = list(tokens)
    while tokens and tokens[-1] in ("00", "??"):
        tokens.pop()  # trailing alignment padding isn't part of the code
    fixed = sum(t != "??" for t in tokens)
    pattern = b"".join(b"." if t == "??" else re.escape(bytes.fromhex(t)) for t in tokens)
    return re.compile(pattern, re.DOTALL), fixed, len(tokens)


def pieces(obj):
    """The whole object, then each labelled function on its own (for objects that differ slightly)."""
    tokens = obj["sig"].split()
    labels = sorted((l for l in obj.get("labels", []) if not LOCAL_LABEL.match(l["name"])),
                    key=lambda l: l["offset"])
    yield "obj", tokens, obj.get("labels", [])
    for i, label in enumerate(labels):
        end = labels[i + 1]["offset"] if i + 1 < len(labels) else len(tokens)
        yield "func", tokens[label["offset"]:end], [{"name": label["name"], "offset": 0}]


def scan(data: bytes, base: int, version: str, min_obj=MIN_BYTES, min_func=MIN_FUNC_BYTES):
    """Yield one dict per 4-aligned match."""
    for lib_file in sorted((SIGS / version).glob("*.json")):
        lib = lib_file.name.removesuffix(".json")
        for obj in json.loads(lib_file.read_text()):
            if "sig" not in obj:
                continue  # bss-only object, nothing to match
            for kind, tokens, labels in pieces(obj):
                regex, fixed, length = compile_sig(tokens)
                if fixed < (min_obj if kind == "obj" else min_func):
                    continue
                for m in regex.finditer(data):
                    if m.start() % 4 == 0:
                        yield {"lib": lib, "obj": obj["name"], "ver": version, "kind": kind,
                               "addr": base + m.start(), "fixed": fixed, "len": length, "labels": labels}


def best_matches(data: bytes, base: int, versions):
    """Greedy: strongest matches first, drop anything overlapping an accepted match.

    A signature that matches in several places is ambiguous (generic code, inlined copies)
    and is dropped entirely.
    """
    hits = [h for ver in versions for h in scan(data, base, ver)]
    places = {}
    for h in hits:
        places.setdefault((h["obj"], h["kind"], h["labels"][0]["name"] if h["labels"] else ""), set()).add(h["addr"])
    hits = [h for h in hits if len(places[(h["obj"], h["kind"], h["labels"][0]["name"] if h["labels"] else "")]) == 1]
    hits.sort(key=lambda h: (h["kind"] != "obj", -h["fixed"], h["ver"]))
    taken = []  # (start, end)
    accepted = []
    for h in hits:
        start, end = h["addr"], h["addr"] + h["len"]
        if any(s < end and start < e for s, e in taken):
            continue
        taken.append((start, end))
        accepted.append(h)
    return sorted(accepted, key=lambda h: h["addr"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary", type=Path)
    ap.add_argument("--score", action="store_true")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()

    data, base = load(args.binary)
    versions = sorted(p.name for p in SIGS.iterdir())

    if args.score:
        for ver in versions:
            hits = [h for h in scan(data, base, ver) if h["kind"] == "obj"]
            objs = {(h["lib"], h["obj"]) for h in hits}
            print(f"{ver}: {len(objs):4} objects, {sum(h['fixed'] for h in hits):7} fixed bytes matched")
        return

    accepted = best_matches(data, base, versions)
    lines, by_kind, covered = [], {}, 0
    for h in accepted:
        covered += h["len"]
        by_kind[h["kind"]] = by_kind.get(h["kind"], 0) + 1
        for label in h["labels"]:
            if not LOCAL_LABEL.match(label["name"]):
                lines.append(f"{label['name']} = 0x{h['addr'] + label['offset']:08X}; // {h['lib']}:{h['obj']} {h['ver']}")

    print(f"{by_kind.get('obj', 0)} whole objects + {by_kind.get('func', 0)} single functions, "
          f"{len(lines)} symbols, {covered:#x} of {len(data):#x} bytes ({100 * covered / len(data):.1f}%) covered")
    if args.out:
        args.out.write_text("\n".join(lines) + "\n")
        print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
