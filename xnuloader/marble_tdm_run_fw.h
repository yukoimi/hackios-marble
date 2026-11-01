/* Five distinct remote intents, one request in flight, no reuse or retries.
 * On a decoded failure proceed only to STOP/CLOSE. Malformed packets are
 * retired and leave the control service alive for fresh STOP ACK / PAS. */
#include "marble_audio_pin_guard_fw.h"
#ifdef XNU_MARBLE_TDM_PINS_ONLY
#include "marble_audio_pins_fw.h"
#endif
#ifdef XNU_MARBLE_AUDIO_DMA_INVENTORY
#include "marble_audio_dma_fw.h"
#endif
static struct {unsigned id[5],tx,reply,sent,rejected,next;uint32_t errors[5];} marble_tdm_run;
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
#ifdef XNU_MARBLE_PCM_DATA_ONLY
static unsigned marble_pcm_data_remote_id,marble_pcm_data_remote_used;
#endif
#include "marble_pcm_memory_fw.h"
#ifdef XNU_MARBLE_PCM_DATA_ONLY
#include "marble_pcm_data_fw.h"
#endif
#endif
static int marble_gpr_progress(void)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_glink_control_state.app_ack)return 1;
    if(!marble_gpr.advertised) {
        uint8_t intent[8]={0};marble_gpr_put32(intent,512);marble_gpr_put32(intent+4,1);
        if(!marble_glink_control_send_message(5,1,1,intent,8))return 0;
        marble_gpr.advertised=1;print("tr: i n=512\n");
    }
    unsigned stage=marble_tdm_run.next,mask=marble_tdm_run.sent;
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
    if(marble_audio_dma_done) {
        if(!marble_pcm_memory_progress(stage))return 0;
        if(!marble_pcm_memory_ready())return 1;
    }
#ifdef XNU_MARBLE_PCM_DATA_ONLY
    if(!marble_pcm_data_progress(stage))return 0;
    if(stage==3 && !marble_pcm_data_can_stop())return 1;
#endif
#endif
    if(marble_tdm_run.rejected || stage>=5 || marble_tdm_run.reply!=mask || marble_tdm_run.tx!=mask)return 1;
#ifdef XNU_MARBLE_AUDIO_AMP_PRESTART
    /* Complete the down/muted ROM configuration before START; keep the
     * existing fast START -> pins -> first PCM path free of I2C traffic. */
    if(stage==2 && !marble_i2c_window_attempted && !marble_pcm_i2c_window(2))return 0;
#endif
    unsigned candidates=0;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++)
        candidates+=marble_glink_control_state.app_intent[i].bytes>=64;
    if(candidates<5)return 1;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];unsigned used=0;
        for(unsigned j=0;j<5;j++)if((mask&(1u<<j)) && intent->id==marble_tdm_run.id[j])used=1;
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
        for(unsigned j=0;j<2;j++)if((marble_pcm_memory.sent&(1u<<j)) && intent->id==marble_pcm_memory.id[j])used=1;
#ifdef XNU_MARBLE_PCM_DATA_ONLY
        if(marble_pcm_data.sent && intent->id==marble_pcm_data.id)used=1;
#endif
#endif
        if(used || intent->bytes<(stage?64u:MARBLE_TDM_RUN_OPEN_BYTES))continue;
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
        if(!stage && !marble_audio_dma_done && !marble_audio_clock_guard(0))return 0;
#else
        if(!stage && !marble_audio_clock_guard(0))return 0;
#endif
#ifdef XNU_MARBLE_AUDIO_DMA_INVENTORY
        if(!stage && !marble_audio_dma_done)marble_audio_dma_inventory();
#endif
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
        if(!marble_pcm_memory_progress(stage))return 0;
        if(!marble_pcm_memory_ready())return 1;
#endif
        uint8_t payload[MARBLE_TDM_RUN_OPEN_BYTES+8]={0};unsigned n=marble_tdm_run_command(payload+8,MARBLE_TDM_RUN_OPEN_BYTES,stage);
        marble_gpr_put32(payload,n);
        if(!n || !marble_glink_control_send_message(9,1,intent->id,payload,n+8))return 0;
        marble_tdm_run.id[stage]=intent->id;marble_tdm_run.sent|=1u<<stage;marble_gpr.sent++;
        print("tr: q s=%d t=%x n=%d id=%x cap=%d\n",(uint64_t)stage,
            (uint64_t)(MARBLE_TDM_RUN_TOKEN+stage),(uint64_t)n,(uint64_t)intent->id,(uint64_t)intent->bytes);return 1;
    }
    return 1;
}
static int marble_gpr_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
#ifdef XNU_MARBLE_PCM_DATA_ONLY
    int data=marble_pcm_data_receive(m,raw);if(data)return data>0;
#endif
    int memory=marble_pcm_memory_receive(m,raw);if(memory)return memory>0;
#endif
    if(marble_glink_control_state.app_mode!=2 || !marble_gpr.sent || m->channel!=marble_glink_control_state.app_remote)return 0;
    if(m->command==6 || m->command==14) {
        for(unsigned i=0;i<5;i++)if((marble_tdm_run.sent&(1u<<i)) && m->parameter==marble_tdm_run.id[i]) {
            if(marble_tdm_run.tx&(1u<<i))return 0;
            marble_tdm_run.tx|=1u<<i;marble_gpr.done++;
            print("tr: d s=%d c=%d id=%x\n",(uint64_t)i,(uint64_t)m->command,(uint64_t)m->parameter);return 1;
        }
        return 0;
    }
    if(!marble_gpr.advertised || marble_gpr.replies>=5 ||
       !marble_glink_fragment(&marble_gpr.rx,m->command,raw,m->bytes,m->channel))return 0;
    print("tr: f c=%d n=%d l=%d u=%d f=%d\n",(uint64_t)m->command,
        (uint64_t)marble_glink_le32(raw+8),(uint64_t)marble_gpr.rx.left,(uint64_t)marble_gpr.rx.used,(uint64_t)m->bytes);
    if(marble_gpr.rx.active)return 1;
    unsigned stage=marble_tdm_run.next;MarbleTdmGraphReply reply;marble_gpr.replies++;
    if(marble_tdm_run.rejected || stage>=5 || !(marble_tdm_run.sent&(1u<<stage)) ||
       !marble_tdm_run_reply(marble_gpr.rx.data,marble_gpr.rx.used,stage,&reply)) {
        marble_tdm_run.rejected++;print("tr: rejected\n");
    } else {
        marble_tdm_run.reply|=1u<<stage;marble_tdm_run.errors[stage]=reply.error;
        marble_tdm_run.next=reply.error?(stage<2?4:stage+1):stage+1;
        print("tr: r s=%d t=%x error=%x c=%x n=32\n",(uint64_t)stage,
            (uint64_t)(MARBLE_TDM_RUN_TOKEN+stage),(uint64_t)reply.error,(uint64_t)reply.client_data);
    }
    if(!marble_glink_control_send(14,1,1))return 0;
    memset(&marble_gpr.rx,0,sizeof(marble_gpr.rx));return 1;
}
