/* Strategic goal module "kit" (see full_strategic.inc): the rest of the kit,
 * all floors; never diamond mining or armour/weapon crafting.
 *
 * Two goals, both on the Sewers ice-enchant trip that every Ice entrant with
 * sapphires makes (the incumbent's enchant_trip: Ice -> Fire -> Troll Mines
 * -> Vault -> Sewers and back down); behaviour before the first Ice arrival
 * is the incumbent's, action for action.
 *  STOCK ("loot"): open the chests that the first descent left in the
 *        Sewers (floor 3) and the Vault (floor 4).
 *  X2 ("farloot"): once the Sewers are looted and the ice enchanting is
 *        over, climb to the Dungeon (floor 1), loot it, and come back to the
 *        Sewers; the incumbent then descends to Ice as before.
 *
 * Why. Each dungeon floor (Dungeon 1, Sewers 3, Vault 4) holds ~7.7 chests;
 * Ice entrants left ~3.1 in the Dungeon, ~2.5 in the Sewers and ~2.1 in the
 * Vault, and know where 0.1 per floor are (the incumbent opens every chest it
 * sees, within 25 steps). A chest gives 1-2 potions half the time (random
 * colour, the healing one in six; Ice entrants know the healing colour 73%
 * of the time but hold 0.10 healing potions, while three more are worth
 * +12.3pp of P(8 | Ice entry)), 4-7 torches 60% of the time (dark Ice;
 * torch20 +2.7pp), ore (diamond, sapphire, ruby, coal, iron), arrows,
 * sometimes a tool. On the trip the agent has its Ice-entry gear and these
 * are cleared floors; looting them on the first descent cost more in
 * Sewers/Vault deaths than it returned (-1.4pp from Sewers entry), and on the
 * troll-phase Vault trips it was null. Evidence (Ice entry, paired, all
 * snapshots): s74 replicas 4:6 +2.96pp [1.05, 4.86]; s75 (disjoint worlds)
 * replicas 2:4 +4.62pp [2.67, 6.56]. Floor-8 arrivals gain mostly through
 * fewer deaths on Fire excursions and on Ice.
 *
 * STOCK. Entry (selector): Ice seen, on the Sewers or the Vault, floor clear,
 * health at least max-1, food, drink and energy at least 4, nothing hostile
 * within 6 (WM), less than KIT_LOOT_STEPS spent looting this floor.
 * Completion (observed): no known chest within 60 path steps and no frontier
 * stand reachable (the floor's budget is then closed). Abandonment: left the
 * floor; hazard (health below 6, food or drink at 3 or less, energy below 2,
 * a hostile within 4); the floor's budget spent. Policy: walk to the nearest
 * known chest and open it, else walk to the best frontier stand.
 * X2. Entry: Ice seen, on the Sewers, the ice enchanting over (four ice
 * pieces or no sapphire), the Sewers budget closed, the Dungeon budget open,
 * the ladders of floors 1-3 known, health max-1, food and drink >= 6, energy
 * >= 5, nothing hostile within 6, not taken before. Completion (observed):
 * back on the Sewers after the Dungeon was looted. Abandonment: 4,000 steps.
 * Policy: climb to the Dungeon, open known chests and walk its frontier
 * (KIT_LOOT_STEPS), descend to the Sewers.
 * Survival (fights, drink, food, healing, sleep) preempts both as usual, and
 * both yield a step while the WM predicts incoming damage. */
#ifndef KIT_LOOT
#define KIT_LOOT ((1<<3)|(1<<4))   /* floors: Sewers, Vault */
#endif
#ifndef KIT_LOOT_STEPS
#define KIT_LOOT_STEPS 800         /* per floor, over all visits */
#endif

