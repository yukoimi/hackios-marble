#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_pil_memory.h"
int main(void)
{
    uint8_t a[256],b[256];
    for(unsigned off=0;off<16;off++)for(unsigned shift=0;shift<16;shift++)for(unsigned bytes=0;bytes<97;bytes++) {
        memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));
        assert(marble_pil_memory_equal(a+off,b+shift,bytes));
        if(bytes) {b[shift+bytes-1]^=1;assert(!marble_pil_memory_equal(a+off,b+shift,bytes));}
        marble_pil_memory_zero(a+off,bytes);assert(marble_pil_memory_is_zero(a+off,bytes));
        for(unsigned i=0;i<sizeof(a);i++)assert(a[i]==(i>=off && i<off+bytes?0:0xa5));
        if(bytes) {a[off+bytes-1]=1;assert(!marble_pil_memory_is_zero(a+off,bytes));}
    }
    puts("marble-pil-memory: all 16x16 alignments, exact head/tail bounds, mismatch and zero checks PASS");return 0;
}
