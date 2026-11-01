/* Serialized protocol engine for the marble 48kHz/S16 audio sink.
 * Caller owns the lock, GLINK transport, verified SID/4K DMA lease, cache
 * publication, DSP/amp lifecycle and thermal/watchdog policy. This engine
 * performs no MMIO, secure call, allocation, power vote or audible enable.
 * next() commits ownership BEFORE the caller publishes/sends the packet;
 * a failed send must call fail(), never roll back or reuse its DMA page.
 * DONE and TX retirement are independently required. Even successful UNMAP
 * is only protocol retirement: reset additionally requires external proof
 * of PAS-off and exact route restoration. No hardware provider exists yet.
 */
#ifndef MARBLE_AUDIO_STREAM_H
#define MARBLE_AUDIO_STREAM_H
#include <stdint.h>
#include "marble_audio_queue.h"
#define XNU_MARBLE_TDM_PCM_GRAPH_ONLY 1
#define XNU_MARBLE_AUDIO_MFC 1
#define MARBLE_PCM_REQUEST_LIMIT 2048
#include "marble_audio_wire/marble_tdm_run.h"
#include "marble_audio_wire/marble_pcm_memory.h"
#include "marble_audio_wire/marble_pcm_data.h"
#include "marble_audio_proof.h"
enum { MAS_MAP, MAS_OPEN, MAS_PREPARE, MAS_START, MAS_DATA, MAS_STOP,
       MAS_CLOSE, MAS_UNMAP, MAS_RETIRED };
typedef struct {
    MarbleAudioQueue *queue;
    uint8_t *page;
    uint32_t phase,live,failed,pending,tx,reply,stop,handle,cookie;
    uint32_t submitted,completed,limit,start_retired;
    uint64_t deadline_us;
    MarbleAudioProof proof;
    uint32_t diagnostic_reply_bytes;uint8_t diagnostic_reply[64];
} MarbleAudioStream;
static int marble_audio_stream_fail(MarbleAudioStream *s)
{if(s)s->failed=1;return 0;}
static int marble_audio_stream_init(MarbleAudioStream *s,MarbleAudioQueue *q,void *page,unsigned limit)
{
    if(!s || s->live || !q || !page || ((uintptr_t)page&4095) || !limit || limit>2048 || !marble_audio_queue_valid(q))return 0;
    *s=(MarbleAudioStream){.queue=q,.page=page,.phase=MAS_MAP,.live=1,.limit=limit};
    for(unsigned i=0;i<4096;i++)s->page[i]=0;
    return 1;
}
static void marble_audio_stream_stop(MarbleAudioStream *s)
{if(s && s->live)s->stop=1;}
/* Bind only before the first command. Production owner must supply the
 * session epoch; initialization alone cannot mint an accepted proof. */
