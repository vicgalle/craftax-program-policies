/* candidate sd2: LGI_SLEEP=1 LGI_DRINK=1 */
#define LGI_SLEEP 1
#define LGI_DRINK 1
/* Logistics module "ice" (push50): Ice-phase supply. Region: a->floors[7].seen
 * (the frontier is the Ice Realm, including every excursion from Ice back
 * through Fire, the Troll Mines and floors 1-4). Before Ice is first seen every
 * hook below returns -1, so the program is the incumbent action for action.
 * Included from full_policy.c next to full_strategic.inc. Compile-time switches
 * select the components (all 0: the incumbent).
 *
 * Evidence (incumbent, s202 arrive7, trip log of work/ice/tools/ice_eval.c):
 * Ice has no food, so every Ice stint (median 156 steps from a full stock at
 * dex 1) ends in an excursion. Excursions that start at energy < 3 die 7.2% of
 * the time, at 3-4 2.5%, at >= 7 0.5%: the incumbent leaves Ice at food 3
 * whatever its energy, and a tired agent then sleeps on Fire or in the dark
 * Troll Mines (energy deaths on Troll visits, Fire shelters among pigmen).
 * Drink deaths on Fire follow departures with part-filled drink. */
#ifndef LGI_DEX            /* spend the Ice arrival XP point on dexterity */
#define LGI_DEX 0
#endif
#ifndef LGI_SLEEP          /* sleep on Ice before a supply departure */
#define LGI_SLEEP 0
#endif
#ifndef LGI_SLEEP_E        /* ... when energy is at most this */
#define LGI_SLEEP_E 5
#endif
#ifndef LGI_SLEEP_FOOD     /* ... and the departure is near: food at most this */
#define LGI_SLEEP_FOOD 6
#endif
#ifndef LGI_DRINK          /* top up drink before crossing Fire */
#define LGI_DRINK 0
#endif
#ifndef LGI_DRINK_ICE      /* Ice: path cost to water accepted before a departure */
#define LGI_DRINK_ICE 250
#endif
#ifndef LGI_DRINK_MINES    /* Troll Mines, homebound: path cost to water accepted */
#define LGI_DRINK_MINES 400
#endif
#ifndef LGI_FILL           /* homebound from a supply floor: eat visible animals to the top */
#define LGI_FILL 0
#endif

static int lg_ice_region(const FPAgent *a) { return a->floors[7].seen; }
static int lgi_max(const FPObservation *o) { return 7+2*o->dex; }

/* Attribute spending at Ice arrival (one XP point: floor 7's first arrival).
 * The incumbent spends it on intelligence (4 -> 5). Dexterity instead raises
 * the food, drink and energy maxima by 2 and slows their decay by 1/8: a full
 * food stock lasts 11 x 29.7 = 327 steps instead of 9 x 26 = 234, and each Ice
 * stint (food max -> 3) grows from 156 to 238 steps, i.e. a third fewer supply
 * trips through Fire for the same Ice time. */
static int lg_ice_xp(FPAgent *a,const FPObservation *o) {
 if(!LGI_DEX || !lg_ice_region(a) || !o->xp)return -1;
 /* Every XP point gained in the region (floor 7's) goes to dexterity. */
 if(o->dex<5)return FP_LEVEL_DEX;
 return -1;
}

/* PROVISION goal (Ice): sleep and heal before the supply departure.
 * Entry: on Ice, no ice-enchant trip pending, the departure near (food <=
 * LGI_SLEEP_FOOD) and energy <= LGI_SLEEP_E, or health <= LGI_SLEEP_HP; food
 * for the rest plus the departure stock (3) in hand; drink >= 2 (filled first
 * when Ice water is reachable); no melee mob within 4. With LGI_HEAL it also
 * enters at the departure itself (food 2-3) while health <= 6.5: an agent that
 * leaves Ice at 6.5 or less cannot come back down after a Fire meal (the
 * contract with the excursion rules) and heals on Fire, which has no water, or
 * in the dark mines (1,396 such departures in the baseline log, 7.4% died).
 * Policy: the incumbent's recovery room, which on Ice encloses the up ladder
 * (ice_base), sealed; sleep to full energy, then wait to LGI_HEAL_TO health.
 * Completion (observed): energy full and, when healing, health >= LGI_HEAL_TO.
 * Abandonment: food or drink at 1; food short of the rest still needed plus 2
 * (a sleep cannot be cut short: the engine wakes the player at full energy
 * only, so it is not started without the food for it; screen 1 woke agents
 * starving on Ice, 3 -> 13 deaths); awake healing stops at the departure stock
 * (3, or 2 with LGI_HEAL at low health); no room can be built (recover yields).
 * The incumbent's food <= 3 departure rule would otherwise pre-empt both. */
