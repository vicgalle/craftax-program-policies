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
/* A pigman blow and an elemental fireball both deal 3 physical + 5 fire. */
static float fire_hit(const FPObservation *o) { return wm_attack_damage(o,7); }
/* Health at which a Fire visit ends: room for the next blow and a margin.
 * (Leaving at max(8, hit+4.5) instead did not help: +0.6pp, noise.) */
static float fire_retreat(const FPObservation *o) {
 return fmaxf(6,fire_hit(o)+2.5f);
}
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
/* A remembered fountain may be reachable yet farther than the up ladder
 * plus the cleared Overworld water route. At critical drink, compare those
 * trips instead of committing to a long Dungeon corridor (ep 17). */
static int water_return(FPAgent *a,const FPObservation *o,FPScratch *s,int water_cost) {
 if(a->level!=1 || o->drink>3)return -1;
 int up=a->floors[1].up,landing=a->floors[0].down;
 if(up<0 || landing<0 || s->dist[up]>=INF)return -1;
 const FPFloor *f=&a->floors[0];int distance=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_WATER || f->map[p]==FP_FOUNTAIN)
  distance=imin(distance,abs(p/FP_SIDE-landing/FP_SIDE)+abs(p%FP_SIDE-landing%FP_SIDE));
 if(distance>=INF || s->dist[up]+10*distance+30>=water_cost)return -1;
 return up==cell(a->r,a->c)?FP_ASCEND:move_path(a,o,s,up);
}
static int seek_water(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(a->level!=1 && a->level!=3 && a->level!=4)return seek_block(a,o,s,FP_WATER,100);
 int g=a->water_goal,start=cell(a->r,a->c),b=g>=0?a->floors[a->level].map[g]:UNKNOWN;
 if(b==FP_FOUNTAIN || b==FP_WATER) {
  int best=-1,facing=0,costbest=INF;
  for(int d=1;d<=4;d++) {
   int rr=g/FP_SIDE-dr[d],cc=g%FP_SIDE-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=s->dist[q]+(q==start && o->dir==d?0:10);
   if(cost<costbest){best=q;facing=d;costbest=cost;}
  }
  if(best>=0 && costbest<=2500) {
   int retreat=water_return(a,o,s,costbest);if(retreat>=0)return retreat;
   a->goal=FP_WATER;a->target=g;
   if(best==start)return o->dir==facing?FP_DO:facing;
   return move_path(a,o,s,best);
  }
 }
 int act=seek_block(a,o,s,FP_WATER,100);
 a->water_goal=act>=0?a->target:-1;
 if(act>=0 && a->level==1 && a->water_goal>=0) {
  int g=a->water_goal,cost=INF;
  for(int d=1;d<=4;d++) {
   int rr=g/FP_SIDE-dr[d],cc=g%FP_SIDE-dc[d];if(inside(rr,cc))cost=imin(cost,s->dist[cell(rr,cc)]+10);
  }
  int retreat=water_return(a,o,s,cost);if(retreat>=0)return retreat;
 }
 return act;
}
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
/* Compare reachable food sources by travel, facing, and sword strikes.
 * A nearby animal can refill food before a distant remembered crop. */
static int forage(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int best=-1,facing=0,start=cell(a->r,a->c);float best_cost=1e30f;
 const FPFloor *f=&a->floors[a->level];
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int passive=s->passive[p],plant=f->map[p]==FP_RIPE;
  if(!passive && !plant)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  int strikes=passive?(int)ceilf((passive==1?3:passive==2?4:6)/wm_sword_damage(o)):1;
  int radius=passive?80:50;
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=s->dist[q]+(q==start && o->dir==d?0:10);
   if(cost>radius*10)continue;
   float score=(float)(cost+strikes*10);
   if(score<best_cost){best=q;facing=d;best_cost=score;a->target=p;a->goal=plant?FP_RIPE:-1;}
  }
 }
 if(best<0)return -1;
 if(best==start)return o->dir==facing?FP_DO:facing;
 return move_path(a,o,s,best);
}
/* Ice shrubs are walkable. A turn towards an adjacent shrub moves onto it,
 * so establish the harvest facing by entering its stand from behind. */
static int ice_wood(FPAgent *a,const FPObservation *o,FPScratch *s) {
 const FPFloor *f=&a->floors[7];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(f->map[p]!=FP_ICE_SHRUB || s->mobs[p])continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||s->mobs[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d){a->target=p;a->goal=FP_TREE;return FP_DO;}
   int cost=s->dist[partner]+10;
   if(cost<=500 && cost<costbest){best=partner;dir=d;costbest=cost;a->target=p;}
  }
 }
 if(best<0)return -1;
 a->goal=FP_TREE;return best==start?dir:move_path(a,o,s,best);
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
static int craft_radius(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action,int radius) {
 const Recipe *recipe=NULL;for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++)if(recipes[i].action==action)recipe=&recipes[i];
 if(!recipe)return -1;
 const FPFloor *f=&a->floors[a->level];
 int table=-1,shared=-1,tcost=INF,scost=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(s->dist[p]<=radius && near(a,p,0)) {
  if(s->dist[p]<tcost){table=p;tcost=s->dist[p];}
  if(near(a,p,recipe->furnace) && s->dist[p]<scost){shared=p;scost=s->dist[p];}
 }
 int needed[5];memcpy(needed,recipe->cost,sizeof(needed));
 if(table<0)needed[0]+=2;if(recipe->furnace && shared<0)needed[1]++;
 static const int resource[5]={FP_TREE,FP_STONE,FP_COAL,FP_IRON,FP_DIAMOND};
 for(int i=0;i<5;i++)if(o->inv[i]<needed[i])return seek_block(a,o,s,resource[i],100);
 int start=cell(a->r,a->c);(void)cfg;
 if(shared>=0)return shared==start?action:move_path(a,o,s,shared);
 if(table>=0) {
  if(table!=start)return move_path(a,o,s,table);
  int action=place(a,o,s,FP_PLACE_FURNACE);
  if(action==FP_PLACE_FURNACE)return action;
  if(action>=1 && action<=4 && near(a,cell(a->r+dr[action],a->c+dc[action]),0))return action;
  /* Clear a placement cell without stepping out of the table's reach. */
  for(int i=0;i<4;i++) {
   int d=1+(o->dir-1+i)%4,r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;
   int p=cell(r,c),b=block(a,r,c);
   if(s->mobs[p] || a->floors[a->level].items[p] || !solid(b) || !mineable(o,b) || b==FP_TABLE || b==FP_FURNACE)continue;
   a->target=p;return o->dir==d?FP_DO:d;
  }
  return -1;
 }
 return place(a,o,s,FP_PLACE_TABLE);
}
static int craft(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action) {
 /* Shallow floors below the Overworld: a table counts within 80 steps, not 25.
  * At 25 the iron-armour craft and the descent alternated every step for
  * 23,000 steps (arrive2 ep 53): one step towards the iron put the table at
  * 26, the craft then wanted two wood for a new table, the mines have none,
  * it gave up and the descent stepped back. New tables also cost 2 of the
  * ~80 trees a world has (9 wood per Mines visit went into tables). */
 return craft_radius(a,o,cfg,s,action,!a->floors[3].seen && a->level>=1?800:250);
}
/* A 2x2 room permits turns by moving between its interior cells. Every side
 * can therefore be sealed without an unavailable rotate-in-place action. */
static void wall_position(int i,int w,int h,int *r,int *c,int *d) {
 if(i<h){*r=i;*c=-1;*d=1;}
 else if(i<2*h){*r=i-h;*c=w;*d=2;}
 else if(i<2*h+w){*r=-1;*c=i-2*h;*d=3;}
 else {*r=h;*c=i-2*h-w;*d=4;}
}
static int chamber_walls(FPAgent *a,const FPObservation *o,FPScratch *s,int anchor,int w,int h) {
 int r=anchor/FP_SIDE,c=anchor%FP_SIDE,n=0;
 for(int rr=r;rr<r+h;rr++)for(int cc=c;cc<c+w;cc++) {
  int b=block(a,rr,cc);
  if(!inside(rr,cc)||(!walkable(b) && !(w*h==2 && mineable(o,b)))||s->mobs[cell(rr,cc)])return -1;
 }
 for(int i=0;i<2*(w+h);i++) {
  int wr,wc,d;wall_position(i,w,h,&wr,&wc,&d);
  int rr=r+wr,cc=c+wc;if(!inside(rr,cc))return -1;
  int b=block(a,rr,cc);
  if(b==UNKNOWN)return -1;
  /* Mob projectiles destroy tables and furnaces. A Dungeon mage opened such a
   * wall and an orc killed the sleeping player (3.5x damage). Gnome arrows
   * opened mine shelters the same way (ep 158, 534); kobolds throw daggers. */
  if(a->level>=1 && a->level<=3 && (b==FP_TABLE || b==FP_FURNACE))return -1;
  /* Shallow Overworld: water already stops zombies. Filling it with stone
   * paved over the pond next to every shelter: one long run placed 632
   * stones on the Overworld and died of thirst beside its old water. */
  if(b==FP_WATER && a->level==0 && !a->floors[3].seen)continue;
  if(walkable(b)||b==FP_WATER||b==FP_LAVA) {
   if((d<=2 && w==1)||(d>=3 && h==1))return -1;
   if(!place_valid(a,s,cell(rr,cc),FP_PLACE_STONE))return -1;n++;
  }
 }
 return n;
}
static int room_has(int anchor,int w,int h,int p) {
 int r=p/FP_SIDE-anchor/FP_SIDE,c=p%FP_SIDE-anchor%FP_SIDE;
 return r>=0 && r<h && c>=0 && c<w;
}
/* Ice base: the up ladder when it is close enough to anchor the healing room.
 * 120 of 468 Ice melee hits (60 traces) were taken walking to the ladder when
 * food reached 3, often out of a shelter besieged by trolls that had gathered
 * against its walls (arrive7 ep 10 r0 t17633), and the trip then started at low
 * health (excursion death .15 below 4 health, .02 above 8). A room enclosing the
 * ladder is left by the ladder itself, and a return lands inside it. */
static int ice_base(const FPAgent *a,const FPScratch *s) {
 int up=a->level==7?a->floors[7].up:-1;
 return up>=0 && s->dist[up]<=ICE_BASE_DIST?up:-1;
}
static int recover(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int start=cell(a->r,a->c),best=a->recovery_goal;
 int w=a->recovery_width,h=a->recovery_height,base=ice_base(a,s);
 if(base>=0 && best>=0 && !room_has(best,w,h,base))best=-1;
 if(best<0 || chamber_walls(a,o,s,best,w,h)<0 || s->dist[best]>350) {
  best=-1;int costbest=INF;const FPFloor *f=&a->floors[a->level];
  water_distances(f,s);
  for(int pass=base>=0?0:1;pass<2 && best<0;pass++)
  for(int p=f->known_lo;p<=f->known_hi;p++) {
   if(s->dist[p]>350)continue;
   int water_distance=s->prefix[p/FP_SIDE][p%FP_SIDE];
   for(int mode=0;mode<3;mode++) {
    int ww=mode==1?1:2,hh=mode==2?1:2;
    if(!pass && !room_has(p,ww,hh,base))continue;
    int n=chamber_walls(a,o,s,p,ww,hh);if(n<0)continue;
    /* Healing can require another drink. Short hydration trips reduce
     * exposure to enemies while health is still low. */
    int cost=s->dist[p]+n*50+wm_risk(a,o,s,p,!a->drinking)*40+water_distance*25;
    /* Troll Mines: deaths cluster in recovery, on the walk to a shelter up to
     * 35 steps away at low health; the water bonus also put rooms beside pools
     * whose deep things fired at every exit. Seal close by instead. */
    if(TB(9) && a->level==5 && troll_phase(a))cost=2*s->dist[p]+n*40+wm_risk(a,o,s,p,!a->drinking)*40+(water_distance<3?(3-water_distance)*60:0);
    /* A room around the up ladder keeps a one-step escape from a troll that
     * catches us building it, and the ladder's surroundings are lit. */
    if(TB(22) && a->level==5 && troll_phase(a) && a->floors[5].up>=0) {
     int ur=a->floors[5].up/FP_SIDE,uc=a->floors[5].up%FP_SIDE,pr=p/FP_SIDE,pc=p%FP_SIDE;
     if(ur>=pr && ur<pr+hh && uc>=pc && uc<pc+ww)cost-=400;
    }
    if(cost<costbest){best=p;costbest=cost;w=ww;h=hh;}
   }
  }
 }
 if(best<0)return o->energy<=1?FP_SLEEP:-1;
 a->recovery_goal=best;a->recovery_width=w;a->recovery_height=h;
 int n=chamber_walls(a,o,s,best,w,h);
 if(n>o->inv[1]+(o->inv[0]>4 && (a->level<1 || a->level>3)?(o->inv[0]-4)/2:0)) {
  int action=seek_block(a,o,s,FP_STONE,30);if(action>=0)return action;
 }
 int r=best/FP_SIDE,c=best%FP_SIDE;
 if(a->r<r||a->r>=r+h||a->c<c||a->c>=c+w)return move_path(a,o,s,best);
 for(int rr=r;rr<r+h;rr++)for(int cc=c;cc<c+w;cc++)
  if(!walkable(block(a,rr,cc)))return move_path(a,o,s,cell(rr,cc));
 for(int i=0;i<2*(w+h);i++) {
  int wr,wc,d;wall_position(i,w,h,&wr,&wc,&d);
  int rr=r+wr,cc=c+wc,b=block(a,rr,cc);
  if(!(walkable(b)||b==FP_WATER||b==FP_LAVA))continue;
  if(b==FP_WATER && a->level==0 && !a->floors[3].seen)continue;
  int stand=cell(rr-dr[d],cc-dc[d]),partner=cell(rr-2*dr[d],cc-2*dc[d]);
  int action=o->inv[1]?FP_PLACE_STONE:o->inv[0]>=6 && walkable(b) && (a->level<1 || a->level>3)?FP_PLACE_TABLE:-1;
  if(action>=0) {
   if(start==stand && o->dir==d)return action;
   if(start==partner)return d;
   return move_path(a,o,s,partner);
  }
 }
 if(!n) {
  if(o->energy<7+2*o->dex)return FP_SLEEP;
  /* REST has the same healing rate as NOOP and locks out food/water actions.
   * Waiting under our own recovery controller preserves those interrupts. */
  /* Enchantment preparation can request recovery at full health. Stay
   * protected until its nine-mana cost is available as well. */
  if((o->health<8+o->str-0.01f || o->mana<9) && o->food>0 && o->drink>0)return FP_NOOP;
 }
 return o->energy<=1?FP_SLEEP:-1;
}
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int explore(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 if(a->level==6 && fire_phase(a)){int act=fire_search(a,o,cfg,s);if(act>=0)return act;}
 FPFloor *f=&a->floors[a->level];
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0;s->prefix[r+1][0]=0;for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 int best=-1;float scorebest=-1e30f;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(s->dist[p]<=0||s->dist[p]>600)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  /* Long elemental excursions consume scarce supplies. Prefer closer
   * frontiers while retaining the configured exploration tradeoff. */
  float bonus=cfg->frontier_bonus*(a->level>=6?0.75f:1);
  float score=bonus*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->visits[p],50);
  if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
  score+=(float)(((uint64_t)p*UINT64_C(0x9e3779b97f4a7c15)^a->rng)>>56)*0.002f;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best>=0){a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);}
 (void)word(a);return FP_NOOP;
}
static int light_unseen(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost);
static int frontier_walk(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
/* Troll Mines: explore() sees frontiers within 60 steps only; in the dark it
 * returned NOOP for 85,000 steps (ep 268: unlit cells all round, 45 torches
 * unused). Light the nearest unseen cells, or walk to any frontier. */
static int troll_explore(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 /* The mines are lit only by torches, so walking to a frontier reveals
  * nothing until the walking torch rule happens to face a placeable cell.
  * Light oracle: +13pp P(8), through time (exit, gems, water, bats found
  * sooner). Walk to the placement that lights the most unseen cells. */
 /* Exit search on the cleared floor: the long-phase runs (4% of entrants,
  * P6 .17) start it with 27 torches and spend them all without finding the
  * ladder, most on small walking-rule gains. Spend each torch where it lights
  * the most unseen cells; the exit comes before Ice's torch stock. */
 if(TB(21) && a->level==5 && troll_phase(a) && o->clear && a->floors[5].down<0 && o->inv[FP_I_TORCH]>0) {
  int alt=light_unseen(a,o,s,30,800);if(alt>=0)return alt;
 }
 if(TB(17) && a->level==5 && troll_phase(a) && o->inv[FP_I_TORCH]>12) {
  int alt=light_unseen(a,o,s,25,300);if(alt>=0)return alt;
 }
 int act=explore(a,o,cfg,s);
 if(!TB(3) || act!=FP_NOOP || a->level!=5 || !troll_phase(a))return act;
 int alt=light_unseen(a,o,s,4,2500);if(alt>=0)return alt;
 alt=frontier_walk(a,o,cfg,s);return alt>=0?alt:act;
}
static int travel(FPAgent *a,const FPObservation *o,FPScratch *s,int down) {
 int p=down?a->floors[a->level].down:a->floors[a->level].up;
 if(p<0 || s->dist[p]>=INF)return -1;
 if(p==cell(a->r,a->c))return down?FP_DESCEND:FP_ASCEND;
 return move_path(a,o,s,p);
}
/* Frontier walker shared with the fire stream (same function there): stands
 * whose 9x11 view still holds four unseen cells, at any distance. */
static int frontier_walk(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 FPFloor *f=&a->floors[a->level];(void)o;
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 int best=-1;float scorebest=-1e30f;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(s->dist[p]<=0||s->dist[p]>=INF)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  if(n<4)continue;
  float score=cfg->frontier_bonus*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->visits[p],50);
  if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best<0)return -1;
 a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);
}
/* Exit search on Fire. The general explorer scores every cell within 60
 * steps, so once the ladder's surroundings were known, cells with nothing
 * unseen nearby outscored real frontiers beyond lava or rock: ep 0 wandered
 * there for ~90 visits before finding an exit 66 cells away. Consider only
 * stands that reveal unseen in-map terrain, at any distance, and first those
 * the drink left can reach and return from; with none, refill first. */
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 FPFloor *f=&a->floors[6];int T,B,L,R;fire_bounds(f,&T,&B,&L,&R);
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0,in=r>=T && r<=B;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN && in && c>=L && c<=R;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 /* A refill trip only helps when the mines stop would top drink up (see the
  * descent provisioning): above that, push to the best stand instead of
  * bouncing between the two ladders (ep 172). */
 int up=f->up,budget=(o->drink-1)*20-10,full=o->drink>7+2*o->dex-3;
 for(int pass=0;pass<2;pass++) {
  int best=-1;float scorebest=-1e30f;
  for(int p=f->known_lo;p<=f->known_hi;p++) {
   if(s->dist[p]<=0||s->dist[p]>=INF)continue;
   int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
   int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
   if(n<4)continue;
   /* Manhattan distance understates the walk back around lava. */
   if(!pass && s->dist[p]/10+(up>=0?(abs(r-up/FP_SIDE)+abs(c-up%FP_SIDE))*5/4:0)>budget)continue;
   float score=cfg->frontier_bonus*0.75f*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->visits[p],50);
   if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
   score+=(float)(((uint64_t)p*UINT64_C(0x9e3779b97f4a7c15)^a->rng)>>56)*0.002f;
   if(score>scorebest){best=p;scorebest=score;}
  }
  if(best>=0){a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);}
  if(!pass && !full){int act=travel(a,o,s,0);if(act>=0)return act;}
 }
 return -1;
}
/* Reach a torch placement with the correct facing by approaching its stand
 * from behind. Turning towards an empty target would move onto that target. */
