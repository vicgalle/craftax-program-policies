#!/usr/bin/env python3
"""Snapshot banks: capture macro-state savestates, resume them with any policy.

capture  Play a cohort from reset with one policy and save the first state at
         each requested macro-state trigger (arrive:K, clear:K, ready:K, up:K),
         or every (subsampled) occurrence of a goal-boundary trigger (leave:K,
         enter:K; --capture-prob, --capture-max).
         The capture run is also an ordinary full run (episodes/visits/tail).
resume   Continue every snapshot of a bank with a (candidate) policy, for R
         replicas. Replica 0 reuses the saved engine RNG, so the capture policy
         reproduces its own episodes exactly; replicas r>0 rekey future engine
         and policy randomness (same world, different dice). Two policies resumed
         on the same bank and replicas are paired by (episode, snap_t, replica).
         --handoff-dir INC --handoff COND runs the candidate until COND (for
         example return:7 = back on Ice, or goalend:fight = the fight open at
         the snapshot has ended) and the incumbent INC afterwards: the local
         comparison of one module invocation. --log-goals writes the goal
         invocation log of hhmm.h (one row per invocation, with parent).

Examples
  python3 bank.py capture --policy-dir ../policies/checkpoints/18_r16 --name b7_D1 --seed 40000000 \
      --episodes 8192 --trigger arrive:5 --trigger arrive:6 --trigger arrive:7 --workers 8
  python3 bank.py resume --policy-dir ../policies/checkpoints/18_r16 --bank build/banks/b7_D1/arrive7.bin \
      --name ice_v1 --replicas 4 --workers 6
  python3 bank_compare.py build/branch_runs/base_arrive7 build/branch_runs/ice_v1

A candidate may only append fields to FPAgent (the loader zero-fills them).
Changes must be inert before the bank's trigger, or the comparison is invalid.
"""
import argparse
import gzip
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import fbf  # noqa: E402
import patch_engine  # noqa: E402

REQUIRED = ("full_policy.c", "full_policy.h")


def policy_files(policy_dir):
    """Every source of a policy directory (full_policy.c/.h and the .inc files it includes)."""
    names = sorted(p.name for p in Path(policy_dir).iterdir() if p.suffix in (".c", ".h", ".inc"))
    missing = [n for n in REQUIRED if n not in names]
    if missing:
        raise SystemExit(f"{policy_dir} is missing {missing}")
    return tuple(names)
CFLAGS = ["-O3", "-std=c11", "-ffp-contract=off", "-Wno-unused-function"]
# Mirrors SnapHeader in branch.c, with its natural-alignment padding (368 bytes).
HEADER = struct.Struct("<8s5IiIif4xdi9i9i9i9i4xQ9QQ2I16i")
assert HEADER.size == 368


