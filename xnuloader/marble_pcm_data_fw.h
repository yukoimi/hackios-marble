#include "marble_pcm_data.h"
#include "marble_audio_qup_hold_fw.h"
static struct {unsigned sent,tx,reply,id,bad,zero,faults,logged;MarblePcmDataReply result;MarbleGlinkAssembly rx;} marble_pcm_data;
#ifndef MARBLE_PCM_DATA_PREPARE
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
static int marble_pcm_i2c_window(unsigned stage)
{
    if(!marble_i2c_window_qualified || marble_i2c_window_attempted)return 0;
#ifdef XNU_MARBLE_AUDIO_AMP_PREBOOT
    if(stage!=1 || marble_pil_auth_attempted || marble_tdm_run.sent || marble_tdm_pins.active)return 0;
#elif defined(XNU_MARBLE_AUDIO_AMP_PRESTART)
    if(stage!=2 || marble_tdm_run.sent!=3 || marble_tdm_run.tx!=3 || marble_tdm_run.reply!=3 || marble_tdm_pins.active)return 0;
#else
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    if(stage!=3 || !marble_audio_native_start_qualified() || !marble_tdm_pins.active)return 0;
#else
    if(stage!=3 || marble_tdm_run.sent!=7 || marble_tdm_run.tx!=7 || marble_tdm_run.reply!=7 || !marble_tdm_pins.active)return 0;
#endif
#endif
    marble_i2c_window_attempted=1;
    uint64_t began=marble_i2c_amp_now(NULL);
    print("aw: window begin stage=%d pins_active=%d amp_commands=0\n",(uint64_t)stage,(uint64_t)marble_tdm_pins.active);
    unsigned lookup=marble_i2c_clock_lookup_cached();
    print("aw: lookup PASS=%d after_START=%d amplifier_commands=0\n",(uint64_t)lookup,(uint64_t)(stage==3));
    if(!lookup)return 0;
#ifdef XNU_MARBLE_AUDIO_QUP_HOLD
    if(stage!=3 || !marble_audio_qup_begin())return 0;
#endif
    if(marble_pil_progress)marble_pil_progress("audio-I2C-window-DAIF",READ_SYSREG(DAIF));
    unsigned amp=0;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(!marble_qup_keepalive_held || marble_i2c_amp_commands || marble_i2c_amp_data_writes)return 0;
    marble_i2c_amp_registers_requested=marble_i2c_amp_quiet_requested=marble_i2c_amp_profile_requested=1;
    amp=1;
    if(!marble_i2c_irq_state("audio-amp-IRQ-before"))return 0;
#endif
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    if(stage!=3 || !marble_audio_native_start_qualified() || !marble_tdm_pins.active)return 0;
#else
    if(stage!=3 || marble_tdm_run.sent!=7 || marble_tdm_run.tx!=7 || marble_tdm_run.reply!=7 || !marble_tdm_pins.active)return 0;
#endif
    marble_i2c_amp_digital_authorized=1;
#endif
    marble_i2c_window_passed=marble_i2c_clock_cycle(1,1,1,amp);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
    marble_i2c_amp_digital_authorized=0;
#endif
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(marble_pil_progress)marble_pil_progress("audio-amp-window-return",((uint64_t)marble_i2c_window_passed<<32)|marble_i2c_amp_commands);
    if(marble_pil_progress)marble_pil_progress("audio-amp-window-us",marble_i2c_amp_now(NULL)-began);
    if(!marble_i2c_irq_state("audio-amp-IRQ-after"))return 0;
#endif
    print("aw: window end PASS=%d config_active=%d clock_active=%d amp_commands=%d\n",(uint64_t)marble_i2c_window_passed,
        (uint64_t)marble_i2c_config_lease.active,(uint64_t)marble_i2c_clock_lease.active,(uint64_t)marble_i2c_amp_commands);
    return marble_i2c_window_passed;
}
#endif
static int marble_pcm_data_prepare(void)
{
    if(!marble_pcm_lease.active || !marble_pcm_route_active || !marble_pcm_memory.handle)return 0;
#ifdef XNU_MARBLE_TDM_PINS_ONLY
    if(!marble_tdm_pins_begin())return 0;
#endif
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
#ifdef XNU_MARBLE_AUDIO_AMP_PRESTART
    if(!marble_i2c_window_attempted || !marble_i2c_window_passed)return 0;
#else
    if(!marble_pcm_i2c_window(3))return 0;
#endif
#endif
    uint64_t address=marble_pcm_lease.result.physical;const volatile uint64_t *p=(const volatile uint64_t *)(uintptr_t)address;
    for(unsigned i=0;i<512;i++)if(p[i])return 0;
    clean_dcache(address,address+4096);return 1;
}
#define MARBLE_PCM_DATA_PREPARE marble_pcm_data_prepare
static int marble_pcm_data_faults(void)
{
    uint32_t global,local;const ArmSmmu4KIo *io=&marble_pcm_lease.ops.io;
    return !io->read32(io->cookie,0x48,&global) && !global &&
        !io->read32(io->cookie,0x85058,&local) && !(local&0xc00001feu);
}
#define MARBLE_PCM_DATA_FAULTS marble_pcm_data_faults
#endif
#ifdef XNU_MARBLE_AUDIO_PCM_REPEAT
#include "marble_pcm_repeat_fw.h"
#else
static unsigned marble_pcm_data_ready(void)
{return marble_pcm_data.sent && marble_pcm_data.tx && marble_pcm_data.reply && !marble_pcm_data.bad && !marble_pcm_data.rx.active;}
static unsigned marble_pcm_data_can_stop(void)
{
    /* A rejected preparation never exposed a buffer to DSP. Retire the
     * STARTed graph normally instead of waiting forever for an unsent DMA.
     * Submitted/malformed DMA still requires retirement or verified PAS. */
    return marble_pcm_data_ready() || (!marble_pcm_data.sent && marble_pcm_data.bad && !marble_pcm_data.rx.active);
}
static void marble_pcm_data_log(unsigned stopped)
{
    if(marble_pcm_data.logged || (!stopped && !marble_pcm_data_ready()))return;
    marble_pcm_data.logged=1;marble_pcm_data.faults=MARBLE_PCM_DATA_FAULTS();
    unsigned ok=marble_pcm_data_ready() && marble_pcm_data.result.kind==1 && !marble_pcm_data.result.error &&
        !marble_pcm_data.result.md_error && marble_pcm_data.zero && marble_pcm_data.faults;
    print("pd: end PASS=%d sent=%d tx=%d reply=%d kind=%d error=%x md=%x zero=%d faults_ok=%d bad=%d PCM_requests=%d PAS_stopped=%d\n",
        (uint64_t)ok,(uint64_t)marble_pcm_data.sent,(uint64_t)marble_pcm_data.tx,(uint64_t)marble_pcm_data.reply,
        (uint64_t)marble_pcm_data.result.kind,(uint64_t)marble_pcm_data.result.error,(uint64_t)marble_pcm_data.result.md_error,
        (uint64_t)marble_pcm_data.zero,(uint64_t)marble_pcm_data.faults,(uint64_t)marble_pcm_data.bad,
        (uint64_t)marble_pcm_data.sent,(uint64_t)stopped);
}
static int marble_pcm_data_progress(unsigned stage)
{
    marble_pcm_data_log(0);
    if(stage!=3 || marble_pcm_data.sent || marble_pcm_data.bad || !marble_pcm_memory_ready() ||
       marble_tdm_run.sent!=7 || marble_tdm_run.tx!=7 || marble_tdm_run.reply!=7)return 1;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];unsigned used=0;
        for(unsigned j=0;j<5;j++)if((marble_tdm_run.sent&(1u<<j)) && intent->id==marble_tdm_run.id[j])used=1;
        for(unsigned j=0;j<2;j++)if((marble_pcm_memory.sent&(1u<<j)) && intent->id==marble_pcm_memory.id[j])used=1;
        if(used || intent->bytes<68 || intent->bytes>=528)continue;
        marble_pcm_data.zero=MARBLE_PCM_DATA_PREPARE();
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        if(marble_pil_progress)marble_pil_progress("audio-amp-PCM-prepare",marble_pcm_data.zero);
#endif
        if(!marble_pcm_data.zero) {marble_pcm_data.bad++;return 1;}
        uint8_t payload[76]={0};unsigned n=marble_pcm_data_command(payload+8,68,marble_pcm_memory.handle);marble_gpr_put32(payload,n);
        if(!n || !marble_glink_control_send_message(9,1,intent->id,payload,n+8))return 0;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        if(marble_pil_progress)marble_pil_progress("audio-amp-PCM-sent",intent->id);
