#!/usr/bin/env python3
"""Runs the five reference reimplementations on a recording and checks them against the original:
one command, any recording (./play --record, or a monkey run recorded with --record).

  check.py TAPE_DIR [--work DIR] [--only move,sst,brain,combat,level] [--levels M:L,...]
           [--jobs N] [--bin PATH] [--sst-pairs N] [--display dummy|offscreen]
           [--replay-again] [--save-baseline] [-v]

From the recording alone it
  1. reads the level starts (the tape's "mark F mission M L" lines);
  2. replays it (tools/recon/run.sh's container, named chk_*) with VALOR_TRACE_STATE, the
     watches of every simulator (prefixed, then split into one log each), VALOR_TRACE_SHOTS and
     VALOR_TRACE_DAMAGE; in a second replay, at the same time, the RAM dumps each simulator
     needs per level (before the first tick, a few vblanks in, a hundred in);
  3. replays it again with the brains watched at the addresses the first replay found (a
     pointer watch loses a re-based brain's constant words: brain_sim.py) and the tick pairs
     the script VM is stepped on (RAM dumps one tick apart where soldiers or dogs are alive);
  4. runs every simulator per level and prints one table: system, level, compared, exact,
     within (a documented range or rounding), fail, and the verdict.

The verdict compares with tools/sim/baseline.json: the recording's own results if it has an entry
(--save-baseline writes one), else the agreement the recorded session reached for that system
(its exact share, and no larger share of fails). Exit status 1 on any FAIL. Everything is kept
under --work (default build/simcheck/NAME): a second run only runs the simulators again
(--replay-again replays too). --bin must take --dump-ram more than once and --stop-at.

Version 1 recordings were made before drawing copies were kept off the devices; replayed
without a display they can part from the recording (the 2026-10-19 session does in 2:3): use
--levels to stop before, or --display offscreen (an OpenGL renderer: the copies run as then).
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SYSTEMS = ["move", "sst", "brain", "combat", "level"]
SOLDIER, DOG = 0x1402, 0x1405
PREFIX = {"move": "mv_", "combat": "cb_", "level": "lv_", "find": "bf_"}


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# ---------------------------------------------------------------- the recording

def read_tape(tape):
    """(version, [(M:L, start vblank)], last vblank)."""
    version, levels, last = 1, [], 0
    with open(os.path.join(tape, "input.tape")) as f:
        for line in f:
            if line.startswith("valor-tape "):
                version = int(line.split()[1])
                continue
            w = line.split()
            if len(w) > 1 and w[0] in ("b", "s", "m", "t", "u", "v", "l", "a", "h", "mark") and w[1].isdigit():
                last = max(last, int(w[1]))
            if w[:1] == ["mark"] and len(w) == 5 and w[2] == "mission":
                levels.append((f"{w[3]}:{w[4]}", int(w[1])))
    return version, levels, last


# ---------------------------------------------------------------- replays

def image():
    h = hashlib.sha256()
    for f in ("Containerfile", "requirements.txt"):
        h.update(open(os.path.join(ROOT, f), "rb").read())
    return f"moh-recomp-dev:{h.hexdigest()[:12]}"


def replay(name, tape, out_log, env, args, binary, display, timeout=3600):
    """One replay in the container, as tools/recon/run.sh runs it; killed after timeout."""
    cname = f"chk_{name}"
    subprocess.run(["podman", "rm", "-f", cname], capture_output=True)
    cmd = ["podman", "run", "--rm", "--name", cname, "--userns=keep-id", "--security-opt", "label=disable",
           "-v", f"{ROOT}:/work", "-w", "/work", "-e", "HOME=/work/build/home",
           "-e", "XDG_RUNTIME_DIR=/tmp/runtime", "-e", f"SDL_VIDEODRIVER={display}",
           "--device", "/dev/dri", "-v", "/etc/localtime:/etc/localtime:ro"]
    for k, v in env.items():
        cmd += ["-e", f"{k}={v}"]
    cmd += [image(), binary, "--bios", "build/openbios/openbios.bin",
            "--disc", "disc/Medal of Honor (USA)/Medal of Honor (USA).cue", "--replay", tape, "--hidden"] + args
    t0 = time.time()
    with open(out_log, "w") as f:
        try:
            subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=timeout, cwd=ROOT)
        except subprocess.TimeoutExpired:
            log(f"{cname}: killed after {timeout} s")
        finally:
            subprocess.run(["podman", "kill", cname], capture_output=True)
            subprocess.run(["podman", "rm", "-f", cname], capture_output=True)
    log(f"{cname}: {time.time() - t0:.0f} s")


def sim_watch(system):
    if system == "find":
        return run_py("brain_sim.py", "watchlist").strip()
    return run_py(f"{system}_sim.py" if system != "level" else "level_vm.py", "watch").strip()


def run_py(script, *args):
    return subprocess.run([sys.executable, os.path.join(HERE, script), *args], capture_output=True,
                          text=True, cwd=ROOT, check=True).stdout


def prefixed(system):
    p = PREFIX[system]
    return ";".join(p + item for item in sim_watch(system).split(";"))


def split_log(src, outs):
    """One log per prefix: its watch lines with the prefix taken off, every other line kept."""
    files = {p: open(path, "w") for p, path in outs.items()}
    pat = re.compile(r"^(watch:? (?:\d+ )?)([a-z]+_)(\w)")
    with open(src, errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if m:
                out = files.get(m[2])
                if out:
                    out.write(m[1] + line[len(m[1]) + len(m[2]):])
                continue
            for out in files.values():
                out.write(line)
    for out in files.values():
        out.close()


def read_trace(path):
    out = []
    with open(path) as f:
        for line in f:
            try:
                d = json.loads(line)
            except json.JSONDecodeError:
                break
            alive = sum(1 for e in d["ents"] if e[1] in (SOLDIER, DOG) and e[9] > 0)
            out.append((d["tick"], d["vblank"], alive))
    return out


def sst_pairs(trace, lo, hi, n):
    """Up to n vblanks V in [lo, hi) to dump at V and V + 1: a tick's, the next tick two vblanks
    on, a soldier or dog alive; evenly spread."""
    cand = [trace[i][1] for i in range(len(trace) - 1)
            if lo + 30 <= trace[i][1] < hi - 4 and trace[i][2] and trace[i + 1][0] == trace[i][0] + 1
            and trace[i + 1][1] == trace[i][1] + 2]
    if len(cand) <= n:
        return cand
    return [cand[(2 * k + 1) * len(cand) // (2 * n)] for k in range(n)]


# ---------------------------------------------------------------- the simulators' outputs

def ints(s):
    return [int(x) for x in s.split()]


def parse_table(text, first_col):
    """move_sim / brain_sim: behaviour rows (not the "(...)" notes): ticks, exact, <=1/16, <=1,
    <=8, fail."""
    r = dict(compared=0, exact=0, within=0, fail=0)
    on = False
    for line in text.splitlines():
        if line.startswith(first_col):
            on = True
            continue
        if not on:
            continue
        if not line.strip() or line.startswith("fields"):
            on = False
            continue
        if line.startswith("("):
            continue
        m = re.match(r"^(.*?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", line)
        if m:
            t, e, a, b, c, f = (int(x) for x in m.groups()[1:])
            r["compared"] += t
            r["exact"] += e
            r["within"] += a + b + c
            r["fail"] += f
    return r


def parse_combat(text):
    r = dict(compared=0, exact=0, within=0, fail=0)
    on = False
    for line in text.splitlines():
        if line.startswith("behaviour"):
            on = True
            continue
        if on and not line.strip():
            break
        m = re.match(r"^(.*?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)$", line) if on else None
        # The watch replayed onto the seed dumps checks the inputs, not combat.
        if m and not m[1].startswith("watch replay"):
            n, e, w, f = (int(x) for x in m.groups()[1:])
            r["compared"] += n
            r["exact"] += e
            r["within"] += w
            r["fail"] += f
    return r


def parse_level(text):
    r = dict(compared=0, exact=0, within=0, fail=0)
    for m in re.finditer(r"^  (\S.*?)\s+(\d+) /\s+(\d+)  (exact|FAILS)$", text, re.M):
        ok, n = int(m[2]), int(m[3])
        r["compared"] += n
        r["exact"] += ok
        r["fail"] += n - ok
    return r


def parse_sst(text):
    m = re.search(r"^  ents: (\d+)/(\d+)", text, re.M)
    ok, n = (int(m[1]), int(m[2])) if m else (0, 0)
    return dict(compared=n, exact=ok, within=0, fail=n - ok)


PARSE = {"move": lambda t: parse_table(t, "behaviour"), "brain": lambda t: parse_table(t, "behaviour"),
         "combat": parse_combat, "level": parse_level, "sst": parse_sst}


# ---------------------------------------------------------------- verdicts

def verdict(res, base, generic):
    if res is None:
        return "ERROR"
    if not res["compared"]:
        return "none"
    if base:
        if res["fail"] <= base["fail"] and res["exact"] * base["compared"] >= base["exact"] * res["compared"]:
            return "PASS"
        return "FAIL"
    if generic is None:
        return "new"
    ex = res["exact"] / res["compared"]
    fl = res["fail"] / res["compared"]
    return "PASS" if ex >= generic["exact"] - 1e-12 and fl <= generic["fail"] + 1e-12 else "FAIL"


def generic_thresholds(baselines):
    """Per system, the recorded session's exact share and fail share over all its levels."""
    sess = baselines.get("2026-10-19_22-22-43_m1l3", {}).get("results", {})
    out = {}
    for system, levels in sess.items():
        n = sum(r["compared"] for r in levels.values())
        if n:
            out[system] = dict(exact=sum(r["exact"] for r in levels.values()) / n,
                               fail=sum(r["fail"] for r in levels.values()) / n)
    return out


