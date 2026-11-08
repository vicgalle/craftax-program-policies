/* Inference-cost benchmark for the Craftax Full program policy.
 *
 * Build against a policy directory D (holding full_policy.c/.h and *.inc):
 *   cc -O3 -std=c11 -ffp-contract=off -Wno-unused-function -I D \
 *      harness/bench.c D/full_policy.c -o bench -lm
 * (parity.py does this for C and C++ policies.)
 *
 * Modes
 *   run    live engine episodes, seeded exactly as the episode evaluator.
 *          One JSON line per episode: action/skill hash, length, floors,
 *          policy nanoseconds (decode + fp_act) per floor.
 *          --dump DIR   write DIR/ep_<n>.act (action,skill bytes per step)
 *          --check DIR  compare against DIR/ep_<n>.act; report the first
 *                       divergent step and stop that episode
 *          --trace FILE write the observation stream for replay
 *          --poison     overwrite FPScratch with 0xA5 before every step
 *   replay FILE [--repeat K]
 *          feed a recorded observation stream to the policy without the
 *          engine; every action and skill must match the recording. Reports
 *          wall ns, instructions and cycles per step (best of K). */
#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "../../engine/craftax_full.c"
#include "full_policy.h"
#include <errno.h>
#ifdef __APPLE__
#include <libproc.h>
#endif
#include <limits.h>
#include <sys/resource.h>
#include <time.h>

#define OBS_BYTES 996
#define STEP_BYTES (OBS_BYTES+6)
#define TRACE_MAGIC 0x46505452u

static uint64_t mix(uint64_t x) {
 x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
#ifdef __APPLE__
static uint64_t now_ns(void){return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);}
static void counters(uint64_t *instructions,uint64_t *cycles) {
 struct rusage_info_v4 ri;
 if(proc_pid_rusage(getpid(),RUSAGE_INFO_V4,(rusage_info_t *)&ri)){*instructions=*cycles=0;return;}
 *instructions=ri.ri_instructions;*cycles=ri.ri_cycles;
}
#else /* instruction and cycle counters are reported as 0 */
static uint64_t now_ns(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000000000u+(uint64_t)ts.tv_nsec;}
static void counters(uint64_t *instructions,uint64_t *cycles){*instructions=*cycles=0;}
#endif
static long integer(const char *p,long lo,long hi) {
 char *end;errno=0;long x=strtol(p,&end,10);
 if(errno||!*p||*end||x<lo||x>hi){fprintf(stderr,"invalid integer: %s\n",p);exit(2);}return x;
}

