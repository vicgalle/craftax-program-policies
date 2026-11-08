/* m_resource.inc: resource model (food, drink, energy): a model of the game, no decisions. The program queries
 * it through natives (rm_time_to_empty, rm_plan_cost, rm_plan_food); its memory is written by rm_observe()
 * (called at the end of fp_observe: the rm_* fields of FPAgent) and by the per-floor route cache (sp_cache).
 * Game facts it holds: the engine's decay formulas below; floor 8 (the Graveyard boss fight) stops the decay
 * counters; the per-floor meal-wait priors of rm_meal_wait (no animal spawns on Ice or the Graveyard).
 *
 * Engine formulas (craftax_full.c ~4576-4615), dex = o->dex, c = 1 - 0.125*(dex-1):
 *   food:   hunger  += c (0.5c asleep) per step; when > 25: food-1,  hunger=0   (awake period floor(25/c)+1)
 *   drink:  thirst  += c (0.5c asleep) per step; when > 20: drink-1, thirst=0   (awake period floor(20/c)+1)
 *   energy: fatigue += c awake; when > 30: energy-1, fatigue=0                  (awake period floor(30/c)+1)
 *           asleep fatigue = min(fatigue-1, 0); when < -10: energy+1, fatigue=0 (+1 per 11 steps asleep)
 *   max stock 7 + 2*dex (craftax_step_get_max_food/drink/energy). Eating (cow +6, plant +4) and drinking
 *   (+1 per drink) reset hunger/thirst to 0 before that step's decay. Fighting the boss stops decrements.
 * The sub-step counters (hunger/thirst/fatigue) are hidden: rm_observe() mirrors them from the observed
 * stock changes. From reset (a->t == 0) they are exact; for a bank snapshot (rm fields zero-filled) a counter
 * is unsynced, assumed 0, until its first observed decrement (then exact up to float rounding).
 *
 * Accessors (k = RM_FOOD, RM_DRINK, RM_ENERGY):
 *   rm_max(o)                         stock maximum 7 + 2*dex
 *   rm_period(o,k,asleep)             steps per unit lost at the current dex (asleep: food/drink only)
 *   rm_time_to_empty(a,o,k)           awake steps until the stock hits 0 (0 if already empty)
 *   rm_time_to(a,o,k,level)           awake steps until the stock falls to `level`
 *   rm_synced(a,k)                    1 when the hidden counter of k is known exactly
 *   rm_last_decrement(a,k)            agent step of the last observed decrement (-1 none seen)
 *   rm_route_cost(a,l0,p0,l1,p1)      steps from cell p0 on floor l0 to cell p1 on floor l1 through known
 *                                     ladders (each ladder action counts 1); -1 if no known route
 *   rm_nearest_source(a,o,k,&floor,&cell,&cost)
 *                                     nearest known source from the agent: RM_DRINK water/fountain (cost to a
 *                                     walkable cell beside it, +1 for the drink action), RM_FOOD ripe plant
 *                                     (beside it), else the Overworld (cows respawn there) at its arrival
 *                                     ladder + RM_COW_SEARCH; RM_ENERGY a sleep nook (a walkable cell with >= 3
 *                                     known solid neighbours, sealed by one stone). Returns cost or -1.
 *   rm_trip_cost(a,o,k,&floor,&cell,&out,&back)
 *                                     round trip to the nearest source: out (to it) + back (to the current
 *                                     cell); returns out+back or -1.
 * Routes are BFS over remembered walkable cells of each floor (FPFloor.map), 1 step per move; mobs, danger
 * and darkness are ignored (paths() adds those costs for the current floor). */
enum { RM_FOOD, RM_DRINK, RM_ENERGY, RM_KINDS };
#define RM_COW_SEARCH 40
#ifndef RM_FOODCAT
#define RM_FOODCAT 1   /* v2 food catalogue */
#endif