# ---------------------------------------------------------------- main

def segments(trace):
    """The levels as the replay played them: [(first vblank, end vblank)], a new one wherever the
    tick counter starts again (a level start; a restart after death keeps the counter going,
    so a level and its restarts are one segment: level_vm.py restarts inside it)."""
    out = []
    for k, (tick, v, _) in enumerate(trace):
        if k == 0 or tick < trace[k - 1][0]:
            if out:
                out[-1][1] = v
            out.append([v, None])
    if out:
        out[-1][1] = trace[-1][1] + 1
    return [tuple(x) for x in out]


def mission_level(path):
    """"M:L" from a RAM dump: the campaign's 0-based mission and level at 0x80038660."""
    with open(path, "rb") as f:
        f.seek(0x38660)
        m, l = struct.unpack("<ii", f.read(8))
    return f"{m + 1}:{l + 1}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tape")
    ap.add_argument("--work")
    ap.add_argument("--only", default=",".join(SYSTEMS))
    ap.add_argument("--levels", help="M:L,... (default: every level the replay plays)")
    ap.add_argument("--jobs", type=int, default=6, help="containers at once (default 6)")
    ap.add_argument("--bin", default="build/cmake/valor")
    ap.add_argument("--sst-pairs", type=int, default=20, help="tick pairs per level for the script VM")
    ap.add_argument("--min-ticks", type=int, default=60, help="shorter levels (or restarts) are left out")
    ap.add_argument("--display", default="dummy", choices=["dummy", "offscreen"])
    ap.add_argument("--replay-again", action="store_true")
    ap.add_argument("--save-baseline", action="store_true")
    ap.add_argument("-v", action="store_true", help="print each simulator's report")
    a = ap.parse_args()
    t_start = time.time()
    tape = os.path.relpath(os.path.abspath(a.tape), ROOT)
    name = os.path.basename(os.path.normpath(tape))
    work = os.path.abspath(a.work or os.path.join(ROOT, "build", "simcheck", name))
    wrel = os.path.relpath(work, ROOT)
    os.makedirs(work, exist_ok=True)
    systems = [s for s in a.only.split(",") if s]
    want = a.levels.split(",") if a.levels else None
    version, starts, last = read_tape(tape)
    log(f"{name}: tape version {version}, the recording starts " + ", ".join(f"{lv} at {v}" for lv, v in starts))
    if version < 2 and a.display == "dummy":
        log("a version 1 recording replayed without a display: it can part from the recording (see --display)")
    # With --levels, the replay can stop at the recording's next level start after the last one.
    stop = []
    if want:
        idx = [k for k, (lv, _) in enumerate(starts) if lv in want]
        if idx and idx[-1] + 1 < len(starts):
            stop = ["--stop-at", str(starts[idx[-1] + 1][1] + 2)]
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs))
    bin_ = a.bin
    tag = re.sub(r"[^A-Za-z0-9_.-]", "_", name)[-24:]

    # Replay 1: the trace and the watches.
    trace = os.path.join(work, "trace.jsonl")
    p1log = os.path.join(work, "replay1.log")
    stamp1 = os.path.join(work, "replay1.done")
    if a.replay_again or not os.path.exists(stamp1):
        watch = ";".join(prefixed(s) for s in ("move", "combat", "level") if s in systems)
        if "brain" in systems:
            watch = ";".join(x for x in (watch, prefixed("find")) if x)
        env = dict(VALOR_TRACE_STATE=f"{wrel}/trace.jsonl", VALOR_TRACE_SHOTS="1", VALOR_TRACE_DAMAGE="1",
                   VALOR_TRACE_INPUT="1")
        if watch:
            env["VALOR_WATCH"] = watch
        log("replay 1: the state trace and the simulators' watches")
        replay(f"{tag}_w", tape, p1log, env, stop, bin_, a.display)
        split_log(p1log, {PREFIX[s]: os.path.join(work, f"{s}.log") for s in ("move", "combat", "level", "find")})
        open(stamp1, "w").close()
    parted = [l.strip() for l in open(p1log, errors="replace") if l.startswith("replay: the game differs")]
    if parted:
        log(f"the replay parted from the recording: {parted[0]}")
    tr = read_trace(trace)
    segs = [(v0, v1) for v0, v1 in segments(tr)
            if sum(1 for _, v, _ in tr if v0 <= v < v1) >= a.min_ticks]
    log(f"trace: {len(tr)} ticks, {len(segs)} levels or restarts")

    # Replay 2: per level a dump before its first tick, a few vblanks in and a hundred in; the
    # brains watched at the addresses replay 1 found; the script VM's tick pairs.
    dumps = {}
    for v0, v1 in segs:
        dumps[v0] = dict(pre=v0 - 1, first=v0 + 1, seed=min(v0 + 7, v1 - 1), later=min(v0 + 100, v1 - 1))
    pairs = {}
    if "sst" in systems:
        pairs = {str(v0): sst_pairs(tr, v0, v1, a.sst_pairs) for v0, v1 in segs}
    path = lambda v: f"{wrel}/d{v}.bin"
    stamp2 = os.path.join(work, "replay2.done")
    if a.replay_again or not os.path.exists(stamp2):
        jobs = []
        vs = sorted({v for d in dumps.values() for v in d.values()})
        if vs:
            args = [x for v in vs for x in ("--dump-ram", f"{v}:{path(v)}")]
            jobs.append(pool.submit(replay, f"{tag}_d", tape, os.path.join(work, "dumps.log"), {},
                                    args + ["--stop-at", str(vs[-1] + 2)], bin_, a.display))
        pv = sorted({x for vs2 in pairs.values() for v in vs2 for x in (v, v + 1)})
        if pv:
            log(f"replay 2: {len(pv) // 2} tick pairs for the script VM")
            # several containers, each up to its own last pair
            n = max(1, min(a.jobs - 2, len(pv) // 40))
            for k in range(n):
                part = pv[k * len(pv) // n:(k + 1) * len(pv) // n]
                args = [x for v in part for x in ("--dump-ram", f"{v}:{path(v)}")]
                jobs.append(pool.submit(replay, f"{tag}_p{k}", tape, os.path.join(work, f"pairs{k}.log"), {},
                                        args + ["--stop-at", str(part[-1] + 2)], bin_, a.display))
        if "brain" in systems:
            brains = set()
            for line in open(os.path.join(work, "find.log"), errors="replace"):
                m = re.match(r"^watch \d+ b\d at ([0-9A-F]+)", line)
                if m:
                    b = int(m[1], 16)
                    if 0x80000000 <= b < 0x801FFE00 and not b & 3:
                        brains.add(b)
            log(f"replay 2: {len(brains)} brain addresses watched")
            env = dict(VALOR_WATCH=run_py("brain_sim.py", "watchlist", *(f"{b:X}" for b in sorted(brains))).strip(),
                       VALOR_TRACE_STATE=f"{wrel}/trace2.jsonl")
            jobs.append(pool.submit(replay, f"{tag}_b", tape, os.path.join(work, "brain.log"), env, stop,
                                    bin_, a.display))
        for j in jobs:
            j.result()
        missing = [v for v in vs + pv if not os.path.exists(os.path.join(ROOT, path(v)))]
        if missing:
            sys.exit(f"replay 2 left no dumps at {missing[:5]}: does {bin_} take --dump-ram more than once?")
        if "brain" in systems:
            # The replays must be the same game, tick for tick.
            if open(trace, "rb").read() != open(os.path.join(work, "trace2.jsonl"), "rb").read():
                log("WARNING: replay 2's trace differs from replay 1's: the brain check is meaningless")
        open(stamp2, "w").close()

    # The levels: what the dump before the first tick holds; a level played again (a restart
    # after death, a new game) gets a suffix.
    levels, seen = [], {}
    for v0, v1 in segs:
        ml = mission_level(os.path.join(ROOT, path(dumps[v0]["pre"])))
        if want and ml not in want:
            continue
        seen[ml] = seen.get(ml, 0) + 1
        levels.append((ml if seen[ml] == 1 else f"{ml}#{seen[ml]}", ml, v0, v1))
    log("levels: " + ", ".join(f"{lv} [{v0}, {v1})" for lv, _, v0, v1 in levels))

    def cmd_for(system, ml, v0, v1):
        d = {k: (v, path(v)) for k, v in dumps[v0].items()}
        if system == "move":
            v, p = d["seed"]
            # the dumps before the first tick and after the first frame too: a watch logs changes
            # only, from a first copy taken after the first frame's ticks (a dump at V is taken
            # before frame V's), so until the seed a word unchanged since would be stale
            w, q = d["pre"]
            x, r = d["first"]
            return ["move_sim.py", "check", trace, os.path.join(work, "move.log"), ml, "--from", str(v0),
                    "--to", str(v1), "--ram", f"{q}@{w}", "--ram", f"{r}@{x}", "--ram", f"{p}@{v}", "--free",
                    "--input", os.path.join(work, "replay1.log")]
        if system == "combat":
            v, p = d["seed"]
            return ["combat_sim.py", "check", trace, os.path.join(work, "combat.log"), "--seed", f"{v}:{p}",
                    "--from", str(v0), "--to", str(v1 - 1), "--level", ml]
        if system == "level":
            (v, p), (w, q) = d["pre"], d["later"]
            return ["level_vm.py", "check", os.path.join(work, "level.log"), trace, f"{p}@{v},{q}@{w}"]
        if system == "brain":
            v, p = d["seed"]
            return ["brain_sim.py", "watch", os.path.join(work, "brain.log"), trace, f"{p}@{v}",
                    "--from", str(v0), "--to", str(v1), "--level", ml]
        if system == "sst":
            vs2 = [str(v) for v in pairs.get(str(v0), [])]
            return ["sst_vm.py", "--dir", work, "--pairs", *vs2] if vs2 else None

    def run_sim(system, lv, ml, v0, v1):
        c = cmd_for(system, ml, v0, v1)
        out = os.path.join(work, "out", f"{system}_{lv.replace(':', '_').replace('#', '_')}.txt")
        if c is None:
            return system, lv, dict(compared=0, exact=0, within=0, fail=0), ""
        p = subprocess.run([sys.executable, os.path.join(HERE, c[0]), *c[1:]], capture_output=True, text=True,
                           cwd=ROOT)
        text = p.stdout + (("\n" + p.stderr) if p.stderr else "")
        with open(out, "w") as f:
            f.write(" ".join(c) + "\n\n" + text)
        if p.returncode:
            return system, lv, None, text
        return system, lv, PARSE[system](p.stdout), text

    os.makedirs(os.path.join(work, "out"), exist_ok=True)
    log("simulators: " + ", ".join(systems))
    simpool = concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(6, os.cpu_count() or 2)))
    futs = [simpool.submit(run_sim, s, lv, ml, v0, v1) for s in systems for lv, ml, v0, v1 in levels]
    results = {}
    for f in futs:
        system, lv, r, text = f.result()
        results.setdefault(system, {})[lv] = r
        if a.v or r is None:
            print(f"\n==== {system} {lv}\n{text}")

    bpath = os.path.join(HERE, "baseline.json")
    baselines = json.load(open(bpath)) if os.path.exists(bpath) else {}
    mine = baselines.get(name, {}).get("results", {})
    generic = generic_thresholds(baselines)
    print(f"\n{'system':8s} {'level':7s} {'compared':>9s} {'exact':>9s} {'within':>8s} {'fail':>6s}  {'exact %':>8s}  verdict")
    bad = False
    for s in systems:
        for lv, _, _, _ in levels:
            r = results.get(s, {}).get(lv)
            v = verdict(r, mine.get(s, {}).get(lv), generic.get(s) if not mine else None)
            bad |= v in ("FAIL", "ERROR")
            if r is None:
                print(f"{s:8s} {lv:7s} {'(error: see ' + wrel + '/out)':>36s}  {v}")
                continue
            pct = f"{100 * r['exact'] / r['compared']:.3f}" if r["compared"] else "-"
            print(f"{s:8s} {lv:7s} {r['compared']:9d} {r['exact']:9d} {r['within']:8d} {r['fail']:6d}  {pct:>8s}  {v}")
    print(f"\nreports: {wrel}/out/; {time.time() - t_start:.0f} s")
    if a.save_baseline:
        baselines[name] = dict(levels=[lv for lv, _, _, _ in levels], sst_pairs=a.sst_pairs,
                               results={s: {lv: r for lv, r in results[s].items() if r} for s in systems})
        with open(bpath, "w") as f:
            json.dump(baselines, f, indent=1, sort_keys=True)
            f.write("\n")
        log(f"baseline for {name} written to {os.path.relpath(bpath, ROOT)}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