static int marble_audio_stream_bind_epoch(MarbleAudioStream *s,uint64_t epoch)
{
    if(!s || !s->live || s->failed || s->phase!=MAS_MAP || s->pending || s->proof.epoch || !epoch)return 0;
    s->proof.epoch=epoch;return 1;
}
static unsigned marble_audio_stream_stage(unsigned phase)
{return phase<=MAS_START?phase-1:phase==MAS_STOP?3:4;}
static int marble_audio_stream_tick(MarbleAudioStream *s,uint64_t now_us)
{
    if(!s || !s->live || s->failed)return 0;
    if(s->pending && now_us>=s->deadline_us)return marble_audio_stream_fail(s);
    return 1;
}
static unsigned marble_audio_stream_next(MarbleAudioStream *s,uint8_t *out,unsigned capacity,uint32_t cookie,uint64_t now_us)
{
    if(!s || !s->live || s->failed || s->pending || !out || !cookie || now_us>UINT64_MAX-1000000 || s->phase==MAS_RETIRED)return 0;
    if(s->phase==MAS_DATA && (s->stop || s->completed==s->limit))s->phase=MAS_STOP;
    unsigned n=0;
    if(s->phase==MAS_MAP || s->phase==MAS_UNMAP)
        n=marble_pcm_memory_command(out,capacity,s->phase==MAS_UNMAP,s->phase==MAS_UNMAP?s->handle:0);
    else if(s->phase==MAS_DATA) {
        if(s->submitted!=s->completed || s->submitted>=s->limit || capacity<68)return 0;
        n=marble_pcm_data_command_index(out,capacity,s->handle,s->submitted);
        if(!n)return 0;
        /* Queue pop and page publication occur only after prior ownership
         * retired. A caller must clean the page before sending this packet. */
        int16_t frames[512];
        marble_audio_queue_pop(s->queue,frames,256);
        if(s->queue->failed)return marble_audio_stream_fail(s);
        for(unsigned i=0;i<512;i++) {
            uint16_t v=(uint16_t)frames[i];s->page[2*i]=(uint8_t)v;s->page[2*i+1]=(uint8_t)(v>>8);
        }
        s->submitted++;
    } else n=marble_tdm_run_command(out,capacity,marble_audio_stream_stage(s->phase));
    if(!n)return 0;
    if(s->phase==MAS_START)s->proof.start_cookie=cookie;
    if(s->phase==MAS_UNMAP)s->proof.unmap_cookie=cookie;
    s->pending=1;s->tx=s->reply=0;s->cookie=cookie;s->deadline_us=now_us+1000000;
    return n;
}
static void marble_audio_stream_retire(MarbleAudioStream *s)
{
    if(s->failed || !s->pending || !s->tx || !s->reply)return;
    s->pending=0;
    if(s->phase==MAS_DATA) {
        s->completed++;
        if(s->stop || s->completed==s->limit)s->phase=MAS_STOP;
    } else if(s->phase==MAS_UNMAP) {s->handle=0;s->phase=MAS_RETIRED;}
    else {
        if(s->phase==MAS_START)s->start_retired=1;
        s->phase++;
    }
}
static int marble_audio_stream_tx_done(MarbleAudioStream *s,uint32_t cookie,unsigned reuse)
{
    if(!s || !s->live || s->failed)return 0;
    if(!s->pending || s->tx || cookie!=s->cookie || reuse>1 || (s->phase==MAS_DATA && !reuse))return marble_audio_stream_fail(s);
    if(s->phase==MAS_MAP)s->proof.flags|=MAPROOF_MAP_TX;
    if(s->phase==MAS_START)s->proof.flags|=MAPROOF_START_TX;
    if(s->phase==MAS_UNMAP)s->proof.flags|=MAPROOF_UNMAP_TX;
    s->tx=1;marble_audio_stream_retire(s);return 1;
}
static int marble_audio_stream_reply(MarbleAudioStream *s,const uint8_t *data,unsigned bytes)
{
    if(!s || !s->live || s->failed)return 0;
    s->diagnostic_reply_bytes=data?bytes:0;
    for(unsigned i=0;i<64;i++)s->diagnostic_reply[i]=data && i<bytes?data[i]:0;
    if(!s->pending || s->reply)return marble_audio_stream_fail(s);
    if(s->phase==MAS_MAP || s->phase==MAS_UNMAP) {
        MarblePcmMemoryReply r;
        if(!marble_pcm_memory_reply(data,bytes,s->phase==MAS_UNMAP,&r) || r.error ||
           r.kind!=(s->phase==MAS_MAP?1u:2u))return marble_audio_stream_fail(s);
        if(s->phase==MAS_MAP) {
            s->handle=r.handle;s->proof.handle=r.handle;s->proof.flags|=MAPROOF_MAP_RX;
            for(unsigned i=0;i<28;i++)s->proof.map[i]=data[i];
        } else {
            s->proof.flags|=MAPROOF_UNMAP_RX;
            for(unsigned i=0;i<32;i++)s->proof.unmap[i]=data[i];
        }
    } else if(s->phase==MAS_DATA) {
        MarblePcmDataReply r;
        if(!marble_pcm_data_reply_index(data,bytes,s->handle,s->submitted-1,&r) || r.kind!=1 || r.error || r.md_error)return marble_audio_stream_fail(s);
    } else {
        MarbleTdmGraphReply r;
        if(!marble_tdm_run_reply(data,bytes,marble_audio_stream_stage(s->phase),&r) || r.error)return marble_audio_stream_fail(s);
        if(s->phase==MAS_START) {
            s->proof.flags|=MAPROOF_START_RX;
            for(unsigned i=0;i<32;i++)s->proof.start[i]=data[i];
        }
    }
    s->reply=1;marble_audio_stream_retire(s);return 1;
}
static int marble_audio_stream_protocol_retired(const MarbleAudioStream *s)
{return s && s->live && !s->failed && !s->pending && !s->handle && s->phase==MAS_RETIRED;}
static int marble_audio_stream_export_proof(const MarbleAudioStream *s,unsigned phase,MarbleAudioProof *out)
{
    if(!s || !out || !s->live || s->failed || !s->proof.epoch || !s->start_retired)return 0;
    if(phase==MAPROOF_CONFIGURE) {
        if(s->phase!=MAS_DATA || !s->pending || s->submitted!=1 || s->completed || s->tx || s->reply)return 0;
    } else if(phase!=MAPROOF_RETIRED || !marble_audio_stream_protocol_retired(s))return 0;
    MarbleAudioProof proof=s->proof;
    proof.phase=phase;proof.submitted=s->submitted;proof.completed=s->completed;
    if(!marble_audio_proof_valid(&proof,proof.epoch,phase))return 0;
    *out=proof;return 1;
}
static int marble_audio_stream_reset_after_pas(MarbleAudioStream *s,unsigned PAS_off,unsigned route_restored)
{
    if(!s || !s->live || PAS_off!=1 || route_restored!=1)return 0;
    for(unsigned i=0;i<4096;i++)s->page[i]=0;
    *s=(MarbleAudioStream){0};return 1;
}
#endif
