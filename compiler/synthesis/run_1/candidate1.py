"""Reserve-aware stock repair with incumbent preparation and visited-route travel."""

def emit(goal):
    defs = r'''
def int sr_local(int r) {
 int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 int act=seek_block(blocks[r],800);
 if(act<0 && r==0 && a.level==7)act=harvest_walkable(ICE_SHRUB,TREE,800);
 return act;
}

def int sr_search(out int skill) {
 skill=LIGHT;
 int act=-1;
 if(a.level==2 || a.level==5 || a.level==7) {
  act=light_unseen(8,1000);
  if(act<0)act=dark_probe(8,1000);
 }
 if(act>=0)return act;
 skill=EXPLORE;
 act=frontier_walk();
 if(act>=0)return act;
 return explore();
}

def int sr_gather(int r,out int skill) {
 skill=GATHER;
 int act=sr_local(r);
 if(act>=0)return act;
 int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 int best=-1,bd=99;
 for(int L=0;L<9;L++) {
  if(L==a.level || !a.floors[L].seen)continue;
  int found=0;
  for(int q=a.floors[L].known_lo;q<=a.floors[L].known_hi;q++) {
   int b=a.floors[L].map[q];
   if(b==blocks[r] || (r==0 && (b==FIRE_TREE || b==ICE_SHRUB)) || (r==1 && b==STALAGMITE)){found=1;break;}
  }
  int d=abs(L-a.level);
  if(found && d<bd){best=L;bd=d;}
 }
 if(best>=0){skill=TRAVEL;act=travel(best>a.level);if(act>=0)return act;}
 if(r==0 && a.level>0 && a.level<6){skill=TRAVEL;act=travel(0);if(act>=0)return act;}
 if(a.level==1){skill=TRAVEL;act=travel(r>=3 && a.floors[2].seen);if(act>=0)return act;}
 return sr_search(&skill);
}

def int sr_repair(out int skill) {
 int margin[5]={4,2,3,3,0};
 for(int r=0;r<5;r++) {
  if(sq_reserve(r)>0 && o.inv[r]<sq_reserve(r)+margin[r]) {
   int act=sr_gather(r,&skill);
   if(act>=0)return act;
  }
 }
 return -1;
}

def int sr_control(out int skill) {
 if(sq_done())return -1;
 int act=sr_repair(&skill);
 if(act>=0)return act;
 int target=sq_target();
 if(target>=0) {
  skill=TRAVEL;
  if(target==a.level)return NOOP;
  if(target>a.level && !a.floors[target].seen)return -1;
  act=travel(target>a.level);
  if(act>=0)return act;
  return sr_search(&skill);
 }
 if(o.inv[I_PICK]<3 || o.inv[I_SWORD]<3)return -1;
 for(int r=0;r<5;r++)
  if(o.inv[r]<sq_need(r) || o.inv[r]<sq_reserve(r))return sr_gather(r,&skill);
 return -1;
}
'''
    rule = r'''
rule sr_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;
  try sr_control(&sk) as sk;
 }
}
'''
    return {'defs': defs, 'rule': rule, 'anchor': 'rule r_strategic [build] {'}
