/* m_nav.inc: the navigation machine: travel to a ladder, frontier
 * exploration (explore, frontier_walk, the exit search fire_search), lighting
 * unseen cells (light_unseen, light_behind) and probing dark rock
 * (dark_probe). The floor-specific choices (dark-entry repairs, the Troll
 * Mines explorer, the Graveyard lighting, when explore runs the exit search)
 * are the program's (p/plan.tr, queries in exports/plan.txt). */

/* Integral image of unseen cells over rows [r0,r1) and columns [c0,c1),
 * counted from that corner (s->prefix[r][c], r0<=r<=r1, c0<=c<=c1): any
 * window inside the rectangle gets the same count as from the whole map. */
static void unseen_prefix(const FPFloor *f,FPScratch *s,int r0,int r1,int c0,int c1) {
 for(int c=c0;c<=c1;c++)s->prefix[r0][c]=0;
 for(int r=r0;r<r1;r++) {
  int n=0;s->prefix[r+1][c0]=0;
  for(int c=c0;c<c1;c++){n+=f->map[cell(r,c)]==UNKNOWN;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
}
/* The rows (box[0..1]) and columns (box[2..3]) spanned by settled cells
 * within steps of the agent, and the integral image of unseen cells over the
 * rectangle their 9x11 windows cover. The settled box holds the agent's cell;
 * clamping keeps every index in the map. */
static void settled_box(const FPAgent *a,FPScratch *s,int steps,int *box) {
 const int *b=s->path_box;
 box[0]=imax(imax(imin(b[0],a->r),a->r-steps),0);box[1]=imin(imin(imax(b[1],a->r),a->r+steps),FP_SIDE-1);
 box[2]=imax(imax(imin(b[2],a->c),a->c-steps),0);box[3]=imin(imin(imax(b[3],a->c),a->c+steps),FP_SIDE-1);
 unseen_prefix(&a->floors[a->level],s,imax(box[0]-4,0),imin(box[1]+5,FP_SIDE),imax(box[2]-5,0),imin(box[3]+6,FP_SIDE));
}
static int explore(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 /* The program may route the search through the exit search first. */
 if(tr_query(a,o,s,TRQ_EXPLORE_FIRE_SEARCH)){int act=fire_search(a,o,cfg,s);if(act>=0)return act;}
 FPFloor *f=&a->floors[a->level];paths_within(a,s,600);
 /* Candidates (0<dist<=600) are settled cells at most 60 steps away (a step
  * costs at least 10). */
 int box[4];settled_box(a,s,60,box);
 int best=-1;float scorebest=-1e30f;
 /* Loop invariants, read once (the compiler may not hoist loads it cannot
  * prove safe past the candidate test); each is the same float value. */
 /* Long elemental excursions consume scarce supplies. Prefer closer
  * frontiers while retaining the configured exploration tradeoff (the
  * program says when: explore_prefer_near). */
 const float bonus=cfg->frontier_bonus*(tr_query(a,o,s,TRQ_EXPLORE_PREFER_NEAR)?0.75f:1),revisit=cfg->revisit_cost*4;
 const int recent=a->t<a->frontier_until?a->last_frontier:-1;const uint64_t rng=a->rng;
 for(int row=box[0];row<=box[1];row++)for(int p=imax(f->known_lo,cell(row,box[2]));p<=imin(f->known_hi,cell(row,box[3]));p++) {
  if(s->dist[p]<=0||s->dist[p]>600)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  float score=bonus*n-0.4f*s->dist[p]-revisit*imin(f->marks[p].visits,50);
  if(p==recent)score+=12;
  /* The tie noise below is at most 255*0.002f, and float addition rounds
   * monotonically: when even that cannot lift the score above the best, the
   * comparison fails and the noise need not be computed. */
  constexpr float max_noise=255.0f*0.002f;
  if(score+max_noise<=scorebest)continue;
  score+=(float)(((uint64_t)p*UINT64_C(0x9e3779b97f4a7c15)^rng)>>56)*0.002f;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best>=0){a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);}
 (void)word(a);return FP_NOOP;
}
static int travel(FPAgent *a,const FPObservation *o,FPScratch *s,int down) {
 int p=down?a->floors[a->level].down:a->floors[a->level].up;
 if(p>=0)paths_to(a,s,p);
 if(p<0 || s->dist[p]>=INF)return -1;
 if(p==cell(a->r,a->c))return down?FP_DESCEND:FP_ASCEND;
 return move_path(a,o,s,p);
}
/* Frontier walker shared with the fire stream (same function there): stands
 * whose 9x11 view still holds four unseen cells, at any distance. */
static int frontier_walk(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 paths_all(a,s);
 FPFloor *f=&a->floors[a->level];(void)o;
 /* Candidates (0<dist<INF) are settled cells. */
 int box[4];settled_box(a,s,FP_SIDE,box);
 int best=-1;float scorebest=-1e30f;
 /* Loop invariants, read once (as in explore). */
 const float bonus=cfg->frontier_bonus,revisit=cfg->revisit_cost*4;
 const int recent=a->t<a->frontier_until?a->last_frontier:-1;
 for(int row=box[0];row<=box[1];row++)for(int p=imax(f->known_lo,cell(row,box[2]));p<=imin(f->known_hi,cell(row,box[3]));p++) {
  if(s->dist[p]<=0||s->dist[p]>=INF)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  if(n<4)continue;
  float score=bonus*n-0.4f*s->dist[p]-revisit*imin(f->marks[p].visits,50);
  if(p==recent)score+=12;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best<0)return -1;
 a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);
}
/* Exit search on a lit floor (explore runs it when the program asks: on
 * Fire). The general explorer scores every cell within 60 steps, so once the ladder's surroundings were known, cells with nothing
 * unseen nearby outscored real frontiers beyond lava or rock: ep 0 wandered
 * there for ~90 visits before finding an exit 66 cells away. Consider only
 * stands that reveal unseen in-map terrain, at any distance, and first those
 * the drink left can reach and return from; with none, refill first. The
 * round-trip budget (steps) and whether a refill trip helps are the
 * program's (fire_search_budget, fire_search_refill). */
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 paths_all(a,s);
 FPFloor *f=&a->floors[a->level];int T,B,L,R;fire_bounds(f,&T,&B,&L,&R);
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0,in=r>=T && r<=B;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN && in && c>=L && c<=R;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 int up=f->up,budget=tr_query(a,o,s,TRQ_FIRE_SEARCH_BUDGET),refill=tr_query(a,o,s,TRQ_FIRE_SEARCH_REFILL);
 for(int pass=0;pass<2;pass++) {
  int best=-1;float scorebest=-1e30f;
  for(int p=f->known_lo;p<=f->known_hi;p++) {
   if(s->dist[p]<=0||s->dist[p]>=INF)continue;
   int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
   int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
   if(n<4)continue;
   /* Manhattan distance understates the walk back around lava. */
   if(!pass && s->dist[p]/10+(up>=0?(abs(r-up/FP_SIDE)+abs(c-up%FP_SIDE))*5/4:0)>budget)continue;
   float score=cfg->frontier_bonus*0.75f*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->marks[p].visits,50);
   if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
   score+=(float)(((uint64_t)p*UINT64_C(0x9e3779b97f4a7c15)^a->rng)>>56)*0.002f;
   if(score>scorebest){best=p;scorebest=score;}
  }
  if(best>=0){a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);}
  if(!pass && refill){int act=travel(a,o,s,0);if(act>=0)return act;}
 }
 return -1;
}
/* Reach a torch placement with the correct facing by approaching its stand
 * from behind. Turning towards an empty target would move onto that target.
 * The nearest placement on the current floor that reveals at least min_gain
 * unseen cells, within max_cost path steps. (The program's graveyard_light.) */