static int rm_stock(const FPObservation *o,int k) { return k==RM_FOOD?o->food:k==RM_DRINK?o->drink:o->energy; }
static float rm_threshold(int k) { return k==RM_FOOD?25.0f:k==RM_DRINK?20.0f:30.0f; }
static float rm_coeff(const FPObservation *o) { return 1.0f-0.125f*(float)(o->dex-1); }
static int rm_max(const FPObservation *o) { return 7+2*o->dex; }
static int rm_period(const FPObservation *o,int k,int asleep) {
 float c=rm_coeff(o)*(asleep && k!=RM_ENERGY?0.5f:1.0f),acc=0;int n=0;
 if(asleep && k==RM_ENERGY)return -1;
 if(c<=0)return INT_MAX;          /* energy rises asleep (+1 per 11 steps) */
 do {acc+=c;n++;} while(!(acc>rm_threshold(k)));  /* same float accumulation as the engine */
 return n;
}
static int rm_synced(const FPAgent *a,int k) { return a->rm_init && a->rm_synced[k]; }
static int rm_time_to(const FPAgent *a,const FPObservation *o,int k,int level) {
 int v=rm_stock(o,k);if(v<=level)return 0;
 float c=rm_coeff(o),acc=a->rm_init?a->rm_acc[k]:0;int n=0;
 if(acc<0)acc=0;                               /* energy after sleep: fatigue <= 0 counts as a fresh counter */
 if(c<=0)return INT_MAX;
 do {acc+=c;n++;} while(!(acc>rm_threshold(k)));
 return n+(v-1-level)*rm_period(o,k,0);
}
static int rm_time_to_empty(const FPAgent *a,const FPObservation *o,int k) { return rm_time_to(a,o,k,0); }

/* Mirror of the hidden counters; called once per observation (fp_observe). */
static void rm_observe(FPAgent *a,const FPObservation *o) {
 if(!a->rm_init) {
  a->rm_init=1;
  for(int k=0;k<RM_KINDS;k++){a->rm_prev[k]=rm_stock(o,k);a->rm_acc[k]=0;a->rm_synced[k]=a->t==0;a->rm_t_dec[k]=-1;}
  return;
 }
 /* FACT: on floor 8, the Graveyard, the boss fight stops the decrements (counters reset, no decrement). */
 float c=rm_coeff(o);int asleep=o->sleeping,boss=o->level==8;
 for(int k=0;k<RM_KINDS;k++) {
  int v=rm_stock(o,k),prev=a->rm_prev[k];float acc=a->rm_acc[k];
  if(k!=RM_ENERGY) {
   float add=(asleep?0.5f:1.0f)*c;
   if(v<prev){acc=0;a->rm_synced[k]=1;a->rm_t_dec[k]=a->t;}
   else if(v>prev){acc=add;a->rm_synced[k]=1;}  /* eat/drink reset the counter before the decay */
   else {acc+=add;if(acc>rm_threshold(k))acc=v==0 || boss || !a->rm_synced[k]?0:rm_threshold(k);}
  } else {
   if(v<prev){acc=0;a->rm_synced[k]=1;a->rm_t_dec[k]=a->t;}
   else if(asleep) {
    acc=acc-1<0?acc-1:0;
    if(v>prev || acc<-10)acc=0;
   } else {acc+=c;if(acc>rm_threshold(k))acc=v==0 || boss || !a->rm_synced[k]?0:rm_threshold(k);}
  }
  a->rm_acc[k]=acc;a->rm_prev[k]=v;
 }
 /* v2 food catalogue: a meal is a food gain of 2 or more (cow/bat/snail +6, plant +4) on this floor. */
 if(RM_FOODCAT && (unsigned)o->level<(unsigned)FP_LEVELS && a->previous.level==o->level) {
  if(o->food>=a->previous.food+2)a->rm_meals[o->level]++;
  if(o->food<rm_max(o) && !o->sleeping)a->rm_hsteps[o->level]++;
 }
}

