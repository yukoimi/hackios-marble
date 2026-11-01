/* Passive candidate inspection: no writes, ATS, allocation or DMA. */
#include "marble_audio_context.h"
static const unsigned marble_audio_context_regs[]={0,0x10,0x20,0x24,0x28,0x2c,0x30,0x38,0x3c,0x58,0x7f4};
#ifndef MARBLE_AUDIO_CONTEXT_READ32
static int marble_audio_context_read32(unsigned offset,uint32_t *out)
{
    unsigned page=offset&~4095u;
    if(!out || (offset&3) || offset>=0x100000)return 0;
    uint64_t par=marble_fw_translation(0x15000000ULL+page);
    if((par&1) || (par&0x0000fffffffff000ULL)!=0x15000000ULL+page)return 0;
    *out=*(volatile uint32_t *)(uintptr_t)(0x15000000ULL+offset);
    __asm__ volatile("dmb sy":::"memory");return 1;
}
#define MARBLE_AUDIO_CONTEXT_READ32 marble_audio_context_read32
#endif
#ifdef XNU_MARBLE_AUDIO_MAP_PROBE
#include "marble_audio_map_fw.h"
#endif
static int marble_audio_context_inventory(const MarbleAudioDmaSnapshot *s)
{
    unsigned cb=marble_audio_context_candidate(s),stable=1;uint32_t v[2][15]={{0}};
    if(cb==0xffffffffu) {print("marble-audio-context: refused candidate=none MMIO_writes=0\n");return 0;}
    unsigned base=0x80000+cb*4096;
    print("marble-audio-context: begin context=%d base=%x MMIO_writes=0\n",(uint64_t)cb,(uint64_t)(0x15000000+base));
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned i=0;i<11;i++)if(!MARBLE_AUDIO_CONTEXT_READ32(base+marble_audio_context_regs[i],&v[pass][i]))return 0;
        if(!MARBLE_AUDIO_CONTEXT_READ32(0x1000+4*cb,&v[pass][11]) ||
           !MARBLE_AUDIO_CONTEXT_READ32(0x1800+4*cb,&v[pass][12]) ||
           !MARBLE_AUDIO_CONTEXT_READ32(base+4,&v[pass][13]) ||
           !MARBLE_AUDIO_CONTEXT_READ32(0x28,&v[pass][14]))return 0;
    }
    for(unsigned i=0;i<15;i++) {
        if(v[0][i]!=v[1][i])stable=0;
        print("marble-audio-context: word index=%d value=%x second=%x\n",(uint64_t)i,(uint64_t)v[0][i],(uint64_t)v[1][i]);
    }
    unsigned inactive=!(v[0][0]&~0xe0u) && !v[0][2] && !v[0][3] && !v[0][4] && !v[0][5] && !(v[0][9]&0xc00001feu) && !(v[0][10]&1);
    print("marble-audio-context: complete PASS=%d stable=%d inactive=%d reads=30 MMIO_writes=0 DMA_requests=0 ownership=0\n",(uint64_t)(stable&&inactive),(uint64_t)stable,(uint64_t)inactive);
#ifdef XNU_MARBLE_AUDIO_MAP_PROBE
    if(stable&&inactive)marble_audio_map_probe(s,v[0]);
#endif
    return stable&&inactive;
}
