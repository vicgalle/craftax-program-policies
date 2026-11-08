#define MID_PEN 0
#define MID_DEX 2
/* Logistics module "mid" (push50): supply goals while the frontier is the
 * Troll Mines or the Fire Realm. Region: floors[5].seen && !floors[7].seen
 * (lg_mid_region). Every goal below acts only in the troll phase (Fire not yet
 * seen), where the logistics oracle has its headroom (+8.3pp from reset; the
 * Fire phase +1.9pp). Outside the region nothing here runs and no state is
 * written: behaviour there is the incumbent's, action for action.
 *
 * PEN ("bat pen"): on-floor food in the Troll Mines.
 *  Why. The mines are a food desert only because they are dark: bats spawn
 *  there at the same rate as snails on the lit floors above (10% a step while
 *  fewer than three are on the floor, 3-14 cells from the player, on path),
 *  random-walk, and despawn at Manhattan distance 14; but a bat is observed
 *  only on a lit cell. The incumbent forages bats it happens to see and, with
 *  none in view at food 3, climbs to the Vault and Sewers (trip deaths:
 *  starvation and drink on those climbs, archers and slimeballs on the way).
 *  A player waiting in a lit area sees the bats that wander into it.
 *  Entry   troll phase, on floor 5, food <= MID_PEN_IN, drink >= 2, not
 *          holding for a heal, energy above the sleep threshold, no failed
 *          pen since the last meal, and a pen stand within MID_PEN_RANGE.
 *  Done    food >= max - 1 (observed).
 *  Abandon food <= MID_PEN_OUT (the incumbent's departure point, food 3),
 *          optionally MID_PEN_PATIENCE steps without a meal, or the floor
 *          left. After a failed pen the incumbent's
 *          food climb runs as before; the pen is not re-entered before a
 *          meal or a new floor visit.
 *  Policy  hunt a visible bat within MID_PEN_HUNT steps; else walk to the pen
 *          stand (the reachable cell whose 9x11 view holds the most lit
 *          walkable cells, minus travel, with a bonus for the up ladder and a
 *          penalty for pool firing lines) and light unlit walkable cells of
 *          its view while torches exceed MID_PEN_TORCHES; else wait.
 *  Yields  (returns -1 for a step, the goal stays active) to drink, healing
 *          and sleep: the rules below it in autonomous() then act.
 */
#ifndef MID_PEN
#define MID_PEN 1
#endif
#ifndef MID_PEN_IN          /* food at which the pen starts */
#define MID_PEN_IN 6
#endif
#ifndef MID_PEN_OUT         /* food at or below which the pen gives up */
#define MID_PEN_OUT 3
#endif
#ifndef MID_PEN_PATIENCE    /* steps without a meal before giving up (0: none) */
#define MID_PEN_PATIENCE 0
#endif
#ifndef MID_PEN_RANGE       /* pen stand: path cost (10 per step) */
#define MID_PEN_RANGE 400
#endif
#ifndef MID_PEN_HUNT        /* bat hunt radius (steps) */
#define MID_PEN_HUNT 12
#endif
#ifndef MID_PEN_TORCHES     /* torches kept (Fire and Ice need them) */
#define MID_PEN_TORCHES 10
#endif
#ifndef MID_PEN_GAIN        /* unlit walkable view cells a pen torch must light */
#define MID_PEN_GAIN 10
#endif
#ifndef MID_TOPUP           /* opportunistic top-ups in the troll phase (0: off) */
#define MID_TOPUP 0
#endif
#ifndef MID_TOPUP_WATER     /* water within this many steps */
#define MID_TOPUP_WATER 8
#endif
#ifndef MID_TOPUP_BAT       /* visible bat within this many steps */
#define MID_TOPUP_BAT 10
#endif
#ifndef MID_MWATER          /* mines water beyond 100 steps / earlier water climb (0: off) */
#define MID_MWATER 0
#endif
#ifndef MID_MW_LEAVE        /* drink at which the water climb leaves (incumbent: 2) */
#define MID_MW_LEAVE 4
#endif
#ifndef MID_FSCOUT          /* Fire scout: Fire visits without the exit before it acts (0: off) */
#define MID_FSCOUT 0
#endif
#ifndef MID_FSCOUT_MARGIN   /* steps of drink kept beyond the walk back */
#define MID_FSCOUT_MARGIN 20
#endif
#ifndef MID_DEX             /* XP in the troll phase: dexterity up to this level first (0: off) */
#define MID_DEX 0
#endif
#ifndef MID_DEXC            /* DEX only when a strength point would not cut a troll's blows-to-kill */
#define MID_DEXC 0
#endif
#ifndef MID_DEX6            /* XP in the fire phase: dexterity up to this level first (0: off) */
#define MID_DEX6 0
#endif
#ifndef MID_LOG
#define MID_LOG 0
#endif

