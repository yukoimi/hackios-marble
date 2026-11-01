#include "../../xnuloader/marble_thermal_window.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    MarbleThermalWindow p={0};uint64_t frequency=19200000,base=UINT64_MAX-frequency*3;
    assert(marble_thermal_window(&p,base,frequency,1,42000,1));
    for(unsigned i=1;i<=4;i++)assert(marble_thermal_window(&p,base+frequency*i,frequency,1,49000,0));
    for(unsigned i=5;i<=14;i++)assert(marble_thermal_window(&p,base+frequency*i,frequency,1,50000,0));
    assert(p.maximum_hot_ticks==frequency*9);
    assert(!marble_thermal_window(&p,base+frequency*15,frequency,1,50000,0) && p.reason==5);
    assert(marble_thermal_window(&p,0,frequency,1,49000,1));
    for(unsigned i=1;i<=8;i++)assert(marble_thermal_window(&p,frequency*i,frequency,1,54000,0));
    assert(marble_thermal_window(&p,frequency*9,frequency,1,49000,0));
    for(unsigned i=10;i<=18;i++)assert(marble_thermal_window(&p,frequency*i,frequency,1,51000,0));
    assert(p.maximum_hot_ticks==frequency*8); /* cooling breaks the consecutive span */
    assert(!marble_thermal_window(&p,frequency*19,frequency,1,55000,0) && p.reason==4);
    assert(!marble_thermal_window(&p,0,frequency,1,50000,1) && p.reason==2);
    assert(!marble_thermal_window(&p,0,frequency,0,30000,1) && p.reason==1);
    assert(marble_thermal_window(&p,0,frequency,1,40000,1));
    assert(!marble_thermal_window(&p,frequency*4,frequency,1,41000,0) && p.reason==3);
    memset(&p,0,sizeof(p));
    assert(!marble_thermal_window(&p,frequency,frequency,1,41000,0) && p.reason==3);
    assert(marble_thermal_window_duration(&p,0,frequency,1,42000,1,60));
    for(unsigned i=1;i<=60;i++)assert(marble_thermal_window_duration(&p,frequency*i,frequency,1,52000,0,60));
    assert(!marble_thermal_window_duration(&p,frequency*61,frequency,1,52000,0,60) && p.reason==5);
    assert(marble_thermal_window_duration(&p,0,frequency,1,42000,1,60));
    assert(!marble_thermal_window_duration(&p,frequency,frequency,1,55000,0,60) && p.reason==4);
    puts("short thermal window: PASS short die excursions, consecutive hot span, reset on cooling, hard limit, invalid/stale readings and counter wrap");
    return 0;
}
