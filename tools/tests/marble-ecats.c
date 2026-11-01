#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_ecats.h"

/* Protocol fixture only: never evidence of hardware translation. */
typedef struct {
    uint64_t regs[7];
    unsigned calls,fail,after,writes,ack_reads;
    unsigned halt_timeout,translate_timeout,stuck_running,stuck_halt,fault;
    unsigned latch_running,latch_halt;
    unsigned expected_trigger,gpu_sid;
} Fixture;
static int fail(Fixture *f) {return ++f->calls==f->fail;}
static int read32(void *p,size_t at,uint32_t *out)
{
    Fixture *f=p;assert(at==0x20);if(fail(f))return 1;
    ++f->ack_reads;
    *out=0x100000;
    if((f->regs[0]&(1u<<16)) && !f->halt_timeout)*out|=2;
    if(f->stuck_halt)*out|=2;
    if(f->stuck_running || ((f->regs[3]&1) && (f->translate_timeout || f->fault)))*out|=1;
    return 0;
}
static int read64(void *p,size_t at,uint64_t *out)
{
    Fixture *f=p;assert(!(at&7) && at<=0x30);if(fail(f))return 1;
    *out=f->regs[at/8];return 0;
}
static int write64(void *p,size_t at,uint64_t value)
{
    Fixture *f=p;assert(marble_tbu_write_allowed(at,8));
    int bad=fail(f);++f->writes;
    if(bad && !f->after)return 1;
    if(at==0)assert((value&~UINT64_C(0x103ff))==UINT64_C(0x80182000));
    f->regs[at/8]=value;
    if(at==0 && value==0x80182000 && f->latch_halt)f->stuck_halt=1;
    if(at==0x18 && value) {
        assert(value==(f->expected_trigger?f->expected_trigger:0xbd) && f->regs[0]==(f->gpu_sid?0x80192000:0x80192005));
        assert(f->regs[6]==(UINT64_C(255)<<36));
        f->regs[5]=f->fault?1:((UINT64_C(0x812340000)+(f->regs[1]&4095))<<12);
        if(f->latch_running)f->stuck_running=1;
    }
    return bad;
}
static int barrier(void *p) {return fail(p);}
static Fixture init(void)
{Fixture f={0};f.regs[0]=0x80182000;return f;}
static int run(Fixture *f,MarbleEcatsResult *r)
{
    ArmSmmu4KIo io={f,0x1000,read32,read64,NULL,write64,barrier};
    return marble_ecats_request(&io,0x60000fff,8,r);
}
static void released(Fixture *f,MarbleEcatsResult *r)
{
    assert(r->restored && !r->retain);
    assert(f->regs[0]==0x80182000 && !f->regs[1] && !f->regs[3] && !f->regs[6]);
}
int main(void)
{
    Fixture f=init();MarbleEcatsResult r;
    assert(run(&f,&r)==MarbleEcatsOk && r.completed && !(r.par&1));
    assert(r.physical==UINT64_C(0x812340fff));released(&f,&r);
    unsigned calls=f.calls;
    f=init();f.gpu_sid=1;
    ArmSmmu4KIo gpu_io={&f,0x1000,read32,read64,NULL,write64,barrier};
    assert(marble_ecats_sid_access(&gpu_io,0,0x100000,0,8,&r)==MarbleEcatsOk);released(&f,&r);
    f=init();assert(marble_ecats_sid_access(&gpu_io,0x400,0x100000,0,8,&r)==MarbleEcatsArgument && !f.writes);
    for(unsigned access=0;access<16;access++) {
        f=init();f.expected_trigger=0xbd | ((access&1)?0x40:0) | ((access&2)?2:0) | ((access&4)?0x100:0);
        ArmSmmu4KIo io={&f,0x1000,read32,read64,NULL,write64,barrier};
        int status=marble_ecats_access(&io,0x60000fff,access,8,&r);
        if(access>7 || ((access&2) && (access&4)))assert(status==MarbleEcatsArgument && !f.writes);
        else {assert(status==MarbleEcatsOk);released(&f,&r);}
    }
    for(unsigned after=0;after<2;after++)for(unsigned n=1;n<=calls;n++) {
        f=init();f.fail=n;f.after=after;
        assert(run(&f,&r)!=MarbleEcatsOk);
        if(r.writes && !r.retain)released(&f,&r);
        if(r.retain)assert(!r.restored);
    }
    f=init();f.fault=1;assert(run(&f,&r)==MarbleEcatsOk && r.par==1 && r.completed);released(&f,&r);
    f=init();f.halt_timeout=1;assert(run(&f,&r)==MarbleEcatsTimeout);released(&f,&r);
    f=init();f.translate_timeout=1;assert(run(&f,&r)==MarbleEcatsTimeout);released(&f,&r);
    f=init();f.latch_running=1;assert(run(&f,&r)==MarbleEcatsTimeout && r.retain && !r.restored);
    f=init();f.latch_halt=1;assert(run(&f,&r)!=MarbleEcatsOk && r.retain && !r.restored);
    /* Busy before our lease must not mutate anything. */
    f=init();f.stuck_running=1;assert(run(&f,&r)==MarbleEcatsBusy && !f.writes);
    f=init();f.stuck_halt=1;assert(run(&f,&r)==MarbleEcatsBusy && !f.writes);
    f=init();f.regs[0]|=1;assert(run(&f,&r)==MarbleEcatsBusy && !f.writes);
    f=init();f.regs[0]|=1u<<16;assert(run(&f,&r)==MarbleEcatsBusy && !f.writes);
    f=init();f.regs[3]=1;assert(run(&f,&r)==MarbleEcatsBusy && !f.writes);
    for(size_t at=0;at<0x1000;at++)for(unsigned width=1;width<=8;width++)
        assert(marble_tbu_write_allowed(at,width)==(width==8 && (at==0 || at==8 || at==0x18 || at==0x30)));
    printf("ECATS protocol fixture: nonzero background preserved, PA byte decode, negative completion, %u IO failures, bounded timeouts and MMIO fence PASS (not hardware proof)\n",calls*2);
}
