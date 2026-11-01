#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_aw2032_analog.h"
typedef struct {unsigned address,calls,fail_at,writes,polls,ready_after,us,restore_fail;uint16_t ctrl,mute,fault;} Model;
static int read_reg(void *ctx,unsigned address,unsigned reg,uint16_t *v)
{
    Model *m=ctx;assert(address==m->address && (reg==1 || reg==4 || reg==5));
    if(++m->calls==m->fail_at)return 1;
    if(reg==4)*v=m->ctrl;else if(reg==5)*v=m->mute;
    else {*v=0x11|m->fault;if(!(m->ctrl&2) && ++m->polls>=m->ready_after)*v|=0x300;}
    return 0;
}
static int write_reg(void *ctx,unsigned address,unsigned reg,uint16_t value)
{
    Model *m=ctx;assert(address==m->address && marble_aw2032_analog_allowed(address,reg,value));
    assert(!(value&1));m->ctrl=value;m->writes++;
    return ++m->calls==m->fail_at || (m->restore_fail && (value&2));
}
static void delay(void *ctx,unsigned us){assert(us==1000);((Model *)ctx)->us+=us;}
static Model fixture(unsigned a){return (Model){.address=a,.ctrl=a==0x34?0xb442:0x3442,.mute=0xf039,.ready_after=1};}
int main(void)
{
    for(unsigned a=0x34;a<=0x35;a++) {
        unsigned digital=a==0x34?0xb442:0x3442;
        for(unsigned v=0;v<65536;v++)assert(marble_aw2032_analog_allowed(a,4,v)==(v==digital || v==(digital&~2u)));
        for(unsigned reg=0;reg<128;reg++)if(reg!=4)assert(!marble_aw2032_analog_allowed(a,reg,0));
        Model m=fixture(a);MarbleAwDigitalOps o={&m,read_reg,write_reg,delay};MarbleAwAnalogResult r={0};
        assert(marble_aw2032_analog_cycle(&o,a,&r) && r.restored && !r.dirty && m.writes==2 && m.us==1000);
        unsigned calls=m.calls;
        for(unsigned f=1;f<=calls;f++) {
            m=fixture(a);m.fail_at=f;r=(MarbleAwAnalogResult){0};assert(!marble_aw2032_analog_cycle(&o,a,&r));
            assert(m.writes<=2 && m.us<=10000 && m.ctrl==digital);
        }
        for(unsigned bit=0;bit<16;bit++)if((1u<<bit)&0xccaa) {
            m=fixture(a);m.fault=1u<<bit;r=(MarbleAwAnalogResult){0};assert(!marble_aw2032_analog_cycle(&o,a,&r));
        }
        m=fixture(a);m.ready_after=11;r=(MarbleAwAnalogResult){0};assert(!marble_aw2032_analog_cycle(&o,a,&r) && r.restored && !r.dirty && m.us==10000);
        m=fixture(a);m.restore_fail=1;r=(MarbleAwAnalogResult){0};assert(!marble_aw2032_analog_cycle(&o,a,&r) && r.dirty);
        m=fixture(a);m.mute^=16;r=(MarbleAwAnalogResult){0};assert(!marble_aw2032_analog_cycle(&o,a,&r) && !m.writes);
    }
    puts("PASS offline-only muted analog primitive: exact AMPPD writes, PLL precondition, HMUTE/90dB held, no faults, 10ms bound and callback restoration failures; not enabled on hardware");
}