/* Routes over the remembered map of floor l from p0. */
#ifndef RM_MINING
#define RM_MINING 1   /* v1: routes may mine rock (3 steps), cross a table (10) or bridge water/lava (6) */
#endif
static const FPObservation *rm_cur_obs; /* mining capability of the current query (rm_nearest_source, rm_observe) */
enum { RM_MAX_COST=10 };  /* rm_enter_cost is 0 (impassable), 1, 3, 6 or 10 */
static_assert(FP_ROUTE_RING==RM_MAX_COST+1,"queued keys lie in [cur, cur+RM_MAX_COST]: one bucket each");
static int rm_enter_cost(const FPFloor *f,int q) {
 int b=f->map[q];const FPObservation *o=rm_cur_obs;
 if(walkable(b))return 1;
 if(!RM_MINING || !o)return 0;
 if(mineable(o,b))return (b==FP_TABLE||b==FP_FURNACE)?10:3;
 if((b==FP_WATER||b==FP_LAVA) && o->inv[1]>0 && o->inv[FP_I_PICK]>0 && !f->marks[q].item)return 6;
 return 0;
}
/* Dial's buckets over the remembered map of floor l from p0, one bit per cell
 * (FPRouteWork). Entry costs are 1..RM_MAX_COST, so while bucket cur pops,
 * every queued key lies in [cur, cur+RM_MAX_COST] and the ring of
 * RM_MAX_COST+1 buckets holds each key in its own bucket. Each reachable cell
 * is settled once, with its exact shortest-route cost d (positive costs that
 * depend only on the cell entered), in nondecreasing d: visit(p,d) sees it
 * then and returns true to stop. The former version (Dial's with linked
 * bucket lists, ~300 KB of thread-local arrays, at most 1+4*FP_CELLS pushes,
 * never more than its list capacity) computed these same costs, and its
 * readers depend only on the costs, never on the order among equal ones. */
template <class Visit>
static void rm_search(const FPAgent *a,int l,int p0,FPRouteWork *w,Visit &&visit) {
 const FPFloor *f=&a->floors[l];
 CellBits settled{w->settled};settled.clear();
 for(auto &b:w->ring)CellBits{b}.clear();
 if(p0<0 || p0>=FP_CELLS)return;
 CellBits{w->ring[0]}.set(p0);
 for(int cur=0,pending=1;pending;cur++) {
  uint64_t *bucket=w->ring[cur%FP_ROUTE_RING];
  for(int i=0;i<kCellWords;i++)while(bucket[i]) {
   int p=i*64+lowest_bit(bucket[i]);bucket[i]&=bucket[i]-1;pending--;
   settled.set(p);if(visit(p,cur))return;
   int r=p/FP_SIDE,c=p%FP_SIDE;
   for(int d=1;d<=4;d++) {
    int rr=r+dr[d],cc=c+dc[d];if(!inside(rr,cc))continue;int q=cell(rr,cc);
    if(settled[q])continue;int k=rm_enter_cost(f,q);if(!k)continue;int nd=cur+k,old=-1;
    for(int j=0;j<FP_ROUTE_RING && old<0;j++)if(CellBits{w->ring[(cur+j)%FP_ROUTE_RING]}[q])old=cur+j;
    if(old>=0 && old<=nd)continue;
    if(old>=0)CellBits{w->ring[old%FP_ROUTE_RING]}.reset(q);else pending++;
    CellBits{w->ring[nd%FP_ROUTE_RING]}.set(q);
   }
  }
 }
}
static int rm_dist(const FPAgent *a,int l,int p0,int p1,FPRouteWork *w) {
 if(p0<0 || p1<0)return -1;if(p0==p1)return 0;
 int d1=-1;rm_search(a,l,p0,w,[&](int p,int d){if(p==p1)d1=d;return p==p1;});
 return d1;
}
static int rm_route_cost(const FPAgent *a,int l0,int p0,int l1,int p1,FPRouteWork *w) {
 if(l0<0 || l1<0 || l0>=FP_LEVELS || l1>=FP_LEVELS)return -1;
 int total=0,l=l0,p=p0;
 while(l!=l1) {
  int up=l1<l,exit=up?a->floors[l].up:a->floors[l].down,next=up?l-1:l+1;
  int arrive=up?a->floors[next].down:a->floors[next].up;
  int d=rm_dist(a,l,p,exit,w);if(d<0 || arrive<0)return -1;
  total+=d+1;l=next;p=arrive;
 }
 int d=rm_dist(a,l,p,p1,w);return d<0?-1:total+d;
}
static int rm_is_source(const FPFloor *f,int k,int p) {
 int b=f->map[p];
 if(k==RM_DRINK)return b==FP_WATER || b==FP_FOUNTAIN;
 if(k==RM_FOOD)return b==FP_RIPE;
 if(!walkable(b))return 0;                     /* RM_ENERGY: a nook that one stone seals */
 int r=p/FP_SIDE,c=p%FP_SIDE,n=0;
 for(int d=1;d<=4;d++){int rr=r+dr[d],cc=c+dc[d];if(!inside(rr,cc) || solid(f->map[cell(rr,cc)]))n++;}
 return n>=3;
}
/* Cost from the arrival cell `p` on floor l to the nearest source of kind k there (stand cell in *cell).
 * Evaluator-only (fp_rm_*): the full cost map lives on the caller's stack. */
