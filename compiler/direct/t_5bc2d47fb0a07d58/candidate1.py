"""Precollect coal in the daylight, then execute the short floor itinerary."""

def emit(goal):
    return {'defs': r'''
def int dc_control(out int skill) {
 if(sq_done())return -1;
 skill=CRAFT;
 if(o.inv[I_PICK]<1)return craft_radius(WOOD_PICK,500);
 if(o.inv[I_SWORD]<2)return craft_radius(STONE_SWORD,500);
 if(o.inv[I_PICK]<2)return craft_radius(STONE_PICK,500);
 int act=-1;
 if(a.intent.stage==0 && a.level==0 && o.inv[I_COAL]<6) {
  skill=GATHER;act=seek_block(COAL,1000);
  if(act>=0)return act;
  skill=EXPLORE;return explore();
 }
 int target=sq_target();
 if(target>=0) {
  if(target==a.level)return NOOP;
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
