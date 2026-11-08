#define RESPONSE_MODE 2
/* A response option owns a short interval after evidence of an unseen
 * attacker. Its sensing, cover and movement children can be refined apart. */
#ifndef RESPONSE_MODE
#define RESPONSE_MODE 1
#endif
static int rs_exposure(const FPAgent *a,int r,int c) {
 int count=0;
 for(int d=1;d<=4;d++)for(int k=1;k<=5;k++) {
  int rr=r+k*dr[d],cc=c+k*dc[d],b=block(a,rr,cc);
  if(solid(b) || b==FP_BOUNDARY || b==FP_INVALID)break;
  if(k>=2 && (b==UNKNOWN || b==FP_WATER))count++;
 }
 return count;
}
static int rs_light(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!o->inv[FP_I_TORCH])return -1;
 int best=-1;float bv=0;
 for(int act=1;act<=4;act++) {
  if(!known_direction(a,act))continue;
  int r=a->r,c=a->c;
  if(act!=o->dir) {
   int nr=r+dr[act],nc=c+dc[act];
   if(inside(nr,nc) && walkable(block(a,nr,nc)) && !mobs_of(s)[cell(nr,nc)]){r=nr;c=nc;}
  }
  int rr=r+dr[act],cc=c+dc[act];
  if(!inside(rr,cc) || !place_valid(a,s,cell(rr,cc),FP_PLACE_TORCH))continue;
  int gain=0;
  for(int y=-3;y<=3;y++)for(int x=-3;x<=3;x++)
   if(y*y+x*x<=12 && block(a,rr+y,cc+x)==UNKNOWN)gain++;
  int action=act==o->dir?FP_PLACE_TORCH:act;
  float damage=wm_incoming_damage(a,o,s,action);
  if(damage>=o->health-1)continue;
  float value=gain-5*damage-(action==FP_PLACE_TORCH?0:2);
  if(gain>=5 && value>bv){bv=value;best=action;}
 }
 return best;
}
static int rs_cover(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!o->inv[FP_I_STONE] || wm_incoming_damage(a,o,s,FP_NOOP)>0)return -1;
 int r=a->r+dr[o->dir],c=a->c+dc[o->dir],here=cell(a->r,a->c);
 if(!inside(r,c) || !place_valid(a,s,cell(r,c),FP_PLACE_STONE))return -1;
 int p=cell(r,c);const FPFloor *f=&a->floors[a->level];
 if(p==f->up || p==f->down || here==f->up || here==f->down)return -1;
 for(int j=0;j<wm_shot_count(a);j++) {
  int q=wm_shot_cell(a,j),hd=wm_shot_heading(a,j);
  for(int k=2;k<=4;k++)if(q==cell(a->r+k*dr[o->dir],a->c+k*dc[o->dir]) &&
      hd && dr[hd]==-dr[o->dir] && dc[hd]==-dc[o->dir])return FP_PLACE_STONE;
 }
 return -1;
}
static int rs_escape(FPAgent *a,const FPObservation *o,FPScratch *s,int event) {
 int here=cell(a->r,a->c);
 if(event>a->rs_event) {
  a->rs_event=event;a->rs_target=0;a->rs_until=a->t+8;
  float bv=(float)rs_exposure(a,a->r,a->c)-1;paths_within(a,s,60);
  for(int y=-3;y<=3;y++)for(int x=-3;x<=3;x++) {
   int r=a->r+y,c=a->c+x;
   if(!inside(r,c) || !walkable(block(a,r,c)))continue;
   int p=cell(r,c),d=s->first[p];
   if(p==here || mobs_of(s)[p] || s->dist[p]>60 || d<1 || d>4 ||
      wm_incoming_damage(a,o,s,d)>0)continue;
   float value=rs_exposure(a,r,c)+0.05f*s->dist[p]+4*wm_melee_threat_at(a,o,r,c);
   if(value<bv){bv=value;a->rs_target=p+1;}
  }
 }
 if(!a->rs_target || a->t>a->rs_until)return -1;
 int p=a->rs_target-1;
 if(p==here){a->rs_target=0;return -1;}
 paths_within(a,s,80);
 int d=s->first[p];
 if(s->dist[p]>80 || d<1 || d>4 || wm_incoming_damage(a,o,s,d)>0) {
  a->rs_target=0;return -1;
 }
 return d;
}
static int r_r3_response(FPAgent *a,const FPObservation *o,const FPConfig *cfg,
                          FPScratch *s,FPRuleCtx *ctx,int *skill) {
 (void)cfg;(void)ctx;
 if(a->level!=5 || o->sleeping || o->resting || wm_melee_within(a,2))return -1;
 int event=a->unseen_hit>a->troll_slime_t?a->unseen_hit:a->troll_slime_t,act=-1;
 if(RESPONSE_MODE&2) {
  act=rs_cover(a,o,s);if(act>=0)RET(act,FP_FIGHT);
 }
 if(event<=0 || a->t-event>8)return -1;
 if(RESPONSE_MODE&4) {
  act=rs_escape(a,o,s,event);if(act>=0)RET(act,FP_TRAVEL);
 }
 if(RESPONSE_MODE&1) {
  act=rs_light(a,o,s);if(act>=0)RET(act,FP_LIGHT);
 }
 return -1;
}