#ifndef LGI_SLEEP_HP       /* also enter while health is at most this (0: off) */
#define LGI_SLEEP_HP 0
#endif
#ifndef LGI_HEAL_TO        /* healing target */
#define LGI_HEAL_TO 9
#endif
#ifndef LGI_HEAL           /* at the departure (food 2-3), keep healing while health <= 6.5 */
#define LGI_HEAL 0
#endif
static float lgi_per(const FPObservation *o) { return 26.0f/(1.0f-0.125f*(o->dex-1)); }
/* Food spent by the rest: the sleep to full energy (half decay), then awake
 * healing to the target at full decay (health +1 per 13 steps asleep, 26 awake). */
static float lgi_rest_food(const FPObservation *o,int heal,float *sleep_food) {
 float per=lgi_per(o),sl=(float)(lgi_max(o)-o->energy)*11;
 *sleep_food=sl*0.5f/per;
 float need=heal?(float)LGI_HEAL_TO-o->health:0,awake=need>sl/13?(need-sl/13)*26:0;
 return *sleep_food+awake/per;
}
static int lgi_sleep_act(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!LGI_SLEEP || a->level!=7 || enchant_trip(a,o)){a->lgi_sleep=0;return -1;}
 int full=o->energy>=lgi_max(o),low=LGI_HEAL && o->health<=6.5f;
 float sf,cost;
 if(a->lgi_sleep) {
  int heal=a->lgi_sleep==2;cost=lgi_rest_food(o,heal,&sf);
  int done=full && (!heal || o->health>=LGI_HEAL_TO || o->health>=8+o->str-0.01f);
  int stock=low?2:3,stop=o->food<=1 || o->drink<=1 || (heal?o->food<stock:(!o->sleeping && !full && o->food<2+sf)) || (full && o->food<stock);
  if(done || stop){a->lgi_sleep=0;a->recovering=0;return -1;}
 } else {
  int want_e=o->energy<=LGI_SLEEP_E,want_h=(LGI_SLEEP_HP && o->health<=LGI_SLEEP_HP) || low;
  if(!(want_e || want_h) || wm_melee_within(a,4) || o->drink<2)return -1;
  cost=lgi_rest_food(o,want_h,&sf);
  int window=o->food<=LGI_SLEEP_FOOD && o->food>=3+cost;
  /* At the departure with low health: heal down to food 2 (awake if a sleep
   * would not fit). */
  if(!window && !(low && o->food>=2 && o->food<=3))return -1;
  /* Sleep drains drink at half rate: fill it first when Ice water is reachable. */
  if(o->drink<=lgi_max(o)-4 && water_cost(a,s)<=1000){a->drinking=1;return -1;}
  a->lgi_sleep=want_h?2:1;
 }
 a->recovering=1;
 /* Awake healing when a sleep would not fit in the food left. */
 if(!o->sleeping && !full && o->food<2+sf && sealed_in(a,o,s))return o->food>0 && o->drink>0?FP_NOOP:-1;
 int act=recover(a,o,s);
 if(act<0){a->lgi_sleep=0;a->recovering=0;return -1;}
 return act;
}

/* DRINK provisioning before a Fire crossing (Fire has no water; drink deaths
 * on Fire follow part-filled departures). Ice: once the departure is near
 * (food <= 4) top drink up when water is within LGI_DRINK_ICE path cost. Troll
 * Mines, homebound (fed: food >= max-3): top up within LGI_DRINK_MINES. Sets
 * the incumbent's drinking flag, which fills to the maximum. */
static void lgi_drink_flag(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!LGI_DRINK || a->drinking || enchant_trip(a,o) || wm_melee_within(a,4))return;
 int mx=lgi_max(o);
 if(a->level==7 && o->food<=4 && o->drink<=mx-3 && water_cost(a,s)<=LGI_DRINK_ICE)a->drinking=1;
 if(a->level==5 && icetrip(a) && o->food>=mx-3 && o->drink<=mx-2 && water_cost(a,s)<=LGI_DRINK_MINES)a->drinking=1;
}
/* Known Ice water within 25 cells of the Ice up ladder (Manhattan). */
static int lgi_ice_water(const FPAgent *a) {
 const FPFloor *f=&a->floors[7];int up=f->up;if(up<0)return 0;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_WATER && abs(p/FP_SIDE-up/FP_SIDE)+abs(p%FP_SIDE-up%FP_SIDE)<=25)return 1;
 return 0;
}
/* Thirsty on Fire with Ice water known near the Ice ladder: drink on Ice
 * (one ladder away) instead of crossing Fire to the mines pools. */
