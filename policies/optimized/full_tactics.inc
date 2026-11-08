/* full_tactics.inc - model-predictive combat shield (planner module).
 *
 * The program's tactical rules look one step ahead. Deaths in the Troll Mines,
 * Fire and Ice come from situations a few steps deep (d62 Troll entries, 455
 * deaths before floor 8 in 800 continuations): 188 final blows were projectiles,
 * 102 hits point-blank from a ready ranged mob at distance 1 (deep things in
 * their pools fire when they cannot back away) and 161 from shots already in
 * flight; most melee deaths followed a low-health retreat that a pursuing troll
 * or pigman cornered a few steps later.
 *
 * This module simulates the local fight with the world model's mechanics and
 * vetoes the program's action only when the simulation predicts that it leads
 * to death much more often than an alternative does (a safety shield). The
 * program's strategy is kept: ladders, spells, potions and every non-combat
 * goal are untouched, and alternatives are restricted to NOOP, the four moves
 * and a strike.
 *
 * Model functions (marked model) are pure functions of WM beliefs and can move
 * into full_wm.inc:
 *   tx_init        local state from beliefs: player, tracked enemies (health,
 *                  cooldown; ranged cooldowns inferred by the WM), the WM's
 *                  projectile tracks (wm_shot_cell/_code/_heading, including
 *                  shots dead-reckoned through darkness), our own missiles.
 *                  arrow_next is not read: it now also holds point-blank cells
 *   tx_step        one engine step: our action, then melee mobs (attack iff
 *                  adjacent after our move and cooldown <= 0, then cooldown 5;
 *                  otherwise approach along the larger offset with p=.75 when
 *                  closer than 10, else a random step), ranged mobs (fire iff
 *                  cooldown <= 0 and at distance 4-5, or within 3 and unable to
 *                  back away; the shot leaves along the larger axis and moves at
 *                  once), mob projectiles (straight, hit on their old or new
 *                  cell), our projectiles
 *   (projectile damage and ranged projectile types come from the WM)
 * The policy part is the rollout continuation (tx_rollout_action, an evasive
 * fighter: avoid every avoidable hit, strike when the faced mob cannot answer)
 * and the shield decision (tx_shield). */

#ifndef TX_ROLLOUTS
#define TX_ROLLOUTS 24
#endif
#ifndef TX_HORIZON
#define TX_HORIZON 8
#endif
/* Veto when the program's action dies this much more often (fraction of rollouts). */
#ifndef TX_MARGIN
#define TX_MARGIN 0.25f
#endif
#ifndef TX_MIN_RISK
#define TX_MIN_RISK 0.30f
#endif
/* Levels the shield acts on (bit mask over 0..7). */
#ifndef TX_LEVELS
#define TX_LEVELS 0xFE
#endif
/* Model ranged-mob shots fired during the simulation. */
#ifndef TX_RANGED
#define TX_RANGED 1
#endif
/* Extra shield alternatives (work/combat prototype): bit 1 place a stone in the faced cell, bit 2 take the
 * ladder we stand on (up; down only to a floor already seen). */
#ifndef TX_ALT2
#define TX_ALT2 3
#endif

#ifdef TX_STATS
long tx_stat[8];
#define TX_COUNT(i) (tx_stat[i]++)
#else
#define TX_COUNT(i) ((void)0)
#endif
enum { TX_MOBS=8, TX_SHOTS=10, TX_STONES=4 };
typedef struct { int r,c,type,kind,cd;float hp; } TxMob;
typedef struct { int r,c,dr,dc,code,mine;float dmg; } TxShot;
typedef struct {
 int r,c,dir,dead,escaped,nm,np,ns,mana,arrows;float hp,taken,mana_clock;
 TxMob m[TX_MOBS];TxShot p[TX_SHOTS];int stone[TX_STONES];
} TxState;

/* ---- model: rules ---- */
static int tx_stone(const TxState *x,int r,int c) {
 for(int i=0;i<x->ns;i++)if(x->stone[i]==cell(r,c))return 1;
 return 0;
}
static int tx_mob_at(const TxState *x,int r,int c) {
 for(int i=0;i<x->nm;i++)if(x->m[i].r==r && x->m[i].c==c)return i;
 return -1;
}
/* The player walks only on known walkable ground. */
static int tx_player_ok(const FPAgent *a,const TxState *x,int r,int c) {
 return inside(r,c) && walkable(block(a,r,c)) && !tx_stone(x,r,c) && tx_mob_at(x,r,c)<0;
}
/* Mob collision (engine table): deep things swim only, elementals cross
 * water and lava, lizards swim, everything else walks. Unknown cells are
 * open to mobs (pessimistic: dark mines hide open ground). */
