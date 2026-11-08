#include "full_policy.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { UNKNOWN=255, INF=0x3f3f3f3f, VAULT_UNUSABLE=1000000,
       GRAVEYARD_TORCH_GAIN=25, GRAVEYARD_RECOVER_HEALTH=10, ICE_TORCH_GAIN=25 };
/* Optional, OFF: stock coal and wood on cleared Fire for a torch-hungry Ice
 * explorer (requested by the Ice stream; untested end to end). */
#ifndef FIRE_ICEPREP
#define FIRE_ICEPREP 0
#endif
static const int dr[5]={0,0,0,-1,1},dc[5]={0,-1,1,0,0};
static int cell(int r,int c) { return r*FP_SIDE+c; }
static int inside(int r,int c) { return (unsigned)r<FP_SIDE && (unsigned)c<FP_SIDE; }
static int imin(int a,int b) { return a<b?a:b; }
static int block(const FPAgent *a,int r,int c) {
 return inside(r,c)?a->floors[a->level].map[cell(r,c)]:FP_BOUNDARY;
}
static int solid(int b) {
 switch(b) {
 case FP_STONE:case FP_TREE:case FP_COAL:case FP_IRON:case FP_DIAMOND:
 case FP_TABLE:case FP_FURNACE:case FP_PLANT:case FP_RIPE:case FP_WALL:
 case FP_MOSS:case FP_STALAGMITE:case FP_SAPPHIRE:case FP_RUBY:case FP_CHEST:
 case FP_FOUNTAIN:case FP_FIRE_TREE:case FP_FIRE_TABLE:case FP_ICE_TABLE:
 case FP_BOSS:case FP_GRAVE:case FP_GRAVE2:case FP_GRAVE3:return 1;
 default:return 0;
 }
}
/* Troll-stream macro-state: from the first Troll Mines arrival until the
 * first Fire arrival, including return trips from the mines. */
static int troll_phase(const FPAgent *a) { return a->floors[5].seen && !a->floors[6].seen; }
/* Ice stream (owns every step once Ice has been seen). */
static int ice_phase(const FPAgent *a) { return a->floors[7].seen; }
/* Ice-stream switches (both on in the candidate): 1 stone shield against a
 * shot flying at us along the faced line, 2 an elemental beside us counts as
 * an incoming point-blank shot. (Pricing elemental firing lines in path
 * risk as on the early floors was null: -0.7pp.) */
#ifndef ICE_DEX7
#define ICE_DEX7 0
#endif
#ifndef ICE_V
#define ICE_V 0x3
#endif
#define ICEB(bit) ((ICE_V>>(bit))&1)
/* Elemental realms after Ice (Fire crossings, Ice itself). */
static int ice_realm(const FPAgent *a) { return ice_phase(a) && (a->level==6 || a->level==7); }
/* Mid-stream macro-state: from the first Sewers arrival until the first
 * Troll Mines arrival (Sewers, Vault and return trips from them). */
static int mid_phase(const FPAgent *a) { return a->floors[3].seen && !a->floors[5].seen; }
static int melee_within(const FPAgent *a,int radius) {
 for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=radius)return 1;
 return 0;
}
static int walkable(int b) {
 return b!=UNKNOWN && b!=FP_INVALID && b!=FP_BOUNDARY && b!=FP_WATER && b!=FP_LAVA && !solid(b);
}
/* A direction is also a movement attempt: UNKNOWN is not a known wall.
 * Never choose one when terrain cannot tell us whether the player moves.
 * Remembered ground and known blocked cells still permit ordinary travel
 * and turning; this does not require the destination to be lit now. */
static int known_direction(const FPAgent *a,int d) {
 int b=block(a,a->r+dr[d],a->c+dc[d]);
 return b!=UNKNOWN && b!=FP_INVALID;
}
static int mineable(const FPObservation *o,int b) {
 if(b==FP_TREE || b==FP_FIRE_TREE || b==FP_ICE_SHRUB || b==FP_CHEST || b==FP_TABLE || b==FP_FURNACE) return 1;
 if(b==FP_STONE || b==FP_COAL || b==FP_STALAGMITE) return o->inv[FP_I_PICK]>=1;
 if(b==FP_IRON) return o->inv[FP_I_PICK]>=2;
 if(b==FP_DIAMOND) return o->inv[FP_I_PICK]>=3;
 return (b==FP_RUBY || b==FP_SAPPHIRE) && o->inv[FP_I_PICK]>=4;
}
static int near(const FPAgent *a,int p,int furnace) {
 int bits=a->floors[a->level].stations[p]; return (bits&1) && (!furnace || (bits&2));
}
static int armour(const FPObservation *o) {
 int n=0;for(int i=0;i<4;i++)n+=o->armour[i];return n;
}
static float sword_damage(const FPObservation *o) {
 static const int base[5]={1,2,3,5,8};
 return base[imin(o->inv[FP_I_SWORD],4)]*(1+0.25f*(o->str-1));
}
static float hit_damage(const FPObservation *o,int type,int ranged) {
 float damage=sword_damage(o);
 if(type==5)damage*=0.5f;if(type==6 && !ranged)damage*=0.8f;if(type>=7)damage*=0.1f;
 if(o->sword_enchant && !(type==7 && o->sword_enchant==1) && !(type==8 && o->sword_enchant==2)) {
  static const float base[5]={1,2,3,5,8};
  damage+=base[imin(o->inv[FP_I_SWORD],4)]*0.5f*(1+0.05f*(o->intelligence-1));
 }
 return damage;
}
/* Public projectile codes are the engine type plus one. */
static float projectile_damage(const FPObservation *o,int projectile,int type,int ranged) {
 if(projectile==3 || projectile==4) {
  if((projectile==3 && type==7) || (projectile==4 && type==8))return 0;
  return 3*(1+0.5f*(o->intelligence-1));
 }
 float physical=5*(1+0.2f*(o->dex-1));
 float damage=physical*(type==5?0.5f:type==6 && !ranged?0.8f:type>=7?0.1f:1);
 if(o->bow_enchant && !(type==7 && o->bow_enchant==1) && !(type==8 && o->bow_enchant==2))damage+=physical*0.5f;
 return damage;
}
static float attack_damage(const FPObservation *o,int type) {
 static const float physical[8]={2,4,3,5,6,6,3,4};
 float damage=physical[type-1]*(1-0.1f*armour(o));
 int fire=0,ice=0;for(int i=0;i<4;i++){fire+=o->armour_enchant[i]==1;ice+=o->armour_enchant[i]==2;}
 if(type==6)damage+=1-0.2f*fire+1-0.2f*ice;
 if(type==7)damage+=5*(1-0.2f*fire);
 if(type==8)damage+=5*(1-0.2f*ice);
 return damage*(o->level==8?1.5f:1);
}
/* The first Fire expedition: from the first Fire arrival, including climbs
 * back to the mines for water, until the first Ice arrival. */
static int fire_phase(const FPAgent *a) { return a->floors[6].seen && !a->floors[7].seen; }
/* A pigman blow and an elemental fireball both deal 3 physical + 5 fire. */
static float fire_hit(const FPObservation *o) { return attack_damage(o,7); }
/* Health at which a Fire visit ends: room for the next blow and a margin.
 * (Leaving at max(8, hit+4.5) instead did not help: +0.6pp, noise.) */
static float fire_retreat(const FPObservation *o) {
 return fmaxf(6,fire_hit(o)+2.5f);
}
static uint64_t word(FPAgent *a) {
 uint64_t x=a->rng;x^=x>>12;x^=x<<25;x^=x>>27;a->rng=x;
 return x*UINT64_C(2685821657736338717);
}
FPConfig fp_default_config(void) {
 return (FPConfig){5,5,3,20,12,4,2000,1200,1,3,4,1.5,2};
}
void fp_reset(FPAgent *a,uint64_t seed) {
 memset(a,0,sizeof(*a));
 for(int l=0;l<FP_LEVELS;l++) {
  memset(a->floors[l].map,UNKNOWN,FP_CELLS);
  a->floors[l].up=a->floors[l].down=-1;
  a->floors[l].known_lo=FP_CELLS;a->floors[l].known_hi=-1;
 }
 for(int i=0;i<6;i++)a->potion_effect[i]=-1;
 a->r=a->c=FP_SIDE/2;a->floors[0].seen=1;
 a->last_frontier=a->recovery_goal=-1;
 a->plant_cell=-1;a->water_goal=-1;a->unseen_hit=-100;
 a->rng=seed?seed:UINT64_C(0x9e3779b97f4a7c15);
 /* The overworld's documented center spawn fixes its coordinate frame. */
 for(int r=0;r<FP_SIDE;r++)for(int c=0;c<FP_SIDE;c++)
  if(r<24||r>=72||c<24||c>=72)a->floors[0].map[cell(r,c)]=FP_BOUNDARY;
}
void fp_reset_cold(FPAgent *a,uint64_t seed,const FPObservation *o) {
 fp_reset(a,seed);
 memset(a->floors[0].map,UNKNOWN,FP_CELLS);a->floors[0].seen=0;
 a->level=o->level;a->floors[o->level].seen=1;
}
static void scalars(FPObservation *o,const float *p) {
 for(int i=0;i<10;i++)o->inv[i]=(int)lroundf(p[i]*p[i]*100);
 o->inv[FP_I_BOOK]=(int)lroundf(p[10]*2);
 o->inv[FP_I_PICK]=(int)lroundf(p[11]*4);o->inv[FP_I_SWORD]=(int)lroundf(p[12]*4);
 o->sword_enchant=(int)lroundf(p[13]);o->bow_enchant=(int)lroundf(p[14]);o->bow=p[15]>0.5f;
 for(int i=0;i<6;i++)o->potions[i]=(int)lroundf(p[16+i]*p[16+i]*100);
 o->health=p[22]*10;o->food=(int)lroundf(p[23]*10);o->drink=(int)lroundf(p[24]*10);
 o->energy=(int)lroundf(p[25]*10);o->mana=(int)lroundf(p[26]*10);o->xp=(int)lroundf(p[27]*10);
 o->dex=(int)lroundf(p[28]*10);o->str=(int)lroundf(p[29]*10);o->intelligence=(int)lroundf(p[30]*10);
 o->dir=3;for(int i=0;i<4;i++)if(p[31+i]>0.5f)o->dir=i+1;
 for(int i=0;i<4;i++){o->armour[i]=(int)lroundf(p[35+i]*2);o->armour_enchant[i]=(int)lroundf(p[39+i]);}
 o->light=p[43];o->sleeping=p[44]>0.5f;o->resting=p[45]>0.5f;
 o->spells[0]=p[46]>0.5f;o->spells[1]=p[47]>0.5f;o->level=(int)lroundf(p[48]*10);
 o->clear=p[49]>0.5f;o->boss_vulnerable=p[50]>0.5f;
}
void fp_decode_compact(FPObservation *o,const uint8_t *record) {
 memset(o,0,sizeof(*o));
 for(int i=0;i<FP_VIEW;i++) {
  const uint8_t *p=record+8*i;o->blocks[i]=p[0];o->items[i]=p[1]?p[1]-1:0;
  o->visible[i]=p[2];memcpy(o->mobs[i],p+3,5);
 }
 float tail[51];memcpy(tail,record+792,sizeof(tail));scalars(o,tail);
}
void fp_decode_float(FPObservation *o,const float *record) {
 memset(o,0,sizeof(*o));
 for(int i=0;i<FP_VIEW;i++) {
  const float *p=record+8*i;o->blocks[i]=(uint8_t)lroundf(p[0]);
  o->items[i]=p[1]>0?(uint8_t)lroundf(p[1]-1):0;o->visible[i]=p[2]>0.5f;
  for(int m=0;m<5;m++)o->mobs[i][m]=(uint8_t)lroundf(p[3+m]);
 }
 scalars(o,record+792);
}

