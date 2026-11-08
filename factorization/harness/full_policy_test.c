#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "../../engine/craftax_full.c"
#include "full_policy.h"
#include <assert.h>

static FPAgent a,b;
static FPScratch scratch;
static CraftaxState state;
static void lazy_worlds(void) {
 Craftax env={0};env.state=&state;env.seed=42;
 craftax_set_lazy_floors(0);craftax_reset_state_from_seed(&env);CraftaxState eager=state;
 craftax_set_lazy_floors(1);craftax_reset_state_from_seed(&env);
 assert(!memcmp(eager.map[0],state.map[0],sizeof(state.map[0])));
 for(int l=1;l<9;l++)craftax_ensure_floor_generated(&state,l);
 assert(!memcmp(eager.map,state.map,sizeof(state.map)));
 assert(!memcmp(eager.item_map,state.item_map,sizeof(state.item_map)));
 assert(!memcmp(eager.light_map,state.light_map,sizeof(state.light_map)));
 assert(!memcmp(eager.up_ladders,state.up_ladders,sizeof(state.up_ladders)));
 assert(!memcmp(eager.down_ladders,state.down_ladders,sizeof(state.down_ladders)));
}
static void empty_world(void) {
 Craftax env={0};env.state=&state;env.seed=42;craftax_set_lazy_floors(0);
 craftax_reset_state_from_seed(&env);
 for(int l=0;l<9;l++) {
  memset(state.map[l],FP_PATH,sizeof(state.map[l]));
  memset(state.light_map[l],255,sizeof(state.light_map[l]));
  memset(state.item_map[l],0,sizeof(state.item_map[l]));
 }
 craftax_refresh_spawn_bits_all(&state);fp_reset(&a,17);
}
static FPObservation observe(void) {
 uint8_t record[996];FPObservation obs;craftax_encode_native_observation(&state,record);fp_decode_compact(&obs,record);return obs;
}
static void equipped(void) {
 state.inventory.wood=40;state.inventory.stone=20;state.inventory.torches=20;
 state.inventory.arrows=10;state.inventory.pickaxe=4;state.inventory.sword=4;
 for(int i=0;i<4;i++)state.inventory.armour[i]=2;
 memset(a.achieved,1,sizeof(a.achieved));
}
static void codecs(void) {
 empty_world();equipped();state.inventory.iron=17;state.inventory.potions[2]=9;
 state.inventory.sapphire=7;state.inventory.books=2;state.player_health=6.25f;
 state.sword_enchantment=2;state.learned_spells[1]=true;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=6;
 state.melee_mobs.position[0][0][0]=23;state.melee_mobs.position[0][0][1]=24;
 state.light_map[0][24][25]=0;
 float record[843];craftax_encode_reset_observation((CraftaxWorldState*)(void*)&state,record);
 FPObservation x=observe(),y;fp_decode_float(&y,record);
 assert(!memcmp(&x,&y,sizeof(x)));
 assert(x.inv[FP_I_IRON]==17 && x.inv[FP_I_SAPPHIRE]==7 && x.potions[2]==9);
 assert(x.health==6.25f && x.mobs[38][0]==7 && !x.visible[50]);
 a.achieved[26]=a.achieved[27]=0;FPConfig cfg=fp_default_config();
 (void)fp_act(&a,&x,0,&cfg,&scratch);assert(!a.achieved[26] && a.achieved[27]);
}
static void observation_boundary(void) {
 empty_world();CraftaxState copy=state;FPObservation x=observe();
 state.map[7][1][1]=FP_DIAMOND;state.map[0][0][0]=FP_RUBY;
 state.potion_mapping[0]^=1;state.state_rng[0]^=123;state.achievements[49]=true;
 FPObservation y=observe();assert(!memcmp(&x,&y,sizeof(x)));
 fp_reset(&a,99);fp_reset(&b,99);FPConfig cfg=fp_default_config();
 assert(fp_act(&a,&x,0,&cfg,&scratch)==fp_act(&b,&y,0,&cfg,&scratch));
 assert(!memcmp(&a,&b,sizeof(a)));state=copy;
}
static void remove_encounters(void) {
 memset(&state.melee_mobs,0,sizeof(state.melee_mobs));memset(&state.ranged_mobs,0,sizeof(state.ranged_mobs));
 memset(&state.passive_mobs,0,sizeof(state.passive_mobs));memset(&state.mob_projectiles,0,sizeof(state.mob_projectiles));
 memset(&state.player_projectiles,0,sizeof(state.player_projectiles));memset(state.mob_bits,0,sizeof(state.mob_bits));
}
static void crafting(void) {
 empty_world();state.inventory.wood=40;state.inventory.stone=30;
 state.inventory.coal=30;state.inventory.iron=30;state.inventory.diamond=20;
 a.achieved[3]=1;FPConfig cfg=fp_default_config();float reward=0;int complete=0;
 for(int t=0;t<200;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  assert(a.r==state.player_position[0]+24 && a.c==state.player_position[1]+24);
  /* Isolate the recipe graph; production benchmarks keep all encounters. */
  remove_encounters();state.player_food=state.player_drink=state.player_energy=9;state.player_health=9;
  int ar=0;for(int i=0;i<4;i++)ar+=state.inventory.armour[i];
  complete=state.inventory.pickaxe==4 && state.inventory.sword==4 && ar==8 && state.achievements[26];
  if(complete)break;
 }
 assert(complete && state.achievements[1] && state.achievements[16] && state.achievements[27]);
}
static void workstation_and_survival(void) {
 empty_world();equipped();state.inventory.pickaxe=state.inventory.sword=2;
 state.inventory.coal=state.inventory.iron=4;state.player_direction=FP_UP;
 state.map[0][23][24]=FP_TABLE;state.map[0][24][23]=FP_TREE;
 state.map[0][24][25]=FP_STONE;craftax_refresh_spawn_bits_all(&state);
 FPConfig cfg=fp_default_config();float reward=0;
 for(int t=0;t<12 && (state.inventory.pickaxe<3 || state.inventory.sword<3);t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  remove_encounters();state.player_food=state.player_drink=state.player_energy=9;state.player_health=9;
 }
 /* Moving down leaves this table's reach. Clear a side cell instead of
  * walking out and immediately back on every furnace-placement attempt. */
 assert(state.achievements[16] && state.inventory.pickaxe>=3 && state.inventory.sword>=3);
 assert(state.map[0][23][24]==FP_TABLE);

 empty_world();equipped();state.player_food=5;state.player_direction=FP_RIGHT;
 state.map[0][24][20]=FP_RIPE;
 state.passive_mobs.mask[0][0]=true;state.passive_mobs.type_id[0][0]=0;state.passive_mobs.health[0][0]=3;
 state.passive_mobs.position[0][0][0]=24;state.passive_mobs.position[0][0][1]=25;
 state.mob_bits[0][24]|=1ULL<<25;craftax_refresh_spawn_bits_all(&state);
 FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_DO); /* Eat the adjacent cow before walking to a crop. */
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(1));assert(state.player_food==9);

 empty_world();equipped();state.player_food=5;state.player_direction=FP_LEFT;
 state.map[0][24][23]=FP_RIPE;
 state.passive_mobs.mask[0][0]=true;state.passive_mobs.type_id[0][0]=0;state.passive_mobs.health[0][0]=3;
 state.passive_mobs.position[0][0][0]=24;state.passive_mobs.position[0][0][1]=28;
 state.mob_bits[0][24]|=1ULL<<28;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DO);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(2));assert(state.player_food==9 && state.achievements[11]);

 empty_world();state.player_health=2;state.inventory.sword=3;state.player_direction=FP_RIGHT;
 state.inventory.potions[0]=1;state.potion_mapping[0]=0;a.potion_effect[0]=0;
 memset(state.map[0],FP_WALL,sizeof(state.map[0]));state.map[0][24][24]=state.map[0][24][25]=FP_PATH;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=2;state.melee_mobs.health[0][0]=9;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][24]|=1ULL<<25;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_POTION_RED);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(3));assert(state.player_health>2);

 empty_world();state.player_energy=0;state.player_direction=FP_LEFT;
 memset(state.map[0],FP_STONE,sizeof(state.map[0]));
 for(int r=24;r<26;r++)for(int c=24;c<26;c++)state.map[0][r][c]=FP_PATH;
 state.map[0][24][26]=FP_PATH;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_SLEEP); /* A missing pickaxe cannot gather shelter stone. */
}
static void transitions(void) {
 empty_world();equipped();FPConfig cfg=fp_default_config();
 state.item_map[0][24][24]=2;state.down_ladders[0][0]=24;state.down_ladders[0][1]=24;
 state.up_ladders[1][0]=7;state.up_ladders[1][1]=19;state.item_map[1][7][19]=3;
 FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DESCEND);
 float reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(1));
 assert(state.player_level==1 && state.player_position[0]==7 && state.player_position[1]==19);
 obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.level==1 && a.observed_r==48 && a.observed_c==48 && action==FP_LEVEL_STR);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(2));
 state.inventory.wood=0;obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action==FP_ASCEND);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(3));
 obs=observe();(void)fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.level==0 && a.observed_r==48 && a.observed_c==48 && state.player_position[0]==24);
}
static void closed_shelter(void) {
 empty_world();memset(state.map[0],FP_STONE,sizeof(state.map[0]));
 state.map[0][24][24]=state.map[0][24][25]=state.map[0][24][26]=FP_PATH;
 craftax_refresh_spawn_bits_all(&state);state.player_health=2;state.player_energy=3;
 state.inventory.pickaxe=3;state.inventory.sword=3;state.inventory.stone=4;
 state.player_direction=FP_LEFT;
 FPConfig cfg=fp_default_config();float reward=0;int sealed=0,healed=0;
 for(int t=0;t<200;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  sealed|=state.map[0][24][26]==FP_STONE;
  assert(state.player_health>=2);
  if(state.player_health>=9 && state.player_energy==9 && !state.is_sleeping && !state.is_resting){healed=1;break;}
 }
 assert(sealed && healed);
}
static void recovery_water_access(void) {
 /* Both ends of this corridor can be sealed with one stone. The farther
  * room has a fountain in its wall, avoiding long hydration trips while
  * recovering. The policy sees it through the ordinary local view. */
 empty_world();equipped();memset(state.map[0],FP_WALL,sizeof(state.map[0]));
 for(int c=24;c<=28;c++)state.map[0][24][c]=FP_PATH;
 state.map[0][23][28]=FP_FOUNTAIN;state.player_direction=FP_RIGHT;
 state.player_health=2;state.player_energy=3;state.player_drink=4;a.recovering=1;
 craftax_refresh_spawn_bits_all(&state);
 FPConfig cfg=fp_default_config();FPObservation obs=observe();
 int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_RIGHT && a.recovery_goal==48*FP_SIDE+51);
 float reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(1));
 int healed=0;
 for(int t=0;t<250;t++) {
  remove_encounters();obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t+2));
  assert(state.player_health>=2);
  assert(a.r==state.player_position[0]+24 && a.c==state.player_position[1]+24);
  if(state.player_health>=9 && state.player_energy==9 && !state.is_sleeping){healed=1;break;}
 }
 assert(healed && state.achievements[4] && state.map[0][23][28]==FP_FOUNTAIN);

}
static void combat(void) {
 empty_world();state.inventory.sword=3;state.player_direction=FP_RIGHT;
 state.player_health=2;state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=0;
 state.melee_mobs.health[0][0]=5;state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][24]|=1ULL<<25;
 FPConfig cfg=fp_default_config();FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_DO);(void)craftax_gameplay_step_native(&state,action,craftax_prng_key(1));
 assert(state.achievements[8] && state.player_health==2);
 empty_world();state.player_health=3;state.inventory.sword=1;state.player_direction=FP_RIGHT;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=2;state.melee_mobs.health[0][0]=9;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;state.mob_bits[0][24]|=1ULL<<25;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action!=FP_DO && action!=FP_RIGHT);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(2));assert(state.player_health==3);
 empty_world();state.player_health=3;state.player_strength=3;state.inventory.sword=4;state.player_direction=FP_RIGHT;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=4;state.melee_mobs.health[0][0]=12;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;state.mob_bits[0][24]|=1ULL<<25;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action!=FP_DO && action!=FP_RIGHT);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(3));assert(state.player_health==3);
}
static void stationary_melee_cooldowns(void) {
 /* Two approaching Pigmen retain their initial cooldown while standing
  * next to the player. Two iceballs leave the first at two HP just as both
  * become ready. Inventing a new cooldown after the second cast would make
  * a third cast concede two fatal attacks before its projectile resolves. */
 empty_world();equipped();remove_encounters();
 state.player_level=8;state.player_strength=state.player_intelligence=5;
 state.player_health=13;state.player_mana=21;state.player_direction=FP_UP;
 state.learned_spells[1]=true;state.sword_enchantment=1;
 state.boss_progress=6;state.boss_timesteps_to_spawn_this_round=0;a.boss_hits=6;
 for(int i=0;i<4;i++){state.inventory.armour[i]=1;state.armour_enchantments[i]=2;}
 memset(state.map[8],FP_WALL,sizeof(state.map[8]));
 for(int r=22;r<=27;r++)state.map[8][r][24]=FP_PATH;
 state.map[8][24][23]=FP_PATH;
 for(int i=0;i<2;i++) {
  int r=i?27:22;
  state.melee_mobs.mask[8][i]=true;state.melee_mobs.type_id[8][i]=6;
  state.melee_mobs.health[8][i]=20;state.melee_mobs.attack_cooldown[8][i]=2;
  state.melee_mobs.position[8][i][0]=r;state.melee_mobs.position[8][i][1]=24;
  state.mob_bits[8][r]|=1ULL<<24;
 }
 craftax_refresh_spawn_bits_all(&state);FPConfig cfg=fp_default_config();float reward=0;
 for(int t=0;t<2;t++) {
  /* Replay the setup actions: this tests cooldown inference even when a
   * boss-floor safety shield would choose a different opening action. */
  FPObservation obs=observe();int action=FP_ICEBALL;
  fp_observe_action(&a,&obs,reward,action,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(900+t));
  assert(state.player_health==13);
 }
 assert(state.melee_mobs.health[8][0]==2);
 assert(state.melee_mobs.attack_cooldown[8][0]==0 && state.melee_mobs.attack_cooldown[8][1]==0);
 FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.enemy_count==2);
 for(int i=0;i<a.enemy_count;i++)assert(a.enemies[i].cooldown<=0);
 assert(action==FP_LEFT);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(902));
 assert(state.player_health==13);
 /* A real observed hit still starts the inferred five-turn cooldown. */
 empty_world();equipped();remove_encounters();
 state.player_level=8;state.player_strength=state.player_intelligence=5;
 state.player_health=13;state.player_mana=21;state.player_direction=FP_RIGHT;
 state.learned_spells[1]=true;state.sword_enchantment=1;
 state.boss_timesteps_to_spawn_this_round=0;
 for(int i=0;i<4;i++){state.inventory.armour[i]=1;state.armour_enchantments[i]=2;}
 state.melee_mobs.mask[8][0]=true;state.melee_mobs.type_id[8][0]=6;
 state.melee_mobs.health[8][0]=20;state.melee_mobs.position[8][0][0]=24;
 state.melee_mobs.position[8][0][1]=25;state.mob_bits[8][24]|=1ULL<<25;
 craftax_refresh_spawn_bits_all(&state);obs=observe();
 action=FP_ICEBALL;fp_observe_action(&a,&obs,0,action,&scratch);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(903));
 assert(state.player_health<13 && state.player_health>0);
 obs=observe();(void)fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.enemy_count==1 && a.enemies[0].cooldown==5);
}
static void navigation_and_replacements(void) {
 empty_world();equipped();state.inventory.pickaxe=3;state.inventory.sword=3;
 state.inventory.diamond=1;state.player_direction=FP_UP;
 memset(state.map[0],FP_STONE,sizeof(state.map[0]));state.map[0][24][24]=FP_PATH;
 state.map[0][23][25]=FP_DIAMOND;craftax_refresh_spawn_bits_all(&state);
 FPConfig cfg=fp_default_config();float reward=0;
 for(int t=0;t<12 && state.inventory.diamond<2;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  remove_encounters();state.player_food=state.player_drink=state.player_energy=9;state.player_health=9;
 }
 /* Two equal routes to a diagonal ore must mine a passage, even though
  * turning changes the facing that will be needed at the destination. */
 assert(state.inventory.diamond>=2);

 empty_world();equipped();state.player_health=2;state.player_drink=0;state.inventory.sword=1;
 memset(state.inventory.armour,0,sizeof(state.inventory.armour));
 memset(state.map[0],FP_WALL,sizeof(state.map[0]));
 for(int c=24;c<=26;c++)state.map[0][24][c]=FP_PATH;
 state.map[0][24][27]=FP_WATER;state.map[0][23][25]=FP_PATH;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=0;state.melee_mobs.health[0][0]=5;
 state.melee_mobs.position[0][0][0]=23;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][23]|=1ULL<<25;craftax_refresh_spawn_bits_all(&state);
 FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_NOOP); /* Drinking cannot walk into a lethal attack. */
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(1));assert(state.player_health==2);

 empty_world();state.inventory.sword=3;state.player_direction=FP_RIGHT;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=0;state.melee_mobs.health[0][0]=5;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][24]|=1ULL<<25;obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DO);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(2));remove_encounters();
 state.player_health=2;state.inventory.sword=1;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=0;state.melee_mobs.health[0][0]=5;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][24]|=1ULL<<25;obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.enemy_count==1 && a.enemies[0].health==5 && action!=FP_DO && action!=FP_RIGHT);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(3));assert(state.player_health==2);

 empty_world();equipped();state.player_health=5.5f;state.player_direction=FP_RIGHT;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=6;state.melee_mobs.health[0][0]=20;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][24]|=1ULL<<25;obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action!=FP_DO && action!=FP_RIGHT); /* Physical armour does not stop fire damage. */
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(4));assert(state.player_health==5.5f);
}
static void arrow_dodge(void) {
 empty_world();state.mob_projectiles.mask[0][0]=true;state.mob_projectiles.type_id[0][0]=0;
 state.mob_projectiles.position[0][0][0]=24;state.mob_projectiles.position[0][0][1]=23;
 state.mob_projectile_directions[0][0][0]=0;state.mob_projectile_directions[0][0][1]=1;
 a.arrow_count=1;a.arrows[0]=48*96+46;a.projectile_types[0]=1;
 FPConfig cfg=fp_default_config();FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action>=FP_LEFT && action<=FP_DOWN && action!=FP_LEFT);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(1));assert(state.player_health==9);
}
static void approaching_melee(void) {
 /* The only route to water steps into a zombie's attack range. Waiting
  * lets it enter the corridor first; a facing sword strike then kills it
  * without the free hit that moving towards it would have conceded. */
 empty_world();equipped();state.inventory.sword=3;state.player_direction=FP_RIGHT;
 state.player_drink=0;
 memset(state.map[0],FP_WALL,sizeof(state.map[0]));
 for(int c=24;c<=26;c++)state.map[0][24][c]=FP_PATH;
 state.map[0][24][27]=FP_WATER;state.map[0][23][25]=FP_PATH;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=0;
 state.melee_mobs.health[0][0]=5;
 state.melee_mobs.position[0][0][0]=23;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][23]|=1ULL<<25;craftax_refresh_spawn_bits_all(&state);
 FPConfig cfg=fp_default_config();float reward=0;int killed=0;
 for(int t=0;t<12;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  if(!t)assert(action==FP_NOOP && a.skill==FP_FIGHT);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  assert(state.player_health==9);
  assert(a.r==state.player_position[0]+24 && a.c==state.player_position[1]+24);
  if(state.achievements[8]){killed=1;break;}
 }
 assert(killed);

 /* Waiting is also useful before an enemy that needs two sword strikes. */
 empty_world();equipped();state.inventory.sword=2;state.player_direction=FP_RIGHT;
 state.player_drink=0;memset(state.inventory.armour,0,sizeof(state.inventory.armour));
 memset(state.map[0],FP_WALL,sizeof(state.map[0]));
 for(int c=24;c<=26;c++)state.map[0][24][c]=FP_PATH;
 state.map[0][24][27]=FP_WATER;state.map[0][23][25]=FP_PATH;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=0;
 state.melee_mobs.health[0][0]=5;
 state.melee_mobs.position[0][0][0]=23;state.melee_mobs.position[0][0][1]=25;
 state.mob_bits[0][23]|=1ULL<<25;craftax_refresh_spawn_bits_all(&state);
 FPObservation obs=observe();assert(fp_act(&a,&obs,0,&cfg,&scratch)==FP_NOOP);
}
static void projectile_health_tracking(void) {
 /* A wounded orc is safe to finish by sword even at low player health.
  * Exercise immediate arrows, an arrow already in flight, and a spell;
  * compare inferred HP against the actual engine after each observation. */
 FPConfig cfg=fp_default_config();
 for(int mode=0;mode<3;mode++) {
  empty_world();equipped();state.player_health=2.8f;state.player_strength=2;
  state.inventory.sword=3;memset(state.inventory.armour,0,sizeof(state.inventory.armour));
  state.player_direction=FP_RIGHT;state.inventory.bow=mode==2?0:1;
  state.inventory.arrows=1;state.learned_spells[0]=mode==2;
  memset(state.map[0],FP_WALL,sizeof(state.map[0]));
  for(int c=23;c<=27;c++)state.map[0][24][c]=FP_PATH;
  state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=2;
  state.melee_mobs.health[0][0]=9;state.melee_mobs.position[0][0][0]=24;
  state.melee_mobs.position[0][0][1]=mode==1?27:26;
  state.mob_bits[0][24]|=1ULL<<state.melee_mobs.position[0][0][1];
  craftax_refresh_spawn_bits_all(&state);float reward=0;int killed=0,sword_finish=0,tracked_hit=0;
  for(int t=0;t<16;t++) {
   FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
   if(!t)assert(action==(mode==2?FP_FIREBALL:FP_SHOOT));
   if(state.melee_mobs.mask[0][0] && a.enemy_count)
    assert(fabsf(a.enemies[0].health-state.melee_mobs.health[0][0])<0.001f);
   if(a.enemy_count && a.enemies[0].health<9)tracked_hit=1;
   reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
   assert(state.player_health>=2.8f);
   if(state.achievements[38]){killed=1;sword_finish=action==FP_DO;break;}
  }
  assert(tracked_hit && (mode==1 || (killed && sword_finish)));
 }
}
static void projectile_replacement(void) {
 /* If several shots could have killed the old occupant, none of their
  * remaining damage should be assigned to a freshly spawned enemy. */
 empty_world();equipped();state.player_health=2.8f;state.player_strength=2;
 state.inventory.sword=3;memset(state.inventory.armour,0,sizeof(state.inventory.armour));
 state.player_direction=FP_RIGHT;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=2;
 state.melee_mobs.health[0][0]=9;state.melee_mobs.position[0][0][0]=24;
 state.melee_mobs.position[0][0][1]=25;state.mob_bits[0][24]|=1ULL<<25;
 a.t=1;a.observed_r=a.observed_c=48;
 a.enemy_count=1;a.enemies[0]=(FPEnemy){48*FP_SIDE+49,3,0,0,4};
 a.shot_count=2;a.shot_pos[0]=48*FP_SIDE+48;a.shot_pos[1]=48*FP_SIDE+50;
 a.shot_dir[0]=FP_RIGHT;a.shot_dir[1]=FP_LEFT;a.shot_type[0]=a.shot_type[1]=5;
 FPConfig cfg=fp_default_config();FPObservation obs=observe();
 int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(a.enemies[0].health==9 && action!=FP_DO && action!=FP_RIGHT);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(7));
 assert(state.player_health==2.8f);
}
static void magic_and_potions(void) {
 empty_world();equipped();state.inventory.books=2;a.achieved[56]=a.achieved[58]=0;
 FPConfig cfg=fp_default_config();float reward=0;
 for(int t=0;t<6;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  remove_encounters();if(state.achievements[56] && state.achievements[58])break;
 }
 assert(state.learned_spells[0] && state.learned_spells[1] && state.achievements[56] && state.achievements[58]);
 empty_world();equipped();state.player_health=7;state.inventory.potions[0]=2;state.potion_mapping[0]=0;a.achieved[62]=0;
 FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_POTION_RED);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(1));remove_encounters();
 obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(a.potion_effect[0]==0);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(2));remove_encounters();state.player_health=5;
 obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action==FP_POTION_RED);
 empty_world();equipped();state.inventory.sapphire=4;state.player_direction=FP_RIGHT;a.achieved[64]=0;
 state.map[0][24][25]=FP_ICE_TABLE;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_ENCHANT_SWORD);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(1));remove_encounters();state.player_mana=9;
 obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action==FP_ENCHANT_ARMOUR);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(2));assert(state.achievements[63] && state.achievements[64]);
 empty_world();equipped();state.player_direction=FP_RIGHT;state.learned_spells[1]=true;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=1;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=6;state.melee_mobs.health[0][0]=20;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=25;state.mob_bits[0][24]|=1ULL<<25;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_ICEBALL);
 empty_world();equipped();state.player_direction=FP_RIGHT;state.player_intelligence=3;
 state.inventory.bow=1;state.learned_spells[0]=true;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=4;state.melee_mobs.health[0][0]=12;
 state.melee_mobs.position[0][0][0]=24;state.melee_mobs.position[0][0][1]=27;state.mob_bits[0][24]|=1ULL<<27;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_FIREBALL);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(3));
 assert(state.achievements[56] && state.player_mana==7);
}
static void lighting_and_boss(void) {
 empty_world();equipped();state.player_level=2;memset(state.light_map[2],0,sizeof(state.light_map[2]));
 state.item_map[2][24][24]=3;
 for(int r=-4;r<=4;r++)for(int c=-4;c<=4;c++)if(r*r+c*c<=22)state.light_map[2][24+r][24+c]=255;
 FPConfig cfg=fp_default_config();float reward=0;int lit=0;
 for(int t=0;t<30;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));remove_encounters();
  if(state.achievements[24]){lit=1;break;}
 }
 assert(lit);
 empty_world();equipped();state.player_level=8;state.player_direction=FP_RIGHT;state.map[8][24][25]=FP_BOSS;
 for(int t=0;t<8;t++) {
  state.boss_timesteps_to_spawn_this_round=0;remove_encounters();FPObservation obs=observe();
  int action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DO);
  /* Isolate the interaction after a cleared wave; encounter tactics are
   * tested separately and production evaluations retain every wave. */
  craftax_do_action_native(&state,action,craftax_prng_key((uint32_t)t));craftax_boss_logic_native(&state);
 }
 assert(state.achievements[48] && state.achievements[49] && state.boss_progress==8);
}
static void graveyard_recovery_and_completion(void) {
 FPConfig cfg=fp_default_config();float reward=0;FPObservation obs;int action;
 empty_world();equipped();state.player_level=8;state.achievements[35]=true;remove_encounters();
 state.player_direction=FP_RIGHT;state.map[8][24][25]=FP_BOSS;
 state.boss_timesteps_to_spawn_this_round=0;
 state.player_food=state.player_drink=2;state.player_health=3;
 state.player_mana=craftax_step_get_max_mana(&state);
 int healed=0;
 for(int t=0;t<200;t++) {
  obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
  if(action==FP_DO){healed=1;break;}
  assert(action==FP_NOOP && state.boss_progress==0);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(500+t));
  assert(state.player_food==2 && state.player_drink==2);
 }
 assert(healed && state.player_health==craftax_step_get_max_health(&state));
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(701));
 obs=observe();(void)fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.boss_hits==1 && a.achieved[48]);
 /* The eighth confirmed hit marks completion. The engine can still have
  * a final spawn window; after that wave the policy must not hit again. */
 a.boss_hits=7;state.boss_progress=7;state.boss_timesteps_to_spawn_this_round=0;
 state.player_position[0]=24;state.player_position[1]=24;state.player_direction=FP_RIGHT;
 a.r=a.observed_r=48;a.c=a.observed_c=48;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DO);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(702));
 obs=observe();(void)fp_act(&a,&obs,reward,&cfg,&scratch);
 assert(a.boss_hits==8 && a.achieved[49] && state.achievements[49]);
 state.boss_timesteps_to_spawn_this_round=0;remove_encounters();
 for(int t=0;t<40;t++) {
  obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_NOOP);
  (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(703+t));
  assert(state.boss_progress==8);
 }
 /* A failed or interrupted interaction does not count as a boss hit. */
 empty_world();equipped();state.player_level=8;state.achievements[35]=true;remove_encounters();
 state.boss_timesteps_to_spawn_this_round=0;obs=observe();
 (void)fp_act(&a,&obs,0,&cfg,&scratch);
 a.previous_action=FP_DO;a.previous_block=FP_BOSS;a.previous.boss_vulnerable=1;
 a.previous.sleeping=1;state.boss_timesteps_to_spawn_this_round=6;
 obs=observe();(void)fp_act(&a,&obs,0,&cfg,&scratch);assert(a.boss_hits==0);
}
static void graveyard_torch_facing(void) {
 FPConfig cfg=fp_default_config();float reward=0;
 empty_world();equipped();state.player_level=8;state.achievements[35]=true;remove_encounters();
 state.boss_timesteps_to_spawn_this_round=0;
 state.player_direction=FP_LEFT;state.player_mana=craftax_step_get_max_mana(&state);
 memset(state.map[8],FP_WALL,sizeof(state.map[8]));
 memset(state.light_map[8],0,sizeof(state.light_map[8]));
 for(int c=23;c<=29;c++)state.map[8][24][c]=FP_PATH;
 for(int c=23;c<=25;c++)state.light_map[8][24][c]=255;
 state.item_map[8][24][23]=1;state.item_map[8][24][24]=3;
 craftax_refresh_spawn_bits_all(&state);
 const int expected[3]={FP_LEFT,FP_RIGHT,FP_PLACE_TORCH};
 for(int t=0;t<3;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  assert(action==expected[t]);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(800+t));
 }
 assert(state.inventory.torches==19 && state.light_map[8][24][26]>12);
 /* A nearby table lets the last carried wood and coal make four torches. */
 empty_world();equipped();state.player_level=8;state.achievements[35]=true;remove_encounters();
 state.boss_timesteps_to_spawn_this_round=0;
 state.player_mana=craftax_step_get_max_mana(&state);
 state.map[8][24][25]=FP_TABLE;
 state.inventory.torches=0;state.inventory.wood=state.inventory.coal=1;
 FPObservation obs=observe();int action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_MAKE_TORCH);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(803));
 assert(state.inventory.torches==4 && state.inventory.wood==0 && state.inventory.coal==0);
}
static void elemental_expedition(void) {
 FPConfig cfg=fp_default_config();FPObservation obs;int action;
 /* Bring the Troll Mines' rubies back after clearance, while preserving
  * the recovery interrupt for an expedition with six health remaining. */
 for(int mode=0;mode<4;mode++) {
  empty_world();equipped();state.player_level=5;state.player_strength=4;
  state.player_intelligence=3;state.player_health=mode==1?6:12;
  state.monsters_killed[5]=mode==2?0:8;state.inventory.ruby=4;
  state.sword_enchantment=2;state.item_map[5][24][24]=3;
  a.floors[5].steps=4000; /* Prospecting is complete; test the ruby-return contract. */
  state.item_map[5][24][26]=2; /* The cleared floor exit is publicly known. */
  if(mode==3)for(int i=0;i<4;i++)state.armour_enchantments[i]=1;
  obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
  assert((action==FP_ASCEND)==(mode==0));
 }
 /* The enchantment achievement records only the first piece. Complete
  * the other pieces too, including replacing an existing ice enchantment. */
 empty_world();equipped();state.player_level=4;state.player_strength=4;
 state.player_intelligence=3;state.player_health=12;state.player_mana=15;
 state.player_direction=FP_LEFT;state.sword_enchantment=2;
 state.inventory.ruby=4;state.armour_enchantments[0]=2;a.floors[5].seen=1;
 state.achievements[31]=true; /* This fixture has already visited the Vault. */
 state.map[4][24][23]=FP_FIRE_TABLE;craftax_refresh_spawn_bits_all(&state);
 float reward=0;
 for(int t=0;t<4;t++) {
  obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
  assert(action==FP_ENCHANT_ARMOUR && a.skill==FP_MAGIC);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  remove_encounters();state.player_mana=15;
 }
 for(int i=0;i<4;i++)assert(state.armour_enchantments[i]==1);
 assert(state.inventory.ruby==0);

 empty_world();equipped();state.player_level=6;state.player_strength=4;
 state.player_intelligence=3;state.player_health=12;state.player_xp=1;
 obs=observe();assert(fp_act(&a,&obs,0,&cfg,&scratch)==FP_LEVEL_INT);
 (void)craftax_gameplay_step_native(&state,FP_LEVEL_INT,craftax_prng_key(1));
 assert(state.player_intelligence==4 && state.player_strength==4);

 /* A sword finish kills before a ready elemental can hit, while a spell
  * would concede that hit. Feed the public history of a previous strike. */
 empty_world();equipped();state.player_strength=4;state.player_intelligence=3;
 state.player_health=3;state.player_direction=FP_RIGHT;state.sword_enchantment=2;
 state.learned_spells[1]=true;
 state.melee_mobs.mask[0][0]=true;state.melee_mobs.type_id[0][0]=6;
 state.melee_mobs.health[0][0]=2;state.melee_mobs.position[0][0][0]=24;
 state.melee_mobs.position[0][0][1]=25;state.mob_bits[0][24]|=1ULL<<25;
 a.t=1;a.observed_r=a.observed_c=48;a.previous=observe();
 a.previous_action=FP_DO;a.previous_target=48*FP_SIDE+49;
 a.enemy_count=1;a.enemies[0]=(FPEnemy){a.previous_target,7,0,1,7.8f};
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_DO && fabsf(a.enemies[0].health-2)<0.001f);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(2));
 assert(state.achievements[44] && state.player_health==3 && state.player_mana==9);
}
static void fire_preparation(void) {
 FPConfig cfg=fp_default_config();FPObservation obs;int action;float reward=0;
 /* A partial stock and eleven health can already buy protection. */
 empty_world();equipped();state.player_level=5;state.player_strength=4;
 state.player_intelligence=3;state.player_health=11;
 state.monsters_killed[5]=8;state.inventory.ruby=1;
 state.sword_enchantment=2;state.item_map[5][24][24]=3;
 a.floors[5].steps=4000; /* The bounded gem search has finished. */
 state.item_map[5][24][26]=2; /* Exit search must not preempt this return fixture. */
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_ASCEND);

 /* An enchantment return at full health must wait safely for mana,
  * rather than descending from the Vault without using its ruby. */
 empty_world();equipped();state.player_level=4;state.player_strength=4;
 state.player_intelligence=3;state.player_health=12;state.player_mana=8;
 state.player_energy=state.player_food=state.player_drink=9;
 state.player_direction=FP_UP;state.inventory.ruby=1;
 state.sword_enchantment=2;state.monsters_killed[4]=8;a.floors[5].seen=1;
 state.achievements[31]=true;
 memset(state.map[4],FP_WALL,sizeof(state.map[4]));
 state.map[4][24][24]=state.map[4][24][25]=FP_PATH;
 state.map[4][23][24]=FP_FIRE_TABLE;state.item_map[4][24][24]=2;
 craftax_refresh_spawn_bits_all(&state);
 for(int t=0;t<16 && state.inventory.ruby;t++) {
  obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
  assert(action==FP_NOOP || action==FP_ENCHANT_ARMOUR);
  assert(state.player_level==4);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(230+t));remove_encounters();
 }
 int fire=0;for(int i=0;i<4;i++)fire+=state.armour_enchantments[i]==1;
 assert(fire==1 && state.inventory.ruby==0 && state.player_level==4);

 /* The gem search has a finite budget even when no ruby is generated. */
 empty_world();equipped();state.player_level=5;state.player_strength=4;
 state.player_health=12;state.player_energy=state.player_food=state.player_drink=9;
 state.monsters_killed[5]=8;state.item_map[5][24][24]=2;
 a.floors[5].steps=3999;state.inventory.torches=0;
 state.inventory.coal=0;cfg.torch_stock=0;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_DESCEND);
}
static void fire_resupply_and_exit(void) {
 FPConfig cfg=fp_default_config();FPObservation obs;int action;
 /* Retreat while hydration remains, or while wounded enough that a
  * waterless Fire shelter cannot sustain recovery. Preserve the vitals
  * through a real floor transition, with no enemy removal during the step. */
 for(int wounded=0;wounded<2;wounded++) {
  empty_world();equipped();state.player_level=6;
  state.player_health=wounded?4:9;state.player_drink=wounded?7:2;
  for(int i=0;i<4;i++)state.armour_enchantments[i]=1;
  state.item_map[6][24][24]=3;
  state.down_ladders[5][0]=state.down_ladders[5][1]=24;
  obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
  assert(action==FP_ASCEND);
  (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(1));
  assert(state.player_level==5 && state.player_drink>0);
  assert(state.player_health==(wounded?4:9));
 }
 /* An unlocked ladder escapes a ready elemental before it can attack,
  * even when the player needs food, water and health. */
 empty_world();equipped();state.player_level=6;state.monsters_killed[6]=8;
 state.player_health=4;state.player_food=state.player_drink=0;
 state.inventory.sapphire=4; /* No pending pre-Ice gem excursion. */
 state.item_map[6][24][24]=2;
 state.up_ladders[7][0]=state.up_ladders[7][1]=24;
 state.melee_mobs.mask[6][0]=true;state.melee_mobs.type_id[6][0]=6;
 state.melee_mobs.health[6][0]=20;state.melee_mobs.position[6][0][0]=24;
 state.melee_mobs.position[6][0][1]=25;state.mob_bits[6][24]|=1ULL<<25;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_DESCEND);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(2));
 assert(state.player_level==7 && state.player_health==4);
 assert(state.melee_mobs.health[6][0]==20);

 /* A later food trip from Ice must finish eating before descending
  * again. Otherwise the two unlocked ladders cause a starvation loop. */
 empty_world();equipped();state.player_level=6;state.monsters_killed[6]=8;
 state.player_food=0;state.player_direction=FP_RIGHT;a.floors[7].seen=1;
 state.item_map[6][24][24]=2;
 state.passive_mobs.mask[6][0]=true;state.passive_mobs.type_id[6][0]=1;
 state.passive_mobs.health[6][0]=4;state.passive_mobs.position[6][0][0]=24;
 state.passive_mobs.position[6][0][1]=25;state.mob_bits[6][24]|=1ULL<<25;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_DO);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(3));
 assert(state.player_level==6 && state.player_food==6);

 /* Gather spare, reachable rubies in the Gnomish Mines. */
 empty_world();equipped();state.player_level=2;state.inventory.ruby=1;
 state.player_direction=FP_RIGHT;state.map[2][24][25]=FP_RUBY;
 craftax_refresh_spawn_bits_all(&state);obs=observe();
 action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DO);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(4));
 assert(state.inventory.ruby==2);

 /* Protect more pieces on the first Vault visit, even after the first
  * enchantment achievement. No Troll Mines visit is needed for this. */
 empty_world();equipped();state.player_level=4;state.player_intelligence=3;
 state.player_mana=15;state.inventory.ruby=2;state.sword_enchantment=2;
 state.armour_enchantments[0]=1;state.player_direction=FP_RIGHT;
 state.map[4][24][25]=FP_FIRE_TABLE;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_ENCHANT_ARMOUR && !a.floors[5].seen);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(5));
 int fire=0;for(int i=0;i<4;i++)fire+=state.armour_enchantments[i]==1;
 assert(fire==2 && state.inventory.ruby==1);
}
static void ice_expedition_and_exit(void) {
 FPConfig cfg=fp_default_config();FPObservation obs;int action;
 /* The first public Ice view starts a return to the Sewers with spare
  * sapphires. Fully protected equipment and an empty gem stock finish it. */
 for(int mode=0;mode<4;mode++) {
  empty_world();equipped();state.player_level=7;state.player_health=9;
  state.inventory.sapphire=mode==1?0:4;state.sword_enchantment=2;
  state.item_map[7][24][24]=3;
  for(int i=0;i<4;i++)state.armour_enchantments[i]=mode==2?2:1;
  if(mode==3)memset(state.inventory.armour,0,sizeof(state.inventory.armour));
  obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
  assert((action==FP_ASCEND)==(mode==0));
 }
 /* Replace fire enchantments using the real engine, even with the first
  * armour achievement earned and an unenchanted sword competing for gems. */
 empty_world();equipped();state.player_level=3;a.floors[7].seen=1;
 state.inventory.sapphire=4;state.player_direction=FP_RIGHT;
 state.map[3][24][25]=FP_ICE_TABLE;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=1;
 craftax_refresh_spawn_bits_all(&state);float reward=0;
 for(int t=0;t<4;t++) {
  state.player_mana=15;obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
  assert(action==FP_ENCHANT_ARMOUR);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  remove_encounters();
 }
 for(int i=0;i<4;i++)assert(state.armour_enchantments[i]==2);
 assert(state.inventory.sapphire==0 && state.sword_enchantment==0);
 /* Missing table knowledge and depleted mana must not bounce between
  * the Sewers and the Vault. Keep the preparation on its current floor. */
 for(int waiting=0;waiting<2;waiting++) {
  empty_world();equipped();state.player_level=3;a.floors[7].seen=1;
  state.monsters_killed[3]=8;state.inventory.sapphire=4;
  state.item_map[3][24][24]=2;state.player_mana=waiting?0:15;
  if(waiting)state.map[3][24][25]=FP_ICE_TABLE;
  for(int i=0;i<4;i++)state.armour_enchantments[i]=1;
  craftax_refresh_spawn_bits_all(&state);obs=observe();
  action=fp_act(&a,&obs,0,&cfg,&scratch);
  assert(action!=FP_DESCEND);
  (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(10+waiting));
  assert(state.player_level==3);
 }
 /* Returning through the Vault preserves Ice protection despite spare
  * rubies, an available fire table, and remembered Troll Mines. */
 empty_world();equipped();state.player_level=4;a.floors[7].seen=1;a.floors[5].seen=1;
 state.monsters_killed[4]=8;state.inventory.ruby=4;state.sword_enchantment=2;
 state.player_mana=15;state.player_direction=FP_RIGHT;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=2;
 state.map[4][24][25]=FP_FIRE_TABLE;state.item_map[4][24][24]=2;
 state.up_ladders[5][0]=state.up_ladders[5][1]=24;
 /* The Vault's chest-loot budget (sg_kit.inc STOCK) is spent. */
 a.kit_loot_steps[4]=1<<20;
 craftax_refresh_spawn_bits_all(&state);obs=observe();
 action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DESCEND);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(4));
 assert(state.player_level==5 && state.inventory.ruby==4);
 for(int i=0;i<4;i++)assert(state.armour_enchantments[i]==2);
 /* Leave Ice with food still available for the route through Fire. */
 empty_world();equipped();state.player_level=7;state.player_food=3;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=2;
 state.item_map[7][24][24]=3;
 state.down_ladders[6][0]=state.down_ladders[6][1]=24;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_ASCEND);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(5));
 assert(state.player_level==6 && state.player_food==3);
 /* An unlocked Graveyard ladder takes precedence over hunger, recovery,
  * enchantment preparation, and a ready Frost Troll's lethal attack. */
 empty_world();equipped();state.player_level=7;state.monsters_killed[7]=8;
 state.player_health=4;state.player_food=state.player_drink=0;
 state.inventory.sapphire=4;state.item_map[7][24][24]=2;
 state.up_ladders[8][0]=state.up_ladders[8][1]=24;
 state.melee_mobs.mask[7][0]=true;state.melee_mobs.type_id[7][0]=7;
 state.melee_mobs.health[7][0]=24;state.melee_mobs.position[7][0][0]=24;
 state.melee_mobs.position[7][0][1]=25;state.mob_bits[7][24]|=1ULL<<25;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DESCEND);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(6));
 assert(state.player_level==8 && state.player_health==4);
 assert(state.achievements[35] && reward>=CRAFTAX_ACHIEVEMENT_REWARD_MAP[35]);
 assert(state.melee_mobs.health[7][0]==24);
 /* Ice torches are spaced by ICE_TORCH_GAIN (25 unseen cells; round 14).
  * Three unlit rows above the faced cell plus five cells beyond the upper
  * view edge make a thirty-cell gain, so the faced placement is taken. */
 empty_world();equipped();state.player_level=7;state.player_direction=FP_UP;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=2;
 for(int r=20;r<=22;r++)for(int c=20;c<=28;c++)state.light_map[7][r][c]=0;
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(action==FP_PLACE_TORCH);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(12));
 assert(state.inventory.torches==19 && state.light_map[7][20][24]>0);
 /* In a narrow corridor, facing an approaching Frost Troll costs one
  * protected hit. Three fireballs then kill it during its real cooldown. */
 empty_world();equipped();state.player_level=7;state.player_strength=4;
 state.player_intelligence=5;state.player_health=12;state.player_mana=12;
 state.player_drink=3;state.player_direction=FP_UP;state.learned_spells[0]=true;
 for(int i=0;i<4;i++){state.inventory.armour[i]=1;state.armour_enchantments[i]=2;}
 memset(state.map[7],FP_WALL,sizeof(state.map[7]));
 for(int c=24;c<=26;c++)state.map[7][24][c]=FP_PATH;
 state.map[7][25][25]=FP_WATER;
 state.melee_mobs.mask[7][0]=true;state.melee_mobs.type_id[7][0]=7;
 state.melee_mobs.health[7][0]=24;state.melee_mobs.position[7][0][0]=24;
 state.melee_mobs.position[7][0][1]=26;state.mob_bits[7][24]|=1ULL<<26;
 craftax_refresh_spawn_bits_all(&state);
 for(int limited=0;limited<2;limited++) {
  state.player_health=limited?9:12;state.player_mana=limited?12:4;
  obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_NOOP);
  fp_reset(&a,17);memset(a.achieved,1,sizeof(a.achieved));
 }
 state.player_health=12;state.player_mana=12;obs=observe();
 action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_RIGHT);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(13));
 assert(fabsf(state.player_health-8.6f)<0.001f && state.player_direction==FP_RIGHT);
 for(int t=0;t<3;t++) {
  obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action==FP_FIREBALL);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(14+t));
 }
 assert(!state.melee_mobs.mask[7][0] && state.achievements[46]);
 assert(fabsf(state.player_health-8.6f)<0.001f);
}
static void late_expedition_robustness(void) {
 FPConfig cfg=fp_default_config();FPObservation obs;int action;float reward=0;
 /* Fire's unused floor XP must strengthen spells before the first Ice
  * descent, while an imminent melee attack still takes priority. */
 for(int danger=0;danger<2;danger++) {
  empty_world();equipped();state.player_level=6;state.player_strength=4;
  state.player_intelligence=3;state.player_health=12;state.player_xp=1;
  state.inventory.sapphire=4; /* XP priority with Ice preparation already supplied. */
  state.achievements[craftax_medium_level_achievement(6)]=true;
  state.monsters_killed[6]=8;state.item_map[6][24][24]=2;
  state.up_ladders[7][0]=state.up_ladders[7][1]=24;
  state.player_direction=FP_RIGHT;state.learned_spells[1]=true;
  if(danger) {
   state.melee_mobs.mask[6][0]=true;state.melee_mobs.type_id[6][0]=6;
   state.melee_mobs.health[6][0]=20;state.melee_mobs.position[6][0][0]=24;
   state.melee_mobs.position[6][0][1]=25;state.mob_bits[6][24]|=1ULL<<25;
  }
  obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
  assert(action==(danger?FP_DESCEND:FP_LEVEL_INT));
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(30+danger));
  if(danger)assert(state.player_level==7 && state.player_health==12);
  else {
   assert(state.player_level==6 && state.player_intelligence==4 && state.player_xp==0);
   remove_encounters();obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);
   assert(action==FP_DESCEND);
  }
 }
 /* With no food source in Fire, continue the return to the Troll Mines
  * instead of descending from this cleared floor back into hungry Ice. */
 empty_world();equipped();state.player_level=6;a.floors[7].seen=1;
 state.player_strength=4;state.player_health=12;state.player_food=2;
 state.monsters_killed[6]=8;state.item_map[6][24][24]=2;
 state.item_map[6][24][20]=3;state.up_ladders[6][0]=24;state.up_ladders[6][1]=20;
 state.down_ladders[5][0]=state.down_ladders[5][1]=24;
 reward=0;
 for(int t=0;t<8 && state.player_level==6;t++) {
  obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action!=FP_DESCEND);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(40+t));remove_encounters();
 }
 assert(state.player_level==5 && state.player_food>0);
 /* Start building a sealed Ice shelter while nine health still provides
  * room for mistakes, then recover to the actual strength-dependent max. */
 empty_world();equipped();state.player_level=7;state.player_strength=4;state.player_health=9;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=2;
 reward=0;obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);
 assert(a.recovering && a.skill==FP_RECOVER);
 for(int t=0;t<140 && state.player_health<12;t++) {
  if(t){obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);}
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(50+t));remove_encounters();
  state.player_food=state.player_drink=9;
 }
 assert(state.player_health==12 && state.inventory.stone<20);
 /* Carried coal creates usable light in Ice even when no torches remain. */
 empty_world();equipped();state.player_level=7;state.player_strength=4;state.player_health=12;
 state.inventory.torches=0;state.inventory.coal=1;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=2;
 state.map[7][23][24]=FP_TABLE;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_MAKE_TORCH);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(200));
 assert(state.inventory.torches>0 && state.inventory.coal==0 && state.inventory.wood==39);
 /* Mine a nearby third diamond on the cleared return route, then use the
  * real crafting recipe to improve armour before taking the Ice route. */
 empty_world();equipped();state.player_level=5;a.floors[7].seen=1;
 state.achievements[craftax_medium_level_achievement(5)]=true;
 state.monsters_killed[5]=8;state.inventory.diamond=2;state.player_direction=FP_RIGHT;
 for(int i=0;i<4;i++){state.inventory.armour[i]=1;state.armour_enchantments[i]=2;}
 state.item_map[5][24][24]=2;state.map[5][24][25]=FP_DIAMOND;
 state.map[5][23][24]=FP_TABLE;craftax_refresh_spawn_bits_all(&state);
 obs=observe();action=fp_act(&a,&obs,0,&cfg,&scratch);assert(action==FP_DO);
 reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(201));remove_encounters();
 assert(state.inventory.diamond==3 && state.player_level==5);
 obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action==FP_DIAMOND_ARMOUR);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(202));
 int protection=0;for(int i=0;i<4;i++){protection+=state.inventory.armour[i];assert(state.armour_enchantments[i]==2);}
 assert(protection==5 && state.inventory.diamond==0);
 /* A protected hit leaves 0.6 health and opens the cooldown needed for
  * three fireballs. Retreating here loses the already established facing. */
 empty_world();equipped();state.player_level=7;state.player_strength=4;
 state.player_intelligence=5;state.player_health=4;state.player_mana=8;
 state.learned_spells[0]=true;state.player_direction=FP_RIGHT;state.sword_enchantment=2;
 for(int i=0;i<4;i++){state.inventory.armour[i]=1;state.armour_enchantments[i]=2;}
 memset(state.map[7],FP_WALL,sizeof(state.map[7]));
 for(int c=23;c<=25;c++)state.map[7][24][c]=FP_PATH;
 state.melee_mobs.mask[7][0]=true;state.melee_mobs.type_id[7][0]=7;
 state.melee_mobs.health[7][0]=24;state.melee_mobs.position[7][0][0]=24;
 state.melee_mobs.position[7][0][1]=25;state.mob_bits[7][24]|=1ULL<<25;
 craftax_refresh_spawn_bits_all(&state);reward=0;
 /* A mid-floor fixture: the agent did not arrive here by ladder, so its
  * memory holds no ladder underfoot (the shield's ladder alternative,
  * TX_ALT2, reads it). Mirror arrival's resets without the landing. */
 a.level=7;a.floors[7].seen=1;a.floors[7].up=-1;a.r=a.c=48;a.floors[7].entered=a.t;
 a.enemy_count=a.shot_count=a.arrow_count=0;
 for(int t=0;t<3;t++) {
  obs=observe();action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action==FP_FIREBALL);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(210+t));
 }
 assert(state.achievements[46] && !state.melee_mobs.mask[7][0]);
 assert(fabsf(state.player_health-0.6f)<0.001f);
}
static void episodes(void) {
 craftax_set_lazy_floors(1);FPConfig cfg=fp_default_config();
 for(uint32_t seed=0;seed<4;seed++) {
  Craftax env={0};env.state=&state;env.seed=seed;craftax_reset_state_from_seed(&env);fp_reset(&a,seed+1);
  float reward=0;double total=0;
  for(int t=0;t<5000 && !craftax_is_game_over_native(&state);t++) {
   FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);assert(action>=0&&action<43);
   CraftaxThreefryKey key,rng,reset;craftax_threefry_split(env.rng_key,&env.rng_key,&key);craftax_threefry_split(key,&rng,&reset);
   reward=craftax_gameplay_step_native(&state,action,rng);total+=reward;
   if(state.player_level==0 && a.level==0)assert(a.r==state.player_position[0]+24 && a.c==state.player_position[1]+24);
  }
  double expected=0.1*(state.player_health-9);
  for(int i=0;i<67;i++)expected+=state.achievements[i]*CRAFTAX_ACHIEVEMENT_REWARD_MAP[i];
  assert(fabs(total-expected)<0.001);
 }
}

