"""Task-specific route and stock controller with iron-first deliveries."""

def emit(goal):
    defs = r'''
def int ds_search(out int sk) {
 int act=-1;
 if(a.level==2) {
  if(o.inv[I_TORCH]>0){sk=LIGHT;act=light_unseen(8,1000);if(act>=0)return act;act=dark_probe(12,600);if(act>=0)return act;}
 }
 sk=EXPLORE;act=frontier_walk();if(act>=0)return act;
 return explore();
}
def int ds_route(int target,out int sk) {
 if(a.level==target){sk=RECOVER;return NOOP;}
 sk=TRAVEL;int act=travel(target>a.level);
 if(act>=0)return act;
 return ds_search(&sk);
}
def int ds_gather(int r,out int sk) {
 sk=GATHER;int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 int act=seek_block(blocks[r],1500);if(act>=0)return act;
 if(r==0 && a.level!=0)return ds_route(0,&sk);
 if(r==3 && a.level!=2)return ds_route(2,&sk);
 return ds_search(&sk);
}
def int ds_pick(out int sk) {
 int tier=o.inv[I_PICK]+1;
 if(o.inv[I_WOOD]<sq_reserve(0)+5)return ds_gather(0,&sk);
 if(tier>=2 && o.inv[I_STONE]<sq_reserve(1)+2)return ds_gather(1,&sk);
 sk=CRAFT;
 if(tier==1)return craft_radius(WOOD_PICK,1500);
 return craft_radius(STONE_PICK,1500);
}
def int ds_control(out int sk) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target>=0) {
  if(sq_reserve(0)>0 && o.inv[I_WOOD]<sq_reserve(0)+4)return ds_gather(0,&sk);
  if(a.level==2 && target>2 && a.floors[2].down<0) {
   if(o.inv[I_WOOD]<sq_reserve(0)+5)return ds_gather(0,&sk);
   if(o.inv[I_COAL]<2)return ds_gather(2,&sk);
   if(o.inv[I_TORCH]<4){sk=CRAFT;return craft_radius(MAKE_TORCH,1500);}
  }
  return ds_route(target,&sk);
 }
 if(o.inv[I_PICK]<2)return ds_pick(&sk);
 if(o.inv[I_IRON]<sq_need(3))return ds_gather(3,&sk);
 if(o.inv[I_WOOD]<sq_need(0))return ds_gather(0,&sk);
 return -1;
}
'''
    rule=r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;try ds_control(&sk) as sk;
 }
}
'''
    return {'defs':defs,'rule':rule,'anchor':'rule r_strategic [build] {'}
