/* Explicit goal hierarchy for the Full controller (see the paper, Section 2).
 *
 * A controlled HHMM over public variables: goal G at each level, a boundary D
 * decided after each new observation, a call stack, and an outcome for every
 * invocation. Phase A (round 15): the goals are defined here as deterministic
 * functions of controller memory (FPAgent) and observation, so the harness can
 * label ANY policy without changing a single action; the controller does not yet
 * select goals through this module (that is phase B, where the controller keeps an
 * HGState in its own memory and its selector mu picks children at boundaries).
 *
 *   depth 0  ADVANCE(k)          advance the deepest frontier from k to k+1
 *   depth 1  CLEAR | FIND_EXIT | DEPART   (on the frontier floor k)
 *            EXCURSION(purpose)  (above the frontier: resupply, enchant, escape...)
 *   depth 2+ HEAL                (the controller's persistent recovery goal)
 *            FIGHT               (an engagement: fight actions until 6 quiet steps)
 *
 * FIGHT nests under HEAL when both are active. Outcomes: completed (the goal's
 * public predicate holds), suspended (a floor phase left upward by an excursion,
 * resumed later as a new invocation), abandoned (left before its predicate held),
 * interrupted (closed because its parent closed), died, horizon, stopped (the
 * evaluation ended, e.g. at floor 8, without the goal's predicate deciding).
 *
 * Engine quantities (damage taken, kills) are accepted only through HGEngine, which
 * the evaluator fills and a policy would pass as NULL: they are logged, never used
 * to decide a boundary. */
#ifndef FP_HHMM_H
#define FP_HHMM_H
#include "full_policy.h"
#include <stdlib.h>
#include <string.h>

enum { HG_ADVANCE, HG_CLEAR, HG_FIND_EXIT, HG_DEPART, HG_EXCURSION, HG_HEAL, HG_FIGHT, HG_GOALS };
enum { HG_OPEN, HG_COMPLETED, HG_SUSPENDED, HG_ABANDONED, HG_INTERRUPTED, HG_DIED, HG_HORIZON, HG_STOPPED };
/* Excursion purposes, decided from public state on its first step (priority order). */
enum { HGP_ESCAPE, HGP_ENCHANT, HGP_FOOD, HGP_WATER, HGP_SLEEP, HGP_HEAL, HGP_WOOD, HGP_OTHER, HGP_COUNT };
enum { HG_DEPTH=4, HG_FIGHT_QUIET=6 };

static const char *const hg_goal_names[HG_GOALS]={"advance","clear","find_exit","depart","excursion","heal","fight"};
static const char *const hg_outcome_names[]={"open","completed","suspended","abandoned","interrupted","died","horizon","stopped"};
static const char *const hg_purpose_names[HGP_COUNT]={"escape","enchant","food","water","sleep","heal","wood","other"};

typedef struct { int level,food,drink,energy,mana,armour,ice,fire,torch,sapphire,ruby,diamond;float health; } HGFeat;
typedef struct { float dmg[3];int kills;float health; } HGEngine;
typedef struct {
 int goal,arg,id,parent,entry_t,resumed,top,kills0;
 HGFeat in;float dmg[3];
} HGFrame;
typedef struct {
 int id,parent,depth,goal,arg,frontier,entry_t,exit_t,outcome,resumed,top,kills;
 HGFeat in,out;float dmg[3];
} HGRecord;
typedef struct {
 HGFrame f[HG_DEPTH];int depth,next_id,frontier;
 int fight_quiet,in_fight,prev_level,prev_skill,prev_near,prev_recovering;float prev_health;
 /* Onsets at the last step: an engagement or a recovery that began from inactive
  * (re-pushes caused by a parent change are not onsets). Goal-entry banks use these. */
 int fight_onset,heal_onset;
 HGFeat last;
} HGState;

