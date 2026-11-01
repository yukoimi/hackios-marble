#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_aw2032_held.h"
typedef struct {unsigned address,reads,writes,fail_read,fail_write,apply_error,status_fault;uint16_t ctrl,mute;uint64_t us;} Model;
static int read_reg(void *ctx,unsigned address,unsigned reg,uint16_t *value)
{
    Model *m=ctx;assert(address==m->address && (reg==1 || reg==4 || reg==5));m->reads++;
    if(m->reads==m->fail_read)return 1;
    *value=reg==4?m->ctrl:reg==5?m->mute:m->status_fault?8:(m->ctrl&3)==0?0x311:(m->ctrl&3)==2?0x11:0;
    return 0;
}
static int write_reg(void *ctx,unsigned address,unsigned reg,uint16_t value)
{
    Model *m=ctx;assert(address==m->address && reg==4);
    assert(marble_aw2032_digital_allowed(address,reg,value) || marble_aw2032_analog_allowed(address,reg,value));m->writes++;
    unsigned error=m->writes==m->fail_write;
    if(!error || m->apply_error)m->ctrl=value;
    return error;
}
static void delay(void *ctx,unsigned us){Model *m=ctx;assert(us==1000 || us==2000);m->us+=us;}
static uint64_t now(void *ctx){return ((Model *)ctx)->us;}
static Model model(unsigned address)
{Model m={0};m.address=address;m.ctrl=address==0x34?0xb443:0x3443;m.mute=0xf039;m.us=5000;return m;}
static MarbleAwDigitalOps ops(Model *m)
{MarbleAwDigitalOps o={m,read_reg,write_reg,delay,0};return o;}
int main(void)
{
    for(unsigned address=0x34;address<=0x35;address++) {
        Model m=model(address);MarbleAwDigitalOps o=ops(&m);MarbleAwHeld s={0};
        assert(marble_aw2032_held_begin(&o,address,now,&s) && s.active && s.dirty && !s.restored && m.writes==2 && m.mute==0xf039);
        m.us+=50000;assert(marble_aw2032_held_check(&o,address,now,&s));
        assert(!marble_aw2032_held_finish(&o,address==0x34?0x35:0x34,now,&s) && s.active && m.writes==2);
        assert(marble_aw2032_held_finish(&o,address,now,&s) && !s.active && !s.dirty && s.restored && !s.failed && m.writes==4 && m.ctrl==(address==0x34?0xb443:0x3443));
        unsigned reads=m.reads;assert(marble_aw2032_held_finish(&o,address,now,&s) && m.writes==4 && reads==m.reads);
        assert(!marble_aw2032_held_begin(&o,address,now,&s));
        for(unsigned read_error=1;read_error<=15;read_error++) {
            m=model(address);o=ops(&m);memset(&s,0,sizeof(s));m.fail_read=read_error;
            int began=marble_aw2032_held_begin(&o,address,now,&s);int ended=marble_aw2032_held_finish(&o,address,now,&s);
            assert(!began || !ended);assert(s.failed && !s.active && m.mute==0xf039);
            assert(!ended || (!s.dirty && m.ctrl==(address==0x34?0xb443:0x3443)));
        }
        for(unsigned apply=0;apply<2;apply++)for(unsigned error=1;error<=4;error++) {
            m=model(address);o=ops(&m);memset(&s,0,sizeof(s));m.fail_write=error;m.apply_error=apply;
            int began=marble_aw2032_held_begin(&o,address,now,&s),ended=marble_aw2032_held_finish(&o,address,now,&s);
            assert(!began || !ended);assert(s.failed && !s.active && m.mute==0xf039);
            assert(!ended || !s.dirty);
        }
        for(unsigned problem=0;problem<3;problem++) {
            m=model(address);o=ops(&m);memset(&s,0,sizeof(s));assert(marble_aw2032_held_begin(&o,address,now,&s));
            if(problem==0)m.us=s.begin_us+600000;if(problem==1)m.status_fault=1;if(problem==2)m.mute=0;
            assert(!marble_aw2032_held_check(&o,address,now,&s) && s.failed);
            marble_aw2032_held_finish(&o,address,now,&s);assert(!s.active);
        }
        m=model(address);o=ops(&m);memset(&s,0,sizeof(s));assert(marble_aw2032_held_finish(&o,address,now,&s) && !m.writes);
        assert(!marble_aw2032_held_begin(&o,address,now,&s));
    }
    puts("PASS offline AW2032 held lifecycle: both ROM profiles, mute/whitelisted registers, separate DATA interval, device binding, every read/write failure and partial-write restoration, exactly-once finish, 600ms/status/mute guard; no firmware binding or sound claim");
}