static void perceive(FPAgent *a,const FPObservation *o,float reward,FPScratch *s) {
 static const int inv_ach[10]={0,9,17,18,19,54,59,3,23,22};
 for(int i=0;i<10;i++)if(o->inv[i])a->achieved[inv_ach[i]]=1;
 static const int picks[4]={5,13,20,60},swords[4]={6,14,21,25};
 for(int i=0;i<4;i++){if(o->inv[FP_I_PICK]>i)a->achieved[picks[i]]=1;if(o->inv[FP_I_SWORD]>i)a->achieved[swords[i]]=1;}
 for(int i=0;i<4;i++){if(o->armour[i]==1)a->achieved[26]=1;if(o->armour[i]>=2)a->achieved[27]=1;if(o->armour_enchant[i])a->achieved[64]=1;}
 if(o->bow)a->achieved[52]=1;if(o->sword_enchant)a->achieved[63]=1;
 if(o->spells[0])a->achieved[55]=1;if(o->spells[1])a->achieved[57]=1;
 if(a->t) {
  const FPObservation *p=&a->previous;int act=a->previous_action;
  if(p->level==8 && o->level==8 && p->boss_vulnerable && !o->boss_vulnerable &&
     act==FP_DO && a->previous_block==FP_BOSS && !p->sleeping && !p->resting) {
   a->achieved[48]=1;
   if(++a->boss_hits>=8)a->achieved[49]=1;
  }
  if(!p->sleeping && !p->resting) {
   if(act==FP_PLACE_TABLE && o->inv[0]<p->inv[0])a->achieved[1]=1;
   if(act==FP_PLACE_FURNACE && o->inv[1]<p->inv[1])a->achieved[16]=1;
   if(act==FP_PLACE_STONE && o->inv[1]<p->inv[1])a->achieved[10]=1;
   if(act==FP_PLACE_PLANT && o->inv[7]<p->inv[7]){a->achieved[7]=1;a->plant_cell=a->previous_target;a->plant_tick=a->t;}
   if(act==FP_PLACE_TORCH && o->inv[8]<p->inv[8])a->achieved[24]=1;
   if(act==FP_SHOOT && o->inv[9]<p->inv[9])a->achieved[53]=1;
   if(act==FP_FIREBALL && o->mana<=p->mana-1)a->achieved[56]=1;
   if(act==FP_ICEBALL && o->mana<=p->mana-1)a->achieved[58]=1;
   if(act>=FP_POTION_RED && act<=FP_POTION_YELLOW && o->potions[act-FP_POTION_RED]<p->potions[act-FP_POTION_RED]) {
    int ix=act-FP_POTION_RED;a->achieved[62]=1;
    if(o->health>p->health+1)a->potion_effect[ix]=0;
    else if(o->health<p->health-2)a->potion_effect[ix]=1;
    else if(o->mana>p->mana+2)a->potion_effect[ix]=2;
    else if(o->mana<p->mana-2)a->potion_effect[ix]=3;
    else if(o->energy>p->energy+2)a->potion_effect[ix]=4;
    else if(o->energy<p->energy-2)a->potion_effect[ix]=5;
    /* A restorative colour shows nothing when its stat is already full.
     * Below full health it cannot be the healing one: stop retesting it. */
    else if(o->level==5 && p->health<7+p->str)a->potion_effect[ix]=6;
   }
   if(act==FP_DO && !a->previous_mob) {
    if(a->previous_block==FP_GRASS)a->farm_attempts++;
    if(a->previous_block==FP_WATER||a->previous_block==FP_FOUNTAIN)a->achieved[4]=1;
    if(a->previous_block==FP_CHEST)a->achieved[61]=1;
    if(a->previous_block==FP_RIPE && o->food>p->food){a->achieved[11]=1;if(a->previous_target==a->plant_cell)a->plant_tick=a->t;}
   }
   if(act==FP_DO && a->previous_mob && reward-0.1f*(o->health-p->health)>0.5f) {
    static const int ach[3][8]={{8,36,38,40,65,42,44,46},{2,50,51,0,0,0,0,0},{12,37,39,41,66,43,45,47}};
    a->achieved[ach[a->previous_mob-1][a->previous_type-1]]=1;
   }
  }
  if(p->sleeping && !o->sleeping)a->achieved[15]=1;
 }
 if(o->level!=a->level) {
  int descending=o->level>a->level;a->level=o->level;
  FPFloor *f=&a->floors[a->level];int landing=descending?f->up:f->down;
  if(!f->seen || landing<0) {a->r=a->c=48;landing=cell(48,48);}
  else {a->r=landing/FP_SIDE;a->c=landing%FP_SIDE;}
  if(descending)f->up=landing;else f->down=landing;
  f->seen=1;f->entered=a->t;a->last_frontier=a->recovery_goal=a->water_goal=-1;a->recovering=0;a->arrow_count=0;a->enemy_count=0;a->shot_count=0;
 }
 FPFloor *f=&a->floors[a->level];f->steps++;f->clear=o->clear;
 if(a->t && a->previous.level==a->level && a->previous_action==FP_PLACE_TORCH && o->inv[8]<a->previous.inv[8]) {
  int r=a->previous_target/FP_SIDE,c=a->previous_target%FP_SIDE;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc))f->lit[cell(r+rr,c+cc)]=1;
 }
 memset(s->mobs,0,FP_CELLS);memset(s->melee,0,FP_CELLS);memset(s->ranged,0,FP_CELLS);
 memset(s->passive,0,FP_CELLS);memset(s->arrow_next,0,FP_CELLS);s->visible_count=0;
 int changes[FP_VIEW],nchange=0,newarrows[3],types[3],narrow=0;
 for(int i=0;i<FP_VIEW;i++) {
  int r=a->r+i/11-4,c=a->c+i%11-5;if(!inside(r,c))continue;
  int q=cell(r,c),old=f->map[q],b=o->blocks[i];
  if(o->visible[i]) {
   f->map[q]=(uint8_t)b;f->items[q]=o->items[i];
   if(b!=FP_BOUNDARY && b!=FP_INVALID) {
    if(q<f->known_lo)f->known_lo=q;if(q>f->known_hi)f->known_hi=q;
   }
   if(old!=b && (old==FP_TABLE||old==FP_FURNACE||b==FP_TABLE||b==FP_FURNACE))changes[nchange++]=q;
   if(o->items[i]==2 || o->items[i]==4)f->down=q;if(o->items[i]==3)f->up=q;
   s->visible_cells[s->visible_count++]=q;
   s->melee[q]=o->mobs[i][0];s->passive[q]=o->mobs[i][1];s->ranged[q]=o->mobs[i][2];
   s->mobs[q]=(s->melee[q]?1:0)|(s->passive[q]?2:0)|(s->ranged[q]?4:0);
  } else if(f->lit[q]||a->level==0||a->level==1||a->level==3||a->level==4||a->level==6)f->map[q]=FP_BOUNDARY;
  if(o->mobs[i][3]) {
   s->arrow_next[q]=1; /* Projectiles damage both their old and new cells. */
   int vr=0,vc=0,found=0;
   for(int j=0;j<a->arrow_count;j++) {
    int rr=a->arrows[j]/FP_SIDE,cc=a->arrows[j]%FP_SIDE;
    if(a->projectile_types[j]==o->mobs[i][3] && abs(r-rr)+abs(c-cc)==1) {vr=r-rr;vc=c-cc;found=1;break;}
   }
   /* A new slimeball has just left the deep thing beside it and keeps
    * flying away from it. Unpredicted shots are the ones agents walk into. */
   if(!found && (a->level==4 || a->level==5)) {
    int n=0,mr=0,mc=0;
    for(int d=1;d<=4;d++) {
     int vr2=r+dr[d]-a->r,vc2=c+dc[d]-a->c;
     if(vr2>=-4 && vr2<=4 && vc2>=-5 && vc2<=5 && o->mobs[(vr2+4)*11+vc2+5][2]){n++;mr=-dr[d];mc=-dc[d];}
    }
    if(n==1){vr=mr;vc=mc;found=1;}
   }
   if(!found) {int rr=a->r-r,cc=a->c-c;
    if(rr==0 && abs(cc)<=2){vc=cc>0?1:-1;found=1;}
    if(cc==0 && abs(rr)<=2){vr=rr>0?1:-1;found=1;}
   }
   if(found && inside(r+vr,c+vc))s->arrow_next[cell(r+vr,c+vc)]=1;
   if(narrow<3){newarrows[narrow]=q;types[narrow++]=o->mobs[i][3];}
  }
 }
 for(int i=0;i<nchange;i++) {
  int r=changes[i]/FP_SIDE,c=changes[i]%FP_SIDE;
  for(int rr=r-1;rr<=r+1;rr++)for(int cc=c-1;cc<=c+1;cc++)if(inside(rr,cc)) {
   int bits=0;for(int r2=rr-1;r2<=rr+1;r2++)for(int c2=cc-1;c2<=cc+1;c2++)if(r2!=rr||c2!=cc) {
    int b2=block(a,r2,c2);if(b2==FP_TABLE)bits|=1;if(b2==FP_FURNACE)bits|=2;
   }
   f->stations[cell(rr,cc)]=(uint8_t)bits;
  }
 }
 memcpy(a->arrows,newarrows,(size_t)narrow*sizeof(int));memcpy(a->projectile_types,types,(size_t)narrow*sizeof(int));a->arrow_count=narrow;
 /* Carry visible shots forward one tick, including a newly fired shot
  * only when the public inventory or mana confirms that it was launched. */
 int shot_pos[4],shot_type[4],shot_dir[4],nshot=a->shot_count;
 memcpy(shot_pos,a->shot_pos,sizeof(a->shot_pos));
 memcpy(shot_type,a->shot_type,sizeof(a->shot_type));
 memcpy(shot_dir,a->shot_dir,sizeof(a->shot_dir));
 if(a->t && a->previous.level==o->level && !a->previous.sleeping && !a->previous.resting) {
  int act=a->previous_action,type=0;
  if(act==FP_SHOOT && o->inv[9]<a->previous.inv[9])type=5;
  if((act==FP_FIREBALL || act==FP_ICEBALL) && o->mana<=a->previous.mana-1)type=act==FP_FIREBALL?3:4;
  if(type && nshot<4){shot_pos[nshot]=cell(a->observed_r,a->observed_c);shot_type[nshot]=type;shot_dir[nshot++]=a->previous.dir;}
 }
 FPEnemy enemies[8];int nenemy=0,used[8]={0};
 /* Mobs freeze while we are on another floor and strike on the arrival step.
  * A health loss on arrival in the Troll Mines shows which trolls have just
  * attacked; the conservative zero cooldown predicted a second, lethal blow
  * and sent us straight back up the ladder, again after every heal (ep 102). */
 int arrival_hits=0;
 if(a->t && a->previous.level!=o->level && o->level==5 && troll_phase(a)) {
  float lost=a->previous.health-o->health,blow=attack_damage(o,6);
  if(lost>=0.6f*blow)arrival_hits=(int)(lost/blow+0.5f);
 }
 static const float hp[8][2]={{5,3},{7,5},{9,6},{11,8},{12,12},{20,4},{20,14},{24,16}};
 for(int kind=0;kind<2;kind++)for(int i=0;i<s->visible_count;i++) {
  int p=s->visible_cells[i],type=kind?s->ranged[p]:s->melee[p];if(!type||nenemy>=8)continue;
  int match=-1,dist=2;
  for(int j=0;j<a->enemy_count;j++) {
   FPEnemy e=a->enemies[j];if(used[j]||e.kind!=kind||e.type!=type)continue;
   /* A new mob can occupy the cell of a mob killed on the previous turn.
    * Its health must start fresh rather than inheriting the victim's HP. */
   if(a->previous_action==FP_DO && a->previous_target==e.p &&
      !a->previous.sleeping && !a->previous.resting && hit_damage(&a->previous,e.type,e.kind)>=e.health)continue;
   int d=abs(e.p/FP_SIDE-p/FP_SIDE)+abs(e.p%FP_SIDE-p%FP_SIDE);
   if(d<dist){dist=d;match=j;}
  }
  FPEnemy e={p,type,kind,0,hp[type-1][kind]};
  if(match<0 && !kind && arrival_hits>0 && abs(p/FP_SIDE-a->r)+abs(p%FP_SIDE-a->c)==1){e.cooldown=5;arrival_hits--;}
  if(match>=0) {
   FPEnemy old=a->enemies[match];used[match]=1;e.health=old.health;e.cooldown=old.cooldown-1;
   if(a->previous_action==FP_DO && a->previous_target==old.p && !a->previous.sleeping && !a->previous.resting) {
    e.health=fmaxf(0.1f,e.health-hit_damage(&a->previous,type,kind));
   }
   int replacement=0;
   /* Projectiles hit after mobs move. A surviving projectile has not hit
    * this enemy; a disappeared shot can have hit its old or next cell. */
   for(int j=0;j<nshot;j++) {
    if(!shot_dir[j])continue;
    int q=shot_pos[j]+dr[shot_dir[j]]*FP_SIDE+dc[shot_dir[j]];
    if(e.p!=shot_pos[j] && (e.p!=q || s->mobs[shot_pos[j]]))continue;
    int rr=q/FP_SIDE-a->r+4,cc=q%FP_SIDE-a->c+5;
    if(rr>=0 && rr<9 && cc>=0 && cc<11 && o->mobs[rr*11+cc][4]==shot_type[j])continue;
    float damage=projectile_damage(o,shot_type[j],type,kind);
    /* A mob where we predicted a kill may be a new occupant. */
    if(damage>=e.health){e.health=hp[type-1][kind];e.cooldown=0;replacement=1;break;}
    e.health-=damage;
   }
   /* An adjacent mob can stand still while cooling down or blocked.
    * Only observed health loss supports inferring a fresh attack cooldown.
    * Otherwise retain the conservative countdown: a spell kills after
    * melee attacks, so a fictitious cooldown can concede a lethal hit. */
   if(!replacement && !kind && o->health<a->previous.health && old.cooldown<=0 && old.p==p && abs(old.p/FP_SIDE-a->r)+abs(old.p%FP_SIDE-a->c)==1)e.cooldown=5;
  }
  s->enemy_health[p]=e.health;s->cooldown[p]=e.cooldown;enemies[nenemy++]=e;
 }
 memcpy(a->enemies,enemies,(size_t)nenemy*sizeof(FPEnemy));a->enemy_count=nenemy;
 /* Retain a heading only when an observed shot matches its predicted move. */
 a->shot_count=0;
 for(int i=0;i<FP_VIEW;i++)if(o->mobs[i][4] && a->shot_count<3) {
  int r=a->r+i/11-4,c=a->c+i%11-5;if(!inside(r,c))continue;
  int p=cell(r,c),d=0;
  for(int j=0;j<nshot;j++)if(shot_type[j]==o->mobs[i][4] && shot_dir[j] && p==shot_pos[j]+dr[shot_dir[j]]*FP_SIDE+dc[shot_dir[j]]){d=shot_dir[j];break;}
  int j=a->shot_count++;a->shot_pos[j]=p;a->shot_type[j]=o->mobs[i][4];a->shot_dir[j]=d;
 }
 int q=cell(a->r,a->c);if(f->visits[q]<65535)f->visits[q]++;
 if(a->level==0 && a->plant_cell>=0 && a->previous_action==FP_DO && a->previous_target==a->plant_cell &&
    a->previous_block==FP_RIPE && f->map[a->plant_cell]==FP_PLANT)a->plant_tick=a->t;
 if(a->plant_cell>=0 && a->t-a->plant_tick>=600 && a->floors[0].map[a->plant_cell]==FP_PLANT)a->floors[0].map[a->plant_cell]=FP_RIPE;
 a->observed_r=a->r;a->observed_c=a->c;
}
static float incoming_damage(const FPAgent *a,const FPObservation *o,FPScratch *s,int action) {
 int r=a->r,c=a->c;
 if(action>=FP_LEFT && action<=FP_DOWN) {
  int rr=r+dr[action],cc=c+dc[action];
  if(inside(rr,cc) && walkable(block(a,rr,cc)) && !s->mobs[cell(rr,cc)]){r=rr;c=cc;}
 }
 int faced=cell(a->r+dr[o->dir],a->c+dc[o->dir]);float damage=0;
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];
  if(e.kind || e.cooldown>0 || abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)!=1)continue;
  if(action==FP_DO && e.p==faced && hit_damage(o,e.type,e.kind)>=e.health)continue;
  damage+=attack_damage(o,e.type);
 }
 /* An elemental beside us fires point-blank whenever its step away is
  * blocked (Fire stream: 74 of 87 sampled fireball hits came from one or
  * two cells; mid stream: same for kobolds and archers). */
 /* Mid stream: a kobold or archer beside us throws point-blank whenever its
  * step away is blocked (119 of 196 traced shot hits in Sewers/Vault deaths
  * came from a ranged mob we had just stepped next to). */
 if((ICEB(1) && ice_realm(a)) || ((a->level==3 || a->level==4) && mid_phase(a)))
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];
   if(!e.kind || e.cooldown>0 || abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)!=1)continue;
   if(action==FP_DO && e.p==faced && hit_damage(o,e.type,e.kind)>=e.health)continue;
   damage+=mid_phase(a)?(a->level==4?5:4)*(1-0.1f*armour(o)):attack_damage(o,e.type);
  }
 if(s->arrow_next[cell(r,c)])damage+=8;
 return damage;
}
static int commit_action(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int skill);
static int emit(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int skill) {
 int moving=!o->sleeping && !o->resting && action>=FP_LEFT && action<=FP_DOWN;
 if(moving && !known_direction(a,action)){action=FP_NOOP;moving=0;}
 float movement_damage=moving?incoming_damage(a,o,s,action):0;
 /* Melee enemies attack after our movement, before their own movement.
  * When safe here, let an enemy two cells away approach instead of giving
  * it a free hit by stepping into reach. This also preserves our facing
  * for an immediate sword strike on the next observation. */
 /* In Ice, an approach can establish the facing needed for three fireballs
  * during the enemy's cooldown. Accept it only with enough mana and more
  * than six health remaining after the predicted attack. */
 int can_aim=o->level==7 && o->dir!=action && o->spells[0] && o->mana>=6 && movement_damage<o->health-6;
 if(moving && !can_aim && a->enemy_count && movement_damage>0 && incoming_damage(a,o,s,FP_NOOP)==0) {
  int rr=a->r+dr[action],cc=a->c+dc[action];
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];
   if(!e.kind && e.cooldown<=0 &&
      abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)==2 &&
      abs(e.p/FP_SIDE-rr)+abs(e.p%FP_SIDE-cc)==1) {
    action=FP_NOOP;skill=FP_FIGHT;moving=0;break;
   }
  }
 }
 /* A slimeball costs most of a Troll Mines health bar. Do not step into a
  * predicted shot path while the current cell is safe. */
 if(moving && (a->level==4 || a->level==5 || (ICEB(1) && ice_realm(a)) || (a->level==3 && mid_phase(a))) && movement_damage>0 && incoming_damage(a,o,s,FP_NOOP)==0){action=FP_NOOP;skill=FP_FIGHT;moving=0;movement_damage=0;}
 /* Check the destination too: survival, crafting, and exploration can all
  * choose a move into an attack that is not adjacent to the current cell. */
 if(moving && movement_damage>=o->health-0.1f) {
  int best=FP_NOOP;float damage=incoming_damage(a,o,s,best);
  for(int d=FP_LEFT;d<=FP_DOWN;d++) {
   int rr=a->r+dr[d],cc=a->c+dc[d];
   if(!inside(rr,cc)||!walkable(block(a,rr,cc))||s->mobs[cell(rr,cc)])continue;
   float next=incoming_damage(a,o,s,d);if(next<damage){best=d;damage=next;}
  }
  action=best;skill=FP_FIGHT;
 }
 return commit_action(a,o,s,action,skill);
}
static int commit_action(FPAgent *a,const FPObservation *o,FPScratch *s,int action,int skill) {
 a->previous=*o;a->previous_action=action;a->skill=skill;
 int r=a->r+dr[o->dir],c=a->c+dc[o->dir],q=inside(r,c)?cell(r,c):-1;
 a->previous_block=block(a,r,c);a->previous_mob=0;a->previous_type=0;
 a->previous_target=q;
 if(q>=0) {
  if(s->melee[q]){a->previous_mob=1;a->previous_type=s->melee[q];}
  else if(s->passive[q]){a->previous_mob=2;a->previous_type=s->passive[q];}
  else if(s->ranged[q]){a->previous_mob=3;a->previous_type=s->ranged[q];}
 }
 if(!o->sleeping && !o->resting && action>=1 && action<=4) {
  r=a->r+dr[action];c=a->c+dc[action];
  if(inside(r,c) && walkable(block(a,r,c)) && !s->mobs[cell(r,c)]){a->r=r;a->c=c;}
 }
 a->t++;return action;
}
/* Binary heap keyed by dist. Sifting moves a hole instead of swapping, which
 * leaves exactly the arrangement (and pop order among equal distances) of
 * successive swaps: route choices between equal-cost paths are unchanged. */
static void heap_update(FPScratch *s,int p) {
 int i=s->pos[p],key=s->dist[p];if(i<0)i=s->heap_size++;
 while(i>0){int j=(i-1)/2;if(s->hkey[j]<=key)break;s->heap[i]=s->heap[j];s->hkey[i]=s->hkey[j];s->pos[s->heap[i]]=i;i=j;}
 s->heap[i]=p;s->hkey[i]=key;s->pos[p]=i;
}
static int heap_pop(FPScratch *s) {
 int p=s->heap[0];s->pos[p]=-2;
 if(--s->heap_size){int n=s->heap_size,x=s->heap[n],key=s->hkey[n],i=0;
  for(;;){int j=2*i+1;if(j>=n)break;
   if(j+1<n && s->hkey[j+1]<s->hkey[j])j++;
   if(key<=s->hkey[j])break;s->heap[i]=s->heap[j];s->hkey[i]=s->hkey[j];s->pos[s->heap[i]]=i;i=j;}
  s->heap[i]=x;s->hkey[i]=key;s->pos[x]=i;
 }return p;
}
/* Ranged mobs fire only along their larger axis, from four or five cells,
 * and the shot flies straight until a solid block. Off-axis cells are safe;
 * on-axis cells with a clear line are where fireballs, daggers and Vault
 * arrows land (floors 1-4: 2,600 of 6,600 failures before the Troll Mines). */
