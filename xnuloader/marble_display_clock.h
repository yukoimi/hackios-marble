/* Balanced firmware-owned display register-bus clock references.
 * No clock rates, power domains, panel settings, PLLs or voltages changed. */
#ifndef XNULOADER_MARBLE_DISPLAY_CLOCK_H
#define XNULOADER_MARBLE_DISPLAY_CLOCK_H
#include <stdint.h>
typedef struct {
    void *ctx;
    int (*id)(void *,const char *,uint64_t *);
    int (*state)(void *,uint64_t,unsigned *,unsigned *);
    int (*enable)(void *,uint64_t);
    int (*disable)(void *,uint64_t);
} MarbleClockOps;
typedef struct {
    uint64_t ids[3];
    unsigned enabled[3],on[3],held,uncertain,prepared,active,dirty;
} MarbleDisplayClock;
static const char *const marble_display_bus_clocks[]={
    "gcc_disp_ahb_clk","disp_cc_mdss_non_gdsc_ahb_clk","disp_cc_mdss_ahb_clk"};

static int marble_display_clock_prepare(const MarbleClockOps *o,MarbleDisplayClock *p)
{
    if(p->held || p->dirty || p->active)return 0;
    p->prepared=0;
    for(unsigned i=0;i<3;i++)
        if(o->id(o->ctx,marble_display_bus_clocks[i],&p->ids[i]) ||
           o->state(o->ctx,p->ids[i],&p->enabled[i],&p->on[i]) ||
           p->enabled[i]>1 || p->on[i]>1)return 0;
    p->prepared=1;return 1;
}
static int marble_display_clock_restore(const MarbleClockOps *o,MarbleDisplayClock *p)
{
    int ok=1;
    for(int i=2;i>=0;i--)if(p->held&(1u<<i)) {
        if(o->disable(o->ctx,p->ids[i]))ok=0;
        else p->held&=~(1u<<i);
    }
    for(unsigned i=0;i<3;i++) {
        unsigned enabled=2,on=2;
        if(o->state(o->ctx,p->ids[i],&enabled,&on) ||
           enabled!=p->enabled[i] || on!=p->on[i])ok=0;
    }
    p->active=p->prepared=0;p->dirty=!ok || !!p->held || !!p->uncertain;
    return !p->dirty;
}
static int marble_display_clock_acquire(const MarbleClockOps *o,MarbleDisplayClock *p)
{
    if(!p->prepared || p->active || p->dirty || p->held)return 0;
    for(unsigned i=0;i<3;i++) {
        unsigned enabled=2,on=2;
        if(o->state(o->ctx,p->ids[i],&enabled,&on) ||
           enabled!=p->enabled[i] || on!=p->on[i])return 0;
    }
    for(unsigned i=0;i<3;i++) {
        unsigned enabled=2,on=2;
        /* An unsuccessful Enable has ambiguous reference ownership. Do not
         * balance it blindly and risk dropping another client's reference. */
        if(o->enable(o->ctx,p->ids[i])) {
            p->uncertain|=1u<<i;marble_display_clock_restore(o,p);return 0;
        }
        p->held|=1u<<i;
        /* A voted clock can be physically on with its local enable bit zero.
         * EnableClock grants our balanced firmware reference; IsOn is the
         * prerequisite for register access. Preserve both values on release. */
        if(o->state(o->ctx,p->ids[i],&enabled,&on) || enabled>1 || on!=1) {
            marble_display_clock_restore(o,p);return 0;
        }
    }
    p->active=1;return 1;
}
#endif
