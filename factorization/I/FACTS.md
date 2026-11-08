# Floor literals left in I, and why

Every remaining `level==N` / `floors[N]` in the part of I reachable from its
entry points is listed here with the game fact it encodes. Strategy does not
belong in I. Where the machine needs a strategy decision it asks the program
(queries, `exports/core.txt`).

## World model and machine (integrator: full_wm.inc, full_policy.cpp)

| Function | Literal | Game fact |
|---|---|---|
| `wm_observe` | `p->level==8 && o->level==8 && boss_vulnerable...` | The Necromancer and its vulnerability waves exist only on floor 8. |
| `wm_observe` | `lit_floor = level 0,1,3,4,6` | The Overworld, Dungeon, Sewers, Vaults and Fire are lit. The Gnomish Mines (2), Troll Mines (5), Ice (7) and Graveyard (8) are dark, and only torches light them. |
| `wm_observe` | `level==4 \|\| level==5` | Projectile-heading model: on floors 4 and 5 the ranged mobs' shots fly away from the mob that fired them. |
| `wm_observe` | `level==0`, `floors[0]` (×2) | Saplings, plants and ripening (600 steps) exist only on the Overworld. |
| `wm_reset` | `floors[0]` (×2) | Episodes start on the Overworld, a 48×48 map placed at the atlas centre. |
| `wm_reset_cold` | `floors[0]` (×2) | A cold start forgets the assumed Overworld boundary. |
| `wm_troll_scope_enabled` | `floors[5].seen \|\| level>=5` | Troll identity tracking is needed from the Troll Mines on: trolls first spawn on floor 5. |
| `wm_attack_damage`, `wm_projectile_hit` | `level==8 ? 1.5f : 1` | Damage taken on the Graveyard is multiplied by 1.5. |
| `fire_table_known` | `floors[4]` | The fire enchantment table spawns in the Vaults (floor 4). |
| `known_trees` | `floors[0]` | Trees (wood) grow on the Overworld. |

## Area files (after level 2)

| Function | Literal | Game fact |
|---|---|---|
| `rm_observe` (m_resource.inc) | `o->level==8` | During the Graveyard boss fight the engine stops the food, drink and energy decrements. |
| `bm_observe` (full_boss_model.inc) | `o->level!=8` | The Necromancer and its waves exist only on floor 8; the boss tracker records waves and arena exploration only there. |
| `nav_floor_hit` (m_nav.inc) | table `{1,3,2,4,5,6,7,8}`, `L<8` (not counted by the census) | Which melee mob type lives on each floor, for the damage of the next floor. |
| `forage`, `rm_meal_wait` (not counted) | animal health 3/4/6; no animals on Ice or the Graveyard | Engine constants. |

The shield's simulator (`full_tactics.inc`) has no floor literals. Its fighter
model switch for floors 5 and up is a `late` argument that the program passes.

Strategy constants in I's C ABI: `fp_default_config()` returns the policy's
`FPConfig`:

- `drink_at`, `eat_at` and `sleep_at`;
- stock targets;
- preparation budgets;
- the retreat health;
- path cost weights.

The C ABI requires these defaults to be defined in I. They hold no floor
literals. The rules read them as `cfg.*`.