static int firing_line(const FPAgent *a,int r,int c,int er,int ec) {
 int dr_=er-r,dc_=ec-c,d=abs(dr_)+abs(dc_);
 if((dr_ && dc_) || d<2 || d>6)return 0;
 int sr=dr_>0?1:dr_<0?-1:0,sc=dc_>0?1:dc_<0?-1:0;
 for(int k=1;k<d;k++)if(solid(block(a,r+k*sr,c+k*sc)))return 0;
 return 1;
}
static int risk(const FPAgent *a,const FPObservation *o,FPScratch *s,int p) {
 int r=p/FP_SIDE,c=p%FP_SIDE,n=0,weak=sword_damage(o)<5?2:1;
 int early=!a->floors[5].seen && a->level>=1 && a->level<=4;
 /* Perception already collected every visible hostile (at most five in
  * Full). Pathfinding calls this repeatedly; skip the other view cells. */
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c);
  if(!e.kind)n+=(d<=1?8:d==2?3:0)*weak;
  if(e.kind && early){if(firing_line(a,r,c,e.p/FP_SIDE,e.p%FP_SIDE))n+=12;else if(d>=3 && d<=5)n+=1;
   /* Mid stream: beside a ranged mob is a point-blank throw (see incoming_damage). */
   if(d<=1 && mid_phase(a) && (a->level==3 || a->level==4))n+=8;}
  else if(e.kind && d>=3 && d<=5)n+=3;
  if(ICEB(1) && e.kind && d<=1 && ice_realm(a))n+=8;
  /* Deep things shoot along a row or column at range 4-5 (or closer when
   * cornered in their pool); a slimeball takes 7-8 of 12 health. Price
   * their lines. Not while drinking: costs that move with the
   * shooters made thirsty agents dither beside the pool until they died. */
  if(e.kind && a->level==5 && troll_phase(a) && !a->drinking && d<=5)n+=(e.p/FP_SIDE==r || e.p%FP_SIDE==c)?20:8;
 }
 if(s->arrow_next[p])n+=100;return n;
}
static void paths(const FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 const FPFloor *f=&a->floors[a->level];
 /* Only observed terrain can enter the heap. Unreachable distances still
  * cover the entire atlas, including adjacent stands outside these bounds. */
 memset(s->dist,0x3f,sizeof(s->dist));
 if(f->known_hi>=f->known_lo)memset(s->pos+f->known_lo,0xff,(size_t)(f->known_hi-f->known_lo+1)*sizeof(*s->pos));
 /* Entry cost by block for this inventory: walk 10, mine 30, workstation
  * 100, bridge water or lava 60 (only where no item lies), impassable -1. */
 int base[256];
 for(int b=0;b<256;b++) {
  if(walkable(b))base[b]=10;
  else if(mineable(o,b))base[b]=(b==FP_TABLE||b==FP_FURNACE)?100:30;
  else if((b==FP_WATER || b==FP_LAVA) && o->inv[1]>0 && o->inv[FP_I_PICK]>0)base[b]=-60;
  else base[b]=-1;
 }
 int danger=a->enemy_count || a->arrow_count,mob_cross=a->level==1 || a->level==3 || a->level==4;
 s->heap_size=0;int start=cell(a->r,a->c);s->dist[start]=0;s->first[start]=0;s->pos[start]=-1;heap_update(s,start);
 while(s->heap_size) {
  int p=heap_pop(s),r=p/FP_SIDE,c=p%FP_SIDE,dp=s->dist[p],from_start=p==start,fp=s->first[p];
  for(int d=1;d<=4;d++) {
   int rr=r+dr[d],cc=c+dc[d];if(!inside(rr,cc))continue;
   /* A mob in a one-cell Dungeon corridor is an obstacle to fight through,
    * not a wall. Treating it as a wall made water, ladders and frontiers
    * reachable or not as the mob crossed the edge of the view, and agents
    * alternated between two rules for dozens of steps. Sewer corridors are
    * one cell wide too: thirsty agents swung between drinking and eating
    * beside a lizard until they starved (ep 22, 71). */
   int q=cell(rr,cc),cost=base[f->map[q]];
   if(s->mobs[q]){if(!mob_cross)continue;if(cost==10)cost=110;}
   if(cost==-1)continue;
   if(cost==-60){if(f->items[q])continue;cost=60;}
   if(s->pos[q]==-2)continue;
   if(danger && abs(rr-a->r)+abs(cc-a->c)<=6)cost+=(int)(cfg->danger_cost*risk(a,o,s,q));
   if(dp+cost<s->dist[q]){s->dist[q]=dp+cost;s->first[q]=from_start?d:fp;heap_update(s,q);}
  }
 }
}
static int move_path(FPAgent *a,const FPObservation *o,FPScratch *s,int p) {
 int d=s->first[p];if(!d)return FP_NOOP;int b=block(a,a->r+dr[d],a->c+dc[d]);
 /* Floor-1 and floor-3 paths may cross a mob (see paths): strike it rather than walk into it. */
 if((a->level==1 || a->level==3 || a->level==4) && inside(a->r+dr[d],a->c+dc[d]) && s->mobs[cell(a->r+dr[d],a->c+dc[d])])return o->dir==d?FP_DO:d;
 if(mineable(o,b) && !walkable(b))return o->dir==d?FP_DO:d;
 if(b==FP_WATER||b==FP_LAVA)return o->dir==d?FP_PLACE_STONE:d;
 return d;
}
static int seek(FPAgent *a,const FPObservation *o,FPScratch *s,uint64_t blocks,int action,int radius) {
 const FPFloor *f=&a->floors[a->level];int best=-1,facing=0,best_cost=INF;
 FPIntent *in=&a->intent;int committed=in->executing && !in->reference;
 if(committed && (in->bound_mask!=blocks || in->bound_level!=a->level)) {
  in->bound_target=-1;in->bound_mask=blocks;in->bound_level=a->level;
 }
 int bound=committed?in->bound_target:-1;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int b=f->map[p];if(b>=64 || !((blocks>>b)&1) || (bound>=0 && p!=bound))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  if(a->recovering && a->recovery_goal>=0 && (b==FP_STONE||b==FP_STALAGMITE)) {
   int ar=a->recovery_goal/FP_SIDE,ac=a->recovery_goal%FP_SIDE;
   if(r>=ar-1 && r<=ar+2 && c>=ac-1 && c<=ac+2)continue;
  }
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   /* Facing affects an interaction here, but movement changes it en route.
    * Pricing a distant stand by the current facing oscillates between two
    * mineable routes every time a blocked movement turns the player. */
   int q=cell(rr,cc),cost=s->dist[q]+(q==cell(a->r,a->c) && o->dir==d?0:10);
   if(cost<best_cost && cost<=radius*10){best=q;facing=d;best_cost=cost;a->target=p;}
  }
 }
 if(best<0) {
  if(bound>=0){in->bound_target=-1;in->replans++;return seek(a,o,s,blocks,action,radius);}
  return -1;
 }
 if(committed)in->bound_target=a->target;
 if(best==cell(a->r,a->c))return o->dir==facing?action:facing;
 return move_path(a,o,s,best);
}
#define MASK(b) (UINT64_C(1)<<(b))
static int seek_block(FPAgent *a,const FPObservation *o,FPScratch *s,int b,int radius) {
 /* Recovery and optional stock gathering must respect mining capability. */
 if((b==FP_STONE || b==FP_COAL || b==FP_IRON || b==FP_DIAMOND || b==FP_SAPPHIRE || b==FP_RUBY) && !mineable(o,b))return -1;
 a->goal=b;uint64_t mask=MASK(b);
 if(b==FP_TREE)mask|=MASK(FP_FIRE_TREE)|MASK(FP_ICE_SHRUB);
 if(b==FP_STONE)mask|=MASK(FP_STALAGMITE);
 if(b==FP_WATER)mask|=MASK(FP_FOUNTAIN);
 return seek(a,o,s,mask,FP_DO,radius);
}
/* Floor 1 has about four fountains. Choosing afresh every step alternated
 * between two of them as danger costs moved with the player, and the radius
 * cutoff alternated drinking with travel. Keep a chosen fountain while it
 * remains reachable, with a margin over the radius used to choose it. Sewer
 * water lies in single cells beyond lizard-held corridors; danger costs that
 * apply only within six cells flipped the target as the player approached
 * and retreated, until it starved (ep 448: 200 steps between two pools). */
/* A remembered fountain may be reachable yet farther than the up ladder
 * plus the cleared Overworld water route. At critical drink, compare those
 * trips instead of committing to a long Dungeon corridor (ep 17). */
static int water_return(FPAgent *a,const FPObservation *o,FPScratch *s,int water_cost) {
 if(a->level!=1 || o->drink>3)return -1;
 int up=a->floors[1].up,landing=a->floors[0].down;
 if(up<0 || landing<0 || s->dist[up]>=INF)return -1;
 const FPFloor *f=&a->floors[0];int distance=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_WATER || f->map[p]==FP_FOUNTAIN)
  distance=imin(distance,abs(p/FP_SIDE-landing/FP_SIDE)+abs(p%FP_SIDE-landing%FP_SIDE));
 if(distance>=INF || s->dist[up]+10*distance+30>=water_cost)return -1;
 return up==cell(a->r,a->c)?FP_ASCEND:move_path(a,o,s,up);
}
static int seek_water(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(a->level!=1 && a->level!=3 && a->level!=4)return seek_block(a,o,s,FP_WATER,100);
 int g=a->water_goal,start=cell(a->r,a->c),b=g>=0?a->floors[a->level].map[g]:UNKNOWN;
 if(b==FP_FOUNTAIN || b==FP_WATER) {
  int best=-1,facing=0,costbest=INF;
  for(int d=1;d<=4;d++) {
   int rr=g/FP_SIDE-dr[d],cc=g%FP_SIDE-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=s->dist[q]+(q==start && o->dir==d?0:10);
   if(cost<costbest){best=q;facing=d;costbest=cost;}
  }
  if(best>=0 && costbest<=2500) {
   int retreat=water_return(a,o,s,costbest);if(retreat>=0)return retreat;
   a->goal=FP_WATER;a->target=g;
   if(best==start)return o->dir==facing?FP_DO:facing;
   return move_path(a,o,s,best);
  }
 }
 int act=seek_block(a,o,s,FP_WATER,100);
 a->water_goal=act>=0?a->target:-1;
 if(act>=0 && a->level==1 && a->water_goal>=0) {
  int g=a->water_goal,cost=INF;
  for(int d=1;d<=4;d++) {
   int rr=g/FP_SIDE-dr[d],cc=g%FP_SIDE-dc[d];if(inside(rr,cc))cost=imin(cost,s->dist[cell(rr,cc)]+10);
  }
  int retreat=water_return(a,o,s,cost);if(retreat>=0)return retreat;
 }
 return act;
}
static int hunt(FPAgent *a,const FPObservation *o,FPScratch *s,int mask,int radius) {
 int best=-1,facing=0,costbest=INF;
 for(int i=0;i<s->visible_count;i++) {
  int p=s->visible_cells[i];if(!(s->mobs[p]&mask))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=s->dist[q]+(q==cell(a->r,a->c) && o->dir==d?0:10);
   if(cost<costbest && cost<=radius*10){best=q;facing=d;costbest=cost;a->target=p;}
  }
 }
 if(best<0)return -1;
 if(best==cell(a->r,a->c))return o->dir==facing?FP_DO:facing;
 return move_path(a,o,s,best);
}
/* Compare reachable food sources by travel, facing, and sword strikes.
 * A nearby animal can refill food before a distant remembered crop. */
static int forage(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int best=-1,facing=0,start=cell(a->r,a->c);float best_cost=1e30f;
 const FPFloor *f=&a->floors[a->level];
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int passive=s->passive[p],plant=f->map[p]==FP_RIPE;
  if(!passive && !plant)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  int strikes=passive?(int)ceilf((passive==1?3:passive==2?4:6)/sword_damage(o)):1;
  int radius=passive?80:50;
  for(int d=1;d<=4;d++) {
   int rr=r-dr[d],cc=c-dc[d];if(!inside(rr,cc))continue;
   int q=cell(rr,cc),cost=s->dist[q]+(q==start && o->dir==d?0:10);
   if(cost>radius*10)continue;
   float score=(float)(cost+strikes*10);
   if(score<best_cost){best=q;facing=d;best_cost=score;a->target=p;a->goal=plant?FP_RIPE:-1;}
  }
 }
 if(best<0)return -1;
 if(best==start)return o->dir==facing?FP_DO:facing;
 return move_path(a,o,s,best);
}
/* Ice shrubs are walkable. A turn towards an adjacent shrub moves onto it,
 * so establish the harvest facing by entering its stand from behind. */
