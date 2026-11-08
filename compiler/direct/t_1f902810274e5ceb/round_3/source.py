"""Preserve preparation and discovery; take known cleared Mines exit promptly."""
def emit(goal):
    return {
        'defs': r'''
def int ds_control(out int skill) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target<0)return -1;
 skill=TRAVEL;
 if(target==a.level)return NOOP;
 if(target<a.level)return travel(0);
 if(a.level!=2 || !o.clear)return -1;
 if(o.inv[I_PICK]<3 || o.inv[I_SWORD]<3)return -1;
 // Missing exits keep the frozen lighting, equipment and supply policy.
 return travel(1);
}
''',
        'rule': r'''
rule ds_sequence [build] {
 if(o.health>6 && o.food>3 && o.drink>3 && o.energy>3 && !wm_hostile_within(4)) {
  int sk=TRAVEL;
  try ds_control(&sk) as sk;
 }
}
''',
        'anchor': 'rule r_strategic [build] {'
    }
