#ifndef MARBLE_AW2032_HELD_H
#define MARBLE_AW2032_HELD_H
/* Reuse the qualified digital/analog register whitelist, but split the
 * lifecycle so DATA can run while both devices remain muted and powered.
 * Pure primitive only: no firmware binding or enabled hardware profile yet.
 * The caller must keep reset, Music/OTP, I2C and clock leases until finish. */
#include "marble_aw2032_analog.h"
typedef struct {
    unsigned attempted,digital_attempted,analog_attempted,active,dirty,failed,finish_attempted,restored;
    unsigned address,writes,digital_polls,analog_polls;
    uint64_t begin_us,held_us;
    uint16_t status;
} MarbleAwHeld;
static int marble_aw2032_held_ops(const MarbleAwDigitalOps *o,unsigned address,uint64_t (*now_us)(void *))
{return o && o->read && o->write && o->delay_us && now_us && (address==0x34 || address==0x35);}
static int marble_aw2032_held_check(const MarbleAwDigitalOps *o,unsigned address,uint64_t (*now_us)(void *),MarbleAwHeld *s)
{
    if(!s || !marble_aw2032_held_ops(o,address,now_us) || s->address!=address || !s->active || s->finish_attempted || s->failed)return 0;
    uint16_t ctrl=0,mute=0;uint16_t held=(address==0x34?0xb443:0x3443)&~3u;
    uint64_t now=now_us(o->ctx);s->held_us=now>=s->begin_us?now-s->begin_us:~0ULL;
    int ok=s->held_us<600000 && !o->read(o->ctx,address,4,&ctrl) && ctrl==held &&
        !o->read(o->ctx,address,5,&mute) && mute==0xf039 &&
        !o->read(o->ctx,address,1,&s->status) && (s->status&0xcfbb)==0x311;
    if(!ok)s->failed=1;
    return ok;
}
static int marble_aw2032_held_begin(const MarbleAwDigitalOps *o,unsigned address,uint64_t (*now_us)(void *),MarbleAwHeld *s)
{
    if(!s || !marble_aw2032_held_ops(o,address,now_us) || s->attempted || s->finish_attempted)return 0;
    s->attempted=1;s->address=address;uint16_t down=address==0x34?0xb443:0x3443,ctrl=0,mute=0;
    if(o->read(o->ctx,address,4,&ctrl) || ctrl!=down || o->read(o->ctx,address,5,&mute) || mute!=0xf039) {s->failed=1;return 0;}
    s->begin_us=now_us(o->ctx);s->dirty=1;s->digital_attempted=1;s->writes++;
    if(o->write(o->ctx,address,4,down&~1u) || o->read(o->ctx,address,4,&ctrl) || ctrl!=(down&~1u) ||
       o->read(o->ctx,address,5,&mute) || mute!=0xf039) {s->failed=1;return 0;}
    unsigned locked=0;
    for(unsigned i=0;i<10;i++) {
        o->delay_us(o->ctx,2000);s->digital_polls++;
        if(o->read(o->ctx,address,1,&s->status) || (s->status&0xcc8a))break;
        if((s->status&0xccbb)==0x11){locked=1;break;}
    }
    if(!locked){s->failed=1;return 0;}
    s->analog_attempted=1;s->writes++;
    if(o->write(o->ctx,address,4,down&~3u) || o->read(o->ctx,address,4,&ctrl) || ctrl!=(down&~3u) ||
       o->read(o->ctx,address,5,&mute) || mute!=0xf039){s->failed=1;return 0;}
    unsigned ready=0;
    for(unsigned i=0;i<10;i++) {
        o->delay_us(o->ctx,1000);s->analog_polls++;
        if(o->read(o->ctx,address,1,&s->status) || (s->status&0xcc8a))break;
        if((s->status&0xcfbb)==0x311){ready=1;break;}
    }
    if(!ready){s->failed=1;return 0;}
    s->active=1;return marble_aw2032_held_check(o,address,now_us,s);
}
static int marble_aw2032_held_finish(const MarbleAwDigitalOps *o,unsigned address,uint64_t (*now_us)(void *),MarbleAwHeld *s)
{
    if(!s || !marble_aw2032_held_ops(o,address,now_us) || (s->attempted && s->address!=address))return 0;
    if(s->finish_attempted)return s->restored;
    s->finish_attempted=1;s->active=0;
    if(!s->dirty){s->restored=1;return 1;}
    if(!s->digital_attempted){s->failed=1;return 0;}
    uint16_t down=address==0x34?0xb443:0x3443,ctrl=0,mute=0;unsigned analog_off=1;
    if(s->analog_attempted) {
        s->writes++;
        analog_off=!o->write(o->ctx,address,4,down&~1u) && !o->read(o->ctx,address,4,&ctrl) && ctrl==(down&~1u) &&
            !o->read(o->ctx,address,5,&mute) && mute==0xf039;
    }
    /* Always attempt digital power-down even if the first restore failed. */
    s->writes++;
    unsigned down_ok=!o->write(o->ctx,address,4,down) && !o->read(o->ctx,address,4,&ctrl) && ctrl==down &&
        !o->read(o->ctx,address,5,&mute) && mute==0xf039;
    uint64_t now=now_us(o->ctx);s->held_us=now>=s->begin_us?now-s->begin_us:~0ULL;
    s->restored=analog_off && down_ok;s->dirty=!s->restored;
    if(!s->restored || s->held_us>=600000)s->failed=1;
    return s->restored;
}
#endif
