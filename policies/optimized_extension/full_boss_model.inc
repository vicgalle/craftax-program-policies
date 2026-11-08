/* Necromancer submodel, round three: public wave and exploration progress,
 * finite search-repair option, and regeneration-aware potion resources.
 * No simulator state or evaluation seed is available to this module. */
#define BM_R3 1
#ifndef BM_SEARCH
#define BM_SEARCH 2
#endif
#ifndef BM_STALL
#define BM_STALL 2000
#endif
#ifndef BM_GAIN
#define BM_GAIN 4
#endif
#ifndef BM_HEAL
#define BM_HEAL 1
#endif
#ifndef BM_IDENTIFY
#define BM_IDENTIFY 1
#endif

static void bm_observe(FPAgent *a,const FPObservation *o,const FPScratch *s) {
 if(o->level!=8)return;
 if(!a->bm_init || a->bm_wave!=a->boss_hits) {
  a->bm_init=1;a->bm_wave=a->boss_hits;a->bm_wave_t=a->bm_progress_t=a->t;
  a->bm_active=0;
 }
 int novel=0;
 for(int i=0;i<s->visible_count;i++) {
  int p=s->visible_cells[i];unsigned char bit=(unsigned char)(1u<<(p&7));
  if(!(a->bm_seen[p>>3]&bit)){a->bm_seen[p>>3]|=bit;novel++;}
 }
 if(novel){a->bm_progress_t=a->t;a->bm_active=0;}
}
static int bm_regenerating(const FPObservation *o) {
 return o->food>0 && o->drink>0 && o->energy>0;
}
static int bm_safe_window(const FPAgent *a,const FPObservation *o) {
 return o->level==8 && o->boss_vulnerable && wm_shot_count(a)==0 && bm_regenerating(o);
}
/* Preserve known healing during an empty, regenerating arena. */
static int bm_healing_needed(const FPAgent *a,const FPObservation *o,FPScratch *s) {
 (void)s;
 if(o->level==8 && bm_safe_window(a,o))return 0;
 return o->health<=6;
}
/* Identify duplicate unknown colours only while recovery is assured and
 * the health deficit makes healing observable. A spare bottle then remains
 * available for the later fight if the experiment discovers healing. */
static int bm_prepare(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!BM_IDENTIFY || !bm_safe_window(a,o) || o->energy<=3 || o->health<7 ||
    o->health>=8+o->str-0.5f || wm_incoming_damage(a,o,s,FP_NOOP)>0)return -1;
 for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==0)return -1;
 for(int i=0;i<6;i++)if(o->potions[i]>=(BM_IDENTIFY==1?2:1) && a->potion_effect[i]<0)
  return FP_POTION_RED+i;
 return -1;
}
static int bm_search(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(o->level!=8)return -1;
 int since=a->bm_progress_t;
 if(!a->bm_active && a->t-since<=BM_STALL)return -1;
 a->bm_active=1;
 return light_unseen(a,o,s,BM_GAIN,INF/2);
}

/* Wave-five child: route to a clear firing line rather than insisting on a
 * sword stand beside a Deep Thing's grave. Visible targets only. */
#ifndef BM_SNIPE_EARLY
#define BM_SNIPE_EARLY 1
#endif
static int bm_snipe(FPAgent *a,const FPObservation *o,FPScratch *s,int aligned_only) {
 if(o->level!=8 || a->t-a->bm_progress_t<=2000 || o->boss_vulnerable || (aligned_only && !BM_SNIPE_EARLY) || a->shot_count>=3)return -1;
 int best=-1,want=0,cast=-1,costbest=INF,here=cell(a->r,a->c);
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];if(!e.kind || e.type!=6)continue;
  int acts[3]={FP_SHOOT,FP_FIREBALL,FP_ICEBALL},codes[3]={5,3,4},shot=-1;
  float damage=0;
  for(int j=0;j<3;j++) {
   if(j==0?(!o->bow || !o->inv[FP_I_ARROW]):(o->mana<2 || !o->spells[j-1]))continue;
   float hit=wm_projectile_damage(o,codes[j],e.type,e.kind);
   if(hit>damage){damage=hit;shot=acts[j];}
  }
  if(shot<0 || damage<e.health)continue;
  for(int d=1;d<=4;d++)for(int k=1;k<=5;k++) {
   int r=e.p/FP_SIDE-k*dr[d],c=e.p%FP_SIDE-k*dc[d];
   if(!inside(r,c) || solid(block(a,r,c)) || block(a,r,c)==UNKNOWN)break;
   int p=cell(r,c);if(k<2 || !walkable(block(a,r,c)) || (s->mobs[p] && p!=here))continue;
   if(aligned_only && (p!=here || o->dir!=d))continue;
   int cost=p==here?0:s->dist[p];
   cost+=o->dir==d && p==here?0:10;
   if(cost<costbest){best=p;want=d;cast=shot;costbest=cost;a->target=e.p;}
  }
 }
 if(best<0 || costbest>=INF)return -1;
 if(best==here) {
  int action=o->dir==want?cast:want;
  if(wm_incoming_damage(a,o,s,action)<o->health-2)return action;
  return -1;
 }
 return move_path(a,o,s,best);
}