static int graveyard_light(FPAgent *a,const FPObservation *o,FPScratch *s) {
 const FPFloor *f=&a->floors[8];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 if(!o->inv[FP_I_TORCH])return -1;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(!place_valid(a,s,p,FP_PLACE_TORCH))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=0;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
  if(gain<GRAVEYARD_TORCH_GAIN)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||s->mobs[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d)return FP_PLACE_TORCH;
   int cost=s->dist[partner]+10;
   if(cost<costbest && cost<=150){best=partner;dir=d;costbest=cost;}
  }
 }
 if(best<0)return -1;
 return best==start?dir:move_path(a,o,s,best);
}
/* Floor 2 is dark: only torch-lit cells are ever observed, and walking past an
 * unlit pocket reveals nothing. The free torch rule waits for 30 unseen cells,
 * so pockets beside water and rock stay dark; a cleared floor whose down ladder
 * lay in one kept agents shuttling for water until the horizon (ep 19: memory
 * covered the floor except a 25-cell pocket holding the ladder). Walk to the
 * reachable placement that lights the most unseen cells, approaching its stand
 * from behind so that the facing is right on arrival. */
static int light_unseen(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost) {
 const FPFloor *f=&a->floors[a->level];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 if(!o->inv[FP_I_TORCH])return -1;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(!place_valid(a,s,p,FP_PLACE_TORCH))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=0;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||s->mobs[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d)return FP_PLACE_TORCH;
   if(s->dist[partner]>max_cost)continue;
   /* One unseen cell is worth a step: large dark regions before small pockets. */
   int cost=s->dist[partner]+10-10*gain;
   if(cost<costbest){best=partner;dir=d;costbest=cost;}
  }
 }
 if(best<0)return -1;
 return best==start?dir:move_path(a,o,s,best);
}
static int enchant_trip(const FPAgent *a,const FPObservation *o);
/* Mines ore lies inside rock, often 6-12 cells from the open cave (arrive2
 * ep 100: three diamonds beyond every torch placed on open ground; 89% of
 * iron-sword Sewers entrants had never seen a Mines diamond). A torch lights
 * 4.7 cells through rock. Mine a lit rock cell whose circle is mostly unseen,
 * from an adjacent stand; light_unseen then places the torch in the gap. Routes
 * tunnel only through observed rock, so dead reckoning stays on known ground
 * (round-14 E9, never screened). */
static int dark_probe(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost) {
 const FPFloor *f=&a->floors[a->level];int start=cell(a->r,a->c),bs=-1,bt=-1,bd=0,costbest=INF;
 if(!o->inv[FP_I_TORCH] || o->inv[FP_I_PICK]<1)return -1;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int b=f->map[p];
  if(!(b==FP_STONE || b==FP_COAL) || f->items[p] || s->mobs[p])continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=0;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d];if(!inside(sr,sc))continue;
   /* The stand may itself be rock: routes tunnel through observed rock. */
   int q=cell(sr,sc);if(s->mobs[q] || s->dist[q]>max_cost)continue;
   int cost=s->dist[q]+20-10*gain;
   if(cost<costbest){costbest=cost;bs=q;bt=p;bd=d;}
  }
 }
 if(bs<0)return -1;
 a->target=bt;
 if(bs!=start)return move_path(a,o,s,bs);
 if(o->dir!=bd)return bd;
 return FP_DO;
}
#include "full_tactics.inc"
static int tactical(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int start=cell(a->r,a->c);
 /* Avoid a lethal simultaneous attack. Cooldowns and health are inferred
  * from consecutive public views and our own attacks, with conservative
  * defaults when an enemy first becomes visible. */
 float incoming=wm_melee_incoming(a,o);
 /* Protected Ice fights can finish during the attacker's cooldown even
  * with little health left. Keep a half-point margin for that first hit;
  * retreat before a lethal hit, rather than abandoning every low-HP cast. */
 if(incoming>=o->health-(o->level==7?0.5f:1) || (o->level!=7 && o->health<=4 && incoming>0)) {
  int best=-1;float costbest=incoming;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c)||!walkable(block(a,r,c))||s->mobs[cell(r,c)]||s->arrow_next[cell(r,c)])continue;
   float cost=wm_melee_threat_at(a,o,r,c);
   if(cost<costbest){best=d;costbest=cost;}
  }
  /* A realm chase only ends in a corner: pigmen and frost trolls keep pace
   * (ep 16 danced 23 steps at 5.2 health with full mana). The up ladder is
   * the one exit they cannot follow. Climb when on it, and prefer the first
   * step of the route to it among equally safe retreats. */
  if(a->level==4 || a->level==5 || a->level==6 || a->level==7 || (a->level==1 && !a->floors[5].seen)) {
   int up=a->floors[a->level].up,climb=FP_ASCEND;
   /* Icetrip on Fire: the Ice ladder is home and often the nearer exit. */
   if(ICETRIP_ESCAPE_NEAR && icetrip(a) && a->level==6) {
    int D=a->floors[6].down;
    if(D>=0 && s->dist[D]<INF && (up<0 || s->dist[up]>=INF || s->dist[D]<=s->dist[up])){up=D;climb=FP_DESCEND;}
   }
   if(up==start)return climb;
   if(best>=0 && up>=0 && s->dist[up]<INF && s->first[up]>0) {
    int d=s->first[up],r=a->r+dr[d],c=a->c+dc[d];
    if(inside(r,c) && walkable(block(a,r,c)) && !s->mobs[cell(r,c)] && !s->arrow_next[cell(r,c)]) {
     float cost=wm_melee_threat_at(a,o,r,c);
     if(cost<=costbest)return d;
    }
   }
  }
  if(best>=0)return best;
 }
 /* Icetrip on Fire: evade rather than trade. In ice armour a pigman blow
  * costs 6.2-6.8 of 12-13 health, and 115 of 194 melee hits on Fire in 60
  * traced worlds landed while turning towards a pigman that had just stepped
  * beside us (64 more while casting at it: spells resolve after its blow). A
  * melee mob only strikes if it starts its turn beside us, and we act first:
  * any step to a cell not beside a ready mob avoids the blow, and pigmen
  * (speed .75) fall behind a player who keeps moving. Step to such a cell,
  * preferring the route to the ladder the trip is heading for; take that
  * ladder if we stand on it (mobs never follow). */
 if(ICETRIP_EVADE && icetrip(a) && a->level==6 && incoming>0) {
  int up_bound=enchant_trip(a,o) || o->food<=2 || o->health<=4,T=up_bound?a->floors[6].up:a->floors[6].down;
  if(ICETRIP_EVADE==1 && T==start)return up_bound?FP_ASCEND:FP_DESCEND;
  /* Variant 2: keep travelling, never dither. Free evasion in any direction
   * (variant 1) circled for dozens of steps until two pigmen cornered it
   * (-9.3pp). Here only the route's next step is taken, when it is safe;
   * otherwise fight as before. Pigmen (speed .75) fall behind. */
  if(ICETRIP_EVADE==2) {
   /* Only far from the destination ladder: a pursuer left beside the ladder
    * is frozen there and strikes on the next arrival (arrival hits tripled
    * when evasion ran to the ladder). Mid-crossing it falls behind and
    * despawns 14 cells back. Near the ladder, fight it as before. */
   if(T<0 || s->dist[T]>=INF || s->dist[T]<200 || s->first[T]<=0)goto no_evade;
   int d=s->first[T],r=a->r+dr[d],c=a->c+dc[d];
   if(!inside(r,c)||!walkable(block(a,r,c))||s->mobs[cell(r,c)]||s->arrow_next[cell(r,c)])goto no_evade;
   for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)goto no_evade;}
   return d;
  }
  int best=-1,bc=INT_MAX;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];
   if(!inside(r,c)||!walkable(block(a,r,c))||s->mobs[cell(r,c)]||s->arrow_next[cell(r,c)])continue;
   int adj=0;
   for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)adj=1;}
   if(adj)continue;
   int cost=10*wm_risk(a,o,s,cell(r,c),!a->drinking)+(T>=0 && s->dist[T]<INF && s->first[T]==d?0:5);
   if(cost<bc){bc=cost;best=d;}
  }
  if(best>=0)return best;
 }
 no_evade:
 if(s->arrow_next[start]) {
  int best=-1,costbest=INT_MAX;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int p=cell(r,c);
   if(!walkable(block(a,r,c))||s->mobs[p]||s->arrow_next[p])continue;
   int cost=wm_risk(a,o,s,p,!a->drinking);if(cost<costbest){best=d;costbest=cost;}
  }if(best>=0)return best;
 }
 for(int i=0;i<4;i++) {
  int d=1+(o->dir-1+i)%4,r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int p=cell(r,c);
  if(!(s->mobs[p]&5))continue;
  if(o->dir!=d)return d;
  int type=s->melee[p]?s->melee[p]:s->ranged[p];
  /* Sword damage resolves before a melee attack; spells resolve after it.
   * Finish a weakened elemental immediately and preserve the remaining mana. */
  if(type>=7 && wm_hit_damage(o,type,s->ranged[p]!=0)>=s->enemy_health[p])return FP_DO;
  if(o->mana>=2) {
   if(type==7 && o->spells[1])return FP_ICEBALL;
   if(type==8 && o->spells[0])return FP_FIREBALL;
  }
  return FP_DO;
 }
 /* Recovery navigation needs uninterrupted turns to reach and seal its
  * shelter. Adjacent attacks and projectile dodges above still interrupt. */
 if(a->level==8 && (a->recovering || o->health<=GRAVEYARD_RECOVER_HEALTH))return -1;
 /* An approaching orc survives the first iron-sword blow. An arrow or
  * spell in the faced cell before it arrives lets that blow kill it before
  * its first attack, including a diagonal approach into that cell. */
 if(a->level==1 && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  int shot=o->bow && o->inv[FP_I_ARROW]?FP_SHOOT:o->spells[0]?FP_FIREBALL:FP_ICEBALL;
  float damage=shot==FP_SHOOT?5*(1+0.2f*(o->dex-1)):3*(1+0.5f*(o->intelligence-1));
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
    int df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(!e.kind && dd==2 && df==1 && wm_hit_damage(o,e.type,0)<e.health && wm_hit_damage(o,e.type,0)+damage>=e.health)return shot;
   }
 }
 /* A troll two cells away steps next into one of our neighbours and
  * attacks after our first blow, which cannot kill 20 HP. When it can step
  * into the faced cell, a spell cast now meets it there; if the spell and
  * one sword blow suffice, the blow then kills it before its first attack. */
 if((a->level==4 || a->level==5) && o->mana>=2 && (o->spells[0] || o->spells[1])) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];float spell=3*(1+0.5f*(o->intelligence-1));
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];float blow=wm_hit_damage(o,e.type,0),pending=0;
    int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c),df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(e.kind || blow>=e.health)continue;
    /* A shot waiting in the faced cell hits the troll as it steps in. */
    for(int j=0;j<a->shot_count;j++)if(a->shot_pos[j]==cell(fr,fc) && a->shot_dir[j]==o->dir)
     pending+=wm_projectile_damage(o,a->shot_type[j],e.type,0);
    /* Without an enchanted sword two spells are needed: start one move
     * earlier, so the first shot waits in the faced cell. */
    if(d==3 && df==2 && o->mana>=4 && blow+spell<e.health && blow+2*spell>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
    if(d==2 && df==1 && blow+spell+pending>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
 }
 /* During an Ice excursion start iceballs into a pigman's greedy approach,
  * including a diagonal approach. The first missile waits in the faced cell;
  * it can hit on the approach turn before the pigman starts beside us. */
 if(icetrip(a) && a->level==6 && o->spells[1] && o->mana>=2 && a->shot_count<3) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int vr=e.p/FP_SIDE-a->r,vc=e.p%FP_SIDE-a->c,dd=abs(vr)+abs(vc);
    if(e.kind || e.type!=7 || dd<2 || dd>3)continue;
    if(dd==2 && abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc)==1)return FP_ICEBALL;
    if(dd==3 && o->mana>=4)for(int axis=0;axis<2;axis++) {
     int nr,nc;if(!wm_greedy_next(a,vr,vc,axis,&nr,&nc))continue;
     if(abs(nr-fr)+abs(nc-fc)==1 && walkable(block(a,nr,nc)))return FP_ICEBALL;
    }
   }
 }
 /* Frost trolls resist the sword and require three fireballs. Cast into
  * their approach before adjacency, including a diagonal final step. The
  * missile persists in the faced cell until after the next mob movement. */
 /* With a fire-enchanted sword (6.2 to a frost troll, resolving before its
  * attack) two fireballs that meet the troll as it steps into the faced cell
  * leave it within one strike: no hit at all. The first must be cast a step
  * earlier, at distance three, when the troll's greedy step (larger offset
  * first) leads next to the faced cell; it waits there in flight. */
 if(a->level==7 && o->sword_enchant==1 && o->spells[0] && o->mana>=4 && a->shot_count<=1) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];
    int vr=e.p/FP_SIDE-a->r,vc=e.p%FP_SIDE-a->c;
    if(e.kind || e.type!=8 || abs(vr)+abs(vc)!=3 || e.health>2*wm_projectile_damage(o,3,8,0)+wm_hit_damage(o,8,0))continue;
    for(int axis=0;axis<2;axis++) {
     int nr,nc;if(!wm_greedy_next(a,vr,vc,axis,&nr,&nc))continue;
     if(abs(nr-fr)+abs(nc-fc)==1 && walkable(block(a,nr,nc)))return FP_FIREBALL;
    }
   }
 }
 if(a->level==7 && o->spells[0] && o->mana>=2) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];
    int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
    int df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(!e.kind && e.type==8 && dd==2 && df==1)return FP_FIREBALL;
   }
 }
 /* Shoot along a clear axis. Turning towards a distant target also approaches it. */
 int d=o->dir;
 for(int k=1;k<=5;k++) {
  int r=a->r+k*dr[d],c=a->c+k*dc[d];if(!inside(r,c))break;int p=cell(r,c);
  if(s->mobs[p]&5) {
   int type=s->melee[p]?s->melee[p]:s->ranged[p];
   if(o->mana>=2 && type==7 && o->spells[1])return FP_ICEBALL;
   if(o->mana>=2 && type==8 && o->spells[0])return FP_FIREBALL;
   if(o->mana>=2 && k>=2 && (o->spells[0] || o->spells[1])) {
    float spell=3*(1+0.5f*(o->intelligence-1));
    float arrow=4*(1+0.2f*(o->dex-1))*(type==5?0.5f:type>=7?0.1f:1);
    if(!o->bow || !o->inv[9] || spell>=arrow)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
   /* Dungeon orcs hit for 3 once adjacent and need two sword strikes. An
    * arrow while one approaches in line lets the first strike finish it. */
   if(o->bow && o->inv[9]>0 && k>=2 && (s->ranged[p] || o->health<6 || a->level>=4 || a->level==1))return FP_SHOOT;
   break;
  }
  if(solid(block(a,r,c)) || block(a,r,c)==UNKNOWN)break;
 }
 /* Icetrip on Fire: turn to shoot a pigman approaching along our row or
  * column. A mob whose smaller offset is zero moves only along that axis,
  * so iceballs cast down the line meet it; three land before it is beside
  * us from four or more cells, and each one shortens the fight that costs a
  * 6+ point blow in ice armour. The shot above only fires along the facing.
  * Turning is free when the neighbour that way is blocked; otherwise it is a
  * step towards it, taken only from four or more cells. */
 if(ICETRIP_PFACE && icetrip(a) && a->level==6 && o->spells[1] && o->mana>=2 && a->shot_count<3) {
  int bestd=0,bk=99;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];if(e.kind || e.type!=7)continue;
   int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,k=abs(er)+abs(ec);
   if((er && ec) || k<2 || k>5 || !wm_firing_line(a,a->r,a->c,e.p/FP_SIDE,e.p%FP_SIDE))continue;
   int want=er>0?FP_DOWN:er<0?FP_UP:ec>0?FP_RIGHT:FP_LEFT;
   if(want==o->dir)continue;
   int nr=a->r+dr[want],nc=a->c+dc[want];
   if(!known_direction(a,want))continue;
   if(inside(nr,nc) && walkable(block(a,nr,nc)) && !s->mobs[cell(nr,nc)] && k<4)continue;
   if(k<bk){bk=k;bestd=want;}
  }
  if(bestd)return bestd;
 }
 /* Face the orc's unobstructed greedy approach while it is still far
  * enough to turn safely. Let it enter the pre-shot cell instead of
  * walking into its reach with the wrong facing. */
 if(a->level==1 && a->dungeon_wait<24 && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  float shot=o->bow && o->inv[FP_I_ARROW]?5*(1+0.2f*(o->dex-1)):3*(1+0.5f*(o->intelligence-1));
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);float blow=wm_hit_damage(o,e.type,0);
   if(!e.kind && dd>=3 && dd<bd && blow<e.health && blow+shot>=e.health){best=i;bd=dd;}
  }
  if(best<0)a->dungeon_wait=0;
  if(best>=0) {
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,clear=wm_greedy_approach(a,s,e,0,NULL,NULL);
   if(clear) {
    if(er*dr[o->dir]+ec*dc[o->dir]>0){a->dungeon_wait++;return FP_NOOP;}
    int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
    int nr=a->r+dr[want],nc=a->c+dc[want];
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || bd>=4))return want;
   }
  }
 }
 /* Pre-casting needs the troll to arrive next to the faced cell. Walking
  * on, or turning away, lets it arrive behind or beside us instead. Face
  * the side it approaches from while it is still three or more cells away,
  * then let it come. Its moves shrink its larger offset first, so a troll
  * in front of us stays in front until it is adjacent. */
 if((a->level==4 || a->level==5) && o->mana>=2 && (o->spells[0] || o->spells[1]) && (a->level==4?a->vault_wait:a->troll_wait)<24 &&
    !(TB(8) && a->level==5 && a->troll_door)) {
  int *wait=a->level==4?&a->vault_wait:&a->troll_wait;
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
   if(!e.kind && dd>=3 && dd<bd && wm_hit_damage(o,e.type,0)<e.health){best=i;bd=dd;}
  }
  if(best<0)*wait=0;
  if(best>=0) {
   /* Only a troll whose greedy approach is unobstructed will arrive. */
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,clear=wm_greedy_approach(a,s,e,0,NULL,NULL);
   if(clear) {
    /* Bounded: a troll that hovers out of reach must not stall supplies. */
    if(er*dr[o->dir]+ec*dc[o->dir]>0){(*wait)++;return FP_NOOP;}
    int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
    int nr=a->r+dr[want],nc=a->c+dc[want];
    /* A known blocked neighbour turns us in place; otherwise turning is
     * a step towards it, safe only from four or more cells away. Unknown
     * terrain cannot establish either outcome. */
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || bd>=4))return want;
   }
  }
 }
 /* Most melee damage before the Troll Mines is taken while turning towards
  * a mob that has just arrived beside us (Dungeon 56%, Sewers 76%, Vault 56%
  * of melee damage in 300-400 sampled worlds): the turn costs the step in
  * which it strikes. A diamond sword kills lizards and knights in one blow,
  * so facing the cell it arrives in removes the hit. Predict that cell from
  * its greedy approach, face it while turning is safe, and wait for it.
  * Only for mobs one blow kills: an iron-sword orc fight costs its hit either
  * way, and waiting under mage fire cost 1.1 points of Dungeon passes. */
 if(!a->floors[5].seen && a->level>=1 && a->level<=4 && a->early_wait<24) {
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
   if(!e.kind && dd>=2 && dd<bd && wm_hit_damage(o,e.type,0)>=e.health){best=i;bd=dd;}
  }
  int exposed=0;
  for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind && wm_firing_line(a,a->r,a->c,a->enemies[i].p/FP_SIDE,a->enemies[i].p%FP_SIDE))exposed=1;
  if(best<0)a->early_wait=0;
  if(best>=0 && !exposed) {
   FPEnemy e=a->enemies[best];int r,c,clear=wm_greedy_approach(a,s,e,1,&r,&c);
   if(clear && abs(r)+abs(c)==1) {
    int want=r>0?FP_DOWN:r<0?FP_UP:c>0?FP_RIGHT:FP_LEFT;
    if(o->dir==want){a->early_wait++;return FP_NOOP;}
    int nr=a->r+dr[want],nc=a->c+dc[want];
    /* A blocked neighbour turns us in place; an open one is a step, safe
     * only while the mob is four or more cells away. */
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || s->mobs[cell(nr,nc)] || bd>=4)){a->early_wait++;return want;}
   }
  }
 }
 return -1;
}

