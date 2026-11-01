/* Selection only: an unreferenced disabled context is not an ownership grant. */
#ifndef MARBLE_AUDIO_CONTEXT_H
#define MARBLE_AUDIO_CONTEXT_H
#include "marble_audio_dma.h"
static unsigned marble_audio_context_candidate(const MarbleAudioDmaSnapshot *s)
{
    if(!s || !marble_audio_dma_geometry(s->scr0,s->id0,s->id1) || s->gfsr ||
       marble_audio_dma_inspect(s).matches || (s->id1>>28)!=6)return 0xffffffffu;
    for(unsigned cb=(s->id1>>16)&255;cb<(s->id1&255);cb++) {
        unsigned used=0;
        for(unsigned i=0;i<(s->id0&255);i++)
            if((s->smr[i]>>31) && (s->s2cr[i]&255)==cb)used=1;
        if(!used)return cb;
    }
    return 0xffffffffu;
}
#endif
