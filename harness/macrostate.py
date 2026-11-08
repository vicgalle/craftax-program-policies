#!/usr/bin/env python3
"""Decompose each floor pass into combat, exit discovery and departure.

Use a run produced by the milestone-enabled autopsy harness. These are
unconditional native episodes: no reset or inventory intervention is made.
"""
import argparse
import collections
import json
from pathlib import Path

import fbf


def decompose(episodes):
    rows = []
    for level in range(8):
        cohort = [r for r in episodes.values() if int(r["max_level"]) >= level]
        if any(f"exit_ready{level}" not in r for r in cohort):
            raise ValueError("run predates milestone logging; rerun the frozen policy")
        clear = [r for r in cohort if int(r[f"cleared{level}"]) >= 0]
        ready = [r for r in clear if int(r[f"exit_ready{level}"]) >= 0]
        passed = [r for r in ready if int(r["max_level"]) > level]
        # Every native descent requires clearance and the observed ladder.
        assert len(passed) == sum(int(r["max_level"]) > level for r in cohort)
        counts = [len(cohort), len(clear), len(ready), len(passed)]
        stages = []
        # Discovery may precede clearance. The second conditional measures
        # whether a cleared run ever knows its exit, regardless of that order.
        for name, n, k in zip(("clear", "exit_known_given_clear", "depart"), counts, counts[1:]):
            stages.append({"stage": name, "eligible": n, "success": k,
                           "rate": k / n if n else None,
                           "wilson95": list(fbf.wilson(k, n)) if n else None})
        rows.append({"floor": level, "counts": counts, "stages": stages,
                     "exit_known_before_clear": sum(
                         0 <= int(r[f"exit_known{level}"]) <= int(r[f"cleared{level}"])
                         for r in clear),
                     "failed_before_clear": dict(collections.Counter(
                         fbf.failure_class(r) for r in cohort
                         if int(r[f"cleared{level}"]) < 0)),
                     "failed_without_exit": dict(collections.Counter(
                         fbf.failure_class(r) for r in clear
                         if int(r[f"exit_ready{level}"]) < 0)),
                     "failed_after_ready": dict(collections.Counter(
                         fbf.failure_class(r) for r in ready if int(r["max_level"]) == level))})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run", type=Path)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    rows = decompose(fbf.load_episodes(args.run))
    print("floor    entered  cleared  exit ready  departed   clear   find exit   depart")
    for row in rows:
        rates = [f"{s['rate']:.3f}" if s["rate"] is not None else "    -"
                 for s in row["stages"]]
        print(f"{row['floor']:5d} " + " ".join(f"{n:8d}" for n in row["counts"])
              + "  " + "    ".join(rates))
    if args.json:
        args.json.write_text(json.dumps(rows, indent=2) + "\n")


if __name__ == "__main__":
    main()