/* The Sewers ice-armour trip is pending: sapphires in hand, armour not yet
 * fully ice-enchanted. Every Ice step before it is taken with fire armour
 * (frost troll hits 5.8-7.4), so nothing on Ice may delay it, and nothing
 * on its outbound route may turn it back. */
static int enchant_trip(const FPAgent *a,const FPObservation *o) {
 int ice=0;for(int i=0;i<4;i++)ice+=o->armour_enchant[i]==2;
 return a->floors[7].seen && wm_armour(o) && o->inv[FP_I_SAPPHIRE] && ice<4;
}
/* Every Vault has exactly one fire table. Searches that gave up at 600
 * steps sent 199 of 5,725 Fire entrants down with unspent rubies. */
static int vault_limit(const FPAgent *a,const FPObservation *o) {
 return (troll_phase(a) || fire_phase(a)) && o->inv[FP_I_RUBY]>=2?1800:600;
}
/* A Vault trip is worth starting while the table is known or the search
 * budget lasts. In the troll phase a known but unreachable table (seen
 * across unexplored darkness) also spends the budget, or the trip
 * climbs, fails to reach it, descends and climbs again (ep 541, 186). */
static int vault_trip_ok(const FPAgent *a,const FPObservation *o) {
 if(troll_phase(a))return a->vault_search<vault_limit(a,o);
 /* Fire phase: a table found unusable (VAULT_UNUSABLE) ends the trips. */
 return (fire_table_known(a) && a->vault_search<VAULT_UNUSABLE) || a->vault_search<vault_limit(a,o);
}
/* Vault enchant trips from the mines took 263 steps (2% deaths each), much of
 * it in a Vault shelter waiting for the nine mana an enchant costs. Mana
 * regenerates while we work here (1 per ~6 steps at int 3): leave with it. */
static int troll_mana_ok(const FPAgent *a,const FPObservation *o) {
 return !TB(23) || !troll_phase(a) || a->level!=5 || o->mana>=9;
}
/* Cheapest stand next to remembered water on this floor (path cost, INF if none). */
static int water_cost(const FPAgent *a,FPScratch *s) {
 const FPFloor *f=&a->floors[a->level];int best=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_WATER || f->map[p]==FP_FOUNTAIN)
  for(int d=1;d<=4;d++){int r=p/FP_SIDE-dr[d],c=p%FP_SIDE-dc[d];if(inside(r,c) && s->dist[cell(r,c)]<best)best=s->dist[cell(r,c)];}
 return best;
}
/* Early stream (floors 0-4 before the first Troll Mines arrival). */
static int early(const FPAgent *a) { return !a->floors[5].seen; }
/* Shallow stream (round 15): Overworld, Dungeon and Gnomish Mines until the
 * first Sewers arrival, including their return trips. */
static int shallow(const FPAgent *a) { return !a->floors[3].seen; }
static int sealed_in(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!a->recovering || a->recovery_goal<0)return 0;
 int rr=a->recovery_goal/FP_SIDE,cc=a->recovery_goal%FP_SIDE;
 return a->r>=rr && a->r<rr+a->recovery_height && a->c>=cc && a->c<cc+a->recovery_width &&
        chamber_walls(a,o,s,a->recovery_goal,a->recovery_width,a->recovery_height)==0;
}
/* Destinations of an icetrip excursion (icetrip_dest). */
enum { ICETRIP_OUT=0, ICETRIP_MINES=5, ICETRIP_HOME=6 };
/* Homebound across Fire after a mines stop. The base rules sent a hungry
 * agent back up from Fire whenever no bat was in view, and the mines rules
 * sent it down again: 5<->6 ladder bounces every step down to food 0. Going
 * home, eat only bats close to the route, keep healthy coal stops short, and
 * never turn back. */
static int icetrip_home(FPAgent *a,const FPObservation *o,FPScratch *s,int *skill) {
 const FPFloor *f=&a->floors[6];int here=cell(a->r,a->c),D=f->down;
 if(a->icetrip_dest!=ICETRIP_HOME || enchant_trip(a,o) || D<0 || s->dist[D]>=INF)return -1;
 int melee=99;float hit=fire_hit(o);
 for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);if(!e.kind && d<melee)melee=d;}
 if(o->food<=7+2*o->dex-3 && o->health>hit+1.5f){int act=hunt(a,o,s,2,8);if(act>=0){*skill=FP_EAT;return act;}}
 if(o->health>=hit+3 && melee>6 && o->inv[FP_I_COAL]<8){int act=seek_block(a,o,s,FP_COAL,6);if(act>=0){*skill=FP_GATHER;return act;}}
 *skill=FP_TRAVEL;return D==here?FP_DESCEND:move_path(a,o,s,D);
}
#ifndef ICETRIP_FORAGE
#define ICETRIP_FORAGE 0
#endif
enum { ICETRIP_WAIT=60 };
/* Ice has no food, so every Ice stay ends in a food excursion (86% leave at
 * food <= 3). The base bounced Ice<->Fire one step at a time until food 2,
 * then crossed Fire in ice armour to the dark Troll Mines (2.1 trips per Ice
 * entrant, 6% death each, ~300 Fire steps). Fire is lit and spawns bats near
 * the player (0.1 per step, 3-14 cells away; first bat eaten after a median
 * 12 Fire steps in traces), so feed beside the Ice ladder: hunt bats close
 * by, mine torch coal next to it while waiting, and go home fed.
 * A pigman left beside the ladder is frozen there and strikes on the next
 * arrival (6.2-6.8 of 12-13 health), including the Ice side's low-health
 * escapes up the ladder: an earlier version that left at the first threat
 * died on arrival in 7 of 20 traced worlds. So fight a pigman at the ladder
 * while one blow is affordable (it costs one blow; three iceballs kill it
 * within its cooldown) and leave it there only when it is not. */
static int icetrip_forage(FPAgent *a,const FPObservation *o,FPScratch *s,int *skill) {
 const FPFloor *f=&a->floors[6];int here=cell(a->r,a->c),D=f->down;
 if(!ICETRIP_FORAGE || a->icetrip_dest!=ICETRIP_OUT || enchant_trip(a,o) || D<0 || s->dist[D]>=INF)return -1;
 int maxfood=7+2*o->dex,mD=99,mU=99,line=0,dr0=D/FP_SIDE,dc0=D%FP_SIDE;float hit=fire_hit(o);
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];int er=e.p/FP_SIDE,ec=e.p%FP_SIDE;
  if(!e.kind){mD=imin(mD,abs(er-dr0)+abs(ec-dc0));mU=imin(mU,abs(er-a->r)+abs(ec-a->c));}
  else if(wm_firing_line(a,a->r,a->c,er,ec))line=1;
 }
 int low=o->health<=hit+1.5f,calm=mU>6 && !line && !low,home=D==here?FP_DESCEND:move_path(a,o,s,D);
 /* Contract with Ice: the base never descends to Ice at 6 health or less
  * (it heals in a Fire shelter or in the mines first), and the Ice rules
  * assume it. Ice's low-health escapes up the ladder leave a frost troll
  * frozen beside it: going straight back down killed (v2: Ice melee deaths
  * 533 -> 1,071, 420 of them entering Ice at <= 6 health). */
 if(o->health<=6.5f)return -1;
 *skill=FP_TRAVEL;
 /* Fed or out of drink: home, unless a pigman near the ladder can still be
  * fought there (waiting on the ladder lets it come; tactical fights it). */
 if(o->food>=maxfood-2 || o->drink<=2 || o->energy<=1) {
  if(calm && o->inv[FP_I_COAL]<8){int act=seek_block(a,o,s,FP_COAL,4);if(act>=0){*skill=FP_GATHER;return act;}}
  if(low || mD>3 || here!=D || ++a->icetrip_guard>20)return home;
  *skill=FP_FIGHT;return FP_NOOP;
 }
 if(!low || mU>5){int act=hunt(a,o,s,2,8);if(act>=0){*skill=FP_EAT;return act;}}
 if(calm && o->inv[FP_I_COAL]<8){int act=seek_block(a,o,s,FP_COAL,6);if(act>=0){*skill=FP_GATHER;return act;}}
 if(calm && o->inv[0]<6){int act=seek_block(a,o,s,FP_TREE,6);if(act>=0){*skill=FP_GATHER;return act;}}
 if(here!=D)return home;
 /* Low: a blow would kill. Leave while the pigman is still two steps off. */
 if(low && mU>=2 && mU<=5)return FP_DESCEND;
 if(++a->icetrip_wait>=ICETRIP_WAIT) {
  /* A ladder walled in by lava or rock sees few bats: cross to the mines. */
  if(o->food<=1){a->icetrip_dest=ICETRIP_MINES;return -1;}
  return FP_DESCEND;
 }
 *skill=FP_EAT;return FP_NOOP;
}
/* The Fire ladder airlock: a sealed 2x2 room around the Ice ladder. Half of
 * all excursion deaths follow a Fire arrival at 6.5 health or less (Ice's
 * escapes up the ladder and hungry departures): 7% of those trips die at
 * 4-6.5 health and 15-23% at 4 or less, because the base rules then cross
 * Fire to the mines (health <= 4, or a pigman near) or build a shelter
 * wherever stone is cheapest. Healing inside sealed stone took no damage in
 * traces (9,000 sleeping steps), and with the ladder inside the room every
 * later arrival from Ice lands behind walls: no frozen pigman beside the
 * ladder, no fireball line. Returns the anchor (top-left) of the cheapest
 * valid room holding the ladder, or -1. */
static int icetrip_lock(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int D=a->floors[6].down;if(D<0)return -1;
 int r=D/FP_SIDE,c=D%FP_SIDE,best=-1,nb=INF;
 for(int k=0;k<4;k++) {
  int ar=r-(k>>1),ac=c-(k&1);if(!inside(ar,ac))continue;
  int n=chamber_walls(a,o,s,cell(ar,ac),2,2);
  if(n>=0 && n<nb){nb=n;best=cell(ar,ac);}
 }
 return best;
}
/* Before descending to Ice from inside a room around the ladder that lacks
 * one or two walls (we left through it, or a bat hunt or coal stop mined it),
 * put them back: the next arrival from Ice, often a low-health escape, then
 * lands behind stone. Only with the stone in hand and no melee mob near. */
static int icetrip_seal(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int lock=icetrip_lock(a,o,s);if(lock<0)return -1;
 int n=chamber_walls(a,o,s,lock,2,2),r=lock/FP_SIDE,c=lock%FP_SIDE,start=cell(a->r,a->c);
 if(n<1 || n>2 || o->inv[FP_I_STONE]<n || wm_melee_within(a,3))return -1;
 if(a->r<r||a->r>=r+2||a->c<c||a->c>=c+2)return -1;
 for(int i=0;i<8;i++) {
  int wr,wc,d;wall_position(i,2,2,&wr,&wc,&d);
  int rr=r+wr,cc=c+wc,b=block(a,rr,cc);
  if(!(walkable(b)||b==FP_WATER||b==FP_LAVA))continue;
  int stand=cell(rr-dr[d],cc-dc[d]),partner=cell(rr-2*dr[d],cc-2*dc[d]);
  if(start==stand && o->dir==d)return FP_PLACE_STONE;
  if(start==partner)return d;
  return move_path(a,o,s,partner);
 }
 return -1;
}
/* A fully dark landing can face the map boundary. A blind torch then
 * fails, while an unknown move would lose the local coordinate frame. Use
 * the remembered ladder to reorient on the lit floor and return instead.
 * At most four headings and120 total actions are allowed for this repair. */
