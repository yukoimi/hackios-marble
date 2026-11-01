#include <assert.h>
#include <stdio.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_smmu.h"
int main(void)
{
    assert(!marble_smmu_has_atos(0x4c017e18)); /* actual marble IDR0 */
    assert(marble_smmu_has_atos(0x48017e18));  /* S1TS, ATOSNS clear */
    assert(!marble_smmu_has_atos(0));         /* no stage1 support */
    /* No global control, stream publication, other context or arbitrary ATS
     * command can pass the adapter's actual MMIO write boundary. */
    for(size_t at=0;at<0x1000;at+=4) {
        assert(!marble_smmu_write_allowed(at,4));
        assert(!marble_smmu_write_allowed(at,8));
    }
    for(size_t at=0x21000;at<0x40000;at+=4) {
        assert(!marble_smmu_write_allowed(at,4));
        assert(!marble_smmu_write_allowed(at,8));
    }
    for(unsigned b=1;b<22;b++) {
        assert(!marble_smmu_write_allowed(0x1000+4*b,4));
        assert(!marble_smmu_write_allowed(0x1800+4*b,4));
    }
    assert(marble_smmu_write_allowed(0x20800,8));
    assert(!marble_smmu_write_allowed(0x20800,4));
    assert(!marble_smmu_write_allowed(0x20808,8));
    assert(!marble_smmu_write_allowed(0x20020,4));
    assert(marble_smmu_write_allowed(0x20020,8));
    assert(!marble_smmu_write_allowed(SIZE_MAX,8));
    assert(!marble_smmu_write_allowed(0x20000,0));
    for(unsigned stage=0;stage<19;stage++)for(unsigned lease=0;lease<2;lease++) {
        assert(marble_smmu_stream_write_allowed(0x800,4,0x80000005,stage,lease)==((stage>=7 && stage<=17) && lease));
        assert(marble_smmu_stream_write_allowed(0xc00,4,0,stage,lease)==((stage>=7 && stage<=17) && lease));
        assert(marble_smmu_stream_write_allowed(0x804,4,0x80000405,stage,lease)==(stage==17 && lease));
        assert(marble_smmu_stream_write_allowed(0xc04,4,0,stage,lease)==(stage==17 && lease));
        assert(!marble_smmu_stream_write_allowed(0x804,4,0x84000005,stage,lease));
        assert(!marble_smmu_stream_write_allowed(0x804,4,0x80000406,stage,lease));
        assert(!marble_smmu_stream_write_allowed(0xc04,4,0x10000,stage,lease));
    }
    for(size_t at=0;at<0x1000;at+=4) {
        assert(!marble_smmu_stream_write_allowed(at,8,0,7,1));
        assert(!marble_smmu_stream_write_allowed(at,4,0x10000,7,1)); /* BYPASS */
        assert(!marble_smmu_stream_write_allowed(at,4,0x80000006,7,1));
        if(at!=0x800 && at!=0xc00)assert(!marble_smmu_stream_write_allowed(at,4,0,7,1));
    }
    puts("marble SMMU MMIO boundary: no global/stream/foreign-bank writes, correct TTBR/ATS width, actual ATOSNS refusal PASS");
    return 0;
}
