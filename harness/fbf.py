"""Shared loading, funnel and failure-taxonomy code for floor-by-floor runs."""
import collections
import csv
import gzip
import json
import math
from pathlib import Path

FLOOR = ["overworld", "dungeon", "gnomish", "sewers", "vault", "troll", "fire", "ice", "graveyard"]
MELEE = ["zombie", "gnome_warrior", "orc_soldier", "lizard", "knight", "troll", "pigman", "frost_troll"]
PROJ = ["arrow", "dagger", "fireball", "iceball", "arrow2", "slimeball", "fireball2", "iceball2"]


def open_text(path):
    path = Path(path)
    return gzip.open(path, "rt", newline="") if path.suffix == ".gz" else open(path, newline="")


def load_episodes(run_dir):
    """Map absolute episode index -> row dict."""
    run_dir = Path(run_dir)
    path = run_dir / "episodes.csv.gz"
    with open_text(path) as f:
        return {int(r["episode"]): r for r in csv.DictReader(f)}


def load_meta(run_dir):
    return json.loads((Path(run_dir) / "summary.json").read_text())


def killer(r):
    s, t = int(r["killer_src"]), int(r["killer_type"])
    if s == 0:
        return "melee:" + MELEE[t] + (" (asleep)" if r["killer_sleep"] == "1" else "")
    if s == 1:
        return "projectile:" + PROJ[t]
    if s == 2:
        need = [x for b, x in ((1, "food"), (2, "drink"), (4, "energy")) if t & b]
        return "starve:" + ("+".join(need) or "?")
    if s == 3:
        return "potion"
    return "unknown"


def failure_class(r):
    """One terminal class per episode."""
    if r["died"] != "1":
        return "survived to horizon"
    L, mx = int(r["death_level"]), int(r["max_level"])
    k = killer(r)
    kind = "starvation/thirst" if k.startswith("starve") else k.split(":")[0]
    if L == 0 and int(r["length"]) < 1000 and k.startswith("starve"):
        return "L0 early thirst deadlock"
    if L < mx:
        return f"return trip: died on L{L} after reaching L{mx}"
    return f"L{L} {FLOOR[L]}: {kind}"


def funnel(E):
    reach = [sum(int(r["max_level"]) >= L for r in E.values()) for L in range(9)]
    deaths = collections.Counter(int(r["death_level"]) for r in E.values() if r["died"] == "1")
    passes = [reach[L + 1] / reach[L] if reach[L] else float("nan") for L in range(8)]
    return reach, passes, deaths


def wilson(k, n, z=1.96):
    if n == 0:
        return (float("nan"), float("nan"))
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (c - h, c + h)


def mcnemar(b, c):
    """Exact two-sided McNemar p-value for b gains and c losses."""
    m = b + c
    if m == 0:
        return 1.0
    k = min(b, c)
    tail = sum(math.comb(m, i) for i in range(k + 1)) / 2 ** m
    return min(1.0, 2 * tail)


def summarize(E):
    n = len(E)
    reach, passes, deaths = funnel(E)
    classes = collections.Counter(failure_class(r) for r in E.values())
    killers = collections.defaultdict(collections.Counter)
    for r in E.values():
        if r["died"] == "1":
            killers[int(r["death_level"])][killer(r)] += 1
    returns = [float(r["return"]) for r in E.values()]
    return {
        "episodes": n,
        "reach": reach,
        "pass_rates": passes,
        "p_floor8": reach[8] / n if n else float("nan"),
        "deaths_by_floor": [deaths[L] for L in range(9)],
        "mean_return": sum(returns) / n if n else float("nan"),
        "mean_length": sum(int(r["length"]) for r in E.values()) / n if n else float("nan"),
        "boss_wins": sum(r["defeat_necromancer"] == "1" for r in E.values()),
        "failure_classes": dict(classes.most_common()),
        "killers_by_floor": {FLOOR[L]: dict(killers[L].most_common()) for L in sorted(killers)},
    }


def format_summary(s):
    lines = [f"episodes {s['episodes']}  mean return {s['mean_return']:.3f}  mean length {s['mean_length']:.0f}  "
             f"floor-8 {s['reach'][8]} ({100 * s['p_floor8']:.3f}%)  boss wins {s['boss_wins']}",
             "floor          reach  deaths  pass(L->L+1)"]
    for L in range(9):
        p = s["pass_rates"][L] if L < 8 else float("nan")
        lines.append(f"  {L} {FLOOR[L]:10s} {s['reach'][L]:6d} {s['deaths_by_floor'][L]:6d}   {p:6.3f}")
    lines.append("top failure classes (share of all episodes)")
    for k, v in list(s["failure_classes"].items())[:14]:
        lines.append(f"  {v:6d} {100 * v / s['episodes']:5.1f}%  {k}")
    return "\n".join(lines)
