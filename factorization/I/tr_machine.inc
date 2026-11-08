/* tr_machine.inc: natives the interpreter adds for TR programs. They give rules
 * the per-cell views of the scratch (which C code reads through typed
 * accessors) and the libc helpers that rule code used. */
static int mob_at(const FPScratch *s,int q) { return mobs_of(s)[q]; }
static int arrow_at(const FPScratch *s,int q) { return arrows_of(s)[q]; }
static int tr_abs(int x) { return abs(x); }
static float tr_fmaxf(float x,float y) { return fmaxf(x,y); }
static float tr_fminf(float x,float y) { return fminf(x,y); }
/* Scratch views for programs. Reads follow the discipline of the C code: the
 * dist and first of a cell are valid once a search has settled it (paths_within,
 * paths_to, paths_all, path_dist, path_near, path_dist_within), and prefix holds
 * what its last filler wrote (water_distances_at, or an integral image). */
static int dist_at(const FPScratch *s,int q) { return s->dist[q]; }
static int first_at(const FPScratch *s,int q) { return s->first[q]; }
static int prefix_at(const FPScratch *s,int r,int c) { return s->prefix[r][c]; }
static void water_distances_at(const FPAgent *a,FPScratch *s,int level) { water_distances(&a->floors[level],s); }
