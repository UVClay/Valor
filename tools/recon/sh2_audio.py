#!/usr/bin/env python3
"""Inspect the level audio: the two music mixes, and the sound banks' samples (host python + numpy).

  sh2_audio.py mix M L                   Mm_l.VB against Mm_lA.VB: per 10 s, RMS of each, of the
                                         difference, the correlation, and the spectral centroid
  sh2_audio.py bank FILE.TAF BANK [OUT]  per VAG of that bank: length, peak, RMS, spectral centroid,
                                         zero crossings, attack/decay shape; with OUT a directory
                                         (under build/), one WAV per VAG (bank's rate: 22050, or
                                         11025 for the voice banks, given as --rate)
  sh2_audio.py same A.TAF B.TAF BANK_A BANK_B   which VAGs are byte-identical between two banks

Writes only WAVs under the directory given; never into the repo's tracked files.
"""
import struct
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from snd_taf import banks  # noqa: E402
from snd_vb import decode  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "extracted/moh/DATA"


def vab_vags(d, off):
    """The VAG byte strings of the VAB at off."""
    nvag = struct.unpack_from("<H", d, off + 0x16)[0]
    nprog = struct.unpack_from("<H", d, off + 0x12)[0]
    ntone = struct.unpack_from("<H", d, off + 0x14)[0]
    hdr = 0x20 + 0x800 + nprog * 16 * 0x20
    sizes = struct.unpack_from("<256H", d, off + hdr)
    p = off + hdr + 0x200
    out = []
    for i in range(nvag):
        n = sizes[i + 1] * 8
        out.append(d[p:p + n])
        p += n
    return out


def feats(x, rate):
    x = np.asarray(x, dtype=np.float64)
    if len(x) == 0:
        return dict(dur=0, peak=0, rms=0, cent=0, zc=0, shape="-")
    peak = np.abs(x).max()
    rms = np.sqrt((x ** 2).mean())
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) if len(x) > 16 else np.zeros(2)
    fr = np.fft.rfftfreq(len(x), 1 / rate) if len(x) > 16 else np.zeros(2)
    cent = (spec * fr).sum() / max(spec.sum(), 1e-9)
    zc = np.count_nonzero(np.diff(np.sign(x))) / max(len(x) / rate, 1e-9) / 2
    # envelope in 8 slices
    sl = np.array_split(np.abs(x), 8)
    env = [s.mean() if len(s) else 0 for s in sl]
    m = max(max(env), 1e-9)
    shape = "".join(" .:-=+*#"[min(7, int(e / m * 7.99))] for e in env)
    return dict(dur=len(x) / rate, peak=peak, rms=rms, cent=cent, zc=zc, shape=shape)


def write_wav(path, x, rate):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.asarray(x, dtype=np.int16).tobytes())


def cmd_mix(m, lvl):
    d = DATA / f"MSN{m}/LVL{lvl}"
    a = np.array(decode((d / f"M{m}_{lvl}.VB").read_bytes()), dtype=np.float64)
    b = np.array(decode((d / f"M{m}_{lvl}A.VB").read_bytes()), dtype=np.float64)
    n = min(len(a), len(b))
    print(f"M{m}_{lvl}: {len(a)/22050:.1f} s / A {len(b)/22050:.1f} s")
    print(f"whole: rms {np.sqrt((a[:n]**2).mean()):.0f} / A {np.sqrt((b[:n]**2).mean()):.0f}"
          f" / diff {np.sqrt(((a[:n]-b[:n])**2).mean()):.0f}")
    step = 22050 * 10
    for i in range(0, n - step + 1, step):
        x, y = a[i:i + step], b[i:i + step]
        r = lambda v: np.sqrt((v ** 2).mean())
        c = np.corrcoef(x, y)[0, 1] if r(x) > 0 and r(y) > 0 else float("nan")
        fx, fy = feats(x, 22050)["cent"], feats(y, 22050)["cent"]
        dd = x - y
        print(f"{i//22050:4d}s rms {r(x):6.0f} A {r(y):6.0f} diff {r(dd):6.0f} corr {c:5.2f}"
              f"  centroid {fx:5.0f} A {fy:5.0f} diff {feats(dd,22050)['cent']:5.0f}")


def cmd_bank(taf, k, out=None, rate=None):
    d = Path(taf).read_bytes()
    _, offs = banks(d)
    vags = vab_vags(d, offs[k])
    rate = rate or (11025 if len(vags) in (37, 81) else 22050)
    if out:
        Path(out).mkdir(parents=True, exist_ok=True)
    for i, v in enumerate(vags):
        x = decode(v)
        f = feats(x, rate)
        print(f"{i:3d} 0x{i:02X} {f['dur']:6.2f}s peak {f['peak']:6.0f} rms {f['rms']:6.0f}"
              f" cent {f['cent']:5.0f} zc {f['zc']:5.0f} |{f['shape']}|")
        if out:
            write_wav(Path(out) / f"{i:02X}.wav", x, rate)


def cmd_same(ta, tb, ka, kb):
    va = vab_vags(Path(ta).read_bytes(), banks(Path(ta).read_bytes())[1][ka])
    vb = vab_vags(Path(tb).read_bytes(), banks(Path(tb).read_bytes())[1][kb])
    for i in range(max(len(va), len(vb))):
        x = va[i] if i < len(va) else None
        y = vb[i] if i < len(vb) else None
        print(f"0x{i:02X} {'same' if x == y else 'differs'}"
              f" ({len(x) if x else '-'} / {len(y) if y else '-'})")


if __name__ == "__main__":
    a = sys.argv[1:]
    rate = None
    if "--rate" in a:
        j = a.index("--rate")
        rate = int(a[j + 1])
        del a[j:j + 2]
    if a[0] == "mix":
        cmd_mix(int(a[1]), int(a[2]))
    elif a[0] == "bank":
        cmd_bank(a[1], int(a[2]), a[3] if len(a) > 3 else None, rate)
    elif a[0] == "same":
        cmd_same(a[1], a[2], int(a[3]), int(a[4]))
    else:
        print(__doc__)
