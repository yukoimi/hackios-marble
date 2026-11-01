/* Native adsp_apps endpoint joining the GLINK ring and audio stream engines.
 * The platform must first own a running DSP, the qualified SMEM/IPCC path,
 * a fresh SID/4K lease and muted amplifier state. No bootstrap, MMIO power
 * control or hardware provider is implemented by this protocol adapter.
 */
#ifndef MARBLE_AUDIO_ENDPOINT_H
#define MARBLE_AUDIO_ENDPOINT_H
#include "marble_audio_stream.h"
#include "marble_audio_glink.h"
typedef int (*MarbleAudioPublish)(void *,const void *,unsigned);
/* -1=fault, 0=wait, 1=board quiet. Called after DATA DONE and TX
 * retirement, before any STOP can be submitted. Default remains ungated. */
typedef int (*MarbleAudioBeforeStop)(void *,const MarbleAudioStream *);
typedef struct {
    uint16_t command,channel;uint32_t parameter,bytes;uint8_t data[16];
} MarbleAudioControl;
typedef struct {
    MarbleAudioStream stream;MarbleAudioGlink ring;MarbleGlinkAssembly rx;
    MarbleGlinkRemoteIntent intents[512];uint8_t available[512],in_use[512];
    unsigned intents_count,remote_channel,version,version_ack,open,open_ack,advertised,ready,failed;
    unsigned pending_slot,pending_cookie,next_cookie,control_head,control_tail;
    MarbleAudioControl controls[8];MarbleAudioPublish publish;
    uint64_t progress_deadline;
    MarbleAudioBeforeStop before_stop;
    MarbleAudioBeforeStop before_data;
    unsigned stop_permitted;
} MarbleAudioEndpoint;
static int mae_fail(MarbleAudioEndpoint *e)
{e->failed=1;marble_audio_stream_fail(&e->stream);return 0;}
static int mae_control(MarbleAudioEndpoint *e,unsigned cmd,unsigned channel,unsigned parameter,const uint8_t *data,unsigned bytes)
{
    if(bytes>16 || (bytes && !data) || e->control_head-e->control_tail>=8)return mae_fail(e);
    MarbleAudioControl *q=&e->controls[e->control_head&7];
    *q=(MarbleAudioControl){.command=(uint16_t)cmd,.channel=(uint16_t)channel,.parameter=parameter,.bytes=bytes};
    for(unsigned i=0;i<bytes;i++)q->data[i]=data[i];e->control_head++;return 1;
}
static int mae_flush(MarbleAudioEndpoint *e)
{
    while(e->control_tail!=e->control_head) {
        MarbleAudioControl *q=&e->controls[e->control_tail&7];
        int sent=marble_audio_glink_send(&e->ring,q->command,q->channel,q->parameter,q->data,q->bytes);
        if(sent<0)return mae_fail(e);
        if(!sent)return 1;
        *q=(MarbleAudioControl){0};e->control_tail++;
    }
    return 1;
}
static int marble_audio_endpoint_init(MarbleAudioEndpoint *e,const MarbleAudioGlinkIO *io,MarbleAudioPublish publish,
    MarbleAudioQueue *queue,void *page,unsigned limit,uint64_t now)
{
    if(!e || e->ring.live || e->stream.live || !publish || now>UINT64_MAX-1000000)return 0;
    *e=(MarbleAudioEndpoint){.publish=publish,.next_cookie=1,.progress_deadline=now+1000000};
    if(!marble_audio_glink_init(&e->ring,io) || !marble_audio_stream_init(&e->stream,queue,page,limit))return mae_fail(e);
    return mae_control(e,0,1,1,0,0);
}
static int mae_name(const char *a,const char *b)
{while(*a && *a==*b){a++;b++;}return *a==*b;}
static int mae_dispatch(MarbleAudioEndpoint *e,const MarbleGlinkControl *m,const uint8_t *frame)
{
    if(m->command==0 || m->command==1) {
        if(m->channel!=1 || !(m->parameter&1))return mae_fail(e);
        if(m->command==0) {if(e->version)return mae_fail(e);e->version=1;return mae_control(e,1,1,1,0,0);}
        if(e->version_ack)return mae_fail(e);e->version_ack=1;return 1;
    }
    if(m->command==2) {
        if(!mae_name(m->name,"adsp_apps"))return 1; /* No intents for unrelated services. */
        if(e->remote_channel || !m->channel)return mae_fail(e);
        e->remote_channel=m->channel;e->open=1;
        static const uint8_t name[10]="adsp_apps";
        return mae_control(e,4,m->channel,0,0,0) && mae_control(e,2,1,10,name,10);
    }
    if(m->command==4) {
        if(m->channel!=1 || m->parameter || !e->open || e->open_ack)return mae_fail(e);
        e->open_ack=1;return 1;
    }
    if(m->command==5) {
        if(!e->remote_channel || m->channel!=e->remote_channel)return 1;
        for(unsigned i=0;i<m->parameter;i++) {
            unsigned index,added;
            if(!marble_glink_intent_accept(e->intents,512,&e->intents_count,
               marble_glink_le32(frame+12+8*i),marble_glink_le32(frame+8+8*i),&index,&added))return mae_fail(e);
            if(!e->in_use[index])e->available[index]=1;
        }
        return 1;
    }
    if(m->command==6 || m->command==14) {
        unsigned slot=e->pending_slot;
        if(m->channel!=e->remote_channel || !e->stream.pending || slot>=e->intents_count ||
           !e->in_use[slot] || e->intents[slot].id!=m->parameter)return mae_fail(e);
        e->in_use[slot]=0;e->available[slot]=m->command==14;
        if(!marble_audio_stream_tx_done(&e->stream,e->pending_cookie,m->command==14))return mae_fail(e);
        return 1;
    }
    if(m->command==9 || m->command==12) {
        if(m->channel!=e->remote_channel || !e->advertised ||
           !marble_glink_fragment(&e->rx,m->command,frame,m->bytes,m->channel))return mae_fail(e);
        if(e->rx.active)return 1;
        int accepted=marble_audio_stream_reply(&e->stream,e->rx.data,e->rx.used);
        e->rx=(MarbleGlinkAssembly){0};
        /* Retire the owned receive intent even when the GPR reply was bad.
         * DMA ownership stays latched; no guessed replacement command. */
        if(!mae_control(e,14,1,1,0,0) || !accepted)return mae_fail(e);
        return 1;
    }
    if(m->command==13)return e->ring.io.kick(e->ring.io.cookie)?1:mae_fail(e);
    if(m->command==15)return 1;
    return mae_fail(e);
}
/* One serialized bounded poll. -1=fault, 0=in progress, 1=protocol retired.
 * The platform must still stop PAS and restore physical resources afterward. */
