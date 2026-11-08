#!/usr/bin/env python3
"""Paired comparison of two branch runs resumed from the same snapshot bank.

Pairs are (episode, snapshot time, replica): a repeated-invocation bank holds
several snapshots per world. Replicas and snapshots of one world share it, so
the interval for the difference in P(reach --floor) is clustered by world
(pair-weighted ratio estimate, cluster sandwich normal approximation). The exact McNemar
p-value treats pairs as independent and is reported for reference only.

  python3 bank_compare.py build/branch_runs/base_a7 build/branch_runs/ice_v1 [--floor 8] [--json out.json]
"""
import argparse
import collections
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fbf  # noqa: E402
from bank import load_rows  # noqa: E402


def cluster_ratio(differences, worlds):
    """Pair-weighted mean and CR1 world-cluster sandwich standard error."""
    by = collections.defaultdict(list)
    for d, world in zip(differences, worlds):
        by[world].append(float(d))
    n = sum(map(len, by.values()))
    if not n:
        raise ValueError("no paired observations")
    mean = sum(map(sum, by.values())) / n
    m = len(by)
    se = math.sqrt(m / (m - 1) * sum((sum(v) - len(v) * mean) ** 2 for v in by.values()) / n ** 2) if m > 1 else float("nan")
    return mean, se, m


def keyed(run):
    return {(int(r["episode"]), int(r["snap_t"]), int(r["replica"])): r for r in load_rows(run)}


def macro(rows, floor_from):
    """Nested milestones per floor: entered, cleared, cleared+exit known, departed."""
    out = {}
    for L in range(floor_from, 8):
        ent = [r for r in rows if int(r["max_level"]) >= L]
        clr = [r for r in ent if int(r[f"cleared{L}"]) >= 0]
        rdy = [r for r in clr if int(r[f"exit_ready{L}"]) >= 0]
        dep = [r for r in rdy if int(r["max_level"]) > L]
        out[L] = [len(ent), len(clr), len(rdy), len(dep)]
    return out


def compare(a_dir, b_dir, floor):
    A, B = keyed(a_dir), keyed(b_dir)
    keys = sorted(set(A) & set(B))
    if len(keys) != len(A) or len(keys) != len(B):
        print(f"warning: {len(A)} vs {len(B)} runs, {len(keys)} paired", file=sys.stderr)
    ra = {k: int(A[k]["max_level"]) >= floor for k in keys}
    rb = {k: int(B[k]["max_level"]) >= floor for k in keys}
    gain = sum(rb[k] and not ra[k] for k in keys)
    loss = sum(ra[k] and not rb[k] for k in keys)
    n = len(keys)
    pa, pb = sum(ra.values()) / n, sum(rb.values()) / n
    mean, se, m = cluster_ratio([int(rb[k]) - int(ra[k]) for k in keys], [k[0] for k in keys])
    z = mean / se if se and se > 0 else float("nan")
    p_cluster = math.erfc(abs(z) / math.sqrt(2)) if z == z else float("nan")
    same = sum(A[k]["length"] == B[k]["length"] and A[k]["return"] == B[k]["return"] for k in keys)
    snap_level = int(A[keys[0]]["snap_level"]) if keys else 0
    reach_a = [sum(int(A[k]["max_level"]) >= L for k in keys) for L in range(9)]
    reach_b = [sum(int(B[k]["max_level"]) >= L for k in keys) for L in range(9)]
    ca = collections.Counter(fbf.failure_class(A[k]) for k in keys if not ra[k])
    cb = collections.Counter(fbf.failure_class(B[k]) for k in keys if not rb[k])
    classes = {c: [ca.get(c, 0), cb.get(c, 0)] for c in sorted(set(ca) | set(cb), key=lambda c: -(ca.get(c, 0) + cb.get(c, 0)))}
    steps_a = sum(int(A[k]["length"]) - int(A[k]["snap_t"]) for k in keys) / n
    steps_b = sum(int(B[k]["length"]) - int(B[k]["snap_t"]) for k in keys) / n
    return {
        "a": str(a_dir), "b": str(b_dir), "floor": floor, "pairs": n, "snapshots": len({k[:2] for k in keys}), "worlds": m,
        "estimand": "pair_weighted", "cluster_method": "CR1_ratio_sandwich",
        "identical_outcomes": same,
        "p_a": pa, "p_b": pb, "diff": pb - pa, "diff_ci95_cluster": [mean - 1.96 * se, mean + 1.96 * se],
        "p_cluster": p_cluster, "gains": gain, "losses": loss, "mcnemar_p_unclustered": fbf.mcnemar(gain, loss),
        "reach_a": reach_a, "reach_b": reach_b,
        "macro_a": macro([A[k] for k in keys], snap_level), "macro_b": macro([B[k] for k in keys], snap_level),
        "failure_classes_a_b": classes, "mean_steps_after_snapshot": [steps_a, steps_b],
    }


def show(r):
    print(f"pairs {r['pairs']} ({r['snapshots']} snapshots, {r['worlds']} worlds), identical outcomes {r['identical_outcomes']}")
    print(f"P(reach {r['floor']}): {r['p_a']:.4f} -> {r['p_b']:.4f}  diff {100 * r['diff']:+.2f}pp "
          f"[{100 * r['diff_ci95_cluster'][0]:+.2f}, {100 * r['diff_ci95_cluster'][1]:+.2f}]  p_cluster={r['p_cluster']:.3g}  "
          f"+{r['gains']}/-{r['losses']} (McNemar unclustered p={r['mcnemar_p_unclustered']:.3g})")
    print("floor  entered        cleared        exit-ready     departed      pass")
    for L in sorted(r["macro_a"]):
        a, b = r["macro_a"][L], r["macro_b"][L]
        pa = a[3] / a[0] if a[0] else float("nan")
        pb = b[3] / b[0] if b[0] else float("nan")
        print(f"  {L}  " + "  ".join(f"{x:5d}->{y:5d}" for x, y in zip(a, b)) + f"   {pa:.3f}->{pb:.3f}")
    print(f"mean steps after snapshot {r['mean_steps_after_snapshot'][0]:.0f} -> {r['mean_steps_after_snapshot'][1]:.0f}")
    print("failure classes (A -> B):")
    for c, (x, y) in list(r["failure_classes_a_b"].items())[:14]:
        print(f"  {x:6d} -> {y:6d}  {c}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--floor", type=int, default=8)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    r = compare(args.a, args.b, args.floor)
    show(r)
    if args.json:
        args.json.write_text(json.dumps(r, indent=1) + "\n")


if __name__ == "__main__":
    main()
