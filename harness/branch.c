/* Savestate branching harness for the Full controller.
 *
 * Same episode loop, instrumentation and CSV outputs as autopsy.c, plus:
 *
 *  capture  Play episodes from reset and write a snapshot the first time each
 *           requested macro-state trigger fires (pre-action, so a resumed
 *           policy chooses the very next action itself):
 *             arrive:K  first step on floor K
 *             clear:K   first step on floor K with 8 kills there
 *             ready:K   first step on floor K, cleared, down ladder in memory
 *             up:K      first step on floor K-1 after first climbing from K
 *           Repeated goal-boundary triggers fire on EVERY occurrence (subject to
 *           --capture-prob P and --capture-max M per episode and trigger), so a
 *           bank can hold several invocations of one module per world:
 *             leave:K   first step on floor K-1 after any climb from K
 *                       (start of an excursion away from K)
 *             enter:K   first step on floor K after any arrival (descent or climb)
 *  goals    Every run is labelled by the explicit goal hierarchy of hhmm.h (ADVANCE ->
 *           CLEAR/FIND_EXIT/DEPART/EXCURSION -> HEAL -> FIGHT), read from controller
 *           memory without changing any action. --log-goals writes one row per goal
 *           invocation (PREFIX_invocations.csv). Goal-entry triggers gheal:K and
 *           gfight:K (repeated, sampled) capture the step after a HEAL/FIGHT
 *           invocation starts on floor K, and the condition goalend:NAME (for
 *           --handoff and --stop) holds once the NAME invocation that was open at
 *           the snapshot has returned (immediately if none was open).
 *  resume   Load snapshots and continue them with the policy compiled into
 *           this binary. With -DHYBRID, a second (incumbent) policy is linked
 *           in under the inc_ prefix (hybrid_inc.c) and takes over at the first
 *           step where the --handoff condition holds: the framework's local
 *           comparison (candidate for one invocation, incumbent afterwards).
 *           Conditions ('|' = any): return:K (on floor K after having been on
 *           another floor since the snapshot), change (any floor change),
 *           steps:N (N steps after the snapshot). Replica 0 continues with the saved engine RNG and
 *           reproduces the uninterrupted episode exactly; replica r>0 rekeys
 *           the future engine and policy RNG (same world and maps, different
 *           dice), giving paired counterfactual replicas across policies.
 *
 * A snapshot holds the complete CraftaxState (lazy floor keys included), the
 * future engine RNG key, and the FPAgent bytes. FPScratch is transient (every
 * field is rebuilt within a step before it is read). A candidate policy may
 * APPEND fields to FPAgent: missing trailing bytes are zero-filled on resume.
 * Any other layout change makes old banks invalid (the loader checks sizes).
 *
 * Oracles (--oracle) are DIAGNOSTIC engine interventions for sizing levers
 * (free food, no damage on revisited floors, ...). They never enter policy
 * evaluation and never touch policy code; the policy still sees only public
 * observations. */
#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "craftax_full_autopsy.c"
#include "full_policy.h"
#include "hhmm.h"
#include <zlib.h>
#include <stddef.h>

#ifndef POLICY_TAG
#define POLICY_TAG 0
#endif

