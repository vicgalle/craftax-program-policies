#ifndef FULL_PROGRAM_POLICY_H
#define FULL_PROGRAM_POLICY_H
#include <stdint.h>

/* Engine-independent, observation-only policy. Each floor has its own local
 * coordinate frame: 96 cells accommodate every displacement on a 48-cell map. */
enum { FP_SIDE=96, FP_CELLS=9216, FP_LEVELS=9, FP_VIEW=99, FP_SCALARS=51 };
enum {
 FP_INVALID,FP_BOUNDARY,FP_GRASS,FP_WATER,FP_STONE,FP_TREE,FP_WOOD,FP_PATH,
 FP_COAL,FP_IRON,FP_DIAMOND,FP_TABLE,FP_FURNACE,FP_SAND,FP_LAVA,FP_PLANT,
 FP_RIPE,FP_WALL,FP_DARKNESS,FP_MOSS,FP_STALAGMITE,FP_SAPPHIRE,FP_RUBY,
 FP_CHEST,FP_FOUNTAIN,FP_FIRE_GRASS,FP_ICE_GRASS,FP_GRAVEL,FP_FIRE_TREE,
 FP_ICE_SHRUB,FP_FIRE_TABLE,FP_ICE_TABLE,FP_BOSS,FP_GRAVE,FP_GRAVE2,
 FP_GRAVE3,FP_VULNERABLE
};
enum {
 FP_NOOP,FP_LEFT,FP_RIGHT,FP_UP,FP_DOWN,FP_DO,FP_SLEEP,FP_PLACE_STONE,
 FP_PLACE_TABLE,FP_PLACE_FURNACE,FP_PLACE_PLANT,FP_WOOD_PICK,FP_STONE_PICK,
 FP_IRON_PICK,FP_WOOD_SWORD,FP_STONE_SWORD,FP_IRON_SWORD,FP_REST,
 FP_DESCEND,FP_ASCEND,FP_DIAMOND_PICK,FP_DIAMOND_SWORD,FP_IRON_ARMOUR,
 FP_DIAMOND_ARMOUR,FP_SHOOT,FP_MAKE_ARROW,FP_FIREBALL,FP_ICEBALL,
 FP_PLACE_TORCH,FP_POTION_RED,FP_POTION_GREEN,FP_POTION_BLUE,FP_POTION_PINK,
 FP_POTION_CYAN,FP_POTION_YELLOW,FP_READ_BOOK,FP_ENCHANT_SWORD,
 FP_ENCHANT_ARMOUR,FP_MAKE_TORCH,FP_LEVEL_DEX,FP_LEVEL_STR,FP_LEVEL_INT,
 FP_ENCHANT_BOW
};
enum { FP_I_WOOD,FP_I_STONE,FP_I_COAL,FP_I_IRON,FP_I_DIAMOND,
 FP_I_SAPPHIRE,FP_I_RUBY,FP_I_SAPLING,FP_I_TORCH,FP_I_ARROW,FP_I_BOOK,
 FP_I_PICK,FP_I_SWORD,FP_INVENTORY };
enum { FP_EXPLORE,FP_GATHER,FP_CRAFT,FP_DRINK,FP_EAT,FP_RECOVER,FP_FIGHT,
 FP_FARM,FP_LIGHT,FP_TRAVEL,FP_LOOT,FP_MAGIC,FP_UPGRADE,FP_SKILL_COUNT };

/* Semantic commands are separate from the legacy transient interaction label.
 * A stage is a simultaneous conjunction; increasing stages form a sequence. */
enum { FP_GOAL_INVENTORY,FP_GOAL_EQUIPMENT,FP_GOAL_ADJACENT,FP_GOAL_FLOOR,
       FP_GOAL_RECOVERY,FP_GOAL_PREPARE };
enum { FP_EQ,FP_RANGE,FP_AT_LEAST };
enum { FP_E_PICK,FP_E_SWORD,FP_E_HELMET,FP_E_CHEST,FP_E_PANTS,FP_E_BOOTS };
enum { FP_MAX_GOALS=8,FP_COMMAND_VERSION=1 };
typedef struct { int kind,subject,relation,lo,hi,direction; } FPGoal;
typedef struct { int version,count,stages;FPGoal goals[FP_MAX_GOALS];int stage[FP_MAX_GOALS]; } FPCommand;
enum { FP_CMD_IDLE,FP_CMD_ACTIVE,FP_CMD_INTERRUPTED,FP_CMD_COMPLETE,FP_CMD_BLOCKED };
typedef struct {
 FPCommand command;
 FPGoal subgoal;
 int enabled,stage,active,status,claim,reference;
 int started,subgoal_started,transitions,interruptions,resumptions,was_interrupted;
 int bound_target,bound_level,blocked_ticks,replans;
 int executing,has_subgoal;
 uint64_t bound_mask;
} FPIntent;