static int tx_mob_ok(const FPAgent *a,const TxState *x,const TxMob *m,int r,int c) {
 if(!inside(r,c) || tx_stone(x,r,c) || tx_mob_at(x,r,c)>=0 || (r==x->r && c==x->c))return 0;
 int b=block(a,r,c);
 if(b==FP_BOUNDARY || b==FP_INVALID)return 0;
 if(b==UNKNOWN)return 1;
 if(solid(b))return 0;
 int water=b==FP_WATER,lava=b==FP_LAVA;
 if(m->kind) {
  if(m->type==6)return water;               /* deep thing */
  if(m->type==7 || m->type==8)return 1;      /* elementals */
  return !water && !lava;
 }
 if(m->type==4)return !lava;                 /* lizard */
 return !water && !lava;
}
/* Projectile damage and the projectile a ranged type fires: the WM's
 * ranged-mob model (wm_projectile_hit, wm_ranged_projectile). */
static float tx_mob_shot(const FPObservation *o,int code) { return wm_projectile_hit(o,code); }
static int tx_ranged_code(int type) { int c=wm_ranged_projectile(type);return c?c:1; }
static int tx_sign(int v) { return v>0?1:v<0?-1:0; }

/* ---- model: state from beliefs ---- */
static void tx_init(const FPAgent *a,const FPObservation *o,const FPScratch *s,TxState *x) {
 memset(x,0,sizeof *x);
 x->r=a->r;x->c=a->c;x->dir=o->dir;x->hp=o->health;
 x->mana=o->mana;x->arrows=o->inv[FP_I_ARROW];
 for(int i=0;i<a->enemy_count && x->nm<TX_MOBS;i++) {
  FPEnemy e=a->enemies[i];int er=e.p/FP_SIDE,ec=e.p%FP_SIDE;
  if(abs(er-a->r)+abs(ec-a->c)>12)continue;
  x->m[x->nm++]=(TxMob){er,ec,e.type,e.kind,e.cooldown,e.health};
 }
 /* Mob projectiles: the WM's tracks (position, code, heading; carried
  * through darkness). */
 for(int j=0;j<wm_shot_count(a) && x->np<TX_SHOTS;j++) {
  int q=wm_shot_cell(a,j),r=q/FP_SIDE,c=q%FP_SIDE,hd=wm_shot_heading(a,j),hr=dr[hd],hc=dc[hd],code=wm_shot_code(a,j);
  if(!hd) {
   /* Unknown heading: a shot on our row or column is assumed to come at us. */
   hr=hc=0;
   if(r==a->r && c!=a->c)hc=tx_sign(a->c-c);
   else if(c==a->c && r!=a->r)hr=tx_sign(a->r-r);
  }
  x->p[x->np++]=(TxShot){r,c,hr,hc,code,0,tx_mob_shot(o,code)};
 }
 /* Our own missiles in flight (headings retained by the WM). */
 for(int j=0;j<a->shot_count && x->np<TX_SHOTS;j++) {
  int d=a->shot_dir[j];if(!d)continue;
  x->p[x->np++]=(TxShot){a->shot_pos[j]/FP_SIDE,a->shot_pos[j]%FP_SIDE,dr[d],dc[d],a->shot_type[j],1,0};
 }
}

/* Deterministic uniform stream for one rollout (common random numbers). */
static float tx_u(uint64_t *z) {
 uint64_t v=(*z+=UINT64_C(0x9e3779b97f4a7c15));
 v=(v^(v>>30))*UINT64_C(0xbf58476d1ce4e5b9);v=(v^(v>>27))*UINT64_C(0x94d049bb133111eb);v^=v>>31;
 return (float)(v>>40)*(1.0f/16777216.0f);
}
static void tx_hit_mob(TxState *x,int i,float dmg) {
 x->m[i].hp-=dmg;
 if(x->m[i].hp<=0){x->m[i]=x->m[--x->nm];}
}
/* Player missile damage against a mob. */
static float tx_missile(const FPObservation *o,const TxShot *p,const TxMob *m) {
 return wm_projectile_damage(o,p->code,m->type,m->kind);
}