static inline HGFeat hg_feat(const FPAgent *a,const FPObservation *o) {
 HGFeat x;memset(&x,0,sizeof x);(void)a;
 x.level=o->level;x.health=o->health;x.food=o->food;x.drink=o->drink;x.energy=o->energy;x.mana=o->mana;
 for(int i=0;i<4;i++){x.armour+=o->armour[i];x.ice+=o->armour_enchant[i]==2;x.fire+=o->armour_enchant[i]==1;}
 x.torch=o->inv[FP_I_TORCH];x.sapphire=o->inv[FP_I_SAPPHIRE];x.ruby=o->inv[FP_I_RUBY];x.diamond=o->inv[FP_I_DIAMOND];
 return x;
}
/* Deepest floor reached: memory, or the observation itself before memory has absorbed it. */
static inline int hg_frontier(const FPAgent *a,const FPObservation *o) {
 int k=0;for(int i=0;i<FP_LEVELS;i++)if(a->floors[i].seen)k=i;
 if(a->level>k)k=a->level;
 return o->level>k?o->level:k;
}
/* A melee enemy remembered within Manhattan 4 of the player (controller memory). */
static inline int hg_near(const FPAgent *a) {
 for(int i=0;i<a->enemy_count;i++){const FPEnemy *e=&a->enemies[i];
  if(!e->kind && abs(e->p/FP_SIDE-a->r)+abs(e->p%FP_SIDE-a->c)<=4)return 1;}
 return 0;
}
static inline int hg_phase(const FPAgent *a,const FPObservation *o,int k) {
 if(!o->clear)return HG_CLEAR;
 return a->floors[k].down>=0?HG_DEPART:HG_FIND_EXIT;
}
static inline int hg_purpose(const HGState *g,const FPAgent *a,const FPObservation *o) {
 HGFeat x=hg_feat(a,o);int maxh=8+o->str;
 if(g->prev_skill==FP_FIGHT || (g->prev_health<=maxh/2.0f && g->prev_near))return HGP_ESCAPE;
 if(x.armour && ((a->floors[7].seen && x.sapphire && x.ice<4) ||
    (a->floors[5].seen && !a->floors[7].seen && ((x.ruby && x.fire<4) || (!o->sword_enchant && (x.ruby || x.sapphire))))))return HGP_ENCHANT;
 if(o->food<=3)return HGP_FOOD;
 if(o->drink<=3 || a->drinking)return HGP_WATER;
 if(o->energy<=3)return HGP_SLEEP;
 if(o->health<maxh-1.01f)return HGP_HEAL;
 if(a->resupplying)return HGP_WOOD;
 return HGP_OTHER;
}

typedef void (*hg_emit_fn)(void *ctx,const HGRecord *r);

static inline void hg_pop(HGState *g,int t,int outcome,const HGFeat *out,const HGEngine *e,hg_emit_fn emit,void *ctx) {
 HGFrame *f=&g->f[--g->depth];
 if(emit) {
  HGRecord r;memset(&r,0,sizeof r);
  r.id=f->id;r.parent=f->parent;r.depth=g->depth;r.goal=f->goal;r.arg=f->arg;r.frontier=g->frontier;
  r.entry_t=f->entry_t;r.exit_t=t;r.outcome=outcome;r.resumed=f->resumed;r.top=f->top;
  r.kills=e?e->kills-f->kills0:0;r.in=f->in;r.out=*out;memcpy(r.dmg,f->dmg,sizeof r.dmg);
  emit(ctx,&r);
 }
}
static inline void hg_push(HGState *g,int goal,int arg,int t,int resumed,const HGFeat *in,const HGEngine *e) {
 HGFrame *f=&g->f[g->depth];memset(f,0,sizeof *f);
 f->goal=goal;f->arg=arg;f->id=g->next_id++;f->parent=g->depth?g->f[g->depth-1].id:-1;f->entry_t=t;f->resumed=resumed;
 f->top=in->level;f->kills0=e?e->kills:0;f->in=*in;g->depth++;
}

/* Desired stack for this step: goals and args. Returns its depth. */
static inline int hg_desired(HGState *g,const FPAgent *a,const FPObservation *o,int k,int *goal,int *arg) {
 int n=0;
 goal[n]=HG_ADVANCE;arg[n++]=k;
 if(o->level<k){goal[n]=HG_EXCURSION;arg[n++]=-1;}
 else {goal[n]=hg_phase(a,o,k);arg[n++]=k;}
 if(a->recovering){goal[n]=HG_HEAL;arg[n++]=o->level;}
 if(g->in_fight){goal[n]=HG_FIGHT;arg[n++]=o->level;}
 return n;
}

/* Outcome of a frame closed while the episode continues. */
static inline int hg_outcome(const HGFrame *f,int parent_closed,int new_goal,const FPObservation *o,int k_old,int k_new) {
 switch(f->goal) {
 case HG_ADVANCE: return k_new>k_old?HG_COMPLETED:HG_ABANDONED;
 case HG_CLEAR: return o->clear && o->level==f->arg?HG_COMPLETED:(new_goal==HG_EXCURSION?HG_SUSPENDED:HG_ABANDONED);
 case HG_FIND_EXIT: return (new_goal==HG_DEPART || k_new>k_old)?HG_COMPLETED:(new_goal==HG_EXCURSION?HG_SUSPENDED:HG_ABANDONED);
 case HG_DEPART: return k_new>k_old?HG_COMPLETED:(new_goal==HG_EXCURSION?HG_SUSPENDED:HG_ABANDONED);
 case HG_EXCURSION: return o->level>=k_old?HG_COMPLETED:HG_ABANDONED;
 case HG_HEAL: if(o->health>=8+o->str-0.01f && o->energy>=7+2*o->dex)return HG_COMPLETED;
  return parent_closed?HG_INTERRUPTED:HG_ABANDONED;
 case HG_FIGHT: return parent_closed?HG_INTERRUPTED:HG_COMPLETED;
 }
 return HG_ABANDONED;
}

static inline void hg_reset(HGState *g) { memset(g,0,sizeof *g);g->prev_level=-1;g->prev_skill=-1; }

