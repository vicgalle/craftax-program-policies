"""Preserve first expedition and unknown-exit preparation; reuse known route legs."""
def emit(goal):
    defs = r'''
def int ds_control(out int sk) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target<0)return -1;
 sk=TRAVEL;
 if(target==a.level)return NOOP;
 if(target<a.level)return travel(0);
 if(a.intent.stage==0)return -1;
 if(a.level==0 && a.floors[1].seen && o.inv[I_PICK]>=3 && o.inv[I_SWORD]>=3) {
  if(o.drink<=6){a.drinking=1;sk=DRINK;return seek_water();}
  if(o.food<=5){sk=EAT;int act=forage();if(act<0)act=hunt(2,40);return act;}
  if(o.energy<=7){a.recovering=1;sk=RECOVER;return recover();}
  if(o.inv[I_WOOD]>=4 && o.inv[I_TORCH]>=4)return travel(1);
  if(a.floors[target].seen)return travel(1);
 }
 if(a.level==1 && a.floors[2].seen)return travel(1);
 if(a.level==2 && o.clear && a.floors[2].down>=0)return travel(1);
 if(a.floors[target].seen)return travel(1);
 return -1;
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
