"""Candidate 1: retain gear preparation, add reserve surplus and explicit safe route search.

This module emits TR only; linking and the sequence monitor belong to the study
harness. It does not read goals, trajectories, or evaluation results.
"""
import re

RESOURCES = ("wood", "stone", "coal", "iron", "diamond")
ANCHOR = "rule r_strategic [build] {"


def emit(goal):
    """Return definitions and one rule using the harness's sq_* helpers.

    React only to the active stage. Obtain the minimum required pickaxe, then
    collect resources in fixed order. Search the current floor first and then
    travel toward the nearest remembered source, falling through to the frozen
    exploration policy when no source is known.
    """
    if not isinstance(goal, dict) or set(goal) - {"name", "sequence"}:
        raise ValueError("invalid goal fields")
    if not isinstance(goal.get("name"), str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", goal["name"]):
        raise ValueError("name must be an identifier")
    stages = goal.get("sequence")
    if not isinstance(stages, list) or not 1 <= len(stages) <= 8:
        raise ValueError("sequence must have 1..8 stages")
    for stage in stages:
        if not isinstance(stage, dict):
            raise ValueError("stage must be an object")
        if set(stage) == {"reach"}:
            if type(stage["reach"]) is not int or not 0 <= stage["reach"] <= 8:
                raise ValueError("reach must be a floor in 0..8")
        elif "acquire" in stage and not set(stage) - {"acquire", "maintain"}:
            for field in ("acquire", "maintain"):
                values = stage.get(field, {})
                if not isinstance(values, dict) or set(values) - set(RESOURCES):
                    raise ValueError("invalid resource mapping")
                for resource, count in values.items():
                    if type(count) is not int or not 0 <= count <= 20:
                        raise ValueError("resource thresholds must be integers in 0..20")
                    if field == "maintain" and count > stage["acquire"].get(resource, 0):
                        raise ValueError("maintain must not exceed acquire")
        else:
            raise ValueError("stage requires reach or acquire")
    defs = r'''
def int sr_gather(int r,out int skill) {
 int blocks[5]={TREE,STONE,COAL,IRON,DIAMOND};
 skill=GATHER;
 int act=seek_block(blocks[r],300);
 if(act<0 && r==0)act=seek_block(FIRE_TREE,300);
 if(act<0 && r==0 && a.level==7)act=harvest_walkable(ICE_SHRUB,TREE,300);
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

def int sr_pick(int tier,out int skill) {
 // Station placement may consume two wood and (for iron) one stone.
 int wood=3,stone=0,coal=0,iron=0;
 if(tier>=2)stone=1;
 if(tier>=3){stone=2;coal=1;iron=1;}
 if(o.inv[I_WOOD]<wood+sq_reserve(0))return sr_gather(0,&skill);
 if(o.inv[I_STONE]<stone+sq_reserve(1))return sr_gather(1,&skill);
 if(o.inv[I_COAL]<coal+sq_reserve(2))return sr_gather(2,&skill);
 if(o.inv[I_IRON]<iron+sq_reserve(3))return sr_gather(3,&skill);
 skill=CRAFT;
 if(tier==1)return craft_radius(WOOD_PICK,500);
 if(tier==2)return craft_radius(STONE_PICK,500);
 return craft_radius(IRON_PICK,500);
}

def int sr_route(int target,out int skill) {
 skill=TRAVEL;
 int act=travel(target>a.level);
 if(act>=0)return act;
 skill=EXPLORE;
 if(a.level==2 && o.inv[I_TORCH]>0){
  act=light_unseen(4,1200);
  if(act>=0){skill=LIGHT;return act;}
 }
 act=frontier_walk();
 if(act>=0)return act;
 return explore();
}

def int sr_control(out int skill) {
 if(sq_done())return -1;
 int target=sq_target();
 if(target>=0 && target<a.level)return sr_route(target,&skill);
 // Active reserves can otherwise block the base crafting rule repeatedly.
 int margin[5]={4,2,3,3,0};
 for(int r=0;r<4;r++) {
  if(sq_reserve(r)>0 && o.inv[r]<sq_reserve(r)+margin[r]) {
   int act=sr_gather(r,&skill);
   if(act>=0)return act;
  }
 }
 if(o.inv[I_PICK]<3 || o.inv[I_SWORD]<3)return -1;
 if(target>=0){
  if(target==a.level)return NOOP;
  if(!o.clear)return -1;
  int held=0;for(int r=0;r<5;r++)held+=sq_reserve(r);
  if(!held && !a.floors[a.level+1].seen)return -1;
  if(a.level==0 && o.inv[I_WOOD]<12+sq_reserve(0)) {
   int act=sr_gather(0,&skill);if(act>=0)return act;
  }
  return sr_route(target,&skill);
 }
 for(int r=0;r<5;r++) {
  if(o.inv[r]<sq_need(r) || o.inv[r]<sq_reserve(r)) {
   int act=sr_gather(r,&skill);
   if(act>=0)return act;
   skill=EXPLORE;
   act=frontier_walk();
   if(act>=0)return act;
   return explore();
  }
 }
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
    return {"defs": defs, "rule": rule, "anchor": ANCHOR}
