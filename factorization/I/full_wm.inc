/* full_wm.inc - the policy's world model (WM).
 *
 * The controller is model-based: its decisions read what this module believes
 * about the hidden game state and what it predicts actions will cause. The
 * WM sees only public observations and the agent's own actions; the engine's
 * state and random keys are never available to it.
 *
 *   Rules and map access   game rules the agent assumes (walkable, solid,
 *                          mineable, station adjacency, placement)
 *   Damage model           armour, our blows and shots, enemy blows, slimeballs
 *   Belief queries         enemies near, firing lines, map bounds and features
 *   Predictions            incoming damage, melee threat at a cell, danger map,
 *                          greedy mob approach, next mob cell
 *   Belief update          wm_observe (perception, run once per observation),
 *                          wm_commit (efference copy and dead reckoning, once
 *                          per committed action), wm_attribute_damage
 *
 * Belief fields of FPAgent written only here: floors[] (map, items, stations,
 * lit, visits, ladders, seen, clear, entered, steps, known bounds), previous,
 * previous_action/block/mob/type/target, achieved[], r, c, observed_r/c,
 * level, t, arrows[], arrow_count, projectile_types[], shot_*, enemies[],
 * enemy_count, potion_effect[], boss_hits, plant_cell, plant_tick,
 * farm_attempts, unseen_hit, troll_slime_t. The policy reads them and never
 * writes them; every other FPAgent field is controller state.
 *
 * Mob-projectile tracks (wm-combat 2026-10-17): arrow_count tracks, track j
 * at cell arrows[j] (local frame). Read their attributes ONLY through the
 * queries wm_shot_code(a,j) (public projectile code 1..8: engine type + 1),
 * wm_shot_heading(a,j) (FP_LEFT..FP_DOWN, 0 when unknown) and
 * wm_shot_unseen(a,j) (observations since it was last seen; > 0 means it is
 * dead-reckoned through darkness or out of view); wm_shot_count(a) and
 * wm_shot_cell(a,j) equal arrow_count and arrows[j]. projectile_types[] holds
 * a WM-private encoding (code | heading<<8 | unseen<<12): never read it
 * directly.
 *
 * Projectile danger map s->arrow_next (rebuilt by every wm_observe). Nonzero
 * at cell q when the WM predicts that a mob projectile hits a player standing
 * on q after the next move:
 *   - the cell of every mob-projectile track and the next cell along its
 *     heading (engine: a projectile at P heading D hits a player on P or P+D);
 *   - a visible projectile of unknown heading on our row or column within two
 *     cells is taken to fly at us; one diagonally beside us marks both cells
 *     it shares with us;
 *   - with WM_PB 1, every cell beside a ranged enemy believed ready
 *     (cooldown <= 0) where wm_point_blank_p >= WM_PB_MIN: its shot would hit
 *     a player there in the same tick and is never visible. Such cells are
 *     not projectile positions: use wm_shot_heading for headings and
 *     wm_point_blank_max(a,s,r,c) > 0 to tell point-blank cells apart.
 * The value is 1, or the most damaging projectile code there in WM_SHOT_DMG
 * variants; callers must treat it as a flag.
 *
 * Ranged-mob model queries (see "Ranged mobs and their projectiles"):
 * wm_ranged_projectile(type), wm_projectile_hit(o,code),
 * wm_ranged_can_enter(a,s,type,r,c), wm_point_blank_p(a,s,e,r,c),
 * wm_point_blank_max(a,s,r,c), wm_point_blank_damage(a,o,s,r,c),
 * wm_fire_direction_ok(mr,mc,r,c,d). enemies[].cooldown of a ranged enemy is
 * its inferred attack cooldown (4 after it fires, -1 per tick).
 *
 * Contract for edits: the policy calls only the functions in this file and
 * reads belief fields; the WM never reads controller state (a policy input such
 * as wm_risk's `shooters` is an explicit argument). The FPAgent layout is
 * shared with saved banks: new fields may only be appended.
 *
 * Evaluator hook (never set by the policy): after each wm_observe, the
 * evaluator may overwrite beliefs with engine truth to measure what better
 * beliefs are worth (information oracles). NULL in every normal build. */
void (*fp_wm_oracle)(FPAgent *a,const FPObservation *o,FPScratch *s,void *ctx);
void *fp_wm_oracle_ctx;
/* Point-blank shots of ready ranged mobs beside us (wm-combat): 0 exposed
 * only through wm_point_blank_p/_damage, 1 also as projectile cells
 * (arrow_next) when at least WM_PB_MIN likely, 2 also as expected damage in
 * wm_incoming_damage. */
#ifndef WM_PB
#define WM_PB 1
#endif
#ifndef WM_PB_MIN
#define WM_PB_MIN 0.3f
#endif

/* ==== Rules and map access ==== */
static int block(const FPAgent *a,int r,int c) {
 return inside(r,c)?a->floors[a->level].map[cell(r,c)]:FP_BOUNDARY;
}
/* Block sets as 64-bit masks over block codes (all codes are below 64; any
 * other value, UNKNOWN included, is in neither mask). A single bit test keeps
 * these predicates small enough to inline at every call. */
#define WM_BIT(b) (UINT64_C(1)<<(b))
#define WM_SOLID (WM_BIT(FP_STONE)|WM_BIT(FP_TREE)|WM_BIT(FP_COAL)|WM_BIT(FP_IRON)|WM_BIT(FP_DIAMOND)| \
 WM_BIT(FP_TABLE)|WM_BIT(FP_FURNACE)|WM_BIT(FP_PLANT)|WM_BIT(FP_RIPE)|WM_BIT(FP_WALL)| \
 WM_BIT(FP_MOSS)|WM_BIT(FP_STALAGMITE)|WM_BIT(FP_SAPPHIRE)|WM_BIT(FP_RUBY)|WM_BIT(FP_CHEST)| \
 WM_BIT(FP_FOUNTAIN)|WM_BIT(FP_FIRE_TREE)|WM_BIT(FP_FIRE_TABLE)|WM_BIT(FP_ICE_TABLE)| \
 WM_BIT(FP_BOSS)|WM_BIT(FP_GRAVE)|WM_BIT(FP_GRAVE2)|WM_BIT(FP_GRAVE3))
/* Not walkable: solid, invalid, off-map, water and lava (and UNKNOWN, below). */
#define WM_UNWALKABLE (WM_SOLID|WM_BIT(FP_INVALID)|WM_BIT(FP_BOUNDARY)|WM_BIT(FP_WATER)|WM_BIT(FP_LAVA))
static int solid(int b) { return (unsigned)b<64 && ((WM_SOLID>>b)&1); }
static int walkable(int b) { return (unsigned)b<64?!((WM_UNWALKABLE>>b)&1):b!=UNKNOWN; }
/* A direction is also a movement attempt: UNKNOWN is not a known wall.
 * Never choose one when terrain cannot tell us whether the player moves.
 * Remembered ground and known blocked cells still permit ordinary travel
 * and turning; this does not require the destination to be lit now. */
static int known_direction(const FPAgent *a,int d) {
 int b=block(a,a->r+dr[d],a->c+dc[d]);
 return b!=UNKNOWN && b!=FP_INVALID;
}
static int mineable(const FPObservation *o,int b) {
 if(b==FP_TREE || b==FP_FIRE_TREE || b==FP_ICE_SHRUB || b==FP_CHEST || b==FP_TABLE || b==FP_FURNACE) return 1;
 if(b==FP_STONE || b==FP_COAL || b==FP_STALAGMITE) return o->inv[FP_I_PICK]>=1;
 if(b==FP_IRON) return o->inv[FP_I_PICK]>=2;
 if(b==FP_DIAMOND) return o->inv[FP_I_PICK]>=3;
 return (b==FP_RUBY || b==FP_SAPPHIRE) && o->inv[FP_I_PICK]>=4;
}
static int near(const FPAgent *a,int p,int furnace) {
 int bits=a->floors[a->level].stations[p]; return (bits&1) && (!furnace || (bits&2));
}
/* Word-at-a-time scans of byte maps (8 cells per test; the tests are exact
 * booleans and only skip cells that the scalar test would reject). */
