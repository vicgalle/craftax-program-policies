# Goal compilers: transfer to unseen goal programs

A goal compiler maps a structured task g to a new rule program p_g over the fixed
machine I and is then run as I(p_g) (specialized as mix(I, p_g)). It makes no
model calls at deployment:

```
C(g; p0) = Link(p0, Encode(g), E(g))
```

- **Encode(g)** is fixed (`compiler.py: common`). It turns the task into constant stage tables (`SQ_N`, `sq_floor`, `sq_stock`, `sq_hold`), progress tracking, and the hard reserve filter that blocks spending below active reserves.
- **E(g)** is the **emitter**, the part that coding agents optimized. It is a self-contained Python module whose `emit(goal)` returns helper definitions and exactly one controller rule.
- **Link** (`compiler.py: link`) inserts that rule into p0 (`../factorization/p0`) just above `r_strategic`, below the inherited survival rules.

A task is a sequence of up to eight stages:

- `{"reach": floor}`;
- `{"acquire": {...}, "maintain": {...}}`, where `maintain` is optional and bounded by `acquire`.

Stages must be completed in order and alive. Maintained reserves accumulate as
coordinatewise maxima and hold until the task ends. An independent monitor
(`monitor.h`, driven by `evaluate.c`) scores progress from engine state. It never
reads the controller's own progress fields.

## Contents

| Path | Role |
|---|---|
| `compiler.py` | Task parser, Encode, Link, and emitter loading. |
| `reference.py` | Emitter of the Reference control: a prescribed reactive sequence controller. |
| `incumbent.py` | The starter emitter. All shared and per-task searches started from it and saw its feedback on their training worlds. The file keeps the name used in the search prompts. |
| `synthesis/run_{1..5}/` | The five shared searches. Each holds three candidates (`candidateK.py`, archived as `round_K/source.py`), their training records (`round_K/result.json`, `roundK.log`), `closed.json`, the agent's `notes.txt` and `agent_run.json` (elapsed time), the selection record `selection.json` and the selected emitter `selected.py`. |
| `direct/<task>/` | The twelve per-task searches, with the same layout. |
| `selected.json` | Index of the 5 shared and 12 per-task selected emitters. |
| `goals/`, `task_manifest.json` | 16 training, 8 validation and 48 test task programs (floors 0–3, wood/stone/coal/iron), the strata, and the predeclared 12-task per-task subset. |
| `generate_tasks.py` | Deterministic task generator (seed 20261024). It regenerates `goals/` and `task_manifest.json` byte for byte and excludes 13 earlier pilot programs (`goals/excluded_previous_sequences.json`). |
| `prompts/` | The verbatim assignments given to the coding agents (see below). |
| `diagnostics/` | The common resource-failure diagnosis (old development worlds) given to every search. |
| `run.py`, `study.py` | Link, compile with trc, build, and evaluate tasks × arms. Outputs go to `build/`. |
| `evaluate_study.py` | The held-out evaluation and the baseline feedback runs. |
| `search.py`, `audit.py`, `selection.py` | Run a search: candidate audit, compile preflight, training feedback, then selection on separate worlds. |
| `report.py` | Analysis from the retained rows: rates, crossed bootstrap, strata, costs, figures. |
| `validate.py` | Execution checks: interpreted/specialized parity, the negative control, and emitter audits. |
| `tests/` | Unit tests for the semantics, monitor, audit, generator and analysis. |
| `results/` | Retained data (see below). |

## Cohorts

All episodes use a 50,000-action horizon. Worlds are `uint32(splitmix64(seed + episode))`.

| Purpose | Tasks | Worlds per task | Seed |
|---|---:|---:|---:|
| Shared training | 16 (lengths 2–4) | 4 | 2,000,000,000 |
| Shared selection | 8 (lengths 2–4) | 8 | 2,010,000,000 |
| Held-out test | 48 (2–4 / 5–6 / 7–8) | 32 | 2,020,000,000 |
| Per-task tuning | 12 test specifications | 8 | 2,030,000,000 |
| Per-task selection | the same 12 | 8 | 2,040,000,000 |