/* A bounded resource model for the local simulator. The hidden regeneration
 * phase starts at zero, giving a conservative mana lower bound. Only the
 * late-floor child uses it; the incumbent prefix is unchanged. */
static int tx_can_cast(const FPObservation *o,const TxState *x,int action) {
 int own=0;for(int i=0;i<x->np;i++)own+=x->p[i].mine;
 if(x->np>=TX_SHOTS || own>=3)return 0;
 if(action==FP_SHOOT)return o->bow && x->arrows>0;
 return x->mana>=2 && ((action==FP_FIREBALL && o->spells[0]) ||
                      (action==FP_ICEBALL && o->spells[1]));
}

/* ---- model: one engine step ---- */
static void tx_step(const FPAgent *a,const FPObservation *o,TxState *x,int action,uint64_t *z) {
 /* Our action. */
 if(action>=FP_LEFT && action<=FP_DOWN) {
  x->dir=action;int rr=x->r+dr[action],cc=x->c+dc[action];
  if(tx_player_ok(a,x,rr,cc)){x->r=rr;x->c=cc;}
 } else {
  int fr=x->r+dr[x->dir],fc=x->c+dc[x->dir];
  if(action==FP_DO) {
   int i=tx_mob_at(x,fr,fc);if(i>=0)tx_hit_mob(x,i,wm_hit_damage(o,x->m[i].type,x->m[i].kind));
  } else if((action==FP_FIREBALL || action==FP_ICEBALL || action==FP_SHOOT) && x->np<TX_SHOTS &&
            (o->level>=5?tx_can_cast(o,x,action):(inside(fr,fc) && !solid(block(a,fr,fc))))) {
   int code=action==FP_FIREBALL?3:action==FP_ICEBALL?4:5;
   /* Native shots spawn at the player and advance in the projectile phase. */
   x->p[x->np++]=(TxShot){o->level>=5?x->r:fr,o->level>=5?x->c:fc,dr[x->dir],dc[x->dir],code,1,0};
   if(o->level>=5){if(action==FP_SHOOT)x->arrows--;else x->mana-=2;}
  } else if(action==FP_PLACE_STONE && x->ns<TX_STONES && inside(fr,fc) && tx_mob_at(x,fr,fc)<0 &&
            (walkable(block(a,fr,fc)) || block(a,fr,fc)==FP_WATER || block(a,fr,fc)==FP_LAVA)) {
   x->stone[x->ns++]=cell(fr,fc);
  } else if((action==FP_ASCEND && a->floors[a->level].up==cell(x->r,x->c)) ||
            (action==FP_DESCEND && a->floors[a->level].down==cell(x->r,x->c))) {
   x->escaped=1;return;
  }
 }
 float taken=0;
 /* Melee mobs. Every mob draws four uniforms each step (aligned streams). */
 for(int i=0;i<x->nm;i++) {
  float u0=tx_u(z),u1=tx_u(z),u2=tx_u(z),u3=tx_u(z);
  TxMob *m=&x->m[i];if(m->kind)continue;
  int vr=x->r-m->r,vc=x->c-m->c,d=abs(vr)+abs(vc);
  if(d==1 && m->cd<=0){taken+=wm_attack_damage(o,m->type);m->cd=5;continue;}
  m->cd--;
  int pr=m->r,pc=m->c;
  if(d<10 && u0<0.75f) {
   int row=abs(vr)>abs(vc) || (abs(vr)==abs(vc) && u1<0.5f);
   if(row)pr+=tx_sign(vr);else pc+=tx_sign(vc);
  } else {int k=1+(int)(u2*4);if(k>4)k=4;pr+=dr[k];pc+=dc[k];}
  (void)u3;
  if((pr!=m->r || pc!=m->c) && tx_mob_ok(a,x,m,pr,pc)){m->r=pr;m->c=pc;}
 }
 /* Ranged mobs: shots leave from the mob's cell along its larger axis. */
 for(int i=0;i<x->nm;i++) {
  float u0=tx_u(z),u1=tx_u(z),u2=tx_u(z),u3=tx_u(z);
  TxMob *m=&x->m[i];if(!m->kind)continue;
  int vr=x->r-m->r,vc=x->c-m->c,d=abs(vr)+abs(vc);
  int row=abs(vr)>abs(vc) || (abs(vr)==abs(vc) && u1<0.5f);
  int hr=row?tx_sign(vr):0,hc=row?0:tx_sign(vc);
  int k=1+(int)(u0*4);if(k>4)k=4;
  int pr=m->r+dr[k],pc=m->c+dc[k];
  if(d>=6 && u2>0.85f){pr=m->r+hr;pc=m->c+hc;}
  if(d<=3 && u2>0.85f){pr=m->r-hr;pc=m->c-hc;}
  int valid=tx_mob_ok(a,x,m,pr,pc);
  int attack=TX_RANGED && m->cd<=0 && ((d>=4 && d<=5) || (d<=3 && !valid));
  (void)u3;
  if(attack && x->np<TX_SHOTS) {
   x->p[x->np++]=(TxShot){m->r,m->c,hr,hc,tx_ranged_code(m->type),0,tx_mob_shot(o,tx_ranged_code(m->type))};
   m->cd=4;continue;
  }
  if(attack){m->cd=4;continue;}
  m->cd--;
  if(valid){m->r=pr;m->c=pc;}
 }
 /* Projectiles, mob shots first. A shot hits on its old or its new cell. */
 for(int pass=0;pass<2;pass++)for(int i=0;i<x->np;i++) {
  TxShot *p=&x->p[i];if(p->mine!=pass)continue;
  int nr=p->r+p->dr,nc=p->c+p->dc,gone=0;
  if(!pass) {
   if((p->r==x->r && p->c==x->c) || (nr==x->r && nc==x->c)){taken+=p->dmg;gone=1;}
   else if(!p->dr && !p->dc)gone=1;
   else if(!inside(nr,nc) || (solid(block(a,nr,nc))) || block(a,nr,nc)==FP_BOUNDARY || tx_stone(x,nr,nc) || tx_mob_at(x,nr,nc)>=0)gone=1;
  } else {
   int j=tx_mob_at(x,p->r,p->c);
   if(j<0)j=tx_mob_at(x,nr,nc);
   if(j>=0){tx_hit_mob(x,j,tx_missile(o,p,&x->m[j]));gone=1;}
   else if(!inside(nr,nc) || solid(block(a,nr,nc)) || block(a,nr,nc)==FP_BOUNDARY || tx_stone(x,nr,nc))gone=1;
  }
  if(gone){x->p[i]=x->p[--x->np];i--;continue;}
  p->r=nr;p->c=nc;
 }
 if(o->level>=5) {
  x->mana_clock=(x->mana_clock+1)*(1+0.25f*(o->intelligence-1));
  if(x->mana_clock>30){x->mana=imin(x->mana+1,6+3*o->intelligence);x->mana_clock=0;}
 }
 x->taken+=taken;x->hp-=taken;
 if(x->hp<=0)x->dead=1;
}