static int rm_floor_source(const FPAgent *a,int l,int p,int k,int *cell_out,FPRouteWork *w) {
 const FPFloor *f=&a->floors[l];int best=-1;
 if(p<0)return -1;
 int dist[FP_CELLS];for(int &d:dist)d=INT_MAX;
 rm_search(a,l,p,w,[&](int q,int d){dist[q]=d;return false;});
 for(int q=f->known_lo;q<=f->known_hi;q++) {
  if(q<0 || q>=FP_CELLS || !rm_is_source(f,k,q))continue;
  if(k==RM_ENERGY){if(dist[q]!=INT_MAX && (best<0 || dist[q]<best)){best=dist[q];*cell_out=q;}continue;}
  int r=q/FP_SIDE,c=q%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int rr=r+dr[d],cc=c+dc[d];if(!inside(rr,cc))continue;int s=cell(rr,cc);
   if(dist[s]!=INT_MAX && (best<0 || dist[s]+1<best)){best=dist[s]+1;*cell_out=s;}
  }
 }
 return best;
}
/* Expected steps to a meal on floor l: hungry steps per meal there, shrunk to a per-floor prior (prior meals
 * RM_PRIOR_M at the prior wait); -1 where no animal spawns (Ice). Priors from the incumbent's 202M trips:
 * cows on the Overworld, lit floors 1-4 and Fire, the dark Troll Mines rarely, the Graveyard never. */
