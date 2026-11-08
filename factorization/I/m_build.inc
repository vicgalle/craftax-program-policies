/* m_build.inc: the building machine: crafting a recipe at a known or placed
 * table (craft_radius), harvesting walkable blocks (harvest_walkable), the
 * memory count of a block (known_count). What to build, when and where is
 * the program's (p/plan.tr and the [build] rules of p/floor8.tr). */

/* Harvest a walkable block (Ice shrubs are walkable: a turn towards an
 * adjacent one moves onto it), so establish the facing by entering its stand
 * from behind. Blocks of the current floor within radius path steps; goal is
 * the resource kind recorded in a->goal. (The program's ice_wood uses it.) */
static int harvest_walkable(FPAgent *a,const FPObservation *o,FPScratch *s,int b,int goal,int radius) {
 paths_within(a,s,radius);
 const FPFloor *f=&a->floors[a->level];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(f->map[p]!=b || mobs_of(s)[p])continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||mobs_of(s)[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d){a->target=p;a->goal=goal;return FP_DO;}
   int cost=s->dist[partner]+10;
   if(cost<=radius && cost<costbest){best=partner;dir=d;costbest=cost;a->target=p;}
  }
 }
 if(best<0)return -1;
 a->goal=goal;return best==start?dir:move_path(a,o,s,best);
}
/* Known cells of block b on the current floor (memory). */
static int known_count(const FPAgent *a,int b) {
 const FPFloor *f=&a->floors[a->level];int n=0;
 for(int p=f->known_lo;p<=f->known_hi;p++)n+=f->map[p]==b;
 return n;
}
static int craft_radius(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action,int radius) {
 const Recipe *recipe=NULL;for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++)if(recipes[i].action==action)recipe=&recipes[i];
 if(!recipe)return -1;
 const FPFloor *f=&a->floors[a->level];
 int table=-1,shared=-1,tcost=INF,scost=INF;
 for(int p=next_station(f,f->known_lo,f->known_hi);p<=f->known_hi;p=next_station(f,p+1,f->known_hi))if(near(a,p,0) && path_near(a,s,p,imin(radius,(near(a,p,recipe->furnace)?scost:tcost)-1))) {
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
   if(mobs_of(s)[p] || a->floors[a->level].marks[p].item || !solid(b) || !mineable(o,b) || b==FP_TABLE || b==FP_FURNACE)continue;
   a->target=p;return o->dir==d?FP_DO:d;
  }
  return -1;
 }
 return place(a,o,s,FP_PLACE_TABLE);
}
/* Is the Sewers ice-armour trip pending? A strategy predicate: the program's
 * def enchant_trip answers (query), for the C code of the supply and combat
 * areas that still asks. */
static int enchant_trip(const FPAgent *a,const FPObservation *o) {
 return tr_query(a,o,NULL,TRQ_ENCHANT_TRIP);
}
