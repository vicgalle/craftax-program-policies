"""Preserve reference opening; route deliveries to the surface and iron mines."""

def emit(goal):
    defs = r'''
def int dx_search(out int skill) {
 int act=-1;
 if(a.level==2 && o.inv[I_TORCH]>0){skill=LIGHT;act=light_unseen(8,1200);if(act>=0)return act;act=dark_probe(12,600);if(act>=0)return act;}
 skill=EXPLORE;act=frontier_walk();if(act>=0)return act;
 return explore();
}
def int dx_route(int target,out int skill) {
 skill=TRAVEL;if(target==a.level)return NOOP;
 int act=travel(target>a.level);if(act>=0)return act;
 if(a.intent.stage==0)return -1;
 return dx_search(&skill);
}
def int dx_gather(int r,out int skill) {
 skill=GATHER;int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 int act=seek_block(blocks[r],1500);if(act>=0)return act;
 if(r==0 && a.level!=0)return dx_route(0,&skill);
 if(r==3 && a.level!=2)return dx_route(2,&skill);
 return dx_search(&skill);
}
def int dx_pick(out int skill) {
 int tier=o.inv[I_PICK]+1;
 if(o.inv[I_WOOD]<3+sq_reserve(0))return dx_gather(0,&skill);
 if(tier>=2 && o.inv[I_STONE]<1+sq_reserve(1))return dx_gather(1,&skill);
 skill=CRAFT;
 if(tier==1)return craft_radius(WOOD_PICK,500);
 return craft_radius(STONE_PICK,500);
}
def int dx_control(out int skill) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target>=0) {
  if(a.intent.stage>1 && o.inv[I_WOOD]<sq_reserve(0)+5)return dx_gather(0,&skill);
  if(a.level==2 && target>2 && a.floors[2].down<0) {
   if(o.inv[I_COAL]<2)return dx_gather(2,&skill);
   if(o.inv[I_TORCH]<4){skill=CRAFT;return craft_radius(MAKE_TORCH,800);}
  }
  return dx_route(target,&skill);
 }
 if(o.inv[I_PICK]<2)return dx_pick(&skill);
 if(a.intent.stage==1) {
  if(o.inv[I_WOOD]<sq_need(0))return dx_gather(0,&skill);
  if(o.inv[I_IRON]<sq_need(3))return dx_gather(3,&skill);
 } else {
  if(o.inv[I_IRON]<sq_need(3))return dx_gather(3,&skill);
  if(o.inv[I_WOOD]<sq_need(0))return dx_gather(0,&skill);
 }
 return -1;
}
'''
    rule=r'''
rule dx_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;try dx_control(&sk) as sk;
 }
}
'''
    return {'defs':defs,'rule':rule,'anchor':'rule r_strategic [build] {'}
