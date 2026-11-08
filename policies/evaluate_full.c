#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "../engine/craftax_full.c"
#include "full_policy.h"
#include <errno.h>
#include <limits.h>

typedef struct {
 double reward,reward2,length,achievements,successes[67],skills[FP_SKILL_COUNT],levels[9];
 int episodes,survived,min_ach,max_ach,max_level,localization_errors;
} Metrics;
static const char *ach_names[67]={
 "collect_wood","place_table","eat_cow","collect_sapling","collect_drink",
 "make_wood_pickaxe","make_wood_sword","place_plant","defeat_zombie","collect_stone",
 "place_stone","eat_plant","defeat_skeleton","make_stone_pickaxe","make_stone_sword",
 "wake_up","place_furnace","collect_coal","collect_iron","collect_diamond",
 "make_iron_pickaxe","make_iron_sword","make_arrow","make_torch","place_torch",
 "make_diamond_sword","make_iron_armour","make_diamond_armour","enter_gnomish_mines",
 "enter_dungeon","enter_sewers","enter_vault","enter_troll_mines","enter_fire_realm",
 "enter_ice_realm","enter_graveyard","defeat_gnome_warrior","defeat_gnome_archer",
 "defeat_orc_soldier","defeat_orc_mage","defeat_lizard","defeat_kobold","defeat_troll",
 "defeat_deep_thing","defeat_pigman","defeat_fire_elemental","defeat_frost_troll",
 "defeat_ice_elemental","damage_necromancer","defeat_necromancer","eat_bat","eat_snail",
 "find_bow","fire_bow","collect_sapphire","learn_fireball","cast_fireball",
 "learn_iceball","cast_iceball","collect_ruby","make_diamond_pickaxe","open_chest",
 "drink_potion","enchant_sword","enchant_armour","defeat_knight","defeat_archer"};
