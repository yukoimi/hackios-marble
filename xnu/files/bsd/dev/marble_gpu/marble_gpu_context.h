/* Owned native4K GPU bank, independent of the live GMU CB0 lease. */
#ifndef MARBLE_GPU_CONTEXT_H
#define MARBLE_GPU_CONTEXT_H
#include "upstream/ArmSmmu4K.h"
#include "upstream/ArmSmmu4KLive.h"
#include "marble_smmu.h"
#define MARBLE_GPU_TABLE_PAGES 8
#define MARBLE_GPU_RAM_BYTES 0x30000
#define MARBLE_GPU_ASID 0x43
static int marble_gpu_smmu_write_allowed(size_t at,unsigned bytes,uint32_t value,unsigned stream_lease)
{
    if(at>=0x21000 && at<0x22000)
        return at!=0x21800 && marble_smmu_write_allowed(at-4096,bytes);
    if(bytes!=4)return 0;
    if(at==0x1004 || at==0x1804)return 1;
    return stream_lease && ((at==0x808 && (value==0 || value==0x80000000u)) ||
        (at==0x80c && (value==0 || value==0x80000400u)) ||
        (at==0x810 && (value==0 || value==0x80000001u)) ||
        (at==0x814 && (value==0 || value==0x80000401u)) ||
        ((at==0xc08 || at==0xc0c || at==0xc10 || at==0xc14) && (value==1 || value==0x20000)));
}
int marble_gpu_smmu_begin(vm_offset_t,const ArmSmmu4KConfig *,const ArmSmmu4KResult *,
    uint64_t *,const ArmSmmu4KRange *,size_t,size_t);
int marble_gpu_smmu_end(vm_offset_t,vm_offset_t);
int marble_gpu_smmu_health(void);
/* Separate live aperture: unmodified fixed-range diagnostic stays unchanged. */
int marble_gpu_smmu_live_begin(vm_offset_t,const ArmSmmu4KLiveArena *,ArmSmmu4KLiveState *,
    const ArmSmmu4KRange *,size_t,int (*)(void *),void *);
int marble_gpu_smmu_live_update(const ArmSmmu4KRange *,size_t,unsigned);
int marble_gpu_smmu_live_ecats(uint64_t,uint64_t);
int marble_gpu_smmu_queue_page(uint64_t,uint64_t,unsigned);
void marble_gpu_smmu_commit_pages(void);
void marble_gpu_smmu_unmap_span(uint64_t,uint64_t);
#define MARBLE_GPU_LIVE_RANGES 65536
#endif
