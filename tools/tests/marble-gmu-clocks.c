#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_clocks.h"
typedef struct {
    uint32_t regs[2][0x80000/4],saved[13];
    unsigned reads,writes,fail_read,fail_write,timeout,guard_lost,us;
} Device;
static Device d;
static int work_status,work_calls;
static int work(void *c)
{
    Device *v=c;work_calls++;
    assert(v->regs[1][0x931c/4]==0x100602 && v->regs[1][0x93f0/4]==0x100603);
    return work_status;
}
static int read_reg(void *cookie,unsigned block,unsigned reg,uint32_t *value)
{
    Device *v=cookie;assert(marble_clock_access(block,reg,4,0));
    if(++v->reads==v->fail_read)return -1;
    *value=v->regs[block][reg/4];return 0;
}
static int write_reg(void *cookie,unsigned block,unsigned reg,uint32_t value)
{
    Device *v=cookie;assert(marble_clock_access(block,reg,4,1));
    if(++v->writes==v->fail_write)return -1;
    v->regs[block][reg/4]=value;
    if(block==1 && (reg==0x9318 || reg==0x93ec) && (value&1) && v->timeout!=1)v->regs[block][reg/4]&=~1u;
    if(block==0 && reg==0x72038) {
        if(value&0x200000)v->regs[0][0x7203c/4]=0;
        if((value&0x100000) && v->timeout!=2) {
            unsigned hz=(v->regs[1][0x9564/4]&255)==0x1e?200000000:150000000;
            v->regs[0][0x7203c/4]=0x2000000|(uint32_t)((uint64_t)hz*(value&0xfffff)/(4*4800000));
        }
    }
    return 0;
}
static void wait_us(void *cookie,unsigned us)
{Device *v=cookie;v->us+=us;assert(v->us<250000);}
static int guard(void *cookie){Device *v=cookie;return v->guard_lost && v->writes>=v->guard_lost;}
static void initial(void)
{
    memset(&d,0,sizeof(d));d.regs[0][0x62000/4]=0x123400;d.regs[0][0x72000/4]=0x3f;
    for(unsigned i=0;i<13;i++)d.saved[i]=d.regs[marble_clock_block[i]][marble_clock_reg[i]/4];
}
static void restored(void)
{for(unsigned i=0;i<13;i++)assert(d.saved[i]==d.regs[marble_clock_block[i]][marble_clock_reg[i]/4]);}
int main(void)
{
    MarbleClockIo io={&d,read_reg,write_reg,wait_us,guard};MarbleClockResult r;
    initial();assert(marble_clock_transaction(&io,&r));restored();
    unsigned writes=d.writes,reads=d.reads;assert(r.measured && r.restored && !r.retain);
    for(unsigned i=1;i<=writes;i++) {
        initial();d.fail_write=i;assert(!marble_clock_transaction(&io,&r));
        if(r.restored)restored();else assert(r.retain || !r.changed);
    }
    for(unsigned i=1;i<=reads;i++) {
        initial();d.fail_read=i;assert(!marble_clock_transaction(&io,&r));
        if(r.restored)restored();else assert(r.retain || !r.changed);
    }
    initial();d.timeout=1;assert(!marble_clock_transaction(&io,&r) && r.retain);
    initial();d.timeout=2;assert(!marble_clock_transaction(&io,&r) && r.restored);restored();
    initial();d.guard_lost=2;assert(!marble_clock_transaction(&io,&r) && r.retain);
    initial();d.regs[1][0x931c/4]=0x602;assert(!marble_clock_transaction(&io,&r) && r.refused && !d.writes);
    initial();d.regs[0][0x72038/4]=0x100000;assert(!marble_clock_transaction(&io,&r) && r.refused && !d.writes);
    initial();assert(marble_clock_transaction_work(&io,&r,work,&d));assert(work_calls==1);restored();
    initial();work_status=-1;assert(!marble_clock_transaction_work(&io,&r,work,&d) && r.restored);restored();
    initial();work_status=-2;assert(!marble_clock_transaction_work(&io,&r,work,&d) && r.retain && !r.restored);
    assert(d.regs[1][0x931c/4]==0x100602 && d.regs[1][0x93f0/4]==0x100603);
    for(unsigned block=0;block<3;block++)for(unsigned at=0;at<0x80000;at+=4) {
        assert(!marble_clock_access(block,at,8,1));
        unsigned found=0;for(unsigned i=0;i<13;i++)found|=marble_clock_block[i]==block && marble_clock_reg[i]==at;
        assert(marble_clock_access(block,at,4,1)==found);
    }
    printf("GMU clocks: %u write + %u read failure points, bounded update/counter timeouts, lost reset guard, ownership refusal, exact MMIO fence PASS\n",writes,reads);
    return 0;
}
