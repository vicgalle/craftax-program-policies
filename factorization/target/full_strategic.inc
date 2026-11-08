/* Strategic goal runtime (benchmarks/strategic_goals). Included just before
 * autonomous(), so goal modules may use every policy helper.
 *
 * A strategic goal targets the agent's kit, stocks or attributes (full
 * diamond armour, ice-enchanted plates, a stock of torches), not a floor
 * milestone. Execution follows the floor runtime's conventions:
 *  - a selector calls it (sg_call) when its entry predicate holds;
 *  - it persists across floors, survival interrupts and excursions; the
 *    rules above the strategic hook in autonomous() (fights, drink, food,
 *    healing, sleep) simply preempt it for a step;
 *  - after each observation, sg_boundary returns it completed or abandoned
 *    by its own predicates, or on death, horizon budget or floor 8.
 * A goal never decides by itself that it succeeded: completion is observed.
 *
 * Each module (sg_early.inc, sg_late.inc, sg_kit.inc) owns some kinds of
 * FP_SG_* and defines, for each:
 *  complete(a,o,f)  observed completion predicate
 *  abandon(a,o,f)   abandonment predicate (budget, hazard, infeasibility)
 *  act(a,o,cfg,s,f,&skill)  the goal's next action, or -1 to yield this step
 * and one selector, which may call a goal of its own kinds and returns 1 if
 * it did. Selectors run in the order of sg_selectors[] while no goal is
 * active; one strategic goal is active at a time (children may nest up to
 * FP_SG_DEPTH). With every module empty the program is the incumbent. */

typedef struct {
 const char *name;
 int (*complete)(const FPAgent *a,const FPObservation *o,const FPStratFrame *f);
 int (*abandon)(const FPAgent *a,const FPObservation *o,const FPStratFrame *f);
 int (*act)(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,FPStratFrame *f,int *skill);
} SGKind;
typedef int (*SGSelect)(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);

static FPStratFrame *sg_top(FPAgent *a) { return a->strat.depth?&a->strat.frame[a->strat.depth-1]:NULL; }
static int sg_active(const FPAgent *a,int kind) {
 for(int i=0;i<a->strat.depth;i++)if(a->strat.frame[i].kind==kind)return 1;
 return 0;
}
/* Call a goal: target and budget (steps, 0 = none) are the goal's own. */
static int sg_call(FPAgent *a,const FPObservation *o,int kind,int target,int budget) {
 FPStrategic *g=&a->strat;
 if(kind<=FP_SG_NONE || kind>=FP_SG_KINDS || g->depth>=FP_SG_DEPTH)return 0;
 FPStratFrame *f=&g->frame[g->depth];memset(f,0,sizeof *f);
 f->kind=kind;f->id=++g->next_id;f->parent=g->depth?g->frame[g->depth-1].id:0;
 f->target=target;f->budget=budget;f->entry_t=a->t;f->entry_level=o->level;
 f->in=(FPGoalPublic){o->level,o->food,o->drink,o->energy,o->health};
 g->depth++;g->calls[kind]++;return 1;
}
static void sg_return(FPAgent *a,int outcome) {
 FPStrategic *g=&a->strat;if(!g->depth)return;
 FPStratFrame *f=&g->frame[--g->depth];
 g->steps[f->kind]+=a->t-f->entry_t;g->last=*f;g->last_outcome=outcome;g->boundary_t=a->t;
 g->completions[f->kind]+=outcome==FP_RO_COMPLETED;g->abandonments[f->kind]+=outcome!=FP_RO_COMPLETED;
}
static void sg_finish(FPAgent *a,int outcome) { while(a->strat.depth)sg_return(a,outcome); }

#include "sg_early.inc"
#include "sg_late.inc"
#include "sg_kit.inc"

/* The goal table, built by the modules' registrations at compile time:
 * read-only data instead of a lazily filled mutable static. */
struct SGTable { SGKind kind[FP_SG_KINDS]; };
static constexpr SGTable sg_table=[] {
 SGTable t{};sg_early_register(t.kind);sg_late_register(t.kind);sg_kit_register(t.kind);return t;
}();
static const SGKind *sg_kind(int kind) {
 return kind>FP_SG_NONE && kind<FP_SG_KINDS && sg_table.kind[kind].act?&sg_table.kind[kind]:NULL;
}
/* Priority among selectors while no strategic goal is active. */
static const SGSelect sg_selectors[]={sg_early_select,sg_late_select,sg_kit_select};

/* Boundary: once after each observation, innermost goal first. */
static void sg_boundary(FPAgent *a,const FPObservation *o) {
 if(o->health<=0){sg_finish(a,FP_RO_DIED);return;}
 if(o->level>=8){sg_finish(a,FP_RO_STOPPED);return;}
 while(a->strat.depth) {
  FPStratFrame *f=sg_top(a);const SGKind *k=sg_kind(f->kind);
  if(!k){sg_return(a,FP_RO_STOPPED);continue;}
  if(k->complete && k->complete(a,o,f)){sg_return(a,FP_RO_COMPLETED);continue;}
  if((f->budget>0 && a->t-f->entry_t>=f->budget) || (k->abandon && k->abandon(a,o,f))){sg_return(a,FP_RO_ABANDONED);continue;}
  break;
 }
}
/* The strategic hook in autonomous(): below survival, above the incumbent's
 * own gear, supply and descent rules. Returns an action or -1. */
static int strategic(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int *skill) {
 if(!a->strat.depth)
  for(unsigned i=0;i<sizeof sg_selectors/sizeof *sg_selectors;i++)if(sg_selectors[i](a,o,cfg,s))break;
 FPStratFrame *f=sg_top(a);if(!f)return -1;
 const SGKind *k=sg_kind(f->kind);if(!k)return -1;
 *skill=FP_EXPLORE;return k->act(a,o,cfg,s,f,skill);
}