static uint64_t goal_seed(uint64_t x) {
 x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
static void goal_contracts(void) {
 FPCommand command;char error[128];FPObservation o={0};
 assert(fp_parse_command("inv:wood:eq:5+equip:pick:min:2;adj:water:up",&command,error,sizeof(error)));
 assert(command.count==3 && command.stages==2 && command.stage[2]==1);
 o.inv[0]=6;assert(!fp_goal_satisfied(&command.goals[0],&o));
 assert(fp_parse_command("inv:wood:range:5:9",&command,error,sizeof(error)));
 assert(fp_goal_satisfied(&command.goals[0],&o));
 assert(fp_parse_command("inv:wood:min:5",&command,error,sizeof(error)));
 o.inv[0]=20;assert(fp_goal_satisfied(&command.goals[0],&o));
 assert(fp_parse_command("equip:pick:eq:2",&command,error,sizeof(error)));
 o.inv[FP_I_PICK]=3;assert(!fp_goal_satisfied(&command.goals[0],&o));
 assert(fp_parse_command("adj:water:up",&command,error,sizeof(error)));
 o.visible[38]=1;o.blocks[38]=FP_WATER;o.dir=FP_LEFT;
 assert(fp_goal_satisfied(&command.goals[0],&o));o.visible[38]=0;
 assert(!fp_goal_satisfied(&command.goals[0],&o));
 const char *invalid[]={"", "inv:wood:eq:5+inv:wood:eq:6", "inv:wood:range:9:3", "inv:wood:min:-1", "inv:foo:eq:1", "adj:tree:north", "floor:9", "floor:1+floor:2", "equip:pick:eq:3;equip:pick:eq:2", "adj:water:up+adj:tree:up", "prepare:6+equip:pick:eq:1", "inv:wood:eq:5;", "inv:wood:eq:5:7", "recover:14"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!fp_parse_command(invalid[i],&command,error,sizeof(error)));
 empty_world();FPConfig cfg=fp_default_config();o=observe();
 (void)fp_act(&a,&o,0,&cfg,&scratch);b=a;
 assert(fp_parse_command("equip:pick:eq:2",&command,error,sizeof(error)));
 assert(fp_set_command(&a,&command,error,sizeof(error)));
 assert(!memcmp(a.floors,b.floors,sizeof(a.floors)) && !memcmp(&a.previous,&b.previous,sizeof(a.previous)));
 b=a;command.goals[0].hi=99;
 assert(!fp_set_command(&a,&command,error,sizeof(error)));assert(!memcmp(&a,&b,sizeof(a)));
 fp_clear_command(&a);assert(!a.intent.enabled);assert(!memcmp(a.floors,b.floors,sizeof(a.floors)));
 assert(fp_parse_command("inv:wood:min:0;inv:stone:min:0",&command,error,sizeof(error)));
 assert(fp_set_command(&a,&command,error,sizeof(error)));
 (void)fp_act(&a,&o,0,&cfg,&scratch);assert(a.intent.stage==1 && a.intent.active==-1);
 (void)fp_act(&a,&o,0,&cfg,&scratch);assert(a.intent.claim && a.intent.status==FP_CMD_COMPLETE);
}
static void goal_regressions(void) {
 const char *commands[]={"inv:wood:eq:5","equip:pick:eq:3","adj:table:right","floor:1"};
 const uint32_t worlds[]={3868477353U,1192128075U,3090729777U,4206239998U};
 FPConfig cfg=fp_default_config();craftax_set_lazy_floors(1);
 for(int k=0;k<4;k++) {
  Craftax env={0};env.state=&state;env.seed=worlds[k];craftax_reset_state_from_seed(&env);
  fp_reset(&a,goal_seed(worlds[k]));FPCommand c;char error[128];assert(fp_parse_command(commands[k],&c,error,sizeof(error)));
  assert(fp_set_command(&a,&c,error,sizeof(error)));float reward=0;int success=0;
  for(int t=0;t<2000 && !craftax_is_game_over_native(&state);t++) {
   FPObservation o=observe();int action=fp_act(&a,&o,reward,&cfg,&scratch);
   if(k==0)success=state.inventory.wood==5;
   if(k==1)success=state.inventory.pickaxe==3;
   if(k==2)success=o.visible[50] && state.map[0][state.player_position[0]][state.player_position[1]+1]==FP_TABLE;
   if(k==3)success=state.player_level==1;
   assert(!a.intent.claim || success);if(success)break;
   CraftaxThreefryKey key,rng,reset;craftax_threefry_split(env.rng_key,&env.rng_key,&key);craftax_threefry_split(key,&rng,&reset);
   reward=craftax_gameplay_step_native(&state,action,rng);
  }
  assert(success);
 }
}
static void goal_sequence_and_revalidation(void) {
 empty_world();state.inventory.wood=20;state.inventory.stone=10;
 state.map[0][24][25]=FP_TABLE;craftax_refresh_spawn_bits_all(&state);
 FPConfig cfg=fp_default_config();FPCommand command;char error[128];float reward=0;
 assert(fp_parse_command("equip:pick:eq:1;equip:pick:eq:2;equip:sword:eq:2+inv:wood:eq:5",&command,error,sizeof(error)));
 assert(fp_set_command(&a,&command,error,sizeof(error)));
 for(int t=0;t<100 && a.intent.status!=FP_CMD_COMPLETE;t++) {
  FPObservation o=observe();int action=fp_act(&a,&o,reward,&cfg,&scratch);
  if(a.intent.claim)assert(state.inventory.pickaxe==2 && state.inventory.sword==2 && state.inventory.wood==5);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));remove_encounters();
 }
 assert(a.intent.status==FP_CMD_COMPLETE);
 /* A forced ACTUAL action removes the remembered table. The next public
  * view corrects its workstation affordance before a new crafting command. */
 empty_world();state.inventory.wood=10;state.inventory.stone=4;
 state.map[0][24][25]=FP_TABLE;state.player_direction=FP_RIGHT;craftax_refresh_spawn_bits_all(&state);
 fp_reset(&a,99);FPObservation o=observe();fp_observe_action(&a,&o,0,FP_DO,&scratch);
 reward=craftax_gameplay_step_native(&state,FP_DO,craftax_prng_key(701));remove_encounters();
 assert(a.floors[0].map[48*FP_SIDE+49]==FP_TABLE);
 assert(fp_parse_command("equip:pick:eq:3",&command,error,sizeof(error)));
 assert(fp_set_command(&a,&command,error,sizeof(error)));o=observe();(void)fp_act(&a,&o,reward,&cfg,&scratch);
 assert(a.floors[0].map[48*FP_SIDE+49]!=FP_TABLE);
 assert(!(a.floors[0].stations[48*FP_SIDE+48]&1));
 /* Goal-conditioned actions still obey the public observation boundary. */
 b=a;CraftaxState copy=state;FPObservation x=observe();
 state.map[7][1][1]=FP_DIAMOND;state.potion_mapping[0]^=1;state.state_rng[0]^=123;
 FPObservation y=observe();assert(!memcmp(&x,&y,sizeof(x)));
 assert(fp_act(&a,&x,reward,&cfg,&scratch)==fp_act(&b,&y,reward,&cfg,&scratch));
 assert(!memcmp(&a,&b,sizeof(a)));state=copy;
}
static void dungeon_precast_setup(int magic,float health,int sword,int strength) {
 empty_world();
 memset(&state.melee_mobs,0,sizeof(state.melee_mobs));memset(&state.ranged_mobs,0,sizeof(state.ranged_mobs));
 memset(&state.passive_mobs,0,sizeof(state.passive_mobs));memset(&state.mob_projectiles,0,sizeof(state.mob_projectiles));
 memset(&state.player_projectiles,0,sizeof(state.player_projectiles));memset(state.mob_bits,0,sizeof(state.mob_bits));
 memset(state.map[1],FP_WALL,sizeof(state.map[1]));memset(state.light_map[1],255,sizeof(state.light_map[1]));
 memset(state.item_map[1],0,sizeof(state.item_map[1]));
 state.map[1][24][24]=state.map[1][24][25]=state.map[1][24][26]=state.map[1][23][25]=FP_PATH;
 state.player_level=1;state.player_position[0]=state.player_position[1]=24;state.player_direction=FP_RIGHT;
 state.player_health=health;state.player_food=state.player_drink=state.player_energy=9;state.player_mana=9;
 state.player_strength=strength;state.player_intelligence=state.player_dexterity=1;state.player_xp=0;
 state.inventory.pickaxe=3;state.inventory.sword=sword;state.inventory.wood=40;state.inventory.stone=20;
 state.inventory.torches=20;state.inventory.bow=!magic;state.inventory.arrows=magic?0:1;
 memset(state.inventory.armour,0,sizeof(state.inventory.armour));memset(state.inventory.potions,0,sizeof(state.inventory.potions));
 state.sword_enchantment=state.bow_enchantment=0;memset(state.armour_enchantments,0,sizeof(state.armour_enchantments));
 state.learned_spells[0]=magic;state.learned_spells[1]=false;state.is_sleeping=state.is_resting=false;
 state.melee_mobs.mask[1][0]=true;state.melee_mobs.type_id[1][0]=2;state.melee_mobs.health[1][0]=9;
 state.melee_mobs.position[1][0][0]=23;state.melee_mobs.position[1][0][1]=25;state.mob_bits[1][23]|=1ULL<<25;
 craftax_refresh_spawn_bits_all(&state);fp_reset(&a,17);a.level=1;a.floors[1].seen=1;
 memset(a.achieved,1,sizeof(a.achieved));
}
static void dungeon_diagonal_precast(void) {
 FPConfig cfg=fp_default_config();
 for(int magic=0;magic<2;magic++) {
  /* With stones in hand the shield's stone alternative (full_tactics.inc,
   * TX_ALT2) walls off the cell the diagonal orc would enter. */
  dungeon_precast_setup(magic,2.8f,3,2);FPObservation o=observe();
  int action=fp_act(&a,&o,0,&cfg,&scratch);assert(action==FP_PLACE_STONE && a.skill==FP_FIGHT);
  /* Without stones the program pre-casts and then strikes. */
  dungeon_precast_setup(magic,2.8f,3,2);state.inventory.stone=0;o=observe();
  assert(o.mobs[3*11+6][0]==3 && o.health==2.8f);
  action=fp_act(&a,&o,0,&cfg,&scratch);assert(action==(magic?FP_FIREBALL:FP_SHOOT));
  /* Step key4 makes the diagonal orc enter the pre-shot facing cell. */
  float reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(4));
  assert(state.melee_mobs.health[1][0]<9);
  o=observe();action=fp_act(&a,&o,reward,&cfg,&scratch);assert(action==FP_DO);
  (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(2));
  assert(!state.melee_mobs.mask[1][0] && state.player_health>=2.8f);
 }
}
static void dungeon_precast_gates(void) {
 FPConfig cfg=fp_default_config();
 dungeon_precast_setup(1,9,3,1);FPObservation o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)!=FP_FIREBALL);
 dungeon_precast_setup(0,9,1,2);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)!=FP_SHOOT);
 dungeon_precast_setup(0,9,4,2);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)!=FP_SHOOT);
}
static void prep_setup(int level,int armour,int torches,int ticks,int pocket) {
 empty_world();
 memset(&state.melee_mobs,0,sizeof(state.melee_mobs));memset(&state.ranged_mobs,0,sizeof(state.ranged_mobs));
 memset(&state.passive_mobs,0,sizeof(state.passive_mobs));memset(&state.mob_projectiles,0,sizeof(state.mob_projectiles));
 memset(&state.player_projectiles,0,sizeof(state.player_projectiles));memset(state.mob_bits,0,sizeof(state.mob_bits));
 memset(state.map[level],FP_WALL,sizeof(state.map[level]));memset(state.light_map[level],255,sizeof(state.light_map[level]));
 memset(state.item_map[level],0,sizeof(state.item_map[level]));
 state.map[level][24][24]=FP_PATH;
 if(pocket) {
  memset(state.light_map[level],0,sizeof(state.light_map[level]));
  for(int r=20;r<=28;r++)for(int c=19;c<=29;c++)state.light_map[level][r][c]=255;
  for(int c=24;c<=30;c++)state.map[level][24][c]=FP_PATH;
  state.map[level][24][31]=FP_DIAMOND;
 } else state.map[level][24][25]=FP_DIAMOND;
 state.item_map[level][24][24]=2;state.down_ladders[level][0]=state.down_ladders[level][1]=24;
 state.player_level=level;state.player_position[0]=state.player_position[1]=24;state.player_direction=pocket?FP_LEFT:FP_RIGHT;
 state.player_health=9;state.player_food=state.player_drink=state.player_energy=9;state.player_mana=9;
 state.player_strength=state.player_intelligence=state.player_dexterity=1;state.player_xp=0;
 state.inventory.pickaxe=state.inventory.sword=4;state.inventory.wood=40;state.inventory.stone=20;
 state.inventory.coal=0;state.inventory.iron=0;state.inventory.diamond=0;state.inventory.sapphire=4;state.inventory.ruby=4;
 state.inventory.torches=torches;state.inventory.bow=0;state.inventory.arrows=20;
 memset(state.inventory.armour,0,sizeof(state.inventory.armour));
 for(int i=0;armour>0 && i<4;i++){state.inventory.armour[i]=armour>=2?2:1;armour-=state.inventory.armour[i];}
 memset(state.inventory.potions,0,sizeof(state.inventory.potions));state.inventory.books=0;
 state.sword_enchantment=state.bow_enchantment=0;memset(state.armour_enchantments,0,sizeof(state.armour_enchantments));
 state.learned_spells[0]=state.learned_spells[1]=false;state.is_sleeping=state.is_resting=false;
 state.monsters_killed[level]=8;craftax_refresh_spawn_bits_all(&state);fp_reset(&a,17);a.level=level;a.floors[level].seen=1;
 a.floors[level].steps=ticks;memset(a.achieved,1,sizeof(a.achieved));
}
static void diamonds_before_exit(void) {
 FPConfig cfg=fp_default_config();prep_setup(2,2,6,3600,0);FPObservation o=observe();
 int action=fp_act(&a,&o,0,&cfg,&scratch);assert(action==FP_DO && a.skill==FP_GATHER);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(4));assert(state.inventory.diamond==1);
 prep_setup(2,8,6,3600,0);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
 prep_setup(3,2,6,3600,0);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
}
static void dark_armour_pocket(void) {
 FPConfig cfg=fp_default_config();prep_setup(2,2,3,3600,1);FPObservation o=observe();
 int action=fp_act(&a,&o,0,&cfg,&scratch);
 assert(action!=FP_DESCEND && a.skill==FP_LIGHT);
 int lit=0;float reward=0;
 for(int t=0;t<20;t++) {
  if(t){o=observe();action=fp_act(&a,&o,reward,&cfg,&scratch);}
  assert(action!=FP_DESCEND);reward=craftax_gameplay_step_native(&state,action,craftax_prng_key((uint32_t)t));
  if(action==FP_PLACE_TORCH){lit=state.light_map[2][24][31]>12;break;}
 }
 assert(lit && state.inventory.torches==2);
 /* Before first Sewers: keep two torches, seek full diamond armour,
  * and prospect until the 7000-step Mines budget. */
 prep_setup(2,2,6,3600,1);o=observe();action=fp_act(&a,&o,0,&cfg,&scratch);
 assert(action!=FP_DESCEND && a.skill==FP_LIGHT);
 prep_setup(2,6,7,3600,1);o=observe();action=fp_act(&a,&o,0,&cfg,&scratch);
 assert(action!=FP_DESCEND && a.skill==FP_LIGHT);
 prep_setup(2,2,7,6000,1);o=observe();action=fp_act(&a,&o,0,&cfg,&scratch);
 assert(action!=FP_DESCEND && a.skill==FP_LIGHT);
 /* Out of light with an empty slot and no coal, the once-per-game coal
  * trip (r_cb_coal) goes back for coal and torches; once it is spent the
  * floor is left as before. */
 prep_setup(2,2,2,3600,1);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)!=FP_DESCEND && a.cb_coal==1);
 prep_setup(2,2,2,3600,1);a.cb_coal=2;o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
 prep_setup(2,8,7,3600,1);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
 /* Perception counts the current step before evaluating the gate. */
 prep_setup(2,2,7,6998,1);o=observe();action=fp_act(&a,&o,0,&cfg,&scratch);
 assert(a.floors[2].steps==6999 && action!=FP_DESCEND && a.skill==FP_LIGHT);
 prep_setup(2,2,7,6999,1);o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
 assert(a.floors[2].steps==7000);
 /* After first Sewers the legacy six-torch/six-armour/time gates remain. */
 prep_setup(2,2,7,3600,1);a.floors[3].seen=1;o=observe();action=fp_act(&a,&o,0,&cfg,&scratch);
 assert(action!=FP_DESCEND && a.skill==FP_LIGHT);
 prep_setup(2,2,6,3600,1);a.floors[3].seen=1;o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
 prep_setup(2,6,7,3600,1);a.floors[3].seen=1;o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
 prep_setup(2,2,7,5*cfg.mine_steps-1,1);a.floors[3].seen=1;o=observe();assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DESCEND);
}
static void vault_precaster_and_retreat_priority(void) {
 FPConfig cfg=fp_default_config();FPObservation o={0};
 for(int i=0;i<FP_VIEW;i++){o.visible[i]=1;o.blocks[i]=FP_PATH;}
 o.level=4;o.health=11;o.food=o.drink=o.energy=9;o.mana=15;
 o.str=o.intelligence=3;o.dex=1;o.dir=FP_RIGHT;o.spells[0]=1;
 o.inv[FP_I_WOOD]=40;o.inv[FP_I_STONE]=20;o.inv[FP_I_TORCH]=20;
 o.inv[FP_I_PICK]=o.inv[FP_I_SWORD]=4;
 for(int i=0;i<4;i++)o.armour[i]=1;
 /* A diagonal knight is not on the ordinary long-axis shooting line.
  * Hold the facing as it approaches, then cast before its first hit. */
 o.mobs[5*11+7][0]=5;
 fp_reset_cold(&a,17,&o);memset(a.achieved,1,sizeof(a.achieved));
 assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_NOOP && a.skill==FP_FIGHT);
 o.mobs[5*11+7][0]=0;o.mobs[4*11+7][0]=5;
 assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_FIREBALL && a.skill==FP_FIGHT);
 /* Public mana confirms launch; the shot vanished into the knight as it
  * entered the faced cell. One sword blow now finishes its remaining HP. */
 o.mana-=2;o.mobs[4*11+7][0]=0;o.mobs[4*11+6][0]=5;
 assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DO);
 assert(a.enemy_count==1 && fabsf(a.enemies[0].health-6)<0.001f);

 /* Low-HP fountain and food errands must not preempt the known up ladder.
  * Both are immediately usable on the right, while the ladder is left. */
 memset(o.mobs,0,sizeof(o.mobs));o.spells[0]=0;o.health=2.2f;
 o.items[4*11+1]=3;
 for(int mode=0;mode<2;mode++) {
  o.food=mode?3:9;o.drink=mode?9:4;
  o.blocks[4*11+6]=mode?FP_RIPE:FP_FOUNTAIN;
  fp_reset_cold(&a,17,&o);memset(a.achieved,1,sizeof(a.achieved));
  assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_LEFT && a.skill==FP_TRAVEL);
 }
}

