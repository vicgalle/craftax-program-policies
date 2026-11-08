/* goal_runtime.inc: the C API of the expedition/excursion goal runtime
 * (FPGoalRuntime, full_policy.h: fp_goal_return, fp_goal_finish,
 * fp_goal_call_excursion) for C callers. The program runs its own TR copy
 * (p/plan.tr: runtime_observe pushes the expedition frame, the commit hook
 * and the dark-entry repairs call excursions). */
static void runtime_push(FPAgent *a,int kind,int frontier) {
 FPGoalRuntime *g=&a->runtime;FPGoalFrame *f=&g->frame[g->depth];
 memset(f,0,sizeof *f);f->kind=kind;f->frontier=frontier;f->entry_t=a->t;
 f->return_outcome=FP_RO_COMPLETED;f->in=g->observed;
 f->id=++g->next_id;f->parent=g->depth?g->frame[g->depth-1].id:0;g->depth++;
}
void fp_goal_return(FPAgent *a,int outcome) {
 FPGoalRuntime *g=&a->runtime;if(!g->depth)return;
 g->last_return=g->frame[--g->depth];g->last_outcome=outcome;g->boundary_t=a->t;
 g->event[g->returns%4]=(FPGoalEvent){g->last_return,g->observed,a->t,outcome};
 g->returns++;g->completions+=outcome==FP_RO_COMPLETED;g->abandonments+=outcome==FP_RO_ABANDONED;
 if(g->depth)g->frame[g->depth-1].suspended=0;
}
void fp_goal_finish(FPAgent *a,int outcome) { while(a->runtime.depth)fp_goal_return(a,outcome); }
int fp_goal_call_excursion(FPAgent *a,int purpose,int mode,int target) {
 FPGoalRuntime *g=&a->runtime;
 if(g->depth!=1 || g->frame[0].kind!=FP_RG_EXPEDITION)return 0;
 g->frame[0].suspended=1;runtime_push(a,FP_RG_EXCURSION,g->frame[0].frontier);
 g->frame[1].purpose=purpose;g->frame[1].mode=mode;g->frame[1].target=target;return 1;
}
