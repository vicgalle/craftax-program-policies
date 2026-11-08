"""Prepare a safe weapon, complete ore stocks locally, and navigate actively."""
def emit(goal):
    return {'defs': r'''
def int ds_gather(int r,out int skill) {
 int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 skill=GATHER;
 int act=seek_block(blocks[r],2500);
 if(act>=0)return act;
 if(a.level>0) {
  skill=TRAVEL;act=travel(0);
  if(act>=0)return act;
 }
 skill=EXPLORE;return explore();
}
def int ds_craft(int recipe,int tier,out int skill) {
 int wood=3,stone=1,coal=0,iron=0;
 if(tier>=3){stone=2;coal=1;iron=1;}
 if(o.inv[I_WOOD]<wood+sq_reserve(0))return ds_gather(0,&skill);
 if(o.inv[I_STONE]<stone+sq_reserve(1))return ds_gather(1,&skill);
 if(o.inv[I_COAL]<coal+sq_reserve(2))return ds_gather(2,&skill);
 if(o.inv[I_IRON]<iron+sq_reserve(3))return ds_gather(3,&skill);
 skill=CRAFT;int act=craft_radius(recipe,1500);
 if(act>=0 && !sq_forbidden(act))return act;
 skill=EXPLORE;return explore();
}
def int ds_control(out int skill) {
 if(sq_done())return -1;
 if(o.inv[I_PICK]<1) {
  if(o.inv[I_WOOD]<3)return ds_gather(0,&skill);
  skill=CRAFT;return craft_radius(WOOD_PICK,1500);
 }
 if(o.inv[I_PICK]<2)return ds_craft(STONE_PICK,2,&skill);
 if(o.inv[I_SWORD]<2)return ds_craft(STONE_SWORD,2,&skill);
 if(o.inv[I_SWORD]<3)return ds_craft(IRON_SWORD,3,&skill);
 int target=sq_target();
 if(target>=0) {
  if(target==a.level)return NOOP;
  skill=TRAVEL;int act=travel(target>a.level);
  if(act>=0)return act;
  skill=EXPLORE;return explore();
 }
 if(o.inv[I_COAL]<6)return ds_gather(2,&skill);
 if(o.inv[I_IRON]<sq_need(3))return ds_gather(3,&skill);
 skill=EXPLORE;return explore();
}
''', 'rule': r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;
  try ds_control(&sk) as sk;
 }
}
''', 'anchor':'rule r_strategic [build] {'}
