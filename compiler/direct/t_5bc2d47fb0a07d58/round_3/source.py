"""Prepare iron gear, two plates and departure supplies before the coal itinerary."""

def emit(goal):
    return {'defs': r'''
def int dc_control(out int skill) {
 if(sq_done())return -1;
 skill=CRAFT;
 if(o.inv[I_PICK]<1)return craft_radius(WOOD_PICK,500);
 if(o.inv[I_SWORD]<2)return craft_radius(STONE_SWORD,500);
 if(o.inv[I_PICK]<2)return craft_radius(STONE_PICK,500);
 if(a.intent.stage==0 && (o.inv[I_PICK]<3 || o.inv[I_SWORD]<3)) {
 int prep=-1;
 if(a.level>0){skill=TRAVEL;prep=travel(0);}
 else {skill=CRAFT;prep=craft_radius(o.inv[I_SWORD]<3?IRON_SWORD:IRON_PICK,1000);}
 if(prep>=0)return prep;
 skill=EXPLORE;return explore();
}
int act=-1;
if(a.intent.stage==0 && a.level==0 && o.inv[I_COAL]<6) {
  skill=GATHER;act=seek_block(COAL,1000);
  if(act>=0)return act;
  skill=EXPLORE;return explore();
 }
 int target=sq_target();
 if(target>=0) {
  if(target==a.level)return NOOP;
  if(target>a.level && a.level==0 && !(a.shallow_go && a.t-a.shallow_go<200)) {
   if(o.drink<=6){a.drinking=1;skill=DRINK;act=seek_water();if(act>=0)return act;}
   if(o.food<=5){skill=EAT;act=forage();if(act<0)act=hunt(2,40);if(act>=0)return act;}
   if(o.energy<=7){a.recovering=1;skill=RECOVER;act=recover();if(act>=0)return act;}
   a.shallow_go=a.t+1;
  }
  skill=TRAVEL;act=travel(target>a.level);
  if(act>=0)return act;
  skill=EXPLORE;return explore();
 }
 if(o.inv[I_COAL]<sq_need(2) || o.inv[I_COAL]<sq_reserve(2)) {
  skill=GATHER;act=seek_block(COAL,1000);
  if(act>=0)return act;
  if(a.level>0){skill=TRAVEL;act=travel(0);if(act>=0)return act;}
  skill=EXPLORE;return explore();
 }
 return -1;
}
''', 'rule': r'''
rule dc_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;
  try dc_control(&sk) as sk;
 }
}
''', 'anchor': 'rule r_strategic [build] {'}
