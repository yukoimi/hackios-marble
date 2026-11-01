#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_rpmh.h"
typedef struct {
    uint32_t regs[64],saved[7];
    unsigned reads,writes,fail_read,fail_write,timeout,issued,us,lost,badstatus,unstable;
} Device;
static Device d;
static const uint32_t addresses[4]={0x30000,0x30010,0x30020,0x30030};
static int read_reg(void *cookie,unsigned reg,uint32_t *out)
{
    Device *v=cookie;assert(marble_rpmh_access(reg,4,0));
    if(++v->reads==v->fail_read)return -1;
    *out=v->regs[reg/4];return 0;
}
static int write_reg(void *cookie,unsigned reg,uint32_t data)
{
    Device *v=cookie;assert(marble_rpmh_access(reg,4,1));
    if(++v->writes==v->fail_write)return -1;
    if(reg==8) {assert(data==1);v->regs[1]&=~1u;return 0;}
    v->regs[reg/4]=data;
    if(reg==0x14 && data==0x1010000) {
        assert(v->regs[0x30/4]==0x108); /* never WRITE */
        assert(v->regs[0x10/4]==1 && v->regs[0x1c/4]==1);
        assert(((v->regs[0x34/4]&0xffff0003)==0x30000 || (v->regs[0x34/4]&0xffff0003)==0x50000) && v->regs[0x38/4]==0);
        v->issued++;
        if(v->timeout)v->regs[0x18/4]=0;
        else {
            if(v->regs[0]&1)v->regs[1]|=1;
            v->regs[0x3c/4]=v->badstatus?0x100:0x10100;
            v->regs[0x40/4]=(v->regs[0x34/4]&0xff)/16+(v->unstable?v->issued:0);
        }
    }
    return 0;
}
static void delay(void *cookie,unsigned us) {Device *v=cookie;v->us+=us;assert(v->us<=20010);}
static int guard(void *cookie) {Device *v=cookie;return v->lost && v->issued;}
static void initial(void)
{
    memset(&d,0,sizeof(d));d.regs[0x18/4]=1;d.regs[0]=0x54;
    d.regs[0x1c/4]=7;d.regs[0x10/4]=7;d.regs[0x30/4]=0x10108;d.regs[0x34/4]=0x41a00;d.regs[0x38/4]=1800;
    for(unsigned i=0;i<7;i++)d.saved[i]=d.regs[marble_rpmh_saved[i]/4];
}
static void restored(void)
{for(unsigned i=0;i<7;i++)assert(d.regs[marble_rpmh_saved[i]/4]==d.saved[i]);}
int main(void)
{
    MarbleRpmhIo io={&d,read_reg,write_reg,delay,guard};MarbleRpmhResult r;
    initial();assert(marble_rpmh_snapshot(&io,addresses,&r));restored();
    unsigned writes=d.writes,reads=d.reads;assert(r.completed==8 && r.restored && !r.retain);
    for(unsigned i=1;i<=writes;i++) {
        initial();d.fail_write=i;assert(!marble_rpmh_snapshot(&io,addresses,&r));
        if(r.restored)restored();else assert(r.retain || !r.changed);
    }
    for(unsigned i=1;i<=reads;i++) {
        initial();d.fail_read=i;assert(!marble_rpmh_snapshot(&io,addresses,&r));
        if(r.restored)restored();else assert(r.retain || !r.changed);
    }
    initial();uint32_t bcm[4]={0x50038,0x50004,0x50000,0x50058};
    assert(marble_rpmh_snapshot_class(&io,bcm,5,&r));restored();
    initial();assert(!marble_rpmh_snapshot_class(&io,bcm,4,&r) && r.refused && !d.writes);
    initial();d.timeout=1;assert(!marble_rpmh_snapshot(&io,addresses,&r) && r.retain && d.issued==1);
    initial();d.lost=1;assert(!marble_rpmh_snapshot(&io,addresses,&r) && r.retain);
    initial();d.badstatus=1;assert(!marble_rpmh_snapshot(&io,addresses,&r) && r.restored);restored();
    initial();d.unstable=1;assert(!marble_rpmh_snapshot(&io,addresses,&r) && r.restored);restored();
    initial();d.regs[0x14/4]=0x1010000;assert(!marble_rpmh_snapshot(&io,addresses,&r) && r.refused && !d.writes);
    initial();d.regs[1]=1;assert(!marble_rpmh_snapshot(&io,addresses,&r) && r.refused && !d.writes);
    initial();uint32_t bad[4]={0x40000,0x30010,0x30020,0x30030};
    assert(!marble_rpmh_snapshot(&io,bad,&r) && r.refused && !d.writes);
    bad[0]=bad[1];assert(!marble_rpmh_snapshot(&io,bad,&r) && r.refused && !d.writes);
    for(unsigned i=0;i<0x100;i++)assert(!marble_rpmh_access(i,8,1));
    assert(!marble_rpmh_access(4,4,1) && !marble_rpmh_access(0x3c,4,1) && !marble_rpmh_access(0x40,4,1));
    printf("RPMh ARC read lease: %u write/%u read fault points, bounded timeout, ownership, response/restore and no WRITE packets PASS\n",writes,reads);
    return 0;
}
