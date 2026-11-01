#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_audio_dma.h"
int main(void)
{
    MarbleAudioDmaSnapshot s={.id0=0x48000078,.id1=78};MarbleAudioDmaAudit a=marble_audio_dma_inspect(&s);
    assert(a.valid && !a.matches && a.first_match==0xffffffffu && a.first_free==0);
    s.smr[3]=0x80001801;s.s2cr[3]=1u<<16;a=marble_audio_dma_inspect(&s);assert(a.valid && a.matches==1 && a.first_match==3 && a.type==1);
    s.smr[7]=0x80ff1800;s.s2cr[7]=17;a=marble_audio_dma_inspect(&s);assert(a.matches==2 && a.first_match==3);
    s.scr0=8;assert(!marble_audio_dma_inspect(&s).valid);s.scr0=0;s.id0=0x48000079;assert(!marble_audio_dma_inspect(&s).valid);
    s.id0=0x48000078;s.id1=79;assert(!marble_audio_dma_inspect(&s).valid);s.id1=255;assert(!marble_audio_dma_inspect(&s).valid);
    s.id0=0x4c017e71;s.id1=0x6000004b;assert(marble_audio_dma_inspect(&s).valid);
    assert(!marble_audio_dma_inspect(NULL).valid);puts("PASS passive audio SID/mask route matching, geometry limits and ambiguity; no ownership inferred");
}
