# Program policies

Each policy is a C program: a directory with `full_policy.c`, `full_policy.h` and
the `.inc` files they include. It reads only the public observation and its own
memory, makes no model calls, and plays with its compiled defaults.

| Directory | What it is |
|---|---|
| `optimized/` | The **Final** checkpoint of the arrival-focused lineage: floor-8 arrival in 1,034/2,048 common worlds (50.49%, Wilson 95% interval [48.32, 52.65]) |
| `optimized_extension/` | The **boss-victory extension**, refined from Final for defeating the Necromancer. It wins 581/2,048 common worlds (28.37%), reaches floor 8 in the same 1,034 worlds, and is the policy P* factored in `../factorization` |
| `checkpoints/` | The other 24 retained checkpoints of the lineage (Figure 2 and the appendix lineage table). Final is `optimized/` and is not duplicated |
| `controls/` | The two matched-budget controls (round-11 control, round-15 control) |
| `evaluate_full.c` | Native evaluator: plays a policy against `../engine/craftax_full.c` and writes one row per world |
| `evaluate.py` | Cohort runner: compiles, runs in parallel chunks and summarizes (Python standard library only) |
| `summarize.py` | Recomputes the lineage table and the paired contrasts from the archived rows (no simulation) |
| `results/` | Archived rows and result records (see below) |

## Checkpoints

| Directory | Paper label | Regime |
|---|---|---|
| `00_initial` | Initial | A |
| `01_r1` … `10_r10` | R1–R10 | A |
| `11_base` | Base | A |
| `12_goal_interface` | Goal interface | A |
| `13_r11` … `15_r13` | R11–R13 | B |
| `16_r14` | R14 | C |
| `17_r15`, `18_r16` | R15, R16 | D |
| `19_kit_goals` | Kit goals | D |
| `20_world_model` | World model | E |
| `21_composition` | Composition | D |
| `22_regions` | Regions | D |
| `23_modules` | Modules | E |
| `../optimized` | Final | E |

The checkpoints are not all sequential descendants: Kit goals and World model
branch from R16 and are composed afterwards. The from-scratch run has no
sources here; its rows are in `results/`.

## Reproduce

From the repository root, with Python 3 and a C compiler:

```sh
# Final: 1,034 arrivals, 183 victories, mean return 166.98 (2,048 worlds, seed 81,000,000)
python3 policies/evaluate.py policies/optimized --check policies/results/common_cohort/optimized.csv.gz
# Boss-victory extension: 581 victories, the same 1,034 arrivals
python3 policies/evaluate.py policies/optimized_extension \
    --check policies/results/common_cohort/optimized_extension.csv.gz
# Any checkpoint, for example the first 128 worlds of R16
python3 policies/evaluate.py policies/checkpoints/18_r16 --episodes 128 \
    --check policies/results/common_cohort/18_r16.csv.gz
# Lineage table and paired contrasts from the archived rows
python3 policies/summarize.py
```

`evaluate.py` compiles with `cc -O3 -std=c11 -ffp-contract=off`, the flags
used for every result in the paper. Episode i plays the world with seed
uint32(splitmix64(seed + i)), and episodes continue after floor-8 arrival up to
the 100,000-action horizon. `--check` requires every row to equal the archived
record. Use `--workers` to set parallelism; a full 2,048-world run took about
16 minutes with 5 workers on an Apple-silicon laptop.
`summarize.py` prints, among other values, Final minus Modules +4.59 points
[+2.44, +6.74] (301 gained, 207 lost) and extension minus Final +19.43 points
[+17.45, +21.42] in victories (453 gained, 55 lost).

## Results

`results/common_cohort/` has the per-world rows of the common cohort (2,048
worlds, seed 81,000,000, horizon 100,000) for every checkpoint (`NN_label`),
`optimized`, `optimized_extension`, both controls, and the first and final
policies of the from-scratch run. The columns are world seed, native return,
achievements, length, final health, deepest floor, boss progress and victory.

| File | Content |
|---|---|
| `lineage.json` | Per-checkpoint arrival, victories and return with intervals; cumulative output tokens and regime (Figure 2 and the appendix lineage table) |
| `frozen_comparisons/` | The frozen paired comparisons of Table 2 (selected compositions): R14 to R16, world model and shield, modules over composition; the recursive row is campaign 3 of `final_campaigns.json` |
| `final_campaigns.json` | The five recursive campaigns that produced Final, each on its own frozen cohort (appendix table of recursive campaigns) |
| `extension_campaigns.json` | The three boss-victory campaigns (appendix table) and the common-cohort comparison with Final |
| `goal_level_rounds.json` | Development and frozen statistics of rounds 14–16 |
| `edit_ledger.json` | The 152 candidate edits of rounds 15–16 with local, repeated-use and endpoint estimates (the proxy-versus-endpoint figure) |

In these archived records, `response_cover` is Final (`optimized/`) and
`boss_progress_rescue` is the boss-victory extension (`optimized_extension/`).
Fields that hold file paths refer to the development repository; they are
kept for provenance, and those files are not part of this release.

Tested only on macOS (arm64) with Apple clang 21.