typedef struct {
 uint8_t blocks[FP_VIEW],items[FP_VIEW],visible[FP_VIEW],mobs[FP_VIEW][5];
 int inv[FP_INVENTORY],potions[6],armour[4],armour_enchant[4];
 float health,light;
 int food,drink,energy,mana,xp,dex,str,intelligence,dir,sleeping,resting;
 int spells[2],sword_enchant,bow_enchant,bow,level,clear,boss_vulnerable;
} FPObservation;
typedef struct {
 int drink_at,eat_at,sleep_at,wood_stock,torch_stock,armour_pieces;
 int prepare_steps,mine_steps,str_first,retreat_health;
 float danger_cost,revisit_cost,frontier_bonus;
} FPConfig;
typedef struct {
 uint8_t map[FP_CELLS],items[FP_CELLS],stations[FP_CELLS],lit[FP_CELLS];
 uint16_t visits[FP_CELLS];
 int up,down,seen,clear,entered,steps,known_lo,known_hi;
} FPFloor;
typedef struct { int p,type,kind,cooldown;float health; } FPEnemy;
/* Persistent callable late expedition runtime; zero-filled legacy-bank migration. */
enum { FP_RG_EXPEDITION=1, FP_RG_EXCURSION=2 };
enum { FP_RO_COMPLETED=1, FP_RO_ABANDONED, FP_RO_DIED, FP_RO_HORIZON, FP_RO_STOPPED, FP_RO_INTERRUPTED };
typedef struct { int level,food,drink,energy;float health; } FPGoalPublic;
typedef struct {
 int kind,id,parent,frontier,entry_t,suspended,departed,return_outcome;
 int purpose,mode,target; /* child-local selector state */
 FPGoalPublic in;
} FPGoalFrame;
typedef struct { FPGoalFrame frame;FPGoalPublic out;int exit_t,outcome; } FPGoalEvent;
typedef struct {
 FPGoalFrame frame[2],last_return;
 FPGoalEvent event[4];FPGoalPublic observed;
 int depth,next_id,boundary_t,last_outcome,returns,completions,abandonments;
} FPGoalRuntime;
typedef struct {
 FPFloor floors[FP_LEVELS];
 FPObservation previous;
 uint8_t achieved[67];
 int r,c,observed_r,observed_c,level,t,previous_action,previous_block,previous_mob,previous_type,previous_target;
 int skill,goal,target,last_frontier,frontier_until,recovery_goal,recovery_width,recovery_height,recovering,drinking;
 int arrows[3],arrow_count,projectile_types[3],potion_effect[6];
 FPEnemy enemies[8];int enemy_count;
 int farm_attempts,plant_cell,plant_tick,resupplying,resupply_tick;
 /* Fountain chosen on floor 1, kept until drinking ends (-1: none). */
 int water_goal;
 /* Set when a resupply trip found no tree left on the overworld. */
 int wood_exhausted;
 /* Step of the last health loss on floor 2 with no adjacent enemy in view. */
 int unseen_hit;
 /* Visible player projectiles in the current floor's local frame. */
 int shot_pos[3],shot_type[3],shot_dir[3],shot_count;
 uint64_t rng;
 /* Count confirmed Necromancer interactions from public wave transitions. */
 int boss_hits;
 /* Consecutive steps spent waiting for an approaching troll, and steps
  * spent searching for an enchantment table on trips from the mines. */
 int troll_wait,vault_search,dungeon_wait,vault_wait;
 FPIntent intent;
 /* Appended for the Fire expedition (zero-filled on resume from banks):
  * steps waited at the Fire up ladder with no pigman within ten cells. */
 int fire_camp;
 /* Early stream: steps spent facing an approaching melee mob's arrival
  * cell on floors 1-4 before the Troll Mines (bounded wait). */
 int early_wait;
 /* Troll stream: doorway fight from a sealed Troll Mines room (cell+1 of the
  * opened wall and of our stand, 0 when not holding; step it was opened). */
 int troll_door,troll_door_stand,troll_door_t;
 /* Step of the last slimeball-sized health loss in the Troll Mines. */
 int troll_slime_t;
 /* Steps spent holding position, low on health, for an approaching troll. */
 int troll_hold;
 /* Fire stream (round 15). fire_prep: 0 until Fire is clear with its exit
  * known, then 1 on the gem trip back to the Troll Mines or 2 descending;
  * fire_prep_steps: steps on the gem trip; fire_dia: steps spent prospecting
  * the Troll Mines for diamonds and sapphires; fire_wait: steps waited on the
  * Fire up ladder for nearby hostiles to clear before a routine climb. */
 int fire_prep,fire_prep_steps,fire_dia,fire_wait;
 /* Icetrip stream (every excursion off Ice after the first Ice arrival):
  * Fire steps spent waiting for a bat this excursion, the destination
  * (0: Fire forage beside the Ice ladder, 5: Troll Mines), and the step
  * the excursion started. Zero-filled on resume from banks. */
 int icetrip_wait,icetrip_dest,icetrip_start;
 /* Steps spent on the Fire ladder waiting for a nearby pigman before going home. */
 int icetrip_guard;
 /* Steps spent lighting the Troll Mines for diamonds during this mines visit. */
 int icetrip_prospect;
 /* Shallow stream (Overworld, Dungeon, Gnomish Mines before the Sewers):
  * steps spent walking Overworld frontiers in search of unknown trees. */
 int shallow_search;
 /* Step at which the Overworld departure check last passed (+1; 0: never). */
 int shallow_go;
 FPGoalRuntime runtime;
 int deep_camp;
 int deep_fort,deep_fort_shape,deep_fort_door;
 int ice_dark_mask,ice_dark_mode,ice_dark_until;
 int troll_dark_mask,troll_dark_mode,troll_dark_until;
} FPAgent;
typedef struct {
 int dist[FP_CELLS],first[FP_CELLS],heap[FP_CELLS],pos[FP_CELLS],heap_size;
 uint8_t mobs[FP_CELLS],melee[FP_CELLS],ranged[FP_CELLS],passive[FP_CELLS];
 uint8_t arrow_next[FP_CELLS];
 float enemy_health[FP_CELLS];int cooldown[FP_CELLS];
 int prefix[FP_SIDE+1][FP_SIDE+1];
 int visible_cells[FP_VIEW],visible_count;
} FPScratch;
FPConfig fp_default_config(void);
void fp_reset(FPAgent *agent,uint64_t seed);
/* Arbitrary reachable observation, with no assumption about spawn location. */
void fp_reset_cold(FPAgent *agent,uint64_t seed,const FPObservation *obs);
void fp_decode_compact(FPObservation *obs,const uint8_t *record);
void fp_decode_float(FPObservation *obs,const float *record);
#define FP_RUNTIME_ABI 1
/* Observe exactly once, then inspect goal boundaries or choose a new child.
 * fp_decide commits exactly one action using that absorbed observation. */
