"""Build and run helpers for the world-model/policy split (wm_eval.c).

A policy directory holds full_policy.c, full_policy.h, full_wm.inc and
full_goals.inc; each compiles into its own wm_eval binary (cached by source
hash). Policies are compared by running their binaries on the same bank
snapshots and replicas (common random numbers).
"""
import csv
import hashlib
import io
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BUILD = HERE / "build"
POLICY = REPO / "policies" / "checkpoints" / "20_world_model"
BANKS = HERE / "build" / "banks"
CFLAGS = ["-O3", "-std=c11", "-ffp-contract=off", "-Wno-unused-function"]
WORKERS = int(os.environ.get("WM_WORKERS", "8"))
sys.path.insert(0, str(HERE))
import patch_engine  # noqa: E402


def _sha(paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(Path(p).read_bytes())
    return h.hexdigest()[:16]


def engine():
    """Instrumented engine copy (identical dynamics, per-step damage by source)."""
    BUILD.mkdir(exist_ok=True)
    src = REPO / "engine" / "craftax_full.c"
    out = BUILD / "craftax_full_autopsy.c"
    stamp = BUILD / "engine.sha"
    key = _sha([src, HERE / "patch_engine.py"])
    if not out.exists() or not stamp.exists() or stamp.read_text() != key:
        out.write_text(patch_engine.instrument(src.read_text()))
        stamp.write_text(key)
    return out


def build(policy_dir=POLICY):
    policy_dir = Path(policy_dir).resolve()
    eng = engine()
    srcs = [HERE / "wm_eval.c", eng] + sorted(policy_dir.glob("full_*"))
    exe = BUILD / f"wm_eval_{_sha(srcs)}"
    if not exe.exists():
        tmp = BUILD / f".tmp_{exe.name}_{os.getpid()}"
        subprocess.run(["cc", *CFLAGS, f"-I{policy_dir}", f"-I{BUILD}", str(HERE / "wm_eval.c"),
                        str(policy_dir / "full_policy.c"), "-o", str(tmp), "-lm", "-lz"], check=True)
        tmp.rename(exe)
    return exe


def _rows(args):
    out = subprocess.run([*map(str, args), "--out", "-"], check=True, capture_output=True, text=True).stdout
    return list(csv.DictReader(io.StringIO(out)))


def eval_bank(bank, policy_dir=POLICY, replicas=(0, 1), select=None, stop_floor=8, oracle=None, audit=False,
              workers=WORKERS):
    exe = build(policy_dir)
    args = [exe, "eval", "--bank", bank, "--replicas", f"{replicas[0]}:{replicas[1]}", "--stop-floor", stop_floor,
            "--workers", workers]
    if oracle:
        args += ["--oracle", oracle]
    if audit:
        args += ["--audit", 1]
    if select is None:
        return _rows(args)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(str(int(i)) for i in sorted(select)) + "\n")
    try:
        return _rows(args + ["--select", f.name])
    finally:
        os.unlink(f.name)


def eval_reset(seed, start, count, policy_dir=POLICY, stop_floor=8, horizon=100000, oracle=None, audit=False,
               workers=WORKERS):
    exe = build(policy_dir)
    args = [exe, "reset", "--seed", seed, "--start", start, "--count", count, "--stop-floor", stop_floor,
            "--horizon", horizon, "--workers", workers]
    if oracle:
        args += ["--oracle", oracle]
    if audit:
        args += ["--audit", 1]
    return _rows(args)


def capture(seed, start, count, levels, out_dir, policy_dir=POLICY, workers=WORKERS):
    exe = build(policy_dir)
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    subprocess.run([str(exe), "capture", "--seed", str(seed), "--start", str(start), "--count", str(count),
                    "--levels", ",".join(map(str, levels)), "--out", str(out_dir), "--workers", str(workers)], check=True)
    return Path(out_dir)


def bank_index(path):
    """[(index, ep, t)] for every snapshot of an HRLSNAP1 bank."""
    import struct
    out, i, size = [], 0, 136
    with open(path, "rb") as f:
        while True:
            h = f.read(size)
            if len(h) < size:
                return out
            comp = struct.unpack_from("<I", h, 16)[0]
            ep, _, t = struct.unpack_from("<iIi", h, 24)
            out.append((i, ep, t))
            f.seek(comp, 1)
            i += 1


def split(path, val_mod=4):
    """Training and validation snapshot indices: worlds with ep % val_mod == 0 are validation."""
    idx = bank_index(path)
    return [i for i, ep, _ in idx if ep % val_mod], [i for i, ep, _ in idx if ep % val_mod == 0]
