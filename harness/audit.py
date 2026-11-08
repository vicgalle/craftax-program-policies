#!/usr/bin/env python3
"""World-model audit: beliefs and predictions against engine truth.

Runs a policy directory from bank snapshots (or reset) with --audit and prints
its error profile on the states that policy visits:

  damage      predicted incoming damage for the chosen action vs actual combat
              damage (melee and projectiles), false alarms
  surprises   hits the WM did not predict, by the false belief behind them:
                unseen    attacker not tracked (darkness, out of view, lost track)
                cooldown  tracked attacker believed to be cooling down
                kill      our blow was believed lethal and it was not
                shot      projectile hit on a cell not predicted dangerous
                other     tracked, ready and adjacent, yet predicted no damage
  beliefs     tracked-enemy health error, lethal-blow misbeliefs (over: believed
              lethal but not; under: lethal but not believed), cooldown errors on
              adjacent enemies, tracked enemies not where believed (ghost), steps
              with an enemy hidden in darkness within two cells
  deaths      deaths before floor 8 with a surprise in their last ten steps, by kind

  python3 audit.py policy --bank arrive5 --split train --limit 600
"""
import argparse
import json
from pathlib import Path

import wm

NAMES = ["unseen", "cooldown", "kill", "shot", "other"]


def summarize(rows):
    keys = [k for k in rows[0] if k not in ("index", "ep", "world_seed", "snap_t", "replica", "max_level", "died", "t_end",
                                            "level_end", "health", "hash", "death_mask") and not k.startswith("arrive")]
    t = {k: sum(float(r[k]) for r in rows) for k in keys}
    deaths = [int(r["death_mask"]) for r in rows if r["died"] == "1" and int(r["max_level"]) < 8]
    return {
        "continuations": len(rows),
        "arrival": sum(int(r["max_level"]) >= 8 for r in rows) / len(rows),
        "steps": t["steps"],
        "damage": {"actual": t["actual_sum"], "melee": t["melee_sum"], "shot": t["shot_sum"], "predicted": t["pred_sum"],
                   "combat_steps": t["combat_steps"], "false_alarms": t["false_alarm"]},
        "surprises": {n: {"hits": t[f"s_{n}"], "damage": t[f"sd_{n}"]} for n in NAMES},
        "surprise_damage_share": sum(t[f"sd_{n}"] for n in NAMES) / max(t["actual_sum"], 1e-9),
        "beliefs": {"tracked": t["hp_n"], "hp_abs_mean": t["hp_abs"] / max(t["hp_n"], 1), "kill_over": t["kill_over"],
                    "kill_under": t["kill_under"], "ghost": t["ghost"], "adjacent": t["cd_n"],
                    "cd_ready_missed": t["cd_ready_missed"], "cd_cool_missed": t["cd_cool_missed"],
                    "hidden_near_steps": t["hidden_near"]},
        "deaths": {"before_8": len(deaths), "with_surprise": sum(1 for m in deaths if m),
                   **{n: sum(1 for m in deaths if m >> i & 1) for i, n in enumerate(NAMES)}},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("policy", nargs="?", default=str(wm.POLICY))
    ap.add_argument("--bank", default="arrive5")
    ap.add_argument("--bank-dir", default=str(wm.BANKS / "d62"))
    ap.add_argument("--split", choices=["all", "train", "val"], default="train")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--replicas", default="1:2")
    ap.add_argument("--reset", help="SEED:START:COUNT instead of a bank")
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.reset:
        seed, start, count = map(int, a.reset.split(":"))
        rows = wm.eval_reset(seed, start, count, a.policy, audit=True)
    else:
        bank = Path(a.bank) if Path(a.bank).exists() else Path(a.bank_dir) / f"{a.bank}.bin"
        tr, va = wm.split(bank)
        sel = None if a.split == "all" else (tr if a.split == "train" else va)
        if sel is not None and a.limit:
            sel = sel[:a.limit]
        r0, r1 = map(int, a.replicas.split(":"))
        rows = wm.eval_bank(bank, a.policy, replicas=(r0, r1), select=sel, audit=True)
    s = summarize(rows)
    print(json.dumps(s, indent=1))
    if a.out:
        Path(a.out).write_text(json.dumps(s, indent=1))


if __name__ == "__main__":
    main()
