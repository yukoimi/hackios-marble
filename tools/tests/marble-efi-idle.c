#include "../../xnuloader/marble_efi_idle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t control, counter, step;
    unsigned writes, waits, fail_write, bad_readback;
} Hardware;
static uint64_t control(void *cookie)
{
    Hardware *h=cookie;
    return h->control ^ ((h->bad_readback && h->writes==1) ? 1 : 0);
}
static int write_control(void *cookie,uint64_t value)
{
    Hardware *h=cookie;
    h->writes++;
    /* Model a failed write that nevertheless changed hardware. */
    h->control=value;
    return h->writes!=h->fail_write;
}
static uint64_t counter(void *cookie) { return ((Hardware *)cookie)->counter; }
static void wait_event(void *cookie)
{
    Hardware *h=cookie;
    h->counter+=h->step;
    h->waits++;
}
static MarbleEFIIdleOps ops(Hardware *h)
{ return (MarbleEFIIdleOps){control,write_control,counter,wait_event,h}; }

int main(void)
{
    uint64_t ctl,period;
    assert(!marble_efi_idle_plan(0,0,&ctl,&period));
    assert(!marble_efi_idle_plan(0,UINT64_MAX,&ctl,&period));
    const uint64_t frequencies[]={1000000,19200000,24000000,100000000,1000000000};
    for (unsigned f=0;f<sizeof(frequencies)/sizeof(*frequencies);f++) {
        uint64_t saved=UINT64_C(0x200000000003ffff);
        assert(marble_efi_idle_plan(saved,frequencies[f],&ctl,&period));
        assert((ctl&~MARBLE_EFI_IDLE_MASK)==(saved&~MARBLE_EFI_IDLE_MASK));
        assert((ctl&4) && !(ctl&(8|(UINT64_C(1)<<17))));
        assert(period*1000000<=frequencies[f]*500);
        assert(period==UINT64_C(1)<<(((ctl>>4)&15)+1));
    }
    assert(marble_efi_idle_plan(0,19200000,&ctl,&period));
    assert(period==8192); /* 426.67 us; one selected edge per full bit cycle. */

    Hardware h={.control=0x203fb,.counter=UINT64_MAX-100,.step=4000};
    MarbleEFIIdle p={0}; MarbleEFIIdleOps o=ops(&h);
    uint64_t saved=h.control;
    assert(marble_efi_idle_begin(&p,&o,19200000));
    assert(!marble_efi_idle_begin(&p,&o,19200000));
    for(unsigned i=0;i<64;i++) {
        marble_efi_idle_poll(&p,h.counter);
        assert(marble_efi_idle_wait(&p,&o));
    }
    assert(p.waits==64 && p.ticks==256000 && p.maximum==4000 && p.max_poll_gap==4000);
    assert(!marble_efi_idle_probe_ok(&p)); /* restore required before qualification */
    assert(marble_efi_idle_restore(&p,&o));
    assert(h.control==saved && marble_efi_idle_probe_ok(&p));

    memset(&p,0,sizeof(p));h=(Hardware){.control=saved,.step=0};
    assert(marble_efi_idle_begin(&p,&o,19200000));
    for(unsigned i=0;i<64;i++)assert(marble_efi_idle_wait(&p,&o));
    assert(marble_efi_idle_restore(&p,&o));
    assert(!marble_efi_idle_probe_ok(&p) && p.immediate==64); /* trapped/no-op WFE */

    memset(&p,0,sizeof(p));h=(Hardware){.control=saved,.step=40000};
    assert(marble_efi_idle_begin(&p,&o,19200000));
    assert(!marble_efi_idle_wait(&p,&o) && p.failed && p.late==1);
    assert(!marble_efi_idle_wait(&p,&o) && h.waits==1);
    assert(marble_efi_idle_restore(&p,&o) && h.control==saved);
    assert(!marble_efi_idle_begin(&p,&o,19200000)); /* fault is not auto-retried */

    memset(&p,0,sizeof(p));h=(Hardware){.control=saved,.step=4000};
    assert(marble_efi_idle_begin(&p,&o,19200000));
    h.control^=4; /* IRQ/firmware stole the event source: never enter WFE. */
    assert(!marble_efi_idle_wait(&p,&o) && !h.waits);
    assert(marble_efi_idle_restore(&p,&o) && h.control==saved);

    memset(&p,0,sizeof(p));h=(Hardware){.control=saved,.fail_write=1};
    assert(!marble_efi_idle_begin(&p,&o,19200000));
    assert(h.control==saved && h.writes==2 && !p.dirty && p.failed);
    memset(&p,0,sizeof(p));h=(Hardware){.control=saved,.bad_readback=1};
    assert(!marble_efi_idle_begin(&p,&o,19200000));
    assert(h.control==saved && h.writes==2 && !p.dirty && p.failed);

    memset(&p,0,sizeof(p));h=(Hardware){.control=saved,.fail_write=2};
    assert(marble_efi_idle_begin(&p,&o,19200000));
    assert(!marble_efi_idle_restore(&p,&o));
    assert(p.dirty && p.failed && !p.active);
    assert(!marble_efi_idle_begin(&p,&o,19200000));
    assert(!marble_efi_idle_wait(&p,&o) && !h.waits);
    puts("marble EFI idle: PASS period/field preservation, event storm rejection, late/control-drift stop, partial writes, rollback, restore latch and counter wrap");
    return 0;
}