## Reproduce

Run from the repository root. Running uses Python's standard library, clang and
clang++. The analysis also needs NumPy and Matplotlib.

```sh
# Analysis from the retained held-out rows -> results/compare.json, compare.md, figures/
python3 compiler/report.py

# Unit tests (23)
python3 -m unittest discover -s compiler/tests -v

# Execution checks
python3 compiler/validate.py controls     # Original = Guards on the 18 navigation tasks: 576 identical pairs
python3 compiler/validate.py audit        # audits the 17 selected emitters
python3 compiler/validate.py parity       # 28 interpreted/specialized checks, 262,588 decisions, equal to results/parity_rows

# Compile one task with a frozen emitter (the worked example in the paper)
python3 compiler/compiler.py compiler/goals/test/t_d48da6015806f6ea.json --arm synthesized \
    --controller compiler/synthesis/run_5/selected.py --output compiler/build/example
python3 compiler/run.py compiler/build/example --seed 2020000000 --episodes 4 --output compiler/build/runs/example

# Re-simulate held-out cells and compare them row by row with results/rows
python3 compiler/evaluate_study.py test --tasks t_d48da6015806f6ea t_63af4ce4a2c84ff1 t_c0a968ce34d45ca2 --episodes 4 --check   # ~2 min on 5 cores
python3 compiler/evaluate_study.py test --check        # all 12,672 held-out episodes
```

Expected `report.py` results on the 48 test tasks × 32 worlds:

| Method | Success | Maintenance violations |
|---|---:|---:|
| Original | 32.36% | 164 |
| Guards | 36.20% | 0 |
| Reference | 36.91% | 0 |
| Shared (mean of five searches; 70.70–72.07%) | 71.30% | 0 |
| Per-task (12-task subset; Shared on the same subset: 69.48%) | 65.62% | 0 |

The primary contrast, Shared minus Guards, is +35.10 points, with crossed-bootstrap
95% interval [+25.78, +45.08]. Shared minus Per-task on the subset is +3.85 points
[−2.34, +9.90]. By length, Shared scores 74.88 / 71.80 / 67.23% against Guards'
57.03 / 34.18 / 17.38%.

Every reported episode uses the specialized build. Each distinct task program is
compiled once, so a full held-out re-run is dominated by clang builds.

## Re-running a search

`prompts/shared_search_prompt.txt` and `prompts/direct_search_prompt.txt` are the
exact assignments. They refer to the original repository layout:
`benchmarks/goal_composition/` is `compiler/` here, and the agent's working
directory should be a new search directory rather than the archived ones.

1. Produce the baseline feedback: `python3 compiler/evaluate_study.py baseline-shared`. Its rows are also retained in `results/feedback_rows/baseline_shared*`.
2. For each candidate, have the agent run `python3 compiler/search.py shared --run N --candidate K --controller compiler/build/search/shared_run_N/candidateK.py`.
3. Select with `python3 compiler/selection.py shared --run N`. The selection feedback is not returned to the agent.

Per-task searches work the same way with `direct --goal compiler/goals/test/TASK.json`.
The archived searches in `synthesis/` and `direct/` are read-only.

## Retained data (`results/`)

| Path | Contents |
|---|---|
| `rows/<arm>_<task>.csv` | Raw held-out rows: one row per world with the monitor's outcome, milestone times and action hash. Arms are `original`, `guards`, `reference`, `shared_run_1..5` and `direct`. |
| `feedback_rows/` | Every training, selection and baseline feedback group seen or used by the searches (`rows.csv`, `summary.json`, `plan.json`, `goal.txt`). |
| `parity_rows/` | The 28 interpreted/specialized parity runs. |
| `diagnostic_traces/` | The 18 common diagnostic and replay episodes on old development worlds. |
| `compare.json`, `compare.md`, `figures/` | Output of `report.py`. |

Tested only on macOS (arm64) with Apple clang 21, Python 3.12, NumPy 2.4 and Matplotlib 3.9.
