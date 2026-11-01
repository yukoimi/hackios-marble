/* Synthetic PM4 interpreter fixture; never real render evidence. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_blit.h"
typedef struct {uint32_t ring[4096],mem[16],scratch[4096],regs[0x10000],wp,target;unsigned mode,reads,fr,fw,writes,delivered,us;} Device;
static Device d;
static int rd(void *c,unsigned a,uint32_t *v) {
 Device *p=c;assert(marble_cp_access(a,0));if(++p->reads==p->fr)return -1;*v=0;
 if(a==0x806 || a==0x807)*v=p->mode==1?28:p->wp;
 if(a==0x821 && p->mode==2)*v=1;
 if(a==0x201 && p->mode==3)*v=1u<<23;
 if(a==0x210 && p->mode==4)*v=2;
 if(a==0x213 && p->mode==5)*v=1;
 return 0;
}
static int wr(void *c,unsigned a,uint32_t v) {
 Device *p=c;unsigned start=p->mem[0];uint32_t fence=p->mem[1]+1;
 assert(a==0x807 && v==(start+139)%4096);unsigned fail=++p->writes==p->fw;
 if(fail && !p->delivered)return -1;p->wp=v;
 AdrenoPM4Packet plan[128];size_t n=0;unsigned bands=0;
 uint32_t linear[139];for(unsigned i=0;i<139;i++)linear[i]=p->ring[(start+i)%4096];
 assert(!AdrenoPM4Plan(linear,139,plan,128,&n));
 for(size_t i=0;i<n;i++) {
  AdrenoPM4Packet *s=plan+i;uint32_t *w=linear+s->header_word+1;
  if(s->type==4){assert(s->payload_words==1);p->regs[s->identifier]=w[0];}
  else if(s->identifier==0x2c) {
   assert(s->payload_words==1 && w[0]==3 && p->regs[0xa9bf]==0xf180 && p->regs[0xb2d2]==0x20000000);
   assert(p->regs[0x88e5]==0x80000000 && p->regs[0x8c18]==p->target && p->regs[0x8c19]==0 && p->regs[0x8c1a]==2);
   assert(p->regs[0x8c00]==0x00f03080 && p->regs[0x8c17]==0x430);
   unsigned top=p->regs[0x8405]>>16,bottom=p->regs[0x8406]>>16;
   uint32_t color=0xff000000|(p->regs[0x8c2c]<<16)|(p->regs[0x8c2d]<<8)|p->regs[0x8c2e];
   assert(top<=bottom && bottom<32);
   for(unsigned y=top;y<=bottom;y++)for(unsigned x=0;x<32;x++)p->scratch[1024+y*32+x]=color;
   bands++;
  } else if(s->identifier==0x46 && s->payload_words==4) {
   assert(w[0]==0x08000031 && w[1]==0x300004 && w[2]==0 && w[3]==fence);p->mem[1]=w[3];
  }
 }
 assert(bands==3);p->mem[0]=v;
 if(p->mode==6)p->scratch[1024+31*32+31]^=1;
 if(p->mode==7)p->scratch[2048]^=1;
 if(p->mode==8)p->mem[1]=2;
 if(p->mode==9)p->mem[0]=28;
 return fail?-1:0;
}
static int health(void *c){return ((Device *)c)->mode==10;}
static void barrier(void *c){(void)c;}
static void delay_us(void *c,unsigned us){Device *p=c;p->us+=us;assert(p->us<=100010);}
static void init(unsigned mode){memset(&d,0,sizeof(d));d.mode=mode;d.mem[0]=28;d.mem[1]=2;d.target=0x501000;}
int main(void) {
 MarbleCpIo io={&d,rd,wr,delay_us,health,barrier};MarbleBlitResult r;
 MarbleSecureResult s={.completed=1,.fence=2,.wptr=28};
 init(0);assert(marble_blit_run(&io,d.ring,d.mem,d.scratch,&s,&r));unsigned nr=d.reads;
 assert(r.completed && r.pixels==1024 && r.guards==3056 && r.words==167);
 for(unsigned n=1;n<=nr;n++){init(0);d.fr=n;assert(!marble_blit_run(&io,d.ring,d.mem,d.scratch,&s,&r));}
 for(unsigned v=0;v<2;v++){init(0);d.fw=1;d.delivered=v;assert(!marble_blit_run(&io,d.ring,d.mem,d.scratch,&s,&r) && r.submitted);}
 for(unsigned mode=1;mode<=10;mode++){init(mode);assert(!marble_blit_run(&io,d.ring,d.mem,d.scratch,&s,&r));}
 init(0);unsigned pos=28;
 for(unsigned fence=3;fence<131;fence++) {
  assert(marble_blit_submit(&io,d.ring,d.mem,d.scratch,pos,fence,&r));pos=r.wptr;
 }
 init(0);d.target=0x2001000;
 assert(marble_blit_submit_target(&io,d.ring,d.mem,d.scratch,28,3,d.target,&r));
 init(0);assert(!marble_blit_submit_target(&io,d.ring,d.mem,d.scratch,28,3,0xdead000,&r) && !d.writes);
 init(0);s.after=1;assert(!marble_blit_run(&io,d.ring,d.mem,d.scratch,&s,&r) && !d.writes);
 printf("2D fixtures PASS: %u read failures, posted kick failures, faults/IRQ/status, wrong pixel/guard/fence/shadow, timeout and secure gate; synthetic only\n",nr);
}
