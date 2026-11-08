#define TV 0x120587D
#include "full_policy.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The parent is retained during a child call; returns preserve its identity. */
static void runtime_push(FPAgent *a,int kind,int frontier) {
 FPGoalRuntime *g=&a->runtime;FPGoalFrame *f=&g->frame[g->depth];
 memset(f,0,sizeof *f);f->kind=kind;f->frontier=frontier;f->entry_t=a->t;
 f->return_outcome=FP_RO_COMPLETED;f->in=g->observed;
 f->id=++g->next_id;f->parent=g->depth?g->frame[g->depth-1].id:0;g->depth++;
}
void fp_goal_return(FPAgent *a,int outcome) {
 FPGoalRuntime *g=&a->runtime;if(!g->depth)return;
 g->last_return=g->frame[--g->depth];g->last_outcome=outcome;g->boundary_t=a->t;
 g->event[g->returns%4]=(FPGoalEvent){g->last_return,g->observed,a->t,outcome};
 g->returns++;g->completions+=outcome==FP_RO_COMPLETED;g->abandonments+=outcome==FP_RO_ABANDONED;
 if(g->depth)g->frame[g->depth-1].suspended=0;
}
void fp_goal_finish(FPAgent *a,int outcome) { while(a->runtime.depth)fp_goal_return(a,outcome); }
int fp_goal_call_excursion(FPAgent *a,int purpose,int mode,int target) {
 FPGoalRuntime *g=&a->runtime;
 if(g->depth!=1 || g->frame[0].kind!=FP_RG_EXPEDITION)return 0;
 g->frame[0].suspended=1;runtime_push(a,FP_RG_EXCURSION,g->frame[0].frontier);
 g->frame[1].purpose=purpose;g->frame[1].mode=mode;g->frame[1].target=target;return 1;
}
static void runtime_observe(FPAgent *a,const FPObservation *o) {
 FPGoalRuntime *g=&a->runtime;int frontier=0;
 for(int k=0;k<FP_LEVELS;k++)if(a->floors[k].seen)frontier=k;
 if(o->level>frontier)frontier=o->level;
 g->observed=(FPGoalPublic){o->level,o->food,o->drink,o->energy,o->health};
 if(o->health<=0){fp_goal_finish(a,FP_RO_DIED);return;}
 if(g->depth==2) {
  FPGoalFrame *f=&g->frame[1];
  if(o->level<f->frontier)f->departed=1;
  if(f->departed && o->level>=f->frontier)fp_goal_return(a,f->return_outcome);
 }
 if(g->depth && o->level>g->frame[0].frontier) {
  if(g->depth==2)fp_goal_return(a,g->frame[1].return_outcome);
  fp_goal_return(a,FP_RO_COMPLETED);
 }
 if(!g->depth && frontier>=5 && frontier<8) {
  runtime_push(a,FP_RG_EXPEDITION,frontier);
  /* One-time migration of old banks captured inside an existing child. */
  if(o->level<frontier){fp_goal_call_excursion(a,0,0,frontier);g->frame[1].departed=1;}
 }
}

enum { UNKNOWN=255, INF=0x3f3f3f3f, VAULT_UNUSABLE=1000000,
       GRAVEYARD_TORCH_GAIN=25, GRAVEYARD_RECOVER_HEALTH=10, ICE_TORCH_GAIN=25,
       ICE_BASE_DIST=400 };
/* Optional, OFF: stock coal and wood on cleared Fire for a torch-hungry Ice
 * explorer (requested by the Ice stream; untested end to end). */
#ifndef FIRE_ICEPREP
#define FIRE_ICEPREP 0
#endif
/* Troll-stream variant bits (development only; stripped from the candidate). */
#ifndef TV
#define TV 0
#endif
#define TB(n) ((TV>>(n))&1)
static const int dr[5]={0,0,0,-1,1},dc[5]={0,-1,1,0,0};
static int cell(int r,int c) { return r*FP_SIDE+c; }
static int inside(int r,int c) { return (unsigned)r<FP_SIDE && (unsigned)c<FP_SIDE; }
static int imin(int a,int b) { return a<b?a:b; }
/* Troll-stream macro-state: from the first Troll Mines arrival until the
 * first Fire arrival, including return trips from the mines. */
static int troll_phase(const FPAgent *a) { return a->floors[5].seen && !a->floors[6].seen; }
/* The first Fire expedition: from the first Fire arrival, including climbs
 * back to the mines for water, until the first Ice arrival. */
static int fire_phase(const FPAgent *a) { return a->floors[6].seen && !a->floors[7].seen; }
/* Icetrip stream: every excursion off Ice after the first Ice arrival (Fire
 * food visits and crossings, Troll Mines stays, Sewers enchant trips). */
static int icetrip(const FPAgent *a) { return a->floors[7].seen && a->level<7; }
/* Troll Mines protections written for the troll phase (arrival-hit inference,
 * deep-thing firing lines, unseen-attacker torches, provisioning before a
 * heal) also hold on every mines visit of an Ice excursion. */
