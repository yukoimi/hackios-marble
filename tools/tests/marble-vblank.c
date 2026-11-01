#include "../../xnuloader/marble_vblank.h"
#include <assert.h>
#include <stdio.h>
static unsigned reads, count_enabled=3, timing_status=1;
static int rd(void *ctx,uint64_t address,uint32_t *value)
{
    (void)ctx;reads++;
    assert(address>=0xae36000 && address<=0xae3626c);
    switch(address-0xae36000){
    case 0:*value=1;break;case 0xa8:*value=count_enabled;break;
    case 0xac:*value=42;break;case 0xb0:*value=100;break;
    case 8:*value=(1200u<<16)|4;break;case 0xc:*value=3000000;break;
    case 0x1c:*value=12000;break;case 0x24:*value=2892000;break;
    case 0x26c:*value=timing_status;break;default:assert(0);
    }return 0;
}
int main(void)
{
    MarbleVBlankSample s={0};MarbleVBlankClock c={0};
    assert(marble_vblank_read(rd,NULL,0,&s));assert(reads==9);
    assert(!marble_vblank_read(rd,NULL,2,&s));assert(reads==9);
    count_enabled=0;assert(marble_vblank_read(rd,NULL,0,&s));
    timing_status=0;assert(!marble_vblank_read(rd,NULL,0,&s));timing_status=1;
    s.frame=0xffffffff;
    assert(!marble_vblank_observe(&c,&s,1,1000000,1000000));
    s.frame=0;assert(!marble_vblank_observe(&c,&s,1,1000100,1000000));
    s.frame=1;assert(marble_vblank_observe(&c,&s,1,1016767,1000000));
    assert(c.edges==1 && c.minimum_period==16667);
    assert(!marble_vblank_observe(&c,&s,1,1017000,1000000));
    s.frame=3;assert(!marble_vblank_observe(&c,&s,1,1050000,1000000));assert(c.missed==1);
    s.frame=4;assert(!marble_vblank_observe(&c,&s,1,1050100,1000000));
    assert(!marble_vblank_observe(&c,&s,0,1060000,1000000));assert(c.invalid==1);
    puts("marble vblank: PASS read whitelist, counter wrap, frame skips, implausible periods and invalid samples");
    return 0;
}
