/* Observation-only semantic controller. Included after the shared mechanics. */
#include <stdio.h>
#include <errno.h>
static int goal_error(char *error,int size,const char *message) {
 if(error && size>0)snprintf(error,(size_t)size,"%s",message);
 return 0;
}
static int goal_value(const FPGoal *g,const FPObservation *o) {
 if(g->kind==FP_GOAL_INVENTORY)return o->inv[g->subject];
 if(g->kind==FP_GOAL_EQUIPMENT)return g->subject==FP_E_PICK?o->inv[FP_I_PICK]:
  g->subject==FP_E_SWORD?o->inv[FP_I_SWORD]:o->armour[g->subject-FP_E_HELMET];
 if(g->kind==FP_GOAL_FLOOR)return o->level;
 return 0;
}
int fp_goal_satisfied(const FPGoal *g,const FPObservation *o) {
 if(!g || !o || g->kind<FP_GOAL_INVENTORY || g->kind>FP_GOAL_PREPARE)return 0;
 if(g->kind==FP_GOAL_INVENTORY && (g->subject<0 || g->subject>FP_I_ARROW))return 0;
 if(g->kind==FP_GOAL_EQUIPMENT && (g->subject<0 || g->subject>FP_E_BOOTS))return 0;
 if(g->kind==FP_GOAL_ADJACENT) {
  if(g->direction<1 || g->direction>4)return 0;
  int i=49+dr[g->direction]*11+dc[g->direction];
  return o->visible[i] && o->blocks[i]==g->subject;
 }
 if(g->kind==FP_GOAL_RECOVERY)return o->health>=g->lo && o->energy>=6 && o->food>=3 && o->drink>=3;
 if(g->kind==FP_GOAL_PREPARE)return o->health>=8 && o->energy>=6 && o->food>=6 && o->drink>=6 &&
  o->inv[FP_I_PICK]>=2 && o->inv[FP_I_SWORD]>=2 && o->inv[FP_I_WOOD]>=g->lo;
 int value=goal_value(g,o);
 return value>=g->lo && (g->relation==FP_AT_LEAST || value<=g->hi);
}
int fp_validate_command(const FPCommand *c,char *error,int size) {
 if(!c || c->version!=FP_COMMAND_VERSION || c->count<1 || c->count>FP_MAX_GOALS || c->stages<1 || c->stages>c->count)
  return goal_error(error,size,"invalid command version or size");
 int stage=-1;
 for(int i=0;i<c->count;i++) {
  const FPGoal *g=&c->goals[i];
  if(c->stage[i]<stage || c->stage[i]>stage+1)return goal_error(error,size,"stages must be contiguous and ordered");
  stage=c->stage[i];
  if(g->kind<FP_GOAL_INVENTORY || g->kind>FP_GOAL_PREPARE || g->relation<FP_EQ || g->relation>FP_AT_LEAST ||
     g->lo<0 || g->hi<g->lo || (g->relation==FP_EQ && g->lo!=g->hi))return goal_error(error,size,"invalid goal bounds");
  if(g->kind==FP_GOAL_INVENTORY && (g->subject<0 || g->subject>FP_I_ARROW || g->hi>99))return goal_error(error,size,"invalid inventory subject or quantity");
  if(g->kind==FP_GOAL_EQUIPMENT && (g->subject<0 || g->subject>FP_E_BOOTS || g->lo<1 || g->hi>(g->subject<=FP_E_SWORD?4:2)))return goal_error(error,size,"invalid equipment tier");
  if(g->kind==FP_GOAL_ADJACENT && (g->subject<FP_BOUNDARY || g->subject>FP_VULNERABLE || g->direction<1 || g->direction>4 || g->lo || g->hi || g->relation!=FP_EQ))return goal_error(error,size,"invalid adjacency binding");
  if(g->kind==FP_GOAL_FLOOR && (g->hi>=FP_LEVELS || g->relation!=FP_EQ || g->subject || g->direction))return goal_error(error,size,"floor must be exact");
  if(g->kind==FP_GOAL_RECOVERY && (g->lo<1 || g->hi>13))return goal_error(error,size,"invalid recovery threshold");
  if(g->kind==FP_GOAL_PREPARE && (g->lo<1 || g->hi>60))return goal_error(error,size,"invalid preparation stock");
  if(g->kind!=FP_GOAL_ADJACENT && g->direction)return goal_error(error,size,"unexpected direction");
  if((g->kind==FP_GOAL_RECOVERY || g->kind==FP_GOAL_PREPARE) && (g->relation!=FP_EQ || g->lo!=g->hi || g->subject))return goal_error(error,size,"invalid extension fields");
  for(int j=0;j<i;j++)if(c->stage[j]!=stage && g->kind==FP_GOAL_EQUIPMENT && c->goals[j].kind==g->kind && c->goals[j].subject==g->subject && g->relation!=FP_AT_LEAST && g->hi<c->goals[j].lo)
   return goal_error(error,size,"equipment cannot be downgraded in a sequence");
  for(int j=0;j<c->count;j++)if(c->stage[j]==stage && c->goals[j].kind==FP_GOAL_PREPARE && g->relation!=FP_AT_LEAST) {
   if((g->kind==FP_GOAL_EQUIPMENT && g->subject<=FP_E_SWORD && g->hi<2) ||
      (g->kind==FP_GOAL_INVENTORY && g->subject==FP_I_WOOD && g->hi<c->goals[j].lo))return goal_error(error,size,"preparation conflicts with conjunction");
  }
  for(int j=0;j<i;j++)if(c->stage[j]==stage) {
   const FPGoal *h=&c->goals[j];
   if(g->kind!=h->kind || g->subject!=h->subject) {
    if(g->kind==FP_GOAL_ADJACENT && h->kind==g->kind && g->direction==h->direction)return goal_error(error,size,"contradictory directional binding");
    continue;
   }
   if(g->kind==FP_GOAL_ADJACENT)continue;
   int high_g=g->relation==FP_AT_LEAST?INT_MAX:g->hi,high_h=h->relation==FP_AT_LEAST?INT_MAX:h->hi;
   if(g->lo>high_h || h->lo>high_g)return goal_error(error,size,"contradictory conjunction");
  }
 }
 if(stage+1!=c->stages)return goal_error(error,size,"incorrect stage count");
 if(error && size>0)error[0]=0;
 return 1;
}
static int goal_name(const char *s,const char *const *names,int n) {
 for(int i=0;i<n;i++)if(!strcmp(s,names[i]))return i;
 return -1;
}
static int goal_number(const char *s,int *value) {
 char *end;errno=0;long x=strtol(s,&end,10);
 if(errno || !*s || *end || x<0 || x>99)return 0;
 *value=(int)x;return 1;
}
int fp_parse_command(const char *text,FPCommand *out,char *error,int size) {
 if(!text || !*text || strlen(text)>=512)return goal_error(error,size,"empty or oversized command");
 FPCommand c={0};c.version=FP_COMMAND_VERSION;c.stages=1;
 char buffer[512];strcpy(buffer,text);char *part=buffer;
 static const char *const inv[]={"wood","stone","coal","iron","diamond","sapphire","ruby","sapling","torch","arrow"};
 static const char *const equip[]={"pick","sword","helmet","chestplate","pants","boots"};
 static const char *const blocks[]={"invalid","boundary","grass","water","stone","tree","wood","path","coal","iron","diamond","table","furnace","sand","lava","plant","ripe","wall","darkness","moss","stalagmite","sapphire","ruby","chest","fountain","fire_grass","ice_grass","gravel","fire_tree","ice_shrub","fire_table","ice_table","boss","grave","grave2","grave3","vulnerable"};
 static const char *const directions[]={"","left","right","up","down"};
 while(part) {
  char *end=strpbrk(part,"+;"),separator=0;if(end){separator=*end;*end=0;}
  if(!*part || c.count>=FP_MAX_GOALS)return goal_error(error,size,"empty or too many goals");
  char *fields[6];int n=0;char *p=part;
  while(p && n<6){fields[n++]=p;char *q=strchr(p,':');if(q){*q=0;p=q+1;}else p=NULL;}
  if(p)return goal_error(error,size,"too many fields");
  FPGoal g={0};
  if(!strcmp(fields[0],"inv") || !strcmp(fields[0],"equip")) {
   if(n<4 || n>5)return goal_error(error,size,"quantity syntax: inv:subject:eq:N / range:L:H / min:N");
   g.kind=!strcmp(fields[0],"inv")?FP_GOAL_INVENTORY:FP_GOAL_EQUIPMENT;
   g.subject=goal_name(fields[1],g.kind==FP_GOAL_INVENTORY?inv:equip,g.kind==FP_GOAL_INVENTORY?10:6);
   if(!strcmp(fields[2],"eq"))g.relation=FP_EQ;
   else if(!strcmp(fields[2],"range"))g.relation=FP_RANGE;
   else if(!strcmp(fields[2],"min"))g.relation=FP_AT_LEAST;
   else return goal_error(error,size,"unknown relation");
   if(n!=(g.relation==FP_RANGE?5:4) || !goal_number(fields[3],&g.lo))return goal_error(error,size,"invalid quantity");
   g.hi=g.relation==FP_AT_LEAST?(g.kind==FP_GOAL_INVENTORY?99:g.subject<=1?4:2):g.lo;
   if(g.relation==FP_RANGE && !goal_number(fields[4],&g.hi))return goal_error(error,size,"invalid upper quantity");
  } else if(!strcmp(fields[0],"adj")) {
   if(n!=3)return goal_error(error,size,"adjacency syntax: adj:block:direction");
   g.kind=FP_GOAL_ADJACENT;g.subject=goal_name(fields[1],blocks,37);g.direction=goal_name(fields[2],directions,5);
  } else {
   if(n!=2 || !goal_number(fields[1],&g.lo))return goal_error(error,size,"invalid floor, recovery or preparation goal");
   if(!strcmp(fields[0],"floor"))g.kind=FP_GOAL_FLOOR;
   else if(!strcmp(fields[0],"recover"))g.kind=FP_GOAL_RECOVERY;
   else if(!strcmp(fields[0],"prepare"))g.kind=FP_GOAL_PREPARE;
   else return goal_error(error,size,"unknown goal family");
   g.hi=g.lo;
  }
  c.goals[c.count]=g;c.stage[c.count++]=c.stages-1;
  if(separator==';')c.stages++;
  part=end?end+1:NULL;
 }
 if(!fp_validate_command(&c,error,size))return 0;
 *out=c;return 1;
}
int fp_set_command(FPAgent *a,const FPCommand *c,char *error,int size) {
 if(!fp_validate_command(c,error,size))return 0;
 int reference=a->intent.reference;
 memset(&a->intent,0,sizeof(a->intent));a->intent.command=*c;
 a->intent.enabled=1;a->intent.active=-1;a->intent.status=FP_CMD_ACTIVE;
 a->intent.started=a->t;a->intent.bound_target=-1;a->intent.bound_level=-1;a->intent.reference=reference;
 return 1;
}
void fp_clear_command(FPAgent *a) { memset(&a->intent,0,sizeof(a->intent)); }
static FPGoal minimum_goal(int kind,int subject,int quantity) {
 return (FPGoal){kind,subject,FP_AT_LEAST,quantity,kind==FP_GOAL_INVENTORY?99:subject>=FP_E_HELMET?2:4,0};
}
/* Affordances observed on another floor can satisfy a missing local resource.
 * Reuse ladder navigation; no resource-specific floor routing table is needed. */