static int ice_wood(FPAgent *a,const FPObservation *o,FPScratch *s) {
 const FPFloor *f=&a->floors[7];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(f->map[p]!=FP_ICE_SHRUB || s->mobs[p])continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||s->mobs[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d){a->target=p;a->goal=FP_TREE;return FP_DO;}
   int cost=s->dist[partner]+10;
   if(cost<=500 && cost<costbest){best=partner;dir=d;costbest=cost;a->target=p;}
  }
 }
 if(best<0)return -1;
 a->goal=FP_TREE;return best==start?dir:move_path(a,o,s,best);
}
static int place_valid(const FPAgent *a,FPScratch *s,int p,int action) {
 const FPFloor *f=&a->floors[a->level];int b=f->map[p];if(s->mobs[p] || f->items[p])return 0;
 if(action==FP_PLACE_PLANT)return b==FP_GRASS;
 if(action==FP_PLACE_TORCH)return b==FP_GRASS||b==FP_PATH||b==FP_SAND||b==FP_FIRE_GRASS||b==FP_ICE_GRASS;
 return walkable(b) || (action==FP_PLACE_STONE && (b==FP_WATER || b==FP_LAVA));
}
static int place(FPAgent *a,const FPObservation *o,FPScratch *s,int action) {
 int r=a->r+dr[o->dir],c=a->c+dc[o->dir];
 if(inside(r,c) && place_valid(a,s,cell(r,c),action))return action;
 /* Turning on open ground moves. Plan the next facing cell as well. */
 for(int d=1;d<=4;d++) {
  r=a->r+dr[d];c=a->c+dc[d];int rr=r+dr[d],cc=c+dc[d];
  if(inside(r,c) && inside(rr,cc) && walkable(block(a,r,c)) && !s->mobs[cell(r,c)] && place_valid(a,s,cell(rr,cc),action))return d;
 }
 return -1;
}
typedef struct {int action,cost[5],furnace,product_kind,product,quantity;} Recipe;
static const Recipe recipes[]={
 {FP_WOOD_PICK,{1,0,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_PICK,1},{FP_WOOD_SWORD,{1,0,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_SWORD,1},
 {FP_STONE_PICK,{1,1,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_PICK,2},{FP_STONE_SWORD,{1,1,0,0,0},0,FP_GOAL_EQUIPMENT,FP_E_SWORD,2},
 {FP_IRON_PICK,{1,1,1,1,0},1,FP_GOAL_EQUIPMENT,FP_E_PICK,3},{FP_IRON_SWORD,{1,1,1,1,0},1,FP_GOAL_EQUIPMENT,FP_E_SWORD,3},
 {FP_DIAMOND_PICK,{1,0,0,0,3},0,FP_GOAL_EQUIPMENT,FP_E_PICK,4},{FP_DIAMOND_SWORD,{1,0,0,0,2},0,FP_GOAL_EQUIPMENT,FP_E_SWORD,4},
 {FP_IRON_ARMOUR,{0,0,3,3,0},1,FP_GOAL_EQUIPMENT,-1,1},{FP_DIAMOND_ARMOUR,{0,0,0,0,3},0,FP_GOAL_EQUIPMENT,-1,2},
 {FP_MAKE_ARROW,{1,1,0,0,0},0,FP_GOAL_INVENTORY,FP_I_ARROW,2},{FP_MAKE_TORCH,{1,0,1,0,0},0,FP_GOAL_INVENTORY,FP_I_TORCH,4}
};
static int craft_radius(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action,int radius) {
 const Recipe *recipe=NULL;for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++)if(recipes[i].action==action)recipe=&recipes[i];
 if(!recipe)return -1;
 const FPFloor *f=&a->floors[a->level];
 int table=-1,shared=-1,tcost=INF,scost=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(s->dist[p]<=radius && near(a,p,0)) {
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
   if(s->mobs[p] || a->floors[a->level].items[p] || !solid(b) || !mineable(o,b) || b==FP_TABLE || b==FP_FURNACE)continue;
   a->target=p;return o->dir==d?FP_DO:d;
  }
  return -1;
 }
 return place(a,o,s,FP_PLACE_TABLE);
}
static int craft(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action) {
 return craft_radius(a,o,cfg,s,action,250);
}
/* A 2x2 room permits turns by moving between its interior cells. Every side
 * can therefore be sealed without an unavailable rotate-in-place action. */
static void wall_position(int i,int w,int h,int *r,int *c,int *d) {
 if(i<h){*r=i;*c=-1;*d=1;}
 else if(i<2*h){*r=i-h;*c=w;*d=2;}
 else if(i<2*h+w){*r=-1;*c=i-2*h;*d=3;}
 else {*r=h;*c=i-2*h-w;*d=4;}
}
static int chamber_walls(FPAgent *a,const FPObservation *o,FPScratch *s,int anchor,int w,int h) {
 int r=anchor/FP_SIDE,c=anchor%FP_SIDE,n=0;
 for(int rr=r;rr<r+h;rr++)for(int cc=c;cc<c+w;cc++) {
  int b=block(a,rr,cc);
  if(!inside(rr,cc)||(!walkable(b) && !(w*h==2 && mineable(o,b)))||s->mobs[cell(rr,cc)])return -1;
 }
 for(int i=0;i<2*(w+h);i++) {
  int wr,wc,d;wall_position(i,w,h,&wr,&wc,&d);
  int rr=r+wr,cc=c+wc;if(!inside(rr,cc))return -1;
  int b=block(a,rr,cc);
  if(b==UNKNOWN)return -1;
  /* Mob projectiles destroy tables and furnaces. A Dungeon mage opened such a
   * wall and an orc killed the sleeping player (3.5x damage). Gnome arrows
   * opened mine shelters the same way (ep 158, 534); kobolds throw daggers. */
  if(a->level>=1 && a->level<=3 && (b==FP_TABLE || b==FP_FURNACE))return -1;
  if(walkable(b)||b==FP_WATER||b==FP_LAVA) {
   if((d<=2 && w==1)||(d>=3 && h==1))return -1;
   if(!place_valid(a,s,cell(rr,cc),FP_PLACE_STONE))return -1;n++;
  }
 }
 return n;
}
/* Capped Manhattan distance to observed water. Recovery and exploration
 * use this scratch array at different times. Two linear sweeps avoid a
 * separate scan of every water tile for each candidate room. */
static void water_distances(const FPFloor *f,FPScratch *s) {
 for(int r=0;r<FP_SIDE;r++)for(int c=0;c<FP_SIDE;c++) {
  int b=f->map[cell(r,c)],d=(b==FP_WATER || b==FP_FOUNTAIN)?0:40;
  if(r)d=imin(d,s->prefix[r-1][c]+1);
  if(c)d=imin(d,s->prefix[r][c-1]+1);
  s->prefix[r][c]=d;
 }
 for(int r=FP_SIDE-1;r>=0;r--)for(int c=FP_SIDE-1;c>=0;c--) {
  int d=s->prefix[r][c];
  if(r+1<FP_SIDE)d=imin(d,s->prefix[r+1][c]+1);
  if(c+1<FP_SIDE)d=imin(d,s->prefix[r][c+1]+1);
  s->prefix[r][c]=d;
 }
}
/* Walls still to place around a w x h room on this floor, ignoring mobs
 * (they move; a cached room must not flip with them). -1 when the room
 * cannot be sealed from inside: an unknown wall, an item on a wall cell, or
 * an open long side of a 1-wide room (facing it would step out). */
static int room_walls(const FPAgent *a,int anchor,int w,int h) {
 int r=anchor/FP_SIDE,c=anchor%FP_SIDE,n=0;
 for(int rr=r;rr<r+h;rr++)for(int cc=c;cc<c+w;cc++)if(!inside(rr,cc)||!walkable(block(a,rr,cc)))return -1;
 for(int i=0;i<2*(w+h);i++) {
  int wr,wc,d;wall_position(i,w,h,&wr,&wc,&d);
  int rr=r+wr,cc=c+wc;if(!inside(rr,cc))return -1;
  int b=block(a,rr,cc);if(b==UNKNOWN)return -1;
  if(walkable(b)||b==FP_WATER||b==FP_LAVA) {
   if((d<=2 && w==1)||(d>=3 && h==1)||a->floors[a->level].items[cell(rr,cc)])return -1;
   n++;
  }
 }
 return n;
}
/* The keep: the sealable room around the Ice up ladder with fewest walls.
 * Ice shelters elsewhere drew frost trolls that waited outside; leaving at
 * food 3 meant walking to the ladder through them (base ep 13, 50, 154). A
 * shelter around the ladder is left by climbing, and a climb taken from it
 * sealed lands back in safety: Ice freezes while we are away. */
static int keep_room(FPAgent *a) {
 if(a->level!=7 || !ice_phase(a))return -1;
 int L=a->floors[7].up;if(L<0)return -1;
 if(a->ice_keep>0 && room_walls(a,a->ice_keep-1,a->ice_keep_w,a->ice_keep_h)>=0)return a->ice_keep-1;
 int best=-1,bn=INF,lr=L/FP_SIDE,lc=L%FP_SIDE;
 for(int mode=0;mode<3;mode++) {
  int w=mode==1?1:2,h=mode==2?1:2;
  for(int r0=lr-h+1;r0<=lr;r0++)for(int c0=lc-w+1;c0<=lc;c0++) {
   int n=inside(r0,c0)?room_walls(a,cell(r0,c0),w,h):-1;
   if(n>=0 && n<bn){bn=n;best=cell(r0,c0);a->ice_keep_w=w;a->ice_keep_h=h;}
  }
 }
 a->ice_keep=best+1;return best;
}
static int in_room(const FPAgent *a,int anchor,int w,int h) {
 int r=anchor/FP_SIDE,c=anchor%FP_SIDE;return a->r>=r && a->r<r+h && a->c>=c && a->c<c+w;
}
static int recover(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int start=cell(a->r,a->c),best=a->recovery_goal;
 int w=a->recovery_width,h=a->recovery_height;
 if(best<0 || chamber_walls(a,o,s,best,w,h)<0 || s->dist[best]>350) {
  best=-1;int costbest=INF;const FPFloor *f=&a->floors[a->level];
  water_distances(f,s);
  int keep=keep_room(a);
  if(keep>=0 && s->dist[keep]<=350){best=keep;w=a->ice_keep_w;h=a->ice_keep_h;}
  else for(int p=f->known_lo;p<=f->known_hi;p++) {
   if(s->dist[p]>350)continue;
   int water_distance=s->prefix[p/FP_SIDE][p%FP_SIDE];
   for(int mode=0;mode<3;mode++) {
    int ww=mode==1?1:2,hh=mode==2?1:2;
    int n=chamber_walls(a,o,s,p,ww,hh);if(n<0)continue;
    /* Healing can require another drink. Short hydration trips reduce
     * exposure to enemies while health is still low. */
    int cost=s->dist[p]+n*50+risk(a,o,s,p)*40+water_distance*25;
    if(cost<costbest){best=p;costbest=cost;w=ww;h=hh;}
   }
  }
 }
 if(best<0)return o->energy<=1?FP_SLEEP:-1;
 a->recovery_goal=best;a->recovery_width=w;a->recovery_height=h;
 int n=chamber_walls(a,o,s,best,w,h);
 if(n>o->inv[1]+(o->inv[0]>4 && (a->level<1 || a->level>3)?(o->inv[0]-4)/2:0)) {
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
  int stand=cell(rr-dr[d],cc-dc[d]),partner=cell(rr-2*dr[d],cc-2*dc[d]);
  int action=o->inv[1]?FP_PLACE_STONE:o->inv[0]>=6 && walkable(b) && (a->level<1 || a->level>3)?FP_PLACE_TABLE:-1;
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
/* Every floor is a 48x48 map. Fire is fully lit, so every in-view cell is
 * observed and off-map cells are remembered as BOUNDARY. The span of known
 * in-map cells bounds the window; an in-map cell beside a boundary cell fixes
 * that edge, and with it the opposite one. Unknown cells outside the window
 * cannot hold the exit, yet they kept drawing searches to the map's rim. */
static void fire_bounds(const FPFloor *f,int *top,int *bottom,int *left,int *right) {
 int rmin=FP_SIDE,rmax=-1,cmin=FP_SIDE,cmax=-1,t=-1,b=FP_SIDE,l=-1,rt=FP_SIDE;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int m=f->map[p];if(m==UNKNOWN||m==FP_BOUNDARY||m==FP_INVALID)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  if(r<rmin)rmin=r;if(r>rmax)rmax=r;if(c<cmin)cmin=c;if(c>cmax)cmax=c;
  if(r>0 && f->map[p-FP_SIDE]==FP_BOUNDARY && r>t)t=r;
  if(r<FP_SIDE-1 && f->map[p+FP_SIDE]==FP_BOUNDARY && r<b)b=r;
  if(c>0 && f->map[p-1]==FP_BOUNDARY && c>l)l=c;
  if(c<FP_SIDE-1 && f->map[p+1]==FP_BOUNDARY && c<rt)rt=c;
 }
 *top=0;*bottom=FP_SIDE-1;*left=0;*right=FP_SIDE-1;
 if(rmax<0)return;
 int T=rmax-47,B=rmin+47,L=cmax-47,R=cmin+47;
 if(t>=0 && t>T)T=t;if(b<FP_SIDE && b<B)B=b;if(l>=0 && l>L)L=l;if(rt<FP_SIDE && rt<R)R=rt;
 if(T+47<B)B=T+47;if(B-47>T)T=B-47;if(L+47<R)R=L+47;if(R-47>L)L=R-47;
 *top=T>0?T:0;*bottom=B<FP_SIDE-1?B:FP_SIDE-1;*left=L>0?L:0;*right=R<FP_SIDE-1?R:FP_SIDE-1;
}
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s);
static int explore(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 if(a->level==6 && fire_phase(a)){int act=fire_search(a,o,cfg,s);if(act>=0)return act;}
 FPFloor *f=&a->floors[a->level];
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0;s->prefix[r+1][0]=0;for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 int best=-1;float scorebest=-1e30f;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(s->dist[p]<=0||s->dist[p]>600)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  /* Long elemental excursions consume scarce supplies. Prefer closer
   * frontiers while retaining the configured exploration tradeoff. */
  float bonus=cfg->frontier_bonus*(a->level>=6?0.75f:1);
  float score=bonus*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->visits[p],50);
  if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
  score+=(float)(((uint64_t)p*UINT64_C(0x9e3779b97f4a7c15)^a->rng)>>56)*0.002f;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best>=0){a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);}
 (void)word(a);return FP_NOOP;
}
static int travel(FPAgent *a,const FPObservation *o,FPScratch *s,int down) {
 int p=down?a->floors[a->level].down:a->floors[a->level].up;
 if(p<0 || s->dist[p]>=INF)return -1;
 if(p==cell(a->r,a->c))return down?FP_DESCEND:FP_ASCEND;
 return move_path(a,o,s,p);
}
/* Frontier walker shared with the fire stream (same function there): stands
 * whose 9x11 view still holds four unseen cells, at any distance. */
static int frontier_walk(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 FPFloor *f=&a->floors[a->level];(void)o;
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 int best=-1;float scorebest=-1e30f;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(s->dist[p]<=0||s->dist[p]>=INF)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
  int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
  if(n<4)continue;
  float score=cfg->frontier_bonus*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->visits[p],50);
  if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
  if(score>scorebest){best=p;scorebest=score;}
 }
 if(best<0)return -1;
 a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);
}
/* Exit search on Fire. The general explorer scores every cell within 60
 * steps, so once the ladder's surroundings were known, cells with nothing
 * unseen nearby outscored real frontiers beyond lava or rock: ep 0 wandered
 * there for ~90 visits before finding an exit 66 cells away. Consider only
 * stands that reveal unseen in-map terrain, at any distance, and first those
 * the drink left can reach and return from; with none, refill first. */
static int fire_search(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 FPFloor *f=&a->floors[6];int T,B,L,R;fire_bounds(f,&T,&B,&L,&R);
 memset(s->prefix[0],0,sizeof(s->prefix[0]));
 for(int r=0;r<FP_SIDE;r++) {
  int n=0,in=r>=T && r<=B;s->prefix[r+1][0]=0;
  for(int c=0;c<FP_SIDE;c++){n+=f->map[cell(r,c)]==UNKNOWN && in && c>=L && c<=R;s->prefix[r+1][c+1]=s->prefix[r][c+1]+n;}
 }
 /* A refill trip only helps when the mines stop would top drink up (see the
  * descent provisioning): above that, push to the best stand instead of
  * bouncing between the two ladders (ep 172). */
 int up=f->up,budget=(o->drink-1)*20-10,full=o->drink>7+2*o->dex-3;
 for(int pass=0;pass<2;pass++) {
  int best=-1;float scorebest=-1e30f;
  for(int p=f->known_lo;p<=f->known_hi;p++) {
   if(s->dist[p]<=0||s->dist[p]>=INF)continue;
   int r=p/FP_SIDE,c=p%FP_SIDE,r0=r>4?r-4:0,c0=c>5?c-5:0,r1=imin(r+5,FP_SIDE),c1=imin(c+6,FP_SIDE);
   int n=s->prefix[r1][c1]-s->prefix[r0][c1]-s->prefix[r1][c0]+s->prefix[r0][c0];
   if(n<4)continue;
   /* Manhattan distance understates the walk back around lava. */
   if(!pass && s->dist[p]/10+(up>=0?(abs(r-up/FP_SIDE)+abs(c-up%FP_SIDE))*5/4:0)>budget)continue;
   float score=cfg->frontier_bonus*0.75f*n-0.4f*s->dist[p]-cfg->revisit_cost*4*imin(f->visits[p],50);
   if(p==a->last_frontier && a->t<a->frontier_until)score+=12;
   score+=(float)(((uint64_t)p*UINT64_C(0x9e3779b97f4a7c15)^a->rng)>>56)*0.002f;
   if(score>scorebest){best=p;scorebest=score;}
  }
  if(best>=0){a->last_frontier=best;a->frontier_until=a->t+10;return move_path(a,o,s,best);}
  if(!pass && !full){int act=travel(a,o,s,0);if(act>=0)return act;}
 }
 return -1;
}
/* Reach a torch placement with the correct facing by approaching its stand
 * from behind. Turning towards an empty target would move onto that target. */