static int mines_guard(const FPAgent *a);
/* Icetrip components (compile-time switches for screening). */
#ifndef ICETRIP_ESCAPE_NEAR
#define ICETRIP_ESCAPE_NEAR 0
#endif
#ifndef ICETRIP_MINESTAY
#define ICETRIP_MINESTAY 0
#endif
#ifndef ICETRIP_LOCK
#define ICETRIP_LOCK 0
#endif
#ifndef ICETRIP_L5SAFE
#define ICETRIP_L5SAFE 1
#endif
static int mines_guard(const FPAgent *a) { return troll_phase(a) || (ICETRIP_L5SAFE && icetrip(a)); }
#ifndef ICETRIP_PFACE
#define ICETRIP_PFACE 0
#endif
#ifndef ICETRIP_FLINE
#define ICETRIP_FLINE 1
#endif
#ifndef ICETRIP_WOOD
#define ICETRIP_WOOD 1
#endif
#ifndef ICETRIP_GEMS
#define ICETRIP_GEMS 1
#endif
#ifndef ICETRIP_DIA_SEEK
#define ICETRIP_DIA_SEEK 50
#endif
#ifndef ICETRIP_DIA_STEPS
#define ICETRIP_DIA_STEPS 250
#endif
#ifndef ICETRIP_DIA_GAIN
#define ICETRIP_DIA_GAIN 16
#endif
#ifndef ICETRIP_DIA_PATH
#define ICETRIP_DIA_PATH 400
#endif
#ifndef ICETRIP_DIA
#define ICETRIP_DIA 1
#endif
#ifndef ICETRIP_EVADE
#define ICETRIP_EVADE 0
#endif
#include "full_wm.inc"
/* Helpers defined in the module files (m_*.inc), included before the selector. */
static int explore(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int troll_explore(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int travel(FPAgent *a,const FPObservation *o,FPScratch *s,int down);
static int frontier_walk(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int graveyard_light(FPAgent *a,const FPObservation *o,FPScratch *s);
static int light_unseen(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost);
static int dark_probe(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost);
static int ice_dark_entry(FPAgent *a,const FPObservation *o,FPScratch *s);
static int troll_dark_entry(FPAgent *a,const FPObservation *o,FPScratch *s);
static int water_return(FPAgent *a,const FPObservation *o,FPScratch *s,int water_cost);
static int seek_water(FPAgent *a,const FPObservation *o,FPScratch *s);
static int forage(FPAgent *a,const FPObservation *o,FPScratch *s);
static void wall_position(int i,int w,int h,int *r,int *c,int *d);
static int chamber_walls(FPAgent *a,const FPObservation *o,FPScratch *s,int anchor,int w,int h);
static int room_has(int anchor,int w,int h,int p);
static int ice_base(const FPAgent *a,const FPScratch *s);
static int recover(FPAgent *a,const FPObservation *o,FPScratch *s);
static int water_cost(const FPAgent *a,FPScratch *s);
static int sealed_in(FPAgent *a,const FPObservation *o,FPScratch *s);
static int icetrip_home(FPAgent *a,const FPObservation *o,FPScratch *s,int *skill);
static int icetrip_forage(FPAgent *a,const FPObservation *o,FPScratch *s,int *skill);
static int icetrip_lock(FPAgent *a,const FPObservation *o,FPScratch *s);
static int icetrip_seal(FPAgent *a,const FPObservation *o,FPScratch *s);
static float fire_hit(const FPObservation *o);
static float fire_retreat(const FPObservation *o);
static int tactical(FPAgent *a,const FPObservation *o,FPScratch *s);
static int ice_wood(FPAgent *a,const FPObservation *o,FPScratch *s);
static int craft_radius(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action,int radius);
static int craft(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action);
static int enchant_trip(const FPAgent *a,const FPObservation *o);
static int vault_limit(const FPAgent *a,const FPObservation *o);
static int vault_trip_ok(const FPAgent *a,const FPObservation *o);
static int troll_mana_ok(const FPAgent *a,const FPObservation *o);
static uint64_t word(FPAgent *a) {
 uint64_t x=a->rng;x^=x>>12;x^=x<<25;x^=x>>27;a->rng=x;
 return x*UINT64_C(2685821657736338717);
}
FPConfig fp_default_config(void) {
 return (FPConfig){5,5,3,20,12,4,2000,1200,1,3,4,1.5,2};
}
void fp_reset(FPAgent *a,uint64_t seed) {
 memset(a,0,sizeof(*a));wm_reset(a);
 a->last_frontier=a->recovery_goal=-1;a->water_goal=-1;
 a->rng=seed?seed:UINT64_C(0x9e3779b97f4a7c15);
}
void fp_reset_cold(FPAgent *a,uint64_t seed,const FPObservation *o) {
 fp_reset(a,seed);wm_reset_cold(a,o);
}
static void scalars(FPObservation *o,const float *p) {
 for(int i=0;i<10;i++)o->inv[i]=(int)lroundf(p[i]*p[i]*100);
 o->inv[FP_I_BOOK]=(int)lroundf(p[10]*2);
 o->inv[FP_I_PICK]=(int)lroundf(p[11]*4);o->inv[FP_I_SWORD]=(int)lroundf(p[12]*4);
 o->sword_enchant=(int)lroundf(p[13]);o->bow_enchant=(int)lroundf(p[14]);o->bow=p[15]>0.5f;
 for(int i=0;i<6;i++)o->potions[i]=(int)lroundf(p[16+i]*p[16+i]*100);
 o->health=p[22]*10;o->food=(int)lroundf(p[23]*10);o->drink=(int)lroundf(p[24]*10);
 o->energy=(int)lroundf(p[25]*10);o->mana=(int)lroundf(p[26]*10);o->xp=(int)lroundf(p[27]*10);
 o->dex=(int)lroundf(p[28]*10);o->str=(int)lroundf(p[29]*10);o->intelligence=(int)lroundf(p[30]*10);
 o->dir=3;for(int i=0;i<4;i++)if(p[31+i]>0.5f)o->dir=i+1;
 for(int i=0;i<4;i++){o->armour[i]=(int)lroundf(p[35+i]*2);o->armour_enchant[i]=(int)lroundf(p[39+i]);}
 o->light=p[43];o->sleeping=p[44]>0.5f;o->resting=p[45]>0.5f;
 o->spells[0]=p[46]>0.5f;o->spells[1]=p[47]>0.5f;o->level=(int)lroundf(p[48]*10);
 o->clear=p[49]>0.5f;o->boss_vulnerable=p[50]>0.5f;
}
void fp_decode_compact(FPObservation *o,const uint8_t *record) {
 memset(o,0,sizeof(*o));
 for(int i=0;i<FP_VIEW;i++) {
  const uint8_t *p=record+8*i;o->blocks[i]=p[0];o->items[i]=p[1]?p[1]-1:0;
  o->visible[i]=p[2];memcpy(o->mobs[i],p+3,5);
 }
 float tail[51];memcpy(tail,record+792,sizeof(tail));scalars(o,tail);
}
void fp_decode_float(FPObservation *o,const float *record) {
 memset(o,0,sizeof(*o));
 for(int i=0;i<FP_VIEW;i++) {
  const float *p=record+8*i;o->blocks[i]=(uint8_t)lroundf(p[0]);
  o->items[i]=p[1]>0?(uint8_t)lroundf(p[1]-1):0;o->visible[i]=p[2]>0.5f;
  for(int m=0;m<5;m++)o->mobs[i][m]=(uint8_t)lroundf(p[3+m]);
 }
 scalars(o,record+792);
}

static int commit_action(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int skill);
static int icetrip_seal(FPAgent *a,const FPObservation *o,FPScratch *s);
static int tx_shield(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int *skill);
static int emit(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int skill) {
 /* Leave the Fire ladder airlock sealed behind us (see icetrip_lock). */
 if(action==FP_DESCEND && ICETRIP_LOCK && icetrip(a) && a->level==6 && !o->sleeping && !o->resting) {
  int seal=icetrip_seal(a,o,s);if(seal>=0){action=seal;skill=FP_RECOVER;}
 }
 int moving=!o->sleeping && !o->resting && action>=FP_LEFT && action<=FP_DOWN;
 if(moving && !known_direction(a,action)){action=FP_NOOP;moving=0;}
 float movement_damage=moving?wm_incoming_damage(a,o,s,action):0;
 /* Melee enemies attack after our movement, before their own movement.
  * When safe here, let an enemy two cells away approach instead of giving
  * it a free hit by stepping into reach. This also preserves our facing
  * for an immediate sword strike on the next observation. */
 /* In Ice, an approach can establish the facing needed for three fireballs
  * during the enemy's cooldown. Accept it only with enough mana and more
  * than six health remaining after the predicted attack. */
 int can_aim=o->level==7 && o->dir!=action && o->spells[0] && o->mana>=6 && movement_damage<o->health-6;
 if(moving && !can_aim && a->enemy_count && movement_damage>0 && wm_incoming_damage(a,o,s,FP_NOOP)==0) {
  int rr=a->r+dr[action],cc=a->c+dc[action];
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];
   if(!e.kind && e.cooldown<=0 &&
      abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)==2 &&
      abs(e.p/FP_SIDE-rr)+abs(e.p%FP_SIDE-cc)==1) {
    action=FP_NOOP;skill=FP_FIGHT;moving=0;break;
   }
  }
 }
 /* A slimeball costs most of a Troll Mines health bar. Do not step into a
  * predicted shot path while the current cell is safe. */
 if(moving && (a->level==4 || a->level==5) && movement_damage>0 && wm_incoming_damage(a,o,s,FP_NOOP)==0){action=FP_NOOP;skill=FP_FIGHT;moving=0;movement_damage=0;}
 /* Check the destination too: survival, crafting, and exploration can all
  * choose a move into an attack that is not adjacent to the current cell. */
 if(moving && movement_damage>=o->health-0.1f) {
  int best=FP_NOOP;float damage=wm_incoming_damage(a,o,s,best);
  for(int d=FP_LEFT;d<=FP_DOWN;d++) {
   int rr=a->r+dr[d],cc=a->c+dc[d];
   if(!inside(rr,cc)||!walkable(block(a,rr,cc))||s->mobs[cell(rr,cc)])continue;
   float next=wm_incoming_damage(a,o,s,d);if(next<damage){best=d;damage=next;}
  }
  action=best;skill=FP_FIGHT;
 }
 /* Model-predictive safety shield (full_tactics.inc). */
 action=tx_shield(a,o,s,action,&skill);
 return commit_action(a,o,s,action,skill);
}
static int commit_action(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int skill) {
 /* A committed upward departure calls the child before the environment acts.
  * A failed ascent retains the child until explicit abandonment or departure. */
 if(action==FP_ASCEND && a->runtime.depth==1 && o->level==a->runtime.frame[0].frontier)
  fp_goal_call_excursion(a,0,0,o->level);
 a->skill=skill;wm_commit(a,o,s,action);return action;
}
static void swap(FPScratch *s,int i,int j) {
 int p=s->heap[i];s->heap[i]=s->heap[j];s->heap[j]=p;s->pos[s->heap[i]]=i;s->pos[s->heap[j]]=j;
}
static void heap_update(FPScratch *s,int p) {
 int i=s->pos[p];if(i<0){i=s->heap_size++;s->heap[i]=p;s->pos[p]=i;}
 while(i>0){int j=(i-1)/2;if(s->dist[s->heap[j]]<=s->dist[p])break;swap(s,i,j);i=j;}
}
static int heap_pop(FPScratch *s) {
 int p=s->heap[0];s->pos[p]=-2;
 if(--s->heap_size){s->heap[0]=s->heap[s->heap_size];s->pos[s->heap[0]]=0;int i=0;
  for(;;){int j=2*i+1;if(j>=s->heap_size)break;
   if(j+1<s->heap_size && s->dist[s->heap[j+1]]<s->dist[s->heap[j]])j++;
   if(s->dist[s->heap[i]]<=s->dist[s->heap[j]])break;swap(s,i,j);i=j;}
 }return p;
}
static void paths(const FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 const FPFloor *f=&a->floors[a->level];
 /* Only observed terrain can enter the heap. Unreachable distances still
  * cover the entire atlas, including adjacent stands outside these bounds. */
 memset(s->dist,0x3f,sizeof(s->dist));
 if(f->known_hi>=f->known_lo)memset(s->pos+f->known_lo,0xff,(size_t)(f->known_hi-f->known_lo+1)*sizeof(*s->pos));
 s->heap_size=0;int start=cell(a->r,a->c);s->dist[start]=0;s->first[start]=0;s->pos[start]=-1;heap_update(s,start);
 while(s->heap_size) {
  int p=heap_pop(s),r=p/FP_SIDE,c=p%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int rr=r+dr[d],cc=c+dc[d];if(!inside(rr,cc))continue;
   /* A mob in a one-cell Dungeon corridor is an obstacle to fight through,
    * not a wall. Treating it as a wall made water, ladders and frontiers
    * reachable or not as the mob crossed the edge of the view, and agents
    * alternated between two rules for dozens of steps. Sewer corridors are
    * one cell wide too: thirsty agents swung between drinking and eating
    * beside a lizard until they starved (ep 22, 71). */
   int q=cell(rr,cc),b=f->map[q],cost=10;
   if(s->mobs[q]){if(a->level!=1 && a->level!=3 && a->level!=4)continue;cost+=100;}
   if(!walkable(b)) {
    if(mineable(o,b))cost=(b==FP_TABLE||b==FP_FURNACE)?100:30;
    else if((b==FP_WATER || b==FP_LAVA) && o->inv[1]>0 && o->inv[FP_I_PICK]>0 && !f->items[q])cost=60;
    else continue;
   }
   if(s->pos[q]==-2)continue;
   if((a->enemy_count || a->arrow_count) && abs(rr-a->r)+abs(cc-a->c)<=6)cost+=(int)(cfg->danger_cost*wm_risk(a,o,s,q,!a->drinking));
   if(s->dist[p]+cost<s->dist[q]){s->dist[q]=s->dist[p]+cost;s->first[q]=p==start?d:s->first[p];heap_update(s,q);}
  }
 }
}
static int move_path(FPAgent *a,const FPObservation *o,FPScratch *s,int p) {
 int d=s->first[p];if(!d)return FP_NOOP;int b=block(a,a->r+dr[d],a->c+dc[d]);
 /* Floor-1 and floor-3 paths may cross a mob (see paths): strike it rather than walk into it. */
 if((a->level==1 || a->level==3 || a->level==4) && inside(a->r+dr[d],a->c+dc[d]) && s->mobs[cell(a->r+dr[d],a->c+dc[d])])return o->dir==d?FP_DO:d;
 if(mineable(o,b) && !walkable(b))return o->dir==d?FP_DO:d;
 if(b==FP_WATER||b==FP_LAVA)return o->dir==d?FP_PLACE_STONE:d;
 return d;
}
static int seek(FPAgent *a,const FPObservation *o,FPScratch *s,uint64_t blocks,int action,int radius) {
 const FPFloor *f=&a->floors[a->level];int best=-1,facing=0,best_cost=INF;
 FPIntent *in=&a->intent;int committed=in->executing && !in->reference;
 if(committed && (in->bound_mask!=blocks || in->bound_level!=a->level)) {
  in->bound_target=-1;in->bound_mask=blocks;in->bound_level=a->level;
 }
 int bound=committed?in->bound_target:-1;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int b=f->map[p];if(b>=64 || !((blocks>>b)&1) || (bound>=0 && p!=bound))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  if(a->recovering && a->recovery_goal>=0 && (b==FP_STONE||b==FP_STALAGMITE)) {
   int ar=a->recovery_goal/FP_SIDE,ac=a->recovery_goal%FP_SIDE;
   if(r>=ar-1 && r<=ar+2 && c>=ac-1 && c<=ac+2)continue;
  }
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   /* Facing affects an interaction here, but movement changes it en route.
    * Pricing a distant stand by the current facing oscillates between two
    * mineable routes every time a blocked movement turns the player. */
   int q=cell(rr,cc),cost=s->dist[q]+(q==cell(a->r,a->c) && o->dir==d?0:10);
   if(cost<best_cost && cost<=radius*10){best=q;facing=d;best_cost=cost;a->target=p;}
  }
 }
 if(best<0) {
  if(bound>=0){in->bound_target=-1;in->replans++;return seek(a,o,s,blocks,action,radius);}
  return -1;
 }
 if(committed)in->bound_target=a->target;
 if(best==cell(a->r,a->c))return o->dir==facing?action:facing;
 return move_path(a,o,s,best);
}
#define MASK(b) (UINT64_C(1)<<(b))
static int seek_block(FPAgent *a,const FPObservation *o,FPScratch *s,int b,int radius) {
 /* Recovery and optional stock gathering must respect mining capability. */
 if((b==FP_STONE || b==FP_COAL || b==FP_IRON || b==FP_DIAMOND || b==FP_SAPPHIRE || b==FP_RUBY) && !mineable(o,b))return -1;
 a->goal=b;uint64_t mask=MASK(b);
 if(b==FP_TREE)mask|=MASK(FP_FIRE_TREE)|MASK(FP_ICE_SHRUB);
 if(b==FP_STONE)mask|=MASK(FP_STALAGMITE);
 if(b==FP_WATER)mask|=MASK(FP_FOUNTAIN);
 return seek(a,o,s,mask,FP_DO,radius);
}
/* Floor 1 has about four fountains. Choosing afresh every step alternated
 * between two of them as danger costs moved with the player, and the radius
 * cutoff alternated drinking with travel. Keep a chosen fountain while it
 * remains reachable, with a margin over the radius used to choose it. Sewer
 * water lies in single cells beyond lizard-held corridors; danger costs that
 * apply only within six cells flipped the target as the player approached
 * and retreated, until it starved (ep 448: 200 steps between two pools). */