static uint64_t mix(uint64_t x) {
 x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
enum { RING=100, MAX_TRIG=32 };
enum { T_ARRIVE, T_CLEAR, T_READY, T_UP, T_LEAVE, T_ENTER, T_GHEAL, T_GFIGHT, T_KINDS };
static const char *trig_name[]={"arrive","clear","ready","up","leave","enter","gheal","gfight"};
static int trig_repeated(int kind) { return kind>=T_LEAVE; }
enum { O_FOOD=1, O_DRINK=2, O_ENERGY=4, O_DEX5=8, O_REVISIT_SAFE=16, O_ARMOUR=32, O_LIGHT=64,
       O_NOMELEE=128, O_NOPROJ=256, O_NOSTARVE=512, O_FULLHEAL=1024, O_MAXSTATS=2048,
       O_ENCHMATCH=4096, O_MANA=8192, O_FREEKILLS=16384, O_DIA3=32768, O_DIA6=65536, O_SAP3=131072, O_RUBY3=262144, O_NOOPS=19 };
/* enchmatch: every armour piece held is fire-enchanted on Fire (6) and ice-enchanted on Ice (7)
 * (perfect enchant logistics); sap3/ruby3: +3 sapphires/rubies at the snapshot; dia3/dia6: +3/+6
 * diamonds at the snapshot (the policy spends them
 * by its own crafting rules); mana: full mana every step; freekills: a floor counts 8 kills on
 * arrival (no clearance requirement). Each name takes an optional @MASK of floors (default
 * --oracle-floors), e.g. nomelee@0x80,revisit_safe. */
static const char *oracle_names[]={"food","drink","energy","dex5","revisit_safe","armour","light","nomelee","noproj","nostarve","fullheal","maxstats","enchmatch","mana","freekills","dia3","dia6","sap3","ruby3",NULL};

typedef struct {
 char magic[8];
 uint32_t state_size,agent_size,comp_size,trig_kind,trig_level;
 int32_t ep; uint32_t world_seed; int32_t t; float previous_reward; double total;
 int32_t max_level,arrive[9],cleared[9],exit_known[9],exit_ready[9];
 uint64_t hash,arrive_hash[9],policy_tag;
 CraftaxThreefryKey rng_key;
 int32_t reserved[16];
} SnapHeader;

typedef struct { int t,level,action,skill,food,drink,energy,mana,sleeping,resting,src,type,near_melee,near_ranged; float health,dmg[4]; } Tick;

static int near_count(const CraftaxState *s,int ranged,int radius) {
 int L=s->player_level,n=0,pr=s->player_position[0],pc=s->player_position[1];
 if(ranged){for(int i=0;i<2;i++)if(s->ranged_mobs.mask[L][i]){int d=abs(s->ranged_mobs.position[L][i][0]-pr)+abs(s->ranged_mobs.position[L][i][1]-pc);if(d<=radius)n++;}}
 else{for(int i=0;i<3;i++)if(s->melee_mobs.mask[L][i]){int d=abs(s->melee_mobs.position[L][i][0]-pr)+abs(s->melee_mobs.position[L][i][1]-pc);if(d<=radius)n++;}}
 return n;
}

/* ---- optional per-step trace (diagnosis of single episodes) ---- */
static FILE *g_trace=NULL;static const char *g_trace_prefix=NULL;
enum { SNAPS=60,SNAP_R=5,SNAP_C=7,SNAP_LEN=2048 };
static char g_snap[SNAPS][SNAP_LEN];static int g_nsnap=0;
static char block_char(int b) {
 static const char *chars="?#.~STw,cidBF:%pPW M^srCo;'=YyEeNGGGn";
 return b>=0 && b<(int)strlen(chars)?chars[b]:'?';
}
static void trace_open(int ep,int rep) {
 if(!g_trace_prefix)return;char path[1024];
 snprintf(path,sizeof path,"%s_ep%d_r%d_steps.csv",g_trace_prefix,ep,rep);g_trace=fopen(path,"w");
 if(!g_trace){perror(path);exit(1);}
 fprintf(g_trace,"t,level,row,col,dir,action,skill,goal,health,food,drink,energy,mana,sleeping,kills");
 for(int i=0;i<3;i++)fprintf(g_trace,",m%d_type,m%d_dr,m%d_dc,m%d_hp,m%d_cd",i,i,i,i,i);
 for(int i=0;i<2;i++)fprintf(g_trace,",r%d_type,r%d_dr,r%d_dc,r%d_hp,r%d_cd",i,i,i,i,i);
 for(int i=0;i<3;i++)fprintf(g_trace,",p%d_type,p%d_dr,p%d_dc",i,i,i);
 fprintf(g_trace,",dmg_melee,dmg_proj,dmg_intr\n");g_nsnap=0;
}
static void trace_step(const CraftaxState *s,int t,int action,const FPAgent *a) {
 if(!g_trace)return;int L=s->player_level,pr=s->player_position[0],pc=s->player_position[1];
 fprintf(g_trace,"%d,%d,%d,%d,%d,%d,%s,%d,%.2f,%d,%d,%d,%d,%d,%d",t,L,pr,pc,s->player_direction,action,fp_skill_name(a->skill),a->goal,
  s->player_health,s->player_food,s->player_drink,s->player_energy,s->player_mana,s->is_sleeping,s->monsters_killed[L]);
 for(int i=0;i<3;i++){if(s->melee_mobs.mask[L][i])fprintf(g_trace,",%d,%d,%d,%.1f,%d",s->melee_mobs.type_id[L][i],s->melee_mobs.position[L][i][0]-pr,s->melee_mobs.position[L][i][1]-pc,s->melee_mobs.health[L][i],s->melee_mobs.attack_cooldown[L][i]);else fprintf(g_trace,",-1,,,,");}
 for(int i=0;i<2;i++){if(s->ranged_mobs.mask[L][i])fprintf(g_trace,",%d,%d,%d,%.1f,%d",s->ranged_mobs.type_id[L][i],s->ranged_mobs.position[L][i][0]-pr,s->ranged_mobs.position[L][i][1]-pc,s->ranged_mobs.health[L][i],s->ranged_mobs.attack_cooldown[L][i]);else fprintf(g_trace,",-1,,,,");}
 for(int i=0;i<3;i++){if(s->mob_projectiles.mask[L][i])fprintf(g_trace,",%d,%d,%d",s->mob_projectiles.type_id[L][i],s->mob_projectiles.position[L][i][0]-pr,s->mob_projectiles.position[L][i][1]-pc);else fprintf(g_trace,",-1,,");}
 fprintf(g_trace,",%.2f,%.2f,%.2f\n",g_dmg[0],g_dmg[1],g_dmg[2]);
 char *o=g_snap[g_nsnap%SNAPS];int n=0;g_nsnap++;
 static const char *dirs="?LRUD";
 n+=snprintf(o+n,SNAP_LEN-n,"t=%d L%d hp=%.1f food=%d drink=%d energy=%d mana=%d action=%d skill=%s facing=%c kills=%d\n",
  t,L,s->player_health,s->player_food,s->player_drink,s->player_energy,s->player_mana,action,fp_skill_name(a->skill),
  dirs[s->player_direction>=0&&s->player_direction<5?s->player_direction:0],s->monsters_killed[L]);
 for(int dr=-SNAP_R;dr<=SNAP_R;dr++){
  for(int dc=-SNAP_C;dc<=SNAP_C;dc++){
   int r=pr+dr,c=pc+dc;char ch=' ';
   if(r<0||c<0||r>=CRAFTAX_MAP_SIZE||c>=CRAFTAX_MAP_SIZE)ch='#';
   else {
    ch=block_char(s->map[L][r][c]);int it=s->item_map[L][r][c];
    if(it==1)ch='!';else if(it==2||it==4)ch='>';else if(it==3)ch='<';
    if(s->light_map[L][r][c]<=12 && ch!='#')ch=(ch==' '?' ':'`');
    for(int i=0;i<3;i++)if(s->passive_mobs.mask[L][i]&&s->passive_mobs.position[L][i][0]==r&&s->passive_mobs.position[L][i][1]==c)ch='a';
    for(int i=0;i<3;i++)if(s->mob_projectiles.mask[L][i]&&s->mob_projectiles.position[L][i][0]==r&&s->mob_projectiles.position[L][i][1]==c)ch='*';
    for(int i=0;i<3;i++)if(s->player_projectiles.mask[L][i]&&s->player_projectiles.position[L][i][0]==r&&s->player_projectiles.position[L][i][1]==c)ch='+';
    for(int i=0;i<2;i++)if(s->ranged_mobs.mask[L][i]&&s->ranged_mobs.position[L][i][0]==r&&s->ranged_mobs.position[L][i][1]==c)ch='R';
    for(int i=0;i<3;i++)if(s->melee_mobs.mask[L][i]&&s->melee_mobs.position[L][i][0]==r&&s->melee_mobs.position[L][i][1]==c)ch='X';
    if(dr==0&&dc==0)ch='@';
   }
   o[n++]=ch;
  }
  o[n++]='\n';
 }
 for(int i=0;i<3;i++)if(s->melee_mobs.mask[L][i])n+=snprintf(o+n,SNAP_LEN-n,"  X melee type %d at (%+d,%+d) hp %.1f cooldown %d\n",s->melee_mobs.type_id[L][i],s->melee_mobs.position[L][i][0]-pr,s->melee_mobs.position[L][i][1]-pc,s->melee_mobs.health[L][i],s->melee_mobs.attack_cooldown[L][i]);
 for(int i=0;i<2;i++)if(s->ranged_mobs.mask[L][i])n+=snprintf(o+n,SNAP_LEN-n,"  R ranged type %d at (%+d,%+d) hp %.1f cooldown %d\n",s->ranged_mobs.type_id[L][i],s->ranged_mobs.position[L][i][0]-pr,s->ranged_mobs.position[L][i][1]-pc,s->ranged_mobs.health[L][i],s->ranged_mobs.attack_cooldown[L][i]);
 if(g_dmg[0]+g_dmg[1]+g_dmg[2]>0)n+=snprintf(o+n,SNAP_LEN-n,"  damage this step: melee %.2f projectile %.2f intrinsic %.2f\n",g_dmg[0],g_dmg[1],g_dmg[2]);
 o[n]=0;
}
static void trace_close(int ep,int rep) {
 if(!g_trace)return;fclose(g_trace);g_trace=NULL;char path[1024];
 snprintf(path,sizeof path,"%s_ep%d_r%d_snaps.txt",g_trace_prefix,ep,rep);FILE *f=fopen(path,"w");if(!f){perror(path);exit(1);}
 fprintf(f,"Last %d steps. Legend: @ player, X melee mob, R ranged mob, * mob projectile, + player projectile, a passive,\n"
  "> ladder down, < ladder up, ! torch, ` unlit cell (invisible to the agent), # out of bounds. Blocks: . grass ~ water S stone\n"
  "T tree , path c coal i iron d diamond B table F furnace : sand %% lava W wall M moss ^ stalagmite s sapphire r ruby\n"
  "C chest o fountain ; fire grass ' ice grass = gravel E fire table e ice table G grave N necromancer.\n"
  "Actions: 0 noop 1 left 2 right 3 up 4 down 5 do 6 sleep 7 stone 17 rest 18 descend 19 ascend 24 shoot 26 fireball 27 iceball.\n\n",SNAPS);
 int first=g_nsnap>SNAPS?g_nsnap-SNAPS:0;
 for(int i=first;i<g_nsnap;i++)fprintf(f,"%s\n",g_snap[i%SNAPS]);
 fclose(f);
}

/* ---- snapshots ---- */
typedef struct { int kind,level; FILE *f; int fired,occ,taken; } Trigger;
static double g_capture_prob=1.0;static int g_capture_max=1<<30;
/* Deterministic subsample of repeated invocations: keep occurrence occ of trigger i in world ws
 * with probability g_capture_prob (independent of the policy's dice). */
static int sample_keep(uint32_t ws,int i,int occ) {
 if(g_capture_prob>=1.0)return 1;
 uint64_t z=mix(((uint64_t)ws<<24)^((uint64_t)i<<16)^(uint64_t)occ^UINT64_C(0xb4c0ffee00000000));
 return (double)(z>>11)*(1.0/9007199254740992.0)<g_capture_prob;
}

/* ---- hybrid handoff (local comparison) ---- */
enum { H_RETURN, H_CHANGE, H_STEPS, H_GOALEND, H_RUNTIMEEND };
static int g_handoff_kind[8],g_handoff_arg[8],g_handoff_n=0;
/* --stop COND (same syntax): end the episode where COND first holds (partition-boundary checks). */
static int g_stop_kind[8],g_stop_arg[8],g_stop_n=0,g_stop_target[8],g_handoff_target[8];
static void *g_inc_scratch=NULL;static int g_runtime_handoff=0;
#ifdef HYBRID
int inc_hybrid_act(void *agent,const uint8_t *record,float previous_reward,void *scratch);
size_t inc_hybrid_agent_size(void);
size_t inc_hybrid_scratch_size(void);
size_t inc_hybrid_runtime_offset(void);
size_t inc_hybrid_runtime_size(void);
void inc_hybrid_observe(void *agent,const void *observation,float reward,void *scratch);
int inc_hybrid_decide(void *agent,const void *observation,void *scratch);
#endif
static void parse_cond(const char *spec,int *kind,int *arg,int *n) {
 char buf[256];snprintf(buf,sizeof buf,"%s",spec);
 for(char *tok=strtok(buf,"|");tok;tok=strtok(NULL,"|")) {
  if(*n>=8){fprintf(stderr,"too many conditions\n");exit(2);}
  int v;
  if(sscanf(tok,"return:%d",&v)==1){kind[*n]=H_RETURN;arg[(*n)++]=v;}
  else if(sscanf(tok,"steps:%d",&v)==1){kind[*n]=H_STEPS;arg[(*n)++]=v;}
  else if(!strcmp(tok,"change")){kind[*n]=H_CHANGE;arg[(*n)++]=0;}
  else if(!strncmp(tok,"goalend:",8) && hg_goal_index(tok+8)>=0){kind[*n]=H_GOALEND;arg[(*n)++]=hg_goal_index(tok+8);}
  else if(!strcmp(tok,"runtimeend:excursion")) {
#ifdef FP_RUNTIME_ABI
   kind[*n]=H_RUNTIMEEND;arg[(*n)++]=FP_RG_EXCURSION;g_runtime_handoff=1;
#else
   fprintf(stderr,"runtimeend needs a policy with FP_RUNTIME_ABI\n");exit(2);
#endif
  }
  else {fprintf(stderr,"bad condition %s\n",tok);exit(2);}
 }
}
static void parse_handoff(const char *spec) { parse_cond(spec,g_handoff_kind,g_handoff_arg,&g_handoff_n); }
/* target[i]: id of the goal invocation a goalend condition waits for (-1: none was open). */
static int open_id(const HGState *g,int id) { for(int d=0;d<g->depth;d++)if(g->f[d].id==id)return 1; return 0; }
static int cond_hit(const int *kind,const int *arg,const int *target,int n,int L,int start_level,unsigned visited,int t,int snap_t,const HGState *g) {
 for(int i=0;i<n;i++) {
  int a=arg[i],k=kind[i];
  if((k==H_RETURN && L==a && (visited&~(1u<<a))) || (k==H_CHANGE && L!=start_level) || (k==H_STEPS && t-snap_t>=a))return 1;
  if(k==H_GOALEND && (target[i]<0 || !open_id(g,target[i])))return 1;
 }
 return 0;
}
static void set_targets(const int *kind,const int *arg,int *target,int n,const HGState *g) {
 for(int i=0;i<n;i++)if(kind[i]==H_GOALEND){int d=hg_find(g,arg[i]);target[i]=d>=0?g->f[d].id:-1;}
}

#ifdef FP_RUNTIME_ABI
static void runtime_targets(const int *kind,const int *arg,int *target,int n,const FPAgent *a) {
 for(int i=0;i<n;i++)if(kind[i]==H_RUNTIMEEND) {
  target[i]=-1;
  for(int d=a->runtime.depth-1;d>=0;d--)if(a->runtime.frame[d].kind==arg[i]){target[i]=a->runtime.frame[d].id;break;}
 }
}
static int runtime_hit(const int *kind,const int *target,int n,const FPAgent *a) {
 for(int i=0;i<n;i++)if(kind[i]==H_RUNTIMEEND) {
  int open=0;for(int d=0;d<a->runtime.depth;d++)if(a->runtime.frame[d].id==target[i])open=1;
  if(target[i]<0 || !open)return 1;
 }
 return 0;
}
#endif
/* ---- goal invocation log ---- */
static int g_log_goals=0;static FILE *g_fi=NULL;
typedef struct { HGRecord *r;int n,cap; } RecBuf;
static void rec_emit(void *ctx,const HGRecord *r) {
 RecBuf *b=ctx;if(!g_log_goals)return;
 if(b->n==b->cap){b->cap=b->cap?2*b->cap:256;b->r=realloc(b->r,(size_t)b->cap*sizeof *b->r);if(!b->r){fprintf(stderr,"allocation failed\n");exit(1);}}
 b->r[b->n++]=*r;
}
static void feat_csv(FILE *f,const HGFeat *x) {
 fprintf(f,",%d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",x->level,x->health,x->food,x->drink,x->energy,x->mana,x->armour,x->ice,x->fire,x->torch,x->sapphire,x->ruby,x->diamond);
}
static void rec_flush(RecBuf *b,int ep,int snap_t,int replica,int max_level,int died) {
 if(!g_log_goals){b->n=0;return;}
 for(int i=0;i<b->n;i++) {
  const HGRecord *r=&b->r[i];
  fprintf(g_fi,"%d,%d,%d,%d,%d,%d,%s,",ep,snap_t,replica,r->id,r->parent,r->depth,hg_goal_names[r->goal]);
  if(r->goal==HG_EXCURSION)fprintf(g_fi,"%s",r->arg>=0 && r->arg<HGP_COUNT?hg_purpose_names[r->arg]:"other");else fprintf(g_fi,"%d",r->arg);
  fprintf(g_fi,",%d,%d,%d,%s,%d,%d",r->frontier,r->entry_t,r->exit_t,hg_outcome_names[r->outcome],r->resumed,r->top);
  feat_csv(g_fi,&r->in);feat_csv(g_fi,&r->out);
  fprintf(g_fi,",%.2f,%.2f,%.2f,%d,%d,%d\n",r->dmg[0],r->dmg[1],r->dmg[2],r->kills,max_level,died);
 }
 b->n=0;
}

#ifdef FP_RUNTIME_ABI
static FILE *g_runtime_fi=NULL;
typedef struct { FPGoalEvent *r;int n,cap; } RuntimeBuf;
static void runtime_collect(const FPAgent *a,RuntimeBuf *b,int *seen) {
 const FPGoalRuntime *g=&a->runtime;
 if(!g_log_goals){*seen=g->returns;return;}
 if(g->returns-*seen>4){fprintf(stderr,"runtime event ring overflow between evaluator checks\n");exit(1);}
 for(;*seen<g->returns;(*seen)++) {
  if(b->n==b->cap){b->cap=b->cap?2*b->cap:32;b->r=realloc(b->r,(size_t)b->cap*sizeof *b->r);if(!b->r){fprintf(stderr,"allocation failed\n");exit(1);}}
  b->r[b->n++]=g->event[*seen%4];
 }
}
static void runtime_flush(RuntimeBuf *b,int ep,int snap_t,int replica,int max_level,int died) {
 static const char *names[]={"open","completed","abandoned","died","horizon","stopped","interrupted"};
 for(int i=0;i<b->n;i++) {
  const FPGoalEvent *e=&b->r[i];const FPGoalFrame *f=&e->frame;
  fprintf(g_runtime_fi,"%d,%d,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d,%s,%d,%d,%d,%.2f,%d,%d,%d,%d,%.2f,%d,%d,%d,%d,%d\n",
   ep,snap_t,replica,f->id,f->parent,f->kind==FP_RG_EXCURSION,
   f->kind==FP_RG_EXCURSION?"excursion":"expedition",f->purpose,f->mode,f->target,f->frontier,f->entry_t,
   names[e->outcome],e->exit_t,f->entry_t<snap_t,f->in.level,f->in.health,f->in.food,f->in.drink,f->in.energy,
   e->out.level,e->out.health,e->out.food,e->out.drink,e->out.energy,max_level,died);
 }
 b->n=0;
}
#endif
static int kills_total(const CraftaxState *s) { int n=0;for(int i=0;i<9;i++)n+=s->monsters_killed[i];return n; }
static Trigger g_trig[MAX_TRIG];static int g_ntrig=0;
static HGState g_saved_hg;static int g_saved_hg_valid=0;
static unsigned char *g_buf=NULL,*g_cbuf=NULL;static uLong g_cbuf_size=0;
static void buffers(void) {
 if(g_buf)return;size_t n=sizeof(CraftaxState)+sizeof(FPAgent)+sizeof(HGState);
 g_buf=malloc(n);g_cbuf_size=compressBound((uLong)n);g_cbuf=malloc(g_cbuf_size);
 if(!g_buf||!g_cbuf){fprintf(stderr,"allocation failed\n");exit(1);}
}
static void write_snapshot(FILE *f,SnapHeader *h,const CraftaxState *state,const FPAgent *agent,const HGState *goals) {
 buffers();
 memcpy(g_buf,state,sizeof *state);memcpy(g_buf+sizeof *state,agent,sizeof *agent);
 if(goals)memcpy(g_buf+sizeof *state+sizeof *agent,goals,sizeof *goals);
 uLongf clen=g_cbuf_size;
 if(compress2(g_cbuf,&clen,g_buf,(uLong)(sizeof *state+sizeof *agent+(goals?sizeof *goals:0)),1)!=Z_OK){fprintf(stderr,"compress failed\n");exit(1);}
 memcpy(h->magic,goals?"FBFSNAP2":"FBFSNAP1",8);h->reserved[1]=goals?sizeof *goals:0;h->state_size=sizeof *state;h->agent_size=sizeof *agent;h->comp_size=(uint32_t)clen;
 h->policy_tag=POLICY_TAG;
 fwrite(h,sizeof *h,1,f);fwrite(g_cbuf,1,clen,f);fflush(f);
}
/* Returns 0 at EOF. Skips (without decompressing) when state==NULL. */
static int read_snapshot(FILE *f,SnapHeader *h,CraftaxState *state,FPAgent *agent) {
 if(fread(h,sizeof *h,1,f)!=1)return 0;
 int v2=!memcmp(h->magic,"FBFSNAP2",8);
 if(memcmp(h->magic,"FBFSNAP1",8) && !v2){fprintf(stderr,"bad snapshot magic\n");exit(1);}
 if(h->state_size!=sizeof(CraftaxState)){fprintf(stderr,"engine state layout differs from the bank\n");exit(1);}
 if(h->agent_size>sizeof(FPAgent)){fprintf(stderr,"FPAgent shrank (%u > %zu): candidate must only append fields\n",h->agent_size,sizeof(FPAgent));exit(1);}
 if(!state){fseek(f,h->comp_size,SEEK_CUR);return 1;}
 buffers();
 if(fread(g_cbuf,1,h->comp_size,f)!=h->comp_size){fprintf(stderr,"truncated snapshot\n");exit(1);}
 if(v2 && h->reserved[1]!=sizeof(HGState)){fprintf(stderr,"goal state layout differs from bank\n");exit(1);}
 uLongf expected=h->state_size+h->agent_size+(v2?sizeof(HGState):0),n=expected;
 if(uncompress(g_buf,&n,g_cbuf,h->comp_size)!=Z_OK || n!=expected){fprintf(stderr,"uncompress failed\n");exit(1);}
 memcpy(state,g_buf,h->state_size);
 memset(agent,0,sizeof *agent);memcpy(agent,g_buf+h->state_size,h->agent_size);
 g_saved_hg_valid=v2;if(v2)memcpy(&g_saved_hg,g_buf+h->state_size+h->agent_size,sizeof g_saved_hg);
 return 1;
}

/* ---- oracles (diagnostic only) ---- */
static int g_oracle=0,g_oracle_floors=0x1ff,g_omask[O_NOOPS+1];
static int on(int bit,int level) {
 if(!(g_oracle&bit))return 0;int i=0;while((1<<i)!=bit)i++;
 return (g_omask[i]>>level)&1;
}
static void oracle_start(CraftaxState *s) {
 if(g_oracle&O_DEX5)s->player_dexterity=5;
 if(g_oracle&O_MAXSTATS){s->player_dexterity=5;s->player_strength=5;s->player_intelligence=5;}
 if(g_oracle&O_ARMOUR)for(int i=0;i<4;i++)s->inventory.armour[i]=2;
 if(g_oracle&O_DIA3)s->inventory.diamond+=3;
 if(g_oracle&O_DIA6)s->inventory.diamond+=6;
 if(g_oracle&O_SAP3)s->inventory.sapphire+=3;
 if(g_oracle&O_RUBY3)s->inventory.ruby+=3;
}
static void oracle_step(CraftaxState *s,float health_before,int level_before,int max_level) {
 int L=level_before,N=s->player_level;
 if(on(O_ENCHMATCH,N)&&(N==6||N==7))for(int i=0;i<4;i++)if(s->inventory.armour[i])s->armour_enchantments[i]=N==6?1:2;
 if(on(O_MANA,N))s->player_mana=craftax_step_get_max_mana(s);
 if(on(O_FREEKILLS,N)&&s->monsters_killed[N]<8)s->monsters_killed[N]=8;
 if(on(O_FOOD,L)){s->player_food=craftax_step_get_max_food(s);s->player_hunger=0;}
 if(on(O_DRINK,L)){s->player_drink=craftax_step_get_max_drink(s);s->player_thirst=0;}
 if(on(O_ENERGY,L)){s->player_energy=craftax_step_get_max_energy(s);s->player_fatigue=0;}
 float mx=(float)craftax_step_get_max_health(s),restore=0;
 if(on(O_NOMELEE,L))restore+=g_dmg[0];
 if(on(O_NOPROJ,L))restore+=g_dmg[1];
 if(on(O_NOSTARVE,L))restore+=g_dmg[2];
 if(restore>0)s->player_health=fminf(mx,s->player_health+restore);
 if(on(O_REVISIT_SAFE,L) && level_before<max_level && s->player_health<health_before)s->player_health=health_before;
 if(on(O_FULLHEAL,L))s->player_health=mx;
 if(on(O_LIGHT,L))memset(s->light_map[N],255,sizeof s->light_map[N]);
}

/* ---- episode ---- */
typedef struct {
 int ep,replica,snap_kind,snap_level,snap_t,snap_occ,handoff_t;uint32_t world_seed;
 int t,max_level,arrive[9],cleared[9],exit_known[9],exit_ready[9];
 uint64_t hash,arrive_hash[9];double total;float previous_reward;
 CraftaxThreefryKey rng_key;
} Ctx;

static int g_stop_floor=99,g_steps=100000;
static void run(Ctx *x,CraftaxState *state,FPAgent *agent,FPScratch *scratch,FILE *fe,FILE *fv,FILE *ft,int capture) {
 static Tick ring[RING];
 FPConfig cfg=fp_default_config();
 trace_open(x->ep,x->replica);
 int visits[9]={0},floor_steps[9]={0};
 int v_level=state->player_level,v_arrive=x->t,v_cleared_in=state->monsters_killed[v_level]>=8,v_food=state->player_food,v_drink=state->player_drink,v_energy=state->player_energy;
 int v_sleep=0,v_low=0;float v_entry=state->player_health,v_min=state->player_health,v_dmg[4]={0};visits[v_level]=1;
 int nring=0,t=x->t,prev_level=state->player_level;
 for(int i=0;i<g_ntrig;i++)g_trig[i].fired=g_trig[i].occ=g_trig[i].taken=0;
 int climbed_from[9]={0};
 /* arrived_from: floor left by the previous step (-1 if it did not change floor). */
 int arrived_from=-1,start_level=state->player_level,inc_on=0;unsigned visited=1u<<start_level;
 x->handoff_t=-1;
 /* Goal monitor: cold-started on the first observation, stepped after every action. */
 static HGState hg;static RecBuf recs;int hg_on=0,pend_heal=-1,pend_fight=-1,runtime_targets_set=0;
#ifdef FP_RUNTIME_ABI
 static RuntimeBuf runtime_recs;runtime_recs.n=0;int runtime_seen=agent->runtime.returns;
#endif
 HGEngine eng;memset(&eng,0,sizeof eng);eng.kills=kills_total(state);eng.health=state->player_health;recs.n=0;
 for(;t<g_steps && !craftax_is_game_over_native(state) && state->player_level<g_stop_floor;t++) {
  if(capture) for(int i=0;i<g_ntrig;i++) {
   Trigger *g=&g_trig[i];if(g->fired)continue;int L=state->player_level,K=g->level,hit=0;
   if(g->kind==T_ARRIVE)hit=L==K;
   else if(g->kind==T_CLEAR)hit=L==K && state->monsters_killed[K]>=8;
   else if(g->kind==T_READY)hit=L==K && state->monsters_killed[K]>=8 && agent->floors[K].down>=0;
   else if(g->kind==T_UP)hit=L==K-1 && climbed_from[K];
   else if(g->kind==T_LEAVE)hit=L==K-1 && arrived_from==K;
   else if(g->kind==T_ENTER)hit=L==K && arrived_from>=0;
   else if(g->kind==T_GHEAL)hit=L==K && pend_heal==K;
   else if(g->kind==T_GFIGHT)hit=L==K && pend_fight==K;
   if(!hit)continue;
   if(trig_repeated(g->kind)) {
    g->occ++;
    if(g->taken>=g_capture_max || !sample_keep(x->world_seed,i,g->occ))continue;
    g->taken++;
   } else g->fired=1;
   SnapHeader h;memset(&h,0,sizeof h);h.trig_kind=g->kind;h.trig_level=K;h.ep=x->ep;h.world_seed=x->world_seed;h.t=t;
   h.reserved[0]=g->occ;
   h.previous_reward=x->previous_reward;h.total=x->total;h.max_level=x->max_level;h.hash=x->hash;h.rng_key=x->rng_key;
   memcpy(h.arrive,x->arrive,sizeof h.arrive);memcpy(h.cleared,x->cleared,sizeof h.cleared);
   memcpy(h.exit_known,x->exit_known,sizeof h.exit_known);memcpy(h.exit_ready,x->exit_ready,sizeof h.exit_ready);
   memcpy(h.arrive_hash,x->arrive_hash,sizeof h.arrive_hash);
   write_snapshot(g->f,&h,state,agent,hg_on?&hg:NULL);
  }
  arrived_from=-1;pend_heal=pend_fight=-1;
  uint8_t record[996];FPObservation obs;craftax_encode_native_observation(state,record);fp_decode_compact(&obs,record);
  if(!hg_on) {
   if(!capture && g_saved_hg_valid) {
    hg=g_saved_hg;
    for(int d=0;d<hg.depth;d++){hg.f[d].resumed=1;hg.f[d].entry_t=t;hg.f[d].in=hg_feat(agent,&obs);hg.f[d].kills0=eng.kills;memset(hg.f[d].dmg,0,sizeof hg.f[d].dmg);}
   } else hg_start(&hg,agent,&obs,t,&eng);
   hg_on=1;
   set_targets(g_stop_kind,g_stop_arg,g_stop_target,g_stop_n,&hg);
   set_targets(g_handoff_kind,g_handoff_arg,g_handoff_target,g_handoff_n,&hg);
  }
  if(g_stop_n && cond_hit(g_stop_kind,g_stop_arg,g_stop_target,g_stop_n,state->player_level,start_level,visited,t,x->snap_t,&hg))break;
  if(!inc_on && g_handoff_n && cond_hit(g_handoff_kind,g_handoff_arg,g_handoff_target,g_handoff_n,state->player_level,start_level,visited,t,x->snap_t,&hg)){inc_on=1;x->handoff_t=t;}
  /* Speculate the decision to absorb observation and detect its boundary.
   * Restore all policy memory (including RNG) if control returns before action. */
  static FPAgent before;HGState hg_before;int rec_before=recs.n,speculative=0;
  if(!inc_on) {
   for(int i=0;i<g_handoff_n;i++)if(g_handoff_kind[i]==H_GOALEND)speculative=1;
   for(int i=0;i<g_stop_n;i++)if(g_stop_kind[i]==H_GOALEND)speculative=1;
  }
  if(speculative)hg_before=hg;
  if(speculative)before=*agent;
  int action;
#ifdef FP_RUNTIME_ABI
  if(g_runtime_handoff) {
   /* Capture existing persistent invocation before this observation can return it.
    * Legacy zero-filled banks bootstrap once during their first observation. */
   if(!runtime_targets_set && agent->runtime.depth) {
    runtime_targets(g_handoff_kind,g_handoff_arg,g_handoff_target,g_handoff_n,agent);
    runtime_targets(g_stop_kind,g_stop_arg,g_stop_target,g_stop_n,agent);runtime_targets_set=1;
   }
#ifdef HYBRID
   if(inc_on)inc_hybrid_observe(agent,&obs,x->previous_reward,scratch);
   else
#endif
   fp_observe(agent,&obs,x->previous_reward,scratch);
   if(!runtime_targets_set) {
    runtime_targets(g_handoff_kind,g_handoff_arg,g_handoff_target,g_handoff_n,agent);
    runtime_targets(g_stop_kind,g_stop_arg,g_stop_target,g_stop_n,agent);runtime_targets_set=1;
   }
   if(runtime_hit(g_stop_kind,g_stop_target,g_stop_n,agent))break;
   if(!inc_on && runtime_hit(g_handoff_kind,g_handoff_target,g_handoff_n,agent)){inc_on=1;x->handoff_t=t;}
#ifdef HYBRID
   if(inc_on)action=inc_hybrid_decide(agent,&obs,scratch);
   else
#endif
   action=fp_decide(agent,&obs,&cfg,scratch);
  } else
#endif
  {
#ifdef HYBRID
  if(inc_on)action=inc_hybrid_act(agent,record,x->previous_reward,g_inc_scratch);
  else
#endif
  action=fp_act(agent,&obs,x->previous_reward,&cfg,scratch);
  }
  hg_step(&hg,agent,&obs,t,&eng,rec_emit,&recs);
  if(speculative && g_stop_n && cond_hit(g_stop_kind,g_stop_arg,g_stop_target,g_stop_n,state->player_level,start_level,visited,t,x->snap_t,&hg)){*agent=before;break;}
#ifdef HYBRID
  if(speculative && g_handoff_n && cond_hit(g_handoff_kind,g_handoff_arg,g_handoff_target,g_handoff_n,state->player_level,start_level,visited,t,x->snap_t,&hg)) {
   *agent=before;inc_on=1;x->handoff_t=t;
   action=inc_hybrid_act(agent,record,x->previous_reward,g_inc_scratch);
   hg=hg_before;recs.n=rec_before;hg_step(&hg,agent,&obs,t,&eng,rec_emit,&recs);
  }
#endif
#ifdef FP_RUNTIME_ABI
  runtime_collect(agent,&runtime_recs,&runtime_seen);
#endif
  if(hg.heal_onset)pend_heal=obs.level;
  if(hg.fight_onset)pend_fight=obs.level;
  int observed_level=obs.level;
  if(obs.clear && x->cleared[observed_level]<0)x->cleared[observed_level]=t;
  if(agent->floors[observed_level].down>=0 && x->exit_known[observed_level]<0)x->exit_known[observed_level]=t;
  if(obs.clear && agent->floors[observed_level].down>=0 && x->exit_ready[observed_level]<0)x->exit_ready[observed_level]=t;
  x->hash=(x->hash^(uint64_t)(action+1))*UINT64_C(0x100000001b3);
  CraftaxThreefryKey step_key,step_rng,reset_key;
  craftax_threefry_split(x->rng_key,&x->rng_key,&step_key);
  craftax_threefry_split(step_key,&step_rng,&reset_key);
  memset(g_dmg,0,sizeof g_dmg);g_hits[0]=g_hits[1]=0;g_last_src=-1;g_last_type=-1;
  int level_before=state->player_level;float health_before=state->player_health;
  x->previous_reward=craftax_gameplay_step_native(state,action,step_rng);x->total+=x->previous_reward;
  if(g_oracle)oracle_step(state,health_before,level_before,x->max_level);
  int level=state->player_level;if(level>x->max_level)x->max_level=level;
  if(level<level_before)climbed_from[level_before]=1;
  if(level!=level_before){arrived_from=level_before;visited|=1u<<level;}
  for(int k=0;k<3;k++)eng.dmg[k]=g_dmg[k];
  eng.kills=kills_total(state);eng.health=state->player_health;hg_account(&hg,&eng);
  if(x->arrive[level]<0){x->arrive[level]=t+1;x->arrive_hash[level]=x->hash;}
  for(int k=0;k<4;k++)v_dmg[k]+=g_dmg[k];
  floor_steps[level_before]++;if(state->is_sleeping)v_sleep++;if(state->player_health<=3)v_low++;
  if(state->player_health<v_min)v_min=state->player_health;
  Tick *k=&ring[nring%RING];nring++;
  k->t=t+1;k->level=level_before;k->action=action;k->skill=agent->skill;k->food=state->player_food;k->drink=state->player_drink;
  k->energy=state->player_energy;k->mana=state->player_mana;k->sleeping=state->is_sleeping;k->resting=state->is_resting;
  k->src=g_last_src;k->type=g_last_type;k->health=state->player_health;memcpy(k->dmg,g_dmg,sizeof g_dmg);
  k->near_melee=near_count(state,0,3);k->near_ranged=near_count(state,1,5);
  trace_step(state,t+1,action,agent);
  int dead=state->player_health<=0.0f;
  if(level!=level_before || dead || t+1==g_steps) {
   int cleared_out=state->monsters_killed[level_before]>=8;
   int arm=0;for(int i=0;i<4;i++)arm+=state->inventory.armour[i];
   const char *exit=dead?"died":level>level_before?"down":level<level_before?"up":"horizon";
   fprintf(fv,"%d,%d,%d,%d,%d,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
    x->ep,v_level,visits[v_level],v_arrive,t+1,exit,v_entry,state->player_health,v_min,v_dmg[0],v_dmg[1],v_dmg[2],v_dmg[3],
    v_cleared_in,cleared_out,v_food,v_drink,v_energy,state->player_food,state->player_drink,state->player_energy,arm,state->inventory.sword,v_sleep,v_low,x->replica,x->snap_t);
   if(!dead && level!=level_before) {
    v_level=level;visits[level]++;v_arrive=t+1;v_cleared_in=state->monsters_killed[level]>=8;
    v_food=state->player_food;v_drink=state->player_drink;v_energy=state->player_energy;v_entry=state->player_health;v_min=v_entry;
    memset(v_dmg,0,sizeof v_dmg);v_sleep=0;v_low=0;
   }
  }
  prev_level=level;
 }
 (void)prev_level;
 int died=state->player_health<=0.0f;
#ifdef FP_RUNTIME_ABI
 /* Terminal observations need no action/perception update, but finish records
  * use public terminal features and preserve distinct exhaustion/death outcomes. */
 uint8_t terminal_record[996];FPObservation terminal_obs;
 craftax_encode_native_observation(state,terminal_record);fp_decode_compact(&terminal_obs,terminal_record);
 agent->runtime.observed=(FPGoalPublic){terminal_obs.level,terminal_obs.food,terminal_obs.drink,terminal_obs.energy,terminal_obs.health};
 int terminal_outcome=died?FP_RO_DIED:(t>=g_steps?FP_RO_HORIZON:(state->player_level>=8?FP_RO_COMPLETED:FP_RO_STOPPED));
 if(terminal_outcome==FP_RO_COMPLETED && agent->runtime.depth==2)fp_goal_return(agent,FP_RO_INTERRUPTED);
 fp_goal_finish(agent,terminal_outcome);
 runtime_collect(agent,&runtime_recs,&runtime_seen);
 if(g_log_goals)runtime_flush(&runtime_recs,x->ep,x->snap_t,x->replica,x->max_level,died);
#endif
 if(hg_on){hg_finish(&hg,t,died,t>=g_steps,state->player_level,&eng,rec_emit,&recs);rec_flush(&recs,x->ep,x->snap_t,x->replica,x->max_level,died);}
 int arm=0,ench=0;for(int i=0;i<4;i++){arm+=state->inventory.armour[i];ench+=state->armour_enchantments[i]!=0;}
 float d100[4]={0},minhp=99;int sk[FP_SKILL_COUNT]={0};int n=nring<RING?nring:RING;
 for(int i=0;i<n;i++){Tick *k=&ring[(nring-1-i)%RING];for(int j=0;j<4;j++)d100[j]+=k->dmg[j];if(k->health<minhp)minhp=k->health;if(i<50)sk[k->skill]++;}
 int mode=0;for(int i=1;i<FP_SKILL_COUNT;i++)if(sk[i]>sk[mode])mode=i;
 Tick none={0};none.src=-1;none.type=-1;
 Tick *last=n?&ring[(nring-1)%RING]:&none;Tick *prev=n>1?&ring[(nring-2)%RING]:last;
 int dl=state->player_level,ach=0;for(int i=0;i<67;i++)ach+=state->achievements[i];
 fprintf(fe,"%d,%u,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d",
  x->ep,x->world_seed,x->total,ach,t,x->max_level,died,dl,died?last->src:-1,died?last->type:-1,died?last->sleeping||g_last_sleep:0,died?g_last_amt:0.0f,
  prev->health,state->player_food,state->player_drink,state->player_energy,state->player_mana,state->is_sleeping,state->is_resting,
  fp_skill_name(agent->skill),fp_skill_name(mode),t-v_arrive,visits[dl],floor_steps[dl],state->monsters_killed[dl]>=8,
  state->inventory.pickaxe,state->inventory.sword,arm,ench,state->sword_enchantment,state->inventory.bow,state->inventory.arrows,
  state->player_dexterity,state->player_strength,state->player_intelligence,state->player_xp,state->light_level,
  near_count(state,0,3),near_count(state,1,5),d100[0],d100[1],d100[2],d100[3],minhp,0,0,
  state->boss_progress,state->achievements[49]);
 for(int i=1;i<9;i++)fprintf(fe,",%d",x->arrive[i]);
 for(int i=1;i<9;i++)fprintf(fe,",%016llx",(unsigned long long)x->arrive_hash[i]);
 for(int i=0;i<8;i++)fprintf(fe,",%d,%d,%d",x->cleared[i],x->exit_known[i],x->exit_ready[i]);
 fprintf(fe,",%d,%s,%d,%d,%d,%d,%d,%d,%016llx\n",x->replica,x->snap_kind>=0?trig_name[x->snap_kind]:"reset",x->snap_level,x->snap_t,g_oracle,g_oracle_floors,x->snap_occ,x->handoff_t,(unsigned long long)x->hash);
 trace_close(x->ep,x->replica);
 if(died) for(int i=n-1;i>=0;i--) {
  Tick *k=&ring[(nring-1-i)%RING];
  fprintf(ft,"%d,%d,%d,%d,%d,%s,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%d\n",x->ep,i,k->t,k->level,k->action,fp_skill_name(k->skill),
   k->health,k->food,k->drink,k->energy,k->mana,k->sleeping,k->src,k->type,k->near_melee,k->near_ranged,k->dmg[0],k->dmg[1],k->dmg[2],k->dmg[3],x->replica,x->snap_t);
 }
 fflush(fe);fflush(fv);fflush(ft);
}

static void parse_trigger(const char *spec) {
 char kind[32];int level;
 if(sscanf(spec,"%31[^:]:%d",kind,&level)!=2 || level<0 || level>8){fprintf(stderr,"bad trigger %s\n",spec);exit(2);}
 int k=-1;for(int i=0;i<T_KINDS;i++)if(!strcmp(kind,trig_name[i]))k=i;
 if(k<0||g_ntrig>=MAX_TRIG){fprintf(stderr,"bad trigger %s\n",spec);exit(2);}
 g_trig[g_ntrig].kind=k;g_trig[g_ntrig].level=level;g_ntrig++;
}
/* Per-oracle floor masks: NAME@MASK; names without one use --oracle-floors (resolved in main). */
static int g_omask_set[O_NOOPS+1];
static int parse_oracle(const char *spec) {
 int mask=0;char buf[512];snprintf(buf,sizeof buf,"%s",spec);
 for(char *tok=strtok(buf,",+");tok;tok=strtok(NULL,",+")) {
  char *at=strchr(tok,'@');int fm=-1;if(at){*at=0;fm=(int)strtol(at+1,NULL,0);}
  int found=0;for(int i=0;oracle_names[i];i++)if(!strcmp(tok,oracle_names[i])){mask|=1<<i;found=1;if(fm>=0){g_omask[i]=fm;g_omask_set[i]=1;}}
  if(!found && strcmp(tok,"none")){fprintf(stderr,"unknown oracle %s\n",tok);exit(2);}
 }
 return mask;
}

int main(int argc,char **argv) {
 uint64_t seed=0;int have_seed=0,replicas=1,first_replica=0,stride=1,offset=0,limit=-1,start=0,only_ep=-1,only_t=-1;
 const char *prefix=NULL,*list=NULL,*bank=NULL,*capture_dir=NULL,*capture_tag="w";
 for(int i=1;i+1<argc;i+=2) {
  if(!strcmp(argv[i],"--seed")){seed=(uint64_t)strtoull(argv[i+1],NULL,10);have_seed=1;}
  else if(!strcmp(argv[i],"--steps"))g_steps=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--out"))prefix=argv[i+1];
  else if(!strcmp(argv[i],"--list"))list=argv[i+1];
  else if(!strcmp(argv[i],"--trace"))g_trace_prefix=argv[i+1];
  else if(!strcmp(argv[i],"--capture"))parse_trigger(argv[i+1]);
  else if(!strcmp(argv[i],"--capture-dir"))capture_dir=argv[i+1];
  else if(!strcmp(argv[i],"--capture-tag"))capture_tag=argv[i+1];
  else if(!strcmp(argv[i],"--resume"))bank=argv[i+1];
  else if(!strcmp(argv[i],"--replicas"))replicas=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--first-replica"))first_replica=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--stride"))stride=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--offset"))offset=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--limit"))limit=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--start"))start=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--episode"))only_ep=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--stop-floor"))g_stop_floor=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--oracle"))g_oracle=parse_oracle(argv[i+1]);
  else if(!strcmp(argv[i],"--oracle-floors"))g_oracle_floors=(int)strtol(argv[i+1],NULL,0);
  else if(!strcmp(argv[i],"--capture-prob"))g_capture_prob=atof(argv[i+1]);
  else if(!strcmp(argv[i],"--capture-max"))g_capture_max=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--handoff"))parse_handoff(argv[i+1]);
  else if(!strcmp(argv[i],"--stop"))parse_cond(argv[i+1],g_stop_kind,g_stop_arg,&g_stop_n);
  else if(!strcmp(argv[i],"--log-goals"))g_log_goals=atoi(argv[i+1]);
  else if(!strcmp(argv[i],"--snap-t"))only_t=atoi(argv[i+1]);
  else {fprintf(stderr,"unknown option %s\n",argv[i]);return 2;}
 }
 for(int i=0;i<=O_NOOPS;i++)if(!g_omask_set[i])g_omask[i]=g_oracle_floors;
