/* Diagnostic CB0 lease; stages7..16 can lease slot0 for exact SID5. */
#ifndef MARBLE_SMMU_H
#define MARBLE_SMMU_H
#include "upstream/ArmSmmu4K.h"
#define MARBLE_SMMU_ABI "MGPU-MARBLE-CONTEXT-v2"
#define MARBLE_SMMU_CB0 0x20000u
#define MARBLE_SMMU_ASID 0x42u
/* SMMUv2 IDR0.ATOSNS means translation operations NOT supported. */
static int marble_smmu_has_atos(uint32_t id0)
{return !!(id0&(1u<<30)) && !(id0&(1u<<26));}
static int marble_smmu_stream_write_allowed(size_t at,unsigned bytes,uint32_t value,
    unsigned stage,unsigned lease)
{
    if((stage<7 || stage>17) || !lease || bytes!=4)return 0;
    /* Mu DT GMU iommus=<... 5 0x400>: two exact IDs, no broad mask. */
    if(stage==17 && ((at==0x804 && (value==0 || value==UINT32_C(0x80000405))) ||
        (at==0xc04 && (value==0 || value==0x20000))))return 1;
    return (at==0x800 && (value==0 || value==UINT32_C(0x80000005))) ||
        (at==0xc00 && (value==0 || value==0x20000));
}
static int marble_smmu_write_allowed(size_t offset,unsigned bytes)
{
    if(bytes==8)return offset==MARBLE_SMMU_CB0+0x20 || offset==MARBLE_SMMU_CB0+0x28 ||
        offset==MARBLE_SMMU_CB0+0x800;
    if(bytes!=4)return 0;
    if(offset==0x1000 || offset==0x1800)return 1; /* CBAR0/CBA2R0 only */
    switch(offset) {
        case MARBLE_SMMU_CB0:case MARBLE_SMMU_CB0+4:case MARBLE_SMMU_CB0+0x10:
        case MARBLE_SMMU_CB0+0x30:case MARBLE_SMMU_CB0+0x38:case MARBLE_SMMU_CB0+0x3c:
        case MARBLE_SMMU_CB0+0x58:case MARBLE_SMMU_CB0+0x610:case MARBLE_SMMU_CB0+0x618:
        case MARBLE_SMMU_CB0+0x7f0:return 1;
        default:return 0;
    }
}
#endif
