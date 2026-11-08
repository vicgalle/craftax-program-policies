/* m_supply.inc: supply machine: foraging, shelters (sealable rooms) and water costs.
 * The supply strategy (drink and food decisions, the Ice-phase excursion legs,
 * the supply planner and the lg_ice/lg_mid supply goals) is TR in p/supply.tr. */

/* First cell in [p,hi] holding a passive mob or a ripe plant, else hi+1
 * (p itself when p>hi). A passive mob can only be on a view cell, and view
 * order is cell order: mob is the first one in [p,hi], else hi+1. The first
 * ripe plant before it is found eight map cells per word. */
static int next_food(const FPFloor *f,const FPScratch *s,int p,int hi) {
 int mob=hi+1;
 for(int i=0;i<FP_VIEW;i++)if(s->passive[i]) {
  int q=cell(s->view_r0+i/kViewCols,s->view_c0+i%kViewCols);if(q>=p){if(q<=hi)mob=q;break;}
 }
 if(p>hi)return p;
 for(;p+7<mob && !has_byte(load8(f->map+p),FP_RIPE);p+=8);
 while(p<mob && f->map[p]!=FP_RIPE)p++;
 return p;
}
/* Compare reachable food sources by travel, facing, and sword strikes.
 * A nearby animal can refill food before a distant remembered crop.
 * Game facts: passive mob health 3, 4 and 6 by kind (strikes = health / sword damage). */
static int forage(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int best=-1,facing=0,start=cell(a->r,a->c);float best_cost=1e30f;
 const FPFloor *f=&a->floors[a->level];
 for(int p=next_food(f,s,f->known_lo,f->known_hi);p<=f->known_hi;p=next_food(f,s,p+1,f->known_hi)) {
  int passive=passive_of(s)[p],plant=f->map[p]==FP_RIPE;
  if(!passive && !plant)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  int strikes=passive?(int)ceilf((passive==1?3:passive==2?4:6)/wm_sword_damage(o)):1;
  int radius=passive?80:50;
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=path_dist_within(a,s,q,radius*10)+(q==start && o->dir==d?0:10);
   if(cost>radius*10)continue;
   float score=(float)(cost+strikes*10);
   if(score<best_cost){best=q;facing=d;best_cost=score;a->target=p;a->goal=plant?FP_RIPE:-1;}
  }
 }
 if(best<0)return -1;
 if(best==start)return o->dir==facing?FP_DO:facing;
 return move_path(a,o,s,best);
}
/* ---- shelters: sealable rooms ----
 * The shelter policy is the program's (queries, exports/supply.txt):
 *   shelter_tables      tables and furnaces stand as walls (and may be placed as walls)
 *   shelter_pond        water stands as a wall (no stone is placed on it)
 *   shelter_base        a cell the room must enclose, when within shelter_base_reach path cost (-1: none)
 *   shelter_kd .. _wpen room cost: kd*path + kn*walls + kr*risk + kw*water distance
 *                       + (water distance < wnear ? (wnear - water distance)*wpen : 0)
 *   shelter_bonus_cell  a cell whose enclosure lowers the cost by shelter_bonus (-1: none) */
/* A 2x2 room permits turns by moving between its interior cells. Every side
 * can therefore be sealed without an unavailable rotate-in-place action. */
static void wall_position(int i,int w,int h,int *r,int *c,int *d) {
 if(i<h){*r=i;*c=-1;*d=1;}
 else if(i<2*h){*r=i-h;*c=w;*d=2;}
 else if(i<2*h+w){*r=-1;*c=i-2*h;*d=3;}
 else {*r=h;*c=i-2*h-w;*d=4;}
}
/* Walls still to place around the w x h room at anchor (-1: no such room). */
static int room_walls(FPAgent *a,const FPObservation *o,FPScratch *s,int anchor,int w,int h,int tables,int pond) {
 int r=anchor/FP_SIDE,c=anchor%FP_SIDE,n=0;
 for(int rr=r;rr<r+h;rr++)for(int cc=c;cc<c+w;cc++) {
  int b=block(a,rr,cc);
  if(!inside(rr,cc)||(!walkable(b) && !(w*h==2 && mineable(o,b)))||mobs_of(s)[cell(rr,cc)])return -1;
 }
 for(int i=0;i<2*(w+h);i++) {
  int wr,wc,d;wall_position(i,w,h,&wr,&wc,&d);
  int rr=r+wr,cc=c+wc;if(!inside(rr,cc))return -1;
  int b=block(a,rr,cc);
  if(b==UNKNOWN)return -1;
  /* Mob projectiles destroy tables and furnaces. */
  if(!tables && (b==FP_TABLE || b==FP_FURNACE))return -1;
  if(b==FP_WATER && pond)continue;
  if(walkable(b)||b==FP_WATER||b==FP_LAVA) {
   if((d<=2 && w==1)||(d>=3 && h==1))return -1;
   if(!place_valid(a,s,cell(rr,cc),FP_PLACE_STONE))return -1;n++;
  }
 }
 return n;
}
static int chamber_walls(FPAgent *a,const FPObservation *o,FPScratch *s,int anchor,int w,int h) {
 return room_walls(a,o,s,anchor,w,h,tr_query(a,o,s,TRQ_SHELTER_TABLES),tr_query(a,o,s,TRQ_SHELTER_POND));
}
static int room_has(int anchor,int w,int h,int p) {
 int r=p/FP_SIDE-anchor/FP_SIDE,c=p%FP_SIDE-anchor%FP_SIDE;
 return r>=0 && r<h && c>=0 && c<w;
}
/* Shelter: keep the recovery room while it stays valid and near, else pick
 * the cheapest sealable 2x2, 1x2 or 2x1 room within 350 path cost (one that
 * encloses the base first), bring the stone, step in, seal it, then sleep to
 * full energy and wait to full health (and the nine mana of an enchant). */
