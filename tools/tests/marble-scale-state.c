#include <assert.h>
#include <stdio.h>
#include "../../xnu/files/bsd/sys/marble_gpu_scale.h"
int main(void)
{
    struct marble_scale_state s={0};
    assert(sizeof(struct marble_scale_wire)==48);
    assert(!marble_scale_deliver(&s));assert(!marble_scale_complete(&s,0,0));
    assert(marble_scale_offer(&s));assert(s.sequence==1);assert(!marble_scale_offer(&s));
    assert(!marble_scale_complete(&s,1,0));assert(marble_scale_deliver(&s));
    assert(!marble_scale_deliver(&s));assert(!marble_scale_complete(&s,2,0));
    assert(marble_scale_complete(&s,1,0));assert(!marble_scale_complete(&s,1,0));
    marble_scale_retire(&s);assert(marble_scale_offer(&s));assert(s.sequence==2);
    assert(marble_scale_deliver(&s));marble_scale_retire(&s); /* timed out */
    assert(!marble_scale_complete(&s,2,0));assert(marble_scale_offer(&s));
    assert(marble_scale_deliver(&s));assert(!marble_scale_complete(&s,2,0));
    assert(marble_scale_complete(&s,3,17));assert(s.status==17);
    marble_scale_retire(&s);assert(marble_scale_offer(&s));
    assert(marble_scale_abandon(&s)==1);assert(s.phase==MarbleScaleIdle);
    assert(!marble_scale_complete(&s,4,0));assert(marble_scale_offer(&s));
    assert(marble_scale_deliver(&s));assert(marble_scale_abandon(&s)==2);
    assert(!marble_scale_offer(&s));assert(!marble_scale_deliver(&s));
    assert(!marble_scale_complete(&s,4,0));assert(marble_scale_complete(&s,5,0));
    assert(!marble_scale_complete(&s,5,0));marble_scale_retire(&s);
    assert(marble_scale_offer(&s));assert(marble_scale_deliver(&s));
    assert(!marble_scale_complete(&s,5,0));assert(marble_scale_complete(&s,6,0));
    marble_scale_retire(&s);s.sequence=UINT64_MAX;assert(!marble_scale_offer(&s));
    assert(marble_scale_layout(1080,2400,4352,4352ULL*2400));
    assert(!marble_scale_layout(1080,2400,4320,4320ULL*2400));
    assert(!marble_scale_layout(1080,2400,4352,4352ULL*2400-1));
    assert(!marble_scale_layout(4097,1,16448,16448));
    assert(!marble_scale_layout(4096,4096,16384,67108864));
    assert(!marble_scale_layout(0,1,64,64));assert(!marble_scale_layout(1,0,64,64));
    puts("PASS sequence lifecycle, stale/duplicate completion, timeout, wrap, padded layout");
    return 0;
}
