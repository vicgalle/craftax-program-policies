#!/usr/bin/env python3
"""Invocation log of the excursion module, reconstructed from per-floor visits.

An excursion starts when the agent climbs from its deepest floor so far (the
frontier D) and ends when it is back on D (returned), goes deeper (advanced),
dies (died) or reaches the horizon. Each excursion is one invocation of the
"resupply/return trip" goal in the hierarchical framework; this is its log:
entry and exit time, highest floor reached (top), outcome, and the floor of
death. Run on a capture bank directory or a branch run directory.

  python3 trips.py build/branch_runs/<run> [--frontier 7] [--json out.json]
  python3 trips.py build/branch_runs/<a> build/branch_runs/<b>     # side by side
"""
import argparse
import collections
import csv
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fbf  # noqa: E402


def load_visits(run):
    groups = collections.defaultdict(list)
    with fbf.open_text(Path(run) / "visits.csv.gz") as f:
        for r in csv.DictReader(f):
            key = (int(r["episode"]), int(r.get("snap_t", 0) or 0), int(r.get("replica", 0) or 0))
            groups[key].append(r)
    for v in groups.values():
        v.sort(key=lambda r: int(r["arrive_t"]))
    return groups


def excursions(visits, snap_max=0):
    """Yield one dict per excursion of one run (visits in time order)."""
    deepest = snap_max
    cur = None
    for v in visits:
        L, ex = int(v["level"]), v["exit"]
        deepest = max(deepest, L)
        if cur is None and L == deepest and ex == "up":
            cur = {"frontier": L, "start": int(v["leave_t"]), "top": L - 1, "floors": [], "health_out": float(v["exit_health"]),
                   "food_out": int(v["food_out"]), "drink_out": int(v["drink_out"]), "energy_out": int(v["energy_out"])}
            continue
        if cur is None:
            continue
        if L < cur["frontier"]:
            cur["top"] = min(cur["top"], L)
            cur["floors"].append(L)
        if ex == "died" or ex == "horizon":
            cur.update(outcome="died" if ex == "died" else "horizon", end=int(v["leave_t"]), death_level=L if ex == "died" else None)
            yield cur
            cur = None
        elif L == cur["frontier"] - 1 and ex == "down":
            cur.update(outcome="returned", end=int(v["leave_t"]), death_level=None)
            yield cur
            cur = None
    if cur is not None:
        cur.update(outcome="open", end=None, death_level=None)
        yield cur


def snapshot_frontiers(run):
    """Deepest floor reached at the snapshot, per (episode, snap_t, replica)."""
    out = {}
    with fbf.open_text(Path(run) / "episodes.csv.gz") as f:
        for r in csv.DictReader(f):
            t = int(r.get("snap_t", 0) or 0)
            m = max([0] + [K for K in range(1, 9) if 0 <= int(r[f"arrive{K}"]) <= t])
            out[(int(r["episode"]), t, int(r.get("replica", 0) or 0))] = m
    return out


def summarize(run, frontier=None):
    G = load_visits(run)
    F = snapshot_frontiers(run)
    rows = collections.defaultdict(lambda: collections.Counter())
    dur = collections.defaultdict(list)
    runs = len(G)
    for key, vis in G.items():
        for e in excursions(vis, F.get(key, 0)):
            if frontier is not None and e["frontier"] != frontier:
                continue
            k = (e["frontier"], e["top"])
            rows[k]["n"] += 1
            rows[k][e["outcome"]] += 1
            if e["outcome"] == "died":
                rows[k][f"died_L{e['death_level']}"] += 1
            if e["end"] is not None:
                dur[k].append(e["end"] - e["start"])
    out = {"run": str(run), "runs": runs, "excursions": {}}
    for k in sorted(rows):
        c = rows[k]
        d = sorted(dur[k])
        out["excursions"][f"{k[0]}->{k[1]}"] = {
            "per_run": c["n"] / runs, "n": c["n"], "died": c["died"], "death_rate": c["died"] / c["n"] if c["n"] else None,
            "median_steps": d[len(d) // 2] if d else None,
            "deaths_by_floor": {x[5:]: c[x] for x in c if x.startswith("died_L")},
        }
    return out


def show(s):
    print(f"{s['run']}: {s['runs']} runs")
    print("  frontier->top  per run      n   died  death/trip  median steps  deaths by floor")
    for k, v in s["excursions"].items():
        print(f"  {k:>12s}  {v['per_run']:7.2f} {v['n']:6d} {v['died']:6d}   {v['death_rate']:.4f}    {v['median_steps']}   {v['deaths_by_floor']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="+", type=Path)
    ap.add_argument("--frontier", type=int, help="only excursions from this frontier floor")
    ap.add_argument("--json", type=Path)
    a = ap.parse_args()
    res = [summarize(r, a.frontier) for r in a.runs]
    for s in res:
        show(s)
    if a.json:
        a.json.write_text(json.dumps(res, indent=1) + "\n")


if __name__ == "__main__":
    main()
