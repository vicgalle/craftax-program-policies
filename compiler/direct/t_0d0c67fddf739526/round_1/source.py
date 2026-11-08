"""Prepare with the inherited equipment policy, then deliver a carried surplus."""


def emit(goal):
    defs = r'''
def int ds_gather(int r,out int skill) {
 int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 skill=GATHER;
 int act=seek_block(blocks[r],1000);
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
 if(best>=0){skill=TRAVEL;act=travel(best>a.level);if(act>=0)return act;}
 skill=EXPLORE;
 act=frontier_walk();
 if(act<0)act=explore();
 return act;
}

def int ds_route(int target,out int skill) {
 skill=TRAVEL;
 int act=-1;
 if(target<a.level)return travel(0);
 if(target==a.level)return NOOP;
 if(o.clear || a.level==0)act=travel(1);
 if(act>=0)return act;
 if(!o.clear){skill=FIGHT;act=hunt(5,40);if(act>=0)return act;}
 if(a.level==2 && o.inv[I_TORCH]>0) {
  skill=LIGHT;
  act=light_unseen(8,1000);
  if(act<0 && a.floors[a.level].steps>1800)act=light_unseen(1,2500);
  if(act<0 && a.floors[a.level].steps>3000)act=dark_probe(10,2500);
  if(act>=0)return act;
 }
 skill=EXPLORE;
 act=frontier_walk();
 if(act<0)act=explore();
 return act;
}

def int ds_control(out int skill) {
 if(sq_done())return -1;
 if(a.intent.stage==0 && (o.inv[I_PICK]<3 || o.inv[I_SWORD]<3))return -1;
 int wood=sq_need(0),iron=sq_need(3);
 if(wood<18)wood=18;
 if(iron<5)iron=5;
 if(o.inv[I_WOOD]<wood)return ds_gather(0,&skill);
 if(o.inv[I_IRON]<iron)return ds_gather(3,&skill);
 int target=sq_target();
 if(target>=0)return ds_route(target,&skill);
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
