#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_i2c_clock_lease.h"
typedef struct {unsigned refs[5],initial[5],operations,fail_at,enable_calls,disable_mask,fail_disable,drift,stuck;int order;} Model;
static int state(void *ctx,unsigned i,unsigned *e,unsigned *on,uint32_t *hz)
{
    Model *m=ctx;if(++m->operations==m->fail_at)return 1;
    *e=!!m->refs[i];*on=m->refs[i]? !(m->stuck && m->refs[i]>m->initial[i]):0;*hz=19200000+m->drift;return 0;
}
static int enable(void *ctx,unsigned i)
{
    Model *m=ctx;m->enable_calls++;if(++m->operations==m->fail_at)return 1;
    m->refs[i]++;return 0;
}
static int disable(void *ctx,unsigned i)
{
    Model *m=ctx;assert((int)i<m->order);m->order=(int)i;m->disable_mask|=1u<<i;
    if(++m->operations==m->fail_at || m->fail_disable)return 1;
    assert(m->refs[i]>m->initial[i]);m->refs[i]--;return 0;
}
static Model initial(unsigned mask)
{
    Model m={0};m.order=5;for(unsigned i=0;i<5;i++)m.refs[i]=m.initial[i]=(mask>>i)&1;return m;
}
int main(void)
{
    for(unsigned mask=0;mask<32;mask++) {
        Model m=initial(mask);MarbleI2cClockOps o={&m,state,enable,disable};MarbleI2cClockLease p={0};
        assert(!marble_i2c_clocks_acquire(&o,&p));assert(!marble_i2c_clocks_restore(&o,&p));
        assert(marble_i2c_clocks_prepare(&o,&p));assert(!marble_i2c_clocks_prepare(&o,&p));
        assert(marble_i2c_clocks_acquire(&o,&p));assert(p.active && p.held==31);
        assert(!marble_i2c_clocks_acquire(&o,&p));assert(marble_i2c_clocks_restore(&o,&p));
        assert(m.disable_mask==31 && !memcmp(m.refs,m.initial,sizeof(m.refs)) && !p.dirty);
        /* Fault each state/enable/disable call. Uncertain enables remain
         * latched, while all unambiguous references unwind in reverse. */
        unsigned calls=m.operations;
        for(unsigned failure=1;failure<=calls;failure++) {
            m=initial(mask);m.fail_at=failure;memset(&p,0,sizeof(p));
            if(marble_i2c_clocks_prepare(&o,&p)) {
                if(marble_i2c_clocks_acquire(&o,&p))assert(!marble_i2c_clocks_restore(&o,&p));
                else if(p.prepared)assert(marble_i2c_clocks_restore(&o,&p));
            }
            for(unsigned i=0;i<5;i++)assert(m.refs[i]>=m.initial[i]);
            if(p.uncertain)assert(p.dirty && !marble_i2c_clocks_prepare(&o,&p));
        }
        m=initial(mask);memset(&p,0,sizeof(p));assert(marble_i2c_clocks_prepare(&o,&p));m.drift=1;
        assert(!marble_i2c_clocks_acquire(&o,&p));assert(!m.enable_calls);assert(!marble_i2c_clocks_restore(&o,&p));assert(p.dirty);
        m=initial(mask);memset(&p,0,sizeof(p));assert(marble_i2c_clocks_prepare(&o,&p));m.stuck=1;
        assert(marble_i2c_clocks_acquire(&o,&p));assert(m.enable_calls==5);assert(marble_i2c_clocks_restore(&o,&p));
    }
    puts("PASS five-clock lease: all initial ownership combinations, each call fault, reverse unwind, drift and stuck clock; no dropped owner references");
}
