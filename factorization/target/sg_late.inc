/* Strategic goal module "late" (see full_strategic.inc): diamond gear from
 * the Troll Mines on. Region: a->floors[5].seen. Kinds: FP_SG_ARMOUR2 (one
 * more diamond plate), FP_SG_WEAPON2 (unused), FP_SG_PROSPECT2 (unused).
 *
 * ARMOUR2 (target: armour value + 1, i.e. one more diamond plate)
 *  entry   floor 5 seen, on floor 5, armour < 8, a diamond-capable pickaxe,
 *          healthy and supplied, no troll near, the phase's step budget
 *          left, and an opportunity: three diamonds in hand, a known diamond
 *          within reach, or (where prospecting is allowed) unseen rock and
 *          torches above the reserve kept for dark Ice.
 *  done    observed: armour value >= target.
 *  abandon budget (steps per call), the phase budget spent, left floor 5,
 *          hazard (health, food, drink, energy), or nothing left to mine or
 *          light (set by the policy when no action is found).
 *  policy  craft a plate at three diamonds (walking to any known table),
 *          mine the nearest known diamond within the mode's radius, light
 *          the placement that shows the most unseen cells near the route
 *          while torches exceed the reserve.
 * Compile-time switches select the phases where the goal is eligible. */
#ifndef SGL_TROLL          /* cleared Troll Mines, before Fire is seen */
#define SGL_TROLL 0
#endif
#ifndef SGL_TROLL_BUDGET   /* steps of plate work per troll phase */
#define SGL_TROLL_BUDGET 1500
#endif
#ifndef SGL_TROLL_RADIUS   /* known-diamond radius (steps) */
#define SGL_TROLL_RADIUS 200
#endif
#ifndef SGL_PROSPECT       /* light unseen rock for diamonds */
#define SGL_PROSPECT 1
#endif
#ifndef SGL_RESERVE        /* torches kept for Ice */
#define SGL_RESERVE 20
#endif
#ifndef SGL_PROBE_RESERVE  /* probe torch reserve in the troll phase (Fire restocks) */
#define SGL_PROBE_RESERVE 10
#endif
#ifndef SGL_RESERVE_LATE   /* probe torch reserve on the gem trip and Ice excursions */
#define SGL_RESERVE_LATE 12
#endif
#ifndef SGL_GAIN           /* light_unseen min gain / max cost */
#define SGL_GAIN 20
#endif
#ifndef SGL_COST
#define SGL_COST 600
#endif
#ifndef SGL_IRONPICK       /* also with an iron pickaxe (diamonds need only iron) */
#define SGL_IRONPICK 1
#endif
#ifndef SGL_PROBE          /* tunnel into dark rock and light it (dark_probe) */
#define SGL_PROBE 1
#endif
#ifndef SGL_PROBE_GAIN
#define SGL_PROBE_GAIN 30
#endif
#ifndef SGL_PROBE_COST
#define SGL_PROBE_COST 400
#endif
#ifndef SGL_TORCHES        /* craft torches from carried wood and coal below this */
#define SGL_TORCHES 0
#endif
#ifndef SGL_FIRE           /* the Fire->Troll gem trip's mines stay (fire phase) */
#define SGL_FIRE 1
#endif
#ifndef SGL_ICE            /* Troll Mines visits of Ice excursions */
#define SGL_ICE 1
#endif
#ifndef SGL_ICE_MIN_DIA    /* Ice-excursion visits: diamonds in hand to enter (a plate is close) */
#define SGL_ICE_MIN_DIA 0
#endif
#ifndef SGL_ICE_BUDGET2    /* visit budget when a plate is one diamond away (2 mod 3 in hand) */
#define SGL_ICE_BUDGET2 0
#endif
#ifndef SGL_ICE_BUDGET     /* plate-goal steps per mines visit (Ice excursions) */
#define SGL_ICE_BUDGET 400
#endif
#ifndef SGL_ICETRIP        /* dedicated plate trip from Ice back to the Troll Mines */
#define SGL_ICETRIP 0
#endif
#ifndef SGL_ICETRIP_MIN    /* diamonds in hand to start it (2: one short of a plate) */
#define SGL_ICETRIP_MIN 2
#endif
#ifndef SGL_ICETRIP_MAX    /* trips per run */
#define SGL_ICETRIP_MAX 1
#endif
#ifndef SGL_ICETRIP_BUDGET /* steps per trip, travel included */
#define SGL_ICETRIP_BUDGET 1500
#endif
#ifndef SGL_ICEFLOOR       /* on Ice: fetch known Ice diamonds that complete a plate */
#define SGL_ICEFLOOR 0
#endif
#ifndef SGL_ICEFLOOR_R     /* radius (steps) */
#define SGL_ICEFLOOR_R 150
#endif
#ifndef SGL_PLATE_FIRST    /* troll phase, iron pickaxe: plates before the diamond pickaxe */
#define SGL_PLATE_FIRST 0
#endif
#ifndef SGL_PF_ARMOUR      /* ... until this armour value */
#define SGL_PF_ARMOUR 6
#endif
#ifndef SGL_PROBE_MIN_DIA  /* probe only with this many diamonds in hand (2: one short of a plate) */
#define SGL_PROBE_MIN_DIA 0
#endif
#ifndef SGL_WEAPON2        /* troll phase, iron pickaxe: WEAPON2 = the diamond pickaxe */
#define SGL_WEAPON2 0
#endif
#ifndef SGL_W2_CLEAR       /* 1: only once floor 5 is clear */
#define SGL_W2_CLEAR 0
#endif
#ifndef SGL_W2_RADIUS      /* known-diamond radius (steps) */
#define SGL_W2_RADIUS 150
#endif
#ifndef SGL_W2_BUDGET      /* steps per troll phase */
#define SGL_W2_BUDGET 1500
#endif
#ifndef SGL_PLATES_ONLY    /* 1: only when a plate can be completed from what is known */
#define SGL_PLATES_ONLY 0
#endif

