/* Evaluation driver for the world-model/policy split (regime E of the feedback cascade).
 *
 *   capture  Play worlds from reset; save the state before the first action on
 *            each requested floor: --seed S --start I --count N --levels 5,6,7 --out DIR
 *   eval     Resume selected bank snapshots with R replicas each, until
 *            --stop-floor, death or the horizon: --bank FILE [--select FILE]
 *            [--replicas A:B] --out FILE
 *   reset    Play worlds from reset: --seed S --start I --count N --out FILE
 *
 * Options for eval and reset:
 *   --oracle hp,cooldown,shots,unseen   information oracles: after each
 *            observation, replace the WM's beliefs with engine truth (enemy
 *            health, attack cooldowns, projectile danger cells, enemies hidden
 *            in darkness within view). The policy code is unchanged.
 *   --audit 1   compare the WM's beliefs and predictions with engine truth on
 *            the states the policy visits; per-continuation counters are added
 *            to the output rows (see Audit).
 *
 * The engine is the side-channel-instrumented copy of craftax_full.c
 * (patch_engine.py: identical dynamics, per-step damage by
 * source). World seeding, the episode loop, snapshots (HRLSNAP1) and replica
 * rekeying follow branch.c:
 * replica 0 reproduces the capture episode, replica r>0 rekeys future dice.
 * The policy is compiled in; compare policies by running their binaries on
 * the same snapshots and replicas (common random numbers). */
#define CRAFTAX_CORE_ONLY
#define CRAFTAX_COMPACT_OBS
#include "craftax_full_autopsy.c"
#include "full_policy.h"
#include <dirent.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

static uint64_t mix(uint64_t x) {
 x+=UINT64_C(0x9e3779b97f4a7c15);x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);
}
static void die(const char *m){fprintf(stderr,"wm_eval: %s\n",m);exit(2);}

/* ---------------- snapshots (HRLSNAP1, as hybrid_eval.c) ---------------- */
typedef struct {
 char magic[8];
 uint32_t state_size,agent_size,comp_size,level;
 int32_t ep;uint32_t world_seed;int32_t t,max_level;
 float previous_reward;int32_t pad;double total;
 uint64_t hash;CraftaxThreefryKey rng_key;
 int32_t arrive[9];int32_t reserved[7];
} Snap;
typedef struct { Snap h;uint8_t *data; } SnapRec;
static uint8_t *g_buf;static size_t g_buf_size;
static void buffers(void) {
 if(g_buf)return;g_buf_size=compressBound(sizeof(CraftaxState)+sizeof(FPAgent));
 g_buf=malloc(g_buf_size+sizeof(CraftaxState)+sizeof(FPAgent));if(!g_buf)die("allocation failed");
}
static void snap_encode(SnapRec *rec,const CraftaxState *st,const FPAgent *ag) {
 buffers();uint8_t *raw=g_buf+g_buf_size;
 memcpy(raw,st,sizeof *st);memcpy(raw+sizeof *st,ag,sizeof *ag);
 uLongf n=g_buf_size;
 if(compress2(g_buf,&n,raw,sizeof *st+sizeof *ag,1)!=Z_OK)die("compress failed");
 memcpy(rec->h.magic,"HRLSNAP1",8);rec->h.state_size=sizeof *st;rec->h.agent_size=sizeof *ag;rec->h.comp_size=(uint32_t)n;
 rec->data=malloc(n);memcpy(rec->data,g_buf,n);
}
static void snap_decode(const SnapRec *rec,CraftaxState *st,FPAgent *ag) {
 buffers();uint8_t *raw=g_buf+g_buf_size;
 uLongf n=rec->h.state_size+rec->h.agent_size;
 if(uncompress(raw,&n,rec->data,rec->h.comp_size)!=Z_OK || n!=rec->h.state_size+rec->h.agent_size)die("uncompress failed");
 memcpy(st,raw,sizeof *st);memset(ag,0,sizeof *ag);memcpy(ag,raw+sizeof *st,rec->h.agent_size);
}
static void snap_write(FILE *f,const SnapRec *r){fwrite(&r->h,sizeof r->h,1,f);fwrite(r->data,1,r->h.comp_size,f);}
static int snap_read(FILE *f,SnapRec *r,int keep) {
 if(fread(&r->h,sizeof r->h,1,f)!=1)return 0;
 if(memcmp(r->h.magic,"HRLSNAP1",8))die("bad snapshot magic");
 if(r->h.state_size!=sizeof(CraftaxState))die("engine state layout differs from the bank");
 if(r->h.agent_size>sizeof(FPAgent))die("FPAgent shrank: the policy may only append fields");
 if(!keep){fseek(f,r->h.comp_size,SEEK_CUR);r->data=NULL;return 1;}
 r->data=malloc(r->h.comp_size);
 if(fread(r->data,1,r->h.comp_size,f)!=r->h.comp_size)die("truncated snapshot");
 return 1;
}

