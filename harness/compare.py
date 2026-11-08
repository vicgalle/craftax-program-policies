#!/usr/bin/env python3
"""Paired comparison of two runs on the same cohort.

  python3 compare.py build/runs/base_D1 build/runs/cand_D1
  python3 compare.py build/runs/base_D1 build/runs/vault_v1_D1 --scope 4
  python3 compare.py build/runs/base_D1 build/runs/vault_screen_D1 --scope 4 --episodes 1024

--scope K declares the candidate's changes inert before its first arrival at
floor K. The check: every candidate episode has the baseline's arrival step and
action-prefix hash at floor K (or is identical when the baseline never reached
K). Baseline episodes that never reached K and were not rerun are then copied
as unchanged. Any violation disables the inference and is reported.
"""
import argparse
import collections
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fbf  # noqa: E402


def same_episode(a, b):
    return a["length"] == b["length"] and a["return"] == b["return"] and a["max_level"] == b["max_level"]


def divergence_floor(a, b):
    """First floor whose first-arrival step or action prefix differs (9 = after the last arrival)."""
    for L in range(1, 9):
        ra, rb = int(a[f"arrive{L}"]), int(b[f"arrive{L}"])
        if ra < 0 and rb < 0:
            break
        if ra != rb or a[f"hash{L}"] != b[f"hash{L}"]:
            return L - 1
    return None if same_episode(a, b) else max(int(a["max_level"]), int(b["max_level"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base", type=Path)
    ap.add_argument("cand", type=Path)
    ap.add_argument("--scope", type=int, help="candidate changes act only from first arrival at this floor")
    ap.add_argument("--episodes", type=int, help="restrict both runs to this many absolute episode indices")
    ap.add_argument("--start", type=int, default=0, help="first absolute index with --episodes")
    ap.add_argument("--json", type=Path, help="write the comparison here")
    a = ap.parse_args()

    mb, mc = fbf.load_meta(a.base), fbf.load_meta(a.cand)
    if mb["seed"] != mc["seed"]:
        raise SystemExit("different cohorts (seed)")
    B, C = fbf.load_episodes(a.base), fbf.load_episodes(a.cand)
    if a.episodes is not None:
        if a.episodes <= 0 or a.start < 0:
            raise SystemExit("--episodes must be positive and --start nonnegative")
        indices = range(a.start, a.start + a.episodes)
        if any(e not in B for e in indices):
            raise SystemExit("baseline lacks requested episode indices")
        B = {e: B[e] for e in indices}
        C = {e: c for e, c in C.items() if e in indices}
    elif a.start:
        raise SystemExit("--start requires --episodes")
    for e in C:
        if e in B and B[e]["world_seed"] != C[e]["world_seed"]:
            raise SystemExit(f"world mismatch at episode {e}")

    notes = []
    inferred = 0
    if a.scope is not None:
        K = a.scope
        violations = []
        for e, c in C.items():
            if e not in B:
                continue
            b = B[e]
            if int(b["max_level"]) >= K:
                if b[f"arrive{K}"] != c[f"arrive{K}"] or b[f"hash{K}"] != c[f"hash{K}"]:
                    violations.append(e)
            elif not same_episode(b, c):
                violations.append(e)
        uncovered = [e for e, b in B.items() if e not in C and int(b["max_level"]) >= K]
        if violations or uncovered:
            notes.append(f"SCOPE CHECK FAILED for floor {K}: {len(violations)} episodes differ before arrival, "
                         f"{len(uncovered)} baseline floor-{K} episodes not rerun. Comparing rerun episodes only.")
        else:
            for e, b in B.items():
                if e not in C:
                    C[e] = b
                    inferred += 1
            notes.append(f"scope check passed for floor {K}: {inferred} episodes that never reach floor {K} copied from baseline")

    eps = sorted(set(B) & set(C))
    B = {e: B[e] for e in eps}
    C = {e: C[e] for e in eps}
    n = len(eps)
    sb, sc = fbf.summarize(B), fbf.summarize(C)

    print(f"paired episodes: {n} (seed {mb['seed']})  base={a.base.name}  cand={a.cand.name}")
    for note in notes:
        print("  " + note)
    div = collections.Counter(divergence_floor(B[e], C[e]) for e in eps)
    unchanged = div.pop(None, 0)
    print(f"identical episodes: {unchanged}/{n}; first divergence by floor: "
          + " ".join(f"L{k}:{v}" for k, v in sorted(div.items())))

    rows = []
    print("\nreach floor     base    cand   gained  lost   McNemar p")
    for L in range(1, 9):
        gain = sum(int(C[e]["max_level"]) >= L > int(B[e]["max_level"]) for e in eps)
        loss = sum(int(B[e]["max_level"]) >= L > int(C[e]["max_level"]) for e in eps)
        p = fbf.mcnemar(gain, loss)
        rows.append({"floor": L, "base": sb["reach"][L], "cand": sc["reach"][L], "gained": gain, "lost": loss, "p": p})
        print(f"  {L} {fbf.FLOOR[L]:10s} {sb['reach'][L]:6d}  {sc['reach'][L]:6d}  {gain:+6d} {-loss:+6d}   {p:.3g}")

    print("\nconditional pass rate L->L+1     base [95% CI]            cand [95% CI]")
    for L in range(8):
        cells = []
        for s in (sb, sc):
            k, m = s["reach"][L + 1], s["reach"][L]
            lo, hi = fbf.wilson(k, m)
            cells.append(f"{(k / m if m else float('nan')):.3f} [{lo:.3f},{hi:.3f}] n={m:<5d}")
        print(f"  {L}->{L + 1} {fbf.FLOOR[L]:10s} {cells[0]}  {cells[1]}")

    d = [float(C[e]["return"]) - float(B[e]["return"]) for e in eps]
    mean = sum(d) / n
    sd = math.sqrt(sum((x - mean) ** 2 for x in d) / max(1, n - 1))
    ci = (mean - 1.96 * sd / math.sqrt(n), mean + 1.96 * sd / math.sqrt(n))
    print(f"\nmean return: base {sb['mean_return']:.3f}  cand {sc['mean_return']:.3f}  paired diff {mean:+.3f} [{ci[0]:+.3f}, {ci[1]:+.3f}]")
    print(f"floor 8: base {sb['reach'][8]}  cand {sc['reach'][8]}   boss wins: base {sb['boss_wins']}  cand {sc['boss_wins']}")

    keys = set(sb["failure_classes"]) | set(sc["failure_classes"])
    delta = sorted(keys, key=lambda k: -abs(sc["failure_classes"].get(k, 0) - sb["failure_classes"].get(k, 0)))
    print("\nfailure classes with the largest change (count base -> cand)")
    for k in delta[:14]:
        x, y = sb["failure_classes"].get(k, 0), sc["failure_classes"].get(k, 0)
        if x != y:
            print(f"  {x:6d} -> {y:6d} ({y - x:+5d})  {k}")

    if a.json:
        a.json.write_text(json.dumps({
            "base": str(a.base), "cand": str(a.cand), "seed": mb["seed"], "paired": n, "notes": notes,
            "inferred_unchanged": inferred, "identical": unchanged, "reach": rows,
            "base_summary": sb, "cand_summary": sc,
            "return_diff": {"mean": mean, "ci95": ci},
        }, indent=1) + "\n")


if __name__ == "__main__":
    main()
