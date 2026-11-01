#include "../../xnuloader/marble_cpucap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t regs[2][0x400/4],mirrors[2],writes;
static unsigned fail_write,fail_read,reads,fail_readback;
static int rd(void *ctx,uint64_t address,uint32_t *out)
{
    (void)ctx;
    if(++reads==fail_read)return -1;
    for(unsigned d=0;d<2;d++) {
        if(address==marble_cpucap_mirror(d)){*out=mirrors[d];return 0;}
        if(address>=marble_cpucap_base(d) && address<marble_cpucap_base(d)+0x400) {
            *out=regs[d][(address-marble_cpucap_base(d))/4];
            if(fail_readback && writes==fail_readback && (address&0xfff)==0x320)*out=39;
            return 0;
        }
    }
    assert(!"read outside big/prime whitelist");return -1;
}
static int wr(void *ctx,uint64_t address,uint32_t value)
{
    (void)ctx;
    if(++writes==fail_write)return -1;
    for(unsigned d=0;d<2;d++) {
        if(address==marble_cpucap_base(d)+0x320){regs[d][0x320/4]=value;return 0;}
        if(address==marble_cpucap_mirror(d)){mirrors[d]=value;return 0;}
    }
    assert(!"write outside four exact registers");return -1;
}
static void setup(MarbleCPUCap *p)
{
    static const unsigned b[]={33,40,46,52,58,63,69,75,81,86,92,98,104,110,116,122,130};
    static const unsigned q[]={41,48,54,61,67,73,80,86,92,98,104,111,117,123,129,135,141,147,152};
    memset(p,0,sizeof(*p));memset(regs,0,sizeof(regs));
    reads=writes=fail_read=fail_write=fail_readback=0;
    for(unsigned d=0;d<2;d++) {
        const unsigned *values=d?q:b;unsigned n=d?19:17;
        for(unsigned i=0;i<40;i++)regs[d][0x100/4+i]=0x40040000|values[i<n?i:n-1];
        regs[d][0x320/4]=d?17:15;mirrors[d]=0x12340000+d;
    }
}
static void restored(void)
{ assert(regs[0][0x320/4]==15 && regs[1][0x320/4]==17 && mirrors[0]==0x12340000 && mirrors[1]==0x12340001); }
int main(void)
{
    MarbleCPUCap p;
    setup(&p);assert(marble_cpucap_prepare(rd,0,&p));assert(writes==0);
    assert(p.row[0]==5 && p.row[1]==3 && p.khz[0]==1209600 && p.khz[1]==1171200);
    assert(marble_cpucap_apply(rd,wr,0,&p));assert(writes==4 && p.active && !p.dirty);
    assert(marble_cpucap_check(rd,0,&p));assert(!marble_cpucap_prepare(rd,0,&p));
    assert(marble_cpucap_restore(rd,wr,0,&p));assert(writes==8);restored();
    for(unsigned w=1;w<=4;w++) {
        setup(&p);assert(marble_cpucap_prepare(rd,0,&p));fail_write=w;
        assert(!marble_cpucap_apply(rd,wr,0,&p));assert(!p.active && !p.dirty);restored();
    }
    setup(&p);assert(marble_cpucap_prepare(rd,0,&p));fail_readback=2;
    assert(!marble_cpucap_apply(rd,wr,0,&p));restored();
    for(unsigned r=1;r<=42;r++) {
        setup(&p);fail_read=r;assert(!marble_cpucap_prepare(rd,0,&p));assert(!writes);
    }
    setup(&p);regs[1][0x100/4+8]^=1;
    assert(!marble_cpucap_prepare(rd,0,&p));assert(!writes);
    setup(&p);regs[0][0x100/4+5]^=1<<16;
    assert(!marble_cpucap_prepare(rd,0,&p));assert(!writes);
    setup(&p);regs[1][0x320/4]=40;
    assert(!marble_cpucap_prepare(rd,0,&p));assert(!writes);
    setup(&p);regs[1][0x100/4+19]--;
    assert(!marble_cpucap_prepare(rd,0,&p));assert(!writes);
    setup(&p);regs[0][0x320/4]=1;regs[1][0x320/4]=2;
    assert(marble_cpucap_prepare(rd,0,&p));assert(marble_cpucap_apply(rd,wr,0,&p));
    assert(!writes && p.row[0]==1 && p.row[1]==2);assert(marble_cpucap_restore(rd,wr,0,&p));
    setup(&p);assert(marble_cpucap_prepare(rd,0,&p));regs[1][0x320/4]--;
    assert(!marble_cpucap_apply(rd,wr,0,&p));assert(!writes);
    setup(&p);assert(marble_cpucap_prepare(rd,0,&p));assert(marble_cpucap_apply(rd,wr,0,&p));
    regs[1][0x320/4]++;assert(!marble_cpucap_check(rd,0,&p));
    fail_write=5;assert(!marble_cpucap_restore(rd,wr,0,&p));assert(p.dirty && p.changed);
    assert(!marble_cpucap_prepare(rd,0,&p));
    assert(marble_cpucap_restore(rd,wr,0,&p));assert(!p.dirty);restored();
    /* Firmware can change even a no-op domain. A restore may not claim PASS
     * just because there were no writes in the acquisition phase. */
    setup(&p);regs[0][0x320/4]=1;regs[1][0x320/4]=2;
    assert(marble_cpucap_prepare(rd,0,&p));assert(marble_cpucap_apply(rd,wr,0,&p));
    regs[1][0x320/4]=17;assert(!marble_cpucap_restore(rd,wr,0,&p));assert(p.dirty);
    puts("marble CPU cap: PASS complete LUT checks, only lower requests, exact write whitelist, stale snapshot refusal, partial failure rollback and dirty latch");
    return 0;
}