/* Product of the incumbent's diamond-pickaxe rule (full_policy.c, guarded
 * edit): with SGL_PLATE_FIRST, in the troll phase the iron-pickaxe agent's
 * diamonds become plates until armour SGL_PF_ARMOUR, then the pickaxe. */
static int sgl_plate_first(const FPAgent *a,const FPObservation *o) {
 return SGL_PLATE_FIRST && a->floors[5].seen && !a->floors[6].seen && wm_armour(o)<SGL_PF_ARMOUR;
}
static int sgl_pick_or_plate(const FPAgent *a,const FPObservation *o) {
 return sgl_plate_first(a,o)?FP_DIAMOND_ARMOUR:FP_DIAMOND_PICK;
}
#ifndef SGL_GEMTRIP        /* 0: incumbent Fire->Troll gem trip; 2: none (ablation) */
#define SGL_GEMTRIP 0
#endif
/* Guarded edits in full_policy.c (fire_prep, rule "Prospecting"): the
 * incumbent's gem trip and fire-phase mines prospecting run unless this says no. */
static int sgl_no_gemtrip(const FPAgent *a,const FPObservation *o) {
 (void)a;(void)o;return SGL_GEMTRIP==2;
}
#ifndef SGL_NO_TB6         /* 1: ablation of the incumbent's cleared-Troll prospecting rule TB(6) */
#define SGL_NO_TB6 0
#endif
static int sgl_no_tb6(const FPAgent *a,const FPObservation *o) { (void)a;(void)o;return SGL_NO_TB6; }
#ifndef SGL_SHELTER        /* light the rock around a Troll Mines shelter on entering it */
#define SGL_SHELTER 0
#endif
#ifndef SGL_SHELTER_GAIN   /* unseen cells the torch must reveal */
#define SGL_SHELTER_GAIN 20
#endif
#ifndef SGL_SHELTER_RESERVE
#define SGL_SHELTER_RESERVE 12
#endif
/* PROSPECT2 in shelter mode (called from the recovery branch, guarded hook in
 * full_policy.c). Healing is ~46% of pre-clear Troll Mines time and shelters
 * are walled into rock that no torch has lit. On the step the agent enters
 * its chosen room facing another room cell (it always does when it walks in
 * along the room's long axis), place a torch there: the light shows ore in
 * the surrounding rock (~0.3-0.7 diamonds per fully unseen circle) and any
 * troll approaching the room. Only with no hostile within 6, no incoming
 * damage, torches above a reserve and enough unseen cells. Logged as a
 * PROSPECT2 call (target: torch count) that completes when the torch is
 * observed placed. */