#ifdef HYBRID
#ifdef FP_RUNTIME_ABI
 if(g_runtime_handoff && (inc_hybrid_agent_size()!=sizeof(FPAgent) || inc_hybrid_scratch_size()!=sizeof(FPScratch) || inc_hybrid_runtime_offset()!=offsetof(FPAgent,runtime) || inc_hybrid_runtime_size()!=sizeof(FPGoalRuntime))) {
  fprintf(stderr,"runtimeend requires identical candidate/incumbent runtime layouts\n");return 2;
 }
#endif
 if(inc_hybrid_agent_size()>sizeof(FPAgent)){fprintf(stderr,"incumbent FPAgent (%zu) larger than candidate (%zu)\n",inc_hybrid_agent_size(),sizeof(FPAgent));return 2;}
 g_inc_scratch=calloc(1,inc_hybrid_scratch_size());if(!g_inc_scratch){fprintf(stderr,"allocation failed\n");return 1;}
#else
 if(g_handoff_n){fprintf(stderr,"--handoff needs a HYBRID build (bank.py --handoff-dir)\n");return 2;}
#endif
 if(!prefix || (!bank && (!have_seed || !list))) {
  fprintf(stderr,"usage: branch --out PREFIX (--seed BASE --list FILE [--capture KIND:LEVEL ... --capture-dir DIR --capture-prob P --capture-max M]\n"
                 "                            | --resume BANK [--replicas R --first-replica R0 --start I --limit N --stride S --offset O --episode E --snap-t T]\n"
                 "                              [--handoff return:K|change|steps:N  (HYBRID build)])\n"
                 "              [--stop-floor F] [--oracle a,b@MASK] [--oracle-floors MASK] [--trace PREFIX] [--steps N]\n"
                 "              [--stop COND] [--log-goals 1]   COND: return:K | change | steps:N | goalend:NAME\n");
  return 2;
 }
 char path[1024];
 snprintf(path,sizeof path,"%s_episodes.csv",prefix);FILE *fe=fopen(path,"w");
 snprintf(path,sizeof path,"%s_visits.csv",prefix);FILE *fv=fopen(path,"w");
 snprintf(path,sizeof path,"%s_tail.csv",prefix);FILE *ft=fopen(path,"w");
 if(g_log_goals) {
  snprintf(path,sizeof path,"%s_invocations.csv",prefix);g_fi=fopen(path,"w");if(!g_fi){perror(path);return 1;}
  fprintf(g_fi,"episode,snap_t,replica,id,parent,depth,goal,arg,frontier,entry_t,exit_t,outcome,resumed,top");
  const char *fn[]={"level","health","food","drink","energy","mana","armour","ice","fire","torch","sapphire","ruby","diamond"};
  for(int w=0;w<2;w++)for(int i=0;i<13;i++)fprintf(g_fi,",%s_%s",w?"out":"in",fn[i]);
  fprintf(g_fi,",dmg_melee,dmg_proj,dmg_intr,kills,final_max_level,final_died\n");
#ifdef FP_RUNTIME_ABI
  snprintf(path,sizeof path,"%s_runtime_invocations.csv",prefix);g_runtime_fi=fopen(path,"w");if(!g_runtime_fi){perror(path);return 1;}
  fprintf(g_runtime_fi,"episode,snap_t,replica,id,parent,depth,goal,purpose,mode,target,frontier,entry_t,outcome,exit_t,resumed,in_level,in_health,in_food,in_drink,in_energy,out_level,out_health,out_food,out_drink,out_energy,final_max_level,final_died\n");
#endif
 }
 if(!fe||!fv||!ft){perror("open");return 1;}
 fprintf(fe,"episode,world_seed,return,achievements,length,max_level,died,death_level,killer_src,killer_type,killer_sleep,killer_amt,"
  "health_prev,food,drink,energy,mana,sleeping,resting,skill,skill_mode50,steps_in_visit,visits_to_floor,steps_on_floor_total,cleared,"
  "pickaxe,sword,armour_sum,armour_ench,sword_ench,bow,arrows,dex,str,intel,xp,light,near_melee3,near_ranged5,"
  "dmg100_melee,dmg100_proj,dmg100_intr,dmg100_potion,minhp100,hits_melee_floor,hits_proj_floor,boss_progress,defeat_necromancer");
 for(int i=1;i<9;i++)fprintf(fe,",arrive%d",i);
 for(int i=1;i<9;i++)fprintf(fe,",hash%d",i);
 for(int i=0;i<8;i++)fprintf(fe,",cleared%d,exit_known%d,exit_ready%d",i,i,i);
 fprintf(fe,",replica,snap_kind,snap_level,snap_t,oracle,oracle_floors,snap_occ,handoff_t,final_hash\n");
 fprintf(fv,"episode,level,visit,arrive_t,leave_t,exit,entry_health,exit_health,min_health,dmg_melee,dmg_proj,dmg_intr,dmg_potion,"
  "cleared_in,cleared_out,food_in,drink_in,energy_in,food_out,drink_out,energy_out,armour_sum,sword,steps_sleeping,steps_le3hp,replica,snap_t\n");
 fprintf(ft,"episode,k,t,level,action,skill,health,food,drink,energy,mana,sleeping,src,type,near_melee3,near_ranged5,dmg_melee,dmg_proj,dmg_intr,dmg_potion,replica,snap_t\n");
 CraftaxState *state=calloc(1,sizeof(*state));FPAgent *agent=malloc(sizeof(*agent));FPScratch *scratch=malloc(sizeof(*scratch));
 if(!state||!agent||!scratch){fprintf(stderr,"allocation failed\n");return 1;}
 craftax_set_lazy_floors(1);craftax_wg_init_cell_templates();
 if(!bank) {
  if(g_ntrig && !capture_dir){fprintf(stderr,"--capture needs --capture-dir\n");return 2;}
  for(int i=0;i<g_ntrig;i++) {
   snprintf(path,sizeof path,"%s/%s%d_%s.bin",capture_dir,trig_name[g_trig[i].kind],g_trig[i].level,capture_tag);
   g_trig[i].f=fopen(path,"wb");if(!g_trig[i].f){perror(path);return 1;}
  }
  FILE *fl=fopen(list,"r");if(!fl){perror(list);return 1;}
  int ep;
  while(fscanf(fl,"%d",&ep)==1) {
   Ctx x;memset(&x,0,sizeof x);x.ep=ep;x.snap_kind=-1;x.snap_level=-1;x.snap_occ=0;
   x.world_seed=(uint32_t)mix(seed+(uint64_t)ep);
   Craftax env={0};env.state=state;env.seed=x.world_seed;craftax_reset_state_from_seed(&env);x.rng_key=env.rng_key;
   fp_reset(agent,mix(x.world_seed));
   for(int i=0;i<9;i++){x.arrive[i]=x.cleared[i]=x.exit_known[i]=x.exit_ready[i]=-1;x.arrive_hash[i]=0;}
   x.hash=UINT64_C(0xcbf29ce484222325);x.arrive[0]=0;x.arrive_hash[0]=x.hash;
   if(g_oracle)oracle_start(state);
   run(&x,state,agent,scratch,fe,fv,ft,1);
  }
  fclose(fl);for(int i=0;i<g_ntrig;i++)fclose(g_trig[i].f);
 } else {
  FILE *fb=fopen(bank,"rb");if(!fb){perror(bank);return 1;}
  SnapHeader h;int index=0,done=0;
  for(;;) {
   /* Records are selected by bank index [start, start+limit), then striped over workers. */
   int sel=index>=start && (limit<0 || index<start+limit);
   int mine=sel && (index-start)%stride==offset;
   if(!read_snapshot(fb,&h,mine?state:NULL,mine?agent:NULL))break;
   index++;if(!mine)continue;
   if(only_ep>=0 && h.ep!=only_ep)continue;
   if(only_t>=0 && h.t!=only_t)continue;
   CraftaxState *saved=malloc(sizeof *saved);FPAgent *saved_agent=malloc(sizeof *saved_agent);
   memcpy(saved,state,sizeof *saved);memcpy(saved_agent,agent,sizeof *saved_agent);
   for(int r=first_replica;r<first_replica+replicas;r++) {
    memcpy(state,saved,sizeof *state);memcpy(agent,saved_agent,sizeof *agent);
    Ctx x;memset(&x,0,sizeof x);x.ep=h.ep;x.replica=r;x.snap_kind=(int)h.trig_kind;x.snap_level=(int)h.trig_level;x.snap_t=h.t;x.snap_occ=h.reserved[0];
    x.world_seed=h.world_seed;x.t=h.t;x.max_level=h.max_level;x.hash=h.hash;x.total=h.total;x.previous_reward=h.previous_reward;
    memcpy(x.arrive,h.arrive,sizeof x.arrive);memcpy(x.cleared,h.cleared,sizeof x.cleared);
    memcpy(x.exit_known,h.exit_known,sizeof x.exit_known);memcpy(x.exit_ready,h.exit_ready,sizeof x.exit_ready);
    memcpy(x.arrive_hash,h.arrive_hash,sizeof x.arrive_hash);x.rng_key=h.rng_key;
    if(r>0) {
     uint64_t z=mix(((uint64_t)(uint32_t)h.ep<<20)^(uint64_t)r^UINT64_C(0x5eed000000000000));
     x.rng_key.word[0]^=(uint32_t)z;x.rng_key.word[1]^=(uint32_t)(z>>32);
     agent->rng^=mix(z);if(!agent->rng)agent->rng=1;
    }
    if(g_oracle)oracle_start(state);
    run(&x,state,agent,scratch,fe,fv,ft,0);done++;
   }
   free(saved);free(saved_agent);
  }
  fclose(fb);
  if(!done)fprintf(stderr,"warning: no snapshots selected from %s\n",bank);
 }
 fclose(fe);fclose(fv);fclose(ft);if(g_fi)fclose(g_fi);
#ifdef FP_RUNTIME_ABI
 if(g_runtime_fi)fclose(g_runtime_fi);
#endif
 free(state);free(agent);free(scratch);return 0;
}
