#!/usr/bin/env python3
"""Evaluate a Craftax Full program policy on a fixed cohort of worlds.

    python3 policies/evaluate.py policies/optimized
    python3 policies/evaluate.py policies/optimized_extension --workers 8
    python3 policies/evaluate.py policies/checkpoints/18_r16 --episodes 128 \
        --check policies/results/common_cohort/18_r16.csv.gz

A policy is a directory holding full_policy.c, full_policy.h and the files they
include. It is compiled together with the native evaluator (evaluate_full.c) and
the engine (../engine/craftax_full.c), with the flags used for every result in
the paper. Episode i of a cohort plays the world with seed
uint32(splitmix64(seed + i)); the defaults are the paper's common cohort
(seed 81,000,000, 2,048 worlds, 100,000 actions). Episodes continue after
floor-8 arrival, so arrivals, Necromancer victories and return come from the
same episode. The policy uses its compiled defaults and only public observations.

Outputs go to policies/build/eval/<policy>-<seed>-<first>-<episodes>/:
episodes.csv (one row per world) and summary.json. With --check, every row
(except the episode index) must equal the given archived record.
Requires Python 3.9+ (standard library only) and a C compiler.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import gzip
import io
import json
import math
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
FLAGS = ["-O3", "-std=c11", "-ffp-contract=off", "-Wno-unused-function"]
COLUMNS = ["episode", "world_seed", "return", "achievements", "length", "health",
           "max_level", "boss_progress", "defeat_necromancer"]


def world_seed(value):
    mask = (1 << 64) - 1
    value = (value + 0x9e3779b97f4a7c15) & mask
    value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & mask
    value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & mask
    return (value ^ (value >> 31)) & 0xffffffff


def wilson(k, n, z=1.959963984540054):
    if n == 0:
        return [0.0, 0.0]
    p = k / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return [100 * (centre - half), 100 * (centre + half)]


def build(policy, out, cc):
    exe = out / "policy_eval"
    subprocess.run([cc, *FLAGS, "-I", str(policy), str(HERE / "evaluate_full.c"),
                    str(policy / "full_policy.c"), "-o", str(exe), "-lm", "-lpthread"],
                   check=True)
    return exe


def run_chunk(exe, out, seed, start, count, steps):
    path = out / "chunks" / f"{start:06d}.csv"
    subprocess.run([str(exe), "eval", "--episodes", str(count), "--seed", str(seed + start),
                    "--steps", str(steps), "--episode-csv", str(path)],
                   check=True, stdout=subprocess.DEVNULL)
    with path.open() as fh:
        rows = list(csv.DictReader(fh))
    if len(rows) != count:
        sys.exit(f"chunk {start}: expected {count} rows, got {len(rows)}")
    for i, row in enumerate(rows):
        row["episode"] = str(start + i)
    return rows


def read_rows(path):
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt") as fh:
        return list(csv.DictReader(fh))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("policy", type=Path, help="policy source directory")
    ap.add_argument("--seed", type=int, default=81000000)
    ap.add_argument("--episodes", type=int, default=2048)
    ap.add_argument("--first", type=int, default=0, help="first episode index of the cohort")
    ap.add_argument("--steps", type=int, default=100000)
    ap.add_argument("--chunk", type=int, default=16)
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--check", type=Path, help="archived episode record (.csv or .csv.gz) to compare with")
    a = ap.parse_args()
    policy = a.policy.resolve()
    if not (policy / "full_policy.c").is_file():
        ap.error(f"{a.policy} has no full_policy.c")
    if a.episodes < 1 or a.first < 0 or not 0 <= a.seed + a.first + a.episodes <= 2147483647:
        ap.error("invalid cohort")
    out = HERE / "build" / "eval" / f"{policy.name}-{a.seed}-{a.first}-{a.episodes}"
    (out / "chunks").mkdir(parents=True, exist_ok=True)
    exe = build(policy, out, a.cc)
    starts = range(a.first, a.first + a.episodes, a.chunk)
    with ThreadPoolExecutor(a.workers) as pool:
        parts = list(pool.map(lambda s: run_chunk(exe, out, a.seed, s, min(a.chunk, a.first + a.episodes - s),
                                                  a.steps), starts))
    rows = [row for part in parts for row in part]
    seeds = [int(r["world_seed"]) for r in rows]
    assert seeds == [world_seed(a.seed + a.first + i) for i in range(a.episodes)], "world seeds"
    with (out / "episodes.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)
    n = len(rows)
    arrivals = sum(int(r["max_level"]) >= 8 for r in rows)
    wins = sum(int(r["defeat_necromancer"]) for r in rows)
    summary = {
        "policy": policy.name, "seed": a.seed, "first": a.first, "episodes": n, "horizon": a.steps,
        "floor8_arrivals": arrivals, "floor8_arrival_pct": 100 * arrivals / n,
        "floor8_arrival_wilson95": wilson(arrivals, n),
        "necromancer_wins": wins, "win_pct": 100 * wins / n, "win_wilson95": wilson(wins, n),
        "mean_return": sum(float(r["return"]) for r in rows) / n,
        "floor_reach_pct": [100 * sum(int(r["max_level"]) >= f for r in rows) / n for f in range(9)],
    }
    if a.check:
        ref = read_rows(a.check)[a.first:a.first + a.episodes]
        key = COLUMNS[1:]
        bad = [i for i, (x, y) in enumerate(zip(rows, ref)) if [x[k] for k in key] != [y[k] for k in key]]
        summary["check"] = {"record": a.check.name, "rows": len(ref),
                            "identical": not bad and len(ref) == n, "differing_rows": bad[:20]}
    (out / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    print(json.dumps(summary, indent=1))
    print(f"wrote {out.relative_to(HERE.parent)}")
    if a.check and not summary["check"]["identical"]:
        sys.exit(1)


if __name__ == "__main__":
    main()