#define RM_PRIOR_M 2
static int rm_meal_wait(const FPAgent *a,int l) {
 static const int prior[FP_LEVELS]={40,120,150,120,120,300,150,-1,-1};
 if(l<0 || l>=FP_LEVELS || prior[l]<0)return -1;
 int w=(a->rm_hsteps[l]+RM_PRIOR_M*prior[l])/(a->rm_meals[l]+RM_PRIOR_M);
 return w>1500?1500:w;
}
static int rm_nearest_source(const FPAgent *a,const FPObservation *o,int k,int *floor_out,int *cell_out,int *cost_out,FPRouteWork *w) {
 rm_cur_obs=o;int here=cell(a->r,a->c),best=-1;
 for(int l=0;l<FP_LEVELS;l++) {
  if(!a->floors[l].seen && l!=a->level)continue;
  int arrive=l==a->level?here:l<a->level?a->floors[l].down:a->floors[l].up;
  int to=l==a->level?0:rm_route_cost(a,a->level,here,l,arrive,w);if(to<0)continue;
  int c1=-1,local=rm_floor_source(a,l,arrive,k,&c1,w);
  if(RM_FOODCAT && k==RM_FOOD){int w=rm_meal_wait(a,l);if(w>=0 && (local<0 || w<local)){local=w;c1=arrive;}}
  else if(local<0 && k==RM_FOOD && l==0){local=RM_COW_SEARCH;c1=arrive;}
  if(local<0)continue;
  if(best<0 || to+local<best){best=to+local;*floor_out=l;*cell_out=c1;}
 }
 if(cost_out)*cost_out=best;
 return best;
}
static int rm_trip_cost(const FPAgent *a,const FPObservation *o,int k,int *floor_out,int *cell_out,int *out,int *back,FPRouteWork *w) {
 int fl=-1,cl=-1,cost=-1;
 if(rm_nearest_source(a,o,k,&fl,&cl,&cost,w)<0)return -1;
 int b=rm_route_cost(a,fl,cl,a->level,cell(a->r,a->c),w);if(b<0)return -1;
 if(floor_out)*floor_out=fl;if(cell_out)*cell_out=cl;if(out)*out=cost;if(back)*back=b;
 return cost+b;
}

/* Evaluator access (audits, prediction-error metrics); the policy never calls these. They take no
 * scratch, so their route workspace lives on their own stack frame. */
int fp_rm_time_to_empty(const FPAgent *a,const FPObservation *o,int k) { return rm_time_to_empty(a,o,k); }
int fp_rm_time_to(const FPAgent *a,const FPObservation *o,int k,int level) { return rm_time_to(a,o,k,level); }
int fp_rm_nearest_source(const FPAgent *a,const FPObservation *o,int k,int *floor_out,int *cell_out,int *cost_out) {
 FPRouteWork w;return rm_nearest_source(a,o,k,floor_out,cell_out,cost_out,&w);
}
int fp_rm_trip_cost(const FPAgent *a,const FPObservation *o,int k,int *floor_out,int *cell_out,int *out,int *back) {
 FPRouteWork w;return rm_trip_cost(a,o,k,floor_out,cell_out,out,back,&w);
}
int fp_rm_route_cost(const FPAgent *a,int l0,int p0,int l1,int p1) { FPRouteWork w;return rm_route_cost(a,l0,p0,l1,p1,&w); }

/* ==== v1 planner support (supply agent) ====================================================================
 * rm_plan_cost(a,o,s,k,&dir): cheapest known source of k (RM_DRINK only for now) from the agent, in steps:
 * the current floor uses the policy's own path costs (s->dist from paths(), /10: danger and mobs included);
 * other floors use the per-floor cache (landing -> source, landing -> next ladder), recomputed only when a
 * floor's step count or the pick level changed, i.e. once per visit. dir: 0 here, -1 up, +1 down; -1 none. */
static int rm_stamp(const FPAgent *a,const FPObservation *o,int l) { return (a->floors[l].steps+1)*8+(o->inv[FP_I_PICK]&7); }
/* From each landing: the cheapest stand beside a known water or fountain (+1 for the drink) and the
 * other ladder. Settled cells come in nondecreasing cost, so the first one beside a source carries the
 * minimum the former full-map scan took over all of them, and `to` reads its own cost; the search stops
 * once both are known. */