static int lgi_drink_home(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!LGI_DRINK || !icetrip(a) || a->level!=6 || enchant_trip(a,o) || o->drink>3 || o->health<=6.5f || a->lgi_aux[0]!=1 || !lgi_ice_water(a))return -1;
 /* Not into a besieged ladder (hostiles within 5 on the last Ice step: the
  * incumbent's crowd escape sends the agent straight back up, and the two
  * rules bounced Ice<->Fire until drink 0, screen 2), and once per thirst. */
 if(a->lgi_aux[1] || a->lgi_aux[2])return -1;
 int D=a->floors[6].down,here=cell(a->r,a->c);
 if(D<0 || path_dist(a,s,D)>=INF || s->dist[D]>300)return -1;
 if(D==here){a->lgi_aux[2]=1;return FP_DESCEND;}
 return move_path(a,o,s,D);
}

/* THIRST trip: with no reachable water known on Ice (dark: lakes are seen
 * only by torchlight), the incumbent explores on until drink 0, then crosses
 * Fire, which has no water, to the mines with its health already draining
 * (departures at drink 0 died 9%). Leave for the mines pools while
 * LGI_THIRST_AT drink remains, and on Fire go on up instead of back down. */
#ifndef LGI_THIRST
#define LGI_THIRST 0
#endif
#ifndef LGI_THIRST_AT
#define LGI_THIRST_AT 4
#endif
static int lgi_thirst(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!LGI_THIRST || enchant_trip(a,o) || o->drink>LGI_THIRST_AT || o->drink<1)return -1;
 if(a->level==7) {
  if(water_cost(a,s)<=1000 || wm_melee_within(a,3))return -1;
  return travel(a,o,s,0);
 }
 if(a->level==6 && icetrip(a) && a->lgi_aux[0]==2 && o->drink<=LGI_THIRST_AT)return travel(a,o,s,0);
 return -1;
}

/* FILL: on a lit supply floor of an excursion (Vault, Sewers) or the mines,
 * the incumbent descends as soon as one animal raised food above 5, and the
 * next Ice stint starts part-filled (food 7 at the median return). Eat
 * visible animals within 30 steps first while food is three short of the top
 * and nothing hostile is close. */
static int lgi_fill(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!LGI_FILL || !icetrip(a) || enchant_trip(a,o) || a->level<3 || a->level>5)return -1;
 if(o->food>lgi_max(o)-3 || o->health<5 || wm_hostile_within(a,4) || o->inv[FP_I_SWORD]<2)return -1;
 return hunt(a,o,s,2,30);
}

/* BOUNCE goal (Fire, at the Ice ladder). Fire is lit and spawns a bat near
 * the player with chance 0.1 per step: 18.5% of the incumbent's Fire arrivals
 * from Ice ended in a meal, and its one-step Ice<->Fire bounces (14,000 in the
 * trip log) killed nobody, while Fire visits of 20-200 steps died 2-3.7% of
 * the time and excursions beyond Fire (Troll Mines, Vault) 3-22%. The
 * incumbent bounces only while food is 3, then hunts any bat within 80 steps or
 * crosses Fire to the dark mines at food 2. Here the bounce continues down to
 * food LGI_BOUNCE_MIN, and a Fire visit hunts only bats within LGI_BOUNCE_R
 * steps (coal within 6 when short), then goes home down the ladder.
 * Entry: on Fire in an excursion, no ice-enchant trip, hungry (food <= max-3)
 * but food >= LGI_BOUNCE_MIN, health > 6.5 (the contract with Ice: never descend
 * at 6 or less), drink >= 2, energy above the sleep threshold, the Ice ladder
 * reachable. Completion (observed): fed (food > max-3). Abandonment: food below
 * LGI_BOUNCE_MIN, a hazard (health, drink, energy) or no route: the incumbent's
 * rules take over (mines trip, Fire shelter). */
#ifndef LGI_BOUNCE
#define LGI_BOUNCE 0
#endif
#ifndef LGI_BOUNCE_MIN
#define LGI_BOUNCE_MIN 2
#endif
#ifndef LGI_BOUNCE_R
#define LGI_BOUNCE_R 15
#endif
static int lgi_bounce(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 if(!LGI_BOUNCE || !icetrip(a) || a->level!=6 || enchant_trip(a,o))return -1;
 int D=a->floors[6].down,here=cell(a->r,a->c);
 if(D<0 || path_dist(a,s,D)>=INF || s->dist[D]>400)return -1;
 if(o->food>lgi_max(o)-3 || o->food<LGI_BOUNCE_MIN || o->health<=6.5f || o->drink<2 || o->energy<=cfg->sleep_at)return -1;
 if(o->inv[FP_I_SWORD]>=2 && !wm_melee_within(a,5)){int act=hunt(a,o,s,2,LGI_BOUNCE_R);if(act>=0)return act;}
 if(o->inv[FP_I_COAL]<8 && o->health>=8 && !wm_hostile_within(a,6) && wm_incoming_damage(a,o,s,FP_NOOP)==0){int act=seek_block(a,o,s,FP_COAL,6);if(act>=0)return act;}
 return D==here?FP_DESCEND:move_path(a,o,s,D);
}

