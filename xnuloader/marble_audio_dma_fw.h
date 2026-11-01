/* Passive GR0 access only. No context registers, ATS, SMR/S2CR writes, DMA,
 * TBU halts, fault clears, allocations or requests to a native IOMMU driver. */
#include "marble_audio_dma.h"
#ifdef XNU_MARBLE_AUDIO_CONTEXT_INVENTORY
#include "marble_audio_context_fw.h"
#endif
static unsigned marble_audio_dma_done;
static unsigned marble_audio_dma_reads,marble_audio_dma_reason;
static uint64_t marble_audio_dma_par;
#ifndef MARBLE_AUDIO_DMA_READ32
static int marble_audio_dma_read32(unsigned offset,uint32_t *out)
{
    if(!out || (offset!=0 && offset!=0x20 && offset!=0x24 && offset!=0x48 &&
       !(offset>=0x800 && offset<0x9e0 && !(offset&3)) &&
       !(offset>=0xc00 && offset<0xde0 && !(offset&3))))return 0;
    uint64_t par=marble_fw_translation(0x15000000ULL);
    marble_audio_dma_par=par;
    if((par&1) || (par&0x0000fffffffff000ULL)!=0x15000000ULL) {marble_audio_dma_reason=1;return 0;}
    *out=*(volatile uint32_t *)(uintptr_t)(0x15000000ULL+offset);
    __asm__ volatile("dmb sy":::"memory");return 1;
}
#define MARBLE_AUDIO_DMA_READ32 marble_audio_dma_read32
#endif
static int marble_audio_dma_read_checked(unsigned offset,uint32_t *out)
{
    if(!MARBLE_AUDIO_DMA_READ32(offset,out))return 0;
    marble_audio_dma_reads++;return 1;
}
static int marble_audio_dma_snapshot(MarbleAudioDmaSnapshot *s)
{
    if(!marble_audio_dma_read_checked(0,&s->scr0) || !marble_audio_dma_read_checked(0x20,&s->id0) ||
       !marble_audio_dma_read_checked(0x24,&s->id1) || !marble_audio_dma_read_checked(0x48,&s->gfsr))return 0;
    if(!marble_audio_dma_geometry(s->scr0,s->id0,s->id1)) {marble_audio_dma_reason=2;return 0;}
    for(unsigned i=0;i<(s->id0&255);i++)if(!marble_audio_dma_read_checked(0x800+4*i,&s->smr[i]) ||
       !marble_audio_dma_read_checked(0xc00+4*i,&s->s2cr[i]))return 0;
    return 1;
}
static int marble_audio_dma_inventory(void)
{
    MarbleAudioDmaSnapshot before={0},after={0};marble_audio_dma_done=1;marble_audio_dma_reads=marble_audio_dma_reason=0;
    print("marble-audio-dma: begin base=15000000 bytes=4096 SID=1801 MMIO_writes=0\n");
    if(!marble_audio_dma_snapshot(&before) || !marble_audio_dma_snapshot(&after)) {
        print("marble-audio-dma: refused reason=%d reads=%d par=%x scr0=%x id0=%x id1=%x MMIO_writes=0\n",(uint64_t)marble_audio_dma_reason,(uint64_t)marble_audio_dma_reads,marble_audio_dma_par,(uint64_t)before.scr0,(uint64_t)before.id0,(uint64_t)before.id1);return 0;
    }
    unsigned stable=!memcmp(&before,&after,sizeof(before));MarbleAudioDmaAudit a=marble_audio_dma_inspect(&before);
    print("marble-audio-dma: geometry scr0=%x id0=%x id1=%x gfsr=%x\n",(uint64_t)before.scr0,(uint64_t)before.id0,(uint64_t)before.id1,(uint64_t)before.gfsr);
    unsigned count=before.id0&255;
    for(unsigned i=0;i<count;i++)print("marble-audio-dma: route index=%d smr=%x s2cr=%x\n",(uint64_t)i,(uint64_t)before.smr[i],(uint64_t)before.s2cr[i]);
    print("marble-audio-dma: complete PASS=%d stable=%d routes=%d matches=%d first=%x free=%x type=%d context=%d MMIO_reads=%d MMIO_writes=0 context_reads=0 DMA_requests=0\n",
        (uint64_t)(a.valid && stable),(uint64_t)stable,(uint64_t)count,(uint64_t)a.matches,(uint64_t)a.first_match,
        (uint64_t)a.first_free,(uint64_t)a.type,(uint64_t)a.context,(uint64_t)marble_audio_dma_reads);
#ifdef XNU_MARBLE_AUDIO_CONTEXT_INVENTORY
    if(a.valid && stable)marble_audio_context_inventory(&before);
#endif
    return a.valid && stable;
}