static int light_behind(FPAgent *a,const FPObservation *o,FPScratch *s,int min_gain,int max_cost) {
 const FPFloor *f=&a->floors[a->level];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 if(!o->inv[FP_I_TORCH])return -1;
 unseen_rows(f,s,f->known_lo,f->known_hi,0);
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(!torch_valid(a,s,p))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=unseen_gain(s,r,c);
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||mobs_of(s)[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d)return FP_PLACE_TORCH;
   int cost=path_dist_within(a,s,partner,imin(max_cost-10,costbest-11))+10; /* only a cheaper stand within max_cost counts */
   if(cost<costbest && cost<=max_cost){best=partner;dir=d;costbest=cost;}
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
 int words=unseen_rows(f,s,f->known_lo,f->known_hi,WM_TORCH_GROUND);
 for(int p:SetBits(s->unseen.cand,words)) {
  if(p<f->known_lo || p>f->known_hi || !torch_valid(a,s,p))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=unseen_gain(s,r,c);
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||mobs_of(s)[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d)return FP_PLACE_TORCH;
   if(path_dist_within(a,s,partner,imin(max_cost,costbest-11+10*gain))>max_cost)continue; /* settled as far as a cheaper stand could lie */
   /* One unseen cell is worth a step: large dark regions before small pockets. */
   int cost=s->dist[partner]+10-10*gain;
   if(cost<costbest){best=partner;dir=d;costbest=cost;}
  }
 }
 if(best<0)return -1;
 return best==start?dir:move_path(a,o,s,best);
}
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
 int words=unseen_rows(f,s,f->known_lo,f->known_hi,WM_BIT(FP_STONE)|WM_BIT(FP_COAL));
 for(int p:SetBits(s->unseen.cand,words)) {
  int b=f->map[p];if(p<f->known_lo || p>f->known_hi)continue;
  if(!(b==FP_STONE || b==FP_COAL) || f->marks[p].item || mobs_of(s)[p])continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=unseen_gain(s,r,c);
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d];if(!inside(sr,sc))continue;
   /* The stand may itself be rock: routes tunnel through observed rock. */
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
/* Melee damage of floor L's resident mob (the descent guard of the program,
 * nav_descent_blocked, prices a return below with it). Game fact: the mob
 * type that lives on each floor. */
static float nav_floor_hit(const FPObservation *o,int L) {
 static const int type[8]={1,3,2,4,5,6,7,8};
 return L>=0 && L<8?wm_attack_damage(o,type[L]):0;
}
