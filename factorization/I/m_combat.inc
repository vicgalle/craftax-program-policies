/* m_combat.inc: the combat machine: the Fire blow model (fire_hit), per-cell
 * scratch views of the observed enemies and a greedy-approach wrapper for the
 * program, the shield's rollout simulator (full_tactics.inc, included by
 * full_policy.cpp) and the boss tracker (full_boss_model.inc). The combat
 * strategy (tactical, cb_lowflee, the unseen-attacker response, the boss-floor
 * choices and the shield's override policy) is the program's: p/combat.tr.
 * Two C copies of strategy remain for C callers: fire_retreat (lg_mid.inc)
 * and tactical (commanded() in full_goals.inc). */

/* A pigman blow and an elemental fireball both deal 3 physical + 5 fire. */
static float fire_hit(const FPObservation *o) { return wm_attack_damage(o,7); }
/* Scratch views of the enemies in the current observation, per cell, and the
 * greedy approach of a tracked enemy by index: natives for the program. */
static int melee_at(const FPScratch *s,int q) { return melee_of(s)[q]; }
static int ranged_at(const FPScratch *s,int q) { return ranged_of(s)[q]; }
static float enemy_health_at(const FPScratch *s,int q) { return enemy_health_of(s)[q]; }
static int wm_greedy_approach_i(const FPAgent *a,const FPScratch *s,int i,int avoid_mobs,int *fr,int *fc) {
 return wm_greedy_approach(a,s,a->enemies[i],avoid_mobs,fr,fc);
}
/* tactical: the combat tactics are the program's (p/combat.tr). This C copy
 * remains only for commanded() (full_goals.inc), the goal-command mode. */
