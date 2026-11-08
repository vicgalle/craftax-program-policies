#!/usr/bin/env python3
"""Recompute the lineage table and paired contrasts from the archived episode rows.

    python3 policies/summarize.py

Reads policies/results/common_cohort/*.csv.gz (2,048 worlds, seed 81,000,000)
and policies/results/lineage.json (cumulative output tokens, regimes). Prints,
for every checkpoint: floor-8 arrival with its Wilson 95% interval, Necromancer
victories, mean native return; then the paired contrasts reported in the paper.
Standard library only; no simulation.
"""
import csv
import gzip
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROWS = HERE / "results" / "common_cohort"
Z = 1.959963984540054
LABELS = {"initial": "Initial", "baseline": "Base", "framework": "Goal interface",
          "strategic": "Kit goals", "wm": "World model", "stack": "Composition",
          "region": "Regions", "modules": "Modules", "recursive": "Final",
          **{f"round{i}": f"R{i}" for i in range(1, 11)}, **{f"r{i}": f"R{i}" for i in range(11, 17)}}


def load(name):
    with gzip.open(ROWS / f"{name}.csv.gz", "rt") as fh:
        rows = list(csv.DictReader(fh))
    assert len(rows) == 2048
    return rows


def wilson(k, n):
    p = k / n
    c = (p + Z * Z / (2 * n)) / (1 + Z * Z / n)
    h = Z * math.sqrt(p * (1 - p) / n + Z * Z / (4 * n * n)) / (1 + Z * Z / n)
    return 100 * (c - h), 100 * (c + h)


def paired(a, b, key):
    """Candidate b minus incumbent a: mean, normal 95% interval, gained, lost (percentage points)."""
    d = [key(y) - key(x) for x, y in zip(a, b)]
    assert [x["world_seed"] for x in a] == [y["world_seed"] for y in b]
    n, m = len(d), sum(d) / len(d)
    s = math.sqrt(sum((v - m) ** 2 for v in d) / (n - 1))
    return 100 * m, 100 * (m - Z * s / math.sqrt(n)), 100 * (m + Z * s / math.sqrt(n)), d.count(1), d.count(-1)


def main():
    lineage = json.loads((HERE / "results" / "lineage.json").read_text())
    points = lineage["checkpoints"] + [lineage["strategic"]] + list(lineage["modules"].values())
    files = sorted(p.name[:-7] for p in ROWS.glob("[0-9][0-9]_*.csv.gz")) + ["optimized"]
    assert len(points) == len(files) == 25
    arrive = lambda r: int(int(r["max_level"]) >= 8)
    win = lambda r: int(r["defeat_necromancer"])
    print(f"{'Checkpoint':<15}{'Regime':>7}{'Tokens(M)':>10}{'Arrival%':>10}{'Wilson 95%':>17}{'Wins%':>8}{'Return':>8}")
    rows = {}
    for point, name in zip(points, files):
        r = rows[name] = load(name)
        k, w = sum(map(arrive, r)), sum(map(win, r))
        ret = sum(float(x["return"]) for x in r) / len(r)
        lo, hi = wilson(k, len(r))
        assert abs(100 * k / len(r) - point["arrival8"]["mean"]) < 1e-9, name
        assert abs(100 * w / len(r) - point["victory"]["mean"]) < 1e-9, name
        print(f"{LABELS[point['stage']]:<15}{point['phase']:>7}{point['x']:>10.2f}{100 * k / len(r):>10.2f}"
              f"{f'[{lo:.2f}, {hi:.2f}]':>17}{100 * w / len(r):>8.2f}{ret:>8.1f}")
    ext = load("optimized_extension")
    print()
    for title, a, b, key in [("Final - Modules, floor-8 arrival", rows["23_modules"], rows["optimized"], arrive),
                             ("Extension - Final, victories", rows["optimized"], ext, win)]:
        m, lo, hi, g, l = paired(a, b, key)
        print(f"{title}: {m:+.2f} pp [{lo:+.2f}, {hi:+.2f}], {g} gained / {l} lost")
    print(f"Extension: {sum(map(win, ext))}/2048 victories; arrivals {sum(map(arrive, ext))} "
          f"(Final {sum(map(arrive, rows['optimized']))}), same arrival worlds: "
          f"{[arrive(x) for x in ext] == [arrive(x) for x in rows['optimized']]}")
    for name in ("r11_control", "r15_control", "scratch_first", "scratch_final"):
        r = load(name)
        print(f"{name}: arrival {100 * sum(map(arrive, r)) / len(r):.2f}%")


if __name__ == "__main__":
    main()
