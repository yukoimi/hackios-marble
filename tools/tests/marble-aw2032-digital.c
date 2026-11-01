#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_aw2032_digital.h"
#include "../../xnuloader/marble_aw2032_digital_fifo.h"
typedef struct {unsigned address,calls,fail_at,writes,us,lock_after,polls,bad_restore;uint16_t ctrl,mute,status;} Model;
static int read_reg(void *ctx,unsigned a,unsigned reg,uint16_t *v)
{
    Model *m=ctx;assert(a==m->address && (reg==1 || reg==4 || reg==5));
    if(++m->calls==m->fail_at)return 1;
    if(reg==4)*v=m->ctrl;else if(reg==5)*v=m->mute;
    else {*v=m->status;if(++m->polls>=m->lock_after)*v|=0x11;}
    return 0;
}
static int write_reg(void *ctx,unsigned a,unsigned reg,uint16_t v)
{
    Model *m=ctx;assert(a==m->address && marble_aw2032_digital_allowed(a,reg,v));
    assert(v&2);m->ctrl=v;m->writes++;
    return ++m->calls==m->fail_at || (m->bad_restore && (v&1));
}
static void delay(void *ctx,unsigned us){assert(us==2000);((Model *)ctx)->us+=us;}
static Model fixture(unsigned a){return (Model){.address=a,.ctrl=a==0x34?0xb443:0x3443,.mute=0xf039,.lock_after=1};}
static int reject_locked(void *ctx,unsigned a)
{Model *m=ctx;assert(a==m->address && m->ctrl==(a==0x34?0xb442:0x3442) && m->mute==0xf039);return 0;}
int main(void)
{
    for(unsigned a=0x34;a<=0x35;a++) {
        uint16_t down=a==0x34?0xb443:0x3443;
        for(unsigned v=0;v<65536;v++) {
            unsigned word=marble_aw2032_register_word(4,(uint16_t)v);
            assert(marble_aw2032_digital_fifo_allowed(MIF_TX,word,a,4,(uint16_t)v,0)==(v==down || v==(down&~1u)));
        }
        assert(marble_aw2032_digital_fifo_allowed(MIF_TX_LEN,3,a,4,down,0));
        assert(!marble_aw2032_digital_fifo_allowed(MIF_TX_LEN,2,a,4,down,0));
        assert(!marble_aw2032_digital_fifo_allowed(MIF_CMD,(2u<<27)|(a<<9),a,4,down,0));
        assert(!marble_aw2032_digital_fifo_allowed(MIF_TX,2,a,2,0,1));
        for(unsigned reg=0;reg<128;reg++)assert(marble_aw2032_digital_fifo_allowed(MIF_TX,reg,a,reg,0,1)==(reg==1 || reg==4 || reg==5));
        for(unsigned v=0;v<65536;v++)assert(marble_aw2032_digital_allowed(a,4,v)==
            (v==(a==0x34?0xb443u:0x3443u) || v==(a==0x34?0xb442u:0x3442u)));
        for(unsigned reg=0;reg<128;reg++)if(reg!=4)assert(!marble_aw2032_digital_allowed(a,reg,0));
        Model m=fixture(a);MarbleAwDigitalOps o={&m,read_reg,write_reg,delay};MarbleAwDigitalResult r={0};
        assert(marble_aw2032_digital_cycle(&o,a,&r) && r.restored && !r.dirty && r.locked && m.writes==2 && m.us==2000);
        unsigned calls=m.calls;
        for(unsigned f=1;f<=calls;f++) {
            m=fixture(a);m.fail_at=f;r=(MarbleAwDigitalResult){0};
            assert(!marble_aw2032_digital_cycle(&o,a,&r));assert(m.writes<=2 && m.us<=20000);
            if(m.writes)assert(m.ctrl==(a==0x34?0xb443:0x3443));
            if(f>=calls-2)assert(r.dirty); /* restoration callback/readback refused */
        }
        m=fixture(a);m.lock_after=11;r=(MarbleAwDigitalResult){0};
        assert(!marble_aw2032_digital_cycle(&o,a,&r) && r.restored && !r.dirty && m.us==20000);
        for(unsigned bit=0;bit<16;bit++)if((1u<<bit)&0xccaa) {
            m=fixture(a);m.status=1u<<bit;r=(MarbleAwDigitalResult){0};
            assert(!marble_aw2032_digital_cycle(&o,a,&r) && r.restored && !r.dirty);
        }
        m=fixture(a);m.mute^=16;r=(MarbleAwDigitalResult){0};assert(!marble_aw2032_digital_cycle(&o,a,&r) && !m.writes);
        m=fixture(a);m.ctrl^=2;r=(MarbleAwDigitalResult){0};assert(!marble_aw2032_digital_cycle(&o,a,&r) && !m.writes);
        m=fixture(a);m.bad_restore=1;r=(MarbleAwDigitalResult){0};assert(!marble_aw2032_digital_cycle(&o,a,&r) && r.dirty);
        m=fixture(a);r=(MarbleAwDigitalResult){0};o.locked_probe=reject_locked;
        assert(!marble_aw2032_digital_cycle(&o,a,&r) && r.locked && r.restored && !r.dirty && m.writes==2);
    }
    assert(!marble_aw2032_digital_allowed(0x33,4,0xb442));
    puts("PASS offline digital PLL: exact ROM controls, AMPPD/HMUTE/90dB held, bounded polling, fault refusal and restoration failures; host model only; hardware acceptance not claimed");
}
