# Composition transfer with independent coding searches

Five independent shared searches, 48 predeclared test tasks and 32 common test worlds per task. Three fixed controls use the same worlds. Twelve predeclared tasks also have fresh per-task searches. The action horizon is 50,000.

| Method | Success | Violations | Failure-penalized steps |
|---|---:|---:|---:|
| Original | 32.36% (497/1536) | 164 | 38358 |
| Guards only | 36.20% (556/1536) | 0 | 37258 |
| Reference | 36.91% (567/1536) | 0 | 35104 |
| Shared search (mean of five) | 71.30% (5476/7680) | 0 | 23531 |
| Per-task search (12-task subset) | 65.62% (252/384) | 0 | 21552 |

Shared episode totals pool five searches for display; the estimand averages searches and tasks. The searches are the independent coding units.

| Contrast | Difference (percentage points) | Crossed bootstrap 95% interval | Status |
|---|---:|---:|---|
| shared minus guards | +35.10 | [+25.78, +45.08] | primary predeclared contrast |
| shared minus reference | +34.39 | [+23.27, +45.38] | secondary descriptive interval |
| shared minus original | +38.95 | [+29.51, +48.93] | secondary descriptive interval |
| shared minus direct subset | +3.85 | [-2.34, +9.90] | secondary descriptive interval; predeclared 12-task subset only |

Each of 10,000 deterministic bootstrap draws resamples five coding searches, 32 paired worlds with one common world sample across every task and arm, and tasks within each length × resource-kind cell. This preserves paired outcomes and the 6/5/5 task composition within each length stratum. Secondary intervals are descriptive and are not adjusted for multiple comparisons.

| Shared search | Success | Task/world bootstrap 95% interval |
|---|---:|---:|
| shared_run_1 | 70.70% | [59.77, 80.28] |
| shared_run_2 | 70.83% | [60.42, 80.14] |
| shared_run_3 | 72.07% | [61.20, 81.51] |
| shared_run_4 | 71.74% | [60.94, 81.18] |
| shared_run_5 | 71.16% | [60.61, 80.53] |

Across independent searches, test success ranges from 70.70% to 72.07% (sample SD 0.59 percentage points).

Milestone reach rates, failure-stage counts, task tables, stratified results and axis-specific variation diagnostics appear in [compare.json](compare.json). Unreached stages retain time −1; a success must finish every milestone without a reserve violation. Figures retain all five search outcomes.

Candidate costs count training and validation/selection episodes. Baseline feedback is reported separately as physical unique episodes and logical exposure for each search. The cost figure charges each search its full baseline exposure, covers only the observed 12-task subset, and shows test quality alongside episodes and environment steps. Old-task diagnostics are common development overhead. Model tokens are unavailable; these data do not establish equal quality or lower total synthesis cost.

Tasks were declared without outcome-based feasibility filtering. Results measure this task generator, fixed monitor, controller interface and game implementation. Acquisition attains inventory thresholds; it need not collect new units after stage activation. Five searches provide limited evidence about coding-agent variability. Each direct task has one search; its comparison conditions on those selected direct controllers. Sequence length counts milestones, some of which can be satisfied by existing inventory. All lengths share a fixed horizon, so length curves also reflect greater action demand. The direct subset is resource-heavy and has three lower-length and one upper-length task per stratum; its comparisons apply to those twelve tasks. Bootstrap intervals do not establish equivalence or arbitrary-program generalization.

[Success by length](figures/success_by_length.pdf) · [Success by resource kind](figures/success_by_kind.pdf) · [Observed subset cost and quality](figures/observed_subset_cost_and_quality.pdf) · [Raw rows](rows/)
