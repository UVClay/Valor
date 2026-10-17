#!/usr/bin/env python3
"""Download PsyQ library signatures (lab313ru/psx_psyq_signatures) into build/psyq_sigs/.

The signatures are derived from Sony's SDK, so they're fetched on demand rather than committed.

Usage: fetch_sigs.py [version ...]     (default: 400 410 420 430 440 450 460 470)
"""
import json
import sys
import urllib.request
from pathlib import Path

REPO = "lab313ru/psx_psyq_signatures"
COMMIT = "e9e46e7e133ef275a79bfce650924f98edb086bc"
DEFAULT_VERSIONS = ["400", "410", "420", "430", "440", "450", "460", "470"]
OUT = Path(__file__).resolve().parents[2] / "build" / "psyq_sigs"


def get(url: str) -> bytes:
    with urllib.request.urlopen(url) as r:
        return r.read()


def main():
    versions = sys.argv[1:] or DEFAULT_VERSIONS
    for ver in versions:
        dest = OUT / ver
        listing = json.loads(get(f"https://api.github.com/repos/{REPO}/contents/{ver}?ref={COMMIT}"))
        names = [e["name"] for e in listing if e["name"].endswith(".json")]
        dest.mkdir(parents=True, exist_ok=True)
        for name in names:
            path = dest / name
            if not path.exists():
                path.write_bytes(get(f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/{ver}/{name}"))
        print(f"{ver}: {len(names)} files")


if __name__ == "__main__":
    main()