static int recover(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int start=cell(a->r,a->c),best=a->recovery_goal;
 int tables=tr_query(a,o,s,TRQ_SHELTER_TABLES),pond=tr_query(a,o,s,TRQ_SHELTER_POND);
 int w=a->recovery_width,h=a->recovery_height,base=tr_query(a,o,s,TRQ_SHELTER_BASE);
 if(base>=0 && !path_near(a,s,base,tr_query(a,o,s,TRQ_SHELTER_BASE_REACH)))base=-1;
 if(base>=0 && best>=0 && !room_has(best,w,h,base))best=-1;
 if(best<0 || room_walls(a,o,s,best,w,h,tables,pond)<0 || !path_near(a,s,best,350)) {
  best=-1;int costbest=INF;const FPFloor *f=&a->floors[a->level];paths_within(a,s,350);
  water_distances(f,s);
  int kd=tr_query(a,o,s,TRQ_SHELTER_KD),kn=tr_query(a,o,s,TRQ_SHELTER_KN),kr=tr_query(a,o,s,TRQ_SHELTER_KR);
  int kw=tr_query(a,o,s,TRQ_SHELTER_KW),wnear=tr_query(a,o,s,TRQ_SHELTER_WNEAR),wpen=tr_query(a,o,s,TRQ_SHELTER_WPEN);
  int bcell=tr_query(a,o,s,TRQ_SHELTER_BONUS_CELL),bonus=tr_query(a,o,s,TRQ_SHELTER_BONUS);
  for(int pass=base>=0?0:1;pass<2 && best<0;pass++)
  for(int p=f->known_lo;p<=f->known_hi;p++) {
   if(s->dist[p]>350)continue;
   int water_distance=s->prefix[p/FP_SIDE][p%FP_SIDE];
   for(int mode=0;mode<3;mode++) {
    int ww=mode==1?1:2,hh=mode==2?1:2;
    if(!pass && !room_has(p,ww,hh,base))continue;
    int n=room_walls(a,o,s,p,ww,hh,tables,pond);if(n<0)continue;
    /* Healing can require another drink. Short hydration trips reduce
     * exposure to enemies while health is still low. */
    int cost=kd*s->dist[p]+n*kn+wm_risk(a,o,s,p,!a->drinking)*kr+water_distance*kw+
             (water_distance<wnear?(wnear-water_distance)*wpen:0);
    if(bcell>=0 && room_has(p,ww,hh,bcell))cost-=bonus;
    if(cost<costbest){best=p;costbest=cost;w=ww;h=hh;}
   }
  }
 }
 if(best<0)return o->energy<=1?FP_SLEEP:-1;
 a->recovery_goal=best;a->recovery_width=w;a->recovery_height=h;
 int n=room_walls(a,o,s,best,w,h,tables,pond);
 if(n>o->inv[1]+(o->inv[0]>4 && tables?(o->inv[0]-4)/2:0)) {
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
  if(b==FP_WATER && pond)continue;
  int stand=cell(rr-dr[d],cc-dc[d]),partner=cell(rr-2*dr[d],cc-2*dc[d]);
  int action=o->inv[1]?FP_PLACE_STONE:o->inv[0]>=6 && walkable(b) && tables?FP_PLACE_TABLE:-1;
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
/* Cheapest stand next to remembered water on this floor (path cost, INF if none). */
static int water_cost(const FPAgent *a,FPScratch *s) {
 paths_all(a,s);
 const FPFloor *f=&a->floors[a->level];int best=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_WATER || f->map[p]==FP_FOUNTAIN)
  for(int d=1;d<=4;d++){int r=p/FP_SIDE-dr[d],c=p%FP_SIDE-dc[d];if(inside(r,c) && s->dist[cell(r,c)]<best)best=s->dist[cell(r,c)];}
 return best;
}
static int sealed_in(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!a->recovering || a->recovery_goal<0)return 0;
 int rr=a->recovery_goal/FP_SIDE,cc=a->recovery_goal%FP_SIDE;
 return a->r>=rr && a->r<rr+a->recovery_height && a->c>=cc && a->c<cc+a->recovery_width &&
        chamber_walls(a,o,s,a->recovery_goal,a->recovery_width,a->recovery_height)==0;
}

/* ---- generic helpers for TR programs (exports/supply.txt) ---- */
/* Scratch write view: prefix[r][c]=v (integral images built by a program). */
static void prefix_put(FPScratch *s,int r,int c,int v) { s->prefix[r][c]=(uint16_t)v; }
/* Window of the in-map part of a fully lit floor l (fire_bounds). */
static void fire_bounds_at(const FPAgent *a,int l,int *top,int *bottom,int *left,int *right) {
 fire_bounds(&a->floors[l],top,bottom,left,right);
}
/* Hit damage (wm_hit_damage) with dstr more points of strength. */
static float wm_hit_damage_str(const FPObservation *o,int dstr,int type,int ranged) {
 FPObservation u=*o;u.str+=dstr;return wm_hit_damage(&u,type,ranged);
}
static float tr_ceilf(float x) { return ceilf(x); }