static uint64_t load8(const uint8_t *p) { uint64_t w;memcpy(&w,p,8);return w; }
static int has_zero_byte(uint64_t w) {
 return ((w-UINT64_C(0x0101010101010101))&~w&UINT64_C(0x8080808080808080))!=0;
}
static int has_byte(uint64_t w,int b) { return has_zero_byte(w^(UINT64_C(0x0101010101010101)*(uint8_t)b)); }
/* First cell in [p,hi] beside a table or furnace (nonzero station bits), else
 * hi+1. Such cells are rare: test eight at a time. */
static int next_station(const FPFloor *f,int p,int hi) {
 for(;p+7<=hi && !load8(f->stations+p);p+=8);
 while(p<=hi && !f->stations[p])p++;
 return p;
}
/* Ground that takes a torch, as a block set (WM_BIT). */
#define WM_TORCH_GROUND (WM_BIT(FP_GRASS)|WM_BIT(FP_PATH)|WM_BIT(FP_SAND)|WM_BIT(FP_FIRE_GRASS)|WM_BIT(FP_ICE_GRASS))
static int torch_ground(int b) { return b==FP_GRASS||b==FP_PATH||b==FP_SAND||b==FP_FIRE_GRASS||b==FP_ICE_GRASS; }
static int place_valid(const FPAgent *a,FPScratch *s,int p,int action) {
 const FPFloor *f=&a->floors[a->level];int b=f->map[p];if(mobs_of(s)[p] || f->marks[p].item)return 0;
 if(action==FP_PLACE_PLANT)return b==FP_GRASS;
 if(action==FP_PLACE_TORCH)return torch_ground(b);
 return walkable(b) || (action==FP_PLACE_STONE && (b==FP_WATER || b==FP_LAVA));
}
/* place_valid(a,s,p,FP_PLACE_TORCH), testing the ground first: floor scans
 * reject most cells on it alone. */
static int torch_valid(const FPAgent *a,const FPScratch *s,int p) {
 const FPFloor *f=&a->floors[a->level];
 return torch_ground(f->map[p]) && !mobs_of(s)[p] && !f->marks[p].item;
}
/* ==== Damage model ==== */
static int wm_armour(const FPObservation *o) {
 int n=0;for(int i=0;i<4;i++)n+=o->armour[i];return n;
}
static float wm_sword_damage(const FPObservation *o) {
 static const int base[5]={1,2,3,5,8};
 return base[imin(o->inv[FP_I_SWORD],4)]*(1+0.25f*(o->str-1));
}
static float wm_hit_damage(const FPObservation *o,int type,int ranged) {
 float damage=wm_sword_damage(o);
 if(type==5)damage*=0.5f;if(type==6 && !ranged)damage*=0.8f;if(type>=7)damage*=0.1f;
 if(o->sword_enchant && !(type==7 && o->sword_enchant==1) && !(type==8 && o->sword_enchant==2)) {
  static const float base[5]={1,2,3,5,8};
  damage+=base[imin(o->inv[FP_I_SWORD],4)]*0.5f*(1+0.05f*(o->intelligence-1));
 }
 return damage;
}
/* Public projectile codes are the engine type plus one. */
static float wm_projectile_damage(const FPObservation *o,int projectile,int type,int ranged) {
 if(projectile==3 || projectile==4) {
  if((projectile==3 && type==7) || (projectile==4 && type==8))return 0;
  return 3*(1+0.5f*(o->intelligence-1));
 }
 float physical=5*(1+0.2f*(o->dex-1));
 float damage=physical*(type==5?0.5f:type==6 && !ranged?0.8f:type>=7?0.1f:1);
 if(o->bow_enchant && !(type==7 && o->bow_enchant==1) && !(type==8 && o->bow_enchant==2))damage+=physical*0.5f;
 return damage;
}
static float wm_attack_damage(const FPObservation *o,int type) {
 static const float physical[8]={2,4,3,5,6,6,3,4};
 float damage=physical[type-1]*(1-0.1f*wm_armour(o));
 int fire=0,ice=0;for(int i=0;i<4;i++){fire+=o->armour_enchant[i]==1;ice+=o->armour_enchant[i]==2;}
 if(type==6)damage+=1-0.2f*fire+1-0.2f*ice;
 if(type==7)damage+=5*(1-0.2f*fire);
 if(type==8)damage+=5*(1-0.2f*ice);
 return damage*(o->level==8?1.5f:1);
}
/* ==== Belief queries ==== */
static int wm_melee_within(const FPAgent *a,int radius) {
 for(int i=0;i<a->enemy_count;i++)if(!a->enemies[i].kind && abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=radius)return 1;
 return 0;
}
static int wm_hostile_within(const FPAgent *a,int radius) {
 for(int i=0;i<a->enemy_count;i++)if(abs(a->enemies[i].p/FP_SIDE-a->r)+abs(a->enemies[i].p%FP_SIDE-a->c)<=radius)return 1;
 return 0;
}
/* Ranged mobs fire only along their larger axis, from four or five cells,
 * and the shot flies straight until a solid block. Off-axis cells are safe;
 * on-axis cells with a clear line are where fireballs, daggers and Vault
 * arrows land (floors 1-4: 2,600 of 6,600 failures before the Troll Mines). */
static int wm_firing_line(const FPAgent *a,int r,int c,int er,int ec) {
 int dr_=er-r,dc_=ec-c,d=abs(dr_)+abs(dc_);
 if((dr_ && dc_) || d<2 || d>6)return 0;
 int sr=dr_>0?1:dr_<0?-1:0,sc=dc_>0?1:dc_<0?-1:0;
 for(int k=1;k<d;k++)if(solid(block(a,r+k*sr,c+k*sc)))return 0;
 return 1;
}
/* Every floor is a 48x48 map. Fire is fully lit, so every in-view cell is
 * observed and off-map cells are remembered as BOUNDARY. The span of known
 * in-map cells bounds the window; an in-map cell beside a boundary cell fixes
 * that edge, and with it the opposite one. Unknown cells outside the window
 * cannot hold the exit, yet they kept drawing searches to the map's rim. */
static void fire_bounds(const FPFloor *f,int *top,int *bottom,int *left,int *right) {
 int rmin=FP_SIDE,rmax=-1,cmin=FP_SIDE,cmax=-1,t=-1,b=FP_SIDE,l=-1,rt=FP_SIDE;
 for(int p=f->known_lo;p<=f->known_hi;p++) {
  int m=f->map[p];if(m==UNKNOWN||m==FP_BOUNDARY||m==FP_INVALID)continue;
  int r=p/FP_SIDE,c=p%FP_SIDE;
  if(r<rmin)rmin=r;if(r>rmax)rmax=r;if(c<cmin)cmin=c;if(c>cmax)cmax=c;
  if(r>0 && f->map[p-FP_SIDE]==FP_BOUNDARY && r>t)t=r;
  if(r<FP_SIDE-1 && f->map[p+FP_SIDE]==FP_BOUNDARY && r<b)b=r;
  if(c>0 && f->map[p-1]==FP_BOUNDARY && c>l)l=c;
  if(c<FP_SIDE-1 && f->map[p+1]==FP_BOUNDARY && c<rt)rt=c;
 }
 *top=0;*bottom=FP_SIDE-1;*left=0;*right=FP_SIDE-1;
 if(rmax<0)return;
 int T=rmax-47,B=rmin+47,L=cmax-47,R=cmin+47;
 if(t>=0 && t>T)T=t;if(b<FP_SIDE && b<B)B=b;if(l>=0 && l>L)L=l;if(rt<FP_SIDE && rt<R)R=rt;
 if(T+47<B)B=T+47;if(B-47>T)T=B-47;if(L+47<R)R=L+47;if(R-47>L)L=R-47;
 *top=T>0?T:0;*bottom=B<FP_SIDE-1?B:FP_SIDE-1;*left=L>0?L:0;*right=R<FP_SIDE-1?R:FP_SIDE-1;
}
/* Capped Manhattan distance to observed water, min(40, |dr|+|dc| to the
 * nearest water or fountain). Recovery and exploration use this scratch
 * array at different times. Linear sweeps avoid a separate scan of every
 * water tile for each candidate room. The distance separates by axis:
 * sweeps down and up the columns give the distance within each column, then
 * sweeps along the rows the distance over columns. Each sweep advances a
 * whole row (or column) of independent cells at a time. */