/* Damage this step if we take `action` now: hits that are certain under the
 * current state (ready adjacent melee, shots on our path, ready ranged mobs
 * point-blank and unable to retreat is approximated as certain). */
static float tx_immediate(const FPAgent *a,const FPObservation *o,const TxState *x,int action) {
 int r=x->r,c=x->c,dir=x->dir,kill=-1;
 if(action>=FP_LEFT && action<=FP_DOWN){dir=action;if(tx_player_ok(a,x,r+dr[action],c+dc[action])){r+=dr[action];c+=dc[action];}}
 if(action==FP_DO) {
  int i=tx_mob_at(x,x->r+dr[dir],x->c+dc[dir]);
  if(i>=0 && wm_hit_damage(o,x->m[i].type,x->m[i].kind)>=x->m[i].hp)kill=i;
 }
 float dmg=0;
 for(int i=0;i<x->nm;i++) {
  const TxMob *m=&x->m[i];if(i==kill || m->cd>0)continue;
  int d=abs(m->r-r)+abs(m->c-c);
  if(!m->kind && d==1)dmg+=wm_attack_damage(o,m->type);
  if(TX_RANGED && m->kind && d==1)dmg+=tx_mob_shot(o,tx_ranged_code(m->type));
 }
 for(int i=0;i<x->np;i++) {
  const TxShot *p=&x->p[i];if(p->mine)continue;
  if((p->r==r && p->c==c) || (p->r+p->dr==r && p->c+p->dc==c))dmg+=p->dmg;
 }
 return dmg;
}
/* Rollout continuation: an evasive fighter. Take the action with the least
 * certain damage; among equals strike a faced mob, else turn to an adjacent
 * mob that is cooling down, else keep the distance from melee mobs. */