static int graveyard_light(FPAgent *a,const FPObservation *o,FPScratch *s) {
 const FPFloor *f=&a->floors[8];int start=cell(a->r,a->c),best=-1,dir=0,costbest=INF;
 if(!o->inv[FP_I_TORCH])return -1;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(!place_valid(a,s,p,FP_PLACE_TORCH))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=0;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
  if(gain<GRAVEYARD_TORCH_GAIN)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||s->mobs[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d)return FP_PLACE_TORCH;
   int cost=s->dist[partner]+10;
   if(cost<costbest && cost<=150){best=partner;dir=d;costbest=cost;}
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
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  if(!place_valid(a,s,p,FP_PLACE_TORCH))continue;
  int r=p/FP_SIDE,c=p%FP_SIDE,gain=0;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
  if(gain<min_gain)continue;
  for(int d=1;d<=4;d++) {
   int sr=r-dr[d],sc=c-dc[d],pr=sr-dr[d],pc=sc-dc[d];
   if(!inside(sr,sc)||!inside(pr,pc)||!walkable(block(a,sr,sc))||s->mobs[cell(sr,sc)])continue;
   int stand=cell(sr,sc),partner=cell(pr,pc);
   if(start==stand && o->dir==d)return FP_PLACE_TORCH;
   if(s->dist[partner]>max_cost)continue;
   /* One unseen cell is worth a step: large dark regions before small pockets. */
   int cost=s->dist[partner]+10-10*gain;
   if(cost<costbest){best=partner;dir=d;costbest=cost;}
  }
 }
 if(best<0)return -1;
 return best==start?dir:move_path(a,o,s,best);
}
static int tactical(FPAgent *a,const FPObservation *o,FPScratch *s) {
 int start=cell(a->r,a->c);
 /* Avoid a lethal simultaneous attack. Cooldowns and health are inferred
  * from consecutive public views and our own attacks, with conservative
  * defaults when an enemy first becomes visible. */
 float incoming=0;int faced=cell(a->r+dr[o->dir],a->c+dc[o->dir]);
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];if(e.kind || e.cooldown>0 || abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)!=1)continue;
  if(e.p==faced && hit_damage(o,e.type,e.kind)>=e.health)continue;
  incoming+=attack_damage(o,e.type);
 }
 /* Protected Ice fights can finish during the attacker's cooldown even
  * with little health left. Keep a half-point margin for that first hit;
  * retreat before a lethal hit, rather than abandoning every low-HP cast. */
 if(incoming>=o->health-(o->level==7?0.5f:1) || (o->level!=7 && o->health<=4 && incoming>0)) {
  int best=-1;float costbest=incoming;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c)||!walkable(block(a,r,c))||s->mobs[cell(r,c)]||s->arrow_next[cell(r,c)])continue;
   float cost=0;for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)cost+=attack_damage(o,e.type);
   }
   if(cost<costbest){best=d;costbest=cost;}
  }
  /* A realm chase only ends in a corner: pigmen and frost trolls keep pace
   * (ep 16 danced 23 steps at 5.2 health with full mana). The up ladder is
   * the one exit they cannot follow. Climb when on it, and prefer the first
   * step of the route to it among equally safe retreats. */
  if(a->level==4 || a->level==5 || a->level==6 || a->level==7 || (a->level==1 && !a->floors[5].seen) || (a->level==3 && mid_phase(a))) {
   int up=a->floors[a->level].up;
   if(up==start)return FP_ASCEND;
   if(best>=0 && up>=0 && s->dist[up]<INF && s->first[up]>0) {
    int d=s->first[up],r=a->r+dr[d],c=a->c+dc[d];
    if(inside(r,c) && walkable(block(a,r,c)) && !s->mobs[cell(r,c)] && !s->arrow_next[cell(r,c)]) {
     float cost=0;for(int i=0;i<a->enemy_count;i++) {
      FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)cost+=attack_damage(o,e.type);
     }
     if(cost<=costbest)return d;
    }
   }
  }
  if(best>=0)return best;
 }
 /* Stone shield (mid stream's Vault finding): a shot flying at us along the
  * faced line stops at a stone placed in the faced empty cell. */
 /* Mid stream: agents carry 60-100 stones into the Vault. A shot about to
  * enter the faced empty cell stops at a stone placed there; it cannot be
  * dodged in a one-cell corridor, where most Vault arrows land. */
 if(((ICEB(0) && ice_realm(a)) || ((a->level==3 || a->level==4) && mid_phase(a))) && o->inv[FP_I_STONE]>0) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir],vi=(dr[o->dir]+4)*11+dc[o->dir]+5;
  if(inside(fr,fc) && s->arrow_next[cell(fr,fc)] && !o->mobs[vi][3] && place_valid(a,s,cell(fr,fc),FP_PLACE_STONE)) {
   /* Only a shot flying along our facing line towards us. */
   int r2=fr+dr[o->dir],c2=fc+dc[o->dir],vi2=(2*dr[o->dir]+4)*11+2*dc[o->dir]+5;
   if(inside(r2,c2) && o->mobs[vi2][3])return FP_PLACE_STONE;
  }
 }
 if(s->arrow_next[start]) {
  int best=-1,costbest=INT_MAX;
  for(int d=1;d<=4;d++) {
   int r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int p=cell(r,c);
   if(!walkable(block(a,r,c))||s->mobs[p]||s->arrow_next[p])continue;
   int cost=risk(a,o,s,p);if(cost<costbest){best=d;costbest=cost;}
  }if(best>=0)return best;
 }
 for(int i=0;i<4;i++) {
  int d=1+(o->dir-1+i)%4,r=a->r+dr[d],c=a->c+dc[d];if(!inside(r,c))continue;int p=cell(r,c);
  if(!(s->mobs[p]&5))continue;
  if(o->dir!=d)return d;
  int type=s->melee[p]?s->melee[p]:s->ranged[p];
  /* Sword damage resolves before a melee attack; spells resolve after it.
   * Finish a weakened elemental immediately and preserve the remaining mana. */
  if(type>=7 && hit_damage(o,type,s->ranged[p]!=0)>=s->enemy_health[p])return FP_DO;
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
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
    int df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(!e.kind && dd==2 && df==1 && hit_damage(o,e.type,0)<e.health && hit_damage(o,e.type,0)+damage>=e.health)return shot;
   }
 }
 /* Mid stream: the same pre-shot for lizards (11) and knights (12) against
  * an iron sword (7.5): an arrow (5) or a spell in the faced cell as the mob
  * steps in lets the first blow kill it before its first attack. Arrows
  * are plentiful (median 34 at death) and cost no mana. */
 if((a->level==3 || a->level==4) && mid_phase(a) && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  int arrow=o->bow && o->inv[FP_I_ARROW]>0;
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
    int df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);float blow=hit_damage(o,e.type,0);
    if(e.kind || dd!=2 || df!=1 || blow>=e.health)continue;
    float spell=projectile_damage(o,o->spells[0]?3:4,e.type,0),shot=projectile_damage(o,5,e.type,0);
    if(arrow && blow+shot>=e.health)return FP_SHOOT;
    if(o->mana>=2 && (o->spells[0] || o->spells[1]) && blow+spell>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
 }
 /* A troll two cells away steps next into one of our neighbours and
  * attacks after our first blow, which cannot kill 20 HP. When it can step
  * into the faced cell, a spell cast now meets it there; if the spell and
  * one sword blow suffice, the blow then kills it before its first attack. */
 /* Sewers (mid stream): the first Sewers chest holds a book. A spell cast
  * into a lizard's arrival cell lets one iron blow (7.5) finish its 11
  * health before it bites; iron-sword entrants pass the Sewers about half
  * as often as diamond-sword ones. */
 if((a->level==4 || a->level==5 || (a->level==3 && mid_phase(a))) && o->mana>=2 && (o->spells[0] || o->spells[1])) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];float spell=3*(1+0.5f*(o->intelligence-1));
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
   for(int i=0;i<a->enemy_count;i++) {
    FPEnemy e=a->enemies[i];float blow=hit_damage(o,e.type,0),pending=0;
    int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c),df=abs(e.p/FP_SIDE-fr)+abs(e.p%FP_SIDE-fc);
    if(e.kind || blow>=e.health)continue;
    /* A shot waiting in the faced cell hits the troll as it steps in. */
    for(int j=0;j<a->shot_count;j++)if(a->shot_pos[j]==cell(fr,fc) && a->shot_dir[j]==o->dir)
     pending+=projectile_damage(o,a->shot_type[j],e.type,0);
    /* Without an enchanted sword two spells are needed: start one move
     * earlier, so the first shot waits in the faced cell. */
    if(d==3 && df==2 && o->mana>=4 && blow+spell<e.health && blow+2*spell>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
    if(d==2 && df==1 && blow+spell+pending>=e.health)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
 }
 /* Frost trolls resist the sword and require three fireballs. Cast into
  * their approach before adjacency, including a diagonal final step. The
  * missile persists in the faced cell until after the next mob movement. */
 if(a->level==7 && o->spells[0] && o->mana>=2) {
  int fr=a->r+dr[o->dir],fc=a->c+dc[o->dir];
  if(inside(fr,fc) && walkable(block(a,fr,fc)) && !s->mobs[cell(fr,fc)])
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
  if(s->mobs[p]&5) {
   int type=s->melee[p]?s->melee[p]:s->ranged[p];
   if(o->mana>=2 && type==7 && o->spells[1])return FP_ICEBALL;
   if(o->mana>=2 && type==8 && o->spells[0])return FP_FIREBALL;
   if(o->mana>=2 && k>=2 && (o->spells[0] || o->spells[1])) {
    float spell=3*(1+0.5f*(o->intelligence-1));
    float arrow=4*(1+0.2f*(o->dex-1))*(type==5?0.5f:type>=7?0.1f:1);
    if(!o->bow || !o->inv[9] || spell>=arrow)return o->spells[0]?FP_FIREBALL:FP_ICEBALL;
   }
   /* Dungeon orcs hit for 3 once adjacent and need two sword strikes. An
    * arrow while one approaches in line lets the first strike finish it. */
   if(o->bow && o->inv[9]>0 && k>=2 && (s->ranged[p] || o->health<6 || a->level>=4 || a->level==1))return FP_SHOOT;
   break;
  }
  if(solid(block(a,r,c)) || block(a,r,c)==UNKNOWN)break;
 }
 /* Face the orc's unobstructed greedy approach while it is still far
  * enough to turn safely. Let it enter the pre-shot cell instead of
  * walking into its reach with the wrong facing. */
 if(a->level==1 && a->dungeon_wait<24 && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  float shot=o->bow && o->inv[FP_I_ARROW]?5*(1+0.2f*(o->dex-1)):3*(1+0.5f*(o->intelligence-1));
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);float blow=hit_damage(o,e.type,0);
   if(!e.kind && dd>=3 && dd<bd && blow<e.health && blow+shot>=e.health){best=i;bd=dd;}
  }
  if(best<0)a->dungeon_wait=0;
  if(best>=0) {
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,r=er,c=ec,clear=1;
   while(clear && abs(r)+abs(c)>1) {
    int moved=0;
    if(abs(r)>=abs(c)){int nr=r-(r>0?1:-1);if(walkable(block(a,a->r+nr,a->c+c))){r=nr;moved=1;}}
    if(!moved && abs(c)>=abs(r)){int nc=c-(c>0?1:-1);if(walkable(block(a,a->r+r,a->c+nc))){c=nc;moved=1;}}
    clear=moved;
   }
   if(clear) {
    if(er*dr[o->dir]+ec*dc[o->dir]>0){a->dungeon_wait++;return FP_NOOP;}
    int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
    int nr=a->r+dr[want],nc=a->c+dc[want];
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || bd>=4))return want;
   }
  }
 }
 /* Mid stream: face the greedy approach of a lizard or knight that one
  * blow plus a pre-shot kills, and let it come into the pre-shot cell;
  * never while standing in a ranged mob's firing line. */
 if((a->level==3 || a->level==4) && mid_phase(a) && a->mid_wait<24 && ((o->mana>=2 && (o->spells[0] || o->spells[1])) || (o->bow && o->inv[FP_I_ARROW]))) {
  float shot=o->bow && o->inv[FP_I_ARROW]?projectile_damage(o,5,4,0):projectile_damage(o,3,4,0);
  int best=-1,bd=7,exposed=0;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);float blow=hit_damage(o,e.type,0);
   if(e.kind && firing_line(a,a->r,a->c,e.p/FP_SIDE,e.p%FP_SIDE))exposed=1;
   if(!e.kind && dd>=3 && dd<bd && blow<e.health && blow+shot>=e.health){best=i;bd=dd;}
  }
  if(best<0)a->mid_wait=0;
  if(best>=0 && !exposed) {
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,r=er,c=ec,clear=1;
   while(clear && abs(r)+abs(c)>1) {
    int moved=0;
    if(abs(r)>=abs(c)){int nr=r-(r>0?1:-1);if(walkable(block(a,a->r+nr,a->c+c))){r=nr;moved=1;}}
    if(!moved && abs(c)>=abs(r)){int nc=c-(c>0?1:-1);if(walkable(block(a,a->r+r,a->c+nc))){c=nc;moved=1;}}
    clear=moved;
   }
   if(clear) {
    if(er*dr[o->dir]+ec*dc[o->dir]>0){a->mid_wait++;return FP_NOOP;}
    int want=abs(er)>=abs(ec)?(er>0?FP_DOWN:FP_UP):(ec>0?FP_RIGHT:FP_LEFT);
    int nr=a->r+dr[want],nc=a->c+dc[want];
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || bd>=4)){a->mid_wait++;return want;}
   }
  }
 }
 /* Pre-casting needs the troll to arrive next to the faced cell. Walking
  * on, or turning away, lets it arrive behind or beside us instead. Face
  * the side it approaches from while it is still three or more cells away,
  * then let it come. Its moves shrink its larger offset first, so a troll
  * in front of us stays in front until it is adjacent. */
 if((a->level==4 || a->level==5) && o->mana>=2 && (o->spells[0] || o->spells[1]) && (a->level==4?a->vault_wait:a->troll_wait)<24) {
  int *wait=a->level==4?&a->vault_wait:&a->troll_wait;
  int best=-1,bd=7;
  for(int i=0;i<a->enemy_count;i++) {
   FPEnemy e=a->enemies[i];int dd=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);
   if(!e.kind && dd>=3 && dd<bd && hit_damage(o,e.type,0)<e.health){best=i;bd=dd;}
  }
  if(best<0)*wait=0;
  if(best>=0) {
   FPEnemy e=a->enemies[best];int er=e.p/FP_SIDE-a->r,ec=e.p%FP_SIDE-a->c,r=er,c=ec,clear=1;
   /* Only a troll whose greedy approach is unobstructed will arrive. */
   while(clear && abs(r)+abs(c)>1) {
    int moved=0;
    if(abs(r)>=abs(c)){int nr=r-(r>0?1:-1);if(walkable(block(a,a->r+nr,a->c+c))){r=nr;moved=1;}}
    if(!moved && abs(c)>=abs(r)){int nc=c-(c>0?1:-1);if(walkable(block(a,a->r+r,a->c+nc))){c=nc;moved=1;}}
    clear=moved;
   }
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
   if(!e.kind && dd>=2 && dd<bd && hit_damage(o,e.type,0)>=e.health){best=i;bd=dd;}
  }
  int exposed=0;
  for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind && firing_line(a,a->r,a->c,a->enemies[i].p/FP_SIDE,a->enemies[i].p%FP_SIDE))exposed=1;
  if(best<0)a->early_wait=0;
  if(best>=0 && !exposed) {
   FPEnemy e=a->enemies[best];int r=e.p/FP_SIDE-a->r,c=e.p%FP_SIDE-a->c,clear=1;
   while(clear && abs(r)+abs(c)>1) {
    int moved=0;
    if(abs(r)>=abs(c)){int nr=r-(r>0?1:-1);if(walkable(block(a,a->r+nr,a->c+c)) && !s->mobs[cell(a->r+nr,a->c+c)]){r=nr;moved=1;}}
    if(!moved && abs(c)>=abs(r)){int nc=c-(c>0?1:-1);if(walkable(block(a,a->r+r,a->c+nc)) && !s->mobs[cell(a->r+r,a->c+nc)]){c=nc;moved=1;}}
    clear=moved;
   }
   if(clear && abs(r)+abs(c)==1) {
    int want=r>0?FP_DOWN:r<0?FP_UP:c>0?FP_RIGHT:FP_LEFT;
    if(o->dir==want){a->early_wait++;return FP_NOOP;}
    int nr=a->r+dr[want],nc=a->c+dc[want];
    /* A blocked neighbour turns us in place; an open one is a step, safe
     * only while the mob is four or more cells away. */
    if(known_direction(a,want) && (!inside(nr,nc) || !walkable(block(a,nr,nc)) || s->mobs[cell(nr,nc)] || bd>=4)){a->early_wait++;return want;}
   }
  }
 }
 return -1;
}

/* The Vault-side enchanting trip only seeks a remembered fire table. When
 * none is known, the climb returns at once and repeats indefinitely. */
static int fire_table_known(const FPAgent *a) {
 const FPFloor *f=&a->floors[4];
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_FIRE_TABLE)return 1;
 return 0;
}
/* The Sewers ice-armour trip is pending: sapphires in hand, armour not yet
 * fully ice-enchanted. Every Ice step before it is taken with fire armour
 * (frost troll hits 5.8-7.4), so nothing on Ice may delay it, and nothing
 * on its outbound route may turn it back. */
static int enchant_trip(const FPAgent *a,const FPObservation *o) {
 int ice=0;for(int i=0;i<4;i++)ice+=o->armour_enchant[i]==2;
 return a->floors[7].seen && armour(o) && o->inv[FP_I_SAPPHIRE] && ice<4;
}
/* Every Vault has exactly one fire table. Searches that gave up at 600
 * steps sent 199 of 5,725 Fire entrants down with unspent rubies. */
static int vault_limit(const FPAgent *a,const FPObservation *o) {
 return (troll_phase(a) || fire_phase(a)) && o->inv[FP_I_RUBY]>=2?1800:600;
}
/* A Vault trip is worth starting while the table is known or the search
 * budget lasts. In the troll phase a known but unreachable table (seen
 * across unexplored darkness) also spends the budget, or the trip
 * climbs, fails to reach it, descends and climbs again (ep 541, 186). */
