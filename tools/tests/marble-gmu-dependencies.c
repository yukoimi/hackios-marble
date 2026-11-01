#include <assert.h>
#include <stdio.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_dependencies.h"
int main(void)
{
    uint8_t aux[32]={0,0,16,0,64,0,128,0,192,0,0,1};uint32_t level=999;
    MarbleCmdDbEntry e={0};e.slave=3;e.bytes=32;e.aux=aux;
    assert(marble_arc_level(&e,5,&level) && level==256);
    assert(marble_arc_level(&e,0,&level) && !level);
    assert(!marble_arc_level(&e,6,&level) && !marble_arc_level(&e,16,&level));
    assert(!marble_arc_level(&e,0x80000005,&level));
    uint32_t minimum=0x20000000|(5u<<14)|15u;
    assert(marble_bcm_covers(0x20000000|(10u<<14)|20u,minimum));
    assert(!marble_bcm_covers((10u<<14)|20u,minimum));
    assert(!marble_bcm_covers(0x20000000|(4u<<14)|20u,minimum));
    assert(!marble_bcm_covers(0x20000000|(10u<<14)|14u,minimum));
    assert(marble_bcm_covers(minimum,minimum));
    puts("GMU dependencies: command-DB index, unknown/padded levels and independent BCM peak/average/valid gates PASS");
    return 0;
}