static int ice_dark_entry(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!a->floors[7].seen || a->floors[8].seen)return -1;
 if(a->ice_dark_until && a->t>=a->ice_dark_until){a->ice_dark_mode=0;a->ice_dark_mask=30;return -1;}
 if(a->level==7) {
  int here=cell(a->r,a->c);
  if(here!=a->floors[7].up || !o->inv[FP_I_TORCH])return -1;
  int unknown=0;for(int d=1;d<=4;d++)unknown+=block(a,a->r+dr[d],a->c+dc[d])==UNKNOWN;
  if(unknown<4){a->ice_dark_mode=0;return -1;}
  if(a->ice_dark_mask==30)return -1;
  if(!a->ice_dark_until) {
   if(a->previous.level!=7 || a->previous_action!=FP_NOOP || a->skill!=FP_EXPLORE)return -1;
   a->ice_dark_until=a->t+120;
  }
  if(a->previous.level==7 && a->previous_action==FP_PLACE_TORCH && o->inv[FP_I_TORCH]>=a->previous.inv[FP_I_TORCH])a->ice_dark_mask|=1<<o->dir;
  if(!(a->ice_dark_mask&(1<<o->dir)))return FP_PLACE_TORCH;
  if(a->ice_dark_mask==30)return -1;
  a->ice_dark_mode=-1;
  if(fp_goal_call_excursion(a,40,0,a->floors[7].up))a->runtime.frame[1].return_outcome=FP_RO_ABANDONED;
  return FP_ASCEND;
 }
 if(a->level!=6 || !a->ice_dark_mode)return -1;
 if(o->food<=1 || o->drink<=1 || o->energy<=1){a->ice_dark_mode=0;a->ice_dark_mask=30;return -1;}
 int down=a->floors[6].down,here=cell(a->r,a->c);
 if(down<0)return -1;
 if(a->ice_dark_mode<0) {
  int best=0,score=INF;
  for(int d=1;d<=4;d++)if(!(a->ice_dark_mask&(1<<d))) {
   int fr=down/FP_SIDE+dr[d],fc=down%FP_SIDE+dc[d],pr=down/FP_SIDE-dr[d],pc=down%FP_SIDE-dc[d];
   if(!inside(fr,fc) || !inside(pr,pc))continue;
   int front=block(a,fr,fc),partner=cell(pr,pc),cost=INF;
   if(front!=UNKNOWN && front!=FP_INVALID && !walkable(front))cost=s->dist[down]+10;
   else if(walkable(block(a,pr,pc)) && !s->mobs[partner])cost=s->dist[partner]+10;
   if(cost<=80 && cost<score){best=d;score=cost;}
  }
  if(!best){a->ice_dark_mode=0;a->ice_dark_mask=30;return -1;}
  a->ice_dark_mode=best;
  if(a->runtime.depth==2 && a->runtime.frame[1].purpose==40)a->runtime.frame[1].mode=best;
 }
 int d=a->ice_dark_mode;
 if(here==down && o->dir==d) {
  if(a->runtime.depth==2 && a->runtime.frame[1].purpose==40)a->runtime.frame[1].return_outcome=FP_RO_COMPLETED;
  return FP_DESCEND;
 }
 int fr=down/FP_SIDE+dr[d],fc=down%FP_SIDE+dc[d],pr=down/FP_SIDE-dr[d],pc=down%FP_SIDE-dc[d];
 if(here==down && block(a,fr,fc)!=UNKNOWN && block(a,fr,fc)!=FP_INVALID && !walkable(block(a,fr,fc)))return d;
 int partner=cell(pr,pc);
 if(here==partner)return d;
 if(s->dist[partner]<INF)return move_path(a,o,s,partner);
 a->ice_dark_mode=0;a->ice_dark_mask=30;return -1;
}
static int troll_dark_entry(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!a->floors[5].seen || a->floors[6].seen)return -1;
 if(a->troll_dark_until && a->t>=a->troll_dark_until){a->troll_dark_mode=0;a->troll_dark_mask=30;return -1;}
 if(a->level==5) {
  int here=cell(a->r,a->c);
  if(here!=a->floors[5].up || !o->inv[FP_I_TORCH])return -1;
  int unknown=0;for(int d=1;d<=4;d++)unknown+=block(a,a->r+dr[d],a->c+dc[d])==UNKNOWN;
  if(unknown<4){a->troll_dark_mode=0;return -1;}
  if(a->troll_dark_mask==30)return -1;
  if(!a->troll_dark_until) {
   if(a->previous.level!=5 || a->previous_action!=FP_NOOP || a->skill!=FP_EXPLORE)return -1;
   a->troll_dark_until=a->t+120;
  }
  if(a->previous.level==5 && a->previous_action==FP_PLACE_TORCH && o->inv[FP_I_TORCH]>=a->previous.inv[FP_I_TORCH])a->troll_dark_mask|=1<<o->dir;
  if(!(a->troll_dark_mask&(1<<o->dir)))return FP_PLACE_TORCH;
  if(a->troll_dark_mask==30)return -1;
  a->troll_dark_mode=-1;
  if(fp_goal_call_excursion(a,41,0,a->floors[5].up))a->runtime.frame[1].return_outcome=FP_RO_ABANDONED;
  return FP_ASCEND;
 }
 if(a->level!=4 || !a->troll_dark_mode)return -1;
 if(o->food<=1 || o->drink<=1 || o->energy<=1){a->troll_dark_mode=0;a->troll_dark_mask=30;return -1;}
 int down=a->floors[4].down,here=cell(a->r,a->c);
 if(down<0)return -1;
 if(a->troll_dark_mode<0) {
  int best=0,score=INF;
  for(int d=1;d<=4;d++)if(!(a->troll_dark_mask&(1<<d))) {
   int fr=down/FP_SIDE+dr[d],fc=down%FP_SIDE+dc[d],pr=down/FP_SIDE-dr[d],pc=down%FP_SIDE-dc[d];
   if(!inside(fr,fc) || !inside(pr,pc))continue;
   int front=block(a,fr,fc),partner=cell(pr,pc),cost=INF;
   if(front!=UNKNOWN && front!=FP_INVALID && !walkable(front))cost=s->dist[down]+10;
   else if(walkable(block(a,pr,pc)) && !s->mobs[partner])cost=s->dist[partner]+10;
   if(cost<=80 && cost<score){best=d;score=cost;}
  }
  if(!best){a->troll_dark_mode=0;a->troll_dark_mask=30;return -1;}
  a->troll_dark_mode=best;
  if(a->runtime.depth==2 && a->runtime.frame[1].purpose==41)a->runtime.frame[1].mode=best;
 }
 int d=a->troll_dark_mode;
 if(here==down && o->dir==d) {
  if(a->runtime.depth==2 && a->runtime.frame[1].purpose==41)a->runtime.frame[1].return_outcome=FP_RO_COMPLETED;
  return FP_DESCEND;
 }
 int fr=down/FP_SIDE+dr[d],fc=down%FP_SIDE+dc[d],pr=down/FP_SIDE-dr[d],pc=down%FP_SIDE-dc[d];
 if(here==down && block(a,fr,fc)!=UNKNOWN && block(a,fr,fc)!=FP_INVALID && !walkable(block(a,fr,fc)))return d;
 int partner=cell(pr,pc);
 if(here==partner)return d;
 if(s->dist[partner]<INF)return move_path(a,o,s,partner);
 a->troll_dark_mode=0;a->troll_dark_mask=30;return -1;
}
#include "full_strategic.inc"
#include "lg_mid.inc"
#include "lg_ice.inc"

static int autonomous(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 a->goal=-1;int act=-1,fire=fire_phase(a);
 if(o->sleeping||o->resting)return emit(a,o,s,FP_NOOP,FP_RECOVER);
 /* Ice stream (lg_ice.inc): the Ice arrival XP point, often carried up to
  * Fire by the ice-enchant trip before it is spent. */
 if(LGI_DEX && lg_ice_region(a) && o->xp && wm_incoming_damage(a,o,s,FP_NOOP)==0){act=lg_ice_xp(a,o);if(act>=0)return emit(a,o,s,act,FP_UPGRADE);}
 /* Spend the Fire floor's XP before travel or enchantment preparation
  * takes priority. Stronger spells shorten elemental fights, and improve
  * mana recovery. A ready attack or incoming projectile still interrupts. */
 /* mid stream (lg_mid.inc): fire-phase attribute spending (off by default). */
 if(lg_mid_xp6(a,o) && wm_incoming_damage(a,o,s,FP_NOOP)==0)return emit(a,o,s,FP_LEVEL_DEX,FP_UPGRADE);
 if(a->level==6 && o->xp && o->intelligence<5 && wm_incoming_damage(a,o,s,FP_NOOP)==0)
  return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
 /* Gem trip. Once Fire is clear and its exit known this is the last chance
  * before Ice: one more plate (1-2 diamonds short) or the sapphires for four
  * ice pieces are worth a climb to prospect the cleared Troll Mines in fire
  * armour (oracles from Fire arrival: +3 diamonds +7.5pp, +3 sapphires
  * +6.1pp). It ends on target, when the prospecting budget is spent, when the
  * mines offer nothing more to light or mine (below), or after 1,500 steps;
  * the mines stop then provisions the descent as usual. */
 int prep=fire?a->fire_prep:0;
 if(fire && !prep && a->level==6 && o->clear && a->floors[6].down>=0)
  prep=a->fire_prep=a->fire_dia<3500 && o->inv[FP_I_PICK]>=3 && o->inv[FP_I_TORCH]>8 &&
   ((o->inv[FP_I_DIAMOND]>=1 && wm_armour(o)<8) || o->inv[FP_I_SAPPHIRE]<4)?1:2;
 if(prep==1 && (++a->fire_prep_steps>1500 || a->fire_dia>=3500 ||
    !((o->inv[FP_I_DIAMOND]>=1 && wm_armour(o)<8) || o->inv[FP_I_SAPPHIRE]<4)))prep=a->fire_prep=2;
 if(fire && (a->level!=6 || (a->floors[6].up!=cell(a->r,a->c) && a->floors[6].down!=cell(a->r,a->c))))a->fire_wait=0;
 /* Floor travel happens before enemy attacks. Take an unlocked exit
  * immediately when it leads to the next unexplored realm. */
 if((a->level==6 || a->level==7) && !a->floors[a->level+1].seen && o->clear && a->floors[a->level].down==cell(a->r,a->c) && prep!=1)
  return emit(a,o,s,FP_DESCEND,FP_TRAVEL);
 /* Known healing can interrupt a fight even when every retreat is blocked. */
 for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==0 && o->health<=6)
  return emit(a,o,s,FP_POTION_RED+i,FP_RECOVER);
 /* A troll costs one unanswered hit even when we strike first: 20 HP needs
  * two diamond-sword blows. After that hit, the next troll would leave us
  * near death, so heal while another hit would leave less than 3 health. */
 int troll_low=a->level==5 && o->health<8+o->str-0.01f && o->health-wm_attack_damage(o,6)<3;
 /* One potion colour restores 8 health, and most agents reach the mines
  * carrying it unidentified. Test unknown colours while a harmful one
  * (-3 health) is survivable and no troll is close; then spend healing
  * potions when a troll would otherwise land the hit that kills. */
 if(a->level==5) {
  int near=0,heal=-1;
  for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=4)near=1;
  for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==0)heal=i;
  if(heal>=0 && o->health-wm_attack_damage(o,6)<3 && (near || o->health<=4))return emit(a,o,s,FP_POTION_RED+heal,FP_RECOVER);
  int quiet=!TB(0) || !troll_phase(a) || (!wm_melee_within(a,10) && o->health>=6.5f);
  if(TB(0) && troll_phase(a))for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind)quiet=0;
  /* Unknown-colour tests can lose 3 HP. A visible incoming projectile
   * must first reach the ordinary tactical dodge decision. Known healing
   * retains its emergency priority above this optional test. */
  if(heal<0 && !near && quiet && o->health>=5 && o->health<=8+o->str-3 &&
     (!troll_phase(a) || wm_incoming_damage(a,o,s,FP_NOOP)==0))
   for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]<0)return emit(a,o,s,FP_POTION_RED+i,FP_RECOVER);
 }
 /* Supply trips at that health meet the next troll. Recovery holds us in
  * shelter until drink or food is critical, as it does once started. */
 int hold=troll_low && o->drink>2 && o->food>2;
 /* Realm retreats follow the route to the up ladder (tactical). */
 int routed=a->level==4 || a->level==5 || a->level==6 || a->level==7 || (a->level==1 && !a->floors[5].seen);
 if(routed)paths(a,o,cfg,s);
 /* Pigmen keep pace and elementals fire point-blank once cornered, so a
  * low fight on Fire ends in death (pre-clear deaths sat a median ~8
  * cells from the up ladder). Mobs never follow a ladder: take it while
  * it is near, before another exchange. */
 if(fire && a->level==6 && o->health<=fire_retreat(o)) {
  int up=a->floors[6].up;
  if(up==cell(a->r,a->c))return emit(a,o,s,FP_ASCEND,FP_TRAVEL);
  if(up>=0 && s->dist[up]<=250){act=travel(a,o,s,0);if(act>=0)return emit(a,o,s,act,FP_TRAVEL);}
 }
 /* A new excursion starts on the first Fire step after a climb from Ice. */
 if(icetrip(a) && a->level==6 && a->t && a->previous.level==7){a->icetrip_wait=0;a->icetrip_guard=0;a->icetrip_dest=ICETRIP_OUT;a->icetrip_start=a->t;}
 /* A food (or rest) stop in the mines: stay until fed (below). */
 if(icetrip(a) && a->level==5 && a->t && a->previous.level==6 && !enchant_trip(a,o) && a->icetrip_dest==ICETRIP_OUT)a->icetrip_dest=ICETRIP_MINES;
 act=tactical(a,o,s);if(act>=0)return emit(a,o,s,act,FP_FIGHT);
 act=troll_dark_entry(a,o,s);if(act>=0)return emit(a,o,s,act,act==FP_PLACE_TORCH?FP_LIGHT:FP_TRAVEL);
 act=ice_dark_entry(a,o,s);if(act>=0)return emit(a,o,s,act,act==FP_PLACE_TORCH?FP_LIGHT:FP_TRAVEL);
 /* Troll Mines doorway fights. After a heal the pack waits outside the room
  * (melee damage per step doubles in the 30 steps after recovery ends) and
  * a walk out meets it unfaced. From inside, open the wall a troll can reach
  * and hold: every troll must then enter that one cell, in front of us, so
  * it is struck first and pre-cast spells meet it there. Reseal when another
  * hit would leave little health. Inside with partial health, keep healing. */
 if(TB(8) && a->level==5 && troll_phase(a)) {
  int here=cell(a->r,a->c),fr=a->r+dr[o->dir],fc=a->c+dc[o->dir],faced=inside(fr,fc)?cell(fr,fc):-1;
  int near=wm_melee_within(a,6);
  if(a->troll_door) {
   int dcell=a->troll_door-1,close=0;
   /* Hold while a troll can still come (within five of the doorway): a hold
    * released for a drink walked out into the troll it was waiting for. */
   for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && abs(e.p/FP_SIDE-dcell/FP_SIDE)+abs(e.p%FP_SIDE-dcell%FP_SIDE)<=5)close=1;}
   /* Releasing must not hand the open room back to recovery, which reseals it. */
   if(a->troll_door_stand!=here+1 || a->troll_door!=faced+1 || !close || a->t-a->troll_door_t>40 || o->drink<=1 || o->food<=1){a->troll_door=0;if(o->health>=8+o->str-0.01f)a->recovering=0;}
   else {
    if(o->health<=wm_attack_damage(o,6)+1 && !s->mobs[faced] && o->inv[1]>0 && walkable(block(a,fr,fc))){a->troll_door=0;return emit(a,o,s,FP_PLACE_STONE,FP_RECOVER);}
    return emit(a,o,s,FP_NOOP,FP_FIGHT);
   }
  }
  if(sealed_in(a,o,s) && near) {
   int full=o->health>=8+o->str-0.01f,critical=o->drink<=1 || o->food<=1;
   if(!full && !critical)a->recovering=1;
   else {
    /* Open the wall that a nearby troll can reach, away from pools. */
    int bestd=0,bestscore=0;
    for(int d=1;d<=4;d++) {
     int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int b=block(a,r,c);
     if(!solid(b) || !mineable(o,b) || b==FP_TABLE || b==FP_FURNACE)continue;
     int open=0;for(int k=1;k<=4;k++){int r2=r+dr[k],c2=c+dc[k];if((r2!=a->r || c2!=a->c) && walkable(block(a,r2,c2)))open=1;}
     if(!open)continue;
     int score=d==o->dir?2:0;
     for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)<=3)score+=10;}
     for(int k=2;k<=6;k++){int r2=a->r+k*dr[d],c2=a->c+k*dc[d];if(inside(r2,c2) && block(a,r2,c2)==FP_WATER)score-=8;}
     if(score>bestscore){bestscore=score;bestd=d;}
    }
    if(bestscore<10)bestd=0;
    if(bestd && bestd!=o->dir)return emit(a,o,s,bestd,FP_FIGHT);
    if(bestd){a->recovering=0;a->troll_door=faced+1;a->troll_door_stand=here+1;a->troll_door_t=a->t;return emit(a,o,s,FP_DO,FP_FIGHT);}
   }
  }
 }
 /* Refit first. 16% of Fire entrants lack fire enchants (P(7) .49 with none,
  * .91 with four): pigman blows cost 6.8 instead of 2.8. 60% of them carry an
  * iron pickaxe (no ruby mining), half of those three diamonds but no wood,
  * and the generic rules crafted only after clearance or went to the
  * Overworld for wood. Fire trees give wood and Fire rock is 2.5% ruby: fix
  * the pickaxe, make plates and mine rubies beside the ladder first; the mines
  * stop then takes rubies to the Vault table (partial-stock rule). Ice is pitch
  * dark and coal-free, and the Ice phase mined coal and fire-tree wood on its
  * Fire food trips in ice armour: stock both here, in fire armour, once Fire
  * is clear, early in a visit (drink >= 7). */
 if(fire && a->level==6 && !wm_hostile_within(a,5) && o->health>fire_retreat(o)+1 &&
    o->drink>=5 && o->food>=3 && o->energy>cfg->sleep_at+1) {
  int fp=0;for(int i=0;i<4;i++)fp+=o->armour_enchant[i]==1;
  int table=0;const FPFloor *f6=&a->floors[6];
  for(int p=f6->known_lo;p<=f6->known_hi;p++)if(s->dist[p]<=250 && near(a,p,0)){table=1;break;}
  int sk=FP_GATHER;act=-1;
  if(o->inv[FP_I_PICK]==3 && o->inv[FP_I_DIAMOND]>=3) {
   if(o->inv[FP_I_WOOD]<(table?1:3))act=seek_block(a,o,s,FP_TREE,25);
   else {act=craft_radius(a,o,cfg,s,FP_DIAMOND_PICK,250);sk=FP_CRAFT;}
  }
  if(act<0 && wm_armour(o)<8 && o->inv[FP_I_DIAMOND]>=3) {
   if(o->inv[FP_I_WOOD]<(table?0:2)){act=seek_block(a,o,s,FP_TREE,25);sk=FP_GATHER;}
   else {act=craft_radius(a,o,cfg,s,FP_DIAMOND_ARMOUR,250);sk=FP_CRAFT;}
  }
  if(act<0){sk=FP_GATHER;if(o->inv[FP_I_PICK]>=4 && wm_armour(o) && fp<4 && o->inv[FP_I_RUBY]<4-fp && vault_trip_ok(a,o))act=seek_block(a,o,s,FP_RUBY,20);}
  if(act<0 && o->clear && a->floors[6].down>=0 && o->inv[FP_I_PICK]>=1 && o->drink>=7) {
   if(o->inv[FP_I_COAL]<14)act=seek_block(a,o,s,FP_COAL,15);
   if(act<0 && o->inv[FP_I_WOOD]<12)act=seek_block(a,o,s,FP_TREE,15);
  }
  if(act>=0)return emit(a,o,s,act,sk);
 }
 /* Heal and sleep in the ladder airlock rather than crossing Fire low or
  * sheltering in the open (see icetrip_lock). */
 if(ICETRIP_LOCK && icetrip(a) && a->level==6 && !enchant_trip(a,o)) {
  int lock=icetrip_lock(a,o,s),mh=8+o->str;
  /* Healing turns food into health about one for one: start only with food
   * for it (the base shelter rule wants food > 2 too), or the room becomes a
   * starvation trap. */
  int want=(o->health<=6.5f && o->health<mh-0.01f && o->food>=3 && o->drink>=3) ||
           (o->energy<=cfg->sleep_at && o->food>=2 && o->drink>=2) ||
           (a->recovering && a->recovery_goal==lock && lock>=0 && o->food>=1 && o->drink>=1);
  /* Out of food or drink inside: stop. The room cannot feed us, and the
   * base exit (mine out, travel or forage) must not fight a re-seal. */
  if(a->recovering && a->recovery_goal==lock && lock>=0 && (o->food<1 || o->drink<1)){a->recovering=0;a->recovery_goal=-1;}
  if(want && lock>=0 && s->dist[lock]<=150) {
   a->recovering=1;a->recovery_goal=lock;a->recovery_width=a->recovery_height=2;
   act=recover(a,o,s);if(act>=0)return emit(a,o,s,act,FP_RECOVER);
  }
 }
 if(icetrip(a) && a->level==6) {
  int skill=FP_TRAVEL;act=icetrip_home(a,o,s,&skill);if(act>=0)return emit(a,o,s,act,skill);
  act=icetrip_forage(a,o,s,&skill);if(act>=0)return emit(a,o,s,act,skill);
 }
 /* Mobs on unlit mine cells are not observed. A gnome beside us in the dark
  * struck every six steps while exploration waited for a frontier (ep 27:
  * four hits from full health, no reply); unseen attackers dealt 29 of the 79 blows in
  * sampled floor-2 deaths. After a loss no visible enemy explains, light the
  * facing cell, whose torch also lights every neighbour, or strike an unlit
  * facing cell when no torch is left. Never step into an unlit cell: moves
  * are tracked only onto known ground, so the map frame would drift. */
 /* Troll Mines too: an invisible troll beside us in the dark struck from
  * full health to death while the agent waited for a frontier (ep 22: 27
  * torches unused, five hits, no reply). */
 int unseen_level=a->level==2 || (a->level==5 && mines_guard(a));
 if(unseen_level)wm_attribute_damage(a,o);
 /* Leave the line it fired along: move to the neighbour with the fewest water
  * or unseen cells in clear view along its row and column at range 2-5. */
 if(TB(12) && a->level==5 && troll_phase(a) && a->troll_slime_t && a->t-a->troll_slime_t<=1 && !wm_melee_within(a,2)) {
  int best=0,bestn=INF;
  for(int d=0;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];
   if(d && (!inside(r,c) || !walkable(block(a,r,c)) || s->mobs[cell(r,c)] || s->arrow_next[cell(r,c)]))continue;
   int n=d?0:1;
   for(int k=1;k<=4;k++)for(int j=1;j<=5;j++) {
    int b=block(a,r+j*dr[k],c+j*dc[k]);if(solid(b))break;
    if(j>=2 && (b==FP_WATER || b==UNKNOWN))n++;
   }
   if(n<bestn){bestn=n;best=d;}
  }
  if(best)return emit(a,o,s,best,FP_FIGHT);
 }
 if(unseen_level && a->t-a->unseen_hit<=4) {
  int r=a->r+dr[o->dir],c=a->c+dc[o->dir];
  int place_failed=a->previous_action==FP_PLACE_TORCH && a->previous_target==cell(r,c) && o->inv[FP_I_TORCH]>=a->previous.inv[FP_I_TORCH];
  /* A torch aimed off the map (unlit off-map cells stay unknown on this
   * floor) fails silently; ep 214 repeated it four times under gnome blows.
   * After a failed placement, light a valid neighbour instead. */
  if(shallow(a) && a->level==2 && place_failed && o->inv[FP_I_TORCH] && (act=place(a,o,s,FP_PLACE_TORCH))>=0 && act!=FP_PLACE_TORCH)return emit(a,o,s,act,FP_LIGHT);
  if(inside(r,c)) {
   int q=cell(r,c),b=block(a,r,c);
   /* Placement fails on a cell holding a mob: strike it instead. */
   int failed=a->level==5 && a->previous_action==FP_PLACE_TORCH && a->previous_target==q && o->inv[FP_I_TORCH]>=a->previous.inv[FP_I_TORCH];
   if(failed && (b==UNKNOWN || walkable(b)))return emit(a,o,s,FP_DO,FP_FIGHT);
   if(o->inv[FP_I_TORCH] && !a->floors[a->level].items[q] && !s->mobs[q] && (b==UNKNOWN||b==FP_GRASS||b==FP_PATH||b==FP_SAND))
    return emit(a,o,s,FP_PLACE_TORCH,FP_LIGHT);
   if(b==UNKNOWN)return emit(a,o,s,FP_DO,FP_FIGHT);
  }
  if(o->inv[FP_I_TORCH] && (act=place(a,o,s,FP_PLACE_TORCH))>=0)return emit(a,o,s,act,FP_LIGHT);
 }
 /* Low-health heals hold 70% of floor-5 deaths, a median 42 steps after the
  * heal starts: walking to a shelter (up to 35 steps) or to water while a
  * troll closes in, which then arrives beside or behind us (ep 233 r1 walked
  * nine steps along a troll's line at 6 health and took the hit unfaced).
  * With a troll in view and not yet adjacent, stop and face its approach:
  * axis spells and the first blow then land before its own. Bounded. */
 if(TB(19) && a->level==5 && troll_phase(a) && (troll_low || o->health<=6) && !sealed_in(a,o,s)) {
  int best=-1,bd=9;
  for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);if(!e.kind && dd>=2 && dd<bd){best=i;bd=dd;}}
  if(best<0)a->troll_hold=0;
  else if(a->troll_hold<40) {
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c;
   int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
   a->troll_hold++;
   if(o->dir==want)return emit(a,o,s,FP_NOOP,FP_FIGHT);
   int nr=a->r+dr[want],nc=a->c+dc[want];
   if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || s->mobs[cell(nr,nc)] || bd>=4))return emit(a,o,s,want,FP_FIGHT);
   return emit(a,o,s,FP_NOOP,FP_FIGHT);
  }
 }
 if(a->level==8 && a->boss_hits>=8 && o->boss_vulnerable)return emit(a,o,s,FP_NOOP,FP_RECOVER);
 if(!routed)paths(a,o,cfg,s);