/* ---------------- audit and oracles ---------------- */
enum { OR_HP=1, OR_CD=2, OR_SHOTS=4, OR_UNSEEN=8 };
/* Surprise categories: a hit the WM did not predict, by the false belief behind it. */
enum { S_UNSEEN, S_COOLDOWN, S_KILL, S_SHOT, S_OTHER, S_N };
typedef struct {
 int32_t steps,combat_steps,false_alarm;
 float pred_sum,actual_sum;               /* predicted vs actual combat damage (chosen action) */
 float melee_sum,shot_sum;                /* actual damage by source */
 int32_t surprise[S_N];float surprise_dmg[S_N];
 int32_t hp_n,kill_over,kill_under,ghost; /* tracked enemies: matched, believed-kill errors, not there */
 float hp_abs;
 int32_t cd_n,cd_ready_missed,cd_cool_missed; /* adjacent melee: believed cooling but ready, and the reverse */
 int32_t hidden_near;                     /* steps with a hostile hidden in darkness within 2 cells */
 int32_t death_mask;                      /* surprise categories in the 10 steps before death */
} Audit;
static int g_oracle=0,g_audit=0;
static const CraftaxState *g_state;static Audit *g_cur;

/* Engine truth at agent cell p on the current floor (frame offset R,C). */
static int truth_at(const CraftaxState *st,int kind,int p,int R,int C,float *hp,int *cd,int *type) {
 int L=st->player_level,r=p/FP_SIDE-R,c=p%FP_SIDE-C;
 if(!kind) {
  for(int i=0;i<CRAFTAX_MAX_MELEE_MOBS;i++)if(st->melee_mobs.mask[L][i] && st->melee_mobs.position[L][i][0]==r && st->melee_mobs.position[L][i][1]==c)
   {*hp=st->melee_mobs.health[L][i];*cd=st->melee_mobs.attack_cooldown[L][i];*type=st->melee_mobs.type_id[L][i]+1;return 1;}
 } else {
  for(int i=0;i<CRAFTAX_MAX_RANGED_MOBS;i++)if(st->ranged_mobs.mask[L][i] && st->ranged_mobs.position[L][i][0]==r && st->ranged_mobs.position[L][i][1]==c)
   {*hp=st->ranged_mobs.health[L][i];*cd=st->ranged_mobs.attack_cooldown[L][i];*type=st->ranged_mobs.type_id[L][i]+1;return 1;}
 }
 return 0;
}
static int hidden(const CraftaxState *st,int r,int c) {
 int L=st->player_level;
 return r<0 || c<0 || r>=CRAFTAX_MAP_SIZE || c>=CRAFTAX_MAP_SIZE || st->light_map[L][r][c]<=12;
}
static void audit_beliefs(const FPAgent *a,const FPObservation *o,const CraftaxState *st,int R,int C) {
 Audit *u=g_cur;int pr=st->player_position[0],pc=st->player_position[1];
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];float hp;int cd,type;
  if(!truth_at(st,e.kind,e.p,R,C,&hp,&cd,&type) || type!=e.type){u->ghost++;continue;}
  float blow=fp_wm_hit_damage(o,e.type,e.kind);
  u->hp_n++;u->hp_abs+=fabsf(e.health-hp);
  u->kill_over+=blow>=e.health && blow<hp;u->kill_under+=blow<e.health && blow>=hp;
  if(!e.kind && abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)==1) {
   u->cd_n++;u->cd_ready_missed+=e.cooldown>0 && cd<=0;u->cd_cool_missed+=e.cooldown<=0 && cd>0;
  }
 }
 int L=st->player_level,near=0;
 for(int i=0;i<CRAFTAX_MAX_MELEE_MOBS;i++)if(st->melee_mobs.mask[L][i]) {
  int r=st->melee_mobs.position[L][i][0],c=st->melee_mobs.position[L][i][1];
  if(abs(r-pr)+abs(c-pc)<=2 && hidden(st,r,c))near=1;
 }
 for(int i=0;i<CRAFTAX_MAX_RANGED_MOBS;i++)if(st->ranged_mobs.mask[L][i]) {
  int r=st->ranged_mobs.position[L][i][0],c=st->ranged_mobs.position[L][i][1];
  if(abs(r-pr)+abs(c-pc)<=2 && hidden(st,r,c))near=1;
 }
 u->hidden_near+=near;
}
static void apply_oracle(FPAgent *a,FPScratch *s,const CraftaxState *st,int R,int C) {
 int L=st->player_level,pr=st->player_position[0],pc=st->player_position[1];
 if(g_oracle&(OR_HP|OR_CD))for(int i=0;i<a->enemy_count;i++) {
  FPEnemy *e=&a->enemies[i];float hp;int cd,type;
  if(!truth_at(st,e->kind,e->p,R,C,&hp,&cd,&type) || type!=e->type)continue;
  if(g_oracle&OR_HP){e->health=hp;s->enemy_health[e->p]=hp;}
  if(g_oracle&OR_CD){e->cooldown=cd;s->cooldown[e->p]=cd;}
 }
 if(g_oracle&OR_SHOTS) {
  /* Danger cells of every mob projectile: its cell and the next one. */
  memset(s->arrow_next,0,FP_CELLS);
  for(int j=0;j<CRAFTAX_MAX_MOB_PROJECTILES;j++)if(st->mob_projectiles.mask[L][j]) {
   int r=st->mob_projectiles.position[L][j][0]+R,c=st->mob_projectiles.position[L][j][1]+C;
   int r2=r+st->mob_projectile_directions[L][j][0],c2=c+st->mob_projectile_directions[L][j][1];
   if(r>=0 && r<FP_SIDE && c>=0 && c<FP_SIDE)s->arrow_next[r*FP_SIDE+c]=1;
   if(r2>=0 && r2<FP_SIDE && c2>=0 && c2<FP_SIDE)s->arrow_next[r2*FP_SIDE+c2]=1;
  }
 }
 if(g_oracle&OR_UNSEEN) {
  /* Enemies within the view window that darkness hides. */
  for(int kind=0;kind<2;kind++)for(int i=0;i<(kind?CRAFTAX_MAX_RANGED_MOBS:CRAFTAX_MAX_MELEE_MOBS);i++) {
   int on=kind?st->ranged_mobs.mask[L][i]:st->melee_mobs.mask[L][i];if(!on)continue;
   int r=kind?st->ranged_mobs.position[L][i][0]:st->melee_mobs.position[L][i][0];
   int c=kind?st->ranged_mobs.position[L][i][1]:st->melee_mobs.position[L][i][1];
   if(abs(r-pr)>4 || abs(c-pc)>5 || !hidden(st,r,c) || a->enemy_count>=8)continue;
   int p=(r+R)*FP_SIDE+(c+C);if(r+R<0 || r+R>=FP_SIDE || c+C<0 || c+C>=FP_SIDE)continue;
   int dup=0;for(int k=0;k<a->enemy_count;k++)dup|=a->enemies[k].p==p;if(dup)continue;
   int type=(kind?st->ranged_mobs.type_id[L][i]:st->melee_mobs.type_id[L][i])+1;
   FPEnemy e={p,type,kind,kind?st->ranged_mobs.attack_cooldown[L][i]:st->melee_mobs.attack_cooldown[L][i],
              kind?st->ranged_mobs.health[L][i]:st->melee_mobs.health[L][i]};
   a->enemies[a->enemy_count++]=e;
   if(kind)s->ranged[p]=(uint8_t)type;else s->melee[p]=(uint8_t)type;
   s->mobs[p]|=kind?4:1;s->enemy_health[p]=e.health;s->cooldown[p]=e.cooldown;
  }
 }
}
static void wm_hook(FPAgent *a,const FPObservation *o,FPScratch *s,void *ctx) {
 (void)ctx;const CraftaxState *st=g_state;
 if(!st || st->player_level!=a->level || o->level!=a->level)return;
 int R=a->r-st->player_position[0],C=a->c-st->player_position[1];
 if(g_audit && g_cur)audit_beliefs(a,o,st,R,C);
 if(g_oracle)apply_oracle(a,s,st,R,C);
}