static void troll_chase_retreat_priority(void) {
 FPConfig cfg=fp_default_config();FPObservation o={0};
 for(int i=0;i<FP_VIEW;i++){o.visible[i]=1;o.blocks[i]=FP_PATH;}
 o.level=5;o.health=5;o.energy=11;o.mana=15;
 o.str=o.intelligence=3;o.dex=2;o.dir=FP_RIGHT;
 o.inv[FP_I_WOOD]=40;o.inv[FP_I_STONE]=20;o.inv[FP_I_TORCH]=20;
 o.inv[FP_I_PICK]=o.inv[FP_I_SWORD]=4;
 for(int i=0;i<4;i++)o.armour[i]=1;
 o.items[4*11+1]=3;o.mobs[7*11+5][0]=6;
 /* A nearby pursuing troll and short known ladder route override critical
  * food/water errands before clearance. This case has no combat spell. */
 for(int mode=0;mode<2;mode++) {
  o.food=mode?0:11;o.drink=mode?11:0;
  o.blocks[4*11+6]=mode?FP_RIPE:FP_FOUNTAIN;
  fp_reset_cold(&a,17,&o);memset(a.achieved,1,sizeof(a.achieved));
  assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_LEFT && a.skill==FP_TRAVEL);
  /* After clearance, one more troll blow (5.6 at iron armour) would leave
   * health 5 below 3 with the troll in reach: the low-health flee
   * (r_cb_lowflee) takes the known ladder route before the supply errand. */
  o.clear=1;fp_reset_cold(&a,17,&o);memset(a.achieved,1,sizeof(a.achieved));
  assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_LEFT && a.skill==FP_TRAVEL);
  /* With health to spare for that blow, preserve the local supply action;
   * broad medical trips exhausted supplies on expensive returns through the
   * Vault and Sewers. */
  o.health=9;fp_reset_cold(&a,17,&o);memset(a.achieved,1,sizeof(a.achieved));
  assert(fp_act(&a,&o,0,&cfg,&scratch)==FP_DO && a.skill==(mode?FP_EAT:FP_DRINK));
  o.health=5;o.clear=0;
 }
}