/* DEPART (Ice): the supply expedition's entry, by time to empty. The
 * incumbent leaves Ice when food reaches 3 wherever it is, and walks to the
 * ladder: 4,685 excursions started at food 2 (2.1% death, 40% beyond Fire)
 * against 22,981 at food 3 (0.4%, 7.5%), because the bounce window for a Fire
 * bat (food 3) was spent walking. Leave when the food expected on reaching the
 * up ladder (food - path steps x decay per step) falls to LGI_DEPART_AT + 0.5,
 * so the Fire stage starts at food 3. Only with the ladder known and reachable,
 * health above 6.5, no ice-enchant trip (its own rule) and no sleep under way. */
#ifndef LGI_DEPART
#define LGI_DEPART 0
#endif
#ifndef LGI_DEPART_AT
#define LGI_DEPART_AT 3
#endif
static int lgi_depart(FPAgent *a,const FPObservation *o,FPScratch *s) {
 if(!LGI_DEPART || a->level!=7 || enchant_trip(a,o) || a->lgi_sleep || o->sleeping)return -1;
 int up=a->floors[7].up;if(up<0 || path_dist(a,s,up)>=INF)return -1;
 if(o->food>LGI_DEPART_AT+4 || o->health<=6.5f || wm_melee_within(a,3))return -1;
 float per=26.0f/(1.0f-0.125f*(o->dex-1)),eta=o->food-(s->dist[up]/10)/per;
 if(eta>LGI_DEPART_AT+0.5f)return -1;
 return up==cell(a->r,a->c)?FP_ASCEND:move_path(a,o,s,up);
}

/* EPOT: an identified energy potion (+8 energy) replaces a sleep. The
 * incumbent drinks one only in the Fire phase; Ice entrants carry 5.6 potions.
 * Use it when energy is at the sleep threshold anywhere in the region, or on
 * Ice when the pre-departure sleep would start. */
#ifndef LGI_EPOT
#define LGI_EPOT 0
#endif
static int lgi_epot(FPAgent *a,const FPObservation *o,const FPConfig *cfg) {
 if(!LGI_EPOT || o->sleeping || o->resting)return -1;
 int want=o->energy<=cfg->sleep_at || (a->level==7 && LGI_SLEEP && o->energy<=LGI_SLEEP_E && o->food<=LGI_SLEEP_FOOD);
 if(!want || o->energy>lgi_max(o)-8+2)return -1;
 for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==4){a->lgi_sleep=0;return FP_POTION_RED+i;}
 return -1;
}

/* The supply hook in autonomous(): after fights, escapes and the incumbent's
 * low-health retreats, before its drink, food, healing and sleep rules. */
static int lg_ice_supply(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int *skill) {
 if(!lg_ice_region(a))return -1;
 int act;
 /* Ice water reachable within the drink rule's radius (seen on the last Ice step). */
 if(a->level==7){a->lgi_aux[0]=water_cost(a,s)<=1000?1:2;a->lgi_aux[1]=wm_hostile_within(a,5);}
 if(o->drink>=lgi_max(o)-1)a->lgi_aux[2]=0;
 lgi_drink_flag(a,o,s);
 if((act=lgi_epot(a,o,cfg))>=0){*skill=FP_RECOVER;return act;}
 if((act=lgi_sleep_act(a,o,s))>=0){*skill=FP_RECOVER;return act;}
 if(LGI_DEPART && a->level==7 && a->drinking && o->drink<lgi_max(o) && water_cost(a,s)<=1000)return -1; /* top up first */
 if((act=lgi_depart(a,o,s))>=0){*skill=FP_TRAVEL;return act;}
 if((act=lgi_drink_home(a,o,s))>=0){*skill=FP_TRAVEL;return act;}
 if((act=lgi_thirst(a,o,s))>=0){*skill=FP_TRAVEL;return act;}
 if((act=lgi_bounce(a,o,cfg,s))>=0){*skill=act==FP_DESCEND?FP_TRAVEL:FP_EAT;return act;}
 if((act=lgi_fill(a,o,s))>=0){*skill=FP_EAT;return act;}
 return -1;
}