static int vault_trip_ok(const FPAgent *a,const FPObservation *o) {
 if(troll_phase(a))return a->vault_search<vault_limit(a,o);
 /* Fire phase: a table found unusable (VAULT_UNUSABLE) ends the trips. */
 return (fire_table_known(a) && a->vault_search<VAULT_UNUSABLE) || a->vault_search<vault_limit(a,o);
}
/* Cheapest stand next to remembered water on this floor (path cost, INF if none). */
static int water_cost(const FPAgent *a,FPScratch *s) {
 const FPFloor *f=&a->floors[a->level];int best=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_WATER || f->map[p]==FP_FOUNTAIN)
  for(int d=1;d<=4;d++){int r=p/FP_SIDE-dr[d],c=p%FP_SIDE-dc[d];if(inside(r,c) && s->dist[cell(r,c)]<best)best=s->dist[cell(r,c)];}
 return best;
}
/* Early stream (floors 0-4 before the first Troll Mines arrival). */
static int early(const FPAgent *a) { return !a->floors[5].seen; }
static int sealed_in(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!a->recovering || a->recovery_goal<0)return 0;
 int rr=a->recovery_goal/FP_SIDE,cc=a->recovery_goal%FP_SIDE;
 return a->r>=rr && a->r<rr+a->recovery_height && a->c>=cc && a->c<cc+a->recovery_width &&
        chamber_walls(a,o,s,a->recovery_goal,a->recovery_width,a->recovery_height)==0;
}
static int autonomous(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 a->goal=-1;int act=-1,fire=fire_phase(a);
 if(o->sleeping||o->resting)return emit(a,o,s,FP_NOOP,FP_RECOVER);
 /* Spend the Fire floor's XP before travel or enchantment preparation
  * takes priority. Stronger spells shorten elemental fights, and improve
  * mana recovery. A ready attack or incoming projectile still interrupts. */
 /* Cross-cutting option, OFF: the floor-7 point on dex (food, drink and
  * energy last 14% longer, maxima 11) instead of int 5. Screened -2.1pp:
  * returns improve but frost trolls need four fireballs at int 4. */
 if(ICE_DEX7 && ice_phase(a) && o->xp && o->dex<2 && incoming_damage(a,o,s,FP_NOOP)==0)
  return emit(a,o,s,FP_LEVEL_DEX,FP_UPGRADE);
 if(a->level==6 && o->xp && o->intelligence<5 && incoming_damage(a,o,s,FP_NOOP)==0)
  return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
 /* Floor travel happens before enemy attacks. Take an unlocked exit
  * immediately when it leads to the next unexplored realm. */
 if((a->level==6 || a->level==7) && !a->floors[a->level+1].seen && o->clear && a->floors[a->level].down==cell(a->r,a->c))
  return emit(a,o,s,FP_DESCEND,FP_TRAVEL);
 /* Known healing can interrupt a fight even when every retreat is blocked. */
 for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==0 && o->health<=6)
  return emit(a,o,s,FP_POTION_RED+i,FP_RECOVER);
 /* A troll costs one unanswered hit even when we strike first: 20 HP needs
  * two diamond-sword blows. After that hit, the next troll would leave us
  * near death, so heal while another hit would leave less than 3 health. */
 int troll_low=a->level==5 && o->health<8+o->str-0.01f && o->health-attack_damage(o,6)<3;
 /* One potion colour restores 8 health, and most agents reach the mines
  * carrying it unidentified. Test unknown colours while a harmful one
  * (-3 health) is survivable and no troll is close; then spend healing
  * potions when a troll would otherwise land the hit that kills. */
 if(a->level==5) {
  int near=0,heal=-1;
  for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=4)near=1;
  for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==0)heal=i;
  if(heal>=0 && o->health-attack_damage(o,6)<3 && (near || o->health<=4))return emit(a,o,s,FP_POTION_RED+heal,FP_RECOVER);
  if(heal<0 && !near && o->health>=5 && o->health<=8+o->str-3)
   for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]<0)return emit(a,o,s,FP_POTION_RED+i,FP_RECOVER);
 }
 /* Supply trips at that health meet the next troll. Recovery holds us in
  * shelter until drink or food is critical, as it does once started. */
 int hold=troll_low && o->drink>2 && o->food>2;
 /* Realm retreats follow the route to the up ladder (tactical). */
 int routed=a->level==4 || a->level==5 || a->level==6 || a->level==7 || (a->level==1 && !a->floors[5].seen) || (a->level==3 && mid_phase(a));
 if(routed)paths(a,o,cfg,s);
 /* Pigmen keep pace and elementals fire point-blank once cornered, so a
  * low fight on Fire ends in death (pre-clear deaths sat a median ~8
  * cells from the up ladder). Mobs never follow a ladder: take it while
  * it is near, before another exchange. */
 if(fire && a->level==6 && o->health<=fire_retreat(o)) {
  int up=a->floors[6].up;
  if(up==cell(a->r,a->c))return emit(a,o,s,FP_ASCEND,FP_TRAVEL);
  if(up>=0 && s->dist[up]<=250){act=travel(a,o,s,0);if(act>=0)return emit(a,o,s,act,FP_TRAVEL);}
 }
 /* Mid stream: a low fight beside the up ladder ends with the climb, before
  * any tactical exchange. Camped agents at 3.2 health stood on the Vault
  * ladder casting at an archer until its next arrow killed them (ep 164 r0);
  * knight blows and arrows arrive in pairs, so with an archer about, leave
  * while one of each would still leave a point. */
 if(mid_phase(a) && (a->level==3 || a->level==4) && a->floors[a->level].up>=0 && o->health<8+o->str-0.01f) {
  int up=a->floors[a->level].up,d=abs(up/FP_SIDE-a->r)+abs(up%FP_SIDE-a->c),archer=0;
  float melee=attack_damage(o,a->level+1),shot=(a->level==4?5:4)*(1-0.1f*armour(o));
  for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=8)archer=1;
  float low=a->level==4?fmaxf(2*melee,archer?melee+shot+1:0):fmaxf(5,2*melee);
  if(o->health<=low) {
   if(d==0)return emit(a,o,s,FP_ASCEND,FP_TRAVEL);
   if(d==1 && routed && s->dist[up]<=20 && (act=travel(a,o,s,0))>=0)return emit(a,o,s,act,FP_TRAVEL);
  }
 }
 act=tactical(a,o,s);if(act>=0)return emit(a,o,s,act,FP_FIGHT);
 /* Mobs on unlit mine cells are not observed. A gnome beside us in the dark
  * struck every six steps while exploration waited for a frontier (ep 27:
  * four hits from full health, no reply); unseen attackers dealt 29 of the 79 blows in
  * sampled floor-2 deaths. After a loss no visible enemy explains, light the
  * facing cell, whose torch also lights every neighbour, or strike an unlit
  * facing cell when no torch is left. Never step into an unlit cell: moves
  * are tracked only onto known ground, so the map frame would drift. */
 /* Troll Mines too: an invisible troll beside us in the dark struck from
  * full health to death while the agent waited for a frontier (ep 22: 27
  * torches unused, five hits, no reply). */
 int unseen_level=a->level==2 || (a->level==5 && troll_phase(a));
 if(unseen_level && a->t && a->previous.level==a->level && o->health<a->previous.health-1.5f) {
  int seen=0;
  for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)==1)seen=1;}
  if(!seen)a->unseen_hit=a->t;
 }
 if(unseen_level && a->t-a->unseen_hit<=4) {
  int r=a->r+dr[o->dir],c=a->c+dc[o->dir];
  if(inside(r,c)) {
   int q=cell(r,c),b=block(a,r,c);
   /* Placement fails on a cell holding a mob: strike it instead. */
   int failed=a->level==5 && a->previous_action==FP_PLACE_TORCH && a->previous_target==q && o->inv[FP_I_TORCH]>=a->previous.inv[FP_I_TORCH];
   if(failed && (b==UNKNOWN || walkable(b)))return emit(a,o,s,FP_DO,FP_FIGHT);
   if(o->inv[FP_I_TORCH] && !a->floors[a->level].items[q] && !s->mobs[q] && (b==UNKNOWN||b==FP_GRASS||b==FP_PATH||b==FP_SAND))
    return emit(a,o,s,FP_PLACE_TORCH,FP_LIGHT);
   if(b==UNKNOWN)return emit(a,o,s,FP_DO,FP_FIGHT);
  }
  if(o->inv[FP_I_TORCH] && (act=place(a,o,s,FP_PLACE_TORCH))>=0)return emit(a,o,s,act,FP_LIGHT);
 }
 if(a->level==8 && a->boss_hits>=8 && o->boss_vulnerable)return emit(a,o,s,FP_NOOP,FP_RECOVER);
 if(!routed)paths(a,o,cfg,s);
