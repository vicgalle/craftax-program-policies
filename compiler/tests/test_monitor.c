#include "monitor.h"
#include <assert.h>
int main(void){
 SQGoal g={.n=4,.steps={{.floor=2},{.floor=-1,.stock={0,0,0,4,0},.keep={0,0,0,2,0}},{.floor=0},{.floor=3}}};
 SQMonitor m;SQState s={.level=3,.health=9};sq_reset(&m);
 sq_check(&g,&m,&s,0);assert(m.stage==0); // future visit doesn't count
 s.level=2;s.stock[3]=4;sq_check(&g,&m,&s,1);assert(m.stage==1);
 sq_check(&g,&m,&s,1);assert(m.stage==1); // same observation can't advance twice
 sq_check(&g,&m,&s,2);assert(m.stage==2 && m.keep[3]==2);
 s.level=0;s.stock[3]=2;sq_check(&g,&m,&s,3);assert(m.stage==3);
 s.level=3;sq_check(&g,&m,&s,4);assert(m.success && !m.violation);
 for(int j=1;j<4;j++)assert(m.at[j]>m.at[j-1]);
 sq_reset(&m);s.level=2;s.stock[3]=4;sq_check(&g,&m,&s,0);sq_check(&g,&m,&s,1);
 s.stock[3]=1;s.level=0;sq_check(&g,&m,&s,2);assert(m.violation && m.stage==2 && !m.success);
 sq_reset(&m);s.level=2;s.health=0;sq_check(&g,&m,&s,0);assert(m.stage==0);
 SQGoal equal={.n=2,.steps={{.floor=0},{.floor=0}}};s.level=0;s.health=9;sq_reset(&m);
 sq_check(&equal,&m,&s,0);assert(m.stage==1 && !m.success);
 sq_check(&equal,&m,&s,1);assert(m.success);
 SQGoal keeps={.n=3,.steps={{.floor=-1,.stock={8,0,0,0,0},.keep={6,0,0,0,0}},
  {.floor=-1,.stock={4,0,0,0,0},.keep={2,0,0,0,0}},{.floor=1}}};
 s.stock[0]=8;sq_reset(&m);sq_check(&keeps,&m,&s,0);sq_check(&keeps,&m,&s,1);assert(m.keep[0]==6);
 s.stock[0]=5;sq_check(&keeps,&m,&s,2);assert(m.violation);
 puts("PASS independent sequence monitor");return 0;
}