static int sgl_shelter_light(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!SGL_SHELTER || a->level!=5 || o->level!=5 || !a->floors[5].seen || o->sleeping || o->resting)return -1;
 if(o->inv[FP_I_TORCH]<=SGL_SHELTER_RESERVE || a->recovery_goal<0 || wm_hostile_within(a,6))return -1;
 int r0=a->recovery_goal/FP_SIDE,c0=a->recovery_goal%FP_SIDE,w=a->recovery_width,h=a->recovery_height;
 if(a->r<r0 || a->r>=r0+h || a->c<c0 || a->c>=c0+w)return -1;
 int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
 if(o->dir<1 || o->dir>4 || fr<r0 || fr>=r0+h || fc<c0 || fc>=c0+w || !inside(fr,fc))return -1;
 int q=cell(fr,fc);const FPFloor *f=&a->floors[5];
 if(!place_valid(a,s,q,FP_PLACE_TORCH))return -1;
 for(int rr=r0;rr<r0+h;rr++)for(int cc=c0;cc<c0+w;cc++)if(f->marks[cell(rr,cc)].item)return -1;
 int gain=0;
 for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
  if(rr*rr+cc*cc<=22 && inside(fr+rr,fc+cc) && f->map[cell(fr+rr,fc+cc)]==UNKNOWN)gain++;
 if(gain<SGL_SHELTER_GAIN || wm_incoming_damage(a,o,s,FP_NOOP)>0)return -1;
 if(!a->strat.depth && sg_call(a,o,FP_SG_PROSPECT2,o->inv[FP_I_TORCH],1))sg_top(a)->local[5]=1;
 return FP_PLACE_TORCH;
}
enum { SGL_M_TROLL=0, SGL_M_FIRE=1, SGL_M_ICE=2, SGL_M_ICEF=3 };

static int sgl_phase(const FPAgent *a) { return a->floors[7].seen?SGL_M_ICE:a->floors[6].seen?SGL_M_FIRE:SGL_M_TROLL; }
static int sgl_healthy(const FPAgent *a,const FPObservation *o) {
 return o->health>6.5f && o->food>3 && o->drink>3 && o->energy>3 && !wm_melee_within(a,6);
}
/* Known diamonds on this floor (memory). */
static int sgl_known(const FPAgent *a) {
 const FPFloor *f=&a->floors[a->level];int n=0;
 for(int p=f->known_lo;p<=f->known_hi;p++)n+=f->map[p]==FP_DIAMOND;
 return n;
}

/* Dark rock: torches light ~4.7 cells through rock, so the interior of thick
 * rock stays unseen from the cave. Mine a lit stone or coal cell whose light
 * circle is mostly unseen, from an adjacent stand (routes tunnel through
 * observed rock); light_unseen then places the torch in the new gap. As
 * dark_probe (floor 2), on any floor. */
