# Feedback-cascade harness (stage 1)

This is the evaluation harness that coding agents used to refine the program
policy, kept as it was used and adapted only to this repository's layout. It
plays policies on the native engine, records per-floor funnels and goal
invocations, captures and resumes savestates, and audits beliefs against
simulator truth. It never changes a policy and never feeds hidden state to it.

All tools build a policy directory (`full_policy.c`, `full_policy.h` and the
files they include) against `../engine/craftax_full.c`. They use an
instrumented copy of the engine (`patch_engine.py`), which adds side-channel
damage counters for diagnostics. Gameplay and RNG draws are unchanged. Outputs
go to `build/` (`build/runs`, `build/banks`, `build/branch_runs`).

## Files by feedback regime (Table 1 of the paper)

| Regime | Feedback | Files |
|---|---|---|
| A | Episode outcomes from fresh worlds | `../policies/evaluate.py` (cohort runner); `run.py` + `autopsy.c` (the same episodes with per-floor visits, failure taxonomy and death causes); `fbf.py` (shared loading, funnel and taxonomy) |
| B | Per-milestone pass rates | `run.py --reach-floor K --baseline RUN` reruns only the worlds where the incumbent reached floor K. `compare.py --scope K` checks that the candidate's actions match the incumbent's up to its arrival at floor K, then reports paired per-floor pass rates (McNemar). `macrostate.py` splits each floor pass into clear, exit discovery and departure |
| C | Savestate branching | `bank.py capture` saves engine state, future RNG key and policy memory at macro-state triggers (`arrive:K`, `clear:K`, `ready:K`, `up:K`). `bank.py resume` continues a bank with any policy for R replicas: replica 0 replays the captured episode exactly, and replicas r>0 rekey future randomness. `bank_compare.py` pairs runs by (world, snapshot, replica) and clusters intervals by world. Implemented in `branch.c` |
| D | Persistent goals | `hhmm.h` labels goal invocations (ADVANCE, CLEAR, FIND_EXIT, DEPART, EXCURSION, HEAL, FIGHT) from controller memory without changing actions. Repeated triggers (`leave:K`, `enter:K`, `gheal:K`, `gfight:K`) capture goal entries. `resume --handoff-dir INC --handoff COND` runs the candidate for one invocation and then the incumbent (single-invocation scope); without a handoff it measures repeated use. `goals.py` and `trips.py` summarize invocation logs and the eventual floor-8 rate by outcome. `hybrid_inc.c` links the incumbent in its own translation unit |
| E | Model audits | `wm_eval.c`, `wm.py`, `audit.py`: replay a policy from reset or from a bank, compare its world-model beliefs and predictions with engine truth at every step (damage predictions, unpredicted hits by false belief, enemy health and cooldown errors), and optionally replace beliefs with truth (`--oracle hp,cooldown,shots,unseen`) |

Counterfactual probes (Section 2.3 of the paper): `bank.py resume --oracle`
applies diagnostic engine interventions such as free food or armour, or no
damage on revisited floors. They are used only to estimate ceilings, never to
evaluate a policy.

## Which checkpoints

The tools were written as the lineage went on. `run.py` and `compare.py` are
from rounds 11–13, `bank.py`/`branch.c` from round 14, and `hhmm.h`, handoffs
and repeated triggers from rounds 15–16. The world-model audit came with the
World model checkpoint. All of them build against every checkpoint from Initial to
Final (`../policies/checkpoints`, `../policies/optimized`). The goal labels in
`hhmm.h` are defined over the R15/R16 controller memory. The audit needs a
world-model policy, meaning a `full_wm.inc` with the `fp_wm_oracle` hook: World
model, Composition, Regions, Modules and Final. A bank stays valid for any
candidate that only appends fields to the policy memory `FPAgent`.

## Smoke test

Run from this directory, with Python 3 (standard library) and a C compiler with
zlib:

```sh
P=../policies/checkpoints/18_r16
# A/B: runs from reset and a paired per-floor comparison
python3 run.py --policy-dir ../policies/checkpoints/17_r15 --name r15 --seed 81000000 --episodes 8
python3 run.py --policy-dir $P --name r16 --seed 81000000 --episodes 8
python3 compare.py build/runs/r15 build/runs/r16
# C/D: capture floor-3 arrivals, resume them twice, compare (zero difference)
python3 bank.py capture --policy-dir $P --name smoke --seed 81000000 --episodes 16 --trigger arrive:3 --log-goals
python3 bank.py resume --policy-dir $P --bank build/banks/smoke/arrive3.bin --name a --replicas 2
python3 bank.py resume --policy-dir $P --bank build/banks/smoke/arrive3.bin --name h --replicas 2 \
    --handoff-dir $P --handoff steps:200
python3 bank_compare.py build/branch_runs/a build/branch_runs/h --floor 8
python3 goals.py build/banks/smoke
# E: belief audit of the World model checkpoint on 4 worlds from reset
python3 audit.py ../policies/checkpoints/20_world_model --reset 81000000:0:4
```

Expected results: the R16 runs reproduce the archived common-cohort rows (for
example world 0 ends at step 12,451 with return 66.1). The resumed bank gives
24 pairs with identical outcomes and a difference of +0.00 points. Replica 0
of each snapshot reproduces the captured episode up to floor 8.

Tested only on macOS (arm64) with Apple clang 21.
