"""Minimal mining preparation with survival rules deciding when to intervene."""

def emit(goal):
    defs = r'''
def int ds_control(out int skill) {
 if(sq_done())return -1;
 int act=-1;
 int target=sq_target();
 if(target>=0) {
  skill=TRAVEL;
  if(target==a.level)return NOOP;
  act=travel(target>a.level);
  if(act>=0)return act;
 } else {
  skill=CRAFT;
  if(o.inv[I_PICK]<1)act=craft_radius(WOOD_PICK,500);
  else if(o.inv[I_PICK]<2)act=craft_radius(STONE_PICK,500);
  else if(o.inv[I_SWORD]<2)act=craft_radius(STONE_SWORD,500);
  else if(o.inv[I_IRON]<sq_need(3)) {
   skill=GATHER;
   act=seek_block(IRON,500);
  }
  if(act>=0)return act;
 }
 skill=EXPLORE;
 act=frontier_walk();
 if(act>=0)return act;
 return explore();
}
'''
    rule = r'''
rule ds_sequence [build] {
 if(o.health>4 && o.food>1 && o.drink>1 && o.energy>1) {
  int sk=GATHER;
  try ds_control(&sk) as sk;
 }
}
'''
    return {'defs': defs, 'rule': rule, 'anchor': 'rule r_strategic [build] {'}
