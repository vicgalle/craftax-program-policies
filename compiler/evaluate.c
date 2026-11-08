/* Native engine stepping with an independent sequence monitor. */
#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "../engine/craftax_full.c"
#include "full_policy.h"
#include "monitor.h"
#include <errno.h>
#include <limits.h>

static uint64_t mix(uint64_t x){
 x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
static int integer(const char *p,int lo,int hi){
 char *end;errno=0;long x=strtol(p,&end,10);
 if(errno || !*p || *end || x<lo || x>hi){fprintf(stderr,"invalid integer: %s\n",p);exit(2);}return (int)x;
}
static SQState state(const CraftaxState *s){
 const CraftaxInventory *i=&s->inventory;
 SQState x={.level=s->player_level,.health=s->player_health,.stock={i->wood,i->stone,i->coal,i->iron,i->diamond}};return x;
}
int main(int argc,char **argv){
 int episodes=8,steps=100000;uint64_t seed=1900000000;
 const char *csv_path=NULL,*goal_path=NULL,*trace_path=NULL;
 if(!(argc&1)){fprintf(stderr,"arguments must be key value pairs\n");return 2;}
 for(int i=1;i+1<argc;i+=2){
  if(!strcmp(argv[i],"--episodes"))episodes=integer(argv[i+1],1,100000);
  else if(!strcmp(argv[i],"--seed"))seed=integer(argv[i+1],0,INT_MAX);
  else if(!strcmp(argv[i],"--steps"))steps=integer(argv[i+1],1,100000);
  else if(!strcmp(argv[i],"--csv"))csv_path=argv[i+1];
  else if(!strcmp(argv[i],"--goal"))goal_path=argv[i+1];
  else if(!strcmp(argv[i],"--trace"))trace_path=argv[i+1];
  else {fprintf(stderr,"unknown option: %s\n",argv[i]);return 2;}
 }
 SQGoal goal={0};if(!goal_path || !sq_load(goal_path,&goal)){fprintf(stderr,"invalid goal\n");return 2;}
 FILE *csv=csv_path?fopen(csv_path,"w"):stdout,*trace=trace_path?fopen(trace_path,"w"):NULL;
 if(!csv || (trace_path&&!trace)){perror("output");return 1;}
 fprintf(csv,"episode,world_seed,success,violation,completed,stages,length,max_level,end_level,health,wood,stone,coal,iron,diamond,return,action_hash,policy_stage,floor_changes,noops,ascends,descends");
 for(int j=0;j<8;j++)fprintf(csv,",stage%d_at",j);fprintf(csv,"\n");
 if(trace)fprintf(trace,"step,action,skill,level,health,wood,stone,coal,iron,diamond,monitor_stage,policy_stage,violation,rule\n");
 CraftaxState *s=calloc(1,sizeof(*s));FPAgent *a=malloc(sizeof(*a));FPScratch *scratch=malloc(sizeof(*scratch));
 if(!s || !a || !scratch)return 1;
 FPConfig cfg=fp_default_config();craftax_set_lazy_floors(1);craftax_wg_init_cell_templates();
 for(int ep=0;ep<episodes;ep++){
  uint32_t world=(uint32_t)mix(seed+(uint64_t)ep);Craftax env={0};env.state=s;env.seed=world;
  craftax_reset_state_from_seed(&env);fp_reset(a,mix(world));
  SQMonitor monitor;sq_reset(&monitor);SQState v=state(s);sq_check(&goal,&monitor,&v,0);
  int t=0,max_level=0,changes=0,noops=0,ascends=0,descends=0;float reward=0;double total=0;uint64_t hash=0;
  for(;t<steps && !craftax_is_game_over_native(s) && !monitor.success && !monitor.violation;t++){
   uint8_t record[996];FPObservation obs;craftax_encode_native_observation(s,record);fp_decode_compact(&obs,record);
   int before[FP_RULES_MAX];if(trace&&ep==0)memcpy(before,a->rule_fires,sizeof before);
   int action=fp_act(a,&obs,reward,&cfg,scratch);noops+=action==FP_NOOP;ascends+=action==FP_ASCEND;descends+=action==FP_DESCEND;
   hash=mix(hash^(uint64_t)(action+64*a->skill));
   CraftaxThreefryKey step_key,step_rng,reset_key;
   craftax_threefry_split(env.rng_key,&env.rng_key,&step_key);craftax_threefry_split(step_key,&step_rng,&reset_key);
   reward=craftax_gameplay_step_native(s,action,step_rng);total+=reward;
   if(s->player_level>max_level)max_level=s->player_level;
   changes+=v.level!=s->player_level;v=state(s);sq_check(&goal,&monitor,&v,t+1);
   if(trace&&ep==0){
    int rule=-1;for(int r=0;r<FP_RULES_MAX;r++)if(a->rule_fires[r]!=before[r])rule=r;
    fprintf(trace,"%d,%d,%d,%d,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",t+1,action,a->skill,v.level,v.health,
     v.stock[0],v.stock[1],v.stock[2],v.stock[3],v.stock[4],monitor.stage,a->intent.stage,monitor.violation,rule);
   }
  }
  fprintf(csv,"%d,%u,%d,%d,%d,%d,%d,%d,%d,%.4f,%d,%d,%d,%d,%d,%.6f,%016llx,%d,%d,%d,%d,%d",
   ep,world,monitor.success,monitor.violation,monitor.stage,goal.n,t,max_level,v.level,v.health,
   v.stock[0],v.stock[1],v.stock[2],v.stock[3],v.stock[4],total,(unsigned long long)hash,a->intent.stage,changes,noops,ascends,descends);
  for(int j=0;j<8;j++)fprintf(csv,",%d",monitor.at[j]);fprintf(csv,"\n");fflush(csv);
 }
 free(s);free(a);free(scratch);if(csv!=stdout)fclose(csv);if(trace)fclose(trace);return 0;
}