enum { MID_G_PEN, MID_G_WATER, MID_G_TRIP, MID_G_SLEEP };

static int lg_mid_region(const FPAgent *a) { return a->floors[5].seen && !a->floors[7].seen; }

static void mid_log_call(FPAgent *a,int g){a->mid_log[g][0]++;}
static void mid_log_end(FPAgent *a,int g,int completed,int t0){a->mid_log[g][completed?1:2]++;a->mid_log[g][3]+=a->t-t0;}
/* Debug builds only: expose the goal log through unused strategic slots
 * (GEMS..ATTRIBUTES) so that the sg column of p50_eval rows carries it. */
static void mid_log_flush(FPAgent *a) {
#if MID_LOG
 for(int g=0;g<4;g++){a->strat.calls[FP_SG_GEMS+g]=a->mid_log[g][0];a->strat.completions[FP_SG_GEMS+g]=a->mid_log[g][1];
  a->strat.abandonments[FP_SG_GEMS+g]=a->mid_log[g][2];a->strat.steps[FP_SG_GEMS+g]=a->mid_log[g][3];}
#else
 (void)a;
#endif
}

/* Lit as the engine lights the mines: torch disks (memory: lit) and the
 * up ladder's own disk (r^2 <= 22, the same 4.7-cell falloff as a torch). */
static int mid_lit(const FPAgent *a,int p) {
 const FPFloor *f=&a->floors[5];if(f->marks[p].lit)return 1;
 int u=f->up;if(u<0)return 0;
 int dr_=p/FP_SIDE-u/FP_SIDE,dc_=p%FP_SIDE-u%FP_SIDE;return dr_*dr_+dc_*dc_<=22;
}
/* Prefix sums of lit walkable cells (s->prefix is free here: explore and
 * recover rebuild it before use). */
static void mid_lit_prefix(const FPAgent *a,FPScratch *s) {
 const FPFloor *f=&a->floors[5];
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){int p=cell(r,c);n+=walkable(f->map[p]) && mid_lit(a,p);s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
}
static int mid_window(const FPScratch *s,int p) {
 int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
 return s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
}
/* Pools in clear view along the stand's row and column at range 2-5: the
 * firing lines of deep things (they spawn on water and shoot along axes). */
static int mid_pool_lines(const FPAgent *a,int p) {
 int r=p/FP_SIDE,c=p%FP_SIDE,n=0;
 for(int k=1;k<=4;k++)for(int j=1;j<=5;j++) {
  int b=block(a,r+j*dr[k],c+j*dc[k]);if(solid(b))break;
  if(j>=2 && b==FP_WATER)n++;
 }
 return n;
}
static int mid_pen_stand(FPAgent *a,FPScratch *s) {
 const FPFloor *f=&a->floors[5];int best=-1,bs=-INF;
 mid_lit_prefix(a,s);paths_within(a,s,MID_PEN_RANGE);
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(s->dist[p]>MID_PEN_RANGE || !walkable(f->map[p]) || mobs_of(s)[p])continue;
  int w=mid_window(s,p);if(w<8)continue;
  int score=4*w-s->dist[p]/10-8*mid_pool_lines(a,p)+(p==f->up?20:0);
  if(score>bs){bs=score;best=p;}
 }
 return best;
}
/* Light unlit walkable cells of the stand's view: walk to the placement that
 * lights the most of them, approaching its stand from behind (light_unseen). */
