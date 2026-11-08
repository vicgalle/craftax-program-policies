# Resource delivery failure diagnosis (old development worlds only)

The old shared controller's resource-delivery failures include **directly observed repeated spending blocks**, alongside a distinct early resource-search/death case. Equipment readiness alone does not explain the long stalls. This feedback uses only the already-used old `mixed_delivery` worlds 0, 2 and 4; they are development evidence and must never be counted as next heldout episodes.

Goal: acquire wood 8 and iron 3, maintain wood 4 and iron 1, then reach floors 2, 0, 3. Horizon: 50,000. For old episode `e`, a one-episode diagnostic run uses seed `1950000000 + e`; the evaluator's existing `uint32_t(mix(seed))` produces the original world. See [machine-readable evidence](resource_failure_evidence.json), [rule/definition mappings](resource_rule_mappings.json), [reproduction script](reproduce_resource_failure.py), and [passive instrumentation script](instrument_resource_failure.py).

| Old episode / world seed | Incumbent stage completion times | Incumbent result | Reference result | Guards result |
| --- | --- | --- | --- | --- |
| 0 / 2439472416 | none | death at 1,329 | 3/4; timeout | identical death at 1,329 |
| 2 / 2361406038 | 448 | 1/4; timeout | 1/4; timeout | 3/4; timeout |
| 4 / 3995976117 | 5,355; 7,286; 7,539 | 3/4; timeout | success at 8,859 | 3/4; timeout |

All nine ordinary runs used the new `run.run(..., trace=True)` in `generated/diagnostics/` and `runs/diagnostics/mixed_delivery_*`. Every original row field, including action hash, matches the corresponding old CSV, except the local episode index is zero. The nine instrumented replays are under `runs/diagnostics/instrumented_*`: their rows are byte-identical to the ordinary runs and every ordinary trace column also matches. Source, image, binary and trace hashes are recorded in the evidence JSON. Instrumented machine/evaluator copies live entirely in `diagnostics/`; the actual guards, monitor, progress, frozen machine, policies, linker and runner were unchanged.

## Actual block evidence

Telemetry records the actual argument and return value of the existing `sq_forbidden`, plus selector action, base-filter result, controller/gather returns, equipment and policy memory. It does not classify NOOPs heuristically.

* **Incumbent episode 2:** 24,867 rejected actions: 22,661 `PLACE_TABLE` and 2,206 `IRON_ARMOUR`. The first is step 630: wood 9, iron 3, iron armour chosen by `r_craft_first_iron_armour`; spending 3 iron would leave 0 below the maintained 1. Steps 3,059–3,140 are 82 consecutive blocked table placements at the same position: wood 4, iron 3, pick 3, sword 3, a known downward ladder and 17 remembered trees. Throughout the final 10,000 steps wood is exactly 5, iron exactly 4 and equipment 3/3; the downward ladder is known on every step. There are 4,721 `sr_control` calls, all returning -1, **zero** `sr_gather` calls, and 5,167 further table attempts. The incumbent explicitly returns -1 for a deeper reach target, handing control to the frozen policy. Repeated rejected crafting preempts lower-priority wood supply/descent behavior. Known trees do not prove reachability, but this trace is not an attempted resource search.
* **Incumbent episode 4:** after completing the return to floor 0 at 7,539, it has 20,710 rejected actions: 17,117 tables and 3,593 iron armour. The first armour block is at 7,991 on floor 2 with iron 3. In the final 10,000 steps wood stays 4 and iron stays 3, gear stays pick 3/sword 4, and 5,318 table placements are rejected. The main originating rules are `r_arrows` and `r_craft_iron_armour`. Zero gather calls occur in that tail. Some floor-2 intervals have no discovered down ladder, so exploration/navigation is also unfinished; blocked crafting is nevertheless an observed source of lost turns. The reference succeeds on this same world with **zero** rejected actions, holding wood 17 on entry to floor 3. This comparison is descriptive, not an isolated causal ablation.
* **Reference/guards also suffer blocks:** reference episode 0 has 20,736 and reference episode 2 has 21,262 rejected actions; guards episodes 2 and 4 have 18,862 and 19,337. A direct travel controller helps when it can supply an action, but failures of travel/search or survival eligibility still expose the guarded frozen fallback.

The frozen `craft_radius` checks recipe and station inputs against **raw inventory**, without reserve margins. A table needs 2 wood, so wood 4 or 5 passes its raw input test but fails the fixed reserve-4 guard. An iron-armour action needs 3 iron, so iron 3 passes raw recipe inputs but fails reserve-1. The post-selection guard replaces the chosen action with `NOOP/RECOVER`; it does not try the next rule. This explains the observed repeated choice/block pattern without changing any machine semantics.

## Equipment gate and the separate early failure

The incumbent gates acquire-stage control on pick and sword tier 3. Episode 0 contains 296 observed `sr_control` failures while that gate is false. After obtaining gear, it makes 194 iron-gather calls returning -1; post-action iron never exceeds 1, and it dies at 1,329 on floor 1 with five enemies recorded. There are **zero spending blocks**, because acquire was never completed and reserves never activated. Guards follows the identical trajectory. Gear gating demonstrably defers acquire control, but these traces do not isolate it as the cause of death. Resource discovery/travel and survival remain relevant here.

Conversely, episodes 2 and 4 retain sufficient gear throughout their final 10,000 steps; their long stalls are not waiting to satisfy the incumbent's acquire gear gate. Reference episode 2 also has 19,450 controller failures on floor 1 with an unknown downward ladder, demonstrating a separate route-discovery limitation alongside its observed craft blocks.

## Actionable feedback within the fixed protocol

1. Plan usable surplus above **maintained reserves**, including station placement, recipe inputs and expected future gear costs; merely satisfying `acquire` does not make subsequent crafting affordable.
2. Handle deeper reach goals and missing routes explicitly. When travel/search returns -1, choose productive exploration or reserve-safe replenishment rather than repeatedly exposing an unaffordable high-priority frozen craft.
3. Keep a resource-recovery path active during reach stages. All selected late incumbent tails make zero explicit gather calls despite wood pinned at its reserve margin.
4. Evaluate preparation gates separately from late delivery stalls. Minimal safe preparation can help early acquisition; tier-3 equipment is already present in the measured late failures. Preserve survival rules and the fixed monitor/guard semantics.

Cost: **18 diagnostic episodes** (nine ordinary + nine passive replays), **three unique old worlds**, **623,034 total engine steps**. Recorded evaluator time sums to 9.374 s ordinary + 9.926 s instrumented; compilation/analysis time is not included. Maximum two CPU workers. No future goals, heldout worlds or new synthesis sources/results were read.
