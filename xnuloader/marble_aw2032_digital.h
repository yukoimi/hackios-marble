/* Bounded digital-only primitive; firmware use requires the explicit qualified START window.
 * OEM aw882xx_device.c: PWDN off, 2ms settle, SYSST PLLS|CLKS polling.
 * Deliberately stops BEFORE AMPPD release/boost/unmute or CCO fallback. */
#ifndef MARBLE_AW2032_DIGITAL_H
#define MARBLE_AW2032_DIGITAL_H
#include <stdint.h>
typedef struct {
    void *ctx;
    int (*read)(void *,unsigned,unsigned,uint16_t *);
    int (*write)(void *,unsigned,unsigned,uint16_t);
    void (*delay_us)(void *,unsigned);
    int (*locked_probe)(void *,unsigned);
} MarbleAwDigitalOps;
typedef struct {
    unsigned attempted,writes,polls,locked,restored,dirty;
    uint16_t sysctrl,sysctrl2,status;
} MarbleAwDigitalResult;
static int marble_aw2032_digital_allowed(unsigned address,unsigned reg,uint16_t value)
{
    if(address!=0x34 && address!=0x35)return 0;
    uint16_t down=address==0x34?0xb443:0x3443;
    return reg==4 && (value==down || value==(uint16_t)(down&~1u));
}
static int marble_aw2032_digital_cycle(const MarbleAwDigitalOps *o,unsigned address,MarbleAwDigitalResult *r)
{
    if(!o || !r || !o->read || !o->write || !o->delay_us || r->attempted ||
       (address!=0x34 && address!=0x35))return 0;
    r->attempted=1;
    uint16_t down=address==0x34?0xb443:0x3443,ctrl=0,mute=0;
    if(o->read(o->ctx,address,4,&ctrl) || o->read(o->ctx,address,5,&mute) || ctrl!=down || mute!=0xf039)return 0;
    r->sysctrl=ctrl;r->sysctrl2=mute;
    /* A failed write can have reached the chip: always attempt restoration. */
    r->dirty=1;r->writes++;
    int started=!o->write(o->ctx,address,4,(uint16_t)(down&~1u));
    if(started)started=!o->read(o->ctx,address,4,&ctrl) && ctrl==(uint16_t)(down&~1u) &&
        !o->read(o->ctx,address,5,&mute) && mute==r->sysctrl2;
    if(started)for(unsigned i=0;i<10;i++) {
        o->delay_us(o->ctx,2000);r->polls++;
        if(o->read(o->ctx,address,1,&r->status) || (r->status&0xcc8au))break;
        if((r->status&0xccbbu)==0x11) {r->locked=1;break;}
    }
    if(started && r->locked && o->locked_probe)started=o->locked_probe(o->ctx,address);
    r->writes++;
    r->restored=!o->write(o->ctx,address,4,down) && !o->read(o->ctx,address,4,&ctrl) && ctrl==down &&
        !o->read(o->ctx,address,5,&mute) && mute==r->sysctrl2;
    r->dirty=!r->restored;
    return started && r->locked && r->restored;
}
#endif
