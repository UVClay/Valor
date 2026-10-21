#!/usr/bin/env python3
"""--press arguments that type a password on the Enigma screen (Options > Password).

  shell_password.py WORD [START_FRAME] [STEP]   prints the --press arguments

The keyboard (the cursor starts on Q):
  Q W E R T Z U I O
  A S D F G H J K L
  <- P Y X C V B N M SEND
Moves vertically first, then horizontally, one press per STEP frames (default 25); a letter is
selected with cross, SEND ends the word.
"""
import sys

ROWS = ["QWERTZUIO", "ASDFGHJKL", "<PYXCVBNM>"]


def pos(ch):
    for r, row in enumerate(ROWS):
        if ch in row:
            return r, row.index(ch)
    raise SystemExit(f"no key {ch}")


def presses(word, frame, step):
    r, c = 0, 0
    out = []
    for ch in word.upper() + ">":
        r2, c2 = pos(ch)
        while r != r2:
            out.append((frame, "down" if r2 > r else "up"))
            r += 1 if r2 > r else -1
            c = min(c, len(ROWS[r]) - 1)
            frame += step
        while c != c2:
            out.append((frame, "right" if c2 > c else "left"))
            c += 1 if c2 > c else -1
            frame += step
        out.append((frame, "cross"))
        frame += step
    return out, frame


if __name__ == "__main__":
    word = sys.argv[1]
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1700
    step = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    p, end = presses(word, start, step)
    print(" ".join(f"--press {f}:{b}" for f, b in p))
    print(f"# ends at frame {end}", file=sys.stderr)
