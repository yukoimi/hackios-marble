#include "../../xnuloader/marble_thermal.h"
#include <assert.h>
#include <stdio.h>

static unsigned reads,mode;
static int read_sensor(void *cookie,uint64_t address,uint32_t *out)
{
    (void)cookie;reads++;
    if(address==0xc222000 || address==0xc223000){*out=mode==1?0:0x20000000;return 0;}
    if(address==0xc222004 || address==0xc223004){*out=mode==2?0:1;return 0;}
    if(address==0xc263014 || address==0xc265014){*out=mode==3?1:mode==4?0x80000000:0x40000000;return 0;}
    uint64_t base=address>=0xc265000?0xc265000:0xc263000;
    assert(address>=base+0xa4 && address<=base+(base==0xc263000?0xdc:0xb0) && !(address&3));
    if(mode==5){*out=0;return 0;}
    if(mode==6)return -1;
    int deci=mode==7?-123:mode==8?1260:address==0xc2630dc?554:320;
    *out=(1u<<21)|(deci&0xfff);return 0;
}
int main(void)
{
    MarbleThermalSample sample;
    assert(marble_thermal_snapshot(read_sensor,0,&sample));
    assert(sample.valid==19 && sample.maximum_mc==55400 && sample.controller==0 && sample.sensor==15);
    assert(reads==25);
    for(mode=1;mode<=6;mode++) {
        reads=0;assert(!marble_thermal_snapshot(read_sensor,0,&sample));assert(reads<=25);
    }
    mode=7;assert(marble_thermal_snapshot(read_sensor,0,&sample));assert(sample.maximum_mc==-12300);
    mode=8;assert(!marble_thermal_snapshot(read_sensor,0,&sample));
    puts("marble thermal: PASS signed temperatures, exact reads, disabled/invalid/critical/watchdog rejection, CC_MON distinction; writes=0");return 0;
}
