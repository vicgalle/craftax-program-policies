"""Depart once an iron sword is ready, carrying all shallow delivery stock."""

def emit(goal):
    return {'anchor':'rule r_strategic [build] {', 'defs':r'''
def int ds_control(out int sk) {
 if(sq_done() || a.intent.stage==0)return -1;
 int target=sq_target();
 if(target>=0) {
  if(target>a.level) {
   if(o.inv[I_SWORD]<3 || o.inv[I_PICK]<2)return -1;
   if(a.level==0) {
    sk=GATHER;
    if(o.inv[I_WOOD]<12)return seek_block(TREE,500);
    if(o.inv[I_STONE]<5)return seek_block(STONE,500);
   }
   sk=TRAVEL;int act=travel(1);
   if(act>=0)return act;
   sk=EXPLORE;return explore();
  }
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
