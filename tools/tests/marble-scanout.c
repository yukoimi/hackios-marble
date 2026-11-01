#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_scanout.h"
int main(void)
{
    _Static_assert(sizeof(MarbleScanoutShared)==128,"separate cache-line ABI");
    /* A copy starting or ending in a writer, or spanning a completed swap,
     * must not be accepted. An untouched black frame at generation zero is
     * valid only after XNU explicitly publishes readiness. */
    assert(!marble_scanout_stable(0,0,0));
    assert(marble_scanout_stable(1,0,0));
    for(uint64_t i=0;i<1024;i++) {
        assert(marble_scanout_stable(1,i,i)==!(i&1));
        assert(!marble_scanout_stable(1,i,i+1));
        assert(!marble_scanout_stable(1,i,i+2));
    }
    assert(!marble_snapshot_complete(0,0,100,100));
    assert(!marble_snapshot_complete(3,2,100,100));
    assert(!marble_snapshot_complete(3,3,99,100));
    assert(marble_snapshot_complete(3,3,100,100));
    puts("marble scanout: odd writer, readiness and cross-generation rejection PASS");
}
