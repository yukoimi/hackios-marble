/* Explicit muted-only next stage. Starting from a locked digital
 * device, test AMPPD release while HMUTE/90dB stay exact. No boost tuning,
 * volume ramp, feedback enable or speaker-switch writes. */
#ifndef MARBLE_AW2032_ANALOG_H
#define MARBLE_AW2032_ANALOG_H
#include "marble_aw2032_digital.h"
typedef struct {unsigned attempted,writes,polls,ready,restored,dirty;uint16_t status;} MarbleAwAnalogResult;
static int marble_aw2032_analog_allowed(unsigned address,unsigned reg,uint16_t value)
{
    if(address!=0x34 && address!=0x35)return 0;
    uint16_t digital=address==0x34?0xb442:0x3442;
    return reg==4 && (value==digital || value==(uint16_t)(digital&~2u));
}
static int marble_aw2032_analog_cycle(const MarbleAwDigitalOps *o,unsigned address,MarbleAwAnalogResult *r)
{
    if(!o || !r || !o->read || !o->write || !o->delay_us || r->attempted || (address!=0x34 && address!=0x35))return 0;
    r->attempted=1;uint16_t digital=address==0x34?0xb442:0x3442,ctrl=0,mute=0,status=0;
    if(o->read(o->ctx,address,4,&ctrl) || ctrl!=digital || o->read(o->ctx,address,5,&mute) || mute!=0xf039 ||
       o->read(o->ctx,address,1,&status) || (status&0xccbb)!=0x11)return 0;
    r->dirty=1;r->writes++;
    int on=!o->write(o->ctx,address,4,(uint16_t)(digital&~2u));
    if(on)on=!o->read(o->ctx,address,4,&ctrl) && ctrl==(uint16_t)(digital&~2u) && !o->read(o->ctx,address,5,&mute) && mute==0xf039;
    if(on)for(unsigned i=0;i<10;i++) {
        o->delay_us(o->ctx,1000);r->polls++;
        if(o->read(o->ctx,address,1,&r->status) || (r->status&0xcc8a))break;
        if((r->status&0xcfbb)==0x311) {r->ready=1;break;}
    }
    r->writes++;
    r->restored=!o->write(o->ctx,address,4,digital) && !o->read(o->ctx,address,4,&ctrl) && ctrl==digital &&
        !o->read(o->ctx,address,5,&mute) && mute==0xf039;
    r->dirty=!r->restored;
    return on && r->ready && r->restored;
}
#endif