static int rm_beside_source(const FPFloor *f,int k,int p) {
 int r=p/FP_SIDE,c=p%FP_SIDE;
 for(int d=1;d<=4;d++) {
  int rr=r+dr[d],cc=c+dc[d];if(!inside(rr,cc))continue;int q=cell(rr,cc);
  if(q>=f->known_lo && q<=f->known_hi && rm_is_source(f,k,q))return 1;
 }
 return 0;
}
static void rm_cache_floor(FPAgent *a,const FPObservation *o,int l,FPRouteWork *work) {
 if(a->sp_cstamp[l]==rm_stamp(a,o,l))return;
 const FPFloor *f=&a->floors[l];rm_cur_obs=o;
 for(int side=0;side<2;side++) {
  int from=side?f->up:f->down,to=side?f->down:f->up,w=-1,lad=-1;
  if(from>=0)rm_search(a,l,from,work,[&](int p,int d) {
   if(w<0 && rm_beside_source(f,RM_DRINK,p))w=d+1;
   if(p==to)lad=d;
   return w>=0 && (to<0 || lad>=0);
  });
  a->sp_cache[l][side*2]=w;a->sp_cache[l][side*2+1]=lad;
 }
 a->sp_cstamp[l]=rm_stamp(a,o,l);
}
static int rm_plan_cost(FPAgent *a,const FPObservation *o,FPScratch *s,int k,int *dir) {
 paths_all(a,s);
 int L=a->level,best=-1;*dir=-1;
 if(k!=RM_DRINK)return -1;
 const FPFloor *f=&a->floors[L];
 for(int q=f->known_lo;q<=f->known_hi;q++) {
  if(q<0 || q>=FP_CELLS || !rm_is_source(f,RM_DRINK,q))continue;
  int rr0=q/FP_SIDE,cc0=q%FP_SIDE;
  for(int dd=1;dd<=4;dd++){int rr=rr0+dr[dd],cc=cc0+dc[dd];if(!inside(rr,cc))continue;int x=s->dist[cell(rr,cc)];
   if(x<INF && (best<0 || x/10+1<best)){best=x/10+1;*dir=0;}}
 }
 /* up */
 if(f->up>=0 && s->dist[f->up]<INF) {
  int acc=s->dist[f->up]/10+1;
  for(int l=L-1;l>=0 && a->floors[l].seen;l--) {
   rm_cache_floor(a,o,l,&s->route);
   if(a->sp_cache[l][0]>=0 && (best<0 || acc+a->sp_cache[l][0]<best)){best=acc+a->sp_cache[l][0];*dir=-1;}
   if(a->sp_cache[l][1]<0 || (best>=0 && acc>=best))break;
   acc+=a->sp_cache[l][1]+1;
  }
 }
 /* down (never past the frontier) */
 if(f->down>=0 && s->dist[f->down]<INF) {
  int acc=s->dist[f->down]/10+1;
  for(int l=L+1;l<FP_LEVELS && a->floors[l].seen;l++) {
   rm_cache_floor(a,o,l,&s->route);
   if(a->sp_cache[l][2]>=0 && (best<0 || acc+a->sp_cache[l][2]<best)){best=acc+a->sp_cache[l][2];*dir=1;}
   if(a->sp_cache[l][3]<0 || (best>=0 && acc>=best))break;
   acc+=a->sp_cache[l][3]+1;
  }
 }
 return best;
}

/* rm_plan_food(a,o,s,&dir): expected steps to the next meal by the v2 food catalogue: waiting here
 * (rm_meal_wait of this floor) or going up/down to a floor with a better meal rate (route by s->dist and the
 * per-floor ladder cache, plus that floor's expected wait). dir: 0 here, -1 up, +1 down. */
static int rm_plan_food(FPAgent *a,const FPObservation *o,FPScratch *s,int *dir) {
 paths_all(a,s);
 int L=a->level,best=rm_meal_wait(a,L);*dir=best>=0?0:-1;
 const FPFloor *f=&a->floors[L];
 if(f->up>=0 && s->dist[f->up]<INF) {
  int acc=s->dist[f->up]/10+1;
  for(int l=L-1;l>=0 && a->floors[l].seen;l--) {
   rm_cache_floor(a,o,l,&s->route);int w=rm_meal_wait(a,l);
   if(w>=0 && (best<0 || acc+w<best)){best=acc+w;*dir=-1;}
   if(a->sp_cache[l][1]<0 || (best>=0 && acc>=best))break;
   acc+=a->sp_cache[l][1]+1;
  }
 }
 return best;
}
