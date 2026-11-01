/* Synthetic fixture: validates failure handling, not hardware execution. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_secure.h"
typedef struct {uint32_t ring[64],mem[16];unsigned reads,writes,fr,fw,mode,us,delivered;} Device;
static Device d;
static int rd(void *c,unsigned a,uint32_t *v) {
 Device *p=c;assert(marble_cp_access(a,0));if(++p->reads==p->fr)return -1;
 *v=0;
 if(a==0xf400)*v=p->writes?(p->mode==2?1:0):(p->mode==1?2:p->mode==11?0:1);
 if(a==0x806 || a==0x807)*v=p->mode==3?27:28;
 if(a==0x821 && p->mode==4)*v=1;
 if(a==0x201 && p->mode==5)*v=1u<<23;
 if(a==0x210 && p->mode==6)*v=2;
 if(a==0x213 && p->mode==7)*v=1;
 return 0;
}
static int wr(void *c,unsigned a,uint32_t v) {
 Device *p=c;assert(a==0x807 && v==28);unsigned fail=++p->writes==p->fw;
 if(fail && !p->delivered)return -1;
 AdrenoPM4Packet plan[2];size_t count=0;
 assert(!AdrenoPM4Plan(p->ring+21,7,plan,2,&count) && count==2);
 assert(p->ring[22]==0 && p->ring[24]==0x08000031 && p->ring[25]==0x300004 && p->ring[27]==2);
 p->mem[0]=p->mode==8?21:28;p->mem[1]=p->mode==9?1:2;
 return fail?-1:0;
}
static void barrier(void *c){(void)c;}
static void delay_us(void *c,unsigned us){Device *p=c;p->us+=us;assert(p->us<=100010);}
static int health(void *c){return ((Device *)c)->mode==10;}
static void init(unsigned mode){memset(&d,0,sizeof(d));d.mode=mode;}
int main(void) {
 MarbleCpIo io={&d,rd,wr,delay_us,health,barrier};MarbleSecureResult r;
 MarbleCpResult cp={.completed=1,.rptr=21,.wptr=21,.fence=1};
 init(0);assert(marble_secure_run(&io,d.ring,d.mem,&cp,1,&r));unsigned nr=d.reads;
 assert(r.completed && r.before==1 && r.after==0 && r.fence==2);
 for(unsigned n=1;n<=nr;n++){init(0);d.fr=n;assert(!marble_secure_run(&io,d.ring,d.mem,&cp,1,&r));}
 for(unsigned v=0;v<2;v++){init(0);d.fw=1;d.delivered=v;assert(!marble_secure_run(&io,d.ring,d.mem,&cp,1,&r) && r.submitted);}
 for(unsigned n=1;n<=10;n++){init(n);assert(!marble_secure_run(&io,d.ring,d.mem,&cp,1,&r));}
 init(11);assert(marble_secure_run(&io,d.ring,d.mem,&cp,1,&r) && !r.before && r.submitted && r.fence==2);
 init(0);assert(!marble_secure_run(&io,d.ring,d.mem,&cp,0,&r) && !d.writes);
 cp.halt_requested=1;assert(!marble_secure_run(&io,d.ring,d.mem,&cp,1,&r) && !d.writes);
 assert(marble_cp_access(0xf400,0) && !marble_cp_access(0xf400,1));
 printf("Secure fixtures PASS: %u read failures, before/after-delivery kick, auth/CP gates, wrong transition/fence/shadow, timeout/fault/IRQ/status/health; no hardware claim\n",nr);
}