static int tx_rollout_action(const FPAgent *a,const FPObservation *o,const TxState *x) {
 int best=FP_NOOP;float bs=1e9f;
 int fr=x->r+dr[x->dir],fc=x->c+dc[x->dir],faced=tx_mob_at(x,fr,fc);
 for(int act=FP_NOOP;act<=FP_DO;act++) {
  if(act==FP_DO && faced<0)continue;
  float sc=10*tx_immediate(a,o,x,act);
  if(act==FP_DO)sc-=3;
  if(act>=FP_LEFT && act<=FP_DOWN) {
   int rr=x->r+dr[act],cc=x->c+dc[act],j=tx_mob_at(x,rr,cc);
   if(j>=0 && !x->m[j].kind && x->m[j].cd>0 && act!=x->dir)sc-=2;
   if(!tx_player_ok(a,x,rr,cc)){rr=x->r;cc=x->c;}
   int near=99;for(int i=0;i<x->nm;i++)if(!x->m[i].kind){int d=abs(x->m[i].r-rr)+abs(x->m[i].c-cc);if(d<near)near=d;}
   if(near<=3)sc-=0.5f*near;
  } else {
   int near=99;for(int i=0;i<x->nm;i++)if(!x->m[i].kind){int d=abs(x->m[i].r-x->r)+abs(x->m[i].c-x->c);if(d<near)near=d;}
   if(near<=3)sc-=0.5f*near;
  }
  if(sc<bs){bs=sc;best=act;}
 }
 if(o->level>=5) {
  /* Find the first opponent along the facing. This is a continuation
   * heuristic, not a claim that a moving opponent certainly gets hit. */
  int target=-1,range=0;
  for(int k=1;k<=5;k++) {
   int r=x->r+k*dr[x->dir],c=x->c+k*dc[x->dir];
   if(!inside(r,c) || solid(block(a,r,c)) || tx_stone(x,r,c))break;
   int j=tx_mob_at(x,r,c);if(j>=0){target=j;range=k;break;}
  }
  if(target>=0) {
   const TxMob *m=&x->m[target];
   float sword=wm_hit_damage(o,m->type,m->kind);
   int actions[3]={FP_FIREBALL,FP_ICEBALL,FP_SHOOT};
   for(int i=0;i<3;i++) {
    int act=actions[i];if(!tx_can_cast(o,x,act))continue;
    int code=act==FP_FIREBALL?3:act==FP_ICEBALL?4:5;
    float hit=wm_projectile_damage(o,code,m->type,m->kind);
    if(hit<=0 || (range==1 && (sword>=m->hp || sword>=hit)))continue;
    /* A spell can finish a resistant enemy during its six-turn attack
     * cycle; the old continuation could only chip it with the sword. */
    float sc=10*tx_immediate(a,o,x,act)-3.5f;
    if(sc<bs){bs=sc;best=act;}
   }
  }
 }
 return best;
}
/* Death rate and mean damage of `first` followed by the rollout policy. */
static void tx_evaluate(const FPAgent *a,const FPObservation *o,const TxState *x0,int first,float *death,float *damage) {
 int dead=0;float dmg=0;
 for(int k=0;k<TX_ROLLOUTS;k++) {
  TxState x=*x0;uint64_t z=UINT64_C(0x5851f42d4c957f2d)*(uint64_t)(k+1)^(uint64_t)a->t*UINT64_C(0x2545F4914F6CDD1D);
  tx_step(a,o,&x,first,&z);
  for(int h=1;h<TX_HORIZON && !x.dead && !x.escaped;h++)tx_step(a,o,&x,tx_rollout_action(a,o,&x),&z);
  dead+=x.dead;dmg+=x.taken;
 }
 *death=(float)dead/TX_ROLLOUTS;*damage=dmg/TX_ROLLOUTS;
}
/* Cheap trigger: a hostile close enough to matter within the horizon. */
static int tx_engaged(const FPAgent *a,const FPObservation *o) {
 float threat=0;
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
  if(!e.kind && d<=5)threat+=wm_attack_damage(o,e.type)*2;
  if(e.kind && d<=7)threat+=tx_mob_shot(o,tx_ranged_code(e.type))*2;
 }
 for(int j=0;j<wm_shot_count(a);j++) {
  int q=wm_shot_cell(a,j);if(abs(q/FP_SIDE-a->r)+abs(q%FP_SIDE-a->c)<=4)threat+=tx_mob_shot(o,wm_shot_code(a,j));
 }
 return threat>=o->health;
}
/* The shield. Returns the action to commit. */
static int tx_shield(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int *skill) {
 if(o->sleeping || o->resting || o->level>7 || !((TX_LEVELS>>o->level)&1))return action;
 if(action>=FP_POTION_RED && action<=FP_POTION_YELLOW)return action;
 if(action==FP_ASCEND || action==FP_DESCEND || action==FP_SLEEP || action==FP_REST)return action;
 if(!tx_engaged(a,o))return action;
 TxState x0;tx_init(a,o,s,&x0);
 float pd,pm;tx_evaluate(a,o,&x0,action,&pd,&pm);TX_COUNT(0);
 if(pd<TX_MIN_RISK)return action;
 TX_COUNT(1);
 int best=-1;float bd=pd,bm=pm;
 int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir],faced=tx_mob_at(&x0,fr,fc);
 for(int alt=FP_NOOP;alt<=FP_DO;alt++) {
  if(alt==action || (alt==FP_DO && faced<0))continue;
  if(alt>=FP_LEFT && alt<=FP_DOWN && !known_direction(a,alt))continue;
  float d,m;tx_evaluate(a,o,&x0,alt,&d,&m);
  if(d<bd-1e-6f || (fabsf(d-bd)<1e-6f && m<bm)){best=alt;bd=d;bm=m;}
 }
 if(TX_ALT2&1 && o->inv[FP_I_STONE]>0 && inside(fr,fc) && faced<0 && !tx_stone(&x0,fr,fc) &&
    (walkable(block(a,fr,fc)) || block(a,fr,fc)==FP_WATER || block(a,fr,fc)==FP_LAVA) && !a->floors[a->level].items[cell(fr,fc)]) {
  float d,m;tx_evaluate(a,o,&x0,FP_PLACE_STONE,&d,&m);
  if(d<bd-1e-6f || (fabsf(d-bd)<1e-6f && m<bm)){best=FP_PLACE_STONE;bd=d;bm=m;}
 }
 if(((TX_ALT2&4) || o->level>=5) && inside(fr,fc) && !solid(block(a,fr,fc))) {
  /* a missile into the faced cell (spells need 2 mana; arrows need the bow) */
  int cast[3]={-1,-1,-1};
  if(o->spells[0] && o->mana>=2)cast[0]=FP_FIREBALL;
  if(o->spells[1] && o->mana>=2)cast[1]=FP_ICEBALL;
  if(o->bow && o->inv[FP_I_ARROW]>0)cast[2]=FP_SHOOT;
  for(int k=0;k<3;k++)if(cast[k]>=0 && cast[k]!=action) {
   float d,m;tx_evaluate(a,o,&x0,cast[k],&d,&m);
   if(d<bd-1e-6f || (fabsf(d-bd)<1e-6f && m<bm)){best=cast[k];bd=d;bm=m;}
  }
 }
 if(TX_ALT2&2) {
  int here=cell(a->r,a->c),alt=-1;
  if(a->floors[a->level].up==here && a->level>0)alt=FP_ASCEND;
  else if(a->floors[a->level].down==here && a->level<7 && a->floors[a->level+1].seen)alt=FP_DESCEND;
  if(alt>=0){float d,m;tx_evaluate(a,o,&x0,alt,&d,&m);if(d<bd-1e-6f){best=alt;bd=d;bm=m;}}
 }
 if(best>=0 && pd-bd>=TX_MARGIN) {
  TX_COUNT(2);if(action>=FP_LEFT && action<=FP_DOWN)TX_COUNT(3);if(best==FP_DO)TX_COUNT(4);if(best==FP_NOOP)TX_COUNT(5);
  if(action==FP_DO)TX_COUNT(6);if(action>=FP_FIREBALL && action<=FP_ICEBALL)TX_COUNT(7);
  *skill=FP_FIGHT;return best;
 }
 return action;
}
