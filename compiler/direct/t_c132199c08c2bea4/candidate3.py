"""Launch with iron tools and modest wood; provision once, then take clear exits."""

def emit(goal):
    defs = r'''
def int ds_launch(out int skill) {
 if(o.inv[I_PICK]<3 || o.inv[I_SWORD]<3 || o.inv[I_WOOD]<12 || !o.clear)return -1;
 int act=-1;
 if(!(a.shallow_go && a.t-a.shallow_go<200)) {
  if(o.drink<=6){skill=DRINK;a.drinking=1;return seek_water();}
  if(o.food<=5) {
   skill=EAT;
   act=forage();if(act>=0)return act;
   act=hunt(2,40);if(act>=0)return act;
   return explore();
  }
  if(o.energy<=8 || o.health<8+o.str-1.01f){skill=RECOVER;a.recovering=1;return recover();}
  a.shallow_go=a.t+1;
 }
 skill=TRAVEL;
 return travel(1);
}

def int ds_control(out int skill) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target<0)return -1;
 skill=TRAVEL;
 if(target==a.level)return NOOP;
 if(target<a.level)return travel(0);
 if(a.intent.stage>0)return travel(1);
 if(a.level==0)return ds_launch(&skill);
 if(a.level==2 && o.clear && o.inv[I_PICK]>=3 && o.inv[I_SWORD]>=3)return travel(1);
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
