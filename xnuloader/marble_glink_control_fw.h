/* Owned polling of lpass control traffic. No global native ISR or allocator.
 * OPEN requests are recorded and left unanswered, as in HakiOS for services it
 * does not speak. The separate apps profile acknowledges only adsp_apps and
 * records its remote intents. The separate GPR-only profile sends one bounded
 * read-only SPF query on one fixed local intent; no audio commands. */
#ifndef MARBLE_GLINK_CONTROL_FW_H
#define MARBLE_GLINK_CONTROL_FW_H
#ifdef XNU_MARBLE
#include "marble_glink_wire.h"
#ifdef XNU_MARBLE_GPR_ONLY
#include "marble_glink_data.h"
#include "marble_gpr.h"
#ifdef XNU_MARBLE_TDM_RUN_ONLY
#include "marble_tdm_run.h"
#elif defined(XNU_MARBLE_AUDIO_CLOCK_ONLY)
#include "marble_audio_clock.h"
#elif defined(XNU_MARBLE_TDM_GRAPH_ONLY)
#ifdef XNU_MARBLE_TDM_CONFIG_ONLY
#include "marble_tdm_config.h"
#define MARBLE_TDM_GRAPH_OPEN_BYTES 400u
#define MARBLE_TDM_GRAPH_ENCODER marble_tdm_config_command
#else
#include "marble_tdm_graph.h"
#define MARBLE_TDM_GRAPH_OPEN_BYTES 296u
#define MARBLE_TDM_GRAPH_ENCODER marble_tdm_graph_command
#endif
#elif defined(XNU_MARBLE_AMDB_LOAD_ONLY)
#include "marble_amdb_cycle.h"
#define MARBLE_GPR_QUERY_BYTES 64u
#elif defined(XNU_MARBLE_AMDB_ONLY)
#include "marble_amdb.h"
#define MARBLE_GPR_TOKEN 0x4d414d01u
#define MARBLE_GPR_QUERY_BYTES 96u
#define MARBLE_GPR_QUERY_LOG "marble-amdb:"
#else
#define MARBLE_GPR_TOKEN 0x4d535001u
#define MARBLE_GPR_QUERY_BYTES 24u
#define MARBLE_GPR_QUERY_LOG "marble-gpr:"
#endif
static struct {
    unsigned advertised,sent,done,replies,ready,basic,remote_id,rejected;
    MarbleGlinkAssembly rx;
} marble_gpr;
#endif
static struct {
    unsigned active,failed,fifo_seen,initial_version,version_seen,version_ack,features;
    unsigned rx_messages,tx_messages,opens,kicks,glink_clears,smp2p_clears,stopped;
    unsigned app_mode,app_requested,app_ack,app_remote,app_intents;
    MarbleGlinkRemoteIntent app_intent[512];
    uint64_t remote_fifo;uint32_t head,tail;
    struct {uint16_t id;char name[33];} deferred[32];
} marble_glink_control_state;
static int marble_glink_control_native_same(void)
{
    uint64_t now[46];
    return marble_glink_smem.qualified && marble_rpmh_words(marble_npa_ram_word,marble_glink_smem.transport,now,46) &&
        !memcmp(now,marble_glink_smem.native,sizeof(now));
}
static int marble_glink_control_begin(void)
{
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    return marble_audio_native_begin();
#else
    if(marble_glink_control_state.active || marble_glink_control_state.failed || marble_pil_auth_attempted ||
       !marble_qmp_hold.on_acked || !marble_glink_control_native_same())return 0;
    marble_glink_control_state.features=1;marble_glink_control_state.active=1;
    print("marble-glink-control: begin qualified_SMEM=1 local_head=0 local_tail=0 native_dispatch=0 allocator_calls=0 local_channels=0 DSP_start=0\n");
    return 1;
#endif
}
static void marble_glink_control_kick(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)(0xed18000+0xc)=0x30000;
    __asm__ volatile("dsb sy" ::: "memory");marble_glink_control_state.kicks++;
}
static int marble_glink_control_send_message(unsigned command,unsigned p1,unsigned p2,const uint8_t *payload,unsigned bytes)
{
    unsigned allowed=command<=1 || command==11 ||
        (marble_glink_control_state.app_mode && (command==2 || command==4));
#ifdef XNU_MARBLE_GPR_ONLY
    allowed|=marble_glink_control_state.app_mode==2 &&
        ((command==5 && p1==1 && p2==1 && bytes==8) ||
         (command==9 && p1==1 &&
#ifdef XNU_MARBLE_TDM_RUN_ONLY
          (bytes==MARBLE_TDM_RUN_OPEN_BYTES+8 || bytes==72
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
           || bytes==52 || bytes==36
#ifdef XNU_MARBLE_PCM_DATA_ONLY
           || bytes==76
#endif
#endif
          )
#elif defined(XNU_MARBLE_AUDIO_CLOCK_ONLY)
          (bytes==84 || bytes==72)
#elif defined(XNU_MARBLE_TDM_GRAPH_ONLY)
          (bytes==MARBLE_TDM_GRAPH_OPEN_BYTES+8 || bytes==72)
#else
          bytes==MARBLE_GPR_QUERY_BYTES+8
#endif
         ) || (command==14 && p1==1 && p2==1 && !bytes));
#endif
    if(!allowed || (bytes && !payload) || (command==2 && bytes!=10) ||
       (command!=2 && command!=5 && command!=9 && bytes))return 0;
    volatile uint32_t *descriptor=(volatile uint32_t *)(uintptr_t)marble_glink_smem.descriptor;
    uint32_t available=0,head=descriptor[1],tail=descriptor[0],total=8+((bytes+7)&~7u);
    if(head!=marble_glink_control_state.head || !marble_glink_ring(16384,head,tail,&available) ||
       16384-available<total+16 || marble_glink_control_state.tx_messages>=
#if defined(XNU_MARBLE_AUDIO_PCM_REPEAT) && XNU_MARBLE_AUDIO_PCM_PERIODS == 8
       44 /* original 32 + DATA/receive-ACK for six extra periods */
#else
       32
#endif
       )return 0;
#ifdef XNU_MARBLE_TDM_RUN_ONLY
    uint8_t frame[MARBLE_TDM_RUN_OPEN_BYTES+16]={0};
#elif defined(XNU_MARBLE_AUDIO_CLOCK_ONLY)
    uint8_t frame[96]={0};
#elif defined(XNU_MARBLE_TDM_GRAPH_ONLY)
    uint8_t frame[MARBLE_TDM_GRAPH_OPEN_BYTES+16]={0};
#elif defined(XNU_MARBLE_AMDB_ONLY) || defined(XNU_MARBLE_AMDB_LOAD_ONLY)
    uint8_t frame[112]={0};
#else
    uint8_t frame[40]={0};
#endif
    uint32_t first=command|(p1<<16);
    if(total>sizeof(frame))return 0;
    for(unsigned i=0;i<4;i++) {frame[i]=(uint8_t)(first>>(8*i));frame[4+i]=(uint8_t)(p2>>(8*i));}
    for(unsigned i=0;i<bytes;i++)frame[8+i]=payload[i];
    for(unsigned i=0;i<total;i+=4) *(volatile uint32_t *)(uintptr_t)(marble_glink_smem.local_fifo+((head+i)&16383))=marble_glink_le32(frame+i);
    __asm__ volatile("dmb sy" ::: "memory");
    head=(head+total)&16383;descriptor[1]=head;marble_glink_control_state.head=head;
    marble_glink_control_state.tx_messages++;marble_glink_control_kick();
    print("marble-glink-control: tx command=%d param1=%d param2=%x local_head=%d bytes=%d\n",(uint64_t)command,(uint64_t)p1,(uint64_t)p2,(uint64_t)head,(uint64_t)total);
    return 1;
}
static int marble_glink_control_send(unsigned command,unsigned p1,unsigned p2)
{return marble_glink_control_send_message(command,p1,p2,NULL,0);}
#ifdef XNU_MARBLE_GPR_ONLY
#ifdef XNU_MARBLE_TDM_RUN_ONLY
#include "marble_tdm_run_fw.h"
#elif defined(XNU_MARBLE_AUDIO_CLOCK_ONLY)
#include "marble_audio_clock_fw.h"
#elif defined(XNU_MARBLE_TDM_GRAPH_ONLY)
#include "marble_tdm_graph_fw.h"
#elif defined(XNU_MARBLE_AMDB_LOAD_ONLY)
#include "marble_amdb_cycle_fw.h"
#else
static int marble_gpr_progress(void)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_glink_control_state.app_ack)return 1;
    if(!marble_gpr.advertised) {
        uint8_t intent[8]={0};marble_gpr_put32(intent,512);marble_gpr_put32(intent+4,1);
        if(!marble_glink_control_send_message(5,1,1,intent,8))return 0;
        marble_gpr.advertised=1;
        print(MARBLE_GPR_QUERY_LOG " local_intent channel=1 id=1 bytes=512 static=1\n");
    }
    if(marble_gpr.sent)return 1;
    for(unsigned i=0;i<marble_glink_control_state.app_intents;i++) {
        MarbleGlinkRemoteIntent *intent=&marble_glink_control_state.app_intent[i];
        if(intent->bytes<MARBLE_GPR_QUERY_BYTES)continue;
        uint8_t payload[MARBLE_GPR_QUERY_BYTES+8]={0};marble_gpr_put32(payload,MARBLE_GPR_QUERY_BYTES);
#ifdef XNU_MARBLE_AMDB_ONLY
        if(marble_amdb_command(payload+8,MARBLE_GPR_QUERY_BYTES,MARBLE_GPR_TOKEN)!=MARBLE_GPR_QUERY_BYTES)return 0;
#else
        if(marble_gpr_state_command(payload+8,MARBLE_GPR_QUERY_BYTES,MARBLE_GPR_TOKEN)!=MARBLE_GPR_QUERY_BYTES)return 0;
#endif
        if(!marble_glink_control_send_message(9,1,intent->id,payload,sizeof(payload)))return 0;
        marble_gpr.sent=1;marble_gpr.remote_id=intent->id;
#ifdef XNU_MARBLE_AMDB_ONLY
#ifdef XNU_MARBLE_AMDB_MFC
        print("marble-amdb: query token=%x opcode=1001007 bytes=96 remote_id=%x remote_bytes=%d local_intents=1 module=7001015 param=80013d6 graph_commands=0 audio_commands=0\n",
#else
        print("marble-amdb: query token=%x opcode=1001007 bytes=96 remote_id=%x remote_bytes=%d local_intents=1 module=700100e param=80013d6 graph_commands=0 audio_commands=0\n",
#endif
#else
        print("marble-gpr: query token=%x opcode=1001021 bytes=24 remote_id=%x remote_bytes=%d local_intents=1 audio_commands=0\n",
#endif
            (uint64_t)MARBLE_GPR_TOKEN,(uint64_t)intent->id,(uint64_t)intent->bytes);return 1;
    }
    return 1;
}
static int marble_gpr_receive(const MarbleGlinkControl *m,const uint8_t *raw)
{
    if(marble_glink_control_state.app_mode!=2 || !marble_gpr.sent ||
       m->channel!=marble_glink_control_state.app_remote)return 0;
    if(m->command==6 || m->command==14) {
        if(marble_gpr.done || m->parameter!=marble_gpr.remote_id)return 0;
        marble_gpr.done=1;
        print(MARBLE_GPR_QUERY_LOG " tx_done command=%d remote_channel=%d remote_id=%x\n",
            (uint64_t)m->command,(uint64_t)m->channel,(uint64_t)m->parameter);return 1;
    }
    if(!marble_gpr.advertised || marble_gpr.replies>=2 ||
       !marble_glink_fragment(&marble_gpr.rx,m->command,raw,m->bytes,m->channel))return 0;
    print(MARBLE_GPR_QUERY_LOG " fragment command=%d channel=%d intent=1 chunk=%d left=%d accumulated=%d frame_bytes=%d\n",
        (uint64_t)m->command,(uint64_t)m->channel,(uint64_t)marble_glink_le32(raw+8),
        (uint64_t)marble_gpr.rx.left,(uint64_t)marble_gpr.rx.used,(uint64_t)m->bytes);
    if(marble_gpr.rx.active)return 1;
#ifdef XNU_MARBLE_AMDB_ONLY
    MarbleAmdbReply reply;
    /* A complete, owned GLINK packet with an unsupported GPR header must
     * still retire its intent. Preserve the control service for fresh STOP
     * ACK and PAS shutdown; metadata acceptance remains explicitly false. */
    if(!marble_amdb_reply(marble_gpr.rx.data,marble_gpr.rx.used,MARBLE_GPR_TOKEN,&reply) ||
       (reply.kind==1 && marble_gpr.ready) || (reply.kind==2 && marble_gpr.basic)) {
        if(marble_gpr.rejected)return 0;
        marble_gpr.rejected=1;marble_gpr.replies++;
        print("marble-amdb: rejected bytes=%d words=",(uint64_t)marble_gpr.rx.used);
        for(unsigned j=0;j<marble_gpr.rx.used && j<32;j+=4) {
            if(j+4<=marble_gpr.rx.used)print("%x,",(uint64_t)marble_gpr_u32(marble_gpr.rx.data+j));
        }
        print(" metadata_acceptance=0 intent_retired=1\n");
        if(!marble_glink_control_send(14,1,1))return 0;
        memset(&marble_gpr.rx,0,sizeof(marble_gpr.rx));return 1;
    }
    marble_gpr.replies++;if(reply.kind==1)marble_gpr.ready=reply.present?2:1;else marble_gpr.basic=1;
    print("marble-amdb: reply token=%x kind=%d error=%x param_error=%x module_error=%x present=%d major=%d minor=%d client_data=%x build=%d,%d,%d,%d,%d,%d bytes=%d remote_channel=%d local_intent=1\n",
        (uint64_t)MARBLE_GPR_TOKEN,(uint64_t)reply.kind,(uint64_t)reply.error,(uint64_t)reply.param_error,(uint64_t)reply.module_error,
        (uint64_t)reply.present,(uint64_t)reply.major,(uint64_t)reply.minor,(uint64_t)reply.client_data,(uint64_t)reply.year,(uint64_t)reply.month,(uint64_t)reply.day,(uint64_t)reply.hour,(uint64_t)reply.minute,(uint64_t)reply.second,
        (uint64_t)marble_gpr.rx.used,(uint64_t)m->channel);
#else
    MarbleGprStateReply reply;
    if(!marble_gpr_state_reply(marble_gpr.rx.data,marble_gpr.rx.used,MARBLE_GPR_TOKEN,&reply) ||
       (reply.kind==1 && marble_gpr.ready) || (reply.kind==2 && marble_gpr.basic))return 0;
    marble_gpr.replies++;if(reply.kind==1)marble_gpr.ready=reply.state?2:1;else marble_gpr.basic=1;
    print("marble-gpr: reply token=%x kind=%d state=%d error=%x bytes=%d remote_channel=%d local_intent=1\n",
        (uint64_t)MARBLE_GPR_TOKEN,(uint64_t)reply.kind,(uint64_t)reply.state,(uint64_t)reply.error,
        (uint64_t)marble_gpr.rx.used,(uint64_t)m->channel);
#endif
    if(!marble_glink_control_send(14,1,1))return 0;
    memset(&marble_gpr.rx,0,sizeof(marble_gpr.rx));return 1;
}
#endif
#endif /* query implementation versus library cycle */
static int marble_glink_control_fifo(void)
{
    if(marble_glink_control_state.fifo_seen)return 1;
    MarbleSmemGetParams q={2,480,0,0,NULL,0x40000000,0};
    efi_status status=marble_glink_smem.provider->get_ex(&q);uint64_t p=(uint64_t)(uintptr_t)q.address;
    if(status==(EFI_ERROR_BIT|14) && !p && !q.size)return 0;
    uint64_t local=marble_glink_smem.local_fifo,desc=marble_glink_smem.descriptor,own=(uint64_t)(uintptr_t)marble_smp2p_owned;
    if(EFI_ERROR(status) || q.size!=16384 || q.flags!=0x40000000 || (p&7) || p<marble_glink_smem.partition+32 ||
       p>marble_glink_smem.partition+marble_glink_smem.partition_bytes-16384 ||
       !(p+16384<=local || local+16384<=p) || !(p+16384<=desc || desc+32<=p) ||
       !own || !(p+16384<=own || own+344<=p))return -1;
    for(uint64_t at=p&~4095ULL;at<p+16384;at+=4096) {
        uint64_t par=marble_fw_translation(at);
        if((par&1) || (par&0x0000fffffffff000ULL)!=at || (par>>56)!=0x44)return -1;
    }
    MarbleSmemGetParams smp={2,443,0,0,NULL,0x40000000,0};
    status=marble_glink_smem.provider->get_ex(&smp);uint64_t other=(uint64_t)(uintptr_t)smp.address;
    if(status==(EFI_ERROR_BIT|14)) {if(other || smp.size)return -1;}
    else if(EFI_ERROR(status) || smp.size!=344 || smp.flags!=0x40000000 || (other&7) ||
       other<marble_glink_smem.partition+32 || other>marble_glink_smem.partition+marble_glink_smem.partition_bytes-344 ||
       !(p+16384<=other || other+344<=p))return -1;
    marble_glink_control_state.remote_fifo=p;marble_glink_control_state.fifo_seen=1;
    print("marble-glink-control: remote item=480 address=%x bytes=16384 NC_pages=1 nonoverlap=1 allocator_calls=0\n",p);
    return 1;
}
static int marble_glink_control_dispatch(const MarbleGlinkControl *m,const uint8_t *raw)
{
    unsigned slot=32;
    for(unsigned i=0;i<32;i++)if(marble_glink_control_state.deferred[i].id==m->channel) {slot=i;break;}
    switch(m->command) {
    case 0:
        marble_glink_control_state.features&=m->parameter&1u;marble_glink_control_state.version_seen=1;
        return marble_glink_control_send(1,1,marble_glink_control_state.features);
    case 1:
        if(m->parameter==marble_glink_control_state.features) {marble_glink_control_state.version_ack=1;return 1;}
        marble_glink_control_state.features&=m->parameter&1u;
        return marble_glink_control_send(0,1,marble_glink_control_state.features);
    case 2:
        if(!marble_glink_control_state.version_seen && !marble_glink_control_state.version_ack)return 0;
        if(slot<32)return str_eq(marble_glink_control_state.deferred[slot].name,m->name);
        for(unsigned i=0;i<32;i++)if(!marble_glink_control_state.deferred[i].id) {slot=i;break;}
        if(slot==32)return 0;
        marble_glink_control_state.deferred[slot].id=m->channel;
        memcpy(marble_glink_control_state.deferred[slot].name,m->name,sizeof(m->name));marble_glink_control_state.opens++;
        unsigned accept=marble_glink_control_state.app_mode && str_eq(m->name,"adsp_apps");
        if(accept) {
            if(marble_glink_control_state.app_requested || !marble_glink_control_send(4,m->channel,0))return 0;
            marble_glink_control_state.app_remote=m->channel;
            if(!marble_glink_control_send_message(2,1,10,(const uint8_t *)"adsp_apps",10))return 0;
            marble_glink_control_state.app_requested=1;
        }
        print("marble-glink-control: remote_open channel=%d name=%s deferred=%d acknowledged=%d local_channels=%d\n",
            (uint64_t)m->channel,m->name,(uint64_t)!accept,(uint64_t)accept,(uint64_t)marble_glink_control_state.app_requested);
        return 1;
    case 4:
        if(!marble_glink_control_state.app_mode || !marble_glink_control_state.app_requested || m->channel!=1 || marble_glink_control_state.app_ack)return 0;
        marble_glink_control_state.app_ack=1;
        print("marble-glink-apps: open_ack local_channel=1 remote_channel=%d local_intents=0 GPR_packets=0\n",(uint64_t)marble_glink_control_state.app_remote);
        return 1;
    case 5:
        if(!marble_glink_control_state.app_mode || !marble_glink_control_state.app_requested || m->channel!=marble_glink_control_state.app_remote)return 0;
        for(unsigned i=0;i<m->parameter;i++) {
            uint32_t bytes=marble_glink_le32(raw+8+8*i),id=marble_glink_le32(raw+12+8*i);unsigned at=0,added=0;
            if(!marble_glink_intent_accept(marble_glink_control_state.app_intent,512,&marble_glink_control_state.app_intents,id,bytes,&at,&added)) {
                print("marble-glink-control: refused step=intent_table count=%d capacity=512 id=%x bytes=%d\n",
                    (uint64_t)marble_glink_control_state.app_intents,(uint64_t)id,(uint64_t)bytes);return 0;
            }
            if(!added)continue;
            unsigned local=0;
#ifdef XNU_MARBLE_GPR_ONLY
            local=marble_gpr.advertised;
#endif
            print("marble-glink-apps: remote_intent channel=%d index=%d id=%x bytes=%d local_intents=%d payload_reads=0\n",
                (uint64_t)m->channel,(uint64_t)at,(uint64_t)id,(uint64_t)bytes,(uint64_t)local);
        }
        return 1;
    case 3:
        if(slot==32 || !marble_glink_control_send(11,m->channel,0))return 0;
        marble_glink_control_state.deferred[slot].id=0;return 1;
    case 13:marble_glink_control_kick();return 1;
    case 15:return slot<32;
#ifdef XNU_MARBLE_GPR_ONLY
    case 6:case 9:case 12:case 14:return marble_gpr_receive(m,raw);
#endif
    default:return 0;
    }
}
/* -1: refused; 0: a valid but incomplete FIFO; +1: queue drained. */
static int marble_glink_control_pump(void)
{
    int fifo=marble_glink_control_fifo();if(fifo<=0)return fifo;
    if(!marble_glink_control_state.initial_version) {
        if(!marble_glink_control_send(0,1,1))return -1;
        marble_glink_control_state.initial_version=1;
    }
    volatile uint32_t *d=(volatile uint32_t *)(uintptr_t)marble_glink_smem.descriptor;
    for(unsigned n=0;n<128;n++) {
        uint32_t head=d[3],tail=d[2],available=0;uint8_t snapshot[528];MarbleGlinkControl message;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        static unsigned amp_trace_pumps;
        unsigned amp_trace=marble_pcm_data.sent && amp_trace_pumps++<6;
        if(amp_trace && marble_pil_progress)marble_pil_progress("audio-amp-ring",((uint64_t)head<<32)|tail);
#endif
        __asm__ volatile("dmb sy" ::: "memory");
        if(tail!=marble_glink_control_state.tail || !marble_glink_ring(16384,head,tail,&available))return -1;
        if(!available)return 1;
        if(available<8 || !marble_glink_copy((const uint8_t *)(uintptr_t)marble_glink_control_state.remote_fifo,16384,head,tail,0,snapshot,8))return -1;
        unsigned command=(unsigned)snapshot[0]|(unsigned)snapshot[1]<<8,parameter=marble_glink_le32(snapshot+4),cap=8;
        if(command==2 && parameter>=2 && parameter<=32)cap=(8+parameter+7)&~7u;
        if(command==5 && marble_glink_control_state.app_mode && parameter && parameter<=64)cap=8+8*parameter;
#ifdef XNU_MARBLE_GPR_ONLY
        if(command==9 || command==12) {
            if(marble_glink_control_state.app_mode!=2 || !marble_gpr.advertised ||
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
               (!marble_gpr.sent && !marble_pcm_memory.sent) ||
#else
               !marble_gpr.sent ||
#endif
               (unsigned)(snapshot[2]|snapshot[3]<<8)!=marble_glink_control_state.app_remote || parameter!=1)return -1;
            if(available<16)return 0;
            if(!marble_glink_copy((const uint8_t *)(uintptr_t)marble_glink_control_state.remote_fifo,16384,head,tail,8,snapshot+8,8))return -1;
            unsigned chunk=marble_glink_le32(snapshot+8),left=marble_glink_le32(snapshot+12);
            if(!chunk || chunk>512 || left>512-chunk)return -1;
            cap=(16+chunk+7)&~7u;
        }
#endif
        unsigned bytes=available<cap?available:cap;
        if(bytes>8 && !marble_glink_copy((const uint8_t *)(uintptr_t)marble_glink_control_state.remote_fifo,16384,head,tail,8,snapshot+8,bytes-8))return -1;
        int parsed=marble_glink_frame(snapshot,bytes,&message,marble_glink_control_state.app_mode);
        if(parsed<=0) {
            if(parsed<0)print("marble-glink-control: refused step=frame command=%x param2=%x available=%d\n",(uint64_t)(snapshot[0]|snapshot[1]<<8),(uint64_t)marble_glink_le32(snapshot+4),(uint64_t)available);
            return parsed;
        }
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        if(amp_trace && marble_pil_progress)marble_pil_progress("audio-amp-dispatch",((uint64_t)message.command<<32)|message.parameter);
#endif
        if(marble_glink_control_state.rx_messages>=(marble_glink_control_state.app_mode?2048u:256u) || !marble_glink_control_dispatch(&message,snapshot))return -1;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        if(amp_trace && marble_pil_progress)marble_pil_progress("audio-amp-dispatch-done",message.command);
#endif
        tail=(tail+message.bytes)&16383;
        __asm__ volatile("dmb sy" ::: "memory");d[2]=tail;marble_glink_control_state.tail=tail;
        __asm__ volatile("dsb sy" ::: "memory");marble_glink_control_state.rx_messages++;
        print("marble-glink-control: rx command=%d param1=%d param2=%x bytes=%d local_tail=%d\n",
            (uint64_t)message.command,(uint64_t)message.channel,(uint64_t)message.parameter,(uint64_t)message.bytes,(uint64_t)tail);
#ifdef XNU_MARBLE_GPR_ONLY
        if(!marble_gpr_progress())return -1;
#endif
    }
    return marble_glink_control_state.app_mode?0:-1;
}
static int marble_glink_control_service(void)
{
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    return marble_audio_native_service();
#else
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    static unsigned amp_trace_services;
    unsigned amp_trace=marble_pcm_data.sent && amp_trace_services++<4;
    if(amp_trace && marble_pil_progress)marble_pil_progress("audio-amp-service-enter",amp_trace_services);
#endif
    if(!marble_glink_control_state.active || marble_glink_control_state.failed || !marble_pil_auth_attempted ||
       !marble_glink_control_native_same())return 0;
    for(unsigned i=0;i<16;i++) {
        int drained=marble_glink_control_pump();if(drained<0)break;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        if(amp_trace && marble_pil_progress)marble_pil_progress("audio-amp-pump-return",drained);
#endif
        uint32_t pending=*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10);
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
        if(amp_trace && marble_pil_progress)marble_pil_progress("audio-amp-pending",pending);
#endif
        if(pending==0xffffffffu)return 1;
        if(pending==0x30000) {if(!drained)return 1;}
        else if(pending==0x30002) {
            MarbleSmp2pItem remote;uint32_t value=0;
            int present=marble_smp2p_remote_snapshot(&remote,&value);
            if(present<0)break;
            if(!present)return 1;
            if(!marble_smp2p_negotiate(&remote))break;
        } else {print("marble-glink-control: refused step=foreign_notification pending=%x foreign_clears=0\n",(uint64_t)pending);break;}
        if(*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)!=pending)break;
        __asm__ volatile("dsb sy" ::: "memory");
        *(volatile uint32_t *)(uintptr_t)(0xed18000+0x1c)=pending;
        __asm__ volatile("dsb sy" ::: "memory");
        if(pending==0x30000)marble_glink_control_state.glink_clears++;else marble_glink_control_state.smp2p_clears++;
    }
    marble_glink_control_state.failed=1;
    print("marble-glink-control: refused step=service native_dispatch=0 allocator_calls=0\n");return 0;
