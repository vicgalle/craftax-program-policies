#!/usr/bin/env python3
"""Build a policy variant into the autopsy harness and run it on a world cohort.

Examples
  # full cohort
  python3 run.py --policy-dir ../policies/checkpoints/18_r16 --name base_D1 --seed 30000000 --episodes 8192
  # only worlds where a baseline run reached floor 4 (for floor-4-scoped changes;
  # compare.py --scope 4 checks the scoping and fills in the other worlds)
  python3 run.py --policy-dir ../policies/checkpoints/18_r16 --name vault_v1_D1 --seed 30000000 \
      --episodes 8192 --reach-floor 4 --baseline build/runs/base_D1

Outputs build/runs/<name>/{episodes.csv.gz, visits.csv.gz, tail.csv.gz, summary.json}.
"""
import argparse
import gzip
import hashlib
import json
import os
import shutil
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


def sha(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(Path(p).read_bytes())
    return h.hexdigest()


def build(policy_dir: Path) -> Path:
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
    key = sha(HERE / "autopsy.c", engine_out, *(policy_dir / f for f in files))[:16]
    exe = bdir / f"autopsy_{key}"
    if not exe.exists():
        tmp = bdir / f".tmp_{key}_{os.getpid()}"
        cmd = ["cc", *CFLAGS, f"-I{policy_dir}", f"-I{bdir}", str(HERE / "autopsy.c"),
               str(policy_dir / "full_policy.c"), "-o", str(tmp), "-lm", "-lpthread"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit("build failed")
        tmp.rename(exe)
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--policy-dir", required=True, type=Path)
    ap.add_argument("--name", required=True)
    ap.add_argument("--seed", required=True, type=int, help="CLI base seed; world e uses splitmix(seed+e)")
    ap.add_argument("--episodes", type=int, help="run absolute episodes 0..N-1")
    ap.add_argument("--start", type=int, default=0, help="first absolute episode (with --episodes)")
    ap.add_argument("--reach-floor", type=int, help="only episodes where --baseline reached this floor")
    ap.add_argument("--baseline", type=Path, help="baseline run directory for --reach-floor")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--runs-dir", type=Path, default=HERE / "build" / "runs")
    ap.add_argument("--force", action="store_true", help="overwrite an existing run directory")
    a = ap.parse_args()

    policy_dir = a.policy_dir.resolve()
    if a.episodes is None:
        raise SystemExit("--episodes is required")
    eps = list(range(a.start, a.start + a.episodes))
    if a.reach_floor is not None:
        if not a.baseline:
            raise SystemExit("--reach-floor needs --baseline")
        meta = fbf.load_meta(a.baseline)
        if meta["seed"] != a.seed:
            raise SystemExit("baseline seed differs")
        B = fbf.load_episodes(a.baseline)
        missing = [e for e in eps if e not in B]
        if missing:
            raise SystemExit(f"baseline lacks {len(missing)} of the requested episodes")
        eps = [e for e in eps if int(B[e]["max_level"]) >= a.reach_floor]

    out = a.runs_dir / a.name
    if out.exists():
        if not a.force:
            raise SystemExit(f"{out} exists (use --force)")
        shutil.rmtree(out)
    raw = out / "raw"
    raw.mkdir(parents=True)

    # Compile and hash the same immutable inputs. Experiments often edit their
    # working copy while a cohort is running; hashing it after the workers exit
    # would otherwise attribute old trajectories to a newer policy.
    source_dir = out / "policy_sources"
    source_dir.mkdir()
    names = policy_files(policy_dir)
    for name in names:
        shutil.copy2(policy_dir / name, source_dir / name)
    policy_hashes = {name: sha(source_dir / name) for name in names}
    exe = build(source_dir.resolve())
    engine_hash = sha(REPO / "engine" / "craftax_full.c")
    harness_hash = sha(HERE / "autopsy.c")
    binary_hash = sha(exe)
    # Longest-first is unknowable; interleave so each worker gets a similar mix.
    w = max(1, min(a.workers, len(eps)))
    procs = []
    t0 = time.time()
    for k in range(w):
        lst = raw / f"w{k}.list"
        lst.write_text("\n".join(str(e) for e in eps[k::w]) + "\n")
        procs.append(subprocess.Popen([str(exe), "--seed", str(a.seed), "--list", str(lst), "--out", str(raw / f"w{k}")]))
    codes = [p.wait() for p in procs]
    elapsed = time.time() - t0
    if any(codes):
        raise SystemExit(f"worker failure: {codes}")

    def merge(kind, sort_key):
        header, rows = None, []
        for k in range(w):
            with open(raw / f"w{k}_{kind}.csv") as f:
                h = f.readline()
                header = header or h
                rows.extend(f.readlines())
        rows.sort(key=sort_key)
        with gzip.open(out / f"{kind}.csv.gz", "wt", compresslevel=6) as g:
            g.write(header)
            g.writelines(rows)

    first_int = lambda line: int(line.split(",", 1)[0])
    merge("episodes", first_int)
    merge("visits", lambda line: (first_int(line), int(line.split(",")[3])))
    merge("tail", lambda line: (first_int(line), int(line.split(",")[2])))
    shutil.rmtree(raw)

    E = fbf.load_episodes(out)
    steps = sum(int(r["length"]) for r in E.values())
    s = fbf.summarize(E)
    s.update({
        "name": a.name, "seed": a.seed, "episode_indices": [eps[0], eps[-1]] if eps else [],
        "requested_start": a.start, "requested_episodes": a.episodes,
        "subset": {"reach_floor": a.reach_floor, "baseline": str(a.baseline)} if a.reach_floor is not None else None,
        "policy_dir": str(policy_dir),
        "policy_sha256": policy_hashes,
        "policy_sources": "policy_sources",
        "engine_sha256": engine_hash,
        "harness_sha256": harness_hash,
        "binary_sha256": binary_hash,
        "workers": w, "seconds": round(elapsed, 1), "steps_per_second": round(steps / max(elapsed, 1e-9)),
    })
    (out / "summary.json").write_text(json.dumps(s, indent=1) + "\n")
    print(fbf.format_summary(s))
    print(f"{len(eps)} episodes, {steps} steps in {elapsed:.0f}s ({steps / max(elapsed, 1e-9):,.0f} steps/s, {w} workers) -> {out}")


if __name__ == "__main__":
    main()