static int command_known_floor(FPAgent *a,const FPObservation *o,FPScratch *s,uint64_t mask) {
 int best=-1,cost=INT_MAX;
 for(int l=0;l<FP_LEVELS;l++)if(l!=a->level && a->floors[l].seen) {
  const FPFloor *f=&a->floors[l];int found=0;
  for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]<64 && ((mask>>f->map[p])&1)){found=1;break;}
  if(found && abs(l-a->level)<cost){best=l;cost=abs(l-a->level);}
 }
 if(best<0)return -1;
 a->intent.subgoal=(FPGoal){FP_GOAL_FLOOR,0,FP_EQ,best,best,0};
 return travel(a,o,s,best>a->level);
}
static int command_craft(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action,int depth);
static int command_resource(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int resource,int quantity,int depth) {
 if(depth>12)return -1;
 static const int blocks[]={FP_TREE,FP_STONE,FP_COAL,FP_IRON,FP_DIAMOND,FP_SAPPHIRE,FP_RUBY};
 static const int picks[]={0,1,1,2,3,4,4};
 a->intent.subgoal=minimum_goal(FP_GOAL_INVENTORY,resource,quantity);
 if(resource<=FP_I_RUBY) {
  int need=picks[resource];
  if(o->inv[FP_I_PICK]<need) {
   static const int actions[]={0,FP_WOOD_PICK,FP_STONE_PICK,FP_IRON_PICK,FP_DIAMOND_PICK};
   return command_craft(a,o,cfg,s,actions[need],depth+1);
  }
  int action=seek_block(a,o,s,blocks[resource],100);
  /* A tool is a navigation capability too: a wooded island enclosed by
   * stone must acquire a pick before exploration can expose more wood. */
  if(action<0 && !a->intent.reference && !o->inv[FP_I_PICK] && o->inv[0]>=3)
   return command_craft(a,o,cfg,s,FP_WOOD_PICK,depth+1);
  if(action<0 && !a->intent.reference) {
   uint64_t mask=MASK(blocks[resource]);if(resource==0)mask|=MASK(FP_FIRE_TREE)|MASK(FP_ICE_SHRUB);
   if(resource==1)mask|=MASK(FP_STALAGMITE);
   return command_known_floor(a,o,s,mask);
  }
  return action;
 }
 if(resource==FP_I_TORCH)return command_craft(a,o,cfg,s,FP_MAKE_TORCH,depth+1);
 if(resource==FP_I_ARROW)return command_craft(a,o,cfg,s,FP_MAKE_ARROW,depth+1);
 if(resource==FP_I_SAPLING)return seek_block(a,o,s,FP_GRASS,100);
 return -1;
}
static int command_craft(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,int action,int depth) {
 if(depth>12)return -1;
 const Recipe *recipe=NULL;
 for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++)if(recipes[i].action==action)recipe=&recipes[i];
 if(!recipe)return -1;
 int table=0,furnace=0;const FPFloor *f=&a->floors[a->level];
 for(int p=f->known_lo;p<=f->known_hi;p++)if(s->dist[p]< (a->intent.reference?251:INF)) {
  if(near(a,p,0))table=1;if(near(a,p,recipe->furnace))furnace=1;
 }
 for(int i=0;i<5;i++) {
  int quantity=recipe->cost[i]+(i==0&&!table?2:0)+(i==1&&recipe->furnace&&!furnace?1:0);
  /* Reserve the final conjunction's stock in addition to construction and
   * recipe inputs. Capability prerequisites may temporarily spend stock. */
  if(!a->intent.reference && depth==0) {
   const FPCommand *c=&a->intent.command;
   int reserve=0;
   const FPGoal *parent=&c->goals[a->intent.active];
   for(int j=0;j<c->count;j++)if(c->stage[j]==a->intent.stage && c->goals[j].kind==FP_GOAL_INVENTORY && c->goals[j].subject==i && !(parent->kind==FP_GOAL_INVENTORY && parent->subject==i) && c->goals[j].lo>reserve)reserve=c->goals[j].lo;
   quantity=imin(99,quantity+reserve);
  }
  if(o->inv[i]<quantity)return command_resource(a,o,cfg,s,i,quantity,depth+1);
 }
 int product=recipe->product;
 if(product<0){product=FP_E_HELMET;for(int i=0;i<4;i++)if(o->armour[i]<recipe->quantity){product=FP_E_HELMET+i;break;}}
 int quantity=recipe->quantity;
 if(recipe->product_kind==FP_GOAL_INVENTORY)quantity=imin(99,o->inv[product]+quantity);
 a->intent.subgoal=minimum_goal(recipe->product_kind,product,quantity);
 return craft_radius(a,o,cfg,s,action,a->intent.reference?250:INF-1);
}
static int command_adjacency(FPAgent *a,const FPObservation *o,FPScratch *s,const FPGoal *g) {
 const FPFloor *f=&a->floors[a->level];int best=-1,bestcost=INF;
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==g->subject) {
  int r=p/FP_SIDE-dr[g->direction],c=p%FP_SIDE-dc[g->direction];if(!inside(r,c))continue;
  int q=cell(r,c);if(s->dist[q]<bestcost){best=q;bestcost=s->dist[q];a->target=p;}
 }
 if(best<0 || bestcost>=INF)return -1;
 /* Adjacency is positional; it does not require facing or DO. */
 return move_path(a,o,s,best);
}
static int command_execute(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s,const FPGoal *g) {
 a->intent.subgoal=*g;
 if(g->kind==FP_GOAL_INVENTORY) {
  int value=o->inv[g->subject];
  if(value<g->lo)return command_resource(a,o,cfg,s,g->subject,g->lo,0);
  if(g->relation!=FP_AT_LEAST && value>g->hi) {
   if(g->subject==FP_I_STONE)return place(a,o,s,FP_PLACE_STONE);
   if(g->subject==FP_I_TORCH)return place(a,o,s,FP_PLACE_TORCH);
   if(g->subject==FP_I_SAPLING)return place(a,o,s,FP_PLACE_PLANT);
   if(g->subject==FP_I_WOOD) {
    if(a->intent.reference)return command_craft(a,o,cfg,s,FP_MAKE_TORCH,0);
    /* Arrow recipes consume one wood, and do not need scarce coal. Choose
     * a product with remaining capacity rather than issuing failed crafts. */
    if(value-g->hi>=2 && !o->inv[FP_I_STONE] && !o->inv[FP_I_COAL]) {
     int station=place(a,o,s,FP_PLACE_TABLE);if(station>=0)return station;
    }
    if(o->inv[FP_I_ARROW]<99)return command_craft(a,o,cfg,s,FP_MAKE_ARROW,1);
    if(o->inv[FP_I_TORCH]<99)return command_craft(a,o,cfg,s,FP_MAKE_TORCH,1);
    int drop=place(a,o,s,FP_PLACE_TORCH);if(drop>=0)return drop;
   }
   if(g->subject==FP_I_COAL) {
    if(o->inv[FP_I_TORCH]>=99 && !a->intent.reference)return place(a,o,s,FP_PLACE_TORCH);
    return command_craft(a,o,cfg,s,FP_MAKE_TORCH,1);
   }
   if(g->subject==FP_I_IRON)return command_craft(a,o,cfg,s,FP_IRON_ARMOUR,0);
   if(g->subject==FP_I_ARROW && o->bow)return FP_SHOOT;
  }
  return -1;
 }
 if(g->kind==FP_GOAL_EQUIPMENT) {
  static const int picks[]={0,FP_WOOD_PICK,FP_STONE_PICK,FP_IRON_PICK,FP_DIAMOND_PICK};
  static const int swords[]={0,FP_WOOD_SWORD,FP_STONE_SWORD,FP_IRON_SWORD,FP_DIAMOND_SWORD};
  if(goal_value(g,o)>g->hi && g->relation!=FP_AT_LEAST){a->intent.status=FP_CMD_BLOCKED;return -1;}
  int action=g->subject==FP_E_PICK?picks[g->lo]:g->subject==FP_E_SWORD?swords[g->lo]:g->lo==1?FP_IRON_ARMOUR:FP_DIAMOND_ARMOUR;
  return command_craft(a,o,cfg,s,action,0);
 }
 if(g->kind==FP_GOAL_ADJACENT) {
  int action=command_adjacency(a,o,s,g);
  if(action<0 && !a->intent.reference && (g->subject==FP_TABLE || g->subject==FP_FURNACE)) {
   if(o->inv[0]<2)return command_resource(a,o,cfg,s,0,2,0);
   if(g->subject==FP_FURNACE && o->inv[1]<1)return command_resource(a,o,cfg,s,1,1,0);
   return place(a,o,s,g->subject==FP_TABLE?FP_PLACE_TABLE:FP_PLACE_FURNACE);
  }
  if(action<0 && !a->intent.reference && !o->inv[FP_I_PICK] && o->inv[0]>=3)
   return command_craft(a,o,cfg,s,FP_WOOD_PICK,0);
  if(action<0 && !a->intent.reference)return command_known_floor(a,o,s,MASK(g->subject));
  return action;
 }
 if(g->kind==FP_GOAL_FLOOR) {
  if(o->level>g->lo)return travel(a,o,s,0);
  return -2; /* Reuse the autonomous preparation and floor-clearance planner. */
 }
 if(g->kind==FP_GOAL_PREPARE) {
  if(o->inv[FP_I_PICK]<2)return command_craft(a,o,cfg,s,FP_STONE_PICK,0);
  if(o->inv[FP_I_SWORD]<2)return command_craft(a,o,cfg,s,FP_STONE_SWORD,0);
  if(o->inv[0]<g->lo)return command_resource(a,o,cfg,s,0,g->lo,0);
 }
 if(o->drink<6)return seek_block(a,o,s,FP_WATER,100);
 if(o->food<6)return forage(a,o,s);
 return recover(a,o,s);
}
static int commanded(FPAgent *a,const FPObservation *o,const FPConfig *cfg,FPScratch *s) {
 FPIntent *in=&a->intent;in->claim=0;a->goal=-1;
 if(in->status==FP_CMD_COMPLETE)return emit(a,o,s,FP_NOOP,FP_RECOVER);
 const FPCommand *c=&in->command;int complete=1;
 for(int i=0;i<c->count;i++)if(c->stage[i]==in->stage && !fp_goal_satisfied(&c->goals[i],o))complete=0;
 if(complete) {
  in->stage++;in->active=-1;in->has_subgoal=0;in->bound_target=-1;in->transitions++;
  if(in->stage==c->stages){in->claim=1;in->status=FP_CMD_COMPLETE;return emit(a,o,s,FP_NOOP,FP_RECOVER);}
  /* Each stage consumes one observation boundary, including stages whose
   * predicates already hold. Never select an absent unsatisfied atom. */
  int pending=0;
  for(int i=0;i<c->count;i++)if(c->stage[i]==in->stage && !fp_goal_satisfied(&c->goals[i],o))pending=1;
  if(!pending)return emit(a,o,s,FP_NOOP,FP_CRAFT);
 }
 if(in->active<0 || fp_goal_satisfied(&c->goals[in->active],o) || in->reference) {
  for(int i=0;i<c->count;i++)if(c->stage[i]==in->stage && !fp_goal_satisfied(&c->goals[i],o)) {
   if(in->active!=i){in->transitions++;in->subgoal_started=a->t;in->has_subgoal=0;in->bound_target=-1;}
   in->active=i;break;
  }
 }
 if(c->goals[in->active].kind==FP_GOAL_FLOOR && o->level<c->goals[in->active].lo && !in->reference) {
  in->subgoal=c->goals[in->active];
  /* A commanded floor transition needs transport readiness, whereas the
   * autonomous planner also collects achievements and expedition equipment. */
  int action;
  if(o->clear && o->health>=6 && o->food>=4 && o->drink>=4 && o->energy>=4 &&
     o->inv[FP_I_PICK]>=2 && o->inv[FP_I_SWORD]>=2) {
   paths(a,o,cfg,s);action=travel(a,o,s,1);
   if(action<0)action=explore(a,o,cfg,s);
   action=emit(a,o,s,action,FP_TRAVEL);
  } else action=autonomous(a,o,cfg,s);
  int interrupted=a->skill==FP_FIGHT || a->skill==FP_DRINK || a->skill==FP_EAT || a->skill==FP_RECOVER;
  if(interrupted && !in->was_interrupted)in->interruptions++;
  if(!interrupted && in->was_interrupted)in->resumptions++;
  in->was_interrupted=interrupted;in->status=interrupted?FP_CMD_INTERRUPTED:FP_CMD_ACTIVE;
  return action;
 }
 int action=-1,skill=FP_CRAFT;
 if(o->sleeping || o->resting){action=FP_NOOP;skill=FP_RECOVER;}
 if(action<0)for(int i=0;i<6;i++)if(o->potions[i] && a->potion_effect[i]==0 && o->health<=6){action=FP_POTION_RED+i;skill=FP_RECOVER;break;}
 if(action<0){action=tactical(a,o,s);if(action>=0)skill=FP_FIGHT;}
 paths(a,o,cfg,s);
 if(!in->reference) {
  if(o->drink>=7+2*o->dex)a->drinking=0;
  if(o->drink<=cfg->drink_at)a->drinking=1;
 }
 if(action<0 && (in->reference?o->drink<=cfg->drink_at:a->drinking)) {
  action=seek_block(a,o,s,FP_WATER,100);skill=FP_DRINK;
  if(action<0 && !in->reference)action=command_known_floor(a,o,s,MASK(FP_WATER)|MASK(FP_FOUNTAIN));
 }
 if(action<0 && o->food<=cfg->eat_at) {
  action=forage(a,o,s);skill=FP_EAT;
  if(action<0 && !in->reference)action=command_known_floor(a,o,s,MASK(FP_RIPE)|MASK(FP_GRASS));
 }
 if(!in->reference && a->recovering && o->energy>=7+2*o->dex && o->health>=8+o->str-0.01f)a->recovering=0;
 if(action<0 && (o->energy<=cfg->sleep_at || o->health<=6 || (!in->reference && a->recovering))) {
  if(!in->reference)a->recovering=1;
  action=recover(a,o,s);skill=FP_RECOVER;
 }
 if(action<0 && !in->reference && o->xp && o->str<3){action=FP_LEVEL_STR;skill=FP_UPGRADE;}
 if(action>=0) {
  in->status=FP_CMD_INTERRUPTED;if(!in->was_interrupted)in->interruptions++;
  in->was_interrupted=1;
 } else {
  if(in->was_interrupted){in->resumptions++;in->was_interrupted=0;}
  in->status=FP_CMD_ACTIVE;
  in->executing=1;FPGoal previous_subgoal=in->subgoal;
  const FPGoal *task=&c->goals[in->active];
  if(!in->reference && in->has_subgoal && !fp_goal_satisfied(&previous_subgoal,o))task=&previous_subgoal;
  action=command_execute(a,o,cfg,s,task);in->executing=0;
  in->has_subgoal=action>=0;
  if(action<0)in->bound_target=-1;
  if(memcmp(&previous_subgoal,&in->subgoal,sizeof(FPGoal))){in->transitions++;in->subgoal_started=a->t;}
  if(action==-2)return autonomous(a,o,cfg,s);
  if(c->goals[in->active].kind==FP_GOAL_ADJACENT)skill=FP_TRAVEL;
  if(c->goals[in->active].kind==FP_GOAL_INVENTORY)skill=FP_GATHER;
  if(action<0){in->blocked_ticks++;action=explore(a,o,cfg,s);skill=FP_EXPLORE;}
 }
 int result=emit(a,o,s,action,skill);
 if(a->skill==FP_FIGHT && in->status!=FP_CMD_INTERRUPTED) {
  in->status=FP_CMD_INTERRUPTED;if(!in->was_interrupted)in->interruptions++;in->was_interrupted=1;
 }
 return result;
}