/* Cold start from controller memory (fresh episode or resumed snapshot), before the
 * first action: every open invocation is marked resumed. */
static inline void hg_start(HGState *g,const FPAgent *a,const FPObservation *o,int t,const HGEngine *e) {
 hg_reset(g);g->frontier=hg_frontier(a,o);
 g->in_fight=a->t>0 && a->skill==FP_FIGHT;g->prev_level=o->level;g->prev_skill=a->t>0?a->skill:-1;
 g->prev_health=o->health;g->prev_near=hg_near(a);g->prev_recovering=a->recovering;
 int goal[HG_DEPTH],arg[HG_DEPTH],n=hg_desired(g,a,o,g->frontier,goal,arg);
 HGFeat x=hg_feat(a,o);
 for(int i=0;i<n;i++) {
  hg_push(g,goal[i],arg[i],t,a->t>0,&x,e);
  if(goal[i]==HG_EXCURSION)g->f[i].arg=hg_purpose(g,a,o);
 }
 g->last=x;
}

/* Post-decision update at step t: memory a has absorbed observation o and chosen
 * `action` (a->skill is its skill label). Boundaries are decided here; closed
 * invocations go to emit. e (may be NULL) adds evaluator-only accounting for the
 * damage and kills of the step that FOLLOWS (pass it again next step). */
static inline void hg_step(HGState *g,const FPAgent *a,const FPObservation *o,int t,const HGEngine *e,hg_emit_fn emit,void *ctx) {
 int k_old=g->frontier,k_new=hg_frontier(a,o);
 /* Fight engagement with hysteresis; a floor change ends it. */
 if(o->level!=g->prev_level)g->in_fight=0;
 int was_fighting=g->in_fight;
 if(a->skill==FP_FIGHT){g->in_fight=1;g->fight_quiet=0;}
 else if(g->in_fight && ++g->fight_quiet>=HG_FIGHT_QUIET)g->in_fight=0;
 g->fight_onset=g->in_fight && !was_fighting;
 g->heal_onset=a->recovering && !g->prev_recovering;
 int goal[HG_DEPTH],arg[HG_DEPTH],n=hg_desired(g,a,o,k_new,goal,arg);
 int keep=0;
 while(keep<g->depth && keep<n && g->f[keep].goal==goal[keep] && (goal[keep]==HG_EXCURSION || g->f[keep].arg==arg[keep]))keep++;
 HGFeat x=hg_feat(a,o);
 for(int d=g->depth-1;d>=keep;d--) {
  int parent_closed=d>keep;
  int ng=d<n?goal[d]:-1;
  int oc=hg_outcome(&g->f[d],parent_closed,ng,o,k_old,k_new);
  hg_pop(g,t,oc,&x,e,emit,ctx);
 }
 g->frontier=k_new;
 for(int d=keep;d<n;d++) {
  hg_push(g,goal[d],arg[d],t,0,&x,e);
  if(goal[d]==HG_EXCURSION)g->f[d].arg=hg_purpose(g,a,o);
 }
 for(int d=0;d<g->depth;d++)if(g->f[d].goal==HG_EXCURSION && o->level<g->f[d].top)g->f[d].top=o->level;
 g->prev_level=o->level;g->prev_skill=a->skill;g->prev_health=o->health;g->prev_near=hg_near(a);g->prev_recovering=a->recovering;g->last=x;
}
/* Engine accounting for the step just simulated (evaluator only). */
static inline void hg_account(HGState *g,const HGEngine *e) {
 for(int d=0;d<g->depth;d++)for(int i=0;i<3;i++)g->f[d].dmg[i]+=e->dmg[i];
}
/* End of the evaluated episode: close every open invocation. reached = the floor the
 * episode ended on is beyond the frontier (e.g. stopped on arrival at floor 8). */
static inline void hg_finish(HGState *g,int t,int died,int horizon,int final_level,const HGEngine *e,hg_emit_fn emit,void *ctx) {
 HGFeat x=g->last;x.level=final_level;if(e)x.health=e->health;
 int reached=final_level>g->frontier;
 while(g->depth) {
  const HGFrame *f=&g->f[g->depth-1];int oc;
  if(died)oc=HG_DIED;
  else if(reached && (f->goal==HG_ADVANCE || f->goal==HG_DEPART || f->goal==HG_FIND_EXIT))oc=HG_COMPLETED;
  else if(reached)oc=HG_INTERRUPTED;
  else oc=horizon?HG_HORIZON:HG_STOPPED;
  hg_pop(g,t,oc,&x,e,emit,ctx);
 }
}
/* Index of the deepest open frame with this goal, or -1. */
static inline int hg_find(const HGState *g,int goal) {
 for(int d=g->depth-1;d>=0;d--)if(g->f[d].goal==goal)return d;
 return -1;
}
static inline int hg_goal_index(const char *name) {
 for(int i=0;i<HG_GOALS;i++)if(!strcmp(name,hg_goal_names[i]))return i;
 return -1;
}
#endif
