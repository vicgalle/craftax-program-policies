"""Preserve initial development; intervene only after reserves become active."""


def emit(goal):
    defs = r'''
def int ds_supply(int r,out int skill) {
 int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 skill=GATHER;
 int act=seek_block(blocks[r],500);
 if(act>=0)return act;
 int best=-1,bd=99;
 for(int L=0;L<9;L++) {
  if(L==a.level || !a.floors[L].seen)continue;
  int found=0;
  for(int q=a.floors[L].known_lo;q<=a.floors[L].known_hi;q++) {
   int b=a.floors[L].map[q];
   if(b==blocks[r] || (r==0 && (b==FIRE_TREE || b==ICE_SHRUB))){found=1;break;}
  }
  int d=abs(L-a.level);
  if(found && d<bd){best=L;bd=d;}
 }
 if(best>=0){skill=TRAVEL;return travel(best>a.level);}
 return -1;
}

def int ds_control(out int skill) {
 if(sq_done() || a.intent.stage==0)return -1;
 int target=sq_target(),act=-1;
 if(target>=0) {
  if(a.level>target){skill=TRAVEL;return travel(0);}
  if(a.level==0 && o.inv[I_WOOD]<18)return ds_supply(0,&skill);
  if(a.level>0 && o.inv[I_WOOD]<6)return ds_supply(0,&skill);
  if(a.level==2 && (o.inv[I_SWORD]<4 || wm_armour()<2)) {
   skill=GATHER;
   if(o.inv[I_IRON]<sq_reserve(3)+3)act=seek_block(IRON,500);
   if(act<0 && o.inv[I_COAL]<3)act=seek_block(COAL,500);
   if(act>=0)return act;
   if(a.floors[2].steps<7000)return -1;
  }
  if(!o.clear && a.level>0)return -1;
  skill=TRAVEL;act=travel(target>a.level);
  if(act>=0)return act;
  if(a.level==2 && o.inv[I_TORCH]>0) {
   skill=LIGHT;
   act=light_unseen(a.floors[2].steps>3000?1:8,1500);
   if(act<0 && a.floors[2].steps>3000)act=dark_probe(10,1500);
   if(act>=0)return act;
  }
  skill=EXPLORE;return frontier_walk();
 }
 if(o.inv[I_WOOD]<sq_need(0))return ds_supply(0,&skill);
 if(o.inv[I_IRON]<sq_need(3))return ds_supply(3,&skill);
 return -1;
}
'''
    rule = r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;
  try ds_control(&sk) as sk;
 }
}
'''
    return {"defs": defs, "rule": rule, "anchor": "rule r_strategic [build] {"}
