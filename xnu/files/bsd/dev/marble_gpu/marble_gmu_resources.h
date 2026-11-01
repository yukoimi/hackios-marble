/* Board resources after Xiaomi 48952ed3 gen7_gmu_init: firmware reservations,
 * first-fit 16KiB GMU log, then first-fit 16KiB non-hwsched HFI allocation.
 * No guessed command-db votes, queue notification, DMA or GMU execution. */
#ifndef MARBLE_GMU_RESOURCES_H
#define MARBLE_GMU_RESOURCES_H
#include "marble_gmu_memory.h"
#define MARBLE_GMU_RESOURCES_ABI "MGPU-MARBLE-GMU-RESOURCES-v1"
#define MARBLE_GMU_RESOURCE_RANGE_CAPACITY 8u
#define MARBLE_GMU_LOG_BYTES 0x4000u
#define MARBLE_GMU_HFI_BYTES 0x4000u
typedef struct {
    ArmSmmu4KRange ranges[MARBLE_GMU_RESOURCE_RANGE_CAPACITY];
    size_t count,pages;
    uint32_t log_iova,hfi_iova,log_register;
} MarbleGmuResourcePlan;
static int marble_gmu_resource_insert(MarbleGmuResourcePlan *p,uint64_t pa,uint32_t *out)
{
    uint64_t at=0x60000000;
    if(p->count>=MARBLE_GMU_RESOURCE_RANGE_CAPACITY)return 0;
    /* The dynamic VMA is 0x60000000..0x7fffffff. Lowest aligned free hole,
     * following find_unmapped_va; never overlap metadata reservations. */
    for(size_t i=0;i<p->count;i++) {
        const ArmSmmu4KRange *r=p->ranges+i;
        if(r->iova+r->bytes<=at)continue;
        if(at+0x4000<=r->iova)break;
        at=(r->iova+r->bytes+4095)&~UINT64_C(4095);
    }
    if(at<0x60000000 || at>0x80000000-0x4000)return 0;
    size_t i=p->count;
    while(i && p->ranges[i-1].iova>at) {p->ranges[i]=p->ranges[i-1];i--;}
    p->ranges[i]=(ArmSmmu4KRange){at,pa,0x4000,ArmSmmu4KRead|ArmSmmu4KWrite|ArmSmmu4KPrivileged,1,3};
    p->count++;p->pages+=4;*out=(uint32_t)at;return 1;
}
static int marble_gmu_resources_plan(const MarbleGmuMemoryPlan *fw,uint64_t log_pa,
    uint64_t hfi_pa,MarbleGmuResourcePlan *out)
{
    if(!out)return 0;
    *out=(MarbleGmuResourcePlan){0};
    if(!fw || fw->count!=6 || fw->pages!=51 || fw->bytes!=0x33000 ||
        fw->ram_payloads!=1 || fw->tcm_payloads!=3 || !log_pa || !hfi_pa ||
        ((log_pa|hfi_pa)&0x3fff) || log_pa>(UINT64_C(1)<<36)-0x4000 ||
        hfi_pa>(UINT64_C(1)<<36)-0x4000 ||
        (log_pa<hfi_pa+0x4000 && hfi_pa<log_pa+0x4000))return 0;
    uint64_t backing=fw->ranges[0].physical,consumed=0;
    if(!backing || (backing&4095) || backing>(UINT64_C(1)<<36)-MARBLE_GMU_BACKING_BYTES)return 0;
    if((log_pa<backing+MARBLE_GMU_BACKING_BYTES && backing<log_pa+0x4000) ||
       (hfi_pa<backing+MARBLE_GMU_BACKING_BYTES && backing<hfi_pa+0x4000))return 0;
    for(size_t i=0;i<fw->count;i++) {
        const ArmSmmu4KRange *r=fw->ranges+i;
        if(!r->bytes || (r->bytes&4095) || (r->iova&4095) ||
            r->iova>UINT32_MAX || r->bytes>(UINT64_C(1)<<32)-r->iova ||
            r->physical!=backing+consumed || r->bytes>fw->bytes-consumed ||
            (i && fw->ranges[i-1].iova+fw->ranges[i-1].bytes>r->iova))return 0;
        consumed+=r->bytes;
    }
    if(consumed!=fw->bytes)return 0;
    MarbleGmuResourcePlan p={0};p.count=fw->count;p.pages=fw->pages;
    for(size_t i=0;i<fw->count;i++)p.ranges[i]=fw->ranges[i];
    if(!marble_gmu_resource_insert(&p,log_pa,&p.log_iova) ||
        !marble_gmu_resource_insert(&p,hfi_pa,&p.hfi_iova))return 0;
    p.log_register=(p.log_iova&0xfffff000)|3u; /* GENERAL_8: 4 pages minus one */
    *out=p;return 1;
}
/* Independent full-image audit after the upstream transport initializer.
 * Expected values derive from Xiaomi init_queues/struct hfi_queue_table.
 * This verifies an empty, stopped queue image only, not a firmware response. */
static int marble_hfi_image_valid(const volatile uint32_t *words,size_t bytes,uint32_t iova)
{
    if(!words || bytes!=MARBLE_GMU_HFI_BYTES || !iova || (iova&4095) || iova>UINT32_MAX-0x3fff)return 0;
    for(unsigned i=0;i<4096;i++) {
        uint32_t expected=0;
        switch(i) {case 1:expected=210;break;case 2:expected=6;break;case 3:expected=12;break;
            case 4:case 5:expected=3;break;default:break;}
        if(i>=6 && i<42) {
            unsigned q=(i-6)/12,field=(i-6)%12;
            switch(field) {
                case 0:expected=1;break;
                case 1:expected=iova+0x350+q*4096;break;
                case 2:expected=(q==2?0x2800:0xa00)|q;break;
                case 3:expected=1024;break;
                default:break;
            }
        }
        if(words[i]!=expected)return 0;
    }
    return 1;
}
#endif
