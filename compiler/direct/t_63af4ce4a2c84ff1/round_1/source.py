"""Prepare a stone sword, preload wood and stone, and visit only the first floor."""

def emit(goal):
    return {'anchor': 'rule r_strategic [build] {', 'defs': r'''
def int ds_stock(int r,int n,out int sk) {
 sk=GATHER;
 if(o.inv[r]>=n)return -1;
 return seek_block(r==0?TREE:STONE,500);
}
def int ds_craft(int act,int wood,int stone,out int sk) {
 int n=ds_stock(0,wood+sq_reserve(0),&sk);
 if(o.inv[I_WOOD]<wood+sq_reserve(0))return n;
 n=ds_stock(1,stone+sq_reserve(1),&sk);
 if(o.inv[I_STONE]<stone+sq_reserve(1))return n;
 sk=CRAFT;return craft_radius(act,500);
}
def int ds_control(out int sk) {
 if(sq_done())return -1;
 if(o.inv[I_PICK]<1)return ds_craft(WOOD_PICK,3,0,&sk);
 if(o.inv[I_SWORD]<2)return ds_craft(STONE_SWORD,3,1,&sk);
 if(a.intent.stage<3 && a.level==0) {
  if(o.inv[I_WOOD]<12)return ds_stock(0,12,&sk);
  if(o.inv[I_STONE]<5)return ds_stock(1,5,&sk);
 }
 int target=sq_target();
 if(target>=0) {
  if(target==a.level)return NOOP;
  sk=TRAVEL;int act=travel(target>a.level);
  if(act>=0)return act;
  sk=EXPLORE;return explore();
 }
 for(int r=0;r<2;r++)if(o.inv[r]<sq_need(r)) {
  int act=ds_stock(r,sq_need(r),&sk);
  if(act>=0)return act;
  if(a.level>0){sk=TRAVEL;return travel(0);}
  sk=EXPLORE;return explore();
 }
 sk=EXPLORE;return explore();
}
''', 'rule': r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;try ds_control(&sk) as sk;
 }
}
'''}