static int mid_pen_light(FPAgent *a,const FPObservation *o,FPScratch *s,int stand) {
 const FPFloor *f=&a->floors[5];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 if(o->inv[FP_I_TORCH]<=MID_PEN_TORCHES)return -1;
 paths_within(a,s,150);
 int R=stand/FP_SIDE,C=stand%FP_SIDE;
 for(int r=R-4;r<=R+4;r++)for(int c=C-5;c<=C+5;c++) {
  if(!inside(r,c))continue;int p=cell(r,c);
  if(!place_valid(a,s,p,FP_PLACE_TORCH))continue;
  int gain=0;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++) {
   int y=r+rr,x=c+cc;if(rr*rr+cc*cc>22 || !inside(y,x) || abs(y-R)>4 || abs(x-C)>5)continue;
   int q=cell(y,x);if(walkable(f->map[q]) && !mid_lit(a,q))gain++;
  }
  if(gain<MID_PEN_GAIN)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||mobs_of(s)[cell(sr,sc)])continue;
   int st=cell(sr,sc),partner=cell(pr,pc);
   if(start==st && o->dir==d)return FP_PLACE_TORCH;
   if(s->dist[partner]>150)continue;
   int cost=s->dist[partner]+10-10*gain;
   if(cost<costbest){best=partner;dir=d;costbest=cost;}
  }
 }
 if(best<0)return -1;
 return best==start?dir:move_path(a,o,s,best);
}
/* The pen owns the troll-phase food decision while active or not yet failed
 * this visit: the incumbent's food climb (TB(2), food <= 3) is bypassed. */
static int lg_mid_owns_food(const FPAgent *a) {
 return MID_PEN && a->mid_pen==1;
}
static void mid_pen_end(FPAgent *a,int completed) {
 mid_log_end(a,MID_G_PEN,completed,a->mid_pen_t);
 a->mid_pen=completed?0:2;a->mid_pen_stand=0;
}
static int mid_pen(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int hold,int *skill) {
 if(!MID_PEN || !troll_phase(a))return -1;
 /* A meal: food above the last value this goal saw (the hook is not reached
  * on every step, so compare with its own memory, not the previous step). */
 int maxfood=7+2*o->dex,meal=o->food>a->mid_pen_food;a->mid_pen_food=o->food;
 /* A failed pen ends at the next meal or on another floor. */
 if(a->mid_pen==2 && (meal || o->level!=5))a->mid_pen=0;
 if(a->mid_pen==1) {
  if(meal)a->mid_pen_meal=a->t;
  if(o->level!=5){mid_pen_end(a,0);a->mid_pen=0;return -1;}
  if(o->food>=maxfood-1){mid_pen_end(a,1);return -1;}
  if(o->food<=MID_PEN_OUT || (MID_PEN_PATIENCE && a->t-a->mid_pen_meal>=MID_PEN_PATIENCE)){mid_pen_end(a,0);return -1;}
 }
 if(a->mid_pen==0) {
  if(o->level!=5 || o->food>MID_PEN_IN || o->food<=MID_PEN_OUT || o->drink<2 || hold || o->energy<=cfg->sleep_at)return -1;
  int st=mid_pen_stand(a,s);if(st<0)return -1;
  a->mid_pen=1;a->mid_pen_t=a->mid_pen_meal=a->t;a->mid_pen_stand=st+1;mid_log_call(a,MID_G_PEN);
 }
 if(a->mid_pen!=1)return -1;
 /* Yield for a step: drink first, sleep and heal in shelter (rules below). */
 if(o->drink<=2 || o->energy<=cfg->sleep_at || a->recovering || hold)return -1;
 if(o->health<=6 && o->food>2)return -1;
 *skill=FP_EAT;
 int act=forage(a,o,s);if(act>=0)return act;
 int st=a->mid_pen_stand-1;
 if(st<0 || path_dist(a,s,st)>=INF){st=mid_pen_stand(a,s);if(st<0)return -1;a->mid_pen_stand=st+1;}
 *skill=FP_LIGHT;act=mid_pen_light(a,o,s,st);if(act>=0)return act;
 *skill=FP_EAT;
 if(st!=cell(a->r,a->c))return move_path(a,o,s,st);
 return FP_NOOP;
}