static void water_distances(const FPFloor *f,FPScratch *s) {
 for(int r=0;r<FP_SIDE;r++)for(int c=0;c<FP_SIDE;c++) {
  int b=f->map[cell(r,c)],d=(b==FP_WATER || b==FP_FOUNTAIN)?0:40;
  s->prefix[r][c]=r?imin(d,s->prefix[r-1][c]+1):d;
 }
 for(int r=FP_SIDE-2;r>=0;r--)for(int c=0;c<FP_SIDE;c++)s->prefix[r][c]=imin(s->prefix[r][c],s->prefix[r+1][c]+1);
 for(int c=1;c<FP_SIDE;c++)for(int r=0;r<FP_SIDE;r++)s->prefix[r][c]=imin(s->prefix[r][c],s->prefix[r][c-1]+1);
 for(int c=FP_SIDE-2;c>=0;c--)for(int r=0;r<FP_SIDE;r++)s->prefix[r][c]=imin(s->prefix[r][c],s->prefix[r][c+1]+1);
}
/* Unseen cells a torch on (r,c) would light: the UNKNOWN cells of the disk
 * rr*rr+cc*cc<=22, whose row r+rr spans |cc|<=floor(sqrt(22-rr*rr)), that
 * is 2,3,4,4,4,4,4,3,2 for rr=-4..4. unseen_rows() fills
 * s->unseen.row[R][k] = UNKNOWN cells among columns 0..k-1 of row R for the
 * rows a scan of [lo,hi] can touch; a disk row is then one difference. */
/* The same pass marks in s->unseen.cand every cell of the rows of [lo,hi]
 * whose block is in the set `want` (bit b for block code b < 64), and
 * returns the number of leading words of marks: the scans then visit only
 * those candidates, in increasing order (SetBits), and still bound them by
 * [lo,hi]. */
static int unseen_rows(const FPFloor *f,FPScratch *s,int lo,int hi,uint64_t want) {
 if(hi<lo)return 0;
 int r0=lo/FP_SIDE-4,r1=hi/FP_SIDE+4,words=want?((hi/FP_SIDE+1)*FP_SIDE+63)/64:0;if(r0<0)r0=0;if(r1>FP_SIDE-1)r1=FP_SIDE-1;
 CellBits cand{s->unseen.cand};if(want)cand.clear();
 for(int r=r0;r<=r1;r++) {
  const uint8_t *m=f->map+r*FP_SIDE;uint8_t *u=s->unseen.row[r];int n=0;u[0]=0;
  if(want && r>=lo/FP_SIDE && r<=hi/FP_SIDE) {
   /* The row's marks gather in registers (columns 0-63, 64-95), then join
    * the words at the row's bit offset, 0 or 32 (96 = 1.5 words). */
   auto hit=[want](int b){return (uint64_t)((b<64)&(int)((want>>(b&63))&1));};
   uint64_t m0=0,m1=0;
   for(int c=0;c<64;c++){int b=m[c];n+=b==UNKNOWN;u[c+1]=(uint8_t)n;m0|=hit(b)<<c;}
   for(int c=64;c<FP_SIDE;c++){int b=m[c];n+=b==UNKNOWN;u[c+1]=(uint8_t)n;m1|=hit(b)<<(c-64);}
   int w=r*FP_SIDE>>6;uint64_t *cw=s->unseen.cand;
   if(!(r*FP_SIDE&63)){cw[w]|=m0;cw[w+1]|=m1;}else{cw[w]|=m0<<32;cw[w+1]|=m0>>32|m1<<32;}
  }
  else for(int c=0;c<FP_SIDE;c++){n+=m[c]==UNKNOWN;u[c+1]=(uint8_t)n;}
 }
 return words;
}
static int unseen_gain(const FPScratch *s,int r,int c) {
 static const int half[9]={2,3,4,4,4,4,4,3,2};
 int gain=0;
 if(r>=4 && r<FP_SIDE-4 && c>=4 && c<FP_SIDE-4) { /* the disk lies inside the atlas */
  for(int k=0;k<9;k++)gain+=s->unseen.row[r+k-4][c+half[k]+1]-s->unseen.row[r+k-4][c-half[k]];
  return gain;
 }
 for(int k=0;k<9;k++) {
  int R=r+k-4;if(R<0 || R>=FP_SIDE)continue;
  int c0=c-half[k],c1=c+half[k]+1;if(c0<0)c0=0;if(c1>FP_SIDE)c1=FP_SIDE;
  gain+=s->unseen.row[R][c1]-s->unseen.row[R][c0];
 }
 return gain;
}
/* The Vault-side enchanting trip only seeks a remembered fire table. When
 * none is known, the climb returns at once and repeats indefinitely. */
static int fire_table_known(const FPAgent *a) {
 const FPFloor *f=&a->floors[4];
 for(int p=f->known_lo;p<=f->known_hi;p++)if(f->map[p]==FP_FIRE_TABLE)return 1;
 return 0;
}
/* Trees in Overworld memory. Felled trees become grass: a world has ~80, and
 * the shallow floors used to fell 52 of them before the Sewers. */
static int known_trees(const FPAgent *a) {
 const FPFloor *f=&a->floors[0];int n=0;
 for(int p=f->known_lo;p<=f->known_hi;p++)n+=f->map[p]==FP_TREE;
 return n;
}
/* ---- Ranged mobs and their projectiles (engine: update_mobs move_ranged,
 * move_mob_projectile; wm-combat 2026-10-17) ----
 * A ranged mob at M with attack cooldown <= 0 fires when the player, after
 * its own move, is 4-5 cells away, or within 3 cells when the mob's proposed
 * step is invalid (proposal: the step away from the player with p .15, else
 * one of four random steps). It then stays at M, its cooldown becomes 4 (else
 * it decrements by 1), and a projectile spawns at M heading along the larger
 * offset towards the player (ties at random). The projectile moves in the same
 * tick: a mob beside the player hits it at once (point blank) and is never
 * seen. Every later tick a projectile at P heading D hits a player standing
 * (after the move) on P or P+D, and stops at solid blocks, mobs and the map
 * edge. At most three mob projectiles exist per floor.
 * Public projectile codes are the engine type plus one; ranged type codes
 * 1..8 fire projectile codes 1,1,3,2,5,6,7,8. */
static int wm_ranged_projectile(int type) {
 static const int code[9]={0,1,1,3,2,5,6,7,8};return type>=1 && type<=8?code[type]:0;
}
/* Damage a mob projectile (public code) does to us: physical, fire and ice
 * parts reduced by armour and the matching enchantments. */
static float wm_projectile_hit(const FPObservation *o,int code) {
 static const float v[9][3]={{0,0,0},{2,0,0},{4,0,0},{0,3,0},{0,0,3},{5,0,0},{4,3,3},{3,5,0},{4,0,5}};
 if(code<1 || code>8)code=6;
 int fe=0,ie=0;for(int i=0;i<4;i++){fe+=o->armour_enchant[i]==1;ie+=o->armour_enchant[i]==2;}
 float d=v[code][0]*(1-0.1f*wm_armour(o))+v[code][1]*(1-0.2f*fe)+v[code][2]*(1-0.2f*ie);
 return d*(o->level==8?1.5f:1);
}
/* Could a ranged mob of this type step into cell (r,c)? Unknown cells count
 * as open. Deep things live in water; elementals cross water and lava. */
static int wm_ranged_can_enter(const FPAgent *a,const FPScratch *s,int type,int r,int c) {
 if(!inside(r,c))return 0;
 int b=block(a,r,c);if(b==FP_BOUNDARY || b==FP_INVALID)return 0;
 if(b==UNKNOWN)return 1;
 if(solid(b) || mobs_of(s)[cell(r,c)])return 0;
 if(type==6)return b==FP_WATER;
 if(type<=5)return b!=FP_WATER && b!=FP_LAVA;
 return 1;
}
/* Probability that a ranged enemy believed ready fires point blank at a
 * player standing on (r,c) beside it: its proposed step must be invalid, and
 * the step onto the player always is. 0 when not adjacent or cooling down. */
