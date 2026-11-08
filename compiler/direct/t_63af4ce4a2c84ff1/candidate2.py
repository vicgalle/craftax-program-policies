"""Keep inherited preparation, then return immediately once delivered to floor one."""

def emit(goal):
    return {'anchor':'rule r_strategic [build] {', 'defs':r'''
def int ds_control(out int sk) {
 if(sq_done() || a.intent.stage==0)return -1;
 int target=sq_target();
 if(target>=0) {
  if(target>a.level)return -1;
  sk=TRAVEL;
  if(target==a.level)return NOOP;
  int act=travel(0);
  if(act>=0)return act;
  sk=EXPLORE;return explore();
 }
 for(int r=0;r<2;r++) {
  if(o.inv[r]<sq_need(r)) {
   sk=GATHER;int act=seek_block(r==0?TREE:STONE,500);
   if(act>=0)return act;
   if(a.level>0){sk=TRAVEL;act=travel(0);if(act>=0)return act;}
   sk=EXPLORE;return explore();
  }
 }
 return -1;
}
''', 'rule':r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;try ds_control(&sk) as sk;
 }
}
'''}
