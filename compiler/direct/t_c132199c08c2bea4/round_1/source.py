"""Clear-gated shallow travel with iron equipment readiness."""

def emit(goal):
    defs = r'''
def int ds_control(out int skill) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target<0)return -1;
 if(target==a.level){skill=TRAVEL;return NOOP;}
 if(target<a.level){skill=TRAVEL;return travel(0);}
 if(!o.clear)return -1;
 if(a.level==0 && (o.inv[I_PICK]<3 || o.inv[I_SWORD]<3 || o.inv[I_WOOD]<12))return -1;
 if(a.level==2 && (o.inv[I_PICK]<3 || o.inv[I_SWORD]<3))return -1;
 skill=TRAVEL;
 int act=travel(1);
 if(act>=0)return act;
 if(a.level==2 && o.inv[I_TORCH]>0) {
  skill=LIGHT;
  act=light_unseen(8,1200);
  if(act>=0)return act;
  act=dark_probe(12,1000);
  if(act>=0)return act;
 }
 skill=EXPLORE;
 return frontier_walk();
}
'''
    rule = r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=TRAVEL;
  try ds_control(&sk) as sk;
 }
}
'''
    return {'defs':defs,'rule':rule,'anchor':'rule r_strategic [build] {'}
