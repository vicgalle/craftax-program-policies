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
typedef struct {
 FPFloor floors[FP_LEVELS];
 FPObservation previous;
 uint8_t achieved[67];
 int r,c,observed_r,observed_c,level,t,previous_action,previous_block,previous_mob,previous_type,previous_target;
 int skill,goal,target,last_frontier,frontier_until,recovery_goal,recovery_width,recovery_height,recovering,drinking;
 int arrows[3],arrow_count,projectile_types[3],potion_effect[6];
 FPEnemy enemies[8];int enemy_count;
 int farm_attempts,plant_cell,plant_tick,resupplying,resupply_tick;
 uint64_t rng;
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
void fp_decode_compact(FPObservation *obs,const uint8_t *record);
void fp_decode_float(FPObservation *obs,const float *record);
int fp_act(FPAgent *agent,const FPObservation *obs,float previous_reward,
           const FPConfig *config,FPScratch *scratch);
const char *fp_skill_name(int skill);
#endif