/* TOPUP: opportunistic on-floor sourcing in the troll phase, floor 5.
 *  Why. 15 of 23 frontier troll deaths (s202 arrive5, 300) came with food
 *  <= 3 or drink <= 2: fights during a heal-and-supply spiral, where health
 *  stops regenerating and the climb then starts at food 1 or drink 0. The
 *  incumbent drinks only at drink <= 5 and hunts visible bats at the bottom of
 *  its priority list. Keep the stocks high when it is cheap.
 *  Entry (per step) troll phase, floor 5, no melee mob within 6, health > 5,
 *          and either drink <= max-3 with water within MID_TOPUP_WATER steps,
 *          or food <= max-3 with a visible bat within MID_TOPUP_BAT steps.
 *  Done    drink at max (the incumbent's drinking flag) / the bat eaten.
 *  Abandon the source out of reach or a melee mob within 6 (per step).
 *  Policy  drink from the nearest water / strike the bat (seek, hunt). */
static int mid_topup(FPAgent *a,const FPObservation *o,FPScratch *s,int *skill) {
 if(!MID_TOPUP || !troll_phase(a) || a->level!=5 || o->health<=5 || wm_melee_within(a,6))return -1;
 int mx=7+2*o->dex,act;
 if(o->drink<=mx-3 && (act=seek_block(a,o,s,FP_WATER,MID_TOPUP_WATER))>=0){a->drinking=1;*skill=FP_DRINK;return act;}
 if(o->food<=mx-3 && (act=hunt(a,o,s,2,MID_TOPUP_BAT))>=0){*skill=FP_EAT;return act;}
 return -1;
}

/* MWATER: the troll-phase water decision when no mines water lies within the
 * incumbent's 100-step radius.
 *  Why. The incumbent then waits for drink 2 and climbs: traced Vault thirst
 *  deaths (s202 arrive5 #24, #150, #494) arrive in the Vault at drink 0-1 and
 *  health 1-5, often with no fountain near the landing.
 *  Entry (per step) troll phase, floor 5, drinking, no water within 100 steps.
 *  Policy  leave by the up ladder at drink <= MID_MW_LEAVE instead of 2 (water
 *          beyond 100 steps is beyond reach at drink <= 5: 21 steps a point).
 *  Done/abandon: drink refilled (the incumbent's drinking flag) / floor left. */
static int mid_mwater(FPAgent *a,const FPObservation *o,FPScratch *s,int hold,int *skill) {
 if(!MID_MWATER || !troll_phase(a) || a->level!=5 || !a->drinking || hold)return -1;
 if(water_cost(a,s)<=1000)return -1;
 int act;
 if(o->drink<=MID_MW_LEAVE && (act=travel(a,o,s,0))>=0){*skill=FP_TRAVEL;return act;}
 return -1;
}

/* FSCOUT ("Fire scout"): the exit search on a cleared Fire beyond the drink
 * radius of the incumbent's fire_search.
 *  Why. Fire has no water. fire_search walks out full, and once drink is
 *  below max-2 accepts only frontiers within (drink-1)*20-10 steps of travel
 *  plus 1.25x the Manhattan way back; the rest of each visit is a refill climb
 *  to the mines. Where the exit lies ~70 cells from the up ladder, every visit
 *  turns back at the same frontier: 2% of arrive5 continuations (s202, 6/300)
 *  cycle Troll Mines <-> Fire until the 100,000-step horizon (897-1,040 floor
 *  changes in traced runs; trace s202 arrive5 #182: exit at Manhattan 74).
 *  Entry   fire phase, on Fire, exit unknown, not on the gem trip, at least
 *          MID_FSCOUT Fire visits without the exit, drink > 3, health and
 *          energy above the incumbent's Fire retreat thresholds, no hostile
 *          within 6.
 *  Done    the exit is known (the incumbent then descends) - observed.
 *  Abandon (per step) drink cannot pay the exact path back plus the margin:
 *          the incumbent's refill climb runs.
 *  Policy  the frontier stand (fire_search's scoring, unknown in-bounds cells
 *          in view) with the best score among those whose path cost from
 *          here plus 1.1x the Manhattan way back fit drink*21 - margin. */