#define TRY(expression,skill) do {act=(expression);if(act>=0)return emit(a,o,s,act,skill);} while(0)
 /* Each cleared Graveyard wave is a safe, unlimited recovery window:
  * food, drink and energy stop decaying on this floor. Restore health and
  * mana before touching the Necromancer and starting the next wave. */
 if(a->level==8 && o->boss_vulnerable) {
  if(o->health<8+o->str-0.01f && o->food>0 && o->drink>0 && o->energy>0)
   return emit(a,o,s,FP_NOOP,FP_RECOVER);
  if(o->mana<6+3*o->intelligence)return emit(a,o,s,FP_NOOP,FP_RECOVER);
  TRY(seek_block(a,o,s,FP_BOSS,100),FP_FIGHT);
 }
 /* Ice is pitch dark and has no coal: its exploration is torches, 1 coal
  * + 1 wood per 4. Fire stone is 5% coal and fire trees give wood. Stock
  * up close to the route once Fire is clear, before the first descent. */
 if(FIRE_ICEPREP && fire && a->level==6 && o->clear && o->health>=7 && o->inv[FP_I_PICK]>=1 && wm_incoming_damage(a,o,s,FP_NOOP)==0) {
  if(o->inv[FP_I_COAL]<10)TRY(seek_block(a,o,s,FP_COAL,10),FP_GATHER);
  if(o->inv[0]<8)TRY(seek_block(a,o,s,FP_TREE,10),FP_GATHER);
 }
 if(prep==1 && a->level==6)TRY(travel(a,o,s,0),FP_TRAVEL);
 if((a->level==6 || a->level==7) && !a->floors[a->level+1].seen && o->clear && prep!=1)TRY(travel(a,o,s,1),FP_TRAVEL);
 /* A shelter cannot be sealed beside realm melee, and walking off only
  * leads the chase. When one more hit would leave little, leave the floor:
  * mobs never follow a ladder, and the floor above is where we heal. */
 /* Dungeon retreat. Health returns one point per 25 steps, and a Dungeon
  * shelter is built under fireballs: low-health fights there ended in kiting
  * an orc beside a mage at 2-3 health (ep 173). Mobs never follow a ladder and
  * the Overworld has water, cows and stone for a safe heal. Leave when one
  * more orc blow or fireball pair would leave little. */
 if(a->level==1 && !a->floors[5].seen && a->floors[1].up>=0 && s->dist[a->floors[1].up]<=200) {
  int near=0;for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);if(d<=(e.kind?6:4))near=1;}
  if((near && o->health<=5) || o->health<=3)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* Heal and sleep on the Overworld, not in a Dungeon shelter: 28 of 48
  * sampled floor-1 deaths happened in recovery mode (14 asleep unsealed, 14
  * under fire while building or holding a shelter for 10-350 steps). When
  * the up ladder is close, a climb ends every pursuit and the Overworld has
  * stone, water and cows. Leave for sleep with energy to spare: climbs that
  * started at energy 3 crossed fights and reached the Overworld at energy 0
  * with health already draining (ep 14 r1, 124 r1). */
 /* Shallow: the sleep climb waits for less energy when the ladder is close
  * and no mob is near. At 5, Dungeon visits lasted 60-90 steps (energy 8 ->
  * 5), never reached past 15 cells from the ladder, and the far exit stayed
  * unknown (arrive1 ep 101, 54 round trips). Each energy point is 31 awake
  * steps: 3 within 10 steps of the ladder, 4 within 20, else 5. At 3 from 27
  * cells a fight on the way left 2 health and 0 energy (arrive1 ep 12). */
 int climb_energy=5;
 if(shallow(a) && a->level==1 && a->floors[1].up>=0) {
  int d=s->dist[a->floors[1].up];climb_energy=d<=100?3:d<=200?4:5;
  for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)<=(e.kind?8:6))climb_energy=5;}
 }
 if(a->level==1 && !a->floors[5].seen && a->floors[1].up>=0 && s->dist[a->floors[1].up]<=300 &&
    ((o->health<8+o->str-0.01f && o->health<=6) || o->energy<=climb_energy))TRY(travel(a,o,s,0),FP_TRAVEL);
 if(routed && a->level!=1) {
  int near=0;for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=4)near=1;
  /* Ice crowds: most Ice deaths (25 traced, a3) went from full health to zero
   * within 30 steps with two or three frost trolls within four cells and an
   * elemental within six. Each troll lands about one hit, so leave while the
   * health covers the hits of every troll within three cells (and of an
   * elemental with a clear line), not just one. Escapes are protective: with
   * none at all, Ice melee deaths rose 390 -> 530 (ice_a4). */
  float threat=wm_attack_damage(o,a->level+1);
  if(a->level==7) {
   int n=0;
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int er=e.p/FP_SIDE,ec=e.p%FP_SIDE,d=abs(er-a->r)+abs(ec-a->c);
    if(!e.kind && d<=3)n++;
    if(e.kind && wm_firing_line(a,a->r,a->c,er,ec))n++;
   }
   if(n>1)threat*=n;
  }
  if(near && (a->level!=5 || ((!o->clear || (TB(16) && troll_phase(a))) && a->floors[5].up>=0 && s->dist[a->floors[5].up]<=150)) && o->health<=threat+1)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* A low-health Vault retreat must precede food and fountain errands:
  * ep 4 hunted at 2.2 HP, while ep 87 alternated water and recovery. */
 /* Troll Mines escapes land here at low health, and this rule carried them on
  * to the Sewers: a 350-step trip with 5.3% deaths (starvation, lizards), then
  * the walk back. With no Vault mob in sight, heal by the ladder instead. */
 int vault_calm=TB(15) && troll_phase(a) && a->level==4 && !wm_melee_within(a,6);
 if(TB(15) && vault_calm)for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind)vault_calm=0;
 if(a->level==4 && !a->recovering && o->health<8+o->str-0.01f && !vault_calm &&
    o->health<=2*wm_attack_damage(o,5))TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Troll recovery chases end in corners. Leave before supply errands or
  * shelter construction, unless the player already occupies a sealed room. */
 /* Cleared mines still send a troll every ~150 steps, and low heals there died
  * 2.8% of the time (fights at 4-6 health). Escape by the ladder there too. */
 if(troll_low && (!o->clear || (TB(16) && a->level==5 && troll_phase(a)))) {
  int near=0;for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=4)near=1;
  int sheltered=0;
  if(a->recovering && a->recovery_goal>=0) {
   int rr=a->recovery_goal/FP_SIDE,cc=a->recovery_goal%FP_SIDE;
   sheltered=a->r>=rr && a->r<rr+a->recovery_height && a->c>=cc && a->c<cc+a->recovery_width &&
             chamber_walls(a,o,s,a->recovery_goal,a->recovery_width,a->recovery_height)==0;
  }
  if(near && !sheltered && a->floors[5].up>=0 && s->dist[a->floors[5].up]<=150)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* Ice stream (lg_ice.inc): supply goals, below fights and escapes. */
 {int sk=FP_TRAVEL;act=lg_ice_supply(a,o,cfg,s,&sk);if(act>=0)return emit(a,o,s,act,sk);}
 /* With the ice-armour trip pending, every Ice step is taken in fire armour
  * (frost troll hits 5.8-7.4; 4% of entrants died on this first visit, p90
  * 126 steps spent drinking or healing). Fire is the safer floor in fire
  * armour and the Troll Mines have water: drink only beside the arrival
  * ladder, then leave. Adjacent fights are still handled above. */
 if(a->level==7 && enchant_trip(a,o) && o->drink>=2) {
  if(o->drink<5+2*o->dex)TRY(seek_block(a,o,s,FP_WATER,4),FP_DRINK);
  TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 if(a->recovering && o->energy>=7+2*o->dex && o->health>=8+o->str-0.01f)a->recovering=0;
 if(o->drink>=7+2*o->dex){a->drinking=0;a->water_goal=-1;}
 if(o->drink<=(a->recovering?2:cfg->drink_at) || (!a->achieved[4] && o->drink<=8))a->drinking=1;
 if(a->drinking && !hold)TRY(seek_water(a,o,s),FP_DRINK);
 /* With no mines water known, the climb to the Vault's fountains at drink 0
  * starts while health already drains and often ends in a thirst death up
  * there. Leave while two drinks remain. */
 /* mid stream (lg_mid.inc): troll-phase supply goals; inert outside its region. */
 {int sk=FP_EAT;act=lg_mid_supply(a,o,cfg,s,hold,&sk);if(act>=0)return emit(a,o,s,act,sk);}
 if(a->level==5 && a->drinking && o->drink<=2)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Floor 5 is a food desert: its bats are seen only on lit cells (4 meals
  * per 1,000 steps against 7 on floors 3-4). Climbs at food 0 died on 12% of
  * trips, health already draining on the walk. Leave at food 3 instead
  * when no animal is in reach; the lit floors above have visible ones. */
 if(TB(2) && a->level==5 && troll_phase(a) && o->food<=3 && !hold && !lg_mid_owns_food(a)) {
  TRY(forage(a,o,s),FP_EAT);
  TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* Without reachable water on floor 1, the climb used to start at zero drink,
  * after health had begun to fall, through corridors where mobs block the
  * route. Leave with enough drink for the trip. Agents climbing from dark
  * mines without torches arrived on floor 1 at zero drink and died there. */
 if((a->level==1 || a->level==2) && a->drinking && o->drink<=(shallow(a) && a->level==2?4:3))TRY(travel(a,o,s,0),FP_TRAVEL);
 /* With no reachable water known, waiting in a shelter cannot restore
  * hydration. Search for water before recovery takes over. */
 /* Shallow: remembered water beyond the 100-step radius is still water;
  * a tree search had led one agent 120 steps from it (arrive1 ep 29). */
 if(shallow(a) && a->level==0 && a->drinking)TRY(seek_block(a,o,s,FP_WATER,500),FP_DRINK);
 if(a->level==0 && a->drinking && o->drink<=2)TRY(explore(a,o,cfg,s),FP_EXPLORE);
 /* The Vault has about four fountains and some maps have none. A recovery
  * sleep there can use up the remaining drink before one is found, and the
  * drink==0 climb below starts with health already draining. Climb to the
  * Sewers' plentiful water while the trip is still affordable. */
 if(a->level==4 && o->drink<=4 && seek_block(a,o,s,FP_WATER,100)<0)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Ice has no reliable food supply. Start the return before starvation
  * consumes the health needed to cross the cleared Fire Realm. */
 if(a->level==7 && o->food<=3)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* A food return must continue through Fire when no animal is reachable.
  * Descending straight back to Ice would repeat the trip without refilling. */
 if(a->level==6 && a->floors[7].seen && o->food<=2) {
  TRY(forage(a,o,s),FP_EAT);
  TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 if(fire && a->level==6) {
  /* An identified energy potion (+8) buys a full visit without sleeping. */
  if(o->energy<=cfg->sleep_at)
   for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==4)return emit(a,o,s,FP_POTION_RED+i,FP_RECOVER);
  /* Sleep and healing need water. A shelter here drains drink that no rest
   * restores, and the pack that gathers outside meets us on waking (ep 1).
   * Climb to the mines water for both; a player whose armour leaves a big
   * blow leaves before the second one can kill. */
  if(o->energy<=cfg->sleep_at || o->health<=fire_retreat(o))TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* Fire has no water. Leave enough hydration and health for the route
  * back to the mines, rather than starting it after starvation begins. */
 if(a->level==6 && (o->drink<=2 || o->health<=4))TRY(travel(a,o,s,0),FP_TRAVEL);
 if(o->food<=(a->recovering||hold?2:cfg->eat_at)) {
  TRY(forage(a,o,s),FP_EAT);
 }
 /* forage() sees only animals in view. Agents slept at food 0 on the
  * Overworld and woke at 2 health (arrive1 ep 2); Overworld starvation was
  * 368 of 16,384 capture deaths. Animals spawn 3-14 cells away at 10% a step
  * on every lit shallow floor: walk to find one. In the Dungeon, climb when
  * none shows (the Overworld always has cows), instead of waiting for 0. */
 if(shallow(a) && a->level<=1 && o->food<=2 && wm_incoming_damage(a,o,s,FP_NOOP)==0) {
  TRY(hunt(a,o,s,2,40),FP_EAT);
  if(a->level==1 && a->floors[1].up>=0)TRY(travel(a,o,s,0),FP_TRAVEL);
  TRY(explore(a,o,cfg,s),FP_EAT);
 }
 if(shallow(a) && a->level==2 && o->food<=3 && a->floors[2].up>=0)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Ice has no coal, and coal is a torch. Fire is lit and 5% of its stone
  * is coal; fire trees give wood. Collect near the route during supply
  * visits while healthy, so that Ice exploration never stalls unlit. */
 if(a->level==6 && a->floors[7].seen && !enchant_trip(a,o) && o->health>=6 && wm_incoming_damage(a,o,s,FP_NOOP)==0) {
  if(o->inv[FP_I_COAL]<8)TRY(seek_block(a,o,s,FP_COAL,12),FP_GATHER);
  if(o->inv[0]<6)TRY(seek_block(a,o,s,FP_TREE,12),FP_GATHER);
 }
 if(a->level==7 && o->clear && o->inv[0]<4 && o->inv[FP_I_COAL]>0)TRY(ice_wood(a,o,s),FP_GATHER);
 /* A wood resupply from the Troll Mines is a ten-floor round trip. Torches,
  * the reason for wood there, are usually in stock (ep 70: 57 torches). */
 if(troll_phase(a) && a->level>=3 && o->inv[FP_I_TORCH]>=8)a->resupplying=0;
 /* A dark Mines floor with no torch and no wood cannot be explored: search
  * the Overworld for trees again while that search budget lasts. */
 if(shallow(a) && a->level==2 && a->wood_exhausted && a->shallow_search<6000 && !o->inv[FP_I_TORCH] && !o->inv[0] && a->floors[2].down<0)a->wood_exhausted=0;

 if(a->level>0 && a->level<8 && o->inv[0]<4 && !a->resupplying && !a->wood_exhausted && !(troll_phase(a) && o->inv[FP_I_TORCH]>=8) &&
    !(ICETRIP_WOOD && icetrip(a))){a->resupplying=1;a->resupply_tick=a->t;}
 /* Dungeon walls cannot be mined, so stone spent there is gone. Every
  * shelter needs it: agents without stone slept unsealed at energy 1 and a
  * single orc blow (3x3.5) killed them from full health (14 of 48 sampled
  * floor-1 deaths, ep 30, 72, 99). Fetch more from the Overworld in time. */
 if(a->level==1 && early(a) && o->inv[1]<3 && o->inv[FP_I_PICK]>0 && !a->resupplying){a->resupplying=1;a->resupply_tick=a->t;}
 /* Starving on a supply visit, the climb rule below walks floor after floor
  * towards the Overworld's cows and dies on the way (L3/L4 starvation, 6% of
  * Troll Mines entrants). The lit floors 3-4 have animals: search here. */
 if(TB(2) && troll_phase(a) && (a->level==3 || a->level==4) && o->food==0 && o->drink>0) {
  TRY(hunt(a,o,s,2,60),FP_EAT);
  TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);
  TRY(explore(a,o,cfg,s),FP_EXPLORE);
 }
 /* Icetrip: no food climb above the Troll Mines (see icetrip_mines). */
 int mines_food=ICETRIP_MINESTAY && icetrip(a) && a->level==5 && a->icetrip_dest==ICETRIP_MINES && !enchant_trip(a,o) && o->drink>0;
 if(a->level>0 && a->level<8 && (o->food==0 || o->drink==0) && !mines_food)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Start Ice recovery with enough health to reach and seal a shelter.
  * Fully healed players need no shelter even with a low strength stat. */
 /* Cleared Ice needs no kills, only the exit, and health returns at the same
  * rate while lighting as in a shelter: 44% of cleared-Ice steps were spent
  * healing, and every food trip during the search risks 2-15%. Heal there
  * only when another frost troll hit would leave little. */
 float ice_heal=a->level==7 && o->clear?fmaxf(6,wm_attack_damage(o,8)+2.5f):10;
 int needs_healing=o->health<8+o->str-0.01f && o->health<=(a->level==8?GRAVEYARD_RECOVER_HEALTH:a->level==7?ice_heal:6);
 if(troll_low)needs_healing=1;
 /* Vault knights cannot be killed without taking one hit, and two often
  * arrive together. Enter that fight only able to absorb both hits. */
 if(a->level==4 && o->health<8+o->str-0.01f && o->health<=2*wm_attack_damage(o,5))needs_healing=1;
 /* A Sewers revisit is a retreat from the Vault. Descend again only at full
  * health: two knight hits there take most of a partial heal. */
 if(a->level==3 && a->floors[4].seen && !a->resupplying && o->health<8+o->str-0.01f)needs_healing=1;
 /* Vault shelters are built beside knights that chase at most of our speed:
  * a recovery started with one within three cells ends in death about one
  * time in four. Climbing leaves every pursuer frozen on this floor, and the
  * cleared Sewers have water for the long heal. A knight that follows cannot
  * attack while we keep moving, so the ladder's distance is not limited; this
  * also covers returns from the Troll Mines, which land far from it. */
 if(a->level==4 && !a->recovering && needs_healing && o->food>2 && o->drink>2 && !vault_calm &&
    a->floors[4].up>=0)TRY(travel(a,o,s,0),FP_TRAVEL);
 if(a->level==8 && (a->recovering || (needs_healing && o->food>0 && o->drink>0))) {
  a->recovering=1;TRY(recover(a,o,s),FP_RECOVER);
 }
 /* Provision before a Troll Mines heal: long sealed heals ran food or drink
  * down to the leave-the-shelter threshold of 2 while health was still low
  * (healing is 46% of uncleared floor-5 time). Top up first when a pool or
  * a bat is close and no troll is. */
 if(a->level==5 && mines_guard(a) && !a->recovering && needs_healing && o->health>=4 && !wm_melee_within(a,4)) {
  if(o->drink<=6 && water_cost(a,s)<=80){a->drinking=1;TRY(seek_water(a,o,s),FP_DRINK);}
  if(o->food<=5)TRY(hunt(a,o,s,2,8),FP_EAT);
 }
 /* The Dungeon sends tired agents up to sleep at energy 5. Sleep before
  * going back down, or the two rules bounce between the ladders (63 climbs
  * per world) until energy reaches the usual threshold of 3. */
 int tired=a->level==0 && !a->floors[5].seen && a->floors[1].seen && o->energy<=5;
 if((a->recovering || o->energy<=cfg->sleep_at || tired || (needs_healing && o->food>2 && o->drink>2)) && a->level<8) {
  a->recovering=1;act=recover(a,o,s);
  /* Never sleep unsealed in the Dungeon: climb to the Overworld instead. */
  if(act==FP_SLEEP && a->level==1 && early(a) && !sealed_in(a,o,s))TRY(travel(a,o,s,0),FP_TRAVEL);
  if(act>=0)return emit(a,o,s,act,FP_RECOVER);
 }
 /* Strategic goals (full_strategic.inc): below survival, above the gear,
  * supply and descent rules. */
 {int sk=FP_EXPLORE;act=strategic(a,o,cfg,s,&sk);if(act>=0)return emit(a,o,s,act,sk);}

 /* Full diamond plates at Troll Mines entry raise P(8) .24 -> .53 (oracle,
  * bank E): a plate takes 0.6 off every troll blow, 0.4 off every slimeball,
  * and carries through Fire and Ice. Plate every three diamonds, and mine
  * every diamond seen while it is safe to walk. */
 if(TB(7) && a->level==5 && troll_phase(a) && wm_armour(o)<8 && o->inv[FP_I_PICK]>=3) {
  if(o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
  if(!troll_low && !wm_melee_within(a,4) && o->food>2 && o->drink>2)TRY(seek_block(a,o,s,FP_DIAMOND,150),FP_GATHER);
 }
 /* The mines stop of a food excursion whose Fire wait found no bat: stay
  * until fed. The base descended whenever no bat was in view, bouncing 5<->6
  * every step down to food 0, then climbed to the Vault and Sewers, where
  * 380 of 4,000 Ice entrants starved. Drink, rest and heal above; here hunt
  * what the torches show and walk the lit tunnels; go back down when fed. */
 if(ICETRIP_MINESTAY && icetrip(a) && a->level==5 && a->icetrip_dest==ICETRIP_MINES && !enchant_trip(a,o)) {
  /* Starving here for a minute: Fire's lit bats on the way home instead. */
  int stay=a->t-a->floors[5].entered;
  if(o->food>=7+2*o->dex-3 || stay>=300 || (o->food==0 && stay>=60)){a->icetrip_dest=ICETRIP_HOME;TRY(travel(a,o,s,1),FP_TRAVEL);}
  TRY(hunt(a,o,s,2,20),FP_EAT);
  TRY(explore(a,o,cfg,s),FP_EXPLORE);
 }
 /* Diamond plates on excursions through the Troll Mines. Each plate takes
  * 0.3 off every pigman blow and fireball and 0.4 off every frost troll blow
  * and iceball (oracles: +6 diamonds at Ice arrival +14.2pp, full diamond
  * armour +23.3pp). Ice entrants carry 0.9 diamonds and armour 5; the mines
  * still hold 5.1 diamonds, 3.1 within 40 steps of the down ladder, but the
  * agent knows 0.2 of them: the mines are dark and those cells were never
  * lit. An enchant trip's outbound rule skipped even the known ones. While
  * safe and supplied, mine known diamonds close by, craft a plate at three,
  * and light unseen rock near the route (one torch shows ~69 cells, ~0.7
  * diamond in stone), keeping torches for Ice. */
 if(ICETRIP_DIA && icetrip(a) && a->level==5 && wm_armour(o)<8 && o->inv[FP_I_PICK]>=3) {
  if(a->t && a->previous.level!=5)a->icetrip_prospect=0;
  int safe=!wm_melee_within(a,6) && o->health>=8 && o->food>=3 && o->drink>=3 && o->energy>=3;
  if(safe && o->inv[FP_I_DIAMOND]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
  if(safe)TRY(seek_block(a,o,s,FP_DIAMOND,ICETRIP_DIA_SEEK),FP_GATHER);
  if(safe && o->inv[FP_I_TORCH]>=12 && a->icetrip_prospect<ICETRIP_DIA_STEPS) {
   a->icetrip_prospect++;
   TRY(light_unseen(a,o,s,ICETRIP_DIA_GAIN,ICETRIP_DIA_PATH),FP_LIGHT);
  }
 }
 /* Ice has no enchantment table. Reuse the cleared route to the Sewers
  * with the sapphires gathered in the mines, then resume the expedition.
  * Armour takes precedence over spending a preparation gem on the sword. */
 if(a->floors[7].seen && wm_armour(o) && o->inv[FP_I_SAPPHIRE]) {
  int ice=0;for(int i=0;i<4;i++)ice+=o->armour_enchant[i]==2;
  if(ice<4) {
   if(a->level>3 && a->level<=7)TRY(travel(a,o,s,0),FP_TRAVEL);
   if(a->level==3) {
    if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_ARMOUR,160),FP_MAGIC);
    else TRY(recover(a,o,s),FP_RECOVER);
    /* An unseen table or recovering mana must not send us back down
     * into an immediate Vault-to-Sewers return loop. */
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
  }
 }
 /* mid stream (lg_mid.inc): troll-phase attribute spending (off by default). */
 if(lg_mid_xp(a,o))return emit(a,o,s,FP_LEVEL_DEX,FP_UPGRADE);
 /* Ice stream (lg_ice.inc): attribute spending at Ice arrival. */
 {act=lg_ice_xp(a,o);if(act>=0)return emit(a,o,s,act,FP_UPGRADE);}
 if(o->xp) {
  /* The last floor rewards strengthen spells against elemental resistance.
   * Keep the earlier strength upgrades for ordinary enemies and preparation. */
  if(a->level>=6 && o->intelligence<5)return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
  if(o->str<(cfg->str_first?3:2))return emit(a,o,s,FP_LEVEL_STR,FP_UPGRADE);
  if(o->intelligence<3)return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
  if(o->str<5)return emit(a,o,s,FP_LEVEL_STR,FP_UPGRADE);
  if(o->intelligence<5)return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
  if(o->dex<5)return emit(a,o,s,FP_LEVEL_DEX,FP_UPGRADE);
 }
 /* A troll survives one unenchanted diamond-sword blow plus a spell, so
  * pre-casting cannot spare the hit it lands. One ruby at the Vault's fire
  * table fixes that: climb back from the mines at full health to enchant. */
 /* Fire plates also take 0.6 off every slimeball and 0.2 off every troll blow,
  * and most floor-5 deaths come before clearance; the armour trip used to wait
  * for it. With rubies, mana for an enchant and no troll near the ladder side,
  * go now (mobs left behind stay where they are, so leave with none close). */
 if(TB(13) && a->level==5 && troll_phase(a) && !o->clear && wm_armour(o) && o->inv[FP_I_RUBY]>0 && o->mana>=9 && o->health>6 &&
    o->food>3 && o->drink>3 && !wm_melee_within(a,6) && vault_trip_ok(a,o)) {
  int fe=0;for(int i=0;i<4;i++)fe+=o->armour_enchant[i]==1;
  if(fe<4 && a->floors[5].up>=0 && s->dist[a->floors[5].up]<=300)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 if(a->floors[5].seen && !a->floors[6].seen && !o->sword_enchant && o->inv[FP_I_RUBY]>0 && vault_trip_ok(a,o)) {
  if(a->level==5 && o->health>=8+o->str-0.01f && troll_mana_ok(a,o))TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==4) {
   if(!fire_table_known(a)) {
    a->vault_search++;
    if(troll_phase(a)){TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);}
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
   /* A remembered table beyond the 100-step radius made the trip descend at
    * once and climb again from the mines, thousands of times (ep 541). */
   if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_SWORD,troll_phase(a)?400:100),FP_MAGIC);
   if(troll_phase(a) && fire_table_known(a) && o->mana>=9 && a->vault_search<vault_limit(a,o)) {
    a->vault_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);
   }
   if(o->mana<9)TRY(recover(a,o,s),FP_RECOVER);
  }
 }
 /* Without a ruby, a sapphire buys the same blow at the Sewers' ice table,
  * two cleared floors up. The search there shares the trip budget. */
 else if(a->floors[5].seen && !a->floors[6].seen && !o->sword_enchant && o->inv[FP_I_SAPPHIRE]>0 && a->vault_search<600) {
  if((a->level==5 || a->level==4) && o->health>=8+o->str-0.01f)TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==3) {
   if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_SWORD,160),FP_MAGIC);
   else TRY(recover(a,o,s),FP_RECOVER);
   a->vault_search++;TRY(explore(a,o,cfg,s),FP_EXPLORE);
  }
 }
 /* Icetrip: Ice's wood<4 rule sends the agent on a ten-floor trip to the
  * Overworld (0.22 per run once plates and torches use wood; 6% died). Fire
  * trees give wood: cut them on the way and end the trip there. (Clearing the
  * flag without wood made Ice re-raise it: an endless 6<->7 bounce.) The
  * flag is also no longer raised on excursion floors (below), where plates
  * and torches had drained wood in the mines. */
 if(ICETRIP_WOOD && icetrip(a) && a->resupplying && a->level==6) {
  if(o->inv[0]>=6)a->resupplying=0;
  else TRY(seek_block(a,o,s,FP_TREE,40),FP_GATHER);
 }
 if(a->resupplying && a->level>0 && a->level<8)TRY(travel(a,o,s,0),FP_TRAVEL);
 if(a->resupplying && a->level==0 && o->inv[0]>=cfg->wood_stock && o->inv[1]>=10 &&
    (o->inv[8]>=cfg->torch_stock || a->t-a->resupply_tick>600))a->resupplying=0;
 /* Shallow: a wood trip ends after 2,500 steps with whatever it found. Known
  * but unreachable trees kept one trip going until a thirst death 12,000
  * steps later (arrive1 ep 29). */
 if(shallow(a) && a->resupplying && a->level==0 && a->t-a->resupply_tick>2500) {
  a->resupplying=0;if(o->inv[0]<4)a->wood_exhausted=1;
 }
 /* Felled trees become grass and never regrow. Agents that had cut every
  * tree they knew stayed on the trip, exploring until they starved (5% of
  * episodes). Continue with the wood in hand once none is left in memory. */
 if(a->resupplying && a->level==0 && a->t-a->resupply_tick>600 && !(shallow(a) && a->shallow_search<6000)) {
  const FPFloor *f0=&a->floors[0];int trees=0;
  for(int p=f0->known_lo;p<=f0->known_hi && !trees;p++)trees=f0->map[p]==FP_TREE;
  if(!trees){a->resupplying=0;a->wood_exhausted=1;}
 }
 if(o->inv[FP_I_BOOK] && (!o->spells[0]||!o->spells[1]))return emit(a,o,s,FP_READ_BOOK,FP_MAGIC);
 if(!a->achieved[56] && o->spells[0] && o->mana>=2)return emit(a,o,s,FP_FIREBALL,FP_MAGIC);
 if(!a->achieved[58] && o->spells[1] && o->mana>=2)return emit(a,o,s,FP_ICEBALL,FP_MAGIC);
 if(!a->achieved[62] && o->health>=7)for(int i=0;i<6;i++)if(o->potions[i])return emit(a,o,s,FP_POTION_RED+i,FP_LOOT);
 if(o->inv[FP_I_PICK]<1)TRY(craft(a,o,cfg,s,FP_WOOD_PICK),FP_CRAFT);
 if(o->inv[FP_I_SWORD]<1)TRY(craft(a,o,cfg,s,FP_WOOD_SWORD),FP_CRAFT);
 if(o->inv[FP_I_SWORD]<2)TRY(craft(a,o,cfg,s,FP_STONE_SWORD),FP_CRAFT);
 if(a->level==0 && !a->achieved[3] && a->farm_attempts<40 && o->inv[FP_I_SWORD]>=2)TRY(seek_block(a,o,s,FP_GRASS,5),FP_FARM);
 if(a->level==0 && o->inv[7] && !a->achieved[7])TRY(place(a,o,s,FP_PLACE_PLANT),FP_FARM);
 if(o->inv[FP_I_PICK]<2)TRY(craft(a,o,cfg,s,FP_STONE_PICK),FP_CRAFT);
 FPFloor *f=&a->floors[a->level];int prepare=a->level==0 && f->steps<cfg->prepare_steps;
 if(o->inv[FP_I_SWORD]<3 && (prepare||a->level==2||a->level==5 || (o->inv[2] && o->inv[3])))TRY(craft(a,o,cfg,s,FP_IRON_SWORD),FP_CRAFT);
 if(o->inv[FP_I_PICK]<3 && (prepare||a->level==2||a->level==5 || (o->inv[2] && o->inv[3])))TRY(craft(a,o,cfg,s,FP_IRON_PICK),FP_CRAFT);
 if(o->inv[FP_I_SWORD]<4 && o->inv[FP_I_PICK]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_SWORD),FP_CRAFT);
 /* Shallow: Mines diamonds go to plates, not to the diamond pick. Full diamond
  * armour at the Troll Mines entry lifts P(8) .227 -> .531 (troll stream's
  * armour oracle); the Mines hold ~5 diamonds and the Troll Mines ~9 at a far
  * higher hazard. The pick (sapphires, rubies) waits for the Troll Mines. */
 if(o->inv[FP_I_PICK]<4 && o->inv[FP_I_PICK]>=3 && !(shallow(a) && wm_armour(o)<8))TRY(craft(a,o,cfg,s,FP_DIAMOND_PICK),FP_CRAFT);
 if(!a->achieved[26] && o->inv[2]>=3 && o->inv[3]>=3)TRY(craft(a,o,cfg,s,FP_IRON_ARMOUR),FP_CRAFT);
 if(wm_armour(o)<8 && o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
 /* Mine protective plates while the enemies are still weak. Diamonds
  * already exposed in the Gnomish Mines otherwise remain unused until
  * the much more dangerous Troll Mines expedition. */
 if(a->level==2 && o->inv[FP_I_PICK]>=(shallow(a)?3:4) && o->inv[FP_I_SWORD]>=4 && wm_armour(o)<8)
  TRY(seek_block(a,o,s,FP_DIAMOND,100),FP_GATHER);
 /* Shallow: count empty slots, not armour value; a diamond plate (2) and two
  * iron pieces reach 4 and left the fourth slot empty. */
 int empty_slot=0;for(int i=0;i<4;i++)empty_slot+=o->armour[i]==0;
 if((shallow(a)?empty_slot>0:wm_armour(o)<cfg->armour_pieces) && (prepare||a->level==2||a->level==5))TRY(craft(a,o,cfg,s,FP_IRON_ARMOUR),FP_CRAFT);
 /* The rule above counts armour value, so a diamond plate hides an empty slot:
  * 11% of Fire entrants have one, most of them carrying the 3 iron and 3 coal
  * a piece needs (P(8 | Fire arrival) rises ~5pp per armour point). */
 if(TB(14) && troll_phase(a) && a->level>=3 && a->level<=5) {
  int empty=0;for(int i=0;i<4;i++)empty+=o->armour[i]==0;
  if(empty && (a->level==5 || (o->inv[2]>=3 && o->inv[3]>=3)))TRY(craft(a,o,cfg,s,FP_IRON_ARMOUR),FP_CRAFT);
 }
 if(a->level==0 && o->inv[0]<cfg->wood_stock)TRY(seek_block(a,o,s,FP_TREE,100),FP_GATHER);
 /* Shallow wood trips: walk to farther known trees, then search unknown
  * Overworld terrain for more. Giving up after 600 steps with no tree in
  * memory left the mines without torches for good: every traced Mines
  * horizon run (arrive2 ep 120, 510, 568, ...) had no wood, no torches and
  * this flag set, and shuttled for water until step 100,000. */
 if(shallow(a) && a->level==0 && a->resupplying && o->inv[0]<cfg->wood_stock) {
  TRY(seek_block(a,o,s,FP_TREE,400),FP_GATHER);
  if(a->shallow_search<6000){a->shallow_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);}
 }
 /* Overworld stone is the only shelter and arrow material the Dungeon has. */
 if(o->inv[1]<(a->level==0?(a->floors[1].seen && early(a)?30:10):3) && o->inv[FP_I_PICK]>0)TRY(seek_block(a,o,s,FP_STONE,20),FP_GATHER);
 /* Wood is the mines' scarcest material (no trees below the Overworld): a
  * table takes 2, a diamond pickaxe 1. 38% of the iron-pickaxe Fire entrants
  * carry the 3 diamonds for a diamond pickaxe but no wood, all of it burnt
  * into torches here; without that pickaxe no ruby or sapphire can be mined. */
 int wood_ok=!TB(24) || !troll_phase(a) || a->level!=5 || o->inv[0]>=4 || o->inv[8]<4;
 if(wood_ok && (!a->achieved[23] || o->inv[8]<cfg->torch_stock) && (a->level==0||a->level==2||a->level==5))TRY(craft(a,o,cfg,s,FP_MAKE_TORCH),FP_CRAFT);
 /* Torches are the mines' exploration and dark Ice needs them too; coal is
  * plentiful here (38 per map). Turn spare wood into stock. */
 if(TB(18) && a->level==5 && troll_phase(a) && o->inv[8]<24 && o->inv[0]>=3)TRY(craft(a,o,cfg,s,FP_MAKE_TORCH),FP_CRAFT);
 /* Arrows halve Dungeon melee damage: an arrow before an orc arrives lets
  * one iron blow kill it. Per 100 floor-1 steps agents took 0.80 melee damage
  * holding six or more arrows, 1.42 with one to five and 2.56 with none.
  * Stock them on the Overworld, where wood and stone are plentiful, and keep
  * ten below it while eight stones remain for a shelter. */
 if(!a->achieved[22] || (o->bow && o->inv[9]<6 && (!early(a) || a->level==0 || o->inv[1]>=3)) ||
    (o->bow && early(a) && a->level>=1 && a->level<=4 && o->inv[9]<10 && o->inv[1]>=8 && o->inv[0]>=6) ||
    (a->level==0 && o->bow && a->floors[1].seen && early(a) && o->inv[9]<(shallow(a) && a->floors[2].seen?20:40) && o->inv[0]>=4 && o->inv[1]>=4 &&
     (!shallow(a) || !a->floors[2].seen || known_trees(a)>=15)))TRY(craft(a,o,cfg,s,FP_MAKE_ARROW),FP_CRAFT);
 if(!a->achieved[53] && o->bow && o->inv[9])return emit(a,o,s,FP_SHOOT,FP_MAGIC);
 if(!a->achieved[10] && o->inv[1])TRY(place(a,o,s,FP_PLACE_STONE),FP_GATHER);
 if(!a->achieved[24] && o->inv[8])TRY(place(a,o,s,FP_PLACE_TORCH),FP_LIGHT);
 /* Use carried coal to replenish Ice's light without another floor trip.
  * Three wood also covers a missing crafting table. */
 if((a->level==7 || a->level==8) && o->inv[8]<6 && o->inv[2]>0 && o->inv[0]>=(a->level==8?1:3))TRY(craft(a,o,cfg,s,FP_MAKE_TORCH),FP_CRAFT);
 if(a->level==8)TRY(graveyard_light(a,o,s),FP_LIGHT);
 if(a->level==2||a->level==5||a->level==7||a->level==8) {
  /* Spread permanent light only where it reveals new terrain. */
  int r=a->r+dr[o->dir],c=a->c+dc[o->dir],gain=0;
  if(inside(r,c) && o->inv[8] && place_valid(a,s,cell(r,c),FP_PLACE_TORCH)) {
   for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
    if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
   /* Coal and resupply trips limit exploration. Walk farther into existing
    * light before spending another torch on mostly overlapping terrain. */
   /* Ice needs closer torch spacing to expose its ladder before scarce
    * food forces another dangerous return through Fire. */
   /* Graveyard has no return ladder or renewable torch materials. Broader
    * placements preserve enough light to find the boss and stranded mobs. */
   /* Ice: same spacing as the lighting explorer (ICE_TORCH_GAIN); ten new
    * cells per torch exhausted coal long before the exit was found. */
   int need=a->level==8?GRAVEYARD_TORCH_GAIN:a->level==7?ICE_TORCH_GAIN:30;
   if(TB(21) && a->level==5 && troll_phase(a) && o->clear && a->floors[5].down<0)need=40;
   if(gain>=need)return emit(a,o,s,FP_PLACE_TORCH,FP_LIGHT);
  }
  /* Ice hits are mostly physical (frost troll 4, iceball 4, x(1-0.1 armour)):
   * full diamond plates at Ice arrival were worth +22pp P(8) (armour oracle).
   * Ice rock holds ~5 diamonds; mine those that lighting exposes. */
  if(a->level==7 && wm_armour(o)<8 && o->inv[FP_I_PICK]>=3)TRY(seek_block(a,o,s,FP_DIAMOND,40),FP_GATHER);
  int stock=a->level==5?4:1,rstock=a->level==2?4:stock;
  /* Icetrip: lighting the mines for diamonds also shows gems. After Ice a
   * ruby has no use (fire enchants are for the first Fire expedition) and a
   * sapphire only for a piece not yet ice-enchanted; mining them cost ~180
   * steps per run in traces of the prospecting candidate. */
  if(ICETRIP_GEMS && icetrip(a)) {
   int ice=0;for(int i=0;i<4;i++)ice+=o->armour_enchant[i]==2;
   stock=4-ice;rstock=0;
  }
  if(o->inv[FP_I_PICK]>=4 && o->inv[5]<stock)TRY(seek_block(a,o,s,FP_SAPPHIRE,40),FP_GATHER);
  /* Nearby mine rubies can protect the entire expedition at the Vault. */
  if(o->inv[FP_I_PICK]>=4 && o->inv[6]<rstock)TRY(seek_block(a,o,s,FP_RUBY,40),FP_GATHER);
  /* The mines hold the most diamonds. Each diamond plate takes 0.6 off a
   * troll hit, and two let full health absorb two hits with margin. */
  if(a->level==5 && o->inv[FP_I_SWORD]>=4 && o->inv[FP_I_PICK]>=4 && wm_armour(o)<8 && o->inv[4]<3)TRY(seek_block(a,o,s,FP_DIAMOND,40),FP_GATHER);
 }
 /* The return from ice enchanting crosses the cleared Troll Mines.
  * Gather reachable diamonds there to reduce the physical component of
  * both Frost Troll attacks and Ice projectiles before going back. */
 if(a->floors[7].seen && a->level==5 && o->clear && wm_armour(o)<8) {
  if(o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
  TRY(seek_block(a,o,s,FP_DIAMOND,60),FP_GATHER);
 }
 /* Exit search in the dark: 5% of entrants clear the mines and never find the
  * ladder (ep 27: 30 torches spent by the walking torch rule, then 22,000
  * unlit steps). After a long search, walk to the placement that lights
  * the most unseen cells, keeping a reserve for Ice. */
 if(TB(4) && a->level==5 && troll_phase(a) && o->clear && f->down<0 && f->steps>=2000 && o->inv[FP_I_TORCH]>10)
  TRY(light_unseen(a,o,s,20,600),FP_LIGHT);
 /* Diamond plates halve a troll blow (5.6 -> 3.2) and carry to Fire and Ice. */
 if(TB(5) && a->level==5 && troll_phase(a) && o->inv[FP_I_PICK]>=4 && wm_armour(o)<8 && o->health>6) {
  if(o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
  TRY(seek_block(a,o,s,FP_DIAMOND,o->clear?100:40),FP_GATHER);
 }
 /* Known gems left behind: at Fire arrival agents still remember 1.35 unmined
  * floor-5 sapphires and 0.45 diamonds (the gathering radius is 40 steps),
  * while P(8 | Fire arrival) is .23-.29 with 0-2 sapphires and .41-.45 with
  * 3-5. Fetch remembered ones on the cleared floor before leaving it. */
 if(TB(10) && a->level==5 && troll_phase(a) && o->clear && o->inv[FP_I_PICK]>=4 && o->health>6 && !wm_melee_within(a,4) && o->food>2 && o->drink>2) {
  if(wm_armour(o)<8 && o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
  if(o->inv[5]<4)TRY(seek_block(a,o,s,FP_SAPPHIRE,150),FP_GATHER);
  if(wm_armour(o)<8)TRY(seek_block(a,o,s,FP_DIAMOND,150),FP_GATHER);
 }
 /* Diamonds sit in rock that only torches reveal (at ready5 agents remember
  * 0.4 unmined of ~8.8). Light unseen rock while plates are missing. */
 if(TB(7) && a->level==5 && troll_phase(a) && wm_armour(o)<8 && o->inv[FP_I_PICK]>=3 && !troll_low && !wm_melee_within(a,4) &&
    o->food>3 && o->drink>3 && o->inv[FP_I_TORCH]>12 && f->steps<6000)
  TRY(light_unseen(a,o,s,20,400),FP_LIGHT);
 /* Gems sit in rock that only torches reveal: at ready5 agents remember 0.4
  * diamonds, 0.7 rubies and 0.9 sapphires of ~26, and gems are causal (oracles:
  * +3 diamonds at arrival +4.5pp P(8), +6 diamonds +13.8pp, +3 sapphires at Fire
  * arrival +6pp). Light unseen rock on the cleared floor while plates,
  * sapphires or rubies are missing, within the ruby search's 4,000 steps. */
 if(TB(6) && a->level==5 && troll_phase(a) && o->clear && o->inv[FP_I_PICK]>=4 && o->health>6 && f->steps<4000 &&
    o->inv[FP_I_TORCH]>12 && o->food>3 && o->drink>3 && !wm_melee_within(a,4)) {
  int fe=0;for(int i=0;i<4;i++)fe+=o->armour_enchant[i]==1;
  if(wm_armour(o)<8 || o->inv[5]<4 || o->inv[6]<4-fe) {
   if(o->inv[5]<4)TRY(seek_block(a,o,s,FP_SAPPHIRE,100),FP_GATHER);
   if(o->inv[6]<4-fe)TRY(seek_block(a,o,s,FP_RUBY,100),FP_GATHER);
   if(wm_armour(o)<8)TRY(seek_block(a,o,s,FP_DIAMOND,100),FP_GATHER);
   TRY(light_unseen(a,o,s,20,400),FP_LIGHT);
  }
 }
 /* Use partial ruby stocks before entering Fire. Waiting for all four
  * gems and perfect health often leaves useful protection unapplied.
  * A bounded search gives incomplete expeditions time to find more gems
  * without indefinitely delaying a floor whose generation lacks them. */
 if(a->level==5 && o->clear && !a->floors[7].seen && wm_armour(o)) {
  int fire=0;for(int i=0;i<4;i++)fire+=o->armour_enchant[i]==1;
  if(fire<4) {
   /* Enchant trips cost 272 steps and 2% deaths each (1.5 per entrant): a
    * trip per ruby found. Carry the rubies the plates need in one trip, unless
    * the search budget is spent or the exit is known with two in hand. */
   int carry=!TB(20) || !troll_phase(a) || o->inv[FP_I_RUBY]>=4-fire || f->steps>=4000 || (f->down>=0 && o->inv[FP_I_RUBY]>=2);
   if(carry && o->inv[FP_I_RUBY]>0 && o->health>6 && vault_trip_ok(a,o) && troll_mana_ok(a,o))TRY(travel(a,o,s,0),FP_TRAVEL);
   /* Waiting for mana must not hand the rubies to the descent rule below. */
   if(carry && o->inv[FP_I_RUBY]>0 && o->health>6 && vault_trip_ok(a,o) && !troll_mana_ok(a,o) && !wm_melee_within(a,6))return emit(a,o,s,FP_NOOP,FP_MAGIC);
   /* Once Fire is open its rock holds more rubies than the mines. (The
    * local `fire` here counts enchanted pieces: test the phase itself.) */
   if(o->inv[FP_I_RUBY]<4-fire && f->steps<4000 && !fire_phase(a) && !(TB(1) && troll_phase(a) && f->down>=0)) {
    TRY(seek_block(a,o,s,FP_RUBY,100),FP_GATHER);
    TRY(troll_explore(a,o,cfg,s),FP_EXPLORE);
   }
  }
 }
 TRY(seek_block(a,o,s,FP_CHEST,25),FP_LOOT);
 if(o->mana>=9) {
  if(o->inv[5] && o->sword_enchant!=2 && !a->floors[7].seen)TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_SWORD,60),FP_MAGIC);
  if(o->inv[6] && !o->sword_enchant)TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_SWORD,60),FP_MAGIC);
  int ice=0,fire=0;for(int i=0;i<4;i++){ice+=o->armour_enchant[i]==2;fire+=o->armour_enchant[i]==1;}
  if(wm_armour(o)) {
   if(o->inv[5] && (!a->achieved[64] || ((a->level==7 || (a->level==3 && a->floors[7].seen)) && ice<4)))TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_ARMOUR,60),FP_MAGIC);
   if(o->inv[6] && (!a->achieved[64] || ((a->level==4 || a->level==6) && fire<4 && !a->floors[7].seen)))TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_ARMOUR,60),FP_MAGIC);
  }
 }
 /* The Vault has fire tables; the Troll Mines supply their rubies. Return
  * with enough gems to protect all four pieces before the Fire expedition.
  * Clear the mines and restore health first so the return trip is survivable.
  * A brief retreat on first Fire arrival also covers gems found en route. */
 int fire_protection=0;for(int i=0;i<4;i++)fire_protection+=o->armour_enchant[i]==1;
 if(wm_armour(o) && a->floors[5].seen && !a->floors[7].seen && fire_protection<4) {
  /* Every Vault holds one fire table, yet 182 of 5,725 Fire entrants
   * carried four rubies unenchanted after 600 search steps; their blows
   * cost 6.8 health instead of 2.8. Search again, by frontiers (with two
   * or more rubies, up to 1,800 steps). A table seen through the walls may
   * have no known route: the seek failed, the ready rule descended and the
   * mines rule climbed straight back (12,000 bounces, ep 3628). When neither
   * the table nor a frontier is reachable, mark the table unusable. */
  if(fire && a->level==4 && o->inv[6] && a->vault_search<VAULT_UNUSABLE) {
   int go=fire_table_known(a)?seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_ARMOUR,400):-1;
   if(go>=0) {
    if(o->mana>=9)return emit(a,o,s,go,FP_MAGIC);
    TRY(recover(a,o,s),FP_RECOVER);
   } else {
    if(a->vault_search<vault_limit(a,o)){a->vault_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);}
    a->vault_search=VAULT_UNUSABLE;
   }
  }
  if(a->level==6 && a->floors[6].steps<3)TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==5 && o->clear && o->health>=8+o->str-0.01f && o->inv[6]>=4-fire_protection && vault_trip_ok(a,o) && troll_mana_ok(a,o))TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==4 && o->inv[6]) {
   /* Search the cleared Vault for its single fire table instead of
    * descending at once and repeating the climb from the mines. */
   if(!fire_table_known(a) && a->vault_search<vault_limit(a,o)) {
    a->vault_search++;
    /* With no frontier left the search is over: end it, or the floor-5
     * trip rule climbs straight back (4<->5 bounces). */
    if(troll_phase(a)){TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);}
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
   if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_ARMOUR,troll_phase(a) || fire?400:100),FP_MAGIC);
   /* Known but unreachable table: find a route within the search budget. */
   if(troll_phase(a) && fire_table_known(a) && o->mana>=9 && a->vault_search<vault_limit(a,o)) {
    a->vault_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);
   }
   if(o->mana<9)TRY(recover(a,o,s),FP_RECOVER);
  }
 }
 int ready=a->level==0?(o->inv[FP_I_SWORD]>=3 && o->inv[FP_I_PICK]>=3 && o->inv[0]>=cfg->wood_stock):
   (a->level==2||a->level==5)?(o->inv[FP_I_SWORD]>=4 && o->inv[FP_I_PICK]>=4 && wm_armour(o)>=2):1;
 if(a->level==0 && f->steps>=cfg->prepare_steps)ready=1;
 if((a->level==2||a->level==5) && f->steps>=cfg->mine_steps)ready=1;
 /* A diamond sword kills Sewer lizards and kobolds in one strike at strength
  * 3; an iron one needs two, and each lizard bites first. Agents leaving the
  * mines with iron swords passed the Sewers 0.66 of the time, with diamond
  * ones 0.91. Most timed-out descents held one diamond, with three more left
  * in rock that no torch had lit. Light unseen rock before the timed descent,
  * keeping torches for the Troll Mines. */
 if(a->level==2 && o->clear && (o->inv[FP_I_SWORD]<4 || wm_armour(o)<(shallow(a)?8:6)) && o->inv[FP_I_PICK]>=3 && o->inv[0]>=1 &&
    o->inv[FP_I_TORCH]>(shallow(a)?2:6) && f->steps<(shallow(a)?7000:5*cfg->mine_steps))TRY(light_unseen(a,o,s,20,1500),FP_LIGHT);
 /* With the open cave lit, look inside the rock for a diamond sword, or
  * for iron while the iron armour is incomplete. */
 if(shallow(a) && a->level==2 && o->clear && (o->inv[FP_I_SWORD]<4 || wm_armour(o)<8) && o->inv[FP_I_PICK]>=3 &&
    o->inv[0]>=1 && o->inv[FP_I_TORCH]>2 && f->steps<7000)TRY(dark_probe(a,o,s,25,400),FP_LIGHT);
 /* A Fire visit lasts as long as the drink, energy and health carried
  * down. Top them up beside the Troll Mines water before each descent. */
 /* Prospecting. Full diamond armour at Troll Mines entry raised P(8) .23 ->
  * .53 (troll stream oracle) and +3 diamonds at Fire arrival +7.5pp. Fire
  * entrants hold 1.2 diamonds and armour 4.7 while the Troll Mines keep ~5.4
  * diamonds and ~6.4 sapphires unseen in the dark (0.45 and 1.35 known; ~2
  * of each within 40 steps of the down ladder). About one rock cell in a
  * hundred is a diamond, another a sapphire: a torch on new rock reveals ~0.3
  * of each. On the cleared mines, healthy and supplied, turn three diamonds
  * into a plate, mine known diamonds and sapphires (to four, the ice pieces),
  * keep 20 torches from the coal and Fire wood carried (dark Ice needs them),
  * and light unseen rock (bounded budget). */
 if(fire && a->level==5 && o->clear && (wm_armour(o)<8 || o->inv[FP_I_SAPPHIRE]<4) && o->inv[FP_I_PICK]>=3 && a->fire_dia<3500 &&
    o->health>=8+o->str-1.01f && o->drink>=5 && o->food>=4 && o->energy>=4 && !wm_melee_within(a,6)) {
  int sk=FP_CRAFT;act=-1;
  if(o->inv[FP_I_DIAMOND]>=3 && wm_armour(o)<8)act=craft_radius(a,o,cfg,s,FP_DIAMOND_ARMOUR,250);
  if(act<0){sk=FP_GATHER;if(wm_armour(o)<8)act=seek_block(a,o,s,FP_DIAMOND,100);}
  if(act<0 && o->inv[FP_I_SAPPHIRE]<4 && o->inv[FP_I_PICK]>=4)act=seek_block(a,o,s,FP_SAPPHIRE,100);
  if(act<0){sk=FP_CRAFT;if(o->inv[FP_I_TORCH]<20 && o->inv[FP_I_COAL]>=1 && o->inv[FP_I_WOOD]>=1)act=craft_radius(a,o,cfg,s,FP_MAKE_TORCH,250);}
  if(act<0){sk=FP_LIGHT;if(o->inv[FP_I_TORCH]>8)act=light_unseen(a,o,s,20,600);}
  if(act>=0){a->fire_dia++;return emit(a,o,s,act,sk);}
  /* Nothing left to light or mine within reach: the gem trip is over. */
  if(prep==1)prep=a->fire_prep=2;
 }
 if(fire && a->level==5 && o->clear && ready && !a->resupplying) {
  /* Margins: drink and energy also fall on the walk back to the ladder,
   * and a full-drink rule made agents with water 20 steps away shuttle
   * between the two forever (ep 16). The drinking flag fills to the top. */
  if(o->drink<=7+2*o->dex-3){a->drinking=1;TRY(seek_water(a,o,s),FP_DRINK);}
  if(o->energy<6 || o->health<8+o->str-1.01f){a->recovering=1;TRY(recover(a,o,s),FP_RECOVER);}
  /* The last descent goes straight to Ice, which has no food. */
  if(prep==2 && o->food<=5)TRY(hunt(a,o,s,2,20),FP_EAT);
 }
 /* A supply visit from the mines leaves full: the next floor-5 stint has no
  * reliable food. Floors 3 and 4 are lit, so their animals are visible. */
 if(TB(2) && troll_phase(a) && (a->level==3 || a->level==4) && a->t-f->entered<400 && o->health>3) {
  if(o->drink<7+2*o->dex-2){a->drinking=1;TRY(seek_water(a,o,s),FP_DRINK);}
  if(o->food<7+2*o->dex-2)TRY(hunt(a,o,s,2,30),FP_EAT);
  if(o->food<=4)TRY(explore(a,o,cfg,s),FP_EXPLORE);
 }
 /* Shallow: leave the Overworld fed, watered and rested; a Dungeon or Mines
  * visit ends when the first of the three runs low. The Dungeon sends agents
  * up at energy 5 and awake energy falls a point per 31 steps, so descents at
  * 7-8 gave Dungeon visits of 30-100 steps: arrive1 ep 101 made 60 round trips
  * and spent 17,000 steps on the Overworld without finding the exit. Sleep
  * last (a point per 11 steps, waking at full energy), then go. */
 /* Once the check passes, walk to the ladder without re-checking: drink,
  * food and energy all fall on the way, and re-checking them kept one agent
  * cycling between water, cows, sleep and the ladder for 18,000 steps. */
 if(shallow(a) && a->level==0 && a->floors[1].seen && o->clear && ready && !a->resupplying &&
    !(a->shallow_go && a->t-a->shallow_go<200)) {
  if(o->drink<=6){a->drinking=1;TRY(seek_water(a,o,s),FP_DRINK);}
  if(o->food<=5){TRY(forage(a,o,s),FP_EAT);TRY(hunt(a,o,s,2,40),FP_EAT);TRY(explore(a,o,cfg,s),FP_EAT);}
  if(o->energy<=7){a->recovering=1;TRY(recover(a,o,s),FP_RECOVER);}
  a->shallow_go=a->t+1;
 }
 if(a->level<8 && o->clear && ready && !a->resupplying && o->health>cfg->retreat_health && !(prep==1 && a->level==5))TRY(travel(a,o,s,1),FP_TRAVEL);
 if(a->level>0 && a->level<8 && (o->food==0 || o->drink==0))TRY(travel(a,o,s,0),FP_TRAVEL);
 /* A cleared mine only needs its ladder. When ordinary exploration has not
  * found it in the time that 95% of descents need, search the small unlit
  * pockets. Searching them earlier spent every torch near the entrance and
  * left the rest of the floor dark for good. */
 if(a->level==2 && o->clear && f->down<0 && f->steps>=4000)TRY(light_unseen(a,o,s,8,600),FP_LIGHT);
 /* The ladder can sit on one unlit cell inside a fully explored cave: in
  * 251 of 13,724 Mines entries (bank D) the agent shuttled between floors 1
  * and 2 for 100,000 steps; at arrive2 ep 63 and 192 the down-ladder cell was
  * the only unknown cell among known path neighbours. No placement lit eight
  * unseen cells any more. Light single unknown cells, farther away too. */
 if(a->level==2 && !a->floors[5].seen && o->clear && f->down<0 && f->steps>=3000)TRY(light_unseen(a,o,s,1,2500),FP_LIGHT);
 /* The exit may lie beyond rock: probe it once every lit pocket is seen. */
 if(shallow(a) && a->level==2 && o->clear && f->down<0 && f->steps>=3000)TRY(dark_probe(a,o,s,10,2500),FP_LIGHT);
 /* Clear Fire from its up ladder. Pigmen within ten cells come to us, and
  * clearance hunts 15-35 cells out ended in 4-6 hits taken on the run back
  * (25 traced deaths of fully fire-enchanted agents). On the ladder cell a
  * low fight ends with one ascend (see above). Where nothing approaches
  * (a ladder in a lava or rock pocket), fall back to hunting after 80 steps. */
 if(fire && a->level==6 && !o->clear && a->floors[6].up>=0 && s->dist[a->floors[6].up]<INF) {
  int near=0,up=a->floors[6].up;
  for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)<10)near=1;}
  if(near)a->fire_camp=0;else a->fire_camp++;
  if(a->fire_camp<80) {
   if(up!=cell(a->r,a->c))TRY(move_path(a,o,s,up),FP_FIGHT);
   return emit(a,o,s,FP_NOOP,FP_FIGHT);
  }
 }
 /* On Fire a bat hunt detours into pigman reach (ep 1): hunt there only
  * when food is needed, or for a bat close by. */
 int bats=fire && a->level==6?(o->food<=6?15:4):15;
 if(bats && o->food<7+2*o->dex && o->inv[FP_I_SWORD]>=2)TRY(hunt(a,o,s,2,bats),FP_EAT);
 /* Ice is pitch dark (default light 0): only torch-lit cells are ever
  * observed and moves are planned onto known cells only, so torches are
  * the exploration itself. Frontier walking with a torch whenever ten new
  * cells would light spent 24-94 torches per exit found, one every ~30 Ice
  * steps, and Ice time is lethal at a near-constant rate. Walk to the
  * reachable placement that lights the most unseen cells instead. Before
  * clearance the same rule runs below, after hunts within 30 steps: placed
  * here it pre-empted them and clearance fell .569 -> .533. */
 if(a->level==7 && o->clear && a->floors[7].down<0 && o->inv[FP_I_TORCH])TRY(light_unseen(a,o,s,ICE_TORCH_GAIN,1200),FP_LIGHT);
 /* Once an elemental realm is clear, uncover its exit. Extra hunts spend
  * the food and health needed to reach the next floor. */
 /* Fire rock is 2.5% ruby; a known Vault table turns each into one fewer
  * point of fire damage per blow and fireball. Mine those close by. */
 if(fire && a->level==6 && wm_armour(o) && o->inv[FP_I_PICK]>=4 && vault_trip_ok(a,o)) {
  int fp=0;for(int i=0;i<4;i++)fp+=o->armour_enchant[i]==1;
  if(o->inv[FP_I_RUBY]<4-fp)TRY(seek_block(a,o,s,FP_RUBY,10),FP_GATHER);
 }
 if((a->level==6 || a->level==7) && o->clear)TRY(explore(a,o,cfg,s),FP_EXPLORE);
 /* A cleared Dungeon only needs its ladder. Distant hunts there kept agents
  * fighting near the entrance for hundreds of steps instead of exploring;
  * approaching mobs still meet the adjacent-combat rules. */
 /* Fire elementals keep four or five cells away and walk on lava; chased,
  * they end cornered and fire point-blank. 74 of 87 sampled fireball hits
  * came from one or two cells. Clear Fire on the pigmen that come to us. */
 int prey=fire && a->level==6?1:5;
 /* Troll Mines: deep things come only to a pool's edge, and walking up to
  * strike one took 3.1 slimeball hits per 1,000 steps (the highest rate of any
  * activity). Trolls come to us; on the cleared floor hunts only cost time. */
 if(TB(11) && a->level==5 && troll_phase(a))prey=o->clear?0:1;
 if(prey && o->inv[FP_I_SWORD]>=2 && (a->level>0 || !a->achieved[8] || !a->achieved[12]))TRY(hunt(a,o,s,prey,a->level==1 && o->clear?3:30),FP_FIGHT);
 if(a->level==0 && !a->achieved[11])TRY(seek_block(a,o,s,FP_RIPE,70),FP_EAT);
 if(a->level==7 && a->floors[7].down<0 && o->inv[FP_I_TORCH])TRY(light_unseen(a,o,s,ICE_TORCH_GAIN,1200),FP_LIGHT);
 /* The general explorer scores only cells within 60 steps, and with those
  * all seen it wanders among known rooms: one Dungeon run covered the same
  * ~380 cells for 16,000 steps without finding the exit (arrive1 ep 3).
  * Walk to stands that still show unseen cells, at any distance (the Fire
  * stream's frontier walker). */
 if(shallow(a) && a->level==1 && a->floors[1].down<0)TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);
 TRY(troll_explore(a,o,cfg,s),FP_EXPLORE);
 return emit(a,o,s,FP_NOOP,FP_EXPLORE);
#undef TRY
}
const char *fp_skill_name(int skill) {
 static const char *names[FP_SKILL_COUNT]={"explore","gather","craft","drink","eat","recover","fight","farm","light","travel","loot","magic","upgrade"};
 return skill>=0 && skill<FP_SKILL_COUNT?names[skill]:"unknown";
}

#include "full_goals.inc"

void fp_observe(FPAgent *a,const FPObservation *o,float reward,FPScratch *s) {
 /* Policy memory that does not survive a floor change. */
 if(wm_observe(a,o,reward,s)){a->last_frontier=a->recovery_goal=a->water_goal=-1;a->recovering=0;}
 runtime_observe(a,o);
 sg_boundary(a,o);
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
