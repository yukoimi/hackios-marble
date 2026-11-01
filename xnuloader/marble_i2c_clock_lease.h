/* Balanced five-clock QUP2 lease. No frequency requests or controller access. */
#ifndef MARBLE_I2C_CLOCK_LEASE_H
#define MARBLE_I2C_CLOCK_LEASE_H
#include <stdint.h>
typedef struct {
    void *ctx;
    int (*state)(void *,unsigned,unsigned *,unsigned *,uint32_t *);
    int (*enable)(void *,unsigned);
    int (*disable)(void *,unsigned);
} MarbleI2cClockOps;
typedef struct {
    unsigned enabled[5],on[5],held,uncertain,prepared,active,dirty;
    uint32_t hz[5];
} MarbleI2cClockLease;
static int marble_i2c_clocks_prepare(const MarbleI2cClockOps *o,MarbleI2cClockLease *p)
{
    if(p->active || p->held || p->uncertain || p->dirty || p->prepared)return 0;
    for(unsigned i=0;i<5;i++)
        if(o->state(o->ctx,i,&p->enabled[i],&p->on[i],&p->hz[i]) ||
           p->enabled[i]>1 || p->on[i]>1)return 0;
    p->prepared=1;return 1;
}
static int marble_i2c_clocks_restore(const MarbleI2cClockOps *o,MarbleI2cClockLease *p)
{
    if(!p->prepared)return 0;
    int ok=1;
    for(int i=4;i>=0;i--)if(p->held&(1u<<i)) {
        if(o->disable(o->ctx,(unsigned)i))ok=0;
        else p->held&=~(1u<<i);
    }
    for(unsigned i=0;i<5;i++) {
        unsigned enabled=2,on=2;uint32_t hz=~0u;
        if(o->state(o->ctx,i,&enabled,&on,&hz) || enabled!=p->enabled[i] ||
           on!=p->on[i] || hz!=p->hz[i])ok=0;
    }
    p->active=p->prepared=0;p->dirty=!ok || !!p->held || !!p->uncertain;
    return !p->dirty;
}
static int marble_i2c_clocks_acquire(const MarbleI2cClockOps *o,MarbleI2cClockLease *p)
{
    if(!p->prepared || p->active || p->dirty || p->held || p->uncertain)return 0;
    for(unsigned i=0;i<5;i++) {
        unsigned enabled=2,on=2;uint32_t hz=~0u;
        if(o->state(o->ctx,i,&enabled,&on,&hz) || enabled!=p->enabled[i] ||
           on!=p->on[i] || hz!=p->hz[i])return 0;
    }
    for(unsigned i=0;i<5;i++) {
        unsigned enabled=2,on=2;uint32_t hz=~0u;
        /* A failed enable may already have acquired a reference. Never
         * blindly disable that ambiguous reference; require a cold reset. */
        if(o->enable(o->ctx,i)) {
            p->uncertain|=1u<<i;marble_i2c_clocks_restore(o,p);return 0;
        }
        p->held|=1u<<i;
        /* This tests reference ownership only. HW-gated AHB branches may
         * remain off without bus activity despite a valid enable vote.
         * Register access needs a separate physical readiness contract. */
        if(o->state(o->ctx,i,&enabled,&on,&hz) || enabled!=1 || on>1) {
            marble_i2c_clocks_restore(o,p);return 0;
        }
    }
    p->active=1;return 1;
}
#endif