static int run(uint64_t seed,int first,int count,int steps,const char *dump,const char *check,
               const char *trace_path,int poison) {
 CraftaxState *state=calloc(1,sizeof(*state));FPAgent *agent=malloc(sizeof(*agent));
 FPScratch *scratch=malloc(sizeof(*scratch));uint8_t *expected=malloc(2*(size_t)steps);
 if(!state||!agent||!scratch||!expected){fprintf(stderr,"allocation failed\n");return 1;}
 FILE *trace=NULL;
 if(trace_path && !(trace=fopen(trace_path,"wb"))){perror(trace_path);return 1;}
 craftax_set_lazy_floors(1);craftax_wg_init_cell_templates();
 FPConfig cfg=fp_default_config();int failures=0;
 for(int ep=first;ep<first+count;ep++) {
  uint32_t world_seed=(uint32_t)mix(seed+(uint64_t)ep);uint64_t agent_seed=mix(world_seed);
  Craftax env={0};env.state=state;env.seed=world_seed;craftax_reset_state_from_seed(&env);
  fp_reset(agent,agent_seed);
  char path[PATH_MAX];FILE *out=NULL;long expected_steps=-1;
  if(dump){snprintf(path,sizeof path,"%s/ep_%06d.act",dump,ep);if(!(out=fopen(path,"wb"))){perror(path);return 1;}}
  if(check) {
   snprintf(path,sizeof path,"%s/ep_%06d.act",check,ep);FILE *in=fopen(path,"rb");
   if(!in){perror(path);return 1;}expected_steps=(long)fread(expected,2,(size_t)steps,in)/1;fclose(in);
  }
  long trace_header=-1;
  if(trace) {
   uint32_t head[2]={TRACE_MAGIC,world_seed};fwrite(head,4,2,trace);fwrite(&agent_seed,8,1,trace);
   trace_header=ftell(trace);uint32_t zero=0;fwrite(&zero,4,1,trace);
  }
  uint64_t hash=UINT64_C(1469598103934665603),lvl_ns[9]={0},lvl_steps[9]={0},total_ns=0;
  int t=0,max_level=0,diverged=-1,div_exp[2]={0},div_got[2]={0};float previous_reward=0;double ret=0;
  for(;t<steps && !craftax_is_game_over_native(state);t++) {
   uint8_t record[OBS_BYTES];FPObservation obs;craftax_encode_native_observation(state,record);
   if(poison)memset(scratch,0xA5,sizeof(*scratch));
   int level=state->player_level;
   uint64_t t0=now_ns();
   fp_decode_compact(&obs,record);
   int action=fp_act(agent,&obs,previous_reward,&cfg,scratch);
   uint64_t dt=now_ns()-t0;
   int skill=agent->skill;
   total_ns+=dt;lvl_ns[level]+=dt;lvl_steps[level]++;
   hash=(hash^(uint64_t)(action|skill<<8))*UINT64_C(1099511628211);
   uint8_t pair[2]={(uint8_t)action,(uint8_t)skill};
   if(out)fwrite(pair,1,2,out);
   if(trace){fwrite(record,1,OBS_BYTES,trace);fwrite(&previous_reward,4,1,trace);fwrite(pair,1,2,trace);}
   if(check && (t>=expected_steps || expected[2*t]!=pair[0] || expected[2*t+1]!=pair[1])) {
    diverged=t;div_got[0]=action;div_got[1]=skill;
    if(t<expected_steps){div_exp[0]=expected[2*t];div_exp[1]=expected[2*t+1];}else div_exp[0]=div_exp[1]=-1;
    break;
   }
   CraftaxThreefryKey step_key,step_rng,reset_key;
   craftax_threefry_split(env.rng_key,&env.rng_key,&step_key);
   craftax_threefry_split(step_key,&step_rng,&reset_key);
   previous_reward=craftax_gameplay_step_native(state,action,step_rng);ret+=previous_reward;
   if(state->player_level>max_level)max_level=state->player_level;
  }
  if(check && diverged<0 && t!=expected_steps){diverged=t;div_exp[0]=div_exp[1]=-1;div_got[0]=div_got[1]=-2;}
  if(out)fclose(out);
  if(trace){long end=ftell(trace);uint32_t n=(uint32_t)t;fseek(trace,trace_header,SEEK_SET);fwrite(&n,4,1,trace);fseek(trace,end,SEEK_SET);}
  printf("{\"ep\":%d,\"world_seed\":%u,\"steps\":%d,\"hash\":\"%016llx\",\"max_level\":%d,\"return\":%.4f,\"ns\":%llu",
   ep,world_seed,t,(unsigned long long)hash,max_level,ret,(unsigned long long)total_ns);
  printf(",\"lvl_steps\":[");for(int i=0;i<9;i++)printf("%s%llu",i?",":"",(unsigned long long)lvl_steps[i]);
  printf("],\"lvl_ns\":[");for(int i=0;i<9;i++)printf("%s%llu",i?",":"",(unsigned long long)lvl_ns[i]);printf("]");
  if(check){printf(",\"diverged\":%d",diverged);if(diverged>=0){failures++;printf(",\"expected\":[%d,%d],\"got\":[%d,%d]",div_exp[0],div_exp[1],div_got[0],div_got[1]);}}
  printf("}\n");fflush(stdout);
 }
 if(trace)fclose(trace);
 free(state);free(agent);free(scratch);free(expected);return failures?3:0;
}