#define TRY(expression,skill) do {act=(expression);if(act>=0)return emit(a,o,s,act,skill);} while(0)
 /* Each cleared Graveyard wave is a safe, unlimited recovery window:
  * food, drink and energy stop decaying on this floor. Restore health and
  * mana before touching the Necromancer and starting the next wave. */
 if(a->level==8 && o->boss_vulnerable) {
  if(o->health<8+o->str-0.01f && o->food>0 && o->drink>0 && o->energy>0)
   return emit(a,o,s,FP_NOOP,FP_RECOVER);
  if(o->mana<6+3*o->intelligence)return emit(a,o,s,FP_NOOP,FP_RECOVER);
  TRY(seek_block(a,o,s,FP_BOSS,100),FP_FIGHT);
 }
 /* Ice is pitch dark and has no coal: its exploration is torches, 1 coal
  * + 1 wood per 4. Fire stone is 5% coal and fire trees give wood. Stock
  * up close to the route once Fire is clear, before the first descent. */
 if(FIRE_ICEPREP && fire && a->level==6 && o->clear && o->health>=7 && o->inv[FP_I_PICK]>=1 && incoming_damage(a,o,s,FP_NOOP)==0) {
  if(o->inv[FP_I_COAL]<10)TRY(seek_block(a,o,s,FP_COAL,10),FP_GATHER);
  if(o->inv[0]<8)TRY(seek_block(a,o,s,FP_TREE,10),FP_GATHER);
 }
 if((a->level==6 || a->level==7) && !a->floors[a->level+1].seen && o->clear)TRY(travel(a,o,s,1),FP_TRAVEL);
 /* A shelter cannot be sealed beside realm melee, and walking off only
  * leads the chase. When one more hit would leave little, leave the floor:
  * mobs never follow a ladder, and the floor above is where we heal. */
 /* Dungeon retreat. Health returns one point per 25 steps, and a Dungeon
  * shelter is built under fireballs: low-health fights there ended in kiting
  * an orc beside a mage at 2-3 health (ep 173). Mobs never follow a ladder and
  * the Overworld has water, cows and stone for a safe heal. Leave when one
  * more orc blow or fireball pair would leave little. */
 if(a->level==1 && !a->floors[5].seen && a->floors[1].up>=0 && s->dist[a->floors[1].up]<=200) {
  int near=0;for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);if(d<=(e.kind?6:4))near=1;}
  if((near && o->health<=5) || o->health<=3)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* Heal and sleep on the Overworld, not in a Dungeon shelter: 28 of 48
  * sampled floor-1 deaths happened in recovery mode (14 asleep unsealed, 14
  * under fire while building or holding a shelter for 10-350 steps). When
  * the up ladder is close, a climb ends every pursuit and the Overworld has
  * stone, water and cows. Leave for sleep with energy to spare: climbs that
  * started at energy 3 crossed fights and reached the Overworld at energy 0
  * with health already draining (ep 14 r1, 124 r1). */
 if(a->level==1 && !a->floors[5].seen && a->floors[1].up>=0 && s->dist[a->floors[1].up]<=300 &&
    ((o->health<8+o->str-0.01f && o->health<=6) || o->energy<=5))TRY(travel(a,o,s,0),FP_TRAVEL);
  /* Sewers retreat (mid stream): three lizard bites or daggers end a visit
  * within twenty steps from near-full health, and shelters there are built
  * under dagger fire. Mobs never follow a ladder; the Mines above are
  * cleared and have water. Leave when two more bites would leave little. */
 if(a->level==3 && mid_phase(a) && a->floors[3].up>=0 && s->dist[a->floors[3].up]<=200) {
  int near=0;for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);if(d<=(e.kind?6:4))near=1;}
  if((near && o->health<=fmaxf(5,2*attack_damage(o,4))) || o->health<=3)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 if(routed && a->level!=1 && !(a->level==3 && mid_phase(a))) {
  int near=0;for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=4)near=1;
  if(near && (a->level!=5 || (!o->clear && a->floors[5].up>=0 && s->dist[a->floors[5].up]<=150)) && o->health<=attack_damage(o,a->level+1)+1)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* A low-health Vault retreat must precede food and fountain errands:
  * ep 4 hunted at 2.2 HP, while ep 87 alternated water and recovery. */
 if(a->level==4 && !a->recovering && o->health<8+o->str-0.01f &&
    o->health<=2*attack_damage(o,5))TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Troll recovery chases end in corners. Leave before supply errands or
  * shelter construction, unless the player already occupies a sealed room. */
 if(troll_low && !o->clear) {
  int near=0;for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=4)near=1;
  int sheltered=0;
  if(a->recovering && a->recovery_goal>=0) {
   int rr=a->recovery_goal/FP_SIDE,cc=a->recovery_goal%FP_SIDE;
   sheltered=a->r>=rr && a->r<rr+a->recovery_height && a->c>=cc && a->c<cc+a->recovery_width &&
             chamber_walls(a,o,s,a->recovery_goal,a->recovery_width,a->recovery_height)==0;
  }
  if(near && !sheltered && a->floors[5].up>=0 && s->dist[a->floors[5].up]<=150)TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* With the ice-armour trip pending, every Ice step is taken in fire armour
  * (frost troll hits 5.8-7.4; 4% of entrants died on this first visit, p90
  * 126 steps spent drinking or healing). Fire is the safer floor in fire
  * armour and the Troll Mines have water: drink only beside the arrival
  * ladder, then leave. Adjacent fights are still handled above. */
 if(a->level==7 && enchant_trip(a,o) && o->drink>=2) {
  if(o->drink<5+2*o->dex)TRY(seek_block(a,o,s,FP_WATER,4),FP_DRINK);
  TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 if(a->recovering && o->energy>=7+2*o->dex && o->health>=8+o->str-0.01f)a->recovering=0;
 if(o->drink>=7+2*o->dex){a->drinking=0;a->water_goal=-1;}
 if(o->drink<=(a->recovering?2:cfg->drink_at) || (!a->achieved[4] && o->drink<=8))a->drinking=1;
 if(a->drinking && !hold)TRY(seek_water(a,o,s),FP_DRINK);
 /* With no mines water known, the climb to the Vault's fountains at drink 0
  * starts while health already drains and often ends in a thirst death up
  * there. Leave while two drinks remain. */
 if(a->level==5 && a->drinking && o->drink<=2)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Without reachable water on floor 1, the climb used to start at zero drink,
  * after health had begun to fall, through corridors where mobs block the
  * route. Leave with enough drink for the trip. Agents climbing from dark
  * mines without torches arrived on floor 1 at zero drink and died there. */
 if((a->level==1 || a->level==2) && a->drinking && o->drink<=3)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* With no reachable water known, waiting in a shelter cannot restore
  * hydration. Search for water before recovery takes over. */
 if(a->level==0 && a->drinking && o->drink<=2)TRY(explore(a,o,cfg,s),FP_EXPLORE);
 /* The Vault has about four fountains and some maps have none. A recovery
  * sleep there can use up the remaining drink before one is found, and the
  * drink==0 climb below starts with health already draining. Climb to the
  * Sewers' plentiful water while the trip is still affordable. */
 if(a->level==4 && o->drink<=4 && seek_block(a,o,s,FP_WATER,100)<0)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Ice has no reliable food supply. Start the return before starvation
  * consumes the health needed to cross the cleared Fire Realm. */
 /* Seal the keep before climbing out of it, so that the return lands safe. */
 if(a->level==7 && o->food<=3 && ice_phase(a) && !melee_within(a,2) && o->inv[1]) {
  int keep=keep_room(a);
  if(keep>=0 && in_room(a,keep,a->ice_keep_w,a->ice_keep_h) && room_walls(a,keep,a->ice_keep_w,a->ice_keep_h)>0 &&
     chamber_walls(a,o,s,keep,a->ice_keep_w,a->ice_keep_h)>0) {
   a->recovery_goal=keep;a->recovery_width=a->ice_keep_w;a->recovery_height=a->ice_keep_h;
   int r=keep/FP_SIDE,c=keep%FP_SIDE,start=cell(a->r,a->c);
   for(int i=0;i<2*(a->ice_keep_w+a->ice_keep_h);i++) {
    int wr,wc,d;wall_position(i,a->ice_keep_w,a->ice_keep_h,&wr,&wc,&d);
    int rr=r+wr,cc=c+wc,b=block(a,rr,cc);
    if(!(walkable(b)||b==FP_WATER||b==FP_LAVA)||!place_valid(a,s,cell(rr,cc),FP_PLACE_STONE))continue;
    int stand=cell(rr-dr[d],cc-dc[d]),partner=cell(rr-2*dr[d],cc-2*dc[d]);
    if(start==stand && o->dir==d)return emit(a,o,s,FP_PLACE_STONE,FP_RECOVER);
    if(start==partner)return emit(a,o,s,d,FP_RECOVER);
    TRY(move_path(a,o,s,partner),FP_RECOVER);
   }
  }
 }
 if(a->level==7 && o->food<=3)TRY(travel(a,o,s,0),FP_TRAVEL);
 /* A food return must continue through Fire when no animal is reachable.
  * Descending straight back to Ice would repeat the trip without refilling. */
 if(a->level==6 && a->floors[7].seen && o->food<=2) {
  TRY(forage(a,o,s),FP_EAT);
  TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 if(fire && a->level==6) {
  /* An identified energy potion (+8) buys a full visit without sleeping. */
  if(o->energy<=cfg->sleep_at)
   for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==4)return emit(a,o,s,FP_POTION_RED+i,FP_RECOVER);
  /* Sleep and healing need water. A shelter here drains drink that no rest
   * restores, and the pack that gathers outside meets us on waking (ep 1).
   * Climb to the mines water for both; a player whose armour leaves a big
   * blow leaves before the second one can kill. */
  if(o->energy<=cfg->sleep_at || o->health<=fire_retreat(o))TRY(travel(a,o,s,0),FP_TRAVEL);
 }
 /* Fire has no water. Leave enough hydration and health for the route
  * back to the mines, rather than starting it after starvation begins. */
 if(a->level==6 && (o->drink<=2 || o->health<=4))TRY(travel(a,o,s,0),FP_TRAVEL);
 if(o->food<=(a->recovering||hold?2:cfg->eat_at)) {
  TRY(forage(a,o,s),FP_EAT);
 }
 /* Ice has no coal, and coal is a torch. Fire is lit and 5% of its stone
  * is coal; fire trees give wood. Collect near the route during supply
  * visits while healthy, so that Ice exploration never stalls unlit. */
 if(a->level==6 && a->floors[7].seen && !enchant_trip(a,o) && o->health>=6 && incoming_damage(a,o,s,FP_NOOP)==0) {
  if(o->inv[FP_I_COAL]<8)TRY(seek_block(a,o,s,FP_COAL,12),FP_GATHER);
  if(o->inv[0]<6)TRY(seek_block(a,o,s,FP_TREE,12),FP_GATHER);
 }
 if(a->level==7 && o->clear && o->inv[0]<4 && o->inv[FP_I_COAL]>0)TRY(ice_wood(a,o,s),FP_GATHER);
 /* A wood resupply from the Troll Mines is a ten-floor round trip. Torches,
  * the reason for wood there, are usually in stock (ep 70: 57 torches). */
 if(troll_phase(a) && a->level>=3 && o->inv[FP_I_TORCH]>=8)a->resupplying=0;
 if(a->level>0 && a->level<8 && o->inv[0]<4 && !a->resupplying && !a->wood_exhausted && !(troll_phase(a) && o->inv[FP_I_TORCH]>=8)){a->resupplying=1;a->resupply_tick=a->t;}
 /* Dungeon walls cannot be mined, so stone spent there is gone. Every
  * shelter needs it: agents without stone slept unsealed at energy 1 and a
  * single orc blow (3x3.5) killed them from full health (14 of 48 sampled
  * floor-1 deaths, ep 30, 72, 99). Fetch more from the Overworld in time. */
 if(a->level==1 && early(a) && o->inv[1]<3 && o->inv[FP_I_PICK]>0 && !a->resupplying){a->resupplying=1;a->resupply_tick=a->t;}
 if(a->level>0 && a->level<8 && (o->food==0 || o->drink==0))TRY(travel(a,o,s,0),FP_TRAVEL);
 /* Start Ice recovery with enough health to reach and seal a shelter.
  * Fully healed players need no shelter even with a low strength stat. */
 int needs_healing=o->health<8+o->str-0.01f && o->health<=(a->level==8?GRAVEYARD_RECOVER_HEALTH:a->level==7?10:6);
 if(troll_low)needs_healing=1;
 /* Vault knights cannot be killed without taking one hit, and two often
  * arrive together. Enter that fight only able to absorb both hits. */
 if(a->level==4 && o->health<8+o->str-0.01f && o->health<=2*attack_damage(o,5))needs_healing=1;
 /* A Sewers revisit is a retreat from the Vault. Descend again only at full
  * health: two knight hits there take most of a partial heal. */
 if(a->level==3 && a->floors[4].seen && !a->resupplying && o->health<8+o->str-0.01f)needs_healing=1;
 /* A Sewers ladder retreat lands beside the ladder back down, and the
  * pursuer waits frozen below it. Go back down healed. */
 if(a->level==2 && mid_phase(a) && !a->resupplying && o->health<8+o->str-0.01f)needs_healing=1;
 /* Vault shelters are built beside knights that chase at most of our speed:
  * a recovery started with one within three cells ends in death about one
  * time in four. Climbing leaves every pursuer frozen on this floor, and the
  * cleared Sewers have water for the long heal. A knight that follows cannot
  * attack while we keep moving, so the ladder's distance is not limited; this
  * also covers returns from the Troll Mines, which land far from it. */
 if(a->level==4 && !a->recovering && needs_healing && o->food>2 && o->drink>2 &&
    a->floors[4].up>=0)TRY(travel(a,o,s,0),FP_TRAVEL);
 if(a->level==8 && (a->recovering || (needs_healing && o->food>0 && o->drink>0))) {
  a->recovering=1;TRY(recover(a,o,s),FP_RECOVER);
 }
 /* Provision before a Troll Mines heal: long sealed heals ran food or drink
  * down to the leave-the-shelter threshold of 2 while health was still low
  * (healing is 46% of uncleared floor-5 time). Top up first when a pool or
  * a bat is close and no troll is. */
 if(a->level==5 && troll_phase(a) && !a->recovering && needs_healing && o->health>=4 && !melee_within(a,4)) {
  if(o->drink<=6 && water_cost(a,s)<=80){a->drinking=1;TRY(seek_water(a,o,s),FP_DRINK);}
  if(o->food<=5)TRY(hunt(a,o,s,2,8),FP_EAT);
 }
 /* The Dungeon sends tired agents up to sleep at energy 5. Sleep before
  * going back down, or the two rules bounce between the ladders (63 climbs
  * per world) until energy reaches the usual threshold of 3. */
 int tired=a->level==0 && !a->floors[5].seen && a->floors[1].seen && o->energy<=5;
 if((a->recovering || o->energy<=cfg->sleep_at || tired || (needs_healing && o->food>2 && o->drink>2)) && a->level<8) {
  a->recovering=1;act=recover(a,o,s);
  /* Never sleep unsealed in the Dungeon: climb to the Overworld instead. */
  if(act==FP_SLEEP && a->level==1 && early(a) && !sealed_in(a,o,s))TRY(travel(a,o,s,0),FP_TRAVEL);
  if(act>=0)return emit(a,o,s,act,FP_RECOVER);
 }

 /* Ice has no enchantment table. Reuse the cleared route to the Sewers
  * with the sapphires gathered in the mines, then resume the expedition.
  * Armour takes precedence over spending a preparation gem on the sword. */
 if(a->floors[7].seen && armour(o) && o->inv[FP_I_SAPPHIRE]) {
  int ice=0;for(int i=0;i<4;i++)ice+=o->armour_enchant[i]==2;
  if(ice<4) {
   if(a->level>3 && a->level<=7)TRY(travel(a,o,s,0),FP_TRAVEL);
   if(a->level==3) {
    if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_ARMOUR,160),FP_MAGIC);
    else TRY(recover(a,o,s),FP_RECOVER);
    /* An unseen table or recovering mana must not send us back down
     * into an immediate Vault-to-Sewers return loop. */
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
  }
 }
 if(o->xp) {
  /* The last floor rewards strengthen spells against elemental resistance.
   * Keep the earlier strength upgrades for ordinary enemies and preparation. */
  if(a->level>=6 && o->intelligence<5)return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
  if(o->str<(cfg->str_first?3:2))return emit(a,o,s,FP_LEVEL_STR,FP_UPGRADE);
  if(o->intelligence<3)return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
  if(o->str<5)return emit(a,o,s,FP_LEVEL_STR,FP_UPGRADE);
  if(o->intelligence<5)return emit(a,o,s,FP_LEVEL_INT,FP_UPGRADE);
  if(o->dex<5)return emit(a,o,s,FP_LEVEL_DEX,FP_UPGRADE);
 }
 /* A troll survives one unenchanted diamond-sword blow plus a spell, so
  * pre-casting cannot spare the hit it lands. One ruby at the Vault's fire
  * table fixes that: climb back from the mines at full health to enchant. */
 if(a->floors[5].seen && !a->floors[6].seen && !o->sword_enchant && o->inv[FP_I_RUBY]>0 && vault_trip_ok(a,o)) {
  if(a->level==5 && o->health>=8+o->str-0.01f)TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==4) {
   if(!fire_table_known(a)) {
    a->vault_search++;
    if(troll_phase(a)){TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);}
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
   /* A remembered table beyond the 100-step radius made the trip descend at
    * once and climb again from the mines, thousands of times (ep 541). */
   if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_SWORD,troll_phase(a)?400:100),FP_MAGIC);
   if(troll_phase(a) && fire_table_known(a) && o->mana>=9 && a->vault_search<vault_limit(a,o)) {
    a->vault_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);
   }
   if(o->mana<9)TRY(recover(a,o,s),FP_RECOVER);
  }
 }
 /* Without a ruby, a sapphire buys the same blow at the Sewers' ice table,
  * two cleared floors up. The search there shares the trip budget. */
 else if(a->floors[5].seen && !a->floors[6].seen && !o->sword_enchant && o->inv[FP_I_SAPPHIRE]>0 && a->vault_search<600) {
  if((a->level==5 || a->level==4) && o->health>=8+o->str-0.01f)TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==3) {
   if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_SWORD,160),FP_MAGIC);
   else TRY(recover(a,o,s),FP_RECOVER);
   a->vault_search++;TRY(explore(a,o,cfg,s),FP_EXPLORE);
  }
 }
 if(a->resupplying && a->level>0 && a->level<8)TRY(travel(a,o,s,0),FP_TRAVEL);
 if(a->resupplying && a->level==0 && o->inv[0]>=cfg->wood_stock && o->inv[1]>=10 &&
    (o->inv[8]>=cfg->torch_stock || a->t-a->resupply_tick>600))a->resupplying=0;
 /* Felled trees become grass and never regrow. Agents that had cut every
  * tree they knew stayed on the trip, exploring until they starved (5% of
  * episodes). Continue with the wood in hand once none is left in memory. */
 if(a->resupplying && a->level==0 && a->t-a->resupply_tick>600) {
  const FPFloor *f0=&a->floors[0];int trees=0;
  for(int p=f0->known_lo;p<=f0->known_hi && !trees;p++)trees=f0->map[p]==FP_TREE;
  if(!trees){a->resupplying=0;a->wood_exhausted=1;}
 }
 if(o->inv[FP_I_BOOK] && (!o->spells[0]||!o->spells[1]))return emit(a,o,s,FP_READ_BOOK,FP_MAGIC);
 if(!a->achieved[56] && o->spells[0] && o->mana>=2)return emit(a,o,s,FP_FIREBALL,FP_MAGIC);
 if(!a->achieved[58] && o->spells[1] && o->mana>=2)return emit(a,o,s,FP_ICEBALL,FP_MAGIC);
 if(!a->achieved[62] && o->health>=7)for(int i=0;i<6;i++)if(o->potions[i])return emit(a,o,s,FP_POTION_RED+i,FP_LOOT);
 if(o->inv[FP_I_PICK]<1)TRY(craft(a,o,cfg,s,FP_WOOD_PICK),FP_CRAFT);
 if(o->inv[FP_I_SWORD]<1)TRY(craft(a,o,cfg,s,FP_WOOD_SWORD),FP_CRAFT);
 if(o->inv[FP_I_SWORD]<2)TRY(craft(a,o,cfg,s,FP_STONE_SWORD),FP_CRAFT);
 if(a->level==0 && !a->achieved[3] && a->farm_attempts<40 && o->inv[FP_I_SWORD]>=2)TRY(seek_block(a,o,s,FP_GRASS,5),FP_FARM);
 if(a->level==0 && o->inv[7] && !a->achieved[7])TRY(place(a,o,s,FP_PLACE_PLANT),FP_FARM);
 if(o->inv[FP_I_PICK]<2)TRY(craft(a,o,cfg,s,FP_STONE_PICK),FP_CRAFT);
 FPFloor *f=&a->floors[a->level];int prepare=a->level==0 && f->steps<cfg->prepare_steps;
 if(o->inv[FP_I_SWORD]<3 && (prepare||a->level==2||a->level==5 || (o->inv[2] && o->inv[3])))TRY(craft(a,o,cfg,s,FP_IRON_SWORD),FP_CRAFT);
 if(o->inv[FP_I_PICK]<3 && (prepare||a->level==2||a->level==5 || (o->inv[2] && o->inv[3])))TRY(craft(a,o,cfg,s,FP_IRON_PICK),FP_CRAFT);
 if(o->inv[FP_I_SWORD]<4 && o->inv[FP_I_PICK]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_SWORD),FP_CRAFT);
 if(o->inv[FP_I_PICK]<4 && o->inv[FP_I_PICK]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_PICK),FP_CRAFT);
 if(!a->achieved[26] && o->inv[2]>=3 && o->inv[3]>=3)TRY(craft(a,o,cfg,s,FP_IRON_ARMOUR),FP_CRAFT);
 if(armour(o)<8 && o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
 /* Mine protective plates while the enemies are still weak. Diamonds
  * already exposed in the Gnomish Mines otherwise remain unused until
  * the much more dangerous Troll Mines expedition. */
 if(a->level==2 && o->inv[FP_I_PICK]>=4 && o->inv[FP_I_SWORD]>=4 && armour(o)<8)
  TRY(seek_block(a,o,s,FP_DIAMOND,100),FP_GATHER);
 if(armour(o)<cfg->armour_pieces && (prepare||a->level==2||a->level==5))TRY(craft(a,o,cfg,s,FP_IRON_ARMOUR),FP_CRAFT);
 if(a->level==0 && o->inv[0]<cfg->wood_stock)TRY(seek_block(a,o,s,FP_TREE,100),FP_GATHER);
 /* Overworld stone is the only shelter and arrow material the Dungeon has. */
 if(o->inv[1]<(a->level==0?(a->floors[1].seen && early(a)?30:10):3) && o->inv[FP_I_PICK]>0)TRY(seek_block(a,o,s,FP_STONE,20),FP_GATHER);
 if((!a->achieved[23] || o->inv[8]<cfg->torch_stock) && (a->level==0||a->level==2||a->level==5))TRY(craft(a,o,cfg,s,FP_MAKE_TORCH),FP_CRAFT);
 /* Arrows halve Dungeon melee damage: an arrow before an orc arrives lets
  * one iron blow kill it. Per 100 floor-1 steps agents took 0.80 melee damage
  * holding six or more arrows, 1.42 with one to five and 2.56 with none.
  * Stock them on the Overworld, where wood and stone are plentiful, and keep
  * ten below it while eight stones remain for a shelter. */
 if(!a->achieved[22] || (o->bow && o->inv[9]<6 && (!early(a) || a->level==0 || o->inv[1]>=3)) ||
    (o->bow && early(a) && a->level>=1 && a->level<=4 && o->inv[9]<10 && o->inv[1]>=8 && o->inv[0]>=6) ||
    (a->level==0 && o->bow && a->floors[1].seen && early(a) && o->inv[9]<40 && o->inv[0]>=4 && o->inv[1]>=4))TRY(craft(a,o,cfg,s,FP_MAKE_ARROW),FP_CRAFT);
 if(!a->achieved[53] && o->bow && o->inv[9])return emit(a,o,s,FP_SHOOT,FP_MAGIC);
 if(!a->achieved[10] && o->inv[1])TRY(place(a,o,s,FP_PLACE_STONE),FP_GATHER);
 if(!a->achieved[24] && o->inv[8])TRY(place(a,o,s,FP_PLACE_TORCH),FP_LIGHT);
 /* Use carried coal to replenish Ice's light without another floor trip.
  * Three wood also covers a missing crafting table. */
 if((a->level==7 || a->level==8) && o->inv[8]<6 && o->inv[2]>0 && o->inv[0]>=(a->level==8?1:3))TRY(craft(a,o,cfg,s,FP_MAKE_TORCH),FP_CRAFT);
 if(a->level==8)TRY(graveyard_light(a,o,s),FP_LIGHT);
 if(a->level==2||a->level==5||a->level==7||a->level==8) {
  /* Spread permanent light only where it reveals new terrain. */
  int r=a->r+dr[o->dir],c=a->c+dc[o->dir],gain=0;
  if(inside(r,c) && o->inv[8] && place_valid(a,s,cell(r,c),FP_PLACE_TORCH)) {
   for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
    if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc) && f->map[cell(r+rr,c+cc)]==UNKNOWN)gain++;
   /* Coal and resupply trips limit exploration. Walk farther into existing
    * light before spending another torch on mostly overlapping terrain. */
   /* Ice needs closer torch spacing to expose its ladder before scarce
    * food forces another dangerous return through Fire. */
   /* Graveyard has no return ladder or renewable torch materials. Broader
    * placements preserve enough light to find the boss and stranded mobs. */
   /* Ice: same spacing as the lighting explorer (ICE_TORCH_GAIN); ten new
    * cells per torch exhausted coal long before the exit was found. */
   if(gain>=(a->level==8?GRAVEYARD_TORCH_GAIN:a->level==7?ICE_TORCH_GAIN:30))return emit(a,o,s,FP_PLACE_TORCH,FP_LIGHT);
  }
  int stock=a->level==5?4:1;
  if(o->inv[FP_I_PICK]>=4 && o->inv[5]<stock)TRY(seek_block(a,o,s,FP_SAPPHIRE,40),FP_GATHER);
  /* Nearby mine rubies can protect the entire expedition at the Vault. */
  if(o->inv[FP_I_PICK]>=4 && o->inv[6]<(a->level==2?4:stock))TRY(seek_block(a,o,s,FP_RUBY,40),FP_GATHER);
  /* The mines hold the most diamonds. Each diamond plate takes 0.6 off a
   * troll hit, and two let full health absorb two hits with margin. */
  if(a->level==5 && o->inv[FP_I_SWORD]>=4 && o->inv[FP_I_PICK]>=4 && armour(o)<8 && o->inv[4]<3)TRY(seek_block(a,o,s,FP_DIAMOND,40),FP_GATHER);
 }
 /* The return from ice enchanting crosses the cleared Troll Mines.
  * Gather reachable diamonds there to reduce the physical component of
  * both Frost Troll attacks and Ice projectiles before going back. */
 if(a->floors[7].seen && a->level==5 && o->clear && armour(o)<8) {
  if(o->inv[4]>=3)TRY(craft(a,o,cfg,s,FP_DIAMOND_ARMOUR),FP_CRAFT);
  TRY(seek_block(a,o,s,FP_DIAMOND,60),FP_GATHER);
 }
 /* Use partial ruby stocks before entering Fire. Waiting for all four
  * gems and perfect health often leaves useful protection unapplied.
  * A bounded search gives incomplete expeditions time to find more gems
  * without indefinitely delaying a floor whose generation lacks them. */
 if(a->level==5 && o->clear && !a->floors[7].seen && armour(o)) {
  int fire=0;for(int i=0;i<4;i++)fire+=o->armour_enchant[i]==1;
  if(fire<4) {
   if(o->inv[FP_I_RUBY]>0 && o->health>6 && vault_trip_ok(a,o))TRY(travel(a,o,s,0),FP_TRAVEL);
   /* Once Fire is open its rock holds more rubies than the mines. (The
    * local `fire` here counts enchanted pieces: test the phase itself.) */
   if(o->inv[FP_I_RUBY]<4-fire && f->steps<4000 && !fire_phase(a)) {
    TRY(seek_block(a,o,s,FP_RUBY,100),FP_GATHER);
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
  }
 }
 TRY(seek_block(a,o,s,FP_CHEST,25),FP_LOOT);
 if(o->mana>=9) {
  if(o->inv[5] && o->sword_enchant!=2 && !a->floors[7].seen)TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_SWORD,60),FP_MAGIC);
  if(o->inv[6] && !o->sword_enchant)TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_SWORD,60),FP_MAGIC);
  int ice=0,fire=0;for(int i=0;i<4;i++){ice+=o->armour_enchant[i]==2;fire+=o->armour_enchant[i]==1;}
  if(armour(o)) {
   if(o->inv[5] && (!a->achieved[64] || ((a->level==7 || (a->level==3 && a->floors[7].seen)) && ice<4)))TRY(seek(a,o,s,MASK(FP_ICE_TABLE),FP_ENCHANT_ARMOUR,60),FP_MAGIC);
   if(o->inv[6] && (!a->achieved[64] || ((a->level==4 || a->level==6) && fire<4 && !a->floors[7].seen)))TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_ARMOUR,60),FP_MAGIC);
  }
 }
 /* The Vault has fire tables; the Troll Mines supply their rubies. Return
  * with enough gems to protect all four pieces before the Fire expedition.
  * Clear the mines and restore health first so the return trip is survivable.
  * A brief retreat on first Fire arrival also covers gems found en route. */
 int fire_protection=0;for(int i=0;i<4;i++)fire_protection+=o->armour_enchant[i]==1;
 if(armour(o) && a->floors[5].seen && !a->floors[7].seen && fire_protection<4) {
  /* Every Vault holds one fire table, yet 182 of 5,725 Fire entrants
   * carried four rubies unenchanted after 600 search steps; their blows
   * cost 6.8 health instead of 2.8. Search again, by frontiers (with two
   * or more rubies, up to 1,800 steps). A table seen through the walls may
   * have no known route: the seek failed, the ready rule descended and the
   * mines rule climbed straight back (12,000 bounces, ep 3628). When neither
   * the table nor a frontier is reachable, mark the table unusable. */
  if(fire && a->level==4 && o->inv[6] && a->vault_search<VAULT_UNUSABLE) {
   int go=fire_table_known(a)?seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_ARMOUR,400):-1;
   if(go>=0) {
    if(o->mana>=9)return emit(a,o,s,go,FP_MAGIC);
    TRY(recover(a,o,s),FP_RECOVER);
   } else {
    if(a->vault_search<vault_limit(a,o)){a->vault_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);}
    a->vault_search=VAULT_UNUSABLE;
   }
  }
  if(a->level==6 && a->floors[6].steps<3)TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==5 && o->clear && o->health>=8+o->str-0.01f && o->inv[6]>=4-fire_protection && vault_trip_ok(a,o))TRY(travel(a,o,s,0),FP_TRAVEL);
  if(a->level==4 && o->inv[6]) {
   /* Search the cleared Vault for its single fire table instead of
    * descending at once and repeating the climb from the mines. */
   if(!fire_table_known(a) && a->vault_search<vault_limit(a,o)) {
    a->vault_search++;
    /* With no frontier left the search is over: end it, or the floor-5
     * trip rule climbs straight back (4<->5 bounces). */
    if(troll_phase(a)){TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);}
    TRY(explore(a,o,cfg,s),FP_EXPLORE);
   }
   if(o->mana>=9)TRY(seek(a,o,s,MASK(FP_FIRE_TABLE),FP_ENCHANT_ARMOUR,troll_phase(a) || fire?400:100),FP_MAGIC);
   /* Known but unreachable table: find a route within the search budget. */
   if(troll_phase(a) && fire_table_known(a) && o->mana>=9 && a->vault_search<vault_limit(a,o)) {
    a->vault_search++;TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);a->vault_search=vault_limit(a,o);
   }
   if(o->mana<9)TRY(recover(a,o,s),FP_RECOVER);
  }
 }
 int ready=a->level==0?(o->inv[FP_I_SWORD]>=3 && o->inv[FP_I_PICK]>=3 && o->inv[0]>=cfg->wood_stock):
   (a->level==2||a->level==5)?(o->inv[FP_I_SWORD]>=4 && o->inv[FP_I_PICK]>=4 && armour(o)>=2):1;
 if(a->level==0 && f->steps>=cfg->prepare_steps)ready=1;
 if((a->level==2||a->level==5) && f->steps>=cfg->mine_steps)ready=1;
 /* A diamond sword kills Sewer lizards and kobolds in one strike at strength
  * 3; an iron one needs two, and each lizard bites first. Agents leaving the
  * mines with iron swords passed the Sewers 0.66 of the time, with diamond
  * ones 0.91. Most timed-out descents held one diamond, with three more left
  * in rock that no torch had lit. Light unseen rock before the timed descent,
  * keeping torches for the Troll Mines. */
 if(a->level==2 && o->clear && (o->inv[FP_I_SWORD]<4 || armour(o)<6) && o->inv[FP_I_PICK]>=3 && o->inv[0]>=1 &&
    o->inv[FP_I_TORCH]>6 && f->steps<5*cfg->mine_steps)TRY(light_unseen(a,o,s,20,1500),FP_LIGHT);
 /* A Fire visit lasts as long as the drink, energy and health carried
  * down. Top them up beside the Troll Mines water before each descent. */
 if(fire && a->level==5 && o->clear && ready && !a->resupplying) {
  /* Margins: drink and energy also fall on the walk back to the ladder,
   * and a full-drink rule made agents with water 20 steps away shuttle
   * between the two forever (ep 16). The drinking flag fills to the top. */
  if(o->drink<=7+2*o->dex-3){a->drinking=1;TRY(seek_water(a,o,s),FP_DRINK);}
  if(o->energy<6 || o->health<8+o->str-1.01f){a->recovering=1;TRY(recover(a,o,s),FP_RECOVER);}
 }
 if(a->level<8 && o->clear && ready && !a->resupplying && o->health>cfg->retreat_health)TRY(travel(a,o,s,1),FP_TRAVEL);
 if(a->level>0 && a->level<8 && (o->food==0 || o->drink==0))TRY(travel(a,o,s,0),FP_TRAVEL);
 /* A cleared mine only needs its ladder. When ordinary exploration has not
  * found it in the time that 95% of descents need, search the small unlit
  * pockets. Searching them earlier spent every torch near the entrance and
  * left the rest of the floor dark for good. */
 if(a->level==2 && o->clear && f->down<0 && f->steps>=4000)TRY(light_unseen(a,o,s,8,600),FP_LIGHT);
 /* The ladder can sit on one unlit cell inside a fully explored cave: in
  * 251 of 13,724 Mines entries (bank D) the agent shuttled between floors 1
  * and 2 for 100,000 steps; at arrive2 ep 63 and 192 the down-ladder cell was
  * the only unknown cell among known path neighbours. No placement lit eight
  * unseen cells any more. Light single unknown cells, farther away too. */
 if(a->level==2 && !a->floors[5].seen && o->clear && f->down<0 && f->steps>=3000)TRY(light_unseen(a,o,s,1,2500),FP_LIGHT);
 /* Clear Fire from its up ladder. Pigmen within ten cells come to us, and
  * clearance hunts 15-35 cells out ended in 4-6 hits taken on the run back
  * (25 traced deaths of fully fire-enchanted agents). On the ladder cell a
  * low fight ends with one ascend (see above). Where nothing approaches
  * (a ladder in a lava or rock pocket), fall back to hunting after 80 steps. */
 if(fire && a->level==6 && !o->clear && a->floors[6].up>=0 && s->dist[a->floors[6].up]<INF) {
  int near=0,up=a->floors[6].up;
  for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)<10)near=1;}
  if(near)a->fire_camp=0;else a->fire_camp++;
  if(a->fire_camp<80) {
   if(up!=cell(a->r,a->c))TRY(move_path(a,o,s,up),FP_FIGHT);
   return emit(a,o,s,FP_NOOP,FP_FIGHT);
  }
 }
 /* Vault clearance from the up ladder (mid stream). Hunts out to thirty
  * steps met archers in one-cell corridors: arrows chased the agent along
  * the corridor and struck whenever a knight ahead made it stop (ep 20, 29
  * r0), and low-health returns to the ladder crossed the same lines (ep 24).
  * Knights within ten cells come to us; on the ladder a lost exchange ends
  * with one ascend. Stand beside the ladder while it lies in an archer's
  * line. Where nothing approaches, fall back to hunting after 80 steps. */
 if(mid_phase(a) && a->level==4 && !o->clear && a->floors[4].up>=0 && s->dist[a->floors[4].up]<INF) {
  /* A knight stuck behind a wall four cells away held agents one kill short
   * of clearance for 90,000 steps of supply climbs (ep 853 r1): after 300
   * camped steps with no knight within two cells, hunt instead. */
  int near=0,engaged=0,up=a->floors[4].up,target=up;
  for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c);if(!e.kind && d<10)near=1;if(!e.kind && d<=2)engaged=1;}
  if(near)a->mid_camp=0;else a->mid_camp++;
  if(engaged)a->mid_idle=0;else a->mid_idle++;
  if(a->mid_camp<80 && a->mid_idle<300) {
   int exposed=0,ur=up/FP_SIDE,uc=up%FP_SIDE;
   for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind && firing_line(a,ur,uc,a->enemies[i].p/FP_SIDE,a->enemies[i].p%FP_SIDE))exposed=1;
   if(exposed) {
    int best=-1,bestn=INF;
    for(int d=1;d<=4;d++) {
     int r=ur+dr[d],c=uc+dc[d];if(!inside(r,c) || !walkable(block(a,r,c)) || s->mobs[cell(r,c)] || s->dist[cell(r,c)]>=INF)continue;
     int n=0;for(int i=0;i<a->enemy_count;i++)if(a->enemies[i].kind && firing_line(a,r,c,a->enemies[i].p/FP_SIDE,a->enemies[i].p%FP_SIDE))n++;
     if(n<bestn){bestn=n;best=cell(r,c);}
    }
    if(best>=0 && !bestn)target=best;
   }
   if(target!=cell(a->r,a->c))TRY(move_path(a,o,s,target),FP_FIGHT);
   return emit(a,o,s,FP_NOOP,FP_FIGHT);
  }
 }
 /* On Fire a bat hunt detours into pigman reach (ep 1): hunt there only
  * when food is needed, or for a bat close by. */
 int bats=fire && a->level==6?(o->food<=6?15:4):15;
 if(bats && o->food<7+2*o->dex && o->inv[FP_I_SWORD]>=2)TRY(hunt(a,o,s,2,bats),FP_EAT);
 /* Ice is pitch dark (default light 0): only torch-lit cells are ever
  * observed and moves are planned onto known cells only, so torches are
  * the exploration itself. Frontier walking with a torch whenever ten new
  * cells would light spent 24-94 torches per exit found, one every ~30 Ice
  * steps, and Ice time is lethal at a near-constant rate. Walk to the
  * reachable placement that lights the most unseen cells instead. Before
  * clearance the same rule runs below, after hunts within 30 steps: placed
  * here it pre-empted them and clearance fell .569 -> .533. */
 if(a->level==7 && o->clear && a->floors[7].down<0 && o->inv[FP_I_TORCH])TRY(light_unseen(a,o,s,ICE_TORCH_GAIN,1200),FP_LIGHT);
 /* Once an elemental realm is clear, uncover its exit. Extra hunts spend
  * the food and health needed to reach the next floor. */
 /* Fire rock is 2.5% ruby; a known Vault table turns each into one fewer
  * point of fire damage per blow and fireball. Mine those close by. */
 if(fire && a->level==6 && armour(o) && o->inv[FP_I_PICK]>=4 && vault_trip_ok(a,o)) {
  int fp=0;for(int i=0;i<4;i++)fp+=o->armour_enchant[i]==1;
  if(o->inv[FP_I_RUBY]<4-fp)TRY(seek_block(a,o,s,FP_RUBY,10),FP_GATHER);
 }
 if((a->level==6 || a->level==7) && o->clear)TRY(explore(a,o,cfg,s),FP_EXPLORE);
 /* A cleared Dungeon only needs its ladder. Distant hunts there kept agents
  * fighting near the entrance for hundreds of steps instead of exploring;
  * approaching mobs still meet the adjacent-combat rules. */
 /* Fire elementals keep four or five cells away and walk on lava; chased,
  * they end cornered and fire point-blank. 74 of 87 sampled fireball hits
  * came from one or two cells. Clear Fire on the pigmen that come to us. */
 /* Cleared Sewers and Vault (mid stream) only need their down ladder. The
  * explorer scores cells within sixty steps, so once those were seen it
  * circled known rooms: the exit took a median 310 Vault steps after
  * clearance (90th percentile 1,740), under archer fire, and hunts of
  * distant mobs there added fights. Walk to real frontiers at any distance;
  * approaching mobs still meet the adjacent-combat rules. */
 if(mid_phase(a) && (a->level==3 || a->level==4) && o->clear && a->floors[a->level].down<0)TRY(frontier_walk(a,o,cfg,s),FP_EXPLORE);
 int prey=fire && a->level==6?1:5;
 if(o->inv[FP_I_SWORD]>=2 && (a->level>0 || !a->achieved[8] || !a->achieved[12]))TRY(hunt(a,o,s,prey,a->level==1 && o->clear?3:30),FP_FIGHT);
 if(a->level==0 && !a->achieved[11])TRY(seek_block(a,o,s,FP_RIPE,70),FP_EAT);
 if(a->level==7 && a->floors[7].down<0 && o->inv[FP_I_TORCH])TRY(light_unseen(a,o,s,ICE_TORCH_GAIN,1200),FP_LIGHT);
 TRY(explore(a,o,cfg,s),FP_EXPLORE);
 return emit(a,o,s,FP_NOOP,FP_EXPLORE);
#undef TRY
}
const char *fp_skill_name(int skill) {
 static const char *names[FP_SKILL_COUNT]={"explore","gather","craft","drink","eat","recover","fight","farm","light","travel","loot","magic","upgrade"};
 return skill>=0 && skill<FP_SKILL_COUNT?names[skill]:"unknown";
}

#include "full_goals.inc"

int fp_act(FPAgent *a,const FPObservation *o,float reward,const FPConfig *cfg,FPScratch *s) {
 perceive(a,o,reward,s);
 return a->intent.enabled?commanded(a,o,cfg,s):autonomous(a,o,cfg,s);
}
void fp_observe_action(FPAgent *a,const FPObservation *o,float reward,int action,FPScratch *s) {
 perceive(a,o,reward,s);
 commit_action(a,o,s,action,FP_EXPLORE);
}
void fp_replay_action(FPAgent *a,const FPObservation *o,float reward,int action,const FPConfig *cfg,FPScratch *s) {
 /* Let each candidate reconstruct its own planning state. Reconcile the
  * action it proposed with the action the historical environment received. */
 (void)fp_act(a,o,reward,cfg,s);
 a->r=a->observed_r;a->c=a->observed_c;a->t--;
 commit_action(a,o,s,action,a->skill);
}