static float wm_point_blank_p(const FPAgent *a,const FPScratch *s,FPEnemy e,int r,int c) {
 int mr=e.p/FP_SIDE,mc=e.p%FP_SIDE;
 if(!e.kind || e.cooldown>0 || abs(mr-r)+abs(mc-c)!=1)return 0;
 int bad=0;
 for(int d=1;d<=4;d++){int rr=mr+dr[d],cc=mc+dc[d];if((rr==r && cc==c) || !wm_ranged_can_enter(a,s,e.type,rr,cc))bad++;}
 int away=!wm_ranged_can_enter(a,s,e.type,2*mr-r,2*mc-c) || (2*mr-r==r && 2*mc-c==c);
 return 0.15f*away+0.85f*bad/4.0f;
}
/* Expected point-blank damage at (r,c) from ranged enemies believed ready. */
static float wm_point_blank_damage(const FPAgent *a,const FPObservation *o,const FPScratch *s,int r,int c) {
 float d=0;
 for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];float p=wm_point_blank_p(a,s,e,r,c);if(p>0)d+=p*wm_projectile_hit(o,wm_ranged_projectile(e.type));}
 return d;
}
/* Mob-projectile track queries: the supported way to read arrows[] and
 * projectile_types[] (see the header). */
static int wm_shot_count(const FPAgent *a) { return a->arrow_count; }
static int wm_shot_cell(const FPAgent *a,int j) { return a->arrows[j]; }
static int wm_shot_code(const FPAgent *a,int j) { return a->projectile_types[j]&255; }
static int wm_shot_heading(const FPAgent *a,int j) { int d=(a->projectile_types[j]>>8)&7;return d<=4?d:0; }
/* Is direction d the one a mob at (mr,mc) would fire at a player on (r,c)? */
static int wm_fire_direction_ok(int mr,int mc,int r,int c,int d) {
 int vr=r-mr,vc=c-mc,ar=abs(vr),ac=abs(vc);
 if(dr[d])return ar>=ac && ar>0 && (vr>0?1:-1)==dr[d];
 return ac>=ar && ac>0 && (vc>0?1:-1)==dc[d];
}
/* ==== Predictions ==== */
static float wm_incoming_damage(const FPAgent *a,const FPObservation *o,FPScratch *s,int action) {
 int r=a->r,c=a->c;
 if(action>=FP_LEFT && action<=FP_DOWN) {
  int rr=r+dr[action],cc=c+dc[action];
  if(inside(rr,cc) && walkable(block(a,rr,cc)) && !mobs_of(s)[cell(rr,cc)]){r=rr;c=cc;}
 }
 int faced=cell(a->r+dr[o->dir],a->c+dc[o->dir]);float damage=0;
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];
  if(e.kind || e.cooldown>0 || abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)!=1)continue;
  if(action==FP_DO && e.p==faced && wm_hit_damage(o,e.type,e.kind)>=e.health)continue;
  damage+=wm_attack_damage(o,e.type);
 }
 if(arrows_of(s)[cell(r,c)])damage+=8;
#if WM_PB==2
 else damage+=wm_point_blank_damage(a,o,s,r,c);
#endif
 return damage;
}
static int wm_risk(const FPAgent *a,const FPObservation *o,FPScratch *s,int p,int shooters) {
 int r=p/FP_SIDE,c=p%FP_SIDE,n=0,weak=wm_sword_damage(o)<5?2:1;
 /* The program prices ranged mobs by phase (queries): firing lines early on,
  * deep-thing lines while guarding in the Troll Mines, elemental lines on Fire. */
 int early=tr_query(a,o,s,TRQ_WM_EARLY),guard=tr_query(a,o,s,TRQ_WM_SHOOTER_GUARD),fline=tr_query(a,o,s,TRQ_WM_FIRE_LINE);
 /* Perception already collected every visible hostile (at most five in
  * Full). Pathfinding calls this repeatedly; skip the other view cells. */
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];int d=abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c);
  if(!e.kind)n+=(d<=1?8:d==2?3:0)*weak;
  if(e.kind && early){if(wm_firing_line(a,r,c,e.p/FP_SIDE,e.p%FP_SIDE))n+=12;else if(d>=3 && d<=5)n+=1;}
  else if(e.kind && d>=3 && d<=5)n+=3;
  /* Deep things shoot along a row or column at range 4-5 (or closer when
   * cornered in their pool); a slimeball takes 7-8 of 12 health. Price
   * their lines. Not while drinking: costs that move with the
   * shooters made thirsty agents dither beside the pool until they died. */
  if(e.kind && guard && shooters && d<=5)n+=(e.p/FP_SIDE==r || e.p%FP_SIDE==c)?20:8;
  /* Icetrip on Fire: fireballs are now the main excursion killer (495 of
   * 1,417 deaths with plates). An elemental shoots along the axis towards
   * us: only cells in its row or column with a clear line are hit, and
   * cornered ones fire point-blank. Price those cells like deep-thing lines. */
  if(e.kind && fline && d<=6 && (d<=1 || wm_firing_line(a,r,c,e.p/FP_SIDE,e.p%FP_SIDE)))n+=20;
 }
 if(arrows_of(s)[p])n+=100;return n;
}
/* Damage this step from adjacent melee enemies that are ready to strike, if
 * we strike the faced cell: an enemy that our blow kills does not strike. */
static float wm_melee_incoming(const FPAgent *a,const FPObservation *o) {
 float incoming=0;int faced=cell(a->r+dr[o->dir],a->c+dc[o->dir]);
 for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];if(e.kind || e.cooldown>0 || abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)!=1)continue;
  if(e.p==faced && wm_hit_damage(o,e.type,e.kind)>=e.health)continue;
  incoming+=wm_attack_damage(o,e.type);
 }
 return incoming;
}
/* Melee damage awaiting us on cell (r,c): ready enemies adjacent to it. */
static float wm_melee_threat_at(const FPAgent *a,const FPObservation *o,int r,int c) {
 float cost=0;for(int i=0;i<a->enemy_count;i++) {
  FPEnemy e=a->enemies[i];if(!e.kind && e.cooldown<=0 && abs(e.p/FP_SIDE-r)+abs(e.p%FP_SIDE-c)==1)cost+=wm_attack_damage(o,e.type);
 }
 return cost;
}
/* Greedy approach of a melee enemy: it closes its larger offset first, else
 * the other, and stops when both are blocked (by terrain, and by other mobs
 * when avoid_mobs). Returns whether it ends beside us; its final offset from
 * us goes to (*fr,*fc) when given. */
static int wm_greedy_approach(const FPAgent *a,const FPScratch *s,FPEnemy e,int avoid_mobs,int *fr,int *fc) {
 int r=e.p/FP_SIDE-a->r,c=e.p%FP_SIDE-a->c,clear=1;
 while(clear && abs(r)+abs(c)>1) {
  int moved=0;
  if(abs(r)>=abs(c)){int nr=r-(r>0?1:-1);if(walkable(block(a,a->r+nr,a->c+c)) && (!avoid_mobs || !mobs_of(s)[cell(a->r+nr,a->c+c)])){r=nr;moved=1;}}
  if(!moved && abs(c)>=abs(r)){int nc=c-(c>0?1:-1);if(walkable(block(a,a->r+r,a->c+nc)) && (!avoid_mobs || !mobs_of(s)[cell(a->r+r,a->c+nc)])){c=nc;moved=1;}}
  clear=moved;
 }
 if(fr)*fr=r;
 if(fc)*fc=c;
 return clear;
}
/* One greedy step of a mob at offset (vr,vc) along axis 0 (rows) or 1
 * (columns), if that axis is a step it may take: its absolute next cell. */
static int wm_greedy_next(const FPAgent *a,int vr,int vc,int axis,int *nr,int *nc) {
 if(axis==0 && (abs(vr)<abs(vc) || !vr))return 0;
 if(axis==1 && (abs(vc)<abs(vr) || !vc))return 0;
 *nr=a->r+vr-(axis==0?(vr>0?1:-1):0);*nc=a->c+vc-(axis==1?(vc>0?1:-1):0);
 return 1;
}
/* A deep thing's slimeball: 4 physical, 3 fire and 3 ice, each reduced by
 * armour and the matching enchantments. */
static float wm_slimeball_damage(const FPObservation *o) {
 int fe=0,ie=0;for(int i=0;i<4;i++){fe+=o->armour_enchant[i]==1;ie+=o->armour_enchant[i]==2;}
 return 4*(1-0.1f*wm_armour(o))+3*(1-0.2f*fe)+3*(1-0.2f*ie);
}
/* ==== Belief update ==== */