static int replay(const char *path,int repeat) {
 FILE *in=fopen(path,"rb");if(!in){perror(path);return 1;}
 fseek(in,0,SEEK_END);long size=ftell(in);fseek(in,0,SEEK_SET);
 uint8_t *data=malloc((size_t)size);if(!data||fread(data,1,(size_t)size,in)!=(size_t)size){fprintf(stderr,"read failed\n");return 1;}
 fclose(in);
 FPAgent *agent=malloc(sizeof(*agent));FPScratch *scratch=malloc(sizeof(*scratch));
 if(!agent||!scratch){fprintf(stderr,"allocation failed\n");return 1;}
 FPConfig cfg=fp_default_config();
 double best_ns=1e300,best_ins=1e300,best_cyc=1e300;long steps=0,episodes=0,mismatches=0;
 for(int k=0;k<repeat;k++) {
  long n=0,ep=0,bad=0;uint64_t i0,c0,i1,c1;counters(&i0,&c0);uint64_t t0=now_ns();
  for(long off=0;off<size;) {
   uint32_t head[2];uint64_t agent_seed;uint32_t count;
   memcpy(head,data+off,8);memcpy(&agent_seed,data+off+8,8);memcpy(&count,data+off+16,4);off+=20;
   if(head[0]!=TRACE_MAGIC){fprintf(stderr,"corrupt trace at %ld\n",off);return 1;}
   fp_reset(agent,agent_seed);
   for(uint32_t t=0;t<count;t++,off+=STEP_BYTES) {
    const uint8_t *p=data+off;float reward;memcpy(&reward,p+OBS_BYTES,4);
    FPObservation obs;fp_decode_compact(&obs,p);
    int action=fp_act(agent,&obs,reward,&cfg,scratch);
    if(action!=p[OBS_BYTES+4] || agent->skill!=p[OBS_BYTES+5]) {
     if(!bad)fprintf(stderr,"replay mismatch: episode %ld step %u expected [%d,%d] got [%d,%d]\n",ep,t,p[OBS_BYTES+4],p[OBS_BYTES+5],action,agent->skill);
     bad++;off+=(long)(count-t)*STEP_BYTES;break; /* later observations no longer follow from this policy */
    }
   }
   n+=count;ep++;
  }
  uint64_t elapsed=now_ns()-t0;counters(&i1,&c1);
  steps=n;episodes=ep;mismatches=bad;
  if(elapsed/(double)n<best_ns)best_ns=elapsed/(double)n;
  if((i1-i0)/(double)n<best_ins)best_ins=(i1-i0)/(double)n;
  if((c1-c0)/(double)n<best_cyc)best_cyc=(c1-c0)/(double)n;
  if(bad)break;
 }
 printf("{\"episodes\":%ld,\"steps\":%ld,\"mismatched_episodes\":%ld,\"repeat\":%d,\"ns_per_step\":%.1f,"
        "\"instructions_per_step\":%.0f,\"cycles_per_step\":%.0f,\"sizeof_agent\":%zu,\"sizeof_scratch\":%zu}\n",
  episodes,steps,mismatches,repeat,best_ns,best_ins,best_cyc,sizeof(FPAgent),sizeof(FPScratch));
 free(data);free(agent);free(scratch);return mismatches?3:0;
}

int main(int argc,char **argv) {
 if(argc<2){fprintf(stderr,"usage: bench run|replay|sizes ...\n");return 2;}
 if(!strcmp(argv[1],"sizes")){printf("{\"sizeof_agent\":%zu,\"sizeof_scratch\":%zu}\n",sizeof(FPAgent),sizeof(FPScratch));return 0;}
 if(!strcmp(argv[1],"replay")) {
  if(argc<3){fprintf(stderr,"usage: bench replay FILE [--repeat K]\n");return 2;}
  int repeat=1;for(int i=3;i+1<argc;i+=2)if(!strcmp(argv[i],"--repeat"))repeat=(int)integer(argv[i+1],1,1000);
  return replay(argv[2],repeat);
 }
 if(strcmp(argv[1],"run")){fprintf(stderr,"unknown mode: %s\n",argv[1]);return 2;}
 uint64_t seed=42;int first=0,count=1,steps=100000,poison=0;const char *dump=NULL,*check=NULL,*trace=NULL;
 for(int i=2;i<argc;i++) {
  const char *arg=argv[i];if(!strcmp(arg,"--poison")){poison=1;continue;}
  if(i+1>=argc){fprintf(stderr,"missing value: %s\n",arg);return 2;}const char *p=argv[++i];
  if(!strcmp(arg,"--seed"))seed=(uint64_t)integer(p,0,INT_MAX);
  else if(!strcmp(arg,"--first"))first=(int)integer(p,0,100000000);
  else if(!strcmp(arg,"--count"))count=(int)integer(p,1,100000000);
  else if(!strcmp(arg,"--steps"))steps=(int)integer(p,1,100000);
  else if(!strcmp(arg,"--dump"))dump=p;
  else if(!strcmp(arg,"--check"))check=p;
  else if(!strcmp(arg,"--trace"))trace=p;
  else {fprintf(stderr,"unknown option: %s\n",arg);return 2;}
 }
 return run(seed,first,count,steps,dump,check,trace,poison);
}