/* Insert into tests/full_policy_test.c, and call from main(). This exercises
 * the chooser through public observation and the unchanged native engine. */
static void unknown_approach_localization(void) {
 static const int rr[5]={0,0,0,-1,1},cc[5]={0,-1,1,0,0};
 FPConfig cfg=fp_default_config();
 for(int d=FP_LEFT;d<=FP_DOWN;d++) {
  empty_world();equipped();remove_encounters();
  state.player_level=5;state.player_direction=d==FP_LEFT?FP_RIGHT:d==FP_RIGHT?FP_LEFT:d==FP_UP?FP_DOWN:FP_UP;
  state.player_mana=4;state.learned_spells[0]=true;
  state.player_food=state.player_drink=state.player_energy=9;state.player_health=9;
  int nr=24+rr[d],nc=24+cc[d],er=24+2*rr[d]+cc[d],ec=24+2*cc[d]-rr[d];
  state.light_map[5][nr][nc]=0;
  state.melee_mobs.mask[5][0]=true;state.melee_mobs.type_id[5][0]=5;state.melee_mobs.health[5][0]=20;
  state.melee_mobs.position[5][0][0]=er;state.melee_mobs.position[5][0][1]=ec;
  state.mob_bits[5][er]|=1ULL<<ec;
  craftax_refresh_spawn_bits_all(&state);
  FPObservation o=observe();assert(!o.visible[(4+rr[d])*11+5+cc[d]]);
  fp_reset_cold(&a,17,&o);memset(a.achieved,1,sizeof(a.achieved));
  int action=fp_act(&a,&o,0,&cfg,&scratch);
  /* Baseline wrongly turns into invisible PATH and predicts no movement. */
  assert(action!=d);
  (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(7));
  assert(a.r==state.player_position[0]+24 && a.c==state.player_position[1]+24);
 }
}

