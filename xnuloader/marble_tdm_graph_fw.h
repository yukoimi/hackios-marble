/* Isolated OPEN/CLOSE pair. Poll transport stays alive on rejected replies
 * so fresh STOP ACK and PAS shutdown can still retire the DSP session. */
static struct {
    unsigned id[2],tx_mask,open_reply,close_reply,rejected;
    uint32_t open_error,close_error;
} marble_tdm_graph;
static int marble_gpr_progress(void)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_glink_control_state.app_ack)return 1;
    if(!marble_gpr.advertised) {
        uint8_t intent[8]={0};marble_gpr_put32(intent,512);marble_gpr_put32(intent+4,1);
        if(!marble_glink_control_send_message(5,1,1,intent,8))return 0;
        marble_gpr.advertised=1;
        print("marble-tdm-graph: local_intent channel=1 id=1 bytes=512 static=1\n");
    }
    if(marble_tdm_graph.rejected || marble_gpr.sent>=2)return 1;
    unsigned close=marble_gpr.sent==1,bytes=close?64:MARBLE_TDM_GRAPH_OPEN_BYTES;
    if(close && (!marble_tdm_graph.open_reply || !(marble_tdm_graph.tx_mask&1)))return 1;
    unsigned candidates=0;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++)
        if(marble_glink_control_state.app_intent[i].bytes>=64)candidates++;
    if(candidates<2)return 1;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];
        if(intent->bytes<bytes || (close && intent->id==marble_tdm_graph.id[0]))continue;
        uint8_t payload[MARBLE_TDM_GRAPH_OPEN_BYTES+8]={0};marble_gpr_put32(payload,bytes);
        if(MARBLE_TDM_GRAPH_ENCODER(payload+8,MARBLE_TDM_GRAPH_OPEN_BYTES,close)!=bytes ||
           !marble_glink_control_send_message(9,1,intent->id,payload,bytes+8))return 0;
        marble_tdm_graph.id[close]=intent->id;marble_gpr.sent++;
        print("marble-tdm-graph: query stage=%d token=%x opcode=%x bytes=%d remote_id=%x remote_bytes=%d\n",
            (uint64_t)close,(uint64_t)(close?MARBLE_TDM_GRAPH_CLOSE_TOKEN:MARBLE_TDM_GRAPH_OPEN_TOKEN),
            (uint64_t)(close?0x01001004:0x01001000),(uint64_t)bytes,(uint64_t)intent->id,(uint64_t)intent->bytes);return 1;
    }
    return 1;
}
static int marble_gpr_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_gpr.sent || m->channel!=marble_glink_control_state.app_remote)return 0;
    if(m->command==6 || m->command==14) {
        unsigned stage=marble_gpr.sent>1 && m->parameter==marble_tdm_graph.id[1];
        if(m->parameter!=marble_tdm_graph.id[stage] || (marble_tdm_graph.tx_mask&(1u<<stage)))return 0;
        marble_tdm_graph.tx_mask|=1u<<stage;marble_gpr.done++;
        print("marble-tdm-graph: tx_done stage=%d command=%d remote_channel=%d remote_id=%x\n",
            (uint64_t)stage,(uint64_t)m->command,(uint64_t)m->channel,(uint64_t)m->parameter);return 1;
    }
    if(!marble_gpr.advertised || marble_gpr.replies>=2 ||
       !marble_glink_fragment(&marble_gpr.rx,m->command,raw,m->bytes,m->channel))return 0;
    print("marble-tdm-graph: fragment command=%d channel=%d intent=1 chunk=%d left=%d accumulated=%d frame_bytes=%d\n",
        (uint64_t)m->command,(uint64_t)m->channel,(uint64_t)marble_glink_le32(raw+8),
        (uint64_t)marble_gpr.rx.left,(uint64_t)marble_gpr.rx.used,(uint64_t)m->bytes);
    if(marble_gpr.rx.active)return 1;
    unsigned stage=marble_tdm_graph.open_reply;MarbleTdmGraphReply reply;marble_gpr.replies++;
    if(marble_tdm_graph.rejected || stage>=marble_gpr.sent ||
       !marble_tdm_graph_reply(marble_gpr.rx.data,marble_gpr.rx.used,stage,&reply)) {
        marble_tdm_graph.rejected++;
        print("marble-tdm-graph: rejected bytes=%d acceptance=0\n",(uint64_t)marble_gpr.rx.used);
    } else {
        if(!stage) {marble_tdm_graph.open_reply=1;marble_tdm_graph.open_error=reply.error;}
        else {marble_tdm_graph.close_reply=1;marble_tdm_graph.close_error=reply.error;}
        print("marble-tdm-graph: reply stage=%d token=%x error=%x client_data=%x bytes=32 remote_channel=%d local_intent=1\n",
            (uint64_t)stage,(uint64_t)(stage?MARBLE_TDM_GRAPH_CLOSE_TOKEN:MARBLE_TDM_GRAPH_OPEN_TOKEN),
            (uint64_t)reply.error,(uint64_t)reply.client_data,(uint64_t)m->channel);
    }
    if(!marble_glink_control_send(14,1,1))return 0;
    memset(&marble_gpr.rx,0,sizeof(marble_gpr.rx));return 1;
}