/* ---------------- episodes ---------------- */
typedef struct {
 int ep,t,max_level,arrive[9];uint32_t world_seed;
 uint64_t hash;double total;float previous_reward;CraftaxThreefryKey key;
 int ring[10],nring;   /* surprise masks of the last ten steps (audit) */
} Run;
static int g_stop_floor=8,g_horizon=100000;
typedef void (*Hook)(void *ctx,const Run *x,const CraftaxState *st,const FPAgent *ag);
static int running(const Run *x,const CraftaxState *st) {
 return x->t<g_horizon && !craftax_is_game_over_native(st) && st->player_level<g_stop_floor;
}
/* After the engine step: classify each combat hit the WM did not predict. */
static void audit_step(Run *x,const CraftaxState *st,const FPAgent *ag,const FPScratch *sc,int action,
                       float pred,int level_before,const int *pre_cd,const int *pre_mask) {
 Audit *u=g_cur;float actual=g_dmg[0]+g_dmg[1];int mask=0;
 u->steps++;u->pred_sum+=pred;u->actual_sum+=actual;u->melee_sum+=g_dmg[0];u->shot_sum+=g_dmg[1];
 if(pred>0 || actual>0)u->combat_steps++;
 if(pred>0 && actual==0)u->false_alarm++;
 if(st->player_level==level_before && ag->level==level_before) {
  int L=level_before,R=ag->r-st->player_position[0],C=ag->c-st->player_position[1];
  /* Faced cell at decision time (a strike does not move us). */
  static const int DR[5]={0,0,0,-1,1},DC[5]={0,-1,1,0,0};
  int dir=ag->previous.dir,faced=dir>=1 && dir<=4?(ag->r+DR[dir])*FP_SIDE+(ag->c+DC[dir]):-1;
  for(int i=0;i<CRAFTAX_MAX_MELEE_MOBS;i++) {
   if(!pre_mask[i] || !st->melee_mobs.mask[L][i] || pre_cd[i]>0 || st->melee_mobs.attack_cooldown[L][i]!=5)continue;
   int r=st->melee_mobs.position[L][i][0],c=st->melee_mobs.position[L][i][1];
   if(abs(r-st->player_position[0])+abs(c-st->player_position[1])!=1)continue;
   int q=(r+R)*FP_SIDE+(c+C),type=st->melee_mobs.type_id[L][i]+1,k=-1;
   for(int j=0;j<ag->enemy_count;j++)if(!ag->enemies[j].kind && ag->enemies[j].p==q && ag->enemies[j].type==type)k=j;
   float dmg=g_dmg[0]/(g_hits[0]>0?g_hits[0]:1);int cat=-1;
   if(k<0)cat=S_UNSEEN;
   else if(ag->enemies[k].cooldown>0)cat=S_COOLDOWN;
   else if(action==FP_DO && q==faced && fp_wm_hit_damage(&ag->previous,type,0)>=ag->enemies[k].health)cat=S_KILL;
   else if(pred<=0)cat=S_OTHER;
   if(cat>=0){u->surprise[cat]++;u->surprise_dmg[cat]+=dmg;mask|=1<<cat;}
  }
  if(g_dmg[1]>0 && !sc->arrow_next[ag->r*FP_SIDE+ag->c]){u->surprise[S_SHOT]++;u->surprise_dmg[S_SHOT]+=g_dmg[1];mask|=1<<S_SHOT;}
 }
 x->ring[x->nring++%10]=mask;
}
static int step_once(Run *x,CraftaxState *st,FPAgent *ag,FPScratch *sc) {
 static FPConfig cfg;static int init=0;if(!init){cfg=fp_default_config();init=1;}
 uint8_t record[996];FPObservation obs;craftax_encode_native_observation(st,record);fp_decode_compact(&obs,record);
 g_state=st;
 fp_observe(ag,&obs,x->previous_reward,sc);
 float pred[6]={0};
 if(g_audit)for(int k=0;k<6;k++)pred[k]=fp_wm_incoming_damage(ag,&obs,sc,k);
 int action=fp_decide(ag,&obs,&cfg,sc);
 int L=st->player_level,pre_cd[CRAFTAX_MAX_MELEE_MOBS],pre_mask[CRAFTAX_MAX_MELEE_MOBS];
 for(int i=0;i<CRAFTAX_MAX_MELEE_MOBS;i++){pre_cd[i]=st->melee_mobs.attack_cooldown[L][i];pre_mask[i]=st->melee_mobs.mask[L][i];}
 x->hash=(x->hash^(uint64_t)(action+1))*UINT64_C(0x100000001b3);
 CraftaxThreefryKey step_key,step_rng,reset_key;
 craftax_threefry_split(x->key,&x->key,&step_key);
 craftax_threefry_split(step_key,&step_rng,&reset_key);
 memset(g_dmg,0,sizeof g_dmg);g_hits[0]=g_hits[1]=0;
 x->previous_reward=craftax_gameplay_step_native(st,action,step_rng);x->total+=x->previous_reward;
 if(g_audit && g_cur)audit_step(x,st,ag,sc,action,(action>=1 && action<=5)?pred[action]:pred[0],L,pre_cd,pre_mask);
 int level=st->player_level;if(level>x->max_level)x->max_level=level;
 if(x->arrive[level]<0)x->arrive[level]=x->t+1;
 return action;
}
static void play(Run *x,CraftaxState *st,FPAgent *ag,FPScratch *sc,Hook hook,void *ctx) {
 for(;running(x,st);x->t++){if(hook)hook(ctx,x,st,ag);step_once(x,st,ag,sc);}
}
static void start_reset(Run *x,CraftaxState *st,FPAgent *ag,uint64_t seed,int ep) {
 memset(x,0,sizeof *x);x->ep=ep;x->world_seed=(uint32_t)mix(seed+(uint64_t)ep);
 Craftax env={0};env.state=st;env.seed=x->world_seed;craftax_reset_state_from_seed(&env);x->key=env.rng_key;
 fp_reset(ag,mix(x->world_seed));
 for(int i=0;i<9;i++)x->arrive[i]=-1;x->arrive[0]=0;x->hash=UINT64_C(0xcbf29ce484222325);
}
static void start_snap(Run *x,const SnapRec *r,int replica,FPAgent *ag) {
 memset(x,0,sizeof *x);
 x->ep=r->h.ep;x->world_seed=r->h.world_seed;x->t=r->h.t;x->max_level=r->h.max_level;x->hash=r->h.hash;
 x->total=r->h.total;x->previous_reward=r->h.previous_reward;x->key=r->h.rng_key;memcpy(x->arrive,r->h.arrive,sizeof x->arrive);
 if(replica>0) {
  uint64_t z=mix(((uint64_t)(uint32_t)r->h.ep<<20)^(uint64_t)replica^UINT64_C(0x5eed000000000000));
  x->key.word[0]^=(uint32_t)z;x->key.word[1]^=(uint32_t)(z>>32);
  ag->rng^=mix(z);if(!ag->rng)ag->rng=1;
 }
}

