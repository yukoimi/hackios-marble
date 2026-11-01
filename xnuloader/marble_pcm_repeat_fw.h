/* Bounded serial WREP buffers in the existing owned 4K page. No page write or
 * cache publication until both typed DONE and RX_DONE_W_REUSE retire the
 * previous request. Independent GPR tokens reject delayed old replies.
 * This is bounded silent recycling, not a continuous audio feeder. */
static struct {unsigned completed,reuse,logged;} marble_pcm_repeat;
#ifndef MARBLE_PCM_REPEAT_RECHECK
static int marble_pcm_repeat_recheck(void)
{
    if(!marble_pcm_lease.active || !marble_pcm_route_active || !marble_pcm_memory.handle ||
       !marble_tdm_pins.active || !marble_i2c_window_passed || marble_i2c_amp_dirty || !MARBLE_PCM_DATA_FAULTS())return 0;
    uint64_t address=marble_pcm_lease.result.physical;
    const volatile uint64_t *p=(const volatile uint64_t *)(uintptr_t)address;
    for(unsigned i=0;i<512;i++)if(p[i])return 0;
    clean_dcache(address,address+4096);return 1;
}
#define MARBLE_PCM_REPEAT_RECHECK marble_pcm_repeat_recheck
#endif
static unsigned marble_pcm_repeat_retired(void)
{
    return marble_pcm_data.sent==1 && marble_pcm_data.tx==1 && marble_pcm_data.reply==1 &&
        !marble_pcm_data.bad && !marble_pcm_data.rx.active && marble_pcm_data.result.kind==1 &&
        !marble_pcm_data.result.error && !marble_pcm_data.result.md_error && marble_pcm_data.zero;
}
static unsigned marble_pcm_data_ready(void)
{return marble_pcm_repeat.completed==MARBLE_PCM_REPEAT_COUNT && marble_pcm_submitted==MARBLE_PCM_REPEAT_COUNT && marble_pcm_repeat_retired() && marble_pcm_data.faults;}
static unsigned marble_pcm_data_can_stop(void)
{
    return marble_pcm_data_ready() || (marble_pcm_data.bad && !marble_pcm_data.rx.active &&
        (!marble_pcm_data.sent || (marble_pcm_data.tx && marble_pcm_data.reply)));
}
static void marble_pcm_data_log(unsigned stopped)
{
    if(marble_pcm_data.logged || (!stopped && !marble_pcm_data_ready()))return;
    marble_pcm_data.logged=1;
    unsigned ok=marble_pcm_data_ready() && MARBLE_PCM_DATA_FAULTS();
    print("pd: end PASS=%d sent=%d tx=%d reply=%d kind=%d error=%x md=%x zero=%d faults_ok=%d bad=%d PCM_requests=%d PAS_stopped=%d\n",
        (uint64_t)ok,(uint64_t)marble_pcm_data.sent,(uint64_t)marble_pcm_data.tx,(uint64_t)marble_pcm_data.reply,
        (uint64_t)marble_pcm_data.result.kind,(uint64_t)marble_pcm_data.result.error,(uint64_t)marble_pcm_data.result.md_error,
        (uint64_t)marble_pcm_data.zero,(uint64_t)marble_pcm_data.faults,(uint64_t)marble_pcm_data.bad,
        (uint64_t)marble_pcm_submitted,(uint64_t)stopped);
    print("pr: end PASS=%d submitted=%d completed=%d unique_tokens=%d page_bytes=4096 reuse_after_DONE_and_TX=1 zero=1 audio_acceptance=0\n",
        (uint64_t)ok,(uint64_t)marble_pcm_submitted,(uint64_t)marble_pcm_repeat.completed,(uint64_t)MARBLE_PCM_REPEAT_COUNT);
}
static int marble_pcm_data_progress(unsigned stage)
{
    if(marble_pcm_repeat_retired() && marble_pcm_repeat.completed<marble_pcm_submitted) {
        marble_pcm_data.faults=MARBLE_PCM_DATA_FAULTS();
        if(!marble_pcm_data.faults || !marble_pcm_repeat.reuse) {marble_pcm_data.bad++;return 1;}
        print("pr: retired index=%d token=%x id=%x faults_ok=1 zero=1\n",(uint64_t)marble_pcm_repeat.completed,
            (uint64_t)(MARBLE_PCM_DATA_TOKEN+marble_pcm_repeat.completed),(uint64_t)marble_pcm_data.id);
        marble_pcm_repeat.completed++;
    }
    marble_pcm_data_log(0);
    if(stage!=3 || marble_pcm_repeat.completed==MARBLE_PCM_REPEAT_COUNT || marble_pcm_data.bad || !marble_pcm_memory_ready() ||
       marble_tdm_run.sent!=7 || marble_tdm_run.tx!=7 || marble_tdm_run.reply!=7)return 1;
    if(marble_pcm_data.sent && marble_pcm_repeat.completed!=marble_pcm_submitted)return 1;
    unsigned index=marble_pcm_submitted;
    if(index>=MARBLE_PCM_REPEAT_COUNT)return 0;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];unsigned used=0;
        for(unsigned j=0;j<5;j++)if((marble_tdm_run.sent&(1u<<j)) && intent->id==marble_tdm_run.id[j])used=1;
        for(unsigned j=0;j<2;j++)if((marble_pcm_memory.sent&(1u<<j)) && intent->id==marble_pcm_memory.id[j])used=1;
        if(used || intent->bytes<68 || intent->bytes>=528 || (index && intent->id!=marble_pcm_data.id))continue;
        unsigned zero=index?MARBLE_PCM_REPEAT_RECHECK():MARBLE_PCM_DATA_PREPARE();
        if(!zero) {marble_pcm_data.bad++;return 1;}
        uint8_t payload[76]={0};unsigned n=marble_pcm_data_command_index(payload+8,68,marble_pcm_memory.handle,index);
        marble_gpr_put32(payload,n);
        if(!n || !marble_glink_control_send_message(9,1,intent->id,payload,n+8)) {marble_pcm_data.bad++;return 0;}
        /* A failed send may have exposed DMA. On success record ownership
         * before any progress callback, with no USB handshake in this profile. */
        marble_pcm_data.sent=1;marble_pcm_data.tx=marble_pcm_data.reply=marble_pcm_data.faults=0;
        marble_pcm_data.zero=zero;marble_pcm_data.id=intent->id;marble_pcm_repeat.reuse=0;
        memset(&marble_pcm_data.result,0,sizeof(marble_pcm_data.result));
        marble_pcm_submitted++;marble_pcm_data_remote_used=1;marble_pcm_data_remote_id=intent->id;
        print("pd: q t=%x n=68 id=%x cap=%d handle=%x bytes=%d frames=%d rate=%d bits=%d channels=2 zero=1\n",
            (uint64_t)(MARBLE_PCM_DATA_TOKEN+index),(uint64_t)intent->id,(uint64_t)intent->bytes,(uint64_t)marble_pcm_memory.handle,
            (uint64_t)MARBLE_PCM_DATA_BYTES,(uint64_t)MARBLE_PCM_DATA_FRAMES,(uint64_t)MARBLE_PCM_DATA_RATE,(uint64_t)MARBLE_PCM_DATA_BITS);
        return 1;
    }
    return 1;
}
static int marble_pcm_data_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
    if(!marble_pcm_data.sent || m->channel!=marble_glink_control_state.app_remote)return 0;
    if(m->command==6 || m->command==14) {
        if(m->parameter!=marble_pcm_data.id)return 0;
        if(marble_pcm_data.tx)marble_pcm_data.bad++;
        marble_pcm_data.tx=1;marble_pcm_repeat.reuse=m->command==14;
        print("pd: d c=%d id=%x\n",(uint64_t)m->command,(uint64_t)m->parameter);return 1;
    }
    if(marble_pcm_data.reply)return 0;
    if(!marble_glink_fragment(&marble_pcm_data.rx,m->command,raw,m->bytes,m->channel))return -1;
    print("pd: f c=%d n=%d l=%d u=%d f=%d\n",(uint64_t)m->command,(uint64_t)marble_glink_le32(raw+8),
        (uint64_t)marble_pcm_data.rx.left,(uint64_t)marble_pcm_data.rx.used,(uint64_t)m->bytes);
    if(marble_pcm_data.rx.active)return 1;
    if(!marble_pcm_data_reply_index(marble_pcm_data.rx.data,marble_pcm_data.rx.used,marble_pcm_memory.handle,
        marble_pcm_submitted-1,&marble_pcm_data.result))marble_pcm_data.bad++;
    else {
        marble_pcm_data.reply=1;
        print("pd: r t=%x error=%x md=%x kind=%d handle=%x n=%d\n",(uint64_t)(MARBLE_PCM_DATA_TOKEN+marble_pcm_submitted-1),
            (uint64_t)marble_pcm_data.result.error,(uint64_t)marble_pcm_data.result.md_error,(uint64_t)marble_pcm_data.result.kind,
            (uint64_t)marble_pcm_memory.handle,(uint64_t)marble_pcm_data.rx.used);
    }
    if(!marble_glink_control_send(14,1,1))return -1;
    memset(&marble_pcm_data.rx,0,sizeof(marble_pcm_data.rx));return 1;
}