static int mid_fscout(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int *skill) {
 if(!MID_FSCOUT || !fire_phase(a))return -1;
 FPFloor *f=&a->floors[6];
 if(a->level!=6)return -1;
 if(a->mid_fire_entered!=f->entered+1){a->mid_fire_entered=f->entered+1;if(f->down<0)a->mid_fire_visits++;}
 if(f->down>=0 || a->fire_prep==1 || a->mid_fire_visits<MID_FSCOUT)return -1;
 int up=f->up;if(up<0 || path_dist(a,s,up)>=INF)return -1;
 if(o->drink<=3 || o->energy<=cfg->sleep_at || o->health<=fire_retreat(o)+1 || wm_hostile_within(a,6))return -1;
 int budget=o->drink*21-MID_FSCOUT_MARGIN;
 if(s->dist[up]/10>=budget)return -1;
 int T,B,L,R;fire_bounds(f,&T,&B,&L,&R);
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0,in=r>=T && r<=B;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN && in && c>=L && c<=R;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 int best=-1;float scorebest=-1e30f;paths_all(a,s);
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(s->dist[p]<=0||s->dist[p]>=INF)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  if(n<4)continue;
  if(s->dist[p]/10+(abs(r-up/FP_SIDE)+abs(c-up%FP_SIDE))*11/10>budget)continue;
  float score=cfg->frontier_bonus*0.75f*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->marks[p].visits,50);
  if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best<0)return -1;
 if(!a->mid_fscout_on){a->mid_fscout_on=1;a->mid_fscout_t=a->t;mid_log_call(a,MID_G_TRIP);}
 a->last_frontier=best;a->frontier_until=a->t+10;*skill=FP_EXPLORE;
 return move_path(a,o,s,best);
}

/* The module's hook in autonomous(), after the drink seek and before the
 * incumbent's troll-phase drink and food climbs. */
static int lg_mid_supply(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int hold,int *skill) {
 if(!lg_mid_region(a))return -1;
 if(a->mid_fscout_on && (a->floors[6].down>=0 || a->level!=6)){mid_log_end(a,MID_G_TRIP,a->floors[6].down>=0,a->mid_fscout_t);a->mid_fscout_on=0;}
 int act=mid_pen(a,o,cfg,s,hold,skill);
 if(act<0)act=mid_mwater(a,o,s,hold,skill);
 if(act<0)act=mid_topup(a,o,s,skill);
 if(act<0)act=mid_fscout(a,o,cfg,s,skill);
 mid_log_flush(a);
 return act;
}

/* Attribute spending in the troll phase (the XP of the Troll Mines arrival):
 * dexterity first, up to MID_DEX. One point slows food, drink and energy
 * decay by 12.5% and raises each maximum by 2 (a full food stock lasts 1.4x
 * longer). The incumbent's order (strength, then intelligence) resumes after. */
static int lg_mid_xp(const FPAgent *a,const FPObservation *o) {
 if(!(MID_DEX && lg_mid_region(a) && troll_phase(a) && o->xp>0 && o->dex<MID_DEX))return 0;
 if(MID_DEXC) {
  /* A troll has 20 health. Strength raises sword damage by 25% of base per
   * point: when one more point still leaves the same number of blows (an
   * enchanted diamond sword: 2 either way), it buys nothing in the mines. */
  FPObservation up=*o;up.str++;
  float d0=wm_hit_damage(o,6,0),d1=wm_hit_damage(&up,6,0);
  if(o->str<5 && ceilf(20/d1)<ceilf(20/d0))return 0;
 }
 return 1;
}
/* Fire phase (the XP of the Fire arrival): dexterity up to MID_DEX6 before the
 * incumbent's Fire rule spends it on intelligence. */
static int lg_mid_xp6(const FPAgent *a,const FPObservation *o) {
 return MID_DEX6 && lg_mid_region(a) && fire_phase(a) && o->xp>0 && o->dex<MID_DEX6;
}