static void ice_diagonal_precast(void) {
 FPConfig cfg=fp_default_config();
 dungeon_precast_setup(1,12,4,4);
 state.map[7][24][24]=state.map[7][24][25]=state.map[7][23][25]=FP_PATH;
 state.player_level=7;state.player_intelligence=5;state.player_mana=12;
 state.melee_mobs.mask[1][0]=false;state.mob_bits[1][23]=0;
 state.melee_mobs.mask[7][0]=true;state.melee_mobs.type_id[7][0]=7;state.melee_mobs.health[7][0]=24;
 state.melee_mobs.position[7][0][0]=23;state.melee_mobs.position[7][0][1]=25;state.mob_bits[7][23]|=1ULL<<25;
 for(int i=0;i<4;i++){state.inventory.armour[i]=1;state.armour_enchantments[i]=2;}
 craftax_refresh_spawn_bits_all(&state);fp_reset(&a,17);memset(a.achieved,1,sizeof(a.achieved));
 FPObservation o=observe();int action=fp_act(&a,&o,0,&cfg,&scratch);assert(action==FP_FIREBALL);
 (void)craftax_gameplay_step_native(&state,action,craftax_prng_key(4));
 assert(state.melee_mobs.health[7][0]==15 && state.player_health==12);
}


/* Cleared Ice can retain coal while the exhausted Overworld disables wood
 * trips. Harvest a remembered local shrub without turning onto that walkable
 * target, then use the native torch recipe. */