/* Initial beliefs: nothing observed yet; the Overworld's documented centre
 * spawn fixes its frame, so cells outside its 48x48 map are boundary. */
static void wm_reset(FPAgent *a) {
 for(int l=0;l<FP_LEVELS;l++) {
  memset(a->floors[l].map,UNKNOWN,FP_CELLS);
  a->floors[l].up=a->floors[l].down=-1;
  a->floors[l].known_lo=FP_CELLS;a->floors[l].known_hi=-1;
 }
 for(int i=0;i<6;i++)a->potion_effect[i]=-1;
 a->r=a->c=FP_SIDE/2;a->floors[0].seen=1;
 a->plant_cell=-1;a->unseen_hit=-100;
 for(int r=0;r<FP_SIDE;r++)for(int c=0;c<FP_SIDE;c++)
  if(r<24||r>=72||c<24||c>=72)a->floors[0].map[cell(r,c)]=FP_BOUNDARY;
}
/* Cold start from an arbitrary observation: no assumption about the spawn. */
static void wm_reset_cold(FPAgent *a,const FPObservation *o) {
 memset(a->floors[0].map,UNKNOWN,FP_CELLS);a->floors[0].seen=0;
 a->level=o->level;a->floors[o->level].seen=1;
}
/* Efference copy and dead reckoning: remember the committed action and what
 * it faced, and move the position estimate when the move must succeed on
 * known terrain. Advances the agent's clock. */
static void wm_commit(FPAgent *a,const FPObservation *o,FPScratch *s,int action) {
 a->previous=*o;a->previous_action=action;
 int r=a->r+dr[o->dir],c=a->c+dc[o->dir],q=inside(r,c)?cell(r,c):-1;
 a->previous_block=block(a,r,c);a->previous_mob=0;a->previous_type=0;
 a->previous_target=q;
 if(q>=0) {
  if(melee_of(s)[q]){a->previous_mob=1;a->previous_type=melee_of(s)[q];}
  else if(passive_of(s)[q]){a->previous_mob=2;a->previous_type=passive_of(s)[q];}
  else if(ranged_of(s)[q]){a->previous_mob=3;a->previous_type=ranged_of(s)[q];}
 }
 if(!o->sleeping && !o->resting && action>=1 && action<=4) {
  r=a->r+dr[action];c=a->c+dc[action];
  if(inside(r,c) && walkable(block(a,r,c)) && !mobs_of(s)[cell(r,c)]){a->r=r;a->c=c;}
 }
 a->t++;
}
/* Attribute a health loss of more than 1.5 that no visible adjacent enemy
 * explains: an unseen attacker (unseen_hit), or, in the troll phase, a
 * slimeball from a deep thing in a pool along our row or column
 * (troll_slime_t). Torching and striking the faced cell kept agents in the
 * firing line (39 slimeball hits per 1,000 such steps). */
static void wm_attribute_damage(FPAgent *a,const FPObservation *o) {
 if(!(a->t && a->previous.level==a->level && o->health<a->previous.health-1.5f))return;
 int seen=0;
 for(int i=0;i<a->enemy_count;i++){FPEnemy e=a->enemies[i];if(!e.kind && abs(e.p/FP_SIDE-a->r)+abs(e.p%FP_SIDE-a->c)==1)seen=1;}
 if(tr_query(a,o,NULL,TRQ_WM_SLIME_ATTRIB)) {
  float loss=a->previous.health-o->health,slime=wm_slimeball_damage(o);
  if(fabsf(loss-slime)<0.6f && fabsf(loss-wm_attack_damage(o,6))>0.6f){seen=1;a->troll_slime_t=a->t;}
 }
 if(!seen)a->unseen_hit=a->t;
}
/* Enemy identity is public only through motion/type. Enumerate globally
 * consistent one-step assignments; carry HP/cooldown only when every maximum
 * matching agrees. A greedy first neighbour can consume the sole predecessor
 * of another visible mob, swapping wounded and fresh identities. */
typedef struct {
 const FPAgent *a;FPEnemy now[8];int n,best,chosen[8];unsigned short possible[8];
} WMEnemyMatch;
static int wm_match_compatible(const FPAgent *a,FPEnemy now,FPEnemy old) {
 if(now.kind!=old.kind || now.type!=old.type)return 0;
 if(abs(now.p/FP_SIDE-old.p/FP_SIDE)+abs(now.p%FP_SIDE-old.p%FP_SIDE)>1)return 0;
 if(a->previous_action==FP_DO && a->previous_target==old.p &&
    !a->previous.sleeping && !a->previous.resting && wm_hit_damage(&a->previous,old.type,old.kind)>=old.health)return 0;
 return 1;
}
/* Sequential occupancy cannot realize a direct swap or closed rotation:
 * every proposed destination is still occupied until one member moves, and
 * no member can be first. Do not mistake these impossible permutations for
 * genuine identity uncertainty when adjacent mobs simply stayed put. */
static int wm_match_cycle(const WMEnemyMatch *m) {
 int next[8];for(int j=0;j<8;j++)next[j]=-1;
 for(int i=0;i<m->n;i++)if(m->chosen[i]>=0) {
  int j=m->chosen[i];
  for(int k=0;k<m->a->enemy_count;k++)if(k!=j && m->now[i].p==m->a->enemies[k].p){next[j]=k;break;}
 }
 for(int j=0;j<m->a->enemy_count;j++) {
  unsigned seen=0;int k=j;
  while(k>=0){if(seen&(1u<<k))return 1;seen|=1u<<k;k=next[k];}
 }
 return 0;
}
static void wm_match_enumerate(WMEnemyMatch *m,int i,unsigned used,int matched) {
 if(matched+m->n-i<m->best)return;
 if(i==m->n) {
  if(wm_match_cycle(m))return;
  if(matched>m->best){m->best=matched;memset(m->possible,0,sizeof m->possible);}
  if(matched==m->best)for(int k=0;k<m->n;k++)m->possible[k]|=(unsigned short)(1u<<(m->chosen[k]<0?8:m->chosen[k]));
  return;
 }
 for(int j=0;j<m->a->enemy_count;j++)if(!(used&(1u<<j)) && wm_match_compatible(m->a,m->now[i],m->a->enemies[j])) {
  m->chosen[i]=j;wm_match_enumerate(m,i+1,used|(1u<<j),matched+1);
 }
 m->chosen[i]=-1;wm_match_enumerate(m,i+1,used,matched);
}
/* mob_cells[kind][0..mob_count[kind]-1]: the visible cells holding a melee
 * (kind 0) or ranged (kind 1) mob, in visible_cells order. */
static void wm_match_observed(const FPAgent *a,const FPScratch *s,int mob_cells[2][FP_VIEW],const int mob_count[2],int *match) {
 WMEnemyMatch m={0};m.a=a;m.best=-1;
 for(int kind=0;kind<2;kind++)for(int i=0;i<mob_count[kind] && m.n<8;i++) {
  int p=mob_cells[kind][i];
  m.now[m.n++]=(FPEnemy){p,kind?ranged_of(s)[p]:melee_of(s)[p],kind,0,0};
 }
 wm_match_enumerate(&m,0,0,0);
 for(int i=0;i<m.n;i++) {
  unsigned v=m.possible[i];match[i]=-1;
  if(v && !(v&(v-1)) && !(v&(1u<<8)))for(int j=0;j<a->enemy_count;j++)if(v==(1u<<j))match[i]=j;
 }
}

