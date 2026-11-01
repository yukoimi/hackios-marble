/* Included inside the isolated GPR transport, after send_message. */
static struct {
    unsigned id[2],tx_mask,load_reply,unload_reply,rejected;
    uint32_t load_error,unload_error;
    uint64_t handle;
} marble_amdb_cycle;
static int marble_gpr_progress(void)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_glink_control_state.app_ack)return 1;
    if(!marble_gpr.advertised) {
        uint8_t intent[8]={0};marble_gpr_put32(intent,512);marble_gpr_put32(intent+4,1);
        if(!marble_glink_control_send_message(5,1,1,intent,8))return 0;
        marble_gpr.advertised=1;
        print("marble-amdb-cycle: local_intent channel=1 id=1 bytes=512 static=1\n");
    }
    if(marble_amdb_cycle.rejected || marble_gpr.sent>=2)return 1;
    unsigned unload=marble_gpr.sent==1;
    if(unload && (!marble_amdb_cycle.load_reply || !(marble_amdb_cycle.tx_mask&1) || !marble_amdb_cycle.handle))return 1;
    unsigned candidates=0;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++)
        if(marble_glink_control_state.app_intent[i].bytes>=64)candidates++;
    /* Reserve a distinct remote intent for the release before acquiring. */
    if(candidates<2)return 1;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];
        if(intent->bytes<64 || (unload && intent->id==marble_amdb_cycle.id[0]))continue;
        uint8_t payload[72]={0};marble_gpr_put32(payload,64);
        if(marble_amdb_cycle_command(payload+8,64,unload,unload?marble_amdb_cycle.handle:0)!=64 ||
           !marble_glink_control_send_message(9,1,intent->id,payload,72))return 0;
        marble_amdb_cycle.id[unload]=intent->id;marble_gpr.sent++;
        print("marble-amdb-cycle: query stage=%d token=%x opcode=%x bytes=64 remote_id=%x remote_bytes=%d module=%x handle=%x graph_commands=0 audio_commands=0\n",
            (uint64_t)unload,(uint64_t)(unload?MARBLE_AMDB_UNLOAD_TOKEN:MARBLE_AMDB_LOAD_TOKEN),
            (uint64_t)(unload?MARBLE_AMDB_UNLOAD:MARBLE_AMDB_LOAD),(uint64_t)intent->id,(uint64_t)intent->bytes,(uint64_t)MARBLE_AMDB_QUERY_MODULE,
            unload?marble_amdb_cycle.handle:0);return 1;
    }
    return 1;
}
static int marble_gpr_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_gpr.sent ||
       m->channel!=marble_glink_control_state.app_remote)return 0;
    if(m->command==6 || m->command==14) {
        unsigned stage=marble_gpr.sent>1 && m->parameter==marble_amdb_cycle.id[1];
        if(m->parameter!=marble_amdb_cycle.id[stage] || (marble_amdb_cycle.tx_mask&(1u<<stage)))return 0;
        marble_amdb_cycle.tx_mask|=1u<<stage;marble_gpr.done++;
        print("marble-amdb-cycle: tx_done stage=%d command=%d remote_channel=%d remote_id=%x\n",
            (uint64_t)stage,(uint64_t)m->command,(uint64_t)m->channel,(uint64_t)m->parameter);return 1;
    }
    if(!marble_gpr.advertised || marble_gpr.replies>=2 ||
       !marble_glink_fragment(&marble_gpr.rx,m->command,raw,m->bytes,m->channel))return 0;
    print("marble-amdb-cycle: fragment command=%d channel=%d intent=1 chunk=%d left=%d accumulated=%d frame_bytes=%d\n",
        (uint64_t)m->command,(uint64_t)m->channel,(uint64_t)marble_glink_le32(raw+8),
        (uint64_t)marble_gpr.rx.left,(uint64_t)marble_gpr.rx.used,(uint64_t)m->bytes);
    if(marble_gpr.rx.active)return 1;
    unsigned stage=marble_amdb_cycle.load_reply;MarbleAmdbCycleReply reply;
    marble_gpr.replies++;
    if(marble_amdb_cycle.rejected || stage>=marble_gpr.sent ||
       !marble_amdb_cycle_reply(marble_gpr.rx.data,marble_gpr.rx.used,stage,&reply)) {
        marble_amdb_cycle.rejected++;
        print("marble-amdb-cycle: rejected bytes=%d acceptance=0\n",(uint64_t)marble_gpr.rx.used);
    } else {
        if(!stage) {marble_amdb_cycle.load_reply=1;marble_amdb_cycle.load_error=reply.error;marble_amdb_cycle.handle=reply.handle;}
        else {marble_amdb_cycle.unload_reply=1;marble_amdb_cycle.unload_error=reply.error;}
        print("marble-amdb-cycle: reply stage=%d token=%x kind=%d error=%x client_data=%x handle=%x bytes=%d remote_channel=%d local_intent=1\n",
            (uint64_t)stage,(uint64_t)(stage?MARBLE_AMDB_UNLOAD_TOKEN:MARBLE_AMDB_LOAD_TOKEN),
            (uint64_t)reply.kind,(uint64_t)reply.error,(uint64_t)reply.client_data,reply.handle,
            (uint64_t)marble_gpr.rx.used,(uint64_t)m->channel);
    }
    if(!marble_glink_control_send(14,1,1))return 0;
    memset(&marble_gpr.rx,0,sizeof(marble_gpr.rx));return 1;
}