static int marble_audio_endpoint_poll(MarbleAudioEndpoint *e,uint64_t now)
{
    if(!e || e->failed || e->ring.failed)return -1;
    unsigned retired=marble_audio_stream_protocol_retired(&e->stream);
    /* Continue draining notifications during SMP2P/PAS shutdown, without
     * applying an idle progress deadline to an already retired stream. */
    if(now>UINT64_MAX-1000000 || (!retired &&
       (!marble_audio_stream_tick(&e->stream,now) || now>=e->progress_deadline))) {mae_fail(e);return -1;}
    if(!mae_flush(e))return -1;
    for(unsigned i=0;i<16;i++) {
        uint8_t frame[528];MarbleGlinkControl message;
        int received=marble_audio_glink_peek(&e->ring,frame,sizeof(frame),&message);
        if(received<0) {mae_fail(e);return -1;}
        if(!received)break;
        int accepted=mae_dispatch(e,&message,frame);
        if(!marble_audio_glink_commit_rx(&e->ring)) {mae_fail(e);return -1;}
        e->progress_deadline=now+1000000;
        if(!mae_flush(e) || !accepted)return -1;
    }
    if(e->control_head!=e->control_tail)return 0;
    if(!e->ready && e->version && e->version_ack && e->open && e->open_ack)e->ready=1;
    if(!e->ready)return 0;
    if(!e->advertised) {
        uint8_t intent[8];mag_put32(intent,512);mag_put32(intent+4,1);
        if(!mae_control(e,5,1,1,intent,8) || !mae_flush(e))return -1;
        e->advertised=1;
        if(e->control_head!=e->control_tail)return 0;
    }
    if(marble_audio_stream_protocol_retired(&e->stream))return 1;
    if(e->stream.pending)return 0;
    unsigned phase=e->stream.phase;
    if(phase==MAS_DATA && (e->stream.stop || e->stream.completed==e->stream.limit))phase=MAS_STOP;
    if(phase==MAS_DATA && e->before_data) {
        int ready=e->before_data(e->ring.io.cookie,&e->stream);
        if(ready<0) {mae_fail(e);return -1;}
        if(!ready)return 0;
    }
    if(phase==MAS_STOP && e->before_stop && !e->stop_permitted) {
        int quiet=e->before_stop(e->ring.io.cookie,&e->stream);
        if(quiet<0) {mae_fail(e);return -1;}
        if(!quiet)return 0;
        e->stop_permitted=1;
    }
    unsigned bytes=phase==MAS_MAP?44:phase==MAS_OPEN?608:phase==MAS_DATA?68:phase==MAS_UNMAP?28:64;
    unsigned slot;
    for(slot=0;slot<e->intents_count;slot++)if(e->available[slot] && !e->in_use[slot] && e->intents[slot].bytes>=bytes)break;
    if(slot==e->intents_count)return 0;
    int room=marble_audio_glink_space(&e->ring,(bytes+16+7)&~7u);
    if(room<0) {mae_fail(e);return -1;}
    if(!room)return 0;
    uint8_t payload[616]={0};uint32_t cookie=e->next_cookie++;
    unsigned n=marble_audio_stream_next(&e->stream,payload+8,608,cookie,now);
    if(n!=bytes) {mae_fail(e);return -1;}
    if(e->stream.phase==MAS_DATA && !e->publish(e->ring.io.cookie,e->stream.page,4096)) {mae_fail(e);return -1;}
    mag_put32(payload,n);e->pending_slot=slot;e->pending_cookie=cookie;
    e->in_use[slot]=1;e->available[slot]=0;
    if(marble_audio_glink_send(&e->ring,9,1,e->intents[slot].id,payload,n+8)!=1) {mae_fail(e);return -1;}
    e->progress_deadline=now+1000000;return 0;
}
#endif
