/* Separate counters/assembly from the five graph commands, one in flight.
 * Keep physical tables and SID until typed UNMAP or verified PAS shutdown. */
#include "marble_pcm_memory.h"
static struct {unsigned sent,tx,reply,id[2],rejected,finished;uint32_t handle,error[2];MarbleGlinkAssembly rx;} marble_pcm_memory;
static unsigned marble_pcm_memory_ready(void)
{return marble_pcm_memory.sent==1 && marble_pcm_memory.tx==1 && marble_pcm_memory.reply==1 && !marble_pcm_memory.rejected && marble_pcm_memory.handle && !marble_pcm_memory.error[0];}
static void marble_pcm_memory_finish(unsigned stopped)
{
    if(marble_pcm_memory.finished || !marble_pcm_lease.active)return;
    unsigned complete=marble_pcm_memory.sent==3 && marble_pcm_memory.tx==3 && marble_pcm_memory.reply==3 &&
        !marble_pcm_memory.rejected && marble_pcm_memory.handle && !marble_pcm_memory.error[0] && !marble_pcm_memory.error[1] && !marble_pcm_memory.rx.active;
    if(!complete && !stopped)return;
    unsigned pcm_requests=0;
#ifdef XNU_MARBLE_PCM_DATA_ONLY
    pcm_requests=marble_pcm_submitted;
#endif
    marble_pcm_memory.finished=1;MarbleAudioMapResult r=marble_pcm_lease.result;
    unsigned route=marble_pcm_route(0),clean=route && marble_audio_map_end(&marble_pcm_lease,&r);
    print("pm: end PASS=%d sent=%x tx=%x reply=%x handle=%x error=%x,%x bad=%d route_restored=%d cleanup=%d PAS_stopped=%d PCM_requests=%d\n",
        (uint64_t)(complete&&clean),(uint64_t)marble_pcm_memory.sent,(uint64_t)marble_pcm_memory.tx,(uint64_t)marble_pcm_memory.reply,
        (uint64_t)marble_pcm_memory.handle,(uint64_t)marble_pcm_memory.error[0],(uint64_t)marble_pcm_memory.error[1],
        (uint64_t)marble_pcm_memory.rejected,(uint64_t)route,(uint64_t)clean,(uint64_t)stopped,(uint64_t)pcm_requests);
    marble_audio_map_log(&r,complete&&clean);
}
static int marble_pcm_memory_progress(unsigned graph_stage)
{
    if(graph_stage>=5 && (marble_tdm_run.sent!=31 || marble_tdm_run.tx!=31 || marble_tdm_run.reply!=31))return 1;
#ifdef XNU_MARBLE_TDM_PINS_ONLY
    if(graph_stage>=5 && !marble_tdm_pins_finish(0))return 1;
#endif
    if(marble_pcm_memory.finished || marble_pcm_memory.rejected || !marble_pcm_lease.active || !marble_pcm_route_active)return 1;
    marble_pcm_memory_finish(0);if(marble_pcm_memory.finished)return 1;
    unsigned stage=marble_pcm_memory.sent?1:0;
    if(stage && (graph_stage<5 || !marble_pcm_memory_ready()))return 1;
    if(marble_pcm_memory.sent&(1u<<stage))return 1;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];unsigned used=0;
        for(unsigned j=0;j<5;j++)if((marble_tdm_run.sent&(1u<<j)) && marble_tdm_run.id[j]==intent->id)used=1;
        for(unsigned j=0;j<2;j++)if((marble_pcm_memory.sent&(1u<<j)) && marble_pcm_memory.id[j]==intent->id)used=1;
#ifdef XNU_MARBLE_PCM_DATA_ONLY
        if(marble_pcm_data_remote_used && marble_pcm_data_remote_id==intent->id)used=1;
#endif
        if(used || intent->bytes<44 || intent->bytes>=528)continue;
        uint8_t payload[52]={0};unsigned n=marble_pcm_memory_command(payload+8,44,stage,stage?marble_pcm_memory.handle:0);
        marble_gpr_put32(payload,n);
        if(!n || !marble_glink_control_send_message(9,1,intent->id,payload,n+8))return 0;
        marble_pcm_memory.id[stage]=intent->id;marble_pcm_memory.sent|=1u<<stage;
        print("pm: q s=%d t=%x n=%d id=%x cap=%d\n",(uint64_t)stage,(uint64_t)(MARBLE_PCM_MEMORY_TOKEN+stage),(uint64_t)n,(uint64_t)intent->id,(uint64_t)intent->bytes);return 1;
    }
    return 1;
}
static int marble_pcm_memory_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
    if(!marble_pcm_memory.sent || m->channel!=marble_glink_control_state.app_remote)return 0;
    if(m->command==6 || m->command==14) {
        for(unsigned i=0;i<2;i++)if((marble_pcm_memory.sent&(1u<<i)) && m->parameter==marble_pcm_memory.id[i]) {
            if(marble_pcm_memory.tx&(1u<<i))marble_pcm_memory.rejected++;
            marble_pcm_memory.tx|=1u<<i;
            print("pm: d s=%d c=%d id=%x\n",(uint64_t)i,(uint64_t)m->command,(uint64_t)m->parameter);return 1;
        }
        return 0;
    }
    if(marble_pcm_memory.sent==marble_pcm_memory.reply)return 0;
    unsigned stage=marble_pcm_memory.sent==1?0:1;
    if(!marble_glink_fragment(&marble_pcm_memory.rx,m->command,raw,m->bytes,m->channel))return -1;
    print("pm: f c=%d n=%d l=%d u=%d f=%d\n",(uint64_t)m->command,(uint64_t)marble_glink_le32(raw+8),
        (uint64_t)marble_pcm_memory.rx.left,(uint64_t)marble_pcm_memory.rx.used,(uint64_t)m->bytes);
    if(marble_pcm_memory.rx.active)return 1;
    MarblePcmMemoryReply reply;
    if(!marble_pcm_memory_reply(marble_pcm_memory.rx.data,marble_pcm_memory.rx.used,stage,&reply))marble_pcm_memory.rejected++;
    else {
        marble_pcm_memory.reply|=1u<<stage;marble_pcm_memory.error[stage]=reply.error;
        if(!stage)marble_pcm_memory.handle=reply.handle;
        print("pm: r s=%d t=%x error=%x handle=%x c=%x n=%d\n",(uint64_t)stage,(uint64_t)(MARBLE_PCM_MEMORY_TOKEN+stage),
            (uint64_t)reply.error,(uint64_t)reply.handle,(uint64_t)reply.client,(uint64_t)marble_pcm_memory.rx.used);
    }
    if(!marble_glink_control_send(14,1,1))return -1;
    memset(&marble_pcm_memory.rx,0,sizeof(marble_pcm_memory.rx));return 1;
}