#endif
        marble_pcm_data.id=intent->id;marble_pcm_data.sent=1;
        marble_pcm_data_remote_used=marble_pcm_submitted=1;marble_pcm_data_remote_id=intent->id;
        print("pd: q t=%x n=68 id=%x cap=%d handle=%x bytes=%d frames=%d rate=%d bits=%d channels=2 zero=1\n",
            (uint64_t)MARBLE_PCM_DATA_TOKEN,(uint64_t)intent->id,(uint64_t)intent->bytes,(uint64_t)marble_pcm_memory.handle,
            (uint64_t)MARBLE_PCM_DATA_BYTES,(uint64_t)MARBLE_PCM_DATA_FRAMES,(uint64_t)MARBLE_PCM_DATA_RATE,(uint64_t)MARBLE_PCM_DATA_BITS);return 1;
    }
    return 1;
}
static int marble_pcm_data_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
    if(!marble_pcm_data.sent || m->channel!=marble_glink_control_state.app_remote)return 0;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(marble_pil_progress)marble_pil_progress("audio-amp-PCM-rx",((uint64_t)m->command<<32)|m->parameter);
#endif
    if(m->command==6 || m->command==14) {
        if(m->parameter!=marble_pcm_data.id)return 0;
        if(marble_pcm_data.tx)marble_pcm_data.bad++;
        marble_pcm_data.tx=1;print("pd: d c=%d id=%x\n",(uint64_t)m->command,(uint64_t)m->parameter);return 1;
    }
    if(marble_pcm_data.reply)return 0;
    if(!marble_glink_fragment(&marble_pcm_data.rx,m->command,raw,m->bytes,m->channel))return -1;
    print("pd: f c=%d n=%d l=%d u=%d f=%d\n",(uint64_t)m->command,(uint64_t)marble_glink_le32(raw+8),
        (uint64_t)marble_pcm_data.rx.left,(uint64_t)marble_pcm_data.rx.used,(uint64_t)m->bytes);
    if(marble_pcm_data.rx.active)return 1;
    if(!marble_pcm_data_reply(marble_pcm_data.rx.data,marble_pcm_data.rx.used,marble_pcm_memory.handle,&marble_pcm_data.result))marble_pcm_data.bad++;
    else {
        marble_pcm_data.reply=1;
        print("pd: r t=%x error=%x md=%x kind=%d handle=%x n=%d\n",(uint64_t)MARBLE_PCM_DATA_TOKEN,
            (uint64_t)marble_pcm_data.result.error,(uint64_t)marble_pcm_data.result.md_error,(uint64_t)marble_pcm_data.result.kind,
            (uint64_t)marble_pcm_memory.handle,(uint64_t)marble_pcm_data.rx.used);
    }
    if(!marble_glink_control_send(14,1,1))return -1;
    memset(&marble_pcm_data.rx,0,sizeof(marble_pcm_data.rx));return 1;
}

#endif /* single PCM or explicit repeat */
