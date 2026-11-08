#!/usr/bin/env python3
"""Goal invocation log summary (hhmm.h; runs made with --log-goals).

One row per invocation of ADVANCE, CLEAR, FIND_EXIT, DEPART, EXCURSION(purpose),
HEAL or FIGHT, with parent, entry/exit time and public state, outcome, damage and
kills during it, and the episode's final floor. This prints, per goal, frontier
and floor: invocations per run, outcome shares, median duration, damage and kills
per invocation, and the eventual P(floor 8) of runs by the invocation's outcome
(descriptive: the continuation value of the state each outcome leaves).

  python3 goals.py build/branch_runs/<run> [branch_runs/<run2>] [--goal fight] [--frontier 7] [--by arg]
"""
import argparse
import collections
import csv
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fbf  # noqa: E402


def load(run):
    p = Path(run) / "invocations.csv.gz"
    if not p.exists():
        raise SystemExit(f"{p} missing: rerun with --log-goals")
    with fbf.open_text(p) as f:
        return list(csv.DictReader(f))


def runs_of(run):
    with fbf.open_text(Path(run) / "episodes.csv.gz") as f:
        return sum(1 for _ in csv.DictReader(f))


def summarize(run, goal=None, frontier=None, by="level"):
    rows = load(run)
    n_runs = runs_of(run)
    groups = collections.defaultdict(list)
    for r in rows:
        if goal and r["goal"] != goal:
            continue
        if frontier is not None and int(r["frontier"]) != frontier:
            continue
        if r["resumed"] == "1":
            continue  # entry not observed (open at the snapshot)
        key = (r["goal"], int(r["frontier"]), r["arg"] if by == "arg" else int(r["in_level"]))
        groups[key].append(r)
    out = []
    for key in sorted(groups, key=lambda k: (k[1], k[0], str(k[2]))):
        g = groups[key]
        oc = collections.Counter(r["outcome"] for r in g)
        dur = [int(r["exit_t"]) - int(r["entry_t"]) for r in g]
        hit = [float(r["dmg_melee"]) + float(r["dmg_proj"]) for r in g]
        win = {o: sum(int(r["final_max_level"]) >= 8 for r in g if r["outcome"] == o) / c for o, c in oc.items()}
        out.append({"goal": key[0], "frontier": key[1], key_name(by): key[2], "per_run": len(g) / n_runs, "n": len(g),
                    "outcomes": {o: c / len(g) for o, c in oc.most_common()}, "median_steps": statistics.median(dur),
                    "damage": sum(hit) / len(g), "kills": sum(int(r["kills"]) for r in g) / len(g),
                    "p8_by_outcome": win})
    return n_runs, out


def key_name(by):
    return "purpose" if by == "arg" else "floor"


def show(run, n_runs, out, by):
    print(f"{run}: {n_runs} runs")
    print(f"  {'goal':10s} {'front':>5s} {key_name(by):>8s} {'per run':>8s} {'n':>7s} {'steps':>6s} {'dmg':>6s} {'kills':>6s}  outcomes (share; P8 of runs)")
    for o in out:
        oc = "  ".join(f"{k} {v:.2f}" + (f" ({o['p8_by_outcome'][k]:.2f})" if k in o["p8_by_outcome"] else "") for k, v in o["outcomes"].items())
        print(f"  {o['goal']:10s} {o['frontier']:5d} {str(o[key_name(by)]):>8s} {o['per_run']:8.2f} {o['n']:7d} {o['median_steps']:6.0f} "
              f"{o['damage']:6.2f} {o['kills']:6.2f}  {oc}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+", type=Path)
    ap.add_argument("--goal")
    ap.add_argument("--frontier", type=int)
    ap.add_argument("--by", choices=["level", "arg"], default="level", help="group by floor of entry or by goal argument (excursion purpose)")
    a = ap.parse_args()
    for run in a.runs:
        n, out = summarize(run, a.goal, a.frontier, a.by)
        show(run, n, out, a.by)


if __name__ == "__main__":
    main()
