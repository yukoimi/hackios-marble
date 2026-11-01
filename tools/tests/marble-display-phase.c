#include <stdint.h>
#include "../../xnu/files/iokit/Kernel/MarbleDisplayPhase.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const uint64_t period=16666667,budget=2000000;
    assert(marble_blank_admit(10,2500,10,9,1,2415,4832,period,budget));
    assert(!marble_blank_admit(10,2300,10,9,1,2415,4832,period,budget));
    assert(!marble_blank_admit(10,4500,10,9,1,2415,4832,period,budget));
    assert(!marble_blank_admit(10,2500,11,9,1,2415,4832,period,budget));
    assert(!marble_blank_admit(10,2500,10,10,1,2415,4832,period,budget));
    assert(marble_blank_admit(0,2500,0,UINT32_MAX,1,2415,4832,period,budget));
    assert(!marble_blank_admit(0,2500,0,0,0,2415,4832,UINT64_MAX,budget));
    assert(!marble_blank_admit(0,2500,0,0,0,2415,0,period,budget));
    assert(marble_blank_copy_completed(10,2500,10,3100,2415,4832));
    assert(!marble_blank_copy_completed(10,2500,11,200,2415,4832));
    assert(!marble_blank_copy_completed(10,2500,10,1800,2415,4832));
    /* Coarse wakeups leave 750us for observing the actual blank. Neither
     * stale timestamps nor waking late allow a copy without admission. */
    uint64_t delay=marble_blank_poll_delay(10,100,9,1,2415,4832,period,budget,250000,750000);
    assert(delay>7000000 && delay<8000000);
    assert(marble_blank_poll_delay(10,2500,9,1,2415,4832,period,budget,250000,750000)==250000);
    assert(marble_blank_poll_delay(10,2500,10,1,2415,4832,period,budget,250000,750000)>15000000);
    assert(marble_blank_poll_delay(10,4800,9,1,2415,4832,period,budget,250000,750000)>7000000);
    assert(marble_blank_poll_delay(0,2500,UINT32_MAX,1,2415,4832,period,budget,250000,750000)==250000);
    assert(marble_blank_poll_delay(10,0,9,1,2415,0,period,budget,250000,750000)==250000);
    assert(marble_blank_poll_delay(10,0,9,1,2415,4832,UINT64_MAX,budget,250000,750000)==250000);
    for(unsigned line=0;line<4832;line++) {
        uint64_t nap=marble_blank_poll_delay(10,line,9,1,2415,4832,period,budget,250000,750000);
        assert(nap>=250000 && nap<period);
        if(marble_blank_admit(10,line,10,9,1,2415,4832,period,budget))assert(nap==250000);
        /* Simulate a wakeup 3ms late: always use the new hardware sample. */
        unsigned advanced=line+(unsigned)((nap+3000000)*4832/period);
        uint32_t frame=10+advanced/4832;unsigned scan=advanced%4832;
        if(marble_blank_admit(frame,scan,frame,9,1,2415,4832,period,budget)) {
            assert(scan>2419 && scan<4828);
            assert(period*(4832-scan-4)/4832>=budget);
        }
    }
    puts("display phase: PASS active/blank boundaries, remaining copy budget, frame races, one present per frame, counter wrap and overrun rejection");
}
