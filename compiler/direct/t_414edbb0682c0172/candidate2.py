"""Shallow delivery relying on the inherited higher-priority survival rules."""

def emit(goal):
    defs = r'''
def int ds_gather(int resource,out int skill) {
 skill=GATHER;
 int blocks[2]={TREE,STONE};
 int act=seek_block(blocks[resource],500);
 if(act>=0)return act;
 skill=EXPLORE;
 act=frontier_walk();
 if(act>=0)return act;
 return explore();
}

def int ds_control(out int skill) {
 if(sq_done())return -1;
 skill=CRAFT;
 if(o.inv[I_PICK]<1)return craft_radius(WOOD_PICK,500);
 if(o.inv[I_SWORD]<2) {
  if(o.inv[I_WOOD]<sq_reserve(0)+3)return ds_gather(0,&skill);
  if(o.inv[I_STONE]<sq_reserve(1)+1)return ds_gather(1,&skill);
  return craft_radius(STONE_SWORD,500);
 }
 for(int r=0;r<2;r++) {
  if(o.inv[r]<sq_need(r) || o.inv[r]<sq_reserve(r))return ds_gather(r,&skill);
 }
 int target=sq_target();
 if(target>=0 && target!=a.level) {
  skill=TRAVEL;
  int act=travel(target>a.level);
  if(act>=0)return act;
 }
 skill=EXPLORE;
 int act=frontier_walk();
 if(act>=0)return act;
 return explore();
}
'''
    rule = r'''
rule ds_sequence [build] {
 if(!sq_done()) {
  int sk=GATHER;
  try ds_control(&sk) as sk;
 }
}
'''
    return {'defs': defs, 'rule': rule, 'anchor': 'rule r_strategic [build] {'}
