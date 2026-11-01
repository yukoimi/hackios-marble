/* Actual Android FDT: apps-smmu@15000000, 1MiB, override 120 SMRs/78 CBs;
 * audio-ion SID1801/maskf, coherent IOVA pool10000000..1fffffff.
 * This only interprets a read-only route snapshot. It grants no ownership. */
#ifndef MARBLE_AUDIO_DMA_H
#define MARBLE_AUDIO_DMA_H
#include <stdint.h>
typedef struct {uint32_t scr0,id0,id1,gfsr,smr[120],s2cr[120];} MarbleAudioDmaSnapshot;
typedef struct {unsigned valid,matches,first_match,first_free,type,context,global_fault;} MarbleAudioDmaAudit;
static int marble_audio_dma_geometry(uint32_t scr0,uint32_t id0,uint32_t id1)
{
    /* FDT overrides are ceilings, not minimum counts. This marble reports
     * 113 SMRs/75 CBs in its raw ID registers. Never read the absent tail. */
    return id0!=0xffffffffu && id1!=0xffffffffu && !(scr0&8) &&
        (id0&(1u<<27)) && (id0&(1u<<30)) &&
        (id0&255)>0 && (id0&255)<=120 && (id1&255)>0 && (id1&255)<=78;
}
static MarbleAudioDmaAudit marble_audio_dma_inspect(const MarbleAudioDmaSnapshot *s)
{
    MarbleAudioDmaAudit r={0};r.first_match=r.first_free=0xffffffffu;
    if(!s || !marble_audio_dma_geometry(s->scr0,s->id0,s->id1))return r;
    r.valid=1;r.global_fault=s->gfsr;
    for(unsigned i=0;i<(s->id0&255);i++) {
        uint32_t v=s->smr[i],route=s->s2cr[i],sid=v&0x7fff,mask=(v>>16)&0x7fff;
        if(!(v>>31)) {if(r.first_free==0xffffffffu)r.first_free=i;continue;}
        if(!((sid^0x1801u)&~mask)) {
            r.matches++;
            if(r.first_match==0xffffffffu) {r.first_match=i;r.type=(route>>16)&3;r.context=route&255;}
        }
    }
    return r;
}
#endif
