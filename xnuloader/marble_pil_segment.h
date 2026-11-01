/* Pure bounds qualification for the pinned ADSP2 first-segment experiment.
 * Does not dereference a physical address or execute firmware. */
#ifndef MARBLE_PIL_SEGMENT_H
#define MARBLE_PIL_SEGMENT_H
typedef struct { uint64_t target,origin,file_bytes,memory_bytes; } MarblePILSegmentPlan;
static uint32_t marble_pil_u32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int marble_pil_segment_plan(const void *raw,uint64_t bytes,MarblePILSegmentPlan *plan)
{
    const uint8_t *d=raw;
    if(!d || !plan || bytes!=5964 || memcmp(d,"\177ELF\1\1\1",7) ||
       marble_pil_u32(d+16)!=0x00a40002 || marble_pil_u32(d+20)!=1 ||
       marble_pil_u32(d+28)!=52 || marble_pil_u32(d+40)!=0x00200034 ||
       d[44]!=27 || d[45])return 0;
    const uint8_t *first=d+84;
    static const uint32_t expected[8]={1,8192,0xfe100000,0x86300000,6760,8192,0x08000005,4096};
    for(unsigned i=0;i<8;i++)if(marble_pil_u32(first+4*i)!=expected[i])return 0;
    /* phnum will be bounded to 2 in a separate 116-byte view. phdr0 must be
     * non-load, so the native loader can touch only this qualified segment. */
    if(marble_pil_u32(d+52)!=0 || marble_pil_u32(d+68)!=916)return 0;
    for(unsigned i=0;i<27;i++) {
        const uint8_t *p=d+52+32*i;
        uint64_t pa=marble_pil_u32(p+12),file=marble_pil_u32(p+16),mem=marble_pil_u32(p+20);
        uint32_t flags=marble_pil_u32(p+24);
        if(marble_pil_u32(p)!=1 || (flags&0x07000000)==0x02000000 || !mem)continue;
        if(!(flags&0x08000000) || file>mem || pa<0x86300000 ||
           pa+mem>0x100000000ULL || pa-0x86300000+mem>0x3100000)return 0;
        for(unsigned j=0;j<i;j++) {
            const uint8_t *q=d+52+32*j;
            uint64_t other=marble_pil_u32(q+12),length=marble_pil_u32(q+20);
            if(marble_pil_u32(q)==1 && (marble_pil_u32(q+24)&0x07000000)!=0x02000000 &&
               length && pa<other+length && other<pa+mem)return 0;
        }
    }
    plan->target=0x9fd00000;plan->origin=0x86300000;
    plan->file_bytes=6760;plan->memory_bytes=8192;
    return 1;
}
#endif
