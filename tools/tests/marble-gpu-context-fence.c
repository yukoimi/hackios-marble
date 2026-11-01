#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t vm_offset_t;
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_context.h"
int main(void)
{
    for(size_t at=0;at<0x40000;at+=4) {
        int region=at>=0x21000 && at<0x22000;
        int allowed=region && marble_smmu_write_allowed(at-4096,4);
        allowed|=at==0x1004 || at==0x1804;
        assert(marble_gpu_smmu_write_allowed(at,4,0x32b,0)==allowed);
        int wide=at==0x21020 || at==0x21028;
        assert(marble_gpu_smmu_write_allowed(at,8,0,0)==wide);
    }
    unsigned slots[]={0x808,0x80c,0x810,0x814,0xc08,0xc0c,0xc10,0xc14};
    for(unsigned i=0;i<8;i++)for(uint32_t v=0;v<0x800;v++) {
        int allowed=i<4?v==0:v==1;
        assert(marble_gpu_smmu_write_allowed(slots[i],4,v,1)==allowed);
        assert(!marble_gpu_smmu_write_allowed(slots[i],4,v,0));
    }
    assert(marble_gpu_smmu_write_allowed(0x808,4,0x80000000,1));
    assert(marble_gpu_smmu_write_allowed(0x80c,4,0x80000400,1));
    assert(marble_gpu_smmu_write_allowed(0x810,4,0x80000001,1));
    assert(marble_gpu_smmu_write_allowed(0x814,4,0x80000401,1));
    assert(!marble_gpu_smmu_write_allowed(0x810,4,0x84000001,1));
    assert(!marble_gpu_smmu_write_allowed(0x818,4,0x80000002,1));
    assert(marble_gpu_smmu_write_allowed(0xc08,4,0x20000,1));
    assert(marble_gpu_smmu_write_allowed(0xc0c,4,0x20000,1));
    puts("GPU bank1 fence: CB0, other banks, global reset, GMU routes, broad masks and unsupported ATS writes refused PASS");
}