static void ice_shrub_supply_setup(int level,int clear,int coal,int wood) {
 empty_world();equipped();
 memset(state.map[level],FP_ICE_GRASS,sizeof(state.map[level]));
 state.player_level=level;state.player_position[0]=state.player_position[1]=24;
 state.player_direction=FP_UP;state.player_strength=4;state.player_intelligence=5;
 state.player_health=12;state.player_mana=21;state.player_xp=0;
 state.player_food=state.player_drink=state.player_energy=9;
 state.inventory.wood=wood;state.inventory.coal=coal;state.inventory.torches=0;
 for(int i=0;i<4;i++)state.armour_enchantments[i]=2;
 state.sword_enchantment=2;state.monsters_killed[level]=clear?8:0;
 state.map[level][24][25]=FP_ICE_SHRUB;state.map[level][23][24]=FP_TABLE;
 craftax_refresh_spawn_bits_all(&state);remove_encounters();
 FPObservation obs=observe();fp_reset_cold(&a,17,&obs);
 memset(a.achieved,1,sizeof(a.achieved));a.wood_exhausted=1;
}
static void ice_shrub_supply(void) {
 FPConfig cfg=fp_default_config();
 ice_shrub_supply_setup(7,1,1,0);float reward=0;int harvested=0;
 for(int t=0;t<10;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(1200+t));remove_encounters();
  assert(a.r==state.player_position[0]+24 && a.c==state.player_position[1]+24);
  if(state.inventory.wood>0){harvested=1;break;}
 }
 assert(harvested && state.map[7][24][25]==FP_ICE_GRASS);
 /* Gate the optional fuel collection on floor, clearance, coal and stock. */
 for(int gate=0;gate<4;gate++) {
  ice_shrub_supply_setup(gate==3?6:7,gate!=0,gate!=1,gate==2?4:0);
  FPObservation obs=observe();(void)fp_act(&a,&obs,0,&cfg,&scratch);
  assert(a.goal!=FP_TREE);
 }
 ice_shrub_supply_setup(7,1,1,2);reward=0;int torches=0;
 for(int t=0;t<10;t++) {
  FPObservation obs=observe();int action=fp_act(&a,&obs,reward,&cfg,&scratch);
  reward=craftax_gameplay_step_native(&state,action,craftax_prng_key(1300+t));remove_encounters();
  if(state.inventory.torches>0){torches=1;break;}
 }
 assert(torches && state.inventory.coal==0 && state.inventory.torches==4);
}

int main(void) {
 craftax_wg_init_cell_templates();unknown_approach_localization();ice_diagonal_precast();ice_shrub_supply();troll_chase_retreat_priority();dungeon_diagonal_precast();dungeon_precast_gates();diamonds_before_exit();dark_armour_pocket();vault_precaster_and_retreat_priority();lazy_worlds();codecs();observation_boundary();crafting();workstation_and_survival();transitions();closed_shelter();recovery_water_access();combat();stationary_melee_cooldowns();navigation_and_replacements();arrow_dodge();approaching_melee();projectile_health_tracking();projectile_replacement();magic_and_potions();lighting_and_boss();graveyard_recovery_and_completion();graveyard_torch_facing();elemental_expedition();fire_resupply_and_exit();ice_expedition_and_exit();late_expedition_robustness();fire_preparation();episodes();goal_contracts();goal_regressions();goal_sequence_and_revalidation();
 puts("PASS: codecs, observation boundary, recipes, floor transitions, recovery, combat, navigation, enemy replacements, elemental damage, projectiles, magic, potions, lighting, boss, rewards");
}
