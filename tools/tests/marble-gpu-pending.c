#include <assert.h>
#include <stdio.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_pending.h"
typedef struct {uint64_t now;int sample,retire;unsigned sampled,retired;uint32_t position,fence;} Device;
static uint64_t now(void *p){return ((Device *)p)->now;}
static int sample(void *p,uint32_t position,uint32_t fence) {
    Device *d=p;assert(position==d->position && fence==d->fence);d->sampled++;return d->sample;
}
static int retire(void *p,uint32_t fence){Device *d=p;assert(fence==d->fence);d->retired++;return d->retire;}
int main(void) {
    Device d={.now=123,.retire=1,.position=4070,.fence=101};MarbleGpuPending p={0};
    MarbleGpuPendingIo io={&d,now,sample,retire};
    assert(marble_pending_observe(&p,&io)==1 && !d.sampled);
    assert(!marble_pending_begin(&p,4096,101,d.now));
    assert(!marble_pending_begin(&p,4070,0,d.now));
    assert(marble_pending_begin(&p,4070,101,d.now));
    assert(!marble_pending_begin(&p,0,102,d.now));
    assert(!marble_pending_observe(&p,&io) && p.active && !d.retired);
    d.now+=999999;assert(!marble_pending_observe(&p,&io) && p.active);
    d.sample=1;assert(marble_pending_observe(&p,&io)==1 && !p.active && d.retired==1);
    assert(marble_pending_observe(&p,&io)==1 && d.retired==1);
    assert(marble_pending_begin(&p,4070,101,d.now));
    d.now+=10000000;
    assert(marble_pending_observe(&p,&io)==1 && !p.active); /* idle observer, real completion */
    p=(MarbleGpuPending){0};d=(Device){.now=100,.retire=1,.position=4070,.fence=101};
    assert(marble_pending_append(&p,d.position,d.fence,d.now));
    for(unsigned i=1;i<MARBLE_GPU_PENDING_MAX;i++) {
        d.now+=100;
        assert(!marble_pending_append(&p,4096,d.fence+1,d.now));
        assert(!marble_pending_append(&p,0,d.fence,d.now));
        assert(!marble_pending_append(&p,0,d.fence+1,99));
        d.position=(d.position+580)&4095;d.fence++;
        assert(marble_pending_append(&p,d.position,d.fence,d.now));
        assert(p.depth==i+1 && p.began_us==100);
        assert(!marble_pending_observe(&p,&io) && !d.retired);
    }
    assert(!marble_pending_append(&p,0,d.fence+1,d.now));
    assert(p.depth==4 && p.position==d.position && p.fence==d.fence);
    d.sample=1;
    assert(marble_pending_observe(&p,&io)==1 && !p.active && !p.depth && d.retired==1);
    /* A stream of later appends cannot postpone an earlier timeout. */
    p=(MarbleGpuPending){0};d=(Device){.now=100,.retire=1,.position=4070,.fence=101};
    assert(marble_pending_append(&p,d.position,d.fence,d.now));
    d.now+=999999;d.position=10;d.fence++;
    assert(marble_pending_append(&p,d.position,d.fence,d.now));
    d.now++;
    assert(marble_pending_observe(&p,&io)==-1 && p.active && p.depth==2 && !d.retired);
    for(unsigned failure=0;failure<4;failure++) {
        p=(MarbleGpuPending){0};d=(Device){.now=500,.retire=1,.position=4070,.fence=101};
        assert(marble_pending_begin(&p,4070,101,d.now));
        if(failure==0)d.now+=1000000; /* timeout, including nonblocking wait */
        if(failure==1)d.sample=-1; /* hardware fault */
        if(failure==2){d.sample=1;d.retire=0;} /* uncertain fence acquire */
        if(failure==3)d.now--; /* broken clock */
        assert(marble_pending_observe(&p,&io)==-1 && p.active && p.failed);
        unsigned retired=d.retired,sampled=d.sampled;d.sample=d.retire=1;
        assert(marble_pending_observe(&p,&io)==-1);
        assert(retired==d.retired && sampled==d.sampled && p.active);
        assert(!marble_pending_begin(&p,0,102,d.now));
        assert(!marble_pending_append(&p,0,102,d.now));
    }
    puts("GPU pending: depth 1..4, oldest timeout, final-idle retirement, busy and poisoned retention PASS");
}
