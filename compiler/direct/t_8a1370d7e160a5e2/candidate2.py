"""Keep spendable stone above the maintained floor throughout preparation."""

def emit(goal):
    defs = r'''
def int ds_route(out int sk) {
 if(sq_done())return -1;
 if(o.inv[I_PICK]>=1 && o.inv[I_STONE]<6){sk=GATHER;int act=seek_block(STONE,500);if(act>=0)return act;}
 if(o.inv[I_PICK]<3 || o.inv[I_SWORD]<3)return -1;
 if(o.inv[I_STONE]<4){sk=GATHER;int act=seek_block(STONE,500);if(act>=0)return act;}
 int target=sq_target();
 if(target<0)return -1;
 sk=TRAVEL;
 if(target==a.level)return NOOP;
 if(target<a.level)return travel(0);
 if(a.level<2)return -1;
 if(a.level==2 && (!o.clear || o.inv[I_SWORD]<4 || wm_armour()<2))return -1;
 int act=travel(1);
 if(act>=0)return act;
 sk=LIGHT;
 act=light_unseen(8,1200);if(act>=0)return act;
 act=dark_probe(8,1200);if(act>=0)return act;
 sk=EXPLORE;act=frontier_walk();if(act>=0)return act;
 return explore();
}
'''
    rule = r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=GATHER;try ds_route(&sk) as sk;
 }
}
'''
    return {'defs':defs, 'rule':rule, 'anchor':'rule r_strategic [build] {'}
