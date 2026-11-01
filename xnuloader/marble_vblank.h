/* marble Android DT: mdp_phys=0xae00000, DSI INTF offsets 0x36000/0x37000.
 * Read-only counters from the pinned Qualcomm sde_hw_intf.c. No clocks,
 * interrupt masks, scanout addresses or panel commands are written here. */
#ifndef XNULOADER_MARBLE_VBLANK_H
#define XNULOADER_MARBLE_VBLANK_H
#include <stdint.h>
typedef int (*MarbleVBlankRead)(void *,uint64_t,uint32_t *);
typedef struct {
    uint64_t base;
    uint32_t enabled,count_enabled,frame,line,hsync,period,start,end,status;
} MarbleVBlankSample;
typedef struct {
    uint64_t last_tick,first_tick,minimum_period,maximum_period;
    uint32_t last_frame,edges,missed,invalid,initialized,anchored;
} MarbleVBlankClock;

static int marble_vblank_read(MarbleVBlankRead read,void *ctx,unsigned dsi,MarbleVBlankSample *s)
{
    if(dsi>1)return 0;
    s->base=0xae36000ULL+(uint64_t)dsi*0x1000;
    static const unsigned offsets[]={0,0xa8,0xac,0xb0,8,0xc,0x1c,0x24,0x26c};
    uint32_t values[9];
    for(unsigned i=0;i<9;i++)if(read(ctx,s->base+offsets[i],&values[i]))return 0;
    s->enabled=values[0];s->count_enabled=values[1];s->frame=values[2];
    s->line=values[3]&0xffff;s->hsync=values[4];s->period=values[5];
    s->start=values[6];s->end=values[7];s->status=values[8];
    uint32_t horizontal_period=s->hsync>>16;
    /* Firmware leaves COUNT_EN=0 on marble even while the status/counters
     * run. Qualcomm's v1 get_status checks INTF_STATUS, not COUNT_EN.
     * A sane timing snapshot alone never qualifies a refresh source: the
     * observer must still see multiple actual, correctly spaced edges. */
    return s->enabled==1 && (s->status&1) && horizontal_period &&
        s->period>horizontal_period && s->start<s->end && s->end<s->period;
}

static int marble_vblank_observe(MarbleVBlankClock *c,const MarbleVBlankSample *s,
    int valid,uint64_t tick,uint64_t frequency)
{
    if(!valid || !frequency){c->invalid++;return 0;}
    if(!c->initialized){c->initialized=1;c->last_tick=c->first_tick=tick;c->last_frame=s->frame;return 0;}
    uint32_t frames=s->frame-c->last_frame;
    if(!frames)return 0;
    uint64_t elapsed=tick-c->last_tick;
    c->last_frame=s->frame;c->last_tick=tick;
    /* The first edge can arrive partway through the initial sampled frame. */
    if(!c->anchored){c->anchored=1;return 0;}
    if(frames!=1 || elapsed<frequency/150 || elapsed>frequency/40){
        c->missed++;return 0;
    }
    if(!c->edges || elapsed<c->minimum_period)c->minimum_period=elapsed;
    if(elapsed>c->maximum_period)c->maximum_period=elapsed;
    c->edges++;return 1;
}
#endif
