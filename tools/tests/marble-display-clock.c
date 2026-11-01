#include "../../xnuloader/marble_display_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct fixture { unsigned refs[3],force[3],calls,failed,uncertain,disable_error; };
static int id(void *ctx,const char *name,uint64_t *out)
{(void)ctx;for(unsigned i=0;i<3;i++)if(!strcmp(name,marble_display_bus_clocks[i])){*out=i;return 0;}return -1;}
static int state(void *ctx,uint64_t i,unsigned *e,unsigned *on)
{struct fixture *f=ctx;*e=!!f->refs[i];*on=*e||f->force[i];return 0;}
static int enable(void *ctx,uint64_t i)
{struct fixture *f=ctx;f->calls++;if(f->failed==i+1)return -1;f->refs[i]++;return 0;}
static int disable(void *ctx,uint64_t i)
{struct fixture *f=ctx;if(f->disable_error==i+1)return -1;assert(f->refs[i]);f->refs[i]--;return 0;}
int main(void)
{
    struct fixture f={.refs={0,2,0},.force={1,1,0}};
    MarbleClockOps o={&f,id,state,enable,disable};MarbleDisplayClock p={0};
    assert(marble_display_clock_prepare(&o,&p));
    assert(marble_display_clock_acquire(&o,&p));assert(p.held==7 && p.active);
    assert(f.refs[0]==1 && f.refs[1]==3 && f.refs[2]==1);
    assert(marble_display_clock_restore(&o,&p));
    assert(f.refs[0]==0 && f.refs[1]==2 && f.refs[2]==0);
    assert(marble_display_clock_prepare(&o,&p));f.refs[0]=1;
    assert(!marble_display_clock_acquire(&o,&p));assert(f.calls==3);f.refs[0]=0;
    p=(MarbleDisplayClock){0};f.failed=2;
    assert(marble_display_clock_prepare(&o,&p));
    assert(!marble_display_clock_acquire(&o,&p));
    assert(p.dirty && p.uncertain==2 && !p.held);
    assert(f.refs[0]==0 && f.refs[1]==2); /* No blind disable of the failed call. */
    assert(!marble_display_clock_prepare(&o,&p));
    p=(MarbleDisplayClock){0};f.failed=0;f.disable_error=3;
    assert(marble_display_clock_prepare(&o,&p));assert(marble_display_clock_acquire(&o,&p));
    assert(!marble_display_clock_restore(&o,&p));assert(p.dirty && p.held==4);
    f.disable_error=0;assert(marble_display_clock_restore(&o,&p));
    assert(f.refs[0]==0 && f.refs[1]==2 && f.refs[2]==0);
    puts("display clock: PASS balanced inherited references, ownership drift, ambiguous enable, failed release recovery");
    return 0;
}
