#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_boot.h"
typedef struct {
    uint32_t regs[4][0x30000/4],saved[MARBLE_BOOT_REGS];
    unsigned reads,writes,fail_read,fail_write,after_delivery,timeout,health_lost,us;
} Device;
static Device d;
static int rd(void *c,unsigned b,unsigned a,uint32_t *v)
{
    Device *p=c;assert(marble_boot_access(b,a,4,0));
    if(++p->reads==p->fail_read)return -1;
    *v=p->regs[b][a/4];return 0;
}
static int wr(void *c,unsigned b,unsigned a,uint32_t v)
{
    Device *p=c;assert(marble_boot_access(b,a,4,1));
    unsigned failed=++p->writes==p->fail_write;
    if(failed && !p->after_delivery)return -1;
    p->regs[b][a/4]=v;
    if(b==0 && a==0x16000 && !v) {
        assert(p->regs[0][0x1673c/4]==0x07030200);
        assert(p->regs[0][0x1664c/4]==0xffffffff);
        assert(p->regs[0][0x26c18/4]==0xffffffff);
        if(p->timeout!=1)p->regs[0][0x16070/4]=0x100;
        p->regs[0][0x16648/4]=4;p->regs[0][0x26c14/4]=8;
        if(p->timeout==3)p->regs[3][marble_boot_rsc[0]/4]=0;
    }
    if(b==1 && a==0xf114 && v==2 && p->timeout!=2)p->regs[1][0xf118/4]=2;
    for(unsigned i=0;i<2;i++)if(b==0 && a==marble_boot_irq_clear[i] && p->timeout!=4)
        p->regs[0][marble_boot_irq_status[i]/4]&=~v;
    return failed?-1:0;
}
static int health(void *c){Device *p=c;return p->health_lost && p->writes>=p->health_lost;}
static void wait_us(void *c,unsigned us){Device *p=c;p->us+=us;assert(p->us<250000);}
static void init(void)
{
    memset(&d,0,sizeof(d));d.regs[0][0x16000/4]=1;d.regs[1][0xf114/4]=8;
    for(unsigned i=0;i<4;i++)d.regs[3][marble_boot_rsc[i]/4]=1;
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)d.saved[i]=d.regs[marble_boot_blocks[i]][marble_boot_regs[i]/4];
}
static void restored(void)
{
    assert(d.regs[0][0x16000/4]==1);
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)assert(d.saved[i]==d.regs[marble_boot_blocks[i]][marble_boot_regs[i]/4]);
    for(unsigned i=0;i<2;i++)assert(!d.regs[0][marble_boot_irq_status[i]/4]);
}
static int boot_work(void *c)
{
    Device *p=c;assert(!p->regs[0][0x16000/4] && p->regs[0][0x16070/4]==0x100);
    p->regs[0][0x16618/4]=1;return p->timeout?-1:0;
}
int main(void)
{
    MarbleBootIo io={&d,rd,wr,wait_us,health};MarbleBootResult r;
    init();assert(marble_boot_transaction(&io,&r));restored();
    unsigned reads=d.reads,writes=d.writes;
    assert(r.fw_result==0x100 && r.last_ack==2 && !r.retain);
    for(unsigned delivered=0;delivered<2;delivered++)for(unsigned n=1;n<=writes;n++) {
        init();d.fail_write=n;d.after_delivery=delivered;
        assert(!marble_boot_transaction(&io,&r));
        if(r.restored)restored();else assert(r.retain || r.refused);
    }
    for(unsigned n=1;n<=reads;n++) {
        init();d.fail_read=n;assert(!marble_boot_transaction(&io,&r));
        if(r.restored)restored();else assert(r.retain || r.refused);
    }
    for(unsigned t=1;t<=4;t++) {
        init();d.timeout=t;assert(!marble_boot_transaction(&io,&r));
        if(t==1){assert(r.stopped && r.restored && !r.retain);restored();}
        else assert(r.retain);
    }
    init();d.health_lost=1;assert(!marble_boot_transaction(&io,&r) && r.retain);
    init();d.regs[0][0x16648/4]=1;assert(!marble_boot_transaction(&io,&r) && r.refused && !d.writes);
    init();d.regs[3][marble_boot_rsc[2]/4]=0;assert(!marble_boot_transaction(&io,&r) && r.refused && !d.writes);
    init();d.regs[0][0x16000/4]=0;assert(!marble_boot_transaction(&io,&r) && r.refused && !d.writes);
    init();d.regs[1][0xf114/4]=2;assert(!marble_boot_transaction(&io,&r) && r.refused && !d.writes);
    init();d.regs[1][0xf114/4]=0;d.saved[9]=0;assert(marble_boot_transaction(&io,&r));restored();
    init();assert(marble_boot_transaction_mode(&io,&r,0));restored();assert(!r.reset_released && !r.initialized && r.configured && r.stopped);
    init();assert(marble_boot_transaction_work(&io,&r,1,boot_work,&d));restored();assert(r.worked && r.work_ok);
    init();d.timeout=5;assert(!marble_boot_transaction_work(&io,&r,1,boot_work,&d));restored();assert(r.worked && !r.work_ok && !r.retain);
    init();d.regs[0][0x16618/4]=1;assert(!marble_boot_transaction_work(&io,&r,1,boot_work,&d) && r.refused && !r.worked);
    for(unsigned b=0;b<5;b++)for(unsigned a=0;a<0x30000;a+=4) {
        assert(!marble_boot_access(b,a,8,1));
        unsigned allowed=b==0 && (a==0x16000 || a==0x26c50 || a==0x16644 || a==0x26c10);
        for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)allowed|=b==marble_boot_blocks[i] && a==marble_boot_regs[i];
        assert(marble_boot_access(b,a,4,1)==!!allowed);
    }
    printf("GMU boot: %u writes (before/after delivery), %u reads, IRQ/RSC ownership, FW/GBIF/RSC/IRQ timeouts and MMIO fence PASS\n",writes,reads);
}
