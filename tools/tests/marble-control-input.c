#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../../userland/src/marble_control_input.h"
int main(void)
{
    uint64_t t=10000000,visible=(t<<1)|1;
    assert(marble_control_bottom_is_local(visible,t,65535));
    assert(marble_control_bottom_is_local(visible,t+2000000,65535));
    assert(!marble_control_bottom_is_local(visible,t+2000001,65535));
    assert(!marble_control_bottom_is_local(visible,t-1,65535));
    assert(!marble_control_bottom_is_local(1,t,65535));
    assert(!marble_control_bottom_is_local(visible,t,65536));
    for(uint32_t y=0;y<=65535;y++) {
        assert(!marble_control_bottom_is_local(t<<1,t,y)); /* hidden overlays preserve every edge */
        if(y<=62000)assert(!marble_control_bottom_is_local(visible,t,y)); /* apps/modules/top/side positions */
    }
    puts("marble-control-input: stale/future/hidden/out-of-range refusal; bottom-only visible overlay classification PASS");
    return 0;
}