static int hunt(FPAgent *a,const FPObservation *o,FPScratch *s,int mask,int radius) {
 int best=-1,facing=0,costbest=INF;
 for(int i=0;i<s->visible_count;i++) {
  int p=s->visible_cells[i];if(!(s->mobs[p]&mask))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=s->dist[q]+(q==cell(a->r,a->c) && o->dir==d?0:10);
   if(cost<costbest && cost<=radius*10){best=q;facing=d;costbest=cost;a->target=p;}
  }
 }
 if(best<0)return -1;
 if(best==cell(a->r,a->c))return o->dir==facing?FP_DO:facing;
 return move_path(a,o,s,best);
}
static int place(FPAgent *a,const FPObservation *o,FPScratch *s,int action) {
 int r=a->r+dr[o->dir],c=a->c+dc[o->dir];
 if(inside(r,c) && place_valid(a,s,cell(r,c),action))return action;
 /* Turning on open ground moves. Plan the next facing cell as well. */
 for(int d=1;d<=4;d++) {
  r=a->r+dr[d];c=a->c+dc[d];int rr=r+dr[d],cc=c+dc[d];
  if(inside(r,c) && inside(rr,cc) && walkable(block(a,r,c)) && !s->mobs[cell(r,c)] && place_valid(a,s,cell(rr,cc),action))return d;
 }
 return -1;
}
typedef struct {int action,cost[5],furnace,product_kind,product,quantity;} Recipe;
static const Recipe recipes[]={
 {FP_WOOD_PICK,{1,0,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_PICK,1},{FP_WOOD_SWORD,{1,0,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_SWORD,1},
 {FP_STONE_PICK,{1,1,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_PICK,2},{FP_STONE_SWORD,{1,1,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_SWORD,2},
 {FP_IRON_PICK,{1,1,1,1,0},1,FP_GOAL_EQUIPMENT,FP_E_PICK,3},{FP_IRON_SWORD,{1,1,1,1,0},1,FP_GOAL_EQUIPMENT,FP_E_SWORD,3},
 {FP_DIAMOND_PICK,{1,0,0,0,3},0,FP_GOAL_EQUIPMENT,FP_E_PICK,4},{FP_DIAMOND_SWORD,{1,0,0,0,2},0,FP_GOAL_EQUIPMENT,FP_E_SWORD,4},
 {FP_IRON_ARMOUR,{0,0,3,3,0},1,FP_GOAL_EQUIPMENT,-1,1},{FP_DIAMOND_ARMOUR,{0,0,0,0,3},0,FP_GOAL_EQUIPMENT,-1,2},
 {FP_MAKE_ARROW,{1,1,0,0,0},0,FP_GOAL_INVENTORY,FP_I_ARROW,2},{FP_MAKE_TORCH,{1,0,1,0,0},0,FP_GOAL_INVENTORY,FP_I_TORCH,4}
};
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int light_unseen(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost);
static int frontier_walk(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int enchant_trip(const FPAgent *a,const FPObservation *o);
#include "full_tactics.inc"
#include "full_boss_model.inc"

/* Early stream (floors 0-4 before the first Troll Mines arrival). */
static int early(const FPAgent *a) { return !a->floors[5].seen; }
/* Shallow stream (round 15): Overworld, Dungeon and Gnomish Mines until the
 * first Sewers arrival, including their return trips. */
static int shallow(const FPAgent *a) { return !a->floors[3].seen; }
/* Destinations of an icetrip excursion (icetrip_dest). */
enum { ICETRIP_OUT=0, ICETRIP_MINES=5, ICETRIP_HOME=6 };
#ifndef ICETRIP_FORAGE
#define ICETRIP_FORAGE 0
#endif
enum { ICETRIP_WAIT=60 };
#include "full_strategic.inc"
#include "lg_mid.inc"
#include "lg_ice.inc"

/* supply planner (m_supply.inc) uses the resource model (m_resource.inc, included later) */
static int rm_plan_cost(FPAgent *a,const FPObservation *o,FPScratch *s,int k,int *dir);
static int rm_time_to_empty(const FPAgent *a,const FPObservation *o,int k);
static int rm_plan_food(FPAgent *a,const FPObservation *o,FPScratch *s,int *dir);
#include "m_core.inc"
#include "m_nav.inc"
#include "m_supply.inc"
#include "m_combat.inc"
#include "m_build.inc"
#include "m_resource.inc"
#undef TRY
#undef RET

typedef int (*FPRuleFn)(FPAgent*,const FPObservation*,const FPConfig*,FPScratch*,FPRuleCtx*,int*);
/* The selector's ordered rule table (X-macro: one line per step, X(function,MODULE)), in the original
 * priority order of autonomous(). r_* steps may emit an action, p_* steps only update state (ctx, agent
 * memory). The first emitting step wins. Step ids (rule_fires indices) are table positions. */
#define FP_RULE_TABLE(X) \
 X(p_init,CORE) \
 X(p_nav_entry,NAV) \
 X(r_asleep,SUPPLY) \
 X(r_lgi_xp_early,BUILD) \
 X(r_lgm_xp6,BUILD) \
 X(r_fire_xp_int,BUILD) \
 X(p_gem_trip_state,BUILD) \
 X(p_fire_wait_reset,NAV) \
 X(r_realm_descend_now,NAV) \
 X(r_potion_known_heal,COMBAT) \
 X(p_troll_low,COMBAT) \
 X(r_mines_potions,COMBAT) \
 X(p_hold,SUPPLY) \
 X(p_routed_paths,CORE) \
 X(r_fire_low_retreat,COMBAT) \
 X(p_icetrip_legs,SUPPLY) \
 X(r_r3_response,COMBAT) \
 X(r_cb_lowflee,COMBAT) \
 X(r_tactical,COMBAT) \
 X(r_troll_dark_entry,NAV) \
 X(r_ice_dark_entry,NAV) \
 X(r_troll_doorway,COMBAT) \
 X(r_fire_refit,BUILD) \
 X(r_icetrip_lock,SUPPLY) \
 X(r_icetrip_home_forage,SUPPLY) \
 X(p_unseen_attrib,COMBAT) \
 X(r_slimeball_dodge,COMBAT) \
 X(r_unseen_attacker,COMBAT) \
 X(r_troll_face_hold,COMBAT) \
 X(r_boss_wait,COMBAT) \
 X(p_paths,CORE) \
 X(r_graveyard_wave,COMBAT) \
 X(r_fire_iceprep,BUILD) \
 X(r_gem_trip_climb,BUILD) \
 X(r_realm_descend,NAV) \
 X(r_dungeon_retreat,COMBAT) \
 X(r_supply_critical,SUPPLY) \
 X(r_dungeon_sleep_climb,SUPPLY) \
 X(r_realm_escape,COMBAT) \
 X(p_vault_calm,COMBAT) \
 X(r_vault_low_retreat,COMBAT) \
 X(r_troll_low_escape,COMBAT) \
 X(r_lgi_supply,SUPPLY) \
 X(r_ice_enchant_visit,BUILD) \
 X(p_supply_flags,SUPPLY) \
 X(r_drink,SUPPLY) \
 X(r_lgm_supply,SUPPLY) \
 X(r_mines_thirst_climb,SUPPLY) \
 X(r_mines_food_leave,SUPPLY) \
 X(r_shallow_thirst_climb,SUPPLY) \
 X(r_ow_water_seek,SUPPLY) \
 X(r_ow_water_explore,SUPPLY) \
 X(r_vault_water_climb,SUPPLY) \
 X(r_ice_food_return,SUPPLY) \
 X(r_fire_food_return,SUPPLY) \
 X(r_fire_energy_heal,SUPPLY) \
 X(r_fire_drink_leave,SUPPLY) \
 X(r_eat_forage,SUPPLY) \
 X(r_shallow_hunger,SUPPLY) \
 X(r_mines_hunger_climb,SUPPLY) \
 X(r_fire_coal_wood,BUILD) \
 X(r_ice_wood,BUILD) \
 X(p_resupply_start,BUILD) \
 X(r_supply_floor_hunt,SUPPLY) \
 X(r_starving_climb,SUPPLY) \
 X(p_needs_healing,SUPPLY) \
 X(r_vault_heal_climb,COMBAT) \
 X(r_graveyard_recover,SUPPLY) \
 X(r_mines_preheal_supply,SUPPLY) \
 X(r_recover,SUPPLY) \
 X(r_strategic,BUILD) \
 X(r_troll_plates,BUILD) \
 X(r_icetrip_mines_stay,SUPPLY) \
 X(r_icetrip_prospect,BUILD) \
 X(r_ice_enchant_trip,BUILD) \
 X(r_lgm_xp,BUILD) \
 X(r_lgi_xp,BUILD) \
 X(r_xp_spend,BUILD) \
 X(r_troll_ruby_trip,BUILD) \
 X(r_sword_enchant_trip,BUILD) \
 X(r_icetrip_fire_wood,BUILD) \
 X(r_cb_coal,BUILD) \
 X(r_resupply_climb,BUILD) \
 X(p_resupply_end,BUILD) \
 X(r_read_book,BUILD) \
 X(r_ach_fireball,BUILD) \
 X(r_ach_iceball,BUILD) \
 X(r_ach_potion,BUILD) \
 X(r_craft_wood_pick,BUILD) \
 X(r_craft_wood_sword,BUILD) \
 X(r_craft_stone_sword,BUILD) \
 X(r_farm_sapling,BUILD) \
 X(r_farm_plant,BUILD) \
 X(r_craft_stone_pick,BUILD) \
 X(p_floor_prepare,CORE) \
 X(r_craft_iron_sword,BUILD) \
 X(r_craft_iron_pick,BUILD) \
 X(r_craft_diamond_sword,BUILD) \
 X(r_craft_diamond_pick,BUILD) \
 X(r_craft_first_iron_armour,BUILD) \
 X(r_craft_diamond_armour,BUILD) \
 X(r_mines_diamonds,BUILD) \
 X(r_craft_iron_armour,BUILD) \
 X(r_troll_empty_slot,BUILD) \
 X(r_ow_wood,BUILD) \
 X(r_shallow_wood_trip,BUILD) \
 X(r_stone,BUILD) \
 X(r_torches,BUILD) \
 X(r_troll_torches,BUILD) \
 X(r_arrows,BUILD) \
 X(r_ach_shoot,BUILD) \
 X(r_ach_place_stone,BUILD) \
 X(r_ach_place_torch,BUILD) \
 X(r_late_torches,BUILD) \
 X(r_graveyard_light,NAV) \
 X(r_torch_spread_gems,NAV) \
 X(r_ice_return_diamonds,BUILD) \
 X(r_troll_exit_light,NAV) \
 X(r_troll_plates2,BUILD) \
 X(r_troll_known_gems,BUILD) \
 X(r_troll_light_diamonds,BUILD) \
 X(r_troll_light_gems,BUILD) \
 X(r_partial_ruby_trip,BUILD) \
 X(r_chest,BUILD) \
 X(r_enchant,BUILD) \
 X(r_fire_enchant_trip,BUILD) \
 X(p_ready,NAV) \
 X(r_mines_light_rock,BUILD) \
 X(r_mines_dark_probe,BUILD) \
 X(r_fire_gem_trip,BUILD) \
 X(r_descent_provision,SUPPLY) \
 X(r_supply_visit_fill,SUPPLY) \
 X(r_shallow_ow_provision,SUPPLY) \
 X(r_descend_ready,NAV) \
 X(r_starving_climb2,SUPPLY) \
 X(r_mines_exit_light,NAV) \
 X(r_mines_exit_light1,NAV) \
 X(r_mines_exit_probe,NAV) \
 X(r_fire_ladder_camp,COMBAT) \
 X(r_hunt_food,SUPPLY) \
 X(r_ice_light_clear,NAV) \
 X(r_fire_rubies,BUILD) \
 X(r_realm_explore_clear,NAV) \
 X(r_hunt_prey,COMBAT) \
 X(r_ripe_plant,SUPPLY) \
 X(r_ice_light,NAV) \
 X(r_dungeon_frontier,NAV) \
 X(r_troll_explore,NAV) \
 X(r_idle,CORE) \

#define FP_RULE_FN(fn,mod) fn,
#define FP_RULE_NAME(fn,mod) #fn,
#define FP_RULE_MOD(fn,mod) FP_MOD_##mod,
static const FPRuleFn fp_rule_fn[]={FP_RULE_TABLE(FP_RULE_FN)};
const char *const fp_rule_names[]={FP_RULE_TABLE(FP_RULE_NAME)};
const unsigned char fp_rule_module[]={FP_RULE_TABLE(FP_RULE_MOD)};
const int fp_rule_count=sizeof(fp_rule_fn)/sizeof(*fp_rule_fn);
_Static_assert(sizeof(fp_rule_fn)/sizeof(*fp_rule_fn)<=FP_RULES_MAX,"rule_fires holds FP_RULES_MAX rules");
const char *const fp_module_names[FP_MOD_COUNT]={"core","nav","supply","combat","build"};

static int autonomous(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 FPRuleCtx ctx;memset(&ctx,0,sizeof ctx);
 for(int i=0;i<fp_rule_count;i++) {
  int skill=FP_EXPLORE;ctx.fired=0;
  int act=fp_rule_fn[i](a,o,cfg,s,&ctx,&skill);
  if(ctx.fired || act>=0){a->rule_fires[i]++;return emit(a,o,s,act,skill);}
 }
 return emit(a,o,s,FP_NOOP,FP_EXPLORE);
}
const char *fp_skill_name(int skill) {
 static const char *names[FP_SKILL_COUNT]={"explore","gather","craft","drink","eat","recover","fight","farm","light","travel","loot","magic","upgrade"};
 return skill>=0 && skill<FP_SKILL_COUNT?names[skill]:"unknown";
}

#include "full_goals.inc"

void fp_observe(FPAgent *a,const FPObservation *o,float reward,FPScratch *s) {
 /* Policy memory that does not survive a floor change. */
 if(wm_observe(a,o,reward,s)){a->last_frontier=a->recovery_goal=a->water_goal=-1;a->recovering=0;}
 if(a->level==5 && mines_guard(a))wm_attribute_damage(a,o);
 runtime_observe(a,o);
 sg_boundary(a,o);
 rm_observe(a,o); /* resource model memory (m_resource.inc); inert */
 bm_observe(a,o,s);
}
int fp_decide(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 if(a->intent.enabled)return commanded(a,o,cfg,s);
 /* Each callable module initially executes the incumbent rule set. Selectors
  * may replace the child implementation while parent context remains intact. */
 switch(a->runtime.depth?a->runtime.frame[a->runtime.depth-1].kind:0) {
 case FP_RG_EXCURSION:return autonomous(a,o,cfg,s);
 case FP_RG_EXPEDITION:return autonomous(a,o,cfg,s);
 default:return autonomous(a,o,cfg,s);
 }
}
int fp_act(FPAgent *a,const FPObservation *o,float reward,const FPConfig *cfg,FPScratch *s) {
 fp_observe(a,o,reward,s);return fp_decide(a,o,cfg,s);
}
void fp_observe_action(FPAgent *a,const FPObservation *o,float reward,int action,FPScratch *s) {
 fp_observe(a,o,reward,s);
 commit_action(a,o,s,action,FP_EXPLORE);
}
void fp_replay_action(FPAgent *a,const FPObservation *o,float reward,int action,const FPConfig *cfg,FPScratch *s) {
 /* Let each candidate reconstruct its own planning state. Reconcile the
  * action it proposed with the action the historical environment received. */
 fp_observe(a,o,reward,s);FPGoalRuntime observed=a->runtime;FPStrategic strat=a->strat;
 (void)fp_decide(a,o,cfg,s);a->runtime=observed;a->strat=strat;
 a->r=a->observed_r;a->c=a->observed_c;a->t--;
 commit_action(a,o,s,action,a->skill);
}