void fp_observe(FPAgent *agent,const FPObservation *obs,float previous_reward,FPScratch *scratch);
int fp_decide(FPAgent *agent,const FPObservation *obs,const FPConfig *config,FPScratch *scratch);
int fp_goal_call_excursion(FPAgent *agent,int purpose,int mode,int target);
void fp_goal_return(FPAgent *agent,int outcome);
void fp_goal_finish(FPAgent *agent,int outcome);
int fp_act(FPAgent *agent,const FPObservation *obs,float previous_reward,
           const FPConfig *config,FPScratch *scratch);
const char *fp_skill_name(int skill);
/* Invalid commands leave the existing objective and world memory intact. */
int fp_validate_command(const FPCommand *command,char *error,int error_size);
int fp_parse_command(const char *text,FPCommand *command,char *error,int error_size);
int fp_set_command(FPAgent *agent,const FPCommand *command,char *error,int error_size);
void fp_clear_command(FPAgent *agent);
int fp_goal_satisfied(const FPGoal *goal,const FPObservation *obs);
/* Warm-history reconstruction: record an ACTUAL action without choosing one.
 * The policy seed, public prefix, and candidate configuration define memory. */
void fp_observe_action(FPAgent *agent,const FPObservation *obs,float previous_reward,
                       int actual_action,FPScratch *scratch);
void fp_replay_action(FPAgent *agent,const FPObservation *obs,float previous_reward,
                      int actual_action,const FPConfig *config,FPScratch *scratch);
#endif