static int tactical(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int start=cell(a->r,a->c);
 /* Avoid a lethal simultaneous attack. Cooldowns and health are inferred
  * from consecutive public views and our own attacks, with conservative
  * defaults when an enemy first becomes visible. */
 float incoming=wm_melee_incoming(a,o);
 if(o->level>=5) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc)) {
   int p=cell(fr,fc),type=melee_of(s)[p]?melee_of(s)[p]:ranged_of(s)[p];
   if(type && wm_hit_damage(o,type,ranged_of(s)[p]!=0)>=enemy_health_of(s)[p] &&
      wm_incoming_damage(a,o,s,FP_DO)<o->health-0.5f)return FP_DO;
  }
 }
 /* Protected Ice fights can finish during the attacker's cooldown even
  * with little health left. Keep a half-point margin for that first hit;
  * retreat before a lethal hit, rather than abandoning every low-HP cast. */
 if(incoming>=o->health-(o->level==7?0.5f:1) || (o->level!=7 && o->health<=4 && incoming>0)) {
  int best=-1;float costbest=incoming;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c)||!walkable(block(a,r,c))||mobs_of(s)[cell(r,c)]||arrows_of(s)[cell(r,c)])continue;
   float cost=wm_melee_threat_at(a,o,r,c);
   if(cost<costbest){best=d;costbest=cost;}
  }
  /* A realm chase only ends in a corner: pigmen and frost trolls keep pace
   * (ep 16 danced 23 steps at 5.2 health with full mana). The up ladder is
   * the one exit they cannot follow. Climb when on it, and prefer the first
   * step of the route to it among equally safe retreats. */
  if(a->level==4 || a->level==5 || a->level==6 || a->level==7 || (a->level==1 && !a->floors[5].seen)) {
   int up=a->floors[a->level].up,climb=FP_ASCEND;
   /* Icetrip on Fire: the Ice ladder is home and often the nearer exit. */
   if(ICETRIP_ESCAPE_NEAR && icetrip(a) && a->level==6) {
    int D=a->floors[6].down;
    if(D>=0 && path_dist(a,s,D)<INF && (up<0 || path_dist(a,s,up)>=INF || s->dist[D]<=s->dist[up])){up=D;climb=FP_DESCEND;}
   }
   if(up==start)return climb;
   if(best>=0 && up>=0 && path_dist(a,s,up)<INF && s->first[up]>0) {
    int d=s->first[up],r=a->r+dr[d],c=a->c+dc[d];
    if(inside(r,c) && walkable(block(a,r,c)) && !mobs_of(s)[cell(r,c)] && !arrows_of(s)[cell(r,c)]) {
     float cost=wm_melee_threat_at(a,o,r,c);
     if(cost<=costbest)return d;
    }
   }
  }
  if(best>=0)return best;
 }
 /* Icetrip on Fire: evade rather than trade. In ice armour a pigman blow
  * costs 6.2-6.8 of 12-13 health, and 115 of 194 melee hits on Fire in 60
  * traced worlds landed while turning towards a pigman that had just stepped
  * beside us (64 more while casting at it: spells resolve after its blow). A
  * melee mob only strikes if it starts its turn beside us, and we act first:
  * any step to a cell not beside a ready mob avoids the blow, and pigmen
  * (speed .75) fall behind a player who keeps moving. Step to such a cell,
  * preferring the route to the ladder the trip is heading for; take that
  * ladder if we stand on it (mobs never follow). */
 if(ICETRIP_EVADE && icetrip(a) && a->level==6 && incoming>0) {
  int up_bound=enchant_trip(a,o) || o->food<=2 || o->health<=4,T=up_bound?a->floors[6].up:a->floors[6].down;
  if(ICETRIP_EVADE==1 && T==start)return up_bound?FP_ASCEND:FP_DESCEND;
  /* Variant 2: keep travelling, never dither. Free evasion in any direction
   * (variant 1) circled for dozens of steps until two pigmen cornered it
   * (-9.3pp). Here only the route's next step is taken, when it is safe;
   * otherwise fight as before. Pigmen (speed .75) fall behind. */
  if(ICETRIP_EVADE==2) {
   /* Only far from the destination ladder: a pursuer left beside the ladder
    * is frozen there and strikes on the next arrival (arrival hits tripled
    * when evasion ran to the ladder). Mid-crossing it falls behind and
    * despawns 14 cells back. Near the ladder, fight it as before. */
   if(T<0 || path_dist(a,s,T)>=INF || s->dist[T]<200 || s->first[T]<=0)goto no_evade;
   int d=s->first[T],r=a->r+dr[d],c=a->c+dc[d];
   if(!inside(r,c)||!walkable(block(a,r,c))||mobs_of(s)[cell(r,c)]||arrows_of(s)[cell(r,c)])goto no_evade;
   for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)goto no_evade;}
   return d;
  }
  int best=-1,bc=INT_MAX;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];
   if(!inside(r,c)||!walkable(block(a,r,c))||mobs_of(s)[cell(r,c)]||arrows_of(s)[cell(r,c)])continue;
   int adj=0;
   for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)adj=1;}
   if(adj)continue;
   int cost=10*wm_risk(a,o,s,cell(r,c),!a->drinking)+(T>=0 && path_dist(a,s,T)<INF && s->first[T]==d?0:5);
   if(cost<bc){bc=cost;best=d;}
  }
  if(best>=0)return best;
 }
 no_evade:
 /* Boss waves are finite. Finish a visible adjacent target rather than
  * cycling forever around its nonlethal point-blank projectile threat.
  * Movement toward the occupied target turns in place; the next DO kills.
  * The shield can still veto this commitment when its rollout is unsafe. */
 if(a->level==8)for(int i=0;i<4;i++) {
  int d=1+(o->dir-1+i)%4,r=a->r+dr[d],c=a->c+dc[d];
  if(!inside(r,c))continue;
  int p=cell(r,c),type=melee_of(s)[p]?melee_of(s)[p]:ranged_of(s)[p];
  if(type && wm_hit_damage(o,type,ranged_of(s)[p]!=0)>=enemy_health_of(s)[p] &&
     wm_incoming_damage(a,o,s,o->dir==d?FP_DO:d)<o->health-2)
   return o->dir==d?FP_DO:d;
 }
 if(arrows_of(s)[start]) {
  int best=-1,costbest=INT_MAX;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int p=cell(r,c);
   if(!walkable(block(a,r,c))||mobs_of(s)[p]||arrows_of(s)[p])continue;
   int cost=wm_risk(a,o,s,p,!a->drinking);if(cost<costbest){best=d;costbest=cost;}
  }if(best>=0)return best;
 }
 for(int i=0;i<4;i++) {
  int d=1+(o->dir-1+i)%4,r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int p=cell(r,c);
  if(!(mobs_of(s)[p]&5))continue;
  if(o->dir!=d)return d;
  int type=melee_of(s)[p]?melee_of(s)[p]:ranged_of(s)[p];
  /* Sword damage resolves before a melee attack; spells resolve after it.
   * Finish a weakened elemental immediately and preserve the remaining mana. */
  if(type>=7 && wm_hit_damage(o,type,ranged_of(s)[p]!=0)>=enemy_health_of(s)[p])return FP_DO;
  if(o->mana>=2) {
   if(type==7 && o->spells[1])return FP_ICEBALL;
   if(type==8 && o->spells[0])return FP_FIREBALL;
  }
  return FP_DO;
 }
 /* Recovery navigation needs uninterrupted turns to reach and seal its
  * shelter. Adjacent attacks and projectile dodges above still interrupt. */
 if(a->level==8 && (a->recovering || o->health<=GRAVEYARD_RECOVER_HEALTH))return -1;
 /* An approaching orc survives the first iron-sword blow. An arrow or
  * spell in the faced cell before it arrives lets that blow kill it before
  * its first attack, including a diagonal approach into that cell. */
 if(a->level==1 && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  int shot=o->bow && o->inv[FP_I_ARROW]?FP_SHOOT:o->spells[0]?FP_FIREBALL:FP_ICEBALL;
  float damage=shot==FP_SHOOT?5*(1+0.2f*(o->dex-1)):3*(1+0.5f*(o->intelligence-1));
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !mobs_of(s)[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
    int df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(!e.kind && dd==2 && df==1 && wm_hit_damage(o,e.type,0)<e.health && wm_hit_damage(o,e.type,0)+damage>=e.health)return shot;
   }
 }
 /* A troll two cells away steps next into one of our neighbours and
  * attacks after our first blow, which cannot kill 20 HP. When it can step
  * into the faced cell, a spell cast now meets it there; if the spell and
  * one sword blow suffice, the blow then kills it before its first attack. */
 if((a->level==4 || a->level==5) && o->mana>=2 && (o->spells[0] || o->spells[1])) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];float spell=3*(1+0.5f*(o->intelligence-1));
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !mobs_of(s)[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];float blow=wm_hit_damage(o,e.type,0),pending=0;
    int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c),df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(e.kind || blow>=e.health)continue;
    /* A shot waiting in the faced cell hits the troll as it steps in. */
    for(int j=0;j<a->shot_count;j++)if(a->shot_pos[j]==cell(fr,fc) && a->shot_dir[j]==o->dir)
     pending+=wm_projectile_damage(o,a->shot_type[j],e.type,0);
    /* Without an enchanted sword two spells are needed: start one move
     * earlier, so the first shot waits in the faced cell. */
    if(d==3 && df==2 && o->mana>=4 && blow+spell<e.health && blow+2*spell>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
    if(d==2 && df==1 && blow+spell+pending>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
 }
 /* During an Ice excursion start iceballs into a pigman's greedy approach,
  * including a diagonal approach. The first missile waits in the faced cell;
  * it can hit on the approach turn before the pigman starts beside us. */
 if(icetrip(a) && a->level==6 && o->spells[1] && o->mana>=2 && a->shot_count<3) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !mobs_of(s)[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int vr=e.p/FP_SIDE-a->r,vc=e.p%FP_SIDE-a->c,dd=abs(vr)+abs(vc);
    if(e.kind || e.type!=7 || dd<2 || dd>3)continue;
    if(dd==2 && abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc)==1)return FP_ICEBALL;
    if(dd==3 && o->mana>=4)for(int axis=0;axis<2;axis++) {
     int nr,nc;if(!wm_greedy_next(a,vr,vc,axis,&nr,&nc))continue;
     if(abs(nr-fr)+abs(nc-fc)==1 && walkable(block(a,nr,nc)))return FP_ICEBALL;
    }
   }
 }
 /* Frost trolls resist the sword and require three fireballs. Cast into
  * their approach before adjacency, including a diagonal final step. The
  * missile persists in the faced cell until after the next mob movement. */
 /* With a fire-enchanted sword (6.2 to a frost troll, resolving before its
  * attack) two fireballs that meet the troll as it steps into the faced cell
  * leave it within one strike: no hit at all. The first must be cast a step
  * earlier, at distance three, when the troll's greedy step (larger offset
  * first) leads next to the faced cell; it waits there in flight. */
 if(a->level==7 && o->sword_enchant==1 && o->spells[0] && o->mana>=4 && a->shot_count<=1) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !mobs_of(s)[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];
    int vr=e.p/FP_SIDE-a->r,vc=e.p%FP_SIDE-a->c;
    if(e.kind || e.type!=8 || abs(vr)+abs(vc)!=3 || e.health>2*wm_projectile_damage(o,3,8,0)+wm_hit_damage(o,8,0))continue;
    for(int axis=0;axis<2;axis++) {
     int nr,nc;if(!wm_greedy_next(a,vr,vc,axis,&nr,&nc))continue;
     if(abs(nr-fr)+abs(nc-fc)==1 && walkable(block(a,nr,nc)))return FP_FIREBALL;
    }
   }
 }
 if(a->level==7 && o->spells[0] && o->mana>=2) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !mobs_of(s)[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];
    int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
    int df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(!e.kind && e.type==8 && dd==2 && df==1)return FP_FIREBALL;
   }
 }
 /* Shoot along a clear axis. Turning towards a distant target also approaches it. */
 int d=o->dir;
 for(int k=1;k<=5;k++) {
  int r=a->r+k*dr[d],c=a->c+k*dc[d];if(!inside(r,c))break;int p=cell(r,c);
  if(mobs_of(s)[p]&5) {
   int type=melee_of(s)[p]?melee_of(s)[p]:ranged_of(s)[p];
   if(o->mana>=2 && type==7 && o->spells[1])return FP_ICEBALL;
   if(o->mana>=2 && type==8 && o->spells[0])return FP_FIREBALL;
   if(o->mana>=2 && k>=2 && (o->spells[0] || o->spells[1])) {
    float spell=3*(1+0.5f*(o->intelligence-1));
    float arrow=4*(1+0.2f*(o->dex-1))*(type==5?0.5f:type>=7?0.1f:1);
    if(!o->bow || !o->inv[9] || spell>=arrow)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
   /* Dungeon orcs hit for 3 once adjacent and need two sword strikes. An
    * arrow while one approaches in line lets the first strike finish it. */
   if(o->bow && o->inv[9]>0 && k>=2 && (ranged_of(s)[p] || o->health<6 || a->level>=4 || a->level==1))return FP_SHOOT;
   break;
  }
  if(solid(block(a,r,c)) || block(a,r,c)==UNKNOWN)break;
 }
 /* Icetrip on Fire: turn to shoot a pigman approaching along our row or
  * column. A mob whose smaller offset is zero moves only along that axis,
  * so iceballs cast down the line meet it; three land before it is beside
  * us from four or more cells, and each one shortens the fight that costs a
  * 6+ point blow in ice armour. The shot above only fires along the facing.
  * Turning is free when the neighbour that way is blocked; otherwise it is a
  * step towards it, taken only from four or more cells. */
 if(ICETRIP_PFACE && icetrip(a) && a->level==6 && o->spells[1] && o->mana>=2 && a->shot_count<3) {
  int bestd=0,bk=99;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];if(e.kind || e.type!=7)continue;
   int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,k=abs(er)+abs(ec);
   if((er && ec) || k<2 || k>5 || !wm_firing_line(a,a->r,a->c,e.p/FP_SIDE,e.p%FP_SIDE))continue;
   int want=er>0?FP_DOWN:er<0?FP_UP:ec>0?FP_RIGHT:FP_LEFT;
   if(want==o->dir)continue;
   int nr=a->r+dr[want],nc=a->c+dc[want];
   if(!known_direction(a,want))continue;
   if(inside(nr,nc) && walkable(block(a,nr,nc)) && !mobs_of(s)[cell(nr,nc)] && k<4)continue;
   if(k<bk){bk=k;bestd=want;}
  }
  if(bestd)return bestd;
 }
 /* Face the orc's unobstructed greedy approach while it is still far
  * enough to turn safely. Let it enter the pre-shot cell instead of
  * walking into its reach with the wrong facing. */
 if(a->level==1 && a->dungeon_wait<24 && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  float shot=o->bow && o->inv[FP_I_ARROW]?5*(1+0.2f*(o->dex-1)):3*(1+0.5f*(o->intelligence-1));
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);float blow=wm_hit_damage(o,e.type,0);
   if(!e.kind && dd>=3 && dd<bd && blow<e.health && blow+shot>=e.health){best=i;bd=dd;}
  }
  if(best<0)a->dungeon_wait=0;
  if(best>=0) {
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,clear=wm_greedy_approach(a,s,e,0,NULL,NULL);
   if(clear) {
    if(er*dr[o->dir]+ec*dc[o->dir]>0){a->dungeon_wait++;return FP_NOOP;}
    int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
    int nr=a->r+dr[want],nc=a->c+dc[want];
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || bd>=4))return want;
   }
  }
 }
 /* Pre-casting needs the troll to arrive next to the faced cell. Walking
  * on, or turning away, lets it arrive behind or beside us instead. Face
  * the side it approaches from while it is still three or more cells away,
  * then let it come. Its moves shrink its larger offset first, so a troll
  * in front of us stays in front until it is adjacent. */
 if((a->level==4 || a->level==5) && o->mana>=2 && (o->spells[0] || o->spells[1]) && (a->level==4?a->vault_wait:a->troll_wait)<24 &&
    !(TB(8) && a->level==5 && a->troll_door)) {
  int *wait=a->level==4?&a->vault_wait:&a->troll_wait;
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
   if(!e.kind && dd>=3 && dd<bd && wm_hit_damage(o,e.type,0)<e.health){best=i;bd=dd;}
  }
  if(best<0)*wait=0;
  if(best>=0) {
   /* Only a troll whose greedy approach is unobstructed will arrive. */
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,clear=wm_greedy_approach(a,s,e,0,NULL,NULL);
   if(clear) {
    /* Bounded: a troll that hovers out of reach must not stall supplies. */
    if(er*dr[o->dir]+ec*dc[o->dir]>0){(*wait)++;return FP_NOOP;}
    int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
    int nr=a->r+dr[want],nc=a->c+dc[want];
    /* A known blocked neighbour turns us in place; otherwise turning is
     * a step towards it, safe only from four or more cells away. Unknown
     * terrain cannot establish either outcome. */
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || bd>=4))return want;
   }
  }
 }
 /* Most melee damage before the Troll Mines is taken while turning towards
  * a mob that has just arrived beside us (Dungeon 56%, Sewers 76%, Vault 56%
  * of melee damage in 300-400 sampled worlds): the turn costs the step in
  * which it strikes. A diamond sword kills lizards and knights in one blow,
  * so facing the cell it arrives in removes the hit. Predict that cell from
  * its greedy approach, face it while turning is safe, and wait for it.
  * Only for mobs one blow kills: an iron-sword orc fight costs its hit either
  * way, and waiting under mage fire cost 1.1 points of Dungeon passes. */
 if(!a->floors[5].seen && a->level>=1 && a->level<=4 && a->early_wait<24) {
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
   if(!e.kind && dd>=2 && dd<bd && wm_hit_damage(o,e.type,0)>=e.health){best=i;bd=dd;}
  }
  int exposed=0;
  for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind && wm_firing_line(a,a->r,a->c,a->enemies[i].p/FP_SIDE,a->enemies[i].p%FP_SIDE))exposed=1;
  if(best<0)a->early_wait=0;
  if(best>=0 && !exposed) {
   FPEnemy e=a->enemies[best];int r,c,clear=wm_greedy_approach(a,s,e,1,&r,&c);
   if(clear && abs(r)+abs(c)==1) {
    int want=r>0?FP_DOWN:r<0?FP_UP:c>0?FP_RIGHT:FP_LEFT;
    if(o->dir==want){a->early_wait++;return FP_NOOP;}
    int nr=a->r+dr[want],nc=a->c+dc[want];
    /* A blocked neighbour turns us in place; an open one is a step, safe
     * only while the mob is four or more cells away. */
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || mobs_of(s)[cell(nr,nc)] || bd>=4)){a->early_wait++;return want;}
   }
  }
 }
 return -1;
}

/* cb_lowflee (the low-health flight) is the program's (p/combat.tr).
 * CB_COAL switches on the coal-torch trip in m_build.inc (cb_coaltrip). */
/* The unseen-attacker responses (full_response.inc) are TR defs in p/combat.tr. */
