#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_aw2032_analog_fifo.h"
typedef struct {
    unsigned address,calls,fail_at,analog,writes,us,hooked;
    uint16_t ctrl,mute;
    MarbleAwAnalogResult probe;
} Model;
static int read_reg(void *ctx,unsigned a,unsigned reg,uint16_t *v)
{
    Model *m=ctx;assert(a==m->address && (reg==1 || reg==4 || reg==5));
    if(++m->calls==m->fail_at)return 1;
    *v=reg==4?m->ctrl:reg==5?m->mute:(m->ctrl&2?0x11:0x311);return 0;
}
static int write_reg(void *ctx,unsigned a,unsigned reg,uint16_t v)
{
    Model *m=ctx;assert(a==m->address);
    assert(m->analog?marble_aw2032_analog_allowed(a,reg,v):marble_aw2032_digital_allowed(a,reg,v));
    m->ctrl=v;m->writes++;return ++m->calls==m->fail_at;
}
static void delay(void *ctx,unsigned us)
{Model *m=ctx;assert(us==(m->analog?1000:2000));m->us+=us;}
static int analog(void *ctx,unsigned a)
{
    Model *m=ctx;assert(m->ctrl==(a==0x34?0xb442:0x3442));
    m->analog=1;m->hooked++;
    MarbleAwDigitalOps o={ctx,read_reg,write_reg,delay};
    int ok=marble_aw2032_analog_cycle(&o,a,&m->probe);m->analog=0;return ok;
}
static Model fixture(unsigned a)
{return (Model){.address=a,.ctrl=a==0x34?0xb443:0x3443,.mute=0xf039};}
int main(void)
{
    for(unsigned a=0x34;a<=0x35;a++) {
        unsigned digital=a==0x34?0xb442:0x3442,down=digital|1;
        for(unsigned v=0;v<65536;v++) {
            uint32_t word=marble_aw2032_register_word(4,v);
            assert(marble_aw2032_analog_fifo_allowed(MIF_TX,word,a,4,v,0)==(v==digital || v==(digital&~2u)));
        }
        for(unsigned reg=0;reg<128;reg++)assert(marble_aw2032_analog_fifo_allowed(MIF_TX,reg,a,reg,0,1)==(reg==1 || reg==4 || reg==5));
        assert(!marble_aw2032_analog_fifo_allowed(MIF_TX_LEN,2,a,4,digital,0));
        assert(!marble_aw2032_analog_fifo_allowed(MIF_CMD,(2u<<27)|(a<<9),a,4,digital,0));
        assert(!marble_aw2032_analog_fifo_allowed(MIF_TX,4,0x33,4,digital,0));
        assert(!marble_aw2032_analog_fifo_allowed(MIF_TX,4,a,4,digital,2));
        Model m=fixture(a);MarbleAwDigitalOps o={&m,read_reg,write_reg,delay,analog};MarbleAwDigitalResult r={0};
        assert(marble_aw2032_digital_cycle(&o,a,&r));
        assert(m.hooked==1 && m.probe.ready && m.probe.restored && r.restored && !r.dirty && m.ctrl==down && m.mute==0xf039 && m.writes==4 && m.us==3000);
        unsigned calls=m.calls;
        for(unsigned f=1;f<=calls;f++) {
            m=fixture(a);m.fail_at=f;r=(MarbleAwDigitalResult){0};
            assert(!marble_aw2032_digital_cycle(&o,a,&r));
            assert(m.ctrl==down && m.mute==0xf039 && m.writes<=4 && m.us<=3000);
        }
    }
    puts("PASS nested digital/muted analog lifecycle, every callback failure, exact independent FIFO whitelist and final PWDN restoration; host model only");
}