def sha(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(Path(p).read_bytes())
    return h.hexdigest()


def runtime_observation_contract(policy_dir):
    """Exact source contract for the shared post-observation handoff ABI."""
    import re
    source = (policy_dir / "full_policy.c").read_text()
    pending = ["perceive", "runtime_observe", "fp_observe"]
    bodies = {}
    while pending:
        name = pending.pop()
        if name in bodies:
            continue
        match = re.search(r"(?m)^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*(?:\s+|\s*\*\s*)" + name + r"\([^;{}]*?\)\s*\{", source)
        if not match:
            if name in ("perceive", "runtime_observe", "fp_observe"):
                raise SystemExit(f"runtime observation ABI is missing {name} in {policy_dir}")
            continue  # libc, macros and control-flow keywords
        end, depth = match.end(), 1
        while depth and end < len(source):
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        body = source[match.start():end]
        bodies[name] = body
        pending.extend(re.findall(r"\b([A-Za-z_]\w*)\s*\(", body))
    return bodies


def build(policy_dir: Path, handoff_dir: Path = None) -> Path:
    if handoff_dir and "#define FP_RUNTIME_ABI" in (policy_dir / "full_policy.h").read_text():
        if (policy_dir / "full_policy.h").read_bytes() != (handoff_dir / "full_policy.h").read_bytes():
            raise SystemExit("runtime hybrid policies require identical headers (including all controller fields)")
        if runtime_observation_contract(policy_dir) != runtime_observation_contract(handoff_dir):
            raise SystemExit("runtime hybrid policies require identical observation and boundary implementations")
    bdir = HERE / "build"
    bdir.mkdir(exist_ok=True)
    engine_src = REPO / "engine" / "craftax_full.c"
    engine_out = bdir / "craftax_full_autopsy.c"
    stamp = bdir / "engine.sha256"
    eh = sha(engine_src, HERE / "patch_engine.py")
    if not engine_out.exists() or not stamp.exists() or stamp.read_text() != eh:
        engine_out.write_text(patch_engine.instrument(engine_src.read_text()))
        stamp.write_text(eh)
    files = policy_files(policy_dir)
    ph = sha(*(policy_dir / f for f in files))
    extra = [HERE / "hybrid_inc.c", *(handoff_dir / f for f in policy_files(handoff_dir))] if handoff_dir else []
    key = sha(HERE / "branch.c", HERE / "hhmm.h", engine_out, *(policy_dir / f for f in files), *extra)[:16]
    exe = bdir / (f"hybrid_{key}" if handoff_dir else f"branch_{key}")
    if not exe.exists():
        tmp = bdir / f".tmp_branch_{key}_{os.getpid()}"
        objs = []
        if handoff_dir:
            # The incumbent is compiled in its own translation unit with its own headers.
            obj = bdir / f".tmp_inc_{key}_{os.getpid()}.o"
            r = subprocess.run(["cc", *CFLAGS, "-c", f"-I{handoff_dir}", str(HERE / "hybrid_inc.c"), "-o", str(obj)],
                               capture_output=True, text=True)
            if r.returncode:
                sys.stderr.write(r.stderr)
                raise SystemExit("incumbent build failed")
            objs = ["-DHYBRID", str(obj)]
        cmd = ["cc", *CFLAGS, f"-DPOLICY_TAG=0x{ph[:16]}ULL", f"-I{policy_dir}", f"-I{bdir}", str(HERE / "branch.c"),
               str(policy_dir / "full_policy.c"), *objs, "-o", str(tmp), "-lm", "-lz"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        for o in objs[1:]:
            Path(o).unlink(missing_ok=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit("build failed")
        tmp.rename(exe)
    return exe


def snapshot_policy(policy_dir: Path, out: Path):
    src = out / "policy_sources"
    src.mkdir(parents=True, exist_ok=True)
    names = policy_files(policy_dir)
    for name in names:
        shutil.copy2(policy_dir / name, src / name)
    return src, {name: sha(src / name) for name in names}


def iter_headers(bank: Path):
    """Yield (offset, header dict) for each snapshot without decompressing."""
    with open(bank, "rb") as f:
        while True:
            off = f.tell()
            raw = f.read(HEADER.size)
            if len(raw) < HEADER.size:
                return
            v = HEADER.unpack(raw)
            h = {"magic": v[0], "state_size": v[1], "agent_size": v[2], "comp_size": v[3],
                 "kind": v[4], "level": v[5], "ep": v[6], "world_seed": v[7], "t": v[8],
                 "max_level": v[11]}
            if h["magic"] not in (b"FBFSNAP1", b"FBFSNAP2"):
                raise SystemExit(f"{bank}: bad magic at {off}")
            f.seek(h["comp_size"], 1)
            yield off, h


def merge_csv(raw: Path, w: int, out: Path, kinds=("episodes", "visits", "tail", "invocations", "runtime_invocations")):
    for kind in kinds:
        if not (raw / f"w0_{kind}.csv").exists():
            continue
        header, rows = None, []
        for k in range(w):
            p = raw / f"w{k}_{kind}.csv"
            with open(p) as f:
                h = f.readline()
                header = header or h
                rows.extend(f.readlines())
        cols = header.rstrip("\n").split(",")
        ir = cols.index("replica")
        it = cols.index("snap_t") if "snap_t" in cols else None
        def key(line):
            parts = line.rstrip("\n").split(",")
            # (episode, snapshot time, replica), then visit / step / invocation id
            st = int(parts[it]) if it is not None else 0
            sub = 0 if kind == "episodes" else int(parts[3]) if kind == "invocations" else int(parts[2])
            return (int(parts[0]), st, int(parts[ir]), sub)
        rows.sort(key=key)
        with gzip.open(out / f"{kind}.csv.gz", "wt", compresslevel=6) as g:
            g.write(header)
            g.writelines(rows)


def run_workers(cmds):
    procs = [subprocess.Popen(c) for c in cmds]
    codes = [p.wait() for p in procs]
    if any(codes):
        raise SystemExit(f"worker failure: {codes}")


def capture(a):
    policy_dir = a.policy_dir.resolve()
    out = (a.banks_dir / a.name)
    if out.exists():
        if not a.force:
            raise SystemExit(f"{out} exists (use --force)")
        shutil.rmtree(out)
    raw = out / "raw"
    raw.mkdir(parents=True)
    src, hashes = snapshot_policy(policy_dir, out)
    exe = build(src.resolve())
    eps = list(range(a.start, a.start + a.episodes))
    w = max(1, min(a.workers, len(eps)))
    cmds = []
    for k in range(w):
        lst = raw / f"w{k}.list"
        lst.write_text("\n".join(str(e) for e in eps[k::w]) + "\n")
        cmd = [str(exe), "--seed", str(a.seed), "--list", str(lst), "--out", str(raw / f"w{k}"),
               "--capture-dir", str(raw), "--capture-tag", f"w{k}",
               "--capture-prob", str(a.capture_prob), "--capture-max", str(a.capture_max)]
        if a.log_goals:
            cmd += ["--log-goals", "1"]
        for t in a.trigger:
            cmd += ["--capture", t]
        cmds.append(cmd)
    t0 = time.time()
    run_workers(cmds)
    elapsed = time.time() - t0
    # Merge banks in episode order (each worker wrote its own file per trigger).
    banks = {}
    for t in a.trigger:
        kind, level = t.split(":")
        name = f"{kind}{level}"
        parts = [raw / f"{name}_w{k}.bin" for k in range(w)]
        recs = []
        for p in parts:
            with open(p, "rb") as f:
                data = f.read()
            for off, h in iter_headers(p):
                recs.append((h["ep"], data[off:off + HEADER.size + h["comp_size"]]))
        recs.sort(key=lambda x: x[0])
        with open(out / f"{name}.bin", "wb") as g:
            for _, blob in recs:
                g.write(blob)
        banks[name] = len(recs)
    merge_csv(raw, w, out)
    shutil.rmtree(raw)
    E = fbf.load_episodes(out)
    s = fbf.summarize(E)
    steps = sum(int(r["length"]) for r in E.values())
    s.update({"name": a.name, "seed": a.seed, "episode_indices": [eps[0], eps[-1]], "triggers": a.trigger,
              "capture_prob": a.capture_prob, "capture_max": a.capture_max,
              "bank_sizes": banks, "policy_dir": str(policy_dir), "policy_sha256": hashes,
              "engine_sha256": sha(REPO / "engine" / "craftax_full.c"), "harness_sha256": sha(HERE / "branch.c", HERE / "hhmm.h"),
              "binary_sha256": sha(exe), "workers": w, "seconds": round(elapsed, 1),
              "steps_per_second": round(steps / max(elapsed, 1e-9))})
    (out / "summary.json").write_text(json.dumps(s, indent=1) + "\n")
    print(fbf.format_summary(s))
    print("banks:", banks, f"{elapsed:.0f}s ->", out)


def summarize_branch(rows, stop_floor):
    n = len(rows)
    reach = [sum(int(r["max_level"]) >= L for r in rows) for L in range(9)]
    snap_level = int(rows[0]["snap_level"]) if rows else 0
    passes = {L: reach[L + 1] / reach[L] if reach[L] else None for L in range(snap_level, 8)}
    from collections import Counter
    classes = Counter(fbf.failure_class(r) if int(r["max_level"]) < stop_floor else "reached" for r in rows)
    k = reach[stop_floor] if stop_floor <= 8 else 0
    lo, hi = fbf.wilson(k, n)
    return {"runs": n, "snapshots": len({r["episode"] for r in rows}), "reach": reach,
            "p_stop": k / n if n else None, "p_stop_wilson95": [lo, hi],
            "pass_rates_from_snapshot_floor": passes,
            "classes": dict(classes.most_common()),
            "mean_steps_after_snapshot": sum(int(r["length"]) - int(r["snap_t"]) for r in rows) / max(n, 1)}


def resume(a):
    policy_dir = a.policy_dir.resolve()
    out = a.runs_dir / a.name
    if out.exists():
        if not a.force:
            raise SystemExit(f"{out} exists (use --force)")
        shutil.rmtree(out)
    raw = out / "raw"
    raw.mkdir(parents=True)
    src, hashes = snapshot_policy(policy_dir, out)
    hand = None
    if a.handoff_dir:
        if not a.handoff:
            raise SystemExit("--handoff-dir needs --handoff")
        hsrc = out / "handoff_sources"
        hsrc.mkdir(parents=True, exist_ok=True)
        names = policy_files(a.handoff_dir.resolve())
        for name in names:
            shutil.copy2(a.handoff_dir.resolve() / name, hsrc / name)
        hand = {name: sha(hsrc / name) for name in names}
    elif a.handoff:
        raise SystemExit("--handoff needs --handoff-dir (the incumbent policy)")
    exe = build(src.resolve(), (out / "handoff_sources").resolve() if hand else None)
    bank = a.bank.resolve()
    total = sum(1 for _ in iter_headers(bank))
    n = max(0, total - a.start) if a.limit is None else max(0, min(a.limit, total - a.start))
    w = max(1, min(a.workers, n))
    cmds = []
    for k in range(w):
        cmd = [str(exe), "--resume", str(bank), "--out", str(raw / f"w{k}"), "--stride", str(w), "--offset", str(k),
               "--replicas", str(a.replicas), "--first-replica", str(a.first_replica), "--stop-floor", str(a.stop_floor)]
        if a.limit is not None:
            cmd += ["--limit", str(a.limit)]
        if a.start:
            cmd += ["--start", str(a.start)]
        if a.oracle:
            cmd += ["--oracle", a.oracle, "--oracle-floors", str(a.oracle_floors)]
        if a.steps:
            cmd += ["--steps", str(a.steps)]
        if a.handoff:
            cmd += ["--handoff", a.handoff]
        if a.stop:
            cmd += ["--stop", a.stop]
        if a.log_goals:
            cmd += ["--log-goals", "1"]
        cmds.append(cmd)
    t0 = time.time()
    run_workers(cmds)
    elapsed = time.time() - t0
    merge_csv(raw, w, out)
    shutil.rmtree(raw)
    rows = load_rows(out)
    s = summarize_branch(rows, a.stop_floor)
    s.update({"name": a.name, "bank": str(bank), "bank_sha256": sha(bank), "bank_snapshots": total, "start": a.start, "limit": a.limit,
              "replicas": [a.first_replica, a.first_replica + a.replicas - 1], "stop_floor": a.stop_floor,
              "oracle": a.oracle, "oracle_floors": a.oracle_floors if a.oracle else None,
              "policy_dir": str(policy_dir), "policy_sha256": hashes, "binary_sha256": sha(exe),
              "handoff": a.handoff, "stop": a.stop, "handoff_dir": str(a.handoff_dir.resolve()) if a.handoff_dir else None,
              "handoff_sha256": hand,
              "harness_sha256": sha(HERE / "branch.c", HERE / "hhmm.h"), "workers": w, "seconds": round(elapsed, 1)})
    (out / "summary.json").write_text(json.dumps(s, indent=1) + "\n")
    print(json.dumps({k: s[k] for k in ("runs", "snapshots", "p_stop", "p_stop_wilson95", "pass_rates_from_snapshot_floor", "mean_steps_after_snapshot")}))
    for k, v in list(s["classes"].items())[:12]:
        print(f"  {v:6d} {100 * v / max(s['runs'], 1):5.1f}%  {k}")
    print(f"{elapsed:.0f}s -> {out}")


def trace(a):
    """Resume one world's snapshot with per-step CSV and ASCII snapshots of its last steps."""
    exe = build(a.policy_dir.resolve(), a.handoff_dir.resolve() if a.handoff_dir else None)
    a.out.mkdir(parents=True, exist_ok=True)
    pre = a.out / f"{a.tag}"
    cmd = [str(exe), "--resume", str(a.bank.resolve()), "--episode", str(a.episode), "--replicas", "1",
           "--first-replica", str(a.replica), "--stop-floor", str(a.stop_floor), "--out", str(pre), "--trace", str(pre)]
    if a.oracle:
        cmd += ["--oracle", a.oracle, "--oracle-floors", str(a.oracle_floors)]
    if a.snap_t is not None:
        cmd += ["--snap-t", str(a.snap_t)]
    if a.handoff:
        cmd += ["--handoff", a.handoff]
    if a.log_goals:
        cmd += ["--log-goals", "1"]
    subprocess.run(cmd, check=True)
    print(f"{pre}_ep{a.episode}_r{a.replica}_steps.csv  {pre}_ep{a.episode}_r{a.replica}_snaps.txt  {pre}_episodes.csv")


def load_rows(run_dir):
    import csv
    with fbf.open_text(Path(run_dir) / "episodes.csv.gz") as f:
        return list(csv.DictReader(f))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("capture")
    c.add_argument("--policy-dir", required=True, type=Path)
    c.add_argument("--name", required=True)
    c.add_argument("--seed", required=True, type=int)
    c.add_argument("--episodes", required=True, type=int)
    c.add_argument("--start", type=int, default=0)
    c.add_argument("--trigger", action="append", required=True)
    c.add_argument("--workers", type=int, default=4)
    c.add_argument("--capture-prob", type=float, default=1.0, help="repeated triggers (leave:K, enter:K): keep each occurrence w.p. P")
    c.add_argument("--capture-max", type=int, default=1 << 30, help="repeated triggers: at most M snapshots per episode and trigger")
    c.add_argument("--banks-dir", type=Path, default=HERE / "build" / "banks")
    c.add_argument("--log-goals", action="store_true", help="write the goal invocation log (invocations.csv.gz)")
    c.add_argument("--force", action="store_true")
    r = sub.add_parser("resume")
    r.add_argument("--policy-dir", required=True, type=Path)
    r.add_argument("--bank", required=True, type=Path)
    r.add_argument("--name", required=True)
    r.add_argument("--replicas", type=int, default=1)
    r.add_argument("--first-replica", type=int, default=0)
    r.add_argument("--start", type=int, default=0, help="first bank record index")
    r.add_argument("--limit", type=int, help="only N snapshots from --start")
    r.add_argument("--stop-floor", type=int, default=8)
    r.add_argument("--oracle", help="diagnostic interventions, e.g. food,drink or revisit_safe")
    r.add_argument("--oracle-floors", type=lambda x: int(x, 0), default=0x1FF, help="bitmask of floors where step oracles apply")
    r.add_argument("--steps", type=int, help="episode horizon (default 100000)")
    r.add_argument("--workers", type=int, default=4)
    r.add_argument("--handoff-dir", type=Path, help="incumbent policy that takes over at --handoff (local comparison)")
    r.add_argument("--handoff", help="return:K | change | steps:N | goalend:NAME | runtimeend:excursion, '|'-separated (any)")
    r.add_argument("--stop", help="end each episode where this condition first holds (same syntax; partition-boundary checks)")
    r.add_argument("--log-goals", action="store_true", help="write the goal invocation log (invocations.csv.gz, hhmm.h)")
    r.add_argument("--runs-dir", type=Path, default=HERE / "build" / "branch_runs")
    r.add_argument("--force", action="store_true")
    t = sub.add_parser("trace")
    t.add_argument("--policy-dir", required=True, type=Path)
    t.add_argument("--bank", required=True, type=Path)
    t.add_argument("--episode", required=True, type=int)
    t.add_argument("--replica", type=int, default=0)
    t.add_argument("--stop-floor", type=int, default=8)
    t.add_argument("--oracle")
    t.add_argument("--oracle-floors", type=lambda x: int(x, 0), default=0x1FF)
    t.add_argument("--snap-t", type=int, help="repeated banks: the snapshot of this episode taken at step T")
    t.add_argument("--handoff-dir", type=Path)
    t.add_argument("--handoff")
    t.add_argument("--log-goals", action="store_true")
    t.add_argument("--out", type=Path, required=True, help="directory for trace files")
    t.add_argument("--tag", default="trace")
    a = ap.parse_args()
    {"capture": capture, "resume": resume, "trace": trace}[a.cmd](a)


if __name__ == "__main__":
    main()