static uint64_t mix(uint64_t x) {
 x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
static double seconds(void) {
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;
}
static Metrics evaluate(FPConfig cfg,int episodes,uint64_t seed,int steps,int random,
                        FILE *trace,FILE *csv) {
 Metrics m={0};m.episodes=episodes;m.min_ach=67;
 CraftaxState *state=calloc(1,sizeof(*state));FPAgent *agent=malloc(sizeof(*agent));FPScratch *scratch=malloc(sizeof(*scratch));
 if(!state||!agent||!scratch){fprintf(stderr,"allocation failed\n");exit(1);}
 craftax_set_lazy_floors(1);craftax_wg_init_cell_templates();
 for(int ep=0;ep<episodes;ep++) {
  uint32_t world_seed=(uint32_t)mix(seed+(uint64_t)ep);
  Craftax env={0};env.state=state;env.seed=world_seed;craftax_reset_state_from_seed(&env);
  fp_reset(agent,mix(world_seed));uint64_t random_rng=mix(world_seed^UINT64_C(0xade1));
  int t=0,max_level=0,visited[9]={1},offset_r[9]={24},offset_c[9]={24},anchored[9]={1};
  double total=0;float previous_reward=0;
  if(trace && ep==0)fprintf(trace,"step,action,skill,goal,reward,health,food,drink,energy,mana,level,row,col,agent_row,agent_col,achievements,wood,stone,coal,iron,diamond,pickaxe,sword,armour,clear\n");
  for(;t<steps && !craftax_is_game_over_native(state);t++) {
   uint8_t record[996];FPObservation obs;craftax_encode_native_observation(state,record);fp_decode_compact(&obs,record);
   int action;
   if(random){random_rng=mix(random_rng);action=(int)(random_rng%43);}
   else {
    action=fp_act(agent,&obs,previous_reward,&cfg,scratch);m.skills[agent->skill]++;
    /* Hidden coordinates are used only here, to audit the independently
     * maintained local frame. They are never passed to fp_act. */
    int level=state->player_level;
    if(!anchored[level]) {
     offset_r[level]=agent->observed_r-state->player_position[0];
     offset_c[level]=agent->observed_c-state->player_position[1];anchored[level]=1;
    }
   }
   CraftaxThreefryKey step_key,step_rng,reset_key;
   craftax_threefry_split(env.rng_key,&env.rng_key,&step_key);
   craftax_threefry_split(step_key,&step_rng,&reset_key);
   previous_reward=craftax_gameplay_step_native(state,action,step_rng);total+=previous_reward;
   int level=state->player_level;visited[level]=1;if(level>max_level)max_level=level;
   if(!random && level==agent->level) {
    if(agent->r!=state->player_position[0]+offset_r[level] || agent->c!=state->player_position[1]+offset_c[level])m.localization_errors++;
   }
   if(trace && ep==0) {
    int ach=0,arm=0;for(int i=0;i<67;i++)ach+=state->achievements[i];for(int i=0;i<4;i++)arm+=state->inventory.armour[i];
    fprintf(trace,"%d,%d,%s,%d,%.6f,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
     t+1,action,random?"random":fp_skill_name(agent->skill),agent->goal,previous_reward,state->player_health,
     state->player_food,state->player_drink,state->player_energy,state->player_mana,level,state->player_position[0],state->player_position[1],
     agent->r,agent->c,ach,state->inventory.wood,state->inventory.stone,state->inventory.coal,state->inventory.iron,
     state->inventory.diamond,state->inventory.pickaxe,state->inventory.sword,arm,state->monsters_killed[level]>=8);
   }
  }
  int ach=0;for(int i=0;i<67;i++){ach+=state->achievements[i];m.successes[i]+=state->achievements[i];}
  for(int i=0;i<9;i++)m.levels[i]+=visited[i];
  m.reward+=total;m.reward2+=total*total;m.length+=t;m.achievements+=ach;
  m.survived+=state->player_health>0 && t==steps;if(ach<m.min_ach)m.min_ach=ach;if(ach>m.max_ach)m.max_ach=ach;
  if(max_level>m.max_level)m.max_level=max_level;
  if(csv)fprintf(csv,"%d,%u,%.6f,%d,%d,%.4f,%d,%d,%d\n",ep,world_seed,total,ach,t,state->player_health,max_level,state->boss_progress,state->achievements[49]);
 }
 free(state);free(agent);free(scratch);return m;
}
static void print_config(FPConfig c) {
 printf("{\"drink_at\":%d,\"eat_at\":%d,\"sleep_at\":%d,\"wood_stock\":%d,\"torch_stock\":%d,\"armour_pieces\":%d,"
  "\"prepare_steps\":%d,\"mine_steps\":%d,\"str_first\":%d,\"retreat_health\":%d,\"danger_cost\":%.4g,\"revisit_cost\":%.4g,\"frontier_bonus\":%.4g}",
  c.drink_at,c.eat_at,c.sleep_at,c.wood_stock,c.torch_stock,c.armour_pieces,c.prepare_steps,c.mine_steps,c.str_first,c.retreat_health,c.danger_cost,c.revisit_cost,c.frontier_bonus);
}
static void print_metrics(Metrics m,FPConfig cfg,uint64_t seed,int steps,int random,double elapsed) {
 double mean=m.reward/m.episodes,var=fmax(0,(m.reward2-m.reward*m.reward/m.episodes)/fmax(1,m.episodes-1));
 printf("{\n  \"policy\":\"%s\",\"seed\":%llu,\"episodes\":%d,\"horizon\":%d,\n",random?"random":"program",(unsigned long long)seed,m.episodes,steps);
 printf("  \"mean_return\":%.6f,\"return_stderr\":%.6f,\"mean_achievements\":%.6f,\n",mean,sqrt(var/m.episodes),m.achievements/m.episodes);
 printf("  \"mean_length\":%.3f,\"survival_rate\":%.6f,\"min_achievements\":%d,\"max_achievements\":%d,\"max_level\":%d,\n",m.length/m.episodes,(double)m.survived/m.episodes,m.min_ach,m.max_ach,m.max_level);
 printf("  \"localization_errors\":%d,\"seconds\":%.3f,\"steps_per_second\":%.0f,\n",m.localization_errors,elapsed,m.length/elapsed);
 printf("  \"config\":");print_config(cfg);printf(",\n  \"achievement_rates\":{");
 for(int i=0;i<67;i++)printf("%s\n    \"%s\":%.6f",i?",":"",ach_names[i],m.successes[i]/m.episodes);
 printf("\n  },\n  \"floor_reach_rates\":[");for(int i=0;i<9;i++)printf("%s%.6f",i?",":"",m.levels[i]/m.episodes);
 printf("],\n  \"skill_steps\":{");for(int i=0;i<FP_SKILL_COUNT;i++)printf("%s\"%s\":%.0f",i?",":"",fp_skill_name(i),m.skills[i]);printf("}\n}\n");
}
static int integer(const char *p,int lo,int hi) {
 char *end;errno=0;long x=strtol(p,&end,10);if(errno||!*p||*end||x<lo||x>hi){fprintf(stderr,"invalid integer: %s\n",p);exit(2);}return (int)x;
}
static float real(const char *p,float lo,float hi) {
 char *end;errno=0;float x=strtof(p,&end);if(errno||!*p||*end||!isfinite(x)||x<lo||x>hi){fprintf(stderr,"invalid real: %s\n",p);exit(2);}return x;
}
static int clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
int main(int argc,char **argv) {
 FPConfig cfg=fp_default_config();int episodes=64,steps=100000,random=0,tune=0,trials=24;uint64_t seed=42;
 const char *trace_path=NULL,*csv_path=NULL;
 for(int i=1;i<argc;i++) {
  const char *arg=argv[i];if(!strcmp(arg,"eval"))continue;if(!strcmp(arg,"tune")){tune=1;continue;}
  if(!strcmp(arg,"--random")){random=1;continue;}
  if(!strcmp(arg,"--help")){puts("usage: craftax_full_policy [eval|tune] [--episodes N] [--seed N] [--steps N] [--random] [--trials N] [--trace FILE] [--episode-csv FILE]\nConfiguration flags match policy/full_default_config.json with underscores replaced by hyphens.");return 0;}
  if(i+1>=argc){fprintf(stderr,"missing value: %s\n",arg);return 2;}const char *p=argv[++i];
  if(!strcmp(arg,"--episodes"))episodes=integer(p,1,1000000);
  else if(!strcmp(arg,"--seed"))seed=(uint64_t)integer(p,0,INT_MAX);
  else if(!strcmp(arg,"--steps"))steps=integer(p,1,100000);
  else if(!strcmp(arg,"--trials"))trials=integer(p,1,100000);
  else if(!strcmp(arg,"--trace"))trace_path=p;
  else if(!strcmp(arg,"--episode-csv"))csv_path=p;
  else if(!strcmp(arg,"--drink-at"))cfg.drink_at=integer(p,0,16);
  else if(!strcmp(arg,"--eat-at"))cfg.eat_at=integer(p,0,16);
  else if(!strcmp(arg,"--sleep-at"))cfg.sleep_at=integer(p,0,16);
  else if(!strcmp(arg,"--wood-stock"))cfg.wood_stock=integer(p,0,60);
  else if(!strcmp(arg,"--torch-stock"))cfg.torch_stock=integer(p,0,60);
  else if(!strcmp(arg,"--armour-pieces"))cfg.armour_pieces=integer(p,0,4);
  else if(!strcmp(arg,"--prepare-steps"))cfg.prepare_steps=integer(p,0,10000);
  else if(!strcmp(arg,"--mine-steps"))cfg.mine_steps=integer(p,0,10000);
  else if(!strcmp(arg,"--str-first"))cfg.str_first=integer(p,0,1);
  else if(!strcmp(arg,"--retreat-health"))cfg.retreat_health=integer(p,0,12);
  else if(!strcmp(arg,"--danger-cost"))cfg.danger_cost=real(p,0,100);
  else if(!strcmp(arg,"--revisit-cost"))cfg.revisit_cost=real(p,0,30);
  else if(!strcmp(arg,"--frontier-bonus"))cfg.frontier_bonus=real(p,0.1f,100);
  else {fprintf(stderr,"unknown option: %s\n",arg);return 2;}
 }
 if(tune) {
  uint64_t rng=mix(seed^UINT64_C(0xd1b54a32d192ed03));double best=-1e30;FPConfig incumbent=cfg;
  for(int t=0;t<trials;t++) {
   FPConfig c=incumbent;if(t) {
    rng=mix(rng);int dim=(int)(rng%13);rng=mix(rng);int sign=(rng&1)?1:-1;
    switch(dim) {
     case 0:c.drink_at=clamp(c.drink_at+sign,2,9);break;
     case 1:c.eat_at=clamp(c.eat_at+sign,2,9);break;
     case 2:c.sleep_at=clamp(c.sleep_at+sign,1,8);break;
     case 3:c.wood_stock=clamp(c.wood_stock+4*sign,4,32);break;
     case 4:c.torch_stock=clamp(c.torch_stock+4*sign,4,32);break;
     case 5:c.armour_pieces=clamp(c.armour_pieces+sign,0,4);break;
     case 6:c.prepare_steps=clamp(c.prepare_steps+300*sign,200,3000);break;
     case 7:c.mine_steps=clamp(c.mine_steps+400*sign,400,3600);break;
     case 8:c.str_first=!c.str_first;break;
     case 9:c.retreat_health=clamp(c.retreat_health+sign,0,7);break;
     case 10:c.danger_cost=fmaxf(0,c.danger_cost+sign*2);break;
     case 11:c.revisit_cost=fmaxf(0,c.revisit_cost+sign*0.5f);break;
     case 12:c.frontier_bonus=fmaxf(0.5f,c.frontier_bonus+sign*0.5f);break;
    }
   }
   double start=seconds();Metrics m=evaluate(c,episodes,seed,steps,0,NULL,NULL);double score=m.reward/episodes;
   if(score>best){best=score;incumbent=c;}
   printf("{\"trial\":%d,\"mean_return\":%.6f,\"best_return\":%.6f,\"seconds\":%.3f,\"config\":",t,score,best,seconds()-start);print_config(c);printf("}\n");fflush(stdout);
  }cfg=incumbent;
 }
 FILE *trace=trace_path?fopen(trace_path,"w"):NULL,*csv=csv_path?fopen(csv_path,"w"):NULL;
 if((trace_path&&!trace)||(csv_path&&!csv)){perror("output file");return 1;}
 if(csv)fprintf(csv,"episode,world_seed,return,achievements,length,health,max_level,boss_progress,defeat_necromancer\n");
 double start=seconds();Metrics m=evaluate(cfg,episodes,seed,steps,random,trace,csv);print_metrics(m,cfg,seed,steps,random,seconds()-start);
 if(trace)fclose(trace);if(csv)fclose(csv);return 0;
}