static int wm_troll_scope_enabled(const FPAgent *a,const FPObservation *o) {
 return a->floors[5].seen || o->level>=5;
}
static int wm_observe(FPAgent *a,const FPObservation *o,float reward,FPScratch *s) {
 int troll_scope=wm_troll_scope_enabled(a,o);
 int floor_changed=0;
 static const int inv_ach[10]={0,9,17,18,19,54,59,3,23,22};
 for(int i=0;i<10;i++)if(o->inv[i])a->achieved[inv_ach[i]]=1;
 static const int picks[4]={5,13,20,60},swords[4]={6,14,21,25};
 for(int i=0;i<4;i++){if(o->inv[FP_I_PICK]>i)a->achieved[picks[i]]=1;if(o->inv[FP_I_SWORD]>i)a->achieved[swords[i]]=1;}
 for(int i=0;i<4;i++){if(o->armour[i]==1)a->achieved[26]=1;if(o->armour[i]>=2)a->achieved[27]=1;if(o->armour_enchant[i])a->achieved[64]=1;}
 if(o->bow)a->achieved[52]=1;if(o->sword_enchant)a->achieved[63]=1;
 if(o->spells[0])a->achieved[55]=1;if(o->spells[1])a->achieved[57]=1;
 if(a->t) {
  const FPObservation *p=&a->previous;int act=a->previous_action;
  if(p->level==8 && o->level==8 && p->boss_vulnerable && !o->boss_vulnerable &&
     act==FP_DO && a->previous_block==FP_BOSS && !p->sleeping && !p->resting) {
   a->achieved[48]=1;
   if(++a->boss_hits>=8)a->achieved[49]=1;
  }
  if(!p->sleeping && !p->resting) {
   if(act==FP_PLACE_TABLE && o->inv[0]<p->inv[0])a->achieved[1]=1;
   if(act==FP_PLACE_FURNACE && o->inv[1]<p->inv[1])a->achieved[16]=1;
   if(act==FP_PLACE_STONE && o->inv[1]<p->inv[1])a->achieved[10]=1;
   if(act==FP_PLACE_PLANT && o->inv[7]<p->inv[7]){a->achieved[7]=1;a->plant_cell=a->previous_target;a->plant_tick=a->t;}
   if(act==FP_PLACE_TORCH && o->inv[8]<p->inv[8])a->achieved[24]=1;
   if(act==FP_SHOOT && o->inv[9]<p->inv[9])a->achieved[53]=1;
   if(act==FP_FIREBALL && o->mana<=p->mana-1)a->achieved[56]=1;
   if(act==FP_ICEBALL && o->mana<=p->mana-1)a->achieved[58]=1;
   if(act>=FP_POTION_RED && act<=FP_POTION_YELLOW && o->potions[act-FP_POTION_RED]<p->potions[act-FP_POTION_RED]) {
    int ix=act-FP_POTION_RED;a->achieved[62]=1;
    if(o->health>p->health+1)a->potion_effect[ix]=0;
    else if(o->health<p->health-2)a->potion_effect[ix]=1;
    else if(o->mana>p->mana+2)a->potion_effect[ix]=2;
    else if(o->mana<p->mana-2)a->potion_effect[ix]=3;
    else if(o->energy>p->energy+2)a->potion_effect[ix]=4;
    else if(o->energy<p->energy-2)a->potion_effect[ix]=5;
    /* A restorative colour shows nothing when its stat is already full.
     * Below full health it cannot be the healing one: stop retesting it. */
    else if(tr_query(a,o,s,TRQ_WM_POTION_RESTORE) && p->health<7+p->str)a->potion_effect[ix]=6;
   }
   if(act==FP_DO && !a->previous_mob) {
    if(a->previous_block==FP_GRASS)a->farm_attempts++;
    if(a->previous_block==FP_WATER||a->previous_block==FP_FOUNTAIN)a->achieved[4]=1;
    if(a->previous_block==FP_CHEST)a->achieved[61]=1;
    if(a->previous_block==FP_RIPE && o->food>p->food){a->achieved[11]=1;if(a->previous_target==a->plant_cell)a->plant_tick=a->t;}
   }
   if(act==FP_DO && a->previous_mob && reward-0.1f*(o->health-p->health)>0.5f) {
    static const int ach[3][8]={{8,36,38,40,65,42,44,46},{2,50,51,0,0,0,0,0},{12,37,39,41,66,43,45,47}};
    a->achieved[ach[a->previous_mob-1][a->previous_type-1]]=1;
   }
  }
  if(p->sleeping && !o->sleeping)a->achieved[15]=1;
 }
 if(o->level!=a->level) {
  /* The engine updates only the active floor. Retain the wounded mob and
   * cooldown we left behind instead of inventing fresh full-health mobs
   * on every recovery trip. Coordinates are local to each floor's map. */
  if(troll_scope) {
  a->wm_floor_enemy_count[a->level]=a->enemy_count;
  memcpy(a->wm_floor_enemy[a->level],a->enemies,(size_t)a->enemy_count*sizeof(FPEnemy));
  }
  int descending=o->level>a->level;a->level=o->level;floor_changed=1;
  if(troll_scope) {
  a->enemy_count=a->wm_floor_enemy_count[a->level];
  memcpy(a->enemies,a->wm_floor_enemy[a->level],(size_t)a->enemy_count*sizeof(FPEnemy));
  } else a->enemy_count=0;
  FPFloor *f=&a->floors[a->level];int landing=descending?f->up:f->down;
  if(!f->seen || landing<0) {a->r=a->c=48;landing=cell(48,48);}
  else {a->r=landing/FP_SIDE;a->c=landing%FP_SIDE;}
  if(descending)f->up=landing;else f->down=landing;
  f->seen=1;f->entered=a->t;a->arrow_count=0;a->shot_count=0;
 }
 FPFloor *f=&a->floors[a->level];f->steps++;f->clear=o->clear;
 if(a->t && a->previous.level==a->level && a->previous_action==FP_PLACE_TORCH && o->inv[8]<a->previous.inv[8]) {
  int r=a->previous_target/FP_SIDE,c=a->previous_target%FP_SIDE;
  for(int rr=-4;rr<=4;rr++)for(int cc=-4;cc<=4;cc++)
   if(rr*rr+cc*cc<=22 && inside(r+rr,c+cc))f->marks[cell(r+rr,c+cc)].lit=1;
 }
 /* Clear the mob layers: the four view layers and the projectile bits. */
 memset(s->mobs,0,sizeof s->mobs);memset(s->melee,0,sizeof s->melee);
 memset(s->ranged,0,sizeof s->ranged);memset(s->passive,0,sizeof s->passive);
 CellBits{s->arrow_next}.clear();s->visible_count=0;
 int changes[FP_VIEW],nchange=0,vis_q[8],vis_code[8],nvis=0;
 /* Visible mob cells by kind (melee, ranged) and in-map view indices holding
  * one of our projectiles, each in view order. */
 int mob_cells[2][FP_VIEW],mob_count[2]={0,0},own_view[FP_VIEW],nown=0;
 /* Position, floor kind, known bounds and the visible count live in locals
  * here: the byte stores below may alias anything as far as the compiler
  * knows, and would otherwise force their reloads on every cell. */
 const int ar=a->r,ac=a->c,lit_floor=a->level==0||a->level==1||a->level==3||a->level==4||a->level==6;
 int known_lo=f->known_lo,known_hi=f->known_hi,nvisible=0;
 s->view_r0=ar-4;s->view_c0=ac-5; /* view index i is the cell (ar+i/11-4, ac+i%11-5) */
 for(int i=0;i<FP_VIEW;i++) {
  int r=ar+i/11-4,c=ac+i%11-5;if(!inside(r,c))continue;
  int q=cell(r,c),old=f->map[q],b=o->blocks[i];
  if(o->visible[i]) {
   int item=o->items[i],melee=o->mobs[i][0],passive=o->mobs[i][1],ranged=o->mobs[i][2];
   f->map[q]=(uint8_t)b;f->marks[q].item=item!=0;
   if(b!=FP_BOUNDARY && b!=FP_INVALID) {
    if(q<known_lo)known_lo=q;if(q>known_hi)known_hi=q;
   }
   if(old!=b && (old==FP_TABLE||old==FP_FURNACE||b==FP_TABLE||b==FP_FURNACE))changes[nchange++]=q;
   if(item==2 || item==4)f->down=q;if(item==3)f->up=q;
   s->visible_cells[nvisible++]=q;
   s->melee[i]=(uint8_t)melee;s->passive[i]=(uint8_t)passive;s->ranged[i]=(uint8_t)ranged;
   s->mobs[i]=(melee?1:0)|(passive?2:0)|(ranged?4:0);
   if(melee)mob_cells[0][mob_count[0]++]=q;
   if(ranged)mob_cells[1][mob_count[1]++]=q;
  } else if(f->marks[q].lit||lit_floor)f->map[q]=FP_BOUNDARY;
  if(o->mobs[i][3] && nvis<8){arrow_mark(s,q);vis_q[nvis]=q;vis_code[nvis++]=o->mobs[i][3];}
  if(o->mobs[i][4])own_view[nown++]=i;
 }
 f->known_lo=known_lo;f->known_hi=known_hi;s->visible_count=nvisible;
 for(int i=0;i<nchange;i++) {
  int r=changes[i]/FP_SIDE,c=changes[i]%FP_SIDE;
  for(int rr=r-1;rr<=r+1;rr++)for(int cc=c-1;cc<=c+1;cc++)if(inside(rr,cc)) {
   int bits=0;for(int r2=rr-1;r2<=rr+1;r2++)for(int c2=cc-1;c2<=cc+1;c2++)if(r2!=rr||c2!=cc) {
    int b2=block(a,r2,c2);if(b2==FP_TABLE)bits|=1;if(b2==FP_FURNACE)bits|=2;
   }
   f->stations[cell(rr,cc)]=(uint8_t)bits;
  }
 }
 /* Mob projectiles (see the ranged-mob model above). Tracks keep a heading:
  * projectile_types[j] = public code | heading<<8 | steps unseen<<12. A
  * visible projectile continues a track along its heading, else one a cell
  * away (the displacement is its heading). A new one was fired last tick by
  * the ranged mob of its type beside it, which stayed put and aimed along its
  * larger offset to us; failing that, by the one cell whose aim at us gives
  * that heading. Tracks with a heading are carried through darkness and out
  * of view until a visible cell, a wall or a mob stops them. Each projectile
  * endangers its cell and the next one. */
 int vdir[8]={0},vmatch[8],pused[3]={0},shooters[8],nshooter=0;
 for(int k=0;k<nvis;k++)vmatch[k]=-1;
 for(int k=0;k<nvis;k++)for(int j=0;j<a->arrow_count && j<3;j++) {
  int code=a->projectile_types[j]&255,d=(a->projectile_types[j]>>8)&7;
  if(pused[j] || code!=vis_code[k] || d<1 || d>4 || a->arrows[j]+dr[d]*FP_SIDE+dc[d]!=vis_q[k])continue;
  pused[j]=1;vmatch[k]=j;vdir[k]=d;break;
 }
 /* A track one cell away: prefer tracks without a heading (a track with
  * one went elsewhere), and give up when two qualify. */
 for(int pass=0;pass<2;pass++)for(int k=0;k<nvis;k++)if(vmatch[k]<0) {
  int r=vis_q[k]/FP_SIDE,c=vis_q[k]%FP_SIDE,n=0,jm=-1;
  for(int j=0;j<a->arrow_count && j<3;j++) {
   int code=a->projectile_types[j]&255,d=(a->projectile_types[j]>>8)&7,rr=a->arrows[j]/FP_SIDE,cc=a->arrows[j]%FP_SIDE;
   if(pused[j] || code!=vis_code[k] || abs(r-rr)+abs(c-cc)!=1 || (pass==0 && d))continue;
   n++;jm=j;
  }
  if(n!=1)continue;
  int rr=a->arrows[jm]/FP_SIDE,cc=a->arrows[jm]%FP_SIDE;
  pused[jm]=1;vmatch[k]=jm;vdir[k]=r<rr?FP_UP:r>rr?FP_DOWN:c<cc?FP_LEFT:FP_RIGHT;
 }
 for(int k=0;k<nvis;k++)if(vmatch[k]<0) {
  int r=vis_q[k]/FP_SIDE,c=vis_q[k]%FP_SIDE,n=0,dd=0,from=-1;
  for(int d=1;d<=4;d++) {
   int mr=r-dr[d],mc=c-dc[d];if(!inside(mr,mc))continue;int m=cell(mr,mc);
   if(!ranged_of(s)[m] || wm_ranged_projectile(ranged_of(s)[m])!=vis_code[k] || !wm_fire_direction_ok(mr,mc,a->r,a->c,d))continue;
   n++;dd=d;from=m;
  }
  if(n==1){vdir[k]=dd;if(nshooter<8)shooters[nshooter++]=from;continue;}
  /* Unattributed shots: in the Vaults and Troll Mines one leaving the only
   * ranged mob beside it flies away from it; one on our row or column
   * within two cells is assumed to fly at us; one diagonally beside us
   * endangers both cells it shares with us (unknown heading). */
  if(a->level==4 || a->level==5) {
   n=0;for(int d=1;d<=4;d++){int mr=r-dr[d],mc=c-dc[d];if(inside(mr,mc) && ranged_of(s)[cell(mr,mc)]){n++;dd=d;}}
   if(n==1){vdir[k]=dd;continue;}
  }
  int vr=a->r-r,vc=a->c-c;
  if(vr==0 && abs(vc)<=2 && vc){vdir[k]=vc>0?FP_RIGHT:FP_LEFT;continue;}
  if(vc==0 && abs(vr)<=2 && vr){vdir[k]=vr>0?FP_DOWN:FP_UP;continue;}
  if(abs(vr)==1 && abs(vc)==1){arrow_mark(s,cell(r+vr,c));arrow_mark(s,cell(r,c+vc));}
 }
 int ntrack=0,track_q[3],track_t[3];
 for(int k=0;k<nvis;k++) {
  int r=vis_q[k]/FP_SIDE,c=vis_q[k]%FP_SIDE,d=vdir[k];
  if(d && inside(r+dr[d],c+dc[d]))arrow_mark(s,cell(r+dr[d],c+dc[d]));
  if(ntrack<3){track_q[ntrack]=vis_q[k];track_t[ntrack++]=vis_code[k]|d<<8;}
 }
 for(int j=0;j<a->arrow_count && j<3;j++) {
  int code=a->projectile_types[j]&255,d=(a->projectile_types[j]>>8)&7,age=((a->projectile_types[j]>>12)&15)+1;
  if(pused[j] || d<1 || d>4 || age>8 || ntrack>=3)continue;
  int r=a->arrows[j]/FP_SIDE+dr[d],c=a->arrows[j]%FP_SIDE+dc[d];
  if(!inside(r,c) || solid(block(a,r,c)) || block(a,r,c)==FP_BOUNDARY || mobs_of(s)[cell(r,c)] || (r==a->r && c==a->c))continue;
  int vr=r-a->r+4,vc=c-a->c+5;if(vr>=0 && vr<9 && vc>=0 && vc<11 && o->visible[vr*11+vc])continue;
  arrow_mark(s,cell(r,c));
  if(inside(r+dr[d],c+dc[d]) && !solid(block(a,r+dr[d],c+dc[d])))arrow_mark(s,cell(r+dr[d],c+dc[d]));
  track_q[ntrack]=cell(r,c);track_t[ntrack++]=code|d<<8|age<<12;
 }
 memcpy(a->arrows,track_q,(size_t)ntrack*sizeof(int));memcpy(a->projectile_types,track_t,(size_t)ntrack*sizeof(int));a->arrow_count=ntrack;
 /* Carry visible shots forward one tick, including a newly fired shot
  * only when the public inventory or mana confirms that it was launched. */
 int shot_pos[4],shot_type[4],shot_dir[4],nshot=a->shot_count;
 memcpy(shot_pos,a->shot_pos,sizeof(a->shot_pos));
 memcpy(shot_type,a->shot_type,sizeof(a->shot_type));
 memcpy(shot_dir,a->shot_dir,sizeof(a->shot_dir));
 if(a->t && a->previous.level==o->level && !a->previous.sleeping && !a->previous.resting) {
  int act=a->previous_action,type=0;
  if(act==FP_SHOOT && o->inv[9]<a->previous.inv[9])type=5;
  if((act==FP_FIREBALL || act==FP_ICEBALL) && o->mana<=a->previous.mana-1)type=act==FP_FIREBALL?3:4;
  if(type && nshot<4){shot_pos[nshot]=cell(a->observed_r,a->observed_c);shot_type[nshot]=type;shot_dir[nshot++]=a->previous.dir;}
 }
 FPEnemy enemies[8];int nenemy=0,used[8]={0},identity[8],observed_index=0;
 if(troll_scope)wm_match_observed(a,s,mob_cells,mob_count,identity);
 /* Mobs freeze while we are on another floor and strike on the arrival step.
  * A health loss on arrival in the Troll Mines shows which trolls have just
  * attacked; the conservative zero cooldown predicted a second, lethal blow
  * and sent us straight back up the ladder, again after every heal (ep 102). */
 int arrival_hits=0;
 if(a->t && a->previous.level!=o->level && tr_query(a,o,s,TRQ_WM_ARRIVAL_GUARD)) {
  float lost=a->previous.health-o->health,blow=wm_attack_damage(o,6);
  if(lost>=0.6f*blow)arrival_hits=(int)(lost/blow+0.5f);
 }
 static const float hp[8][2]={{5,3},{7,5},{9,6},{11,8},{12,12},{20,4},{20,14},{24,16}};
 for(int kind=0;kind<2;kind++)for(int i=0;i<mob_count[kind];i++) {
  int p=mob_cells[kind][i],type=kind?ranged_of(s)[p]:melee_of(s)[p];if(nenemy>=8)continue;
  int match=-1;
  if(troll_scope)match=identity[observed_index++];
  else {
   int dist=2;
  for(int j=0;j<a->enemy_count;j++) {
   FPEnemy e=a->enemies[j];if(used[j]||e.kind!=kind||e.type!=type)continue;
   /* A new mob can occupy the cell of a mob killed on the previous turn.
    * Its health must start fresh rather than inheriting the victim's HP. */
   if(a->previous_action==FP_DO && a->previous_target==e.p &&
      !a->previous.sleeping && !a->previous.resting && wm_hit_damage(&a->previous,e.type,e.kind)>=e.health)continue;
   int d=abs(e.p/FP_SIDE-p/FP_SIDE)+abs(e.p%FP_SIDE-p%FP_SIDE);
   if(d<dist){dist=d;match=j;}
  }
  }
  FPEnemy e={p,type,kind,0,hp[type-1][kind]};
  if(match<0 && !kind && arrival_hits>0 && abs(p/FP_SIDE-a->r)+abs(p%FP_SIDE-a->c)==1){e.cooldown=5;arrival_hits--;}
  if(match>=0) {
   FPEnemy old=a->enemies[match];used[match]=1;e.health=old.health;e.cooldown=old.cooldown-1;
   if(a->previous_action==FP_DO && a->previous_target==old.p && !a->previous.sleeping && !a->previous.resting) {
    e.health=fmaxf(0.1f,e.health-wm_hit_damage(&a->previous,type,kind));
   }
   int replacement=0;
   /* Projectiles hit after mobs move. A surviving projectile has not hit
    * this enemy; a disappeared shot can have hit its old or next cell. */
   for(int j=0;j<nshot;j++) {
    if(!shot_dir[j])continue;
    int q=shot_pos[j]+dr[shot_dir[j]]*FP_SIDE+dc[shot_dir[j]];
    if(e.p!=shot_pos[j] && (e.p!=q || mobs_of(s)[shot_pos[j]]))continue;
    int rr=q/FP_SIDE-a->r+4,cc=q%FP_SIDE-a->c+5;
    if(rr>=0 && rr<9 && cc>=0 && cc<11 && o->mobs[rr*11+cc][4]==shot_type[j])continue;
    float damage=wm_projectile_damage(o,shot_type[j],type,kind);
    /* A mob where we predicted a kill may be a new occupant. */
    if(damage>=e.health){e.health=hp[type-1][kind];e.cooldown=0;replacement=1;break;}
    e.health-=damage;
   }
   /* An adjacent mob can stand still while cooling down or blocked.
    * Only observed health loss supports inferring a fresh attack cooldown.
    * Otherwise retain the conservative countdown: a spell kills after
    * melee attacks, so a fictitious cooldown can concede a lethal hit. */
   if(!replacement && !kind && o->health<a->previous.health && old.cooldown<=0 && old.p==p && abs(old.p/FP_SIDE-a->r)+abs(old.p%FP_SIDE-a->c)==1)e.cooldown=5;
   /* A ranged mob believed ready that stayed put has fired when a new
    * projectile left it, when it stood beside us and we lost a projectile's
    * worth of health (point blank), or when we stood 4-5 cells away (it
    * always fires then) and its spawn cell was hidden, blocked or the floor
    * already held three projectiles. Firing resets its cooldown to 4. */
   if(kind && !replacement && old.cooldown<=0 && old.p==p) {
    int fired=0,mr=p/FP_SIDE,mc=p%FP_SIDE,d0=abs(mr-a->r)+abs(mc-a->c);
    for(int k=0;k<nshooter;k++)fired|=shooters[k]==p;
    if(!fired && d0==1 && o->health<=a->previous.health-0.6f*wm_projectile_hit(o,wm_ranged_projectile(type)))fired=1;
    if(!fired && (d0==4 || d0==5)) {
     int unseen=nvis>=3;
     for(int d=1;d<=4;d++)if(wm_fire_direction_ok(mr,mc,a->r,a->c,d)) {
      int rr=mr+dr[d],cc=mc+dc[d],vr=rr-a->r+4,vc=cc-a->c+5;
      if(!inside(rr,cc) || solid(block(a,rr,cc)) || mobs_of(s)[cell(rr,cc)] || vr<0 || vr>=9 || vc<0 || vc>=11 || !o->visible[vr*11+vc])unseen=1;
     }
     fired=unseen;
    }
    if(fired)e.cooldown=4;
   }
  }
  if(match<0 && kind)for(int k=0;k<nshooter;k++)if(shooters[k]==p)e.cooldown=4;
  /* p is a view cell (mob_cells): store at its view index. */
  s->enemy_health[(p/FP_SIDE-s->view_r0)*kViewCols+p%FP_SIDE-s->view_c0]=e.health;enemies[nenemy++]=e;
 }
 memcpy(a->enemies,enemies,(size_t)nenemy*sizeof(FPEnemy));a->enemy_count=nenemy;
#if WM_PB==1
 /* Point-blank danger: cells beside a ready ranged mob where its shot is
  * likely enough (wm_point_blank_p >= WM_PB_MIN) count as projectile cells. */
 for(int i=0;i<nenemy;i++)if(enemies[i].kind && enemies[i].cooldown<=0)for(int d=1;d<=4;d++) {
  int r=enemies[i].p/FP_SIDE+dr[d],c=enemies[i].p%FP_SIDE+dc[d];
  if(inside(r,c) && wm_point_blank_p(a,s,enemies[i],r,c)>=WM_PB_MIN)arrow_mark(s,cell(r,c));
 }
#endif
 /* Retain a heading only when an observed shot matches its predicted move. */
 a->shot_count=0;
 for(int k=0;k<nown && a->shot_count<3;k++) {
  int i=own_view[k],p=cell(a->r+i/11-4,a->c+i%11-5),d=0;
  for(int j=0;j<nshot;j++)if(shot_type[j]==o->mobs[i][4] && shot_dir[j] && p==shot_pos[j]+dr[shot_dir[j]]*FP_SIDE+dc[shot_dir[j]]){d=shot_dir[j];break;}
  int j=a->shot_count++;a->shot_pos[j]=p;a->shot_type[j]=o->mobs[i][4];a->shot_dir[j]=d;
 }
 /* Saturate at 50: every reader takes imin(visits,50), and min(count,50) is all
  * min(min(count,65535),50) ever read (kVisitCap). */
 int q=cell(a->r,a->c);if(f->marks[q].visits<kVisitCap)f->marks[q].visits++;
 if(a->level==0 && a->plant_cell>=0 && a->previous_action==FP_DO && a->previous_target==a->plant_cell &&
    a->previous_block==FP_RIPE && f->map[a->plant_cell]==FP_PLANT)a->plant_tick=a->t;
 if(a->plant_cell>=0 && a->t-a->plant_tick>=600 && a->floors[0].map[a->plant_cell]==FP_PLANT)a->floors[0].map[a->plant_cell]=FP_RIPE;
 a->observed_r=a->r;a->observed_c=a->c;
 if(fp_wm_oracle)fp_wm_oracle(a,o,s,fp_wm_oracle_ctx);
 return floor_changed;
}
/* ==== Evaluator API (exported; never called by the policy) ==== */
float fp_wm_incoming_damage(const FPAgent *a,const FPObservation *o,FPScratch *s,int action) {
 return wm_incoming_damage(a,o,s,action);
}
float fp_wm_hit_damage(const FPObservation *o,int type,int ranged) { return wm_hit_damage(o,type,ranged); }