/* ---------------- worker pool ---------------- */
typedef struct { int32_t max_level,died,t_end,level_end,arrive[9];float health;uint64_t hash;Audit audit; } Res;
typedef struct { volatile int64_t next;int64_t pad[7]; } Counter;
static int g_workers=8;
typedef void (*Chunk)(int64_t chunk,Res *out,CraftaxState *st,FPAgent *ag,FPScratch *sc);
static void pool(int64_t nchunks,int per_chunk,Res *results,Chunk fn) {
 Counter *c=mmap(NULL,sizeof *c,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
 if(c==MAP_FAILED)die("mmap failed");c->next=0;
 int W=g_workers;pid_t *pids=calloc(W,sizeof *pids);fflush(stdout);fflush(stderr);
 for(int w=0;w<W;w++) {
  pid_t p=fork();if(p<0)die("fork failed");
  if(!p) {
   CraftaxState *st=calloc(1,sizeof *st);FPAgent *ag=malloc(sizeof *ag);FPScratch *sc=malloc(sizeof *sc);
   for(;;) {
    int64_t k=__atomic_fetch_add(&c->next,1,__ATOMIC_SEQ_CST);if(k>=nchunks)break;
    fn(k,results+k*per_chunk,st,ag,sc);
   }
   _exit(0);
  }
  pids[w]=p;
 }
 int bad=0;
 for(int w=0;w<W;w++){int status;waitpid(pids[w],&status,0);if(!WIFEXITED(status)||WEXITSTATUS(status))bad=1;}
 if(bad)die("a worker failed");
 munmap(c,sizeof *c);free(pids);
}
static Res *shared_results(int64_t n) {
 Res *r=mmap(NULL,(size_t)n*sizeof *r,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
 if(r==MAP_FAILED)die("mmap failed");memset(r,0,(size_t)n*sizeof *r);return r;
}
static void finish(Res *o,const Run *x,const CraftaxState *st) {
 o->max_level=x->max_level;o->died=st->player_health<=0.0f;o->t_end=x->t;o->level_end=st->player_level;
 o->health=st->player_health;o->hash=x->hash;memcpy(o->arrive,x->arrive,sizeof o->arrive);
 if(g_audit && o->died){int m=0;for(int i=0;i<10 && i<x->nring;i++)m|=x->ring[i];o->audit.death_mask=m;}
}

/* ---------------- capture ---------------- */
static uint64_t g_seed;static int g_start,g_count,g_levels[9],g_nlevels;static const char *g_out;
typedef struct { int fired[9];FILE *f[9]; } CapCtx;
static void cap_hook(void *ctx,const Run *x,const CraftaxState *st,const FPAgent *ag) {
 CapCtx *c=ctx;int L=st->player_level;
 if(c->fired[L] || !c->f[L])return;c->fired[L]=1;
 SnapRec r;memset(&r,0,sizeof r);snap_encode(&r,st,ag);
 r.h.level=L;r.h.ep=x->ep;r.h.world_seed=x->world_seed;r.h.t=x->t;r.h.max_level=x->max_level;
 r.h.previous_reward=x->previous_reward;r.h.total=x->total;r.h.hash=x->hash;r.h.rng_key=x->key;
 memcpy(r.h.arrive,x->arrive,sizeof r.h.arrive);
 snap_write(c->f[L],&r);free(r.data);
}
static void cap_chunk(int64_t k,Res *out,CraftaxState *st,FPAgent *ag,FPScratch *sc) {
 static CapCtx c;static int opened=0;char path[4096];
 if(!opened) {
  memset(&c,0,sizeof c);
  for(int i=0;i<g_nlevels;i++){int L=g_levels[i];snprintf(path,sizeof path,"%s/.w%d_arrive%d.bin",g_out,(int)getpid(),L);c.f[L]=fopen(path,"wb");if(!c.f[L]){perror(path);_exit(1);}setvbuf(c.f[L],NULL,_IOFBF,1<<20);}
  opened=1;
 }
 memset(c.fired,0,sizeof c.fired);
 Run x;start_reset(&x,st,ag,g_seed,g_start+(int)k);play(&x,st,ag,sc,cap_hook,&c);
 for(int i=0;i<g_nlevels;i++)fflush(c.f[g_levels[i]]);
 finish(out,&x,st);
}
static int cmp_ep(const void *a,const void *b){const SnapRec *x=a,*y=b;return x->h.ep<y->h.ep?-1:x->h.ep>y->h.ep;}
static void merge_level(int L) {
 char path[4096];SnapRec *all=NULL;int n=0,cap=0;
 DIR *d=opendir(g_out);if(!d)die("cannot open output directory");
 struct dirent *e;char suffix[64];snprintf(suffix,sizeof suffix,"_arrive%d.bin",L);
 while((e=readdir(d))) {
  size_t ln=strlen(e->d_name),ls=strlen(suffix);
  if(strncmp(e->d_name,".w",2) || ln<ls || strcmp(e->d_name+ln-ls,suffix))continue;
  snprintf(path,sizeof path,"%s/%s",g_out,e->d_name);FILE *f=fopen(path,"rb");if(!f){perror(path);exit(1);}
  SnapRec r;while(snap_read(f,&r,1)){if(n==cap){cap=cap?2*cap:1024;all=realloc(all,cap*sizeof *all);}all[n++]=r;}
  fclose(f);unlink(path);
 }
 closedir(d);qsort(all,n,sizeof *all,cmp_ep);
 snprintf(path,sizeof path,"%s/arrive%d.bin",g_out,L);FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 for(int i=0;i<n;i++){snap_write(f,&all[i]);free(all[i].data);}
 fclose(f);free(all);fprintf(stderr,"arrive%d: %d snapshots\n",L,n);
}

/* ---------------- eval / reset ---------------- */
static SnapRec *g_bank;static int g_nbank,g_r0=0,g_r1=1;
static void eval_chunk(int64_t k,Res *out,CraftaxState *st,FPAgent *ag,FPScratch *sc) {
 const SnapRec *r=&g_bank[k];int R=g_r1-g_r0;
 CraftaxState *saved=malloc(sizeof *saved);FPAgent *saved_ag=malloc(sizeof *saved_ag);
 snap_decode(r,saved,saved_ag);
 for(int q=0;q<R;q++) {
  Run x;memcpy(st,saved,sizeof *st);memcpy(ag,saved_ag,sizeof *ag);start_snap(&x,r,g_r0+q,ag);
  g_cur=&out[q].audit;play(&x,st,ag,sc,NULL,NULL);finish(&out[q],&x,st);g_cur=NULL;
 }
 free(saved);free(saved_ag);
}
static void reset_chunk(int64_t k,Res *out,CraftaxState *st,FPAgent *ag,FPScratch *sc) {
 Run x;start_reset(&x,st,ag,g_seed,g_start+(int)k);g_cur=&out->audit;play(&x,st,ag,sc,NULL,NULL);finish(out,&x,st);g_cur=NULL;
}
static void audit_header(FILE *f) {
 if(!g_audit)return;
 fprintf(f,",steps,combat_steps,false_alarm,pred_sum,actual_sum,melee_sum,shot_sum");
 static const char *n[S_N]={"unseen","cooldown","kill","shot","other"};
 for(int i=0;i<S_N;i++)fprintf(f,",s_%s,sd_%s",n[i],n[i]);
 fprintf(f,",hp_n,hp_abs,kill_over,kill_under,ghost,cd_n,cd_ready_missed,cd_cool_missed,hidden_near,death_mask");
}
static void audit_row(FILE *f,const Audit *u) {
 if(!g_audit)return;
 fprintf(f,",%d,%d,%d,%.3f,%.3f,%.3f,%.3f",u->steps,u->combat_steps,u->false_alarm,u->pred_sum,u->actual_sum,u->melee_sum,u->shot_sum);
 for(int i=0;i<S_N;i++)fprintf(f,",%d,%.3f",u->surprise[i],u->surprise_dmg[i]);
 fprintf(f,",%d,%.3f,%d,%d,%d,%d,%d,%d,%d,%d",u->hp_n,u->hp_abs,u->kill_over,u->kill_under,u->ghost,u->cd_n,
  u->cd_ready_missed,u->cd_cool_missed,u->hidden_near,u->death_mask);
}

/* ---------------- main ---------------- */
static int parse_int(const char *p){char *e;errno=0;long v=strtol(p,&e,10);if(errno||!*p||*e)die("bad integer");return (int)v;}
int main(int argc,char **argv) {
 if(argc<2)die("usage: wm_eval capture|eval|reset [options]");
 const char *mode=argv[1],*bank=NULL,*select=NULL;
 for(int i=2;i<argc;i++) {
  const char *a=argv[i];if(i+1>=argc){fprintf(stderr,"missing value for %s\n",a);return 2;}const char *v=argv[++i];
  if(!strcmp(a,"--seed"))g_seed=(uint64_t)strtoull(v,NULL,10);
  else if(!strcmp(a,"--start"))g_start=parse_int(v);
  else if(!strcmp(a,"--count"))g_count=parse_int(v);
  else if(!strcmp(a,"--workers"))g_workers=parse_int(v);
  else if(!strcmp(a,"--stop-floor"))g_stop_floor=parse_int(v);
  else if(!strcmp(a,"--horizon"))g_horizon=parse_int(v);
  else if(!strcmp(a,"--bank"))bank=v;
  else if(!strcmp(a,"--select"))select=v;
  else if(!strcmp(a,"--out"))g_out=v;
  else if(!strcmp(a,"--audit"))g_audit=parse_int(v);
  else if(!strcmp(a,"--replicas")){if(sscanf(v,"%d:%d",&g_r0,&g_r1)!=2||g_r0<0||g_r1<=g_r0)die("--replicas A:B");}
  else if(!strcmp(a,"--oracle")) {
   char buf[256];snprintf(buf,sizeof buf,"%s",v);
   for(char *t=strtok(buf,",");t;t=strtok(NULL,",")) {
    if(!strcmp(t,"hp"))g_oracle|=OR_HP;else if(!strcmp(t,"cooldown"))g_oracle|=OR_CD;
    else if(!strcmp(t,"shots"))g_oracle|=OR_SHOTS;else if(!strcmp(t,"unseen"))g_oracle|=OR_UNSEEN;
    else if(!strcmp(t,"none"));else die("unknown oracle");
   }
  }
  else if(!strcmp(a,"--levels")) {
   char buf[256];snprintf(buf,sizeof buf,"%s",v);
   for(char *t=strtok(buf,",");t;t=strtok(NULL,",")){int L=parse_int(t);if(L<1||L>8)die("level out of range");g_levels[g_nlevels++]=L;}
  }
  else {fprintf(stderr,"unknown option %s\n",a);return 2;}
 }
 craftax_set_lazy_floors(1);craftax_wg_init_cell_templates();
 if(g_oracle || g_audit){fp_wm_oracle=wm_hook;fp_wm_oracle_ctx=NULL;}
 if(!strcmp(mode,"capture")) {
  if(!g_out||!g_count||!g_nlevels)die("capture needs --out, --count and --levels");
  if(g_oracle||g_audit)die("capture runs the plain policy");
  Res *res=shared_results(g_count);pool(g_count,1,res,cap_chunk);
  for(int i=0;i<g_nlevels;i++)merge_level(g_levels[i]);
  char path[4096];snprintf(path,sizeof path,"%s/episodes.csv",g_out);FILE *f=fopen(path,"w");if(!f){perror(path);return 1;}
  fprintf(f,"ep,world_seed,max_level,died,t_end,health");for(int L=0;L<9;L++)fprintf(f,",arrive%d",L);fprintf(f,",hash\n");
  for(int k=0;k<g_count;k++){Res *r=&res[k];fprintf(f,"%d,%u,%d,%d,%d,%.3f",g_start+k,(uint32_t)mix(g_seed+(uint64_t)(g_start+k)),r->max_level,r->died,r->t_end,r->health);
   for(int L=0;L<9;L++)fprintf(f,",%d",r->arrive[L]);fprintf(f,",%016llx\n",(unsigned long long)r->hash);}
  fclose(f);return 0;
 }
 if(!g_out)die("--out is required");
 FILE *out=strcmp(g_out,"-")?fopen(g_out,"w"):stdout;if(!out){perror(g_out);return 1;}
 if(!strcmp(mode,"eval")) {
  if(!bank)die("eval needs --bank");
  int *want=NULL,nwant=0;
  if(select){FILE *f=fopen(select,"r");if(!f){perror(select);return 1;}int x,cap=0;while(fscanf(f,"%d",&x)==1){if(nwant==cap){cap=cap?2*cap:1024;want=realloc(want,cap*sizeof *want);}want[nwant++]=x;}fclose(f);}
  for(int i=1;i<nwant;i++)if(want[i]<=want[i-1])die("--select indices must be strictly increasing");
  FILE *fb=fopen(bank,"rb");if(!fb){perror(bank);return 1;}
  int n=0,cap=0,idx=0,*index_of=NULL,w=0;SnapRec r;
  while(snap_read(fb,&r,!select || (w<nwant && want[w]==idx))) {
   if(r.data){if(n==cap){cap=cap?2*cap:1024;g_bank=realloc(g_bank,cap*sizeof *g_bank);index_of=realloc(index_of,cap*sizeof *index_of);}index_of[n]=idx;g_bank[n++]=r;w++;}
   idx++;
  }
  fclose(fb);g_nbank=n;if(select && w!=nwant)die("--select index beyond the bank");
  int R=g_r1-g_r0;Res *res=shared_results((int64_t)n*R);pool(n,R,res,eval_chunk);
  fprintf(out,"index,ep,world_seed,snap_t,replica,max_level,died,t_end,level_end,health,hash");audit_header(out);fprintf(out,"\n");
  for(int i=0;i<n;i++)for(int q=0;q<R;q++) {
   Res *o=&res[(int64_t)i*R+q];
   fprintf(out,"%d,%d,%u,%d,%d,%d,%d,%d,%d,%.3f,%016llx",index_of[i],g_bank[i].h.ep,g_bank[i].h.world_seed,g_bank[i].h.t,g_r0+q,
    o->max_level,o->died,o->t_end,o->level_end,o->health,(unsigned long long)o->hash);
   audit_row(out,&o->audit);fprintf(out,"\n");
  }
 } else if(!strcmp(mode,"reset")) {
  if(!g_count)die("reset needs --count");
  Res *res=shared_results(g_count);pool(g_count,1,res,reset_chunk);
  fprintf(out,"ep,world_seed,max_level,died,t_end,level_end,health");for(int L=1;L<9;L++)fprintf(out,",arrive%d",L);fprintf(out,",hash");audit_header(out);fprintf(out,"\n");
  for(int k=0;k<g_count;k++) {
   Res *o=&res[k];
   fprintf(out,"%d,%u,%d,%d,%d,%d,%.3f",g_start+k,(uint32_t)mix(g_seed+(uint64_t)(g_start+k)),o->max_level,o->died,o->t_end,o->level_end,o->health);
   for(int L=1;L<9;L++)fprintf(out,",%d",o->arrive[L]);fprintf(out,",%016llx",(unsigned long long)o->hash);
   audit_row(out,&o->audit);fprintf(out,"\n");
  }
 } else die("unknown mode");
 if(out!=stdout)fclose(out);
 return 0;
}