#endif
}
static int marble_glink_control_quiet(void)
{
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    return marble_audio_native_quiet();
#else
    if(!marble_smp2p_stop_qualified)return 0;
    volatile uint32_t *d=(volatile uint32_t *)(uintptr_t)marble_glink_smem.descriptor;
    for(unsigned i=0;i<16;i++) {
        if(!marble_glink_control_service())return 0;
        if(marble_glink_control_state.fifo_seen && *(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)==0xffffffffu &&
           d[2]==d[3] && d[2]==marble_glink_control_state.tail && d[1]==marble_glink_control_state.head)return 1;
    }
    return 0;
#endif
}
static void marble_glink_control_stopped(void)
{
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    marble_audio_native_stopped();
#else
    marble_glink_control_state.stopped=1;marble_glink_control_state.active=0;
    unsigned local_intents=0,gpr_packets=0;
#ifdef XNU_MARBLE_GPR_ONLY
    local_intents=marble_gpr.advertised;gpr_packets=marble_gpr.sent;
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
#ifdef XNU_MARBLE_TDM_PINS_ONLY
    marble_tdm_pins_finish(1);
#endif
#ifdef XNU_MARBLE_PCM_DATA_ONLY
    marble_pcm_data_log(1);
#ifdef XNU_MARBLE_AUDIO_PCM_REPEAT
    gpr_packets+=marble_pcm_submitted;
#else
    gpr_packets+=marble_pcm_data.sent;
#endif
#endif
    marble_pcm_memory_finish(1);
    gpr_packets+=(marble_pcm_memory.sent&1)+((marble_pcm_memory.sent>>1)&1);
#endif
#ifdef XNU_MARBLE_TDM_RUN_ONLY
    unsigned pins_ok=marble_gpr.sent && marble_audio_clock_guard(1);
#ifdef XNU_MARBLE_TDM_PCM_GRAPH_ONLY
#ifdef XNU_MARBLE_PCM_DATA_ONLY
#ifdef XNU_MARBLE_AUDIO_MFC
    print("tr: topology modules=3 links=2 rate=96000 bits=32 channels=2 buffers=1\n");
    print("mfc: path source_rate=48000 source_bits=16 output_rate=96000 output_bits=32 channels=2 module=7001015 instance=6003 mapped_bytes=4096 PCM_bytes=1024 conversion_verified=0 audio_acceptance=0\n");
#else
    print("tr: topology modules=2 links=1 rate=96000 bits=32 channels=2 buffers=1\n");
#endif
#else
    print("tr: topology modules=2 links=1 rate=96000 bits=32 channels=2 buffers=0\n");
#endif
#endif
    print("tr: end s=%d rx=%d d=%d m=%x tx=%x r=%x bad=%d p=%d f=%d n=%d e=%x,%x,%x,%x,%x\n",
        (uint64_t)marble_gpr.sent,(uint64_t)marble_gpr.replies,(uint64_t)marble_gpr.done,(uint64_t)marble_tdm_run.sent,
        (uint64_t)marble_tdm_run.tx,(uint64_t)marble_tdm_run.reply,(uint64_t)marble_tdm_run.rejected,(uint64_t)pins_ok,
        (uint64_t)marble_gpr.rx.active,(uint64_t)marble_gpr.rx.used,(uint64_t)marble_tdm_run.errors[0],
        (uint64_t)marble_tdm_run.errors[1],(uint64_t)marble_tdm_run.errors[2],(uint64_t)marble_tdm_run.errors[3],(uint64_t)marble_tdm_run.errors[4]);
#elif defined(XNU_MARBLE_AUDIO_CLOCK_ONLY)
    unsigned pins_ok=marble_gpr.sent && marble_audio_clock_guard(1);
    print("prm: complete sent=%d replies=%d tx_done=%d request_reply=%d release_reply=%d request_error=%x release_error=%x rejected=%d fragment_active=%d fragment_bytes=%d pins_ok=%d PAS_stopped=1 graph_commands=0 amp_commands=0 audio_acceptance=0\n",
        (uint64_t)marble_gpr.sent,(uint64_t)marble_gpr.replies,(uint64_t)marble_gpr.done,
        (uint64_t)marble_audio_clock.request_reply,(uint64_t)marble_audio_clock.release_reply,
        (uint64_t)marble_audio_clock.request_error,(uint64_t)marble_audio_clock.release_error,
        (uint64_t)marble_audio_clock.rejected,(uint64_t)marble_gpr.rx.active,(uint64_t)marble_gpr.rx.used,(uint64_t)pins_ok);
#elif defined(XNU_MARBLE_TDM_GRAPH_ONLY)
#ifdef XNU_MARBLE_TDM_CONFIG_ONLY
    print("marble-tdm-graph: configuration params=3\n");
#endif
    print("marble-tdm-graph: complete sent=%d replies=%d tx_done=%d open_reply=%d close_reply=%d open_error=%x close_error=%x rejected=%d fragment_active=%d fragment_bytes=%d PAS_stopped=1 graph=4001 module=700100e prepare_commands=0 start_commands=0 clock_commands=0 amp_commands=0 audio_acceptance=0\n",
        (uint64_t)marble_gpr.sent,(uint64_t)marble_gpr.replies,(uint64_t)marble_gpr.done,
        (uint64_t)marble_tdm_graph.open_reply,(uint64_t)marble_tdm_graph.close_reply,
        (uint64_t)marble_tdm_graph.open_error,(uint64_t)marble_tdm_graph.close_error,
        (uint64_t)marble_tdm_graph.rejected,(uint64_t)marble_gpr.rx.active,(uint64_t)marble_gpr.rx.used);
#elif defined(XNU_MARBLE_AMDB_LOAD_ONLY)
    print("marble-amdb-cycle: complete sent=%d replies=%d tx_done=%d load_reply=%d unload_reply=%d load_error=%x unload_error=%x handle=%x rejected=%d fragment_active=%d fragment_bytes=%d PAS_stopped=1 graph_commands=0 audio_acceptance=0\n",
        (uint64_t)marble_gpr.sent,(uint64_t)marble_gpr.replies,(uint64_t)marble_gpr.done,
        (uint64_t)marble_amdb_cycle.load_reply,(uint64_t)marble_amdb_cycle.unload_reply,
        (uint64_t)marble_amdb_cycle.load_error,(uint64_t)marble_amdb_cycle.unload_error,marble_amdb_cycle.handle,
        (uint64_t)marble_amdb_cycle.rejected,(uint64_t)marble_gpr.rx.active,(uint64_t)marble_gpr.rx.used);
#else
#ifdef XNU_MARBLE_AMDB_ONLY
    print("marble-amdb: complete sent=%d replies=%d module_state=%d tx_done=%d fragment_active=%d fragment_bytes=%d PAS_stopped=1 graph_commands=0 audio_acceptance=0\n",
#else
    print("marble-gpr: complete sent=%d replies=%d SPF_state=%d tx_done=%d fragment_active=%d fragment_bytes=%d PAS_stopped=1 audio_acceptance=0\n",
#endif
        (uint64_t)marble_gpr.sent,(uint64_t)marble_gpr.replies,(uint64_t)marble_gpr.ready,(uint64_t)marble_gpr.done,
        (uint64_t)marble_gpr.rx.active,(uint64_t)marble_gpr.rx.used);
#endif
#endif /* cycle completion */
    if(marble_glink_control_state.app_mode)print("marble-glink-apps: complete PASS=%d remote_channel=%d local_channel=1 requested=%d open_ACK=%d remote_intents=%d intent_capacity=512 local_intents=%d GPR_packets=%d PAS_stopped=1 retired_after_PAS=1 close_handshake=0 audio_acceptance=0\n",
        (uint64_t)(marble_glink_control_state.app_requested && marble_glink_control_state.app_ack && marble_glink_control_state.app_intents),
        (uint64_t)marble_glink_control_state.app_remote,(uint64_t)marble_glink_control_state.app_requested,
        (uint64_t)marble_glink_control_state.app_ack,(uint64_t)marble_glink_control_state.app_intents,(uint64_t)local_intents,(uint64_t)gpr_packets);
    print("marble-glink-control: complete PASS=%d FIFO_seen=%d version_seen=%d version_ACK=%d RX_messages=%d TX_messages=%d remote_opens=%d GLINK_clears=%d SMP2P_clears=%d native_unchanged=%d allocator_calls=0 native_dispatch=0 local_channels=0 GPR_packets=%d PAS_stopped=1 DSP_start=1 audio_acceptance=0\n",
        (uint64_t)(!marble_glink_control_state.failed && marble_glink_control_state.fifo_seen),
        (uint64_t)marble_glink_control_state.fifo_seen,(uint64_t)marble_glink_control_state.version_seen,(uint64_t)marble_glink_control_state.version_ack,
        (uint64_t)marble_glink_control_state.rx_messages,(uint64_t)marble_glink_control_state.tx_messages,(uint64_t)marble_glink_control_state.opens,
        (uint64_t)marble_glink_control_state.glink_clears,(uint64_t)marble_glink_control_state.smp2p_clears,(uint64_t)marble_glink_control_native_same(),(uint64_t)gpr_packets);
#endif /* native endpoint or legacy control */
}
#endif
#endif
