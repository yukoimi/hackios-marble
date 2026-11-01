/* Marble firmware reservations, after the pinned AdrenoFirmware parser.
 * Xiaomi 48952ed3 gen7_gmu_process_prealloc supplies the VMA/metadata rules.
 * No allocation, MMIO, TCM access or GMU execution occurs here. */
#ifndef MARBLE_GMU_MEMORY_H
#define MARBLE_GMU_MEMORY_H
#include "marble_gpu_plan.h"
#define MARBLE_GMU_MEMORY_ABI "MGPU-MARBLE-GMU-MEMORY-v1"
#define MARBLE_GMU_RANGE_CAPACITY 6u
#define MARBLE_GMU_TABLE_CAPACITY 8u
#define MARBLE_GMU_BACKING_BYTES 0x34000u
typedef struct {
    ArmSmmu4KRange ranges[MARBLE_GMU_RANGE_CAPACITY];
    size_t count,bytes,pages,ram_payloads,tcm_payloads;
} MarbleGmuMemoryPlan;
static int marble_gmu_region(uint64_t start,uint64_t bytes)
{
    if(!bytes)return -1;
    for(unsigned i=0;i<5;i++) {
        const AdrenoFirmwareRegion *r=marble_gmu_regions+i;
        if(start>=r->address && bytes<=r->bytes && start-r->address<=r->bytes-bytes)return (int)i;
    }
    return -1;
}
static int marble_gmu_memory_plan(const AdrenoFirmwareBlock *blocks,size_t count,
    uint64_t data_pa,size_t capacity,MarbleGmuMemoryPlan *out)
{
    if(!out)return 0;
    *out=(MarbleGmuMemoryPlan){0};
    if(!blocks || !count || count>32 || !data_pa || (data_pa&4095) ||
        data_pa>=(UINT64_C(1)<<36) || capacity>(UINT64_C(1)<<36)-data_pa)return 0;
    MarbleGmuMemoryPlan p={0};unsigned payload_seen=0;
    for(size_t i=0;i<count;i++) {
        const AdrenoFirmwareBlock *b=blocks+i;
        if(b->bytes) {payload_seen=1;continue;}
        if(payload_seen || (b->type!=1 && b->type!=7))return 0;
        int region=marble_gmu_region(b->address,b->value);
        if(region<0)return 0;
        if(region==0 || region==2)continue; /* TCM is not system RAM. */
        if((b->address&4095) || p.count==MARBLE_GMU_RANGE_CAPACITY)return 0;
        uint64_t bytes=((uint64_t)b->value+4095)&~UINT64_C(4095);
        if(marble_gmu_region(b->address,bytes)!=region || p.bytes>capacity || bytes>capacity-p.bytes)return 0;
        if(p.count && b->address<p.ranges[p.count-1].iova+p.ranges[p.count-1].bytes)return 0;
        /* Linux uses READ|WRITE|PRIV. Its default mappings permit execute;
         * explicitly allow instructions only in the GMU cache region here. */
        unsigned permissions=ArmSmmu4KRead|ArmSmmu4KWrite|ArmSmmu4KPrivileged;
        if(region==1)permissions|=ArmSmmu4KExecute;
        p.ranges[p.count++]=(ArmSmmu4KRange){b->address,data_pa+p.bytes,bytes,permissions,1,3};
        p.bytes+=(size_t)bytes;p.pages+=(size_t)(bytes/4096);
    }
    if(!p.count)return 0;
    for(size_t i=0;i<count;i++) {
        const AdrenoFirmwareBlock *b=blocks+i;if(!b->bytes)continue;
        int region=marble_gmu_region(b->address,b->bytes);
        if(region<0 || (unsigned)region!=b->region_identifier)return 0;
        if(region==0 || region==2) {p.tcm_payloads++;continue;}
        unsigned found=0;
        for(size_t j=0;j<p.count;j++) {
            const ArmSmmu4KRange *r=p.ranges+j;
            if(b->address>=r->iova && b->bytes<=r->bytes && b->address-r->iova<=r->bytes-b->bytes)found++;
        }
        if(found!=1)return 0;
        p.ram_payloads++;
    }
    *out=p;return 1;
}
#endif
