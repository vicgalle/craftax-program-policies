/* Independent milestone monitor: native engine state only, no policy fields. */
#ifndef SEQUENCE_MONITOR_H
#define SEQUENCE_MONITOR_H
#include <stdio.h>
#include <string.h>
typedef struct {int floor,stock[5],keep[5];} SQStep;
typedef struct {int n;SQStep steps[8];} SQGoal;
typedef struct {int level,stock[5];float health;} SQState;
typedef struct {int stage,success,violation,last_step,at[8],keep[5];} SQMonitor;
static int sq_load(const char *path,SQGoal *g){
 FILE *f=fopen(path,"r");if(!f)return 0;
 if(fscanf(f,"%d",&g->n)!=1 || g->n<1 || g->n>8){fclose(f);return 0;}
 int ok=1;
 for(int j=0;j<g->n;j++){
  SQStep *s=&g->steps[j];ok&=fscanf(f,"%d",&s->floor)==1;
  for(int r=0;r<5;r++)ok&=fscanf(f,"%d",&s->stock[r])==1;
  for(int r=0;r<5;r++)ok&=fscanf(f,"%d",&s->keep[r])==1;
  if(s->floor< -1 || s->floor>8)ok=0;
  int total=0;
  for(int r=0;r<5;r++){total+=s->stock[r];if(s->stock[r]<0 || s->stock[r]>20 || s->keep[r]<0 || s->keep[r]>s->stock[r] || (s->floor>=0 && s->stock[r]))ok=0;}
  if(s->floor<0 && !total)ok=0;
 }
 char tail;if(fscanf(f," %c",&tail)==1)ok=0;fclose(f);return ok;
}
static void sq_reset(SQMonitor *m){memset(m,0,sizeof(*m));m->last_step=-1;for(int j=0;j<8;j++)m->at[j]=-1;}
static void sq_check(const SQGoal *g,SQMonitor *m,const SQState *s,int step){
 if(m->success || m->violation || step<=m->last_step)return;
 m->last_step=step;
 for(int r=0;r<5;r++)if(s->stock[r]<m->keep[r]){m->violation=1;return;}
 if(s->health<=0)return;
 const SQStep *p=&g->steps[m->stage];
 if(p->floor>=0 && s->level!=p->floor)return;
 for(int r=0;r<5;r++)if(s->stock[r]<p->stock[r])return;
 m->at[m->stage]=step;m->stage++;
 for(int r=0;r<5;r++)if(p->keep[r]>m->keep[r])m->keep[r]=p->keep[r];
 m->success=m->stage==g->n;
}
#endif