static int sgl_probe(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost) {
 const FPFloor *f=&a->floors[a->level];int start=cell(a->r,a->c),bs=-1,bt=-1,bd=0,costbest=INF;
 if(!o->inv[FP_I_TORCH] || o->inv[FP_I_PICK]<1)return -1;
 int words=unseen_rows(f,s,f->known_lo,f->known_hi,WM_BIT(FP_STONE)|WM_BIT(FP_COAL));
 for(int p:SetBits(s->unseen.cand,words)) {
  int b=f->map[p];if(p<f->known_lo || p>f->known_hi)continue;
  if(!(b==FP_STONE || b==FP_COAL) || f->marks[p].item || mobs_of(s)[p])continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=unseen_gain(s,r,c);
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d];if(!inside(sr,sc))continue;
   int q=cell(sr,sc);if(mobs_of(s)[q] || path_dist_within(a,s,q,imin(max_cost,costbest-21+10*gain))>max_cost)continue; /* settled as far as a cheaper stand could lie */
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
/* Incumbent trips that rank above its gear rules: enchant the sword or the
 * armour at a Vault/Sewers table. The plate goal yields to them. */
static int sgl_trip_pending(const FPAgent *a,const FPObservation *o) {
 if(o->xp || o->inv[FP_I_BOOK])return 1;
 if(enchant_trip(a,o))return 1;
 if(a->floors[5].seen && !a->floors[6].seen && !o->sword_enchant && o->inv[FP_I_RUBY]>0 && vault_trip_ok(a,o))return 1;
 if(a->floors[5].seen && !a->floors[6].seen && !o->sword_enchant && o->inv[FP_I_SAPPHIRE]>0 && a->vault_search<600)return 1;
 int fp=0;for(int i=0;i<4;i++)fp+=o->armour_enchant[i]==1;
 if(!a->floors[7].seen && wm_armour(o) && fp<4 && o->inv[FP_I_RUBY]>0 && o->inv[FP_I_RUBY]>=4-fp && vault_trip_ok(a,o))return 1;
 return 0;
}
static int sgl_reserve(const FPStratFrame *f) { return f->local[0]==SGL_M_TROLL?SGL_PROBE_RESERVE:SGL_RESERVE_LATE; }
/* After a call found nothing to do, re-enter only once something changed:
 * more diamonds known or in hand, or more torches. */
static void sgl_idle(FPAgent *a,const FPObservation *o) {
 a->sgl_retry_t=a->t+100;a->sgl_pad[0]=sgl_known(a)+1;a->sgl_pad[1]=o->inv[FP_I_DIAMOND]+1;a->sgl_pad[2]=o->inv[FP_I_TORCH]+1;a->sgl_pad[3]=a->level+1;
}
static int sgl_idle_now(const FPAgent *a,const FPObservation *o) {
 if(a->t<a->sgl_retry_t)return 1;
 return a->sgl_pad[3]==a->level+1 && sgl_known(a)+1<=a->sgl_pad[0] && o->inv[FP_I_DIAMOND]+1<=a->sgl_pad[1] && o->inv[FP_I_TORCH]+1<=a->sgl_pad[2];
}
/* Budgets and hazards shared by the plate goal and its prospecting child. */
static int sgl_ice_budget(const FPObservation *o) {
 return SGL_ICE_BUDGET2 && o->inv[FP_I_DIAMOND]%3==2?SGL_ICE_BUDGET2:SGL_ICE_BUDGET;
}
static int sgl_spent_out(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 if(o->health<=5 || o->food<=2 || o->drink<=2 || o->energy<=2)return 1;
 if(f->local[0]==SGL_M_TROLL && a->sgl_spent[SGL_M_TROLL]>=SGL_TROLL_BUDGET)return 1;
 if(f->local[0]==SGL_M_FIRE && a->fire_dia>=3500)return 1;
 if(f->local[0]==SGL_M_ICE && !f->local[4] && a->sgl_spent[SGL_M_ICE]>=sgl_ice_budget(o))return 1;
 return 0;
}
/* PROSPECT2: one more diamond known on this floor (target: known count),
 * by tunnelling into dark rock and lighting it. Abandoned when the torches
 * reach the reserve, nothing is left to probe, off floor 5, or on the
 * parent's budgets and hazards. */
static int sgl_prospect_complete(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 if(f->local[5])return o->inv[FP_I_TORCH]<f->target;
 return o->level==5 && a->level==5 && sgl_known(a)>=f->target;
}
static int sgl_prospect_abandon(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 if(f->local[5])return 1; /* shelter light: one step (budget 1) */
 return f->local[1] || o->level!=5 || o->inv[FP_I_TORCH]<=sgl_reserve(f) || sgl_spent_out(a,o,f);
}
static int sgl_prospect_act(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,FPStratFrame *f,int *skill) {
 (void)cfg;
 if(f->local[5])return -1;
 if(a->level!=5 || wm_hostile_within(a,5) || o->health<=6.5f || sgl_trip_pending(a,o))return -1;
 a->sgl_spent[f->local[0]]++;if(f->local[0]==SGL_M_FIRE)a->fire_dia++;
 /* A freshly opened gap first (it lights more than any cave placement). */
 int act=light_unseen(a,o,s,SGL_PROBE_GAIN,40);
 if(act<0)act=sgl_probe(a,o,s,SGL_PROBE_GAIN,SGL_PROBE_COST);
 if(act>=0){f->local[3]++;*skill=FP_LIGHT;return act;}
 /* Nothing left to probe: this child and its plate goal end. */
 a->sgl_spent[f->local[0]]--;if(f->local[0]==SGL_M_FIRE)a->fire_dia--;
 f->local[1]=1;sgl_idle(a,o);
 if(a->strat.depth>=2 && (a->strat.frame[a->strat.depth-2].kind==FP_SG_ARMOUR2 || a->strat.frame[a->strat.depth-2].kind==FP_SG_WEAPON2))
  a->strat.frame[a->strat.depth-2].local[1]=1;
 return -1;
}
static int sgl_armour_complete(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 (void)a;return wm_armour(o)>=f->target;
}
static int sgl_armour_abandon(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 if(f->local[1])return 1;
 if(f->local[0]==SGL_M_ICEF)return o->level!=7 || o->health<=6 || o->food<=2 || o->drink<=2 || o->energy<=2;
 if(f->local[4]){if(o->level<5 || o->level>7)return 1;}
 else if(o->level!=5)return 1;
 return sgl_spent_out(a,o,f);
}
static int sgl_armour_act(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,FPStratFrame *f,int *skill) {
 if(f->local[4] && (a->level==6 || a->level==7)) {
  /* The trip: up through Fire to the mines (survival rules preempt). */
  int act=travel(a,o,s,0);if(act<0){f->local[1]=1;return -1;}
  *skill=FP_TRAVEL;return act;
 }
 if(f->local[0]==SGL_M_ICEF) {
  /* Ice: walk to known Ice diamonds that complete a plate, craft it. */
  if(a->level!=7 || wm_hostile_within(a,6) || o->health<=8 || sgl_trip_pending(a,o))return -1;
  int act=-1;
  if(o->inv[FP_I_DIAMOND]>=3){act=craft_radius(a,o,cfg,s,FP_DIAMOND_ARMOUR,3000);if(act>=0){*skill=FP_CRAFT;return act;}}
  act=seek_block(a,o,s,FP_DIAMOND,SGL_ICEFLOOR_R);if(act>=0){*skill=FP_GATHER;return act;}
  f->local[1]=1;sgl_idle(a,o);return -1;
 }
 if(a->level!=5)return -1;
 /* Yield to the incumbent while anything hostile is close: fights, heals
  * and drinking sit above this hook and the next step returns here. */
 if(wm_hostile_within(a,5) || o->health<=6.5f)return -1;
 /* XP, books and the incumbent's enchant trips go first. */
 if(sgl_trip_pending(a,o))return -1;
 a->sgl_spent[f->local[0]]++;
 if(f->local[0]==SGL_M_FIRE)a->fire_dia++;
 int act=-1;
 /* An iron pickaxe with wood: the incumbent turns three diamonds into the
  * diamond pickaxe (gems for enchantments) before plates. */
 if(o->inv[FP_I_PICK]<4 && o->inv[FP_I_DIAMOND]>=3 && o->inv[FP_I_WOOD]>=1 && !sgl_plate_first(a,o)){a->sgl_spent[f->local[0]]--;return -1;}
 if(o->inv[FP_I_DIAMOND]>=3 && wm_armour(o)<8) {
  act=craft_radius(a,o,cfg,s,FP_DIAMOND_ARMOUR,3000);
  if(act>=0){*skill=FP_CRAFT;return act;}
 }
 act=seek_block(a,o,s,FP_DIAMOND,f->local[4]?150:f->local[0]==SGL_M_ICE?50:f->local[0]==SGL_M_FIRE?100:SGL_TROLL_RADIUS);
 if(act>=0){*skill=FP_GATHER;return act;}
 /* The gem trip also wants sapphires (the incumbent's order). */
 if(f->local[0]==SGL_M_FIRE && o->inv[FP_I_SAPPHIRE]<4 && o->inv[FP_I_PICK]>=4) {
  act=seek_block(a,o,s,FP_SAPPHIRE,100);if(act>=0){*skill=FP_GATHER;return act;}
 }
 if(f->local[0]==SGL_M_FIRE && o->inv[FP_I_TORCH]<20 && o->inv[FP_I_COAL]>=1 && o->inv[FP_I_WOOD]>=1) {
  act=craft_radius(a,o,cfg,s,FP_MAKE_TORCH,250);if(act>=0){*skill=FP_CRAFT;return act;}
 }
 if(SGL_TORCHES && o->inv[FP_I_TORCH]<SGL_TORCHES && o->inv[FP_I_WOOD]>=1 && o->inv[FP_I_COAL]>=1) {
  act=craft_radius(a,o,cfg,s,FP_MAKE_TORCH,250);
  if(act>=0){*skill=FP_CRAFT;return act;}
 }
 /* Cave lighting as each phase's incumbent rule does it. */
 if(f->local[0]==SGL_M_FIRE && o->inv[FP_I_TORCH]>8) {
  act=light_unseen(a,o,s,20,600);if(act>=0){f->local[2]++;*skill=FP_LIGHT;return act;}
 }
 if(f->local[4] && o->inv[FP_I_TORCH]>=12) {
  act=light_unseen(a,o,s,ICETRIP_DIA_GAIN,600);if(act>=0){f->local[2]++;*skill=FP_LIGHT;return act;}
 }
 if(f->local[0]==SGL_M_ICE && !f->local[4] && o->inv[FP_I_TORCH]>=12 && a->icetrip_prospect<ICETRIP_DIA_STEPS) {
  a->icetrip_prospect++;
  act=light_unseen(a,o,s,ICETRIP_DIA_GAIN,ICETRIP_DIA_PATH);if(act>=0){f->local[2]++;*skill=FP_LIGHT;return act;}
 }
 if(f->local[0]==SGL_M_TROLL && SGL_PROSPECT && o->inv[FP_I_TORCH]>SGL_RESERVE) {
  act=light_unseen(a,o,s,SGL_GAIN,SGL_COST);
  if(act>=0){f->local[2]++;*skill=FP_LIGHT;return act;}
 }
 /* Nothing known within reach and the cave is lit: find a diamond in dark
  * rock (child goal PROSPECT2). */
 if(SGL_PROBE && o->inv[FP_I_DIAMOND]%3>=SGL_PROBE_MIN_DIA && o->inv[FP_I_TORCH]>sgl_reserve(f) && sg_call(a,o,FP_SG_PROSPECT2,sgl_known(a)+1,0)) {
  FPStratFrame *c=sg_top(a);c->local[0]=f->local[0];c->local[4]=f->local[4];
  a->sgl_spent[c->local[0]]--;if(c->local[0]==SGL_M_FIRE)a->fire_dia--;
  f->local[3]++;return sgl_prospect_act(a,o,cfg,s,c,skill);
 }
 /* Nothing to craft, mine or light: the goal ends (observed next boundary). */
 f->local[1]=1;a->sgl_spent[f->local[0]]--;sgl_idle(a,o);
 return -1;
}

/* WEAPON2: the diamond pickaxe in the Troll Mines (target: pickaxe level 4).
 * 70% of Troll entrants carry an iron pickaxe; rubies and sapphires (fire
 * plates, sword enchantment) need the diamond one, and the incumbent makes it
 * only from diamonds that happen to lie within 100 steps. Policy: craft at
 * three diamonds (walking to any known table), mine known diamonds within
 * SGL_W2_RADIUS, else find one in dark rock (PROSPECT2 child, troll reserve).
 * Abandoned: no wood for the pickaxe, the phase budget, hazard, off floor 5,
 * nothing left to mine or probe. Completion observed: pickaxe level 4. */
static int sgl_w2_complete(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) { (void)a;(void)f;return o->inv[FP_I_PICK]>=4; }
static int sgl_w2_abandon(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 return f->local[1] || o->level!=5 || o->inv[FP_I_WOOD]<1 || sgl_spent_out(a,o,f);
}
static int sgl_w2_act(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,FPStratFrame *f,int *skill) {
 if(a->level!=5 || wm_hostile_within(a,5) || o->health<=6.5f || sgl_trip_pending(a,o))return -1;
 a->sgl_spent[SGL_M_TROLL]++;
 int act=-1;
 if(o->inv[FP_I_DIAMOND]>=3){act=craft_radius(a,o,cfg,s,FP_DIAMOND_PICK,3000);if(act>=0){*skill=FP_CRAFT;return act;}}
 act=seek_block(a,o,s,FP_DIAMOND,SGL_W2_RADIUS);if(act>=0){*skill=FP_GATHER;return act;}
 if(SGL_PROBE && o->inv[FP_I_TORCH]>SGL_PROBE_RESERVE && sg_call(a,o,FP_SG_PROSPECT2,sgl_known(a)+1,0)) {
  FPStratFrame *c=sg_top(a);c->local[0]=SGL_M_TROLL;a->sgl_spent[SGL_M_TROLL]--;
  return sgl_prospect_act(a,o,cfg,s,c,skill);
 }
 f->local[1]=1;a->sgl_spent[SGL_M_TROLL]--;sgl_idle(a,o);return -1;
}
static constexpr void sg_late_register(SGKind *table) {
 table[FP_SG_ARMOUR2]=SGKind{"armour2",sgl_armour_complete,sgl_armour_abandon,sgl_armour_act};
 table[FP_SG_PROSPECT2]=SGKind{"prospect2",sgl_prospect_complete,sgl_prospect_abandon,sgl_prospect_act};
 table[FP_SG_WEAPON2]=SGKind{"weapon2",sgl_w2_complete,sgl_w2_abandon,sgl_w2_act};
}
static int sg_late_select(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 (void)cfg;(void)s;
 if(!a->floors[5].seen)return 0;
 /* Ice: a dedicated plate trip when a plate is one diamond away. */
 if(SGL_ICETRIP && a->level==7 && o->level==7 && a->sgl_trip<SGL_ICETRIP_MAX && wm_armour(o)<8 && o->inv[FP_I_PICK]>=3 &&
    o->inv[FP_I_DIAMOND]>=SGL_ICETRIP_MIN && !enchant_trip(a,o) && o->health>=8+o->str-1.01f && o->food>=6 && o->drink>=6 &&
    o->energy>=5 && o->inv[FP_I_TORCH]>=12 && !wm_hostile_within(a,6) && a->floors[7].up>=0 && a->floors[6].up>=0) {
  if(!sg_call(a,o,FP_SG_ARMOUR2,wm_armour(o)+1,SGL_ICETRIP_BUDGET))return 0;
  sg_top(a)->local[0]=SGL_M_ICE;sg_top(a)->local[4]=1;a->sgl_trip++;return 1;
 }
 if(SGL_ICEFLOOR && a->level==7 && o->level==7 && wm_armour(o)<8 && o->inv[FP_I_PICK]>=3 && o->health>8 && o->food>3 && o->drink>3 &&
    o->energy>3 && !wm_hostile_within(a,6) && !enchant_trip(a,o) && o->inv[FP_I_DIAMOND]+sgl_known(a)>=3 && !sgl_idle_now(a,o)) {
  if(!sg_call(a,o,FP_SG_ARMOUR2,wm_armour(o)+1,400))return 0;
  sg_top(a)->local[0]=SGL_M_ICEF;return 1;
 }
 if(a->level!=5 || o->level!=5)return 0;
 if(wm_armour(o)>=8 || o->inv[FP_I_PICK]<(SGL_IRONPICK?3:4) || !sgl_healthy(a,o))return 0;
 int phase=sgl_phase(a);
 if(phase==SGL_M_TROLL) {
  if(SGL_WEAPON2 && o->inv[FP_I_PICK]==3 && o->inv[FP_I_WOOD]>=1 && (o->clear || !SGL_W2_CLEAR) &&
     a->sgl_spent[SGL_M_TROLL]<SGL_W2_BUDGET && !sgl_idle_now(a,o) && o->health>=8+o->str-1.01f) {
   if(!sg_call(a,o,FP_SG_WEAPON2,4,0))return 0;
   sg_top(a)->local[0]=SGL_M_TROLL;return 1;
  }
  if(!SGL_TROLL || !o->clear || a->sgl_spent[SGL_M_TROLL]>=SGL_TROLL_BUDGET)return 0;
  /* A plate needs a table: a known one on this floor or two wood. */
  if(SGL_PLATES_ONLY && o->inv[FP_I_DIAMOND]+sgl_known(a)<3)return 0;
  /* Once this call gave up (nothing to mine or light), do not re-enter
   * until something new is known. */
  if(sgl_idle_now(a,o))return 0;
  if(!sg_call(a,o,FP_SG_ARMOUR2,wm_armour(o)+1,0))return 0;
  sg_top(a)->local[0]=SGL_M_TROLL;return 1;
 }
 if(phase==SGL_M_FIRE) {
  /* The gem trip's mines stay (incumbent: fire_prep, rule "Prospecting"):
   * same entry, plus dark-rock probing once the cave offers nothing. */
  if(!SGL_FIRE || !o->clear || a->fire_dia>=3500 || sgl_idle_now(a,o))return 0;
  if(o->health<8+o->str-1.01f || o->drink<5 || o->food<4 || o->energy<4)return 0;
  if(!sg_call(a,o,FP_SG_ARMOUR2,wm_armour(o)+1,0))return 0;
  sg_top(a)->local[0]=SGL_M_FIRE;return 1;
 }
 if(phase==SGL_M_ICE && icetrip(a)) {
  /* Mines visits of Ice excursions (incumbent: ICETRIP_DIA). The visit
   * budget restarts on each arrival from Fire. */
  if(!SGL_ICE || sgl_idle_now(a,o))return 0;
  if(a->sgl_trip_t!=a->floors[5].entered+1){a->sgl_trip_t=a->floors[5].entered+1;a->icetrip_prospect=0;a->sgl_spent[SGL_M_ICE]=0;}
  /* Never during the incumbent's Ice->Sewers ice-enchant trip (the goal
   * would only yield to it; kit's chest looting relies on that trip). */
  if(a->sgl_spent[SGL_M_ICE]>=sgl_ice_budget(o) || o->health<8 || o->inv[FP_I_DIAMOND]<SGL_ICE_MIN_DIA || enchant_trip(a,o))return 0;
  if(!sg_call(a,o,FP_SG_ARMOUR2,wm_armour(o)+1,0))return 0;
  sg_top(a)->local[0]=SGL_M_ICE;return 1;
 }
 return 0;
}