static int kit_loot_safe(const FPAgent *a,const FPObservation *o,int entry) {
 if(entry)return o->health>=8+o->str-1.01f && o->food>=4 && o->drink>=4 && o->energy>=4 && !wm_hostile_within(a,6);
 return o->health>=6 && o->food>3 && o->drink>3 && o->energy>=2 && !wm_hostile_within(a,4);
}
static int kit_loot_entry(const FPAgent *a,const FPObservation *o) {
 if(!a->floors[7].seen)return 0;
 return ((KIT_LOOT>>o->level)&1) && o->clear && a->kit_loot_steps[o->level]<KIT_LOOT_STEPS && kit_loot_safe(a,o,1);
}
static int kit_loot_complete(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 (void)a;(void)o;return f->local[0]==1;
}
static int kit_loot_abandon(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 return o->level!=f->entry_level || !kit_loot_safe(a,o,0) || a->kit_loot_steps[o->level]>=KIT_LOOT_STEPS;
}
static int kit_loot_act(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,FPStratFrame *f,int *skill) {
 if(wm_incoming_damage(a,o,s,FP_NOOP)>0)return -1;
 a->kit_loot_steps[o->level]++;
 int act=seek_block(a,o,s,FP_CHEST,60);if(act>=0){*skill=FP_LOOT;return act;}
 act=frontier_walk(a,o,cfg,s);if(act>=0){*skill=FP_EXPLORE;return act;}
 /* Nothing left: the floor is done for good. */
 f->local[0]=1;a->kit_loot_steps[o->level]=KIT_LOOT_STEPS;return -1;
}

static int kit_far_entry(const FPAgent *a,const FPObservation *o) {
 int ice=0;for(int i=0;i<4;i++)ice+=o->armour_enchant[i]==2;
 return a->floors[7].seen && !a->kit_farloot && o->level==3 && (ice>=4 || !o->inv[FP_I_SAPPHIRE]) &&
        a->kit_loot_steps[3]>=KIT_LOOT_STEPS && a->kit_loot_steps[1]<KIT_LOOT_STEPS &&
        a->floors[3].up>=0 && a->floors[2].up>=0 && a->floors[2].down>=0 && a->floors[1].down>=0 &&
        o->health>=8+o->str-1.01f && o->food>=6 && o->drink>=6 && o->energy>=5 && !wm_hostile_within(a,6);
}
static int kit_far_complete(const FPAgent *a,const FPObservation *o,const FPStratFrame *f) {
 (void)a;return o->level==3 && f->local[1]==1;
}
static int kit_far_act(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,FPStratFrame *f,int *skill) {
 *skill=FP_TRAVEL;
 if(f->local[1]==0) {
  if(o->level>1)return travel(a,o,s,0);
  if(o->level<1)return travel(a,o,s,1);
  if(wm_incoming_damage(a,o,s,FP_NOOP)>0)return -1;
  if(a->kit_loot_steps[1]<KIT_LOOT_STEPS) {
   a->kit_loot_steps[1]++;
   int act=seek_block(a,o,s,FP_CHEST,60);if(act>=0){*skill=FP_LOOT;return act;}
   act=frontier_walk(a,o,cfg,s);if(act>=0){*skill=FP_EXPLORE;return act;}
   a->kit_loot_steps[1]=KIT_LOOT_STEPS;
  }
  f->local[1]=1;
 }
 if(o->level<3)return travel(a,o,s,1);
 if(o->level>3)return travel(a,o,s,0);
 return -1;
}
static void kit_far_update(FPAgent *a) {
 if(a->kit_farloot==1 && !sg_active(a,FP_SG_X2))a->kit_farloot=2;
}

static constexpr void sg_kit_register(SGKind *table) {
 table[FP_SG_STOCK]=SGKind{"loot",kit_loot_complete,kit_loot_abandon,kit_loot_act};
 table[FP_SG_X2]=SGKind{"farloot",kit_far_complete,NULL,kit_far_act};
}
static int sg_kit_select(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 (void)cfg;(void)s;
 kit_far_update(a);
 if(kit_loot_entry(a,o))return sg_call(a,o,FP_SG_STOCK,0,KIT_LOOT_STEPS);
 if(kit_far_entry(a,o)){a->kit_farloot=1;return sg_call(a,o,FP_SG_X2,0,4000);}
 return 0;
}
