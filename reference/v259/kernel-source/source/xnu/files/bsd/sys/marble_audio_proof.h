/* Fixed-size resource-owner proof. Contains real typed GPR replies retained
 * by the serialized endpoint, never legacy firmware progress counters.
 * The epoch binds it to one externally owned session. Cache publication and
 * proof transport belong to the caller; this codec does not touch hardware.
 * TX flags are assertions by the trusted endpoint after cookie validation,
 * not cryptographic authentication or independent observation of GLINK. */
#ifndef MARBLE_AUDIO_PROOF_H
#define MARBLE_AUDIO_PROOF_H
#include <stdint.h>
#include "marble_audio_wire/marble_tdm_run.h"
#include "marble_audio_wire/marble_pcm_memory.h"
enum { MAPROOF_CONFIGURE=1,MAPROOF_RETIRED=2 };
enum { MAPROOF_MAP_RX=1,MAPROOF_MAP_TX=2,MAPROOF_START_RX=4,
       MAPROOF_START_TX=8,MAPROOF_UNMAP_RX=16,MAPROOF_UNMAP_TX=32 };
typedef struct {
    uint64_t epoch;
    uint32_t phase,handle,start_cookie,unmap_cookie,submitted,completed;
    uint8_t map[28],start[32],unmap[32];
    uint32_t flags;
} MarbleAudioProof;
_Static_assert(sizeof(MarbleAudioProof)==128,"audio proof ABI must be two cache lines");
static int marble_audio_proof_valid(const MarbleAudioProof *p,uint64_t epoch,unsigned phase)
{
    if(!p || !epoch || p->epoch!=epoch || p->phase!=phase ||
       (phase!=MAPROOF_CONFIGURE && phase!=MAPROOF_RETIRED) || !p->start_cookie ||
       p->submitted>2048 || p->completed>p->submitted)return 0;
    MarblePcmMemoryReply map;
    MarbleTdmGraphReply start;
    if(!marble_pcm_memory_reply(p->map,28,0,&map) || map.kind!=1 || map.error || map.handle!=p->handle ||
       !marble_tdm_run_reply(p->start,32,2,&start) || start.error)return 0;
    if(phase==MAPROOF_CONFIGURE) {
        if(p->flags!=15 || p->unmap_cookie || p->submitted!=1 || p->completed)return 0;
        for(unsigned i=0;i<32;i++)if(p->unmap[i])return 0;
    } else {
        MarblePcmMemoryReply unmap;
        if(p->flags!=63 || !p->unmap_cookie || p->unmap_cookie==p->start_cookie ||
           !p->submitted || p->submitted!=p->completed ||
           !marble_pcm_memory_reply(p->unmap,32,1,&unmap) || unmap.kind!=2 || unmap.error)return 0;
    }
    return 1;
}
#endif
