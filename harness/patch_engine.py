"""Write a side-channel-instrumented copy of craftax_full.c.

Adds writes to file-static counters next to the four statements that lower
player health (melee attack, projectile hit, intrinsic de-recovery, harmful
potion). Gameplay, RNG draws and control flow are unchanged; run.py's
--check mode verifies identical episodes against the stock evaluator.
"""
import sys
from pathlib import Path

HEADER = r"""
/* ---- autopsy side channel (diagnostics only) ---- */
static float g_dmg[4];              /* 0 melee, 1 projectile, 2 intrinsic, 3 potion */
static int g_last_src = -1, g_last_type = -1, g_last_sleep = 0;
static float g_last_amt = 0.0f;
static int g_hits[2];               /* melee / projectile hit events this step */
"""

PATCHES = [
    ("    state->player_health -= damage * (float)(int32_t)is_attacking_player;\n",
     "    if (is_attacking_player && damage > 0.0f) { g_dmg[0] += damage; g_hits[0]++; g_last_src = 0; g_last_type = mob_type;"
     " g_last_sleep = state->is_sleeping; g_last_amt = damage; }\n"),
    ("    state->player_health -= damage * (float)(int32_t)hit_player;\n",
     "    if (hit_player && damage > 0.0f) { g_dmg[1] += damage; g_hits[1]++; g_last_src = 1; g_last_type = projectile_type;"
     " g_last_sleep = state->is_sleeping; g_last_amt = damage; }\n"),
    ("    state->player_health = new_health;\n",
     "    if (new_health < state->player_health) { g_dmg[2] += state->player_health - new_health; g_last_src = 2;"
     " g_last_type = (state->player_food <= 0) * 1 + (state->player_drink <= 0) * 2"
     " + (state->player_energy <= 0 && !state->is_sleeping) * 4;"
     " g_last_sleep = state->is_sleeping; g_last_amt = state->player_health - new_health; }\n"),
    ("    state->player_health += (float)delta_health;\n",
     "    if (delta_health < 0) { g_dmg[3] += (float)-delta_health; g_last_src = 3; g_last_type = potion_index;"
     " g_last_sleep = 0; g_last_amt = (float)-delta_health; }\n"),
]


def instrument(src: str) -> str:
    for old, prefix in PATCHES:
        if src.count(old) != 1:
            raise SystemExit(f"engine changed: expected one match for {old.strip()!r}")
        src = src.replace(old, prefix + old)
    return HEADER + src


if __name__ == "__main__":
    Path(sys.argv[2]).write_text(instrument(Path(sys.argv[1]).read_text()))
