/* Synthetic PAS control fixture; never authentication or GPU evidence. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_zap.h"
typedef struct {unsigned n,fail,bad_answer,copy_fail,copies,stage,health_at,health_n;} Device;
static int call(void *c,uint32_t command,uint32_t args,uint64_t a,uint64_t b,uint64_t d,uint64_t *answer)
{
 Device *p=c;unsigned n=++p->n;
 if(command==0x601)assert(args==1 && a>=0x02000201 && a<=0x02000206 && !b && !d);
 else {assert(a==13);if(command==0x201)assert(args==0x82 && b==UINT64_C(0x843000000) && !d);
 else if(command==0x202)assert(args==3 && b==MARBLE_ZAP_PHYS && d==0x1000 && p->stage==1);
 else {assert((command==0x205 || command==0x206) && args==1 && !b && !d);if(command==0x205)assert(p->copies==1);}}
 *answer=command==0x601?1:0;
 if(n==p->fail)return -4;
 if(n==p->bad_answer){*answer=7;return 0;}
 if(command==0x201)p->stage=1;
 if(command==0x202)p->stage=2;
 if(command==0x206)p->stage=0;
 return 0;
}
static int copy(void *c,uint64_t address,unsigned bytes)
{Device *p=c;assert(p->stage==2 && address==MARBLE_ZAP_PHYS && bytes==0x1000);p->copies++;return p->copy_fail;}
static int health(void *c){Device *p=c;return ++p->health_n==p->health_at;}
int main(void)
{
 Device d={0};MarbleZapIo io={&d,call,copy,health};MarbleZapResult r;
 assert(marble_zap_transaction(&io,UINT64_C(0x843000000),&r));
 assert(d.n==8 && r.available==4 && r.authenticated && !r.unloaded && r.retain);
 assert(!marble_zap_shutdown(&io,&r,0) && d.n==8);
 assert(marble_zap_shutdown(&io,&r,1) && r.unloaded && !r.retain && d.n==9);
 for(unsigned n=1;n<=9;n++)for(unsigned wrong=0;wrong<2;wrong++) {
  d=(Device){0};if(wrong)d.bad_answer=n;else d.fail=n;
  int loaded=marble_zap_transaction(&io,UINT64_C(0x843000000),&r);
  assert(loaded==(n==5 || n==9));
  if(n<=4)assert(!d.copies && !r.attempted);
  else {assert(r.retain && !r.unloaded);
    assert(marble_zap_shutdown(&io,&r,1)==(n!=9));
    assert(r.retain==(n==9) && r.unloaded==(n!=9));}
 }
 d=(Device){.copy_fail=1};assert(!marble_zap_transaction(&io,UINT64_C(0x843000000),&r) && r.retain && !r.authenticated);
 assert(marble_zap_shutdown(&io,&r,1));
 for(unsigned n=1;n<=6;n++){d=(Device){.health_at=n};assert(!marble_zap_transaction(&io,UINT64_C(0x843000000),&r));}
 d=(Device){0};assert(!marble_zap_transaction(&io,0x1234,&r) && !d.n);
 puts("PAS13 fixture: all SCM failures/results, copy and health errors; no copy before MEM_SETUP, mandatory shutdown/retain on uncertainty PASS; no hardware claim");
}
