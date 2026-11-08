#include "full_policy.cpp"
#include "def_ids.h"
#include <assert.h>
#include <stdio.h>
static TREnv e;
static int call(int id,int x=0){TRVal args[1]={tr_i(x)};return tr_invoke(e,*tr_program(),id,args).i;}
int main(int argc,char **argv){
 assert(argc==2);FPAgent *a=(FPAgent*)calloc(1,sizeof(*a));FPScratch *s=(FPScratch*)calloc(1,sizeof(*s));
 FPObservation o={};FPConfig cfg=fp_default_config();e.a=a;e.o=&o;e.s=s;e.cfg=&cfg;
 o.health=9;o.food=o.drink=o.energy=9;a->level=o.level=3;call(TEST_SQ_OBSERVE);assert(a->intent.stage==0);
 a->level=o.level=2;call(TEST_SQ_OBSERVE);assert(a->intent.stage==1);
 for(int r=0;r<5;r++)o.inv[r]=5;call(TEST_SQ_OBSERVE);assert(a->intent.stage==2);
 for(int r=0;r<5;r++)assert(call(TEST_SQ_RESERVE,r)==5);
 a->level=o.level=0;o.health=0;call(TEST_SQ_OBSERVE);assert(a->intent.stage==2);
 o.health=9;call(TEST_SQ_OBSERVE);assert(a->intent.stage==3 && call(TEST_SQ_DONE));
 // Independently measured engine costs exercise the emitted TR guards.
 FILE *f=fopen(argv[1],"r");assert(f);int action,cost[5],checks=0;
 while(fscanf(f,"%d%d%d%d%d%d",&action,cost,cost+1,cost+2,cost+3,cost+4)==6){
  for(int r=0;r<5;r++)if(cost[r]){
   for(int i=0;i<5;i++)o.inv[i]=99;
   o.inv[r]=5+cost[r]-1;int skill=FP_CRAFT;
   assert(tr_filter_hook(a,&o,s,action,&skill)!=action);checks++;
   o.inv[r]++;assert(tr_filter_hook(a,&o,s,action,&skill)==action);checks++;
  }
 }
 // No navigation restrictions are hidden in the guard-only treatment.
 for(int i=0;i<5;i++)o.inv[i]=99;
 int skill=FP_TRAVEL;assert(tr_filter_hook(a,&o,s,FP_DESCEND,&skill)==FP_DESCEND);
 assert(tr_filter_hook(a,&o,s,FP_ASCEND,&skill)==FP_ASCEND);
 fp_reset(a,1);assert(!a->intent.stage && !a->intent.enabled);
 printf("PASS TR sequence state and %d engine-cost checks\n",checks);fclose(f);free(a);free(s);return 0;
}
