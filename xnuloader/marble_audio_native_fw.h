/* Resource-provider bring-up for the SAME endpoint used by XNU. This
 * explicitly isolated firmware profile has one FIFO owner, two zero periods,
 * and no audible enable. It is not the cross-core XNU handoff or a HAL sink.
 * Never write legacy graph/control counters to manufacture authorization. */
#ifndef MARBLE_AUDIO_NATIVE_FW_H
#define MARBLE_AUDIO_NATIVE_FW_H
#if !defined(XNU_MARBLE_AUDIO_MFC) || !defined(XNU_MARBLE_AUDIO_DIGITAL_EARLY_STOP) || !defined(XNU_MARBLE_FIRMWARE_ONLY)
#error Native endpoint bring-up requires isolated muted MFC/early-stop profile
#endif
#include "../xnu/files/bsd/sys/marble_audio_endpoint.h"
static struct {
    MarbleAudioEndpoint endpoint;
    MarbleAudioQueue queue;
    unsigned attempted,active,failed,stopped,restored,published,configured;
    unsigned last_phase,submitted,completed,glink_clears,smp2p_clears;
    uint64_t epoch;
    unsigned proof_logged;
} marble_audio_native;
static uint64_t marble_audio_native_now(void)
{uint64_t t=READ_SYSREG(CNTVCT_EL0),f=READ_SYSREG(CNTFRQ_EL0);return t/f*1000000+t%f*1000000/f;}
static unsigned marble_audio_native_request_count(void)
{return marble_audio_native.published;}
static int marble_audio_native_read(void *cookie,uint32_t at,void *out,uint32_t bytes)
{
    (void)cookie;
    if(!out || at>MASMEM_BYTES || bytes>MASMEM_BYTES-at)return 0;
    for(uint64_t p=(0x80900000ULL+at)&~4095ULL;p<0x80900000ULL+at+bytes;p+=4096) {
        uint64_t par=marble_fw_translation(p);
        if((par&1) || (par&0x0000fffffffff000ULL)!=p)return 0;
    }
    const volatile uint8_t *src=(const volatile uint8_t *)(uintptr_t)(0x80900000ULL+at);
    for(unsigned i=0;i<bytes;i++)((uint8_t *)out)[i]=src[i];
    __asm__ volatile("dmb sy" ::: "memory");return 1;
}
static int marble_audio_native_write(void *cookie,uint32_t at,const void *in,uint32_t bytes)
{
    (void)cookie;MarbleAudioGlink *g=&marble_audio_native.endpoint.ring;
    unsigned allowed=g->live && ((bytes==4 && (at==g->memory.item[0]+4 || at==g->memory.item[0]+8)) ||
        (at>=g->memory.item[1] && bytes<=16384 && at-g->memory.item[1]<=16384-bytes));
    if(!allowed || !in || !bytes || ((at|bytes)&3) || !marble_audio_native.active || marble_audio_native.failed)return 0;
    volatile uint32_t *dst=(volatile uint32_t *)(uintptr_t)(0x80900000ULL+at);
    for(unsigned i=0;i<bytes/4;i++)dst[i]=marble_glink_le32((const uint8_t *)in+4*i);
    __asm__ volatile("dmb sy" ::: "memory");return 1;
}
static void marble_audio_native_barrier(void *cookie)
{(void)cookie;__asm__ volatile("dsb sy" ::: "memory");}
static int marble_audio_native_kick(void *cookie)
{
    (void)cookie;
    if(!marble_audio_native.active || marble_audio_native.failed || !marble_pil_auth_attempted)return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)(0xed18000+0xc)=0x30000;
    __asm__ volatile("dsb sy" ::: "memory");return 1;
}
static int marble_audio_native_start_qualified(void)
{
    const MarbleAudioStream *s=&marble_audio_native.endpoint.stream;
    MarbleAudioProof proof;
    return marble_audio_native.active && !marble_audio_native.failed && !marble_audio_native.endpoint.failed &&
        s->live && !s->failed && s->start_retired==1 && s->phase==MAS_DATA && s->pending &&
        s->submitted==1 && !s->completed && s->handle && s->cookie && marble_pcm_lease.active && marble_pcm_route_active &&
        marble_audio_stream_export_proof(s,MAPROOF_CONFIGURE,&proof) &&
        marble_audio_proof_valid(&proof,marble_audio_native.epoch,MAPROOF_CONFIGURE);
}
static int marble_audio_native_publish(void *cookie,const void *page,unsigned bytes)
{
    (void)cookie;const MarbleAudioStream *s=&marble_audio_native.endpoint.stream;
    if(bytes!=4096 || !marble_pcm_lease.active || !marble_pcm_route_active ||
       (uintptr_t)page!=marble_pcm_lease.result.physical || !s->start_retired || !s->pending ||
       s->phase!=MAS_DATA || s->submitted!=marble_audio_native.published+1 || s->completed!=marble_audio_native.published ||
       !marble_pcm_data_faults())return 0;
    /* This first hardware provider exercise NEVER accepts nonzero samples. */
    for(unsigned i=0;i<4096;i++)if(((const uint8_t *)page)[i])return 0;
    if(!marble_audio_native.configured) {
        if(!marble_audio_native_start_qualified() || !marble_tdm_pins_begin() || !marble_pcm_i2c_window(3))return 0;
        marble_audio_native.configured=1;
        print("aud-owner-proof: phase=configure epoch=%x flags=15 MAP_typed=1 START_typed=1 START_TX=1 submitted=1 completed=0 audible=0\n",marble_audio_native.epoch);
    }
    if(!marble_tdm_pins.active || !marble_i2c_window_passed || marble_i2c_amp_dirty || !marble_pcm_data_faults())return 0;
    MarbleAudioDmaSnapshot routes=marble_pcm_lease.snapshot;
    routes.smr[5]=0x80001801;routes.s2cr[5]=5;
    if(!marble_audio_map_routes(&marble_pcm_lease.ops.io,&routes))return 0;
    clean_dcache((uint64_t)(uintptr_t)page,(uint64_t)(uintptr_t)page+4096);
    marble_audio_native.published++;return 1;
}
static void marble_audio_native_log(const char *phase)
{
    const MarbleAudioEndpoint *e=&marble_audio_native.endpoint;
    print("aud-native: phase=%s active=%d failed=%d stream=%d pending=%d tx=%d reply=%d started=%d submitted=%d completed=%d published=%d configured=%d RX=%d TX=%d PAS_stopped=%d restored=%d owner=EFI zero=1 XNU_sink=0 audio_acceptance=0\n",
        phase,(uint64_t)marble_audio_native.active,(uint64_t)marble_audio_native.failed,(uint64_t)e->stream.phase,
        (uint64_t)e->stream.pending,(uint64_t)e->stream.tx,(uint64_t)e->stream.reply,(uint64_t)e->stream.start_retired,
        (uint64_t)(marble_audio_native.stopped?marble_audio_native.submitted:e->stream.submitted),
        (uint64_t)(marble_audio_native.stopped?marble_audio_native.completed:e->stream.completed),
        (uint64_t)marble_audio_native.published,(uint64_t)marble_audio_native.configured,
        (uint64_t)e->ring.rx_messages,(uint64_t)e->ring.tx_messages,(uint64_t)marble_audio_native.stopped,(uint64_t)marble_audio_native.restored);
}
static int marble_audio_native_begin(void)
{
    if(marble_audio_native.attempted || marble_pil_auth_attempted || !marble_qmp_hold.on_acked ||
       !marble_glink_control_native_same())return 0;
    marble_audio_native.attempted=marble_audio_native.active=1;
    marble_audio_native.epoch=READ_SYSREG(CNTVCT_EL0);
    if(!marble_audio_native.epoch)return 0;
    marble_audio_native.last_phase=0xffffffffu;
    marble_audio_native_log("begin");return 1;
}
static int marble_audio_native_service(void)
{
    MarbleAudioEndpoint *e=&marble_audio_native.endpoint;
    if(!marble_audio_native.active || marble_audio_native.failed || !marble_pil_auth_attempted ||
       !marble_glink_control_native_same())return 0;
    for(unsigned round=0;round<16;round++) {
        if(!e->ring.live) {
            MarbleAudioSmem view={0};
            if(!marble_audio_smem_find(marble_audio_native_read,0,&view) || (view.present!=3 && view.present!=7))goto fail;
            if(view.present==7) {
                if(!marble_audio_clock_guard(0) || !marble_audio_dma_inventory() ||
                   !marble_pcm_lease.active || !marble_pcm_route_active)goto fail;
                MarbleAudioGlinkIO io={0,marble_audio_native_read,marble_audio_native_write,marble_audio_native_barrier,marble_audio_native_kick};
                if(!marble_audio_endpoint_init(e,&io,marble_audio_native_publish,&marble_audio_native.queue,
                   (void *)(uintptr_t)marble_pcm_lease.result.physical,2,marble_audio_native_now()))goto fail;
                if(!marble_audio_stream_bind_epoch(&e->stream,marble_audio_native.epoch))goto fail;
            }
        }
        if(e->ring.live) {
            if(marble_audio_endpoint_poll(e,marble_audio_native_now())<0 || !marble_pcm_data_faults())goto fail;
            if(e->stream.phase!=marble_audio_native.last_phase) {
                marble_audio_native.last_phase=e->stream.phase;marble_audio_native_log("progress");
            }
        }
        uint32_t pending=*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10);
        if(pending==0xffffffffu)return 1;
        if(pending!=0x30000 && pending!=0x30002)goto fail;
        if(pending==0x30002) {
            MarbleSmp2pItem remote;uint32_t value=0;int present=marble_smp2p_remote_snapshot(&remote,&value);
            if(present<0 || (present && !marble_smp2p_negotiate(&remote)))goto fail;
            if(!present)return 1;
        }
        if(*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)!=pending)goto fail;
        __asm__ volatile("dsb sy" ::: "memory");
        *(volatile uint32_t *)(uintptr_t)(0xed18000+0x1c)=pending;
        __asm__ volatile("dsb sy" ::: "memory");
        if(pending==0x30000)marble_audio_native.glink_clears++;else marble_audio_native.smp2p_clears++;
    }
fail:
    marble_audio_native.failed=1;marble_audio_native_log("failure-retain");return 0;
}
static int marble_audio_native_complete(void)
{
    MarbleAudioEndpoint *e=&marble_audio_native.endpoint;
    MarbleAudioProof proof;
    if(!marble_audio_native.active || marble_audio_native.failed || !marble_audio_stream_protocol_retired(&e->stream) ||
       e->control_head!=e->control_tail || marble_audio_native.published!=2 || e->stream.completed!=2 ||
       !marble_audio_native.configured || !marble_pcm_data_faults() ||
       !marble_audio_stream_export_proof(&e->stream,MAPROOF_RETIRED,&proof) ||
       !marble_audio_proof_valid(&proof,marble_audio_native.epoch,MAPROOF_RETIRED))return 0;
    if(!marble_audio_native.proof_logged) {
        marble_audio_native.proof_logged=1;
        print("aud-owner-proof: phase=retired epoch=%x flags=63 MAP_typed=1 START_typed=1 UNMAP_typed=1 UNMAP_TX=1 submitted=2 completed=2 audible=0\n",marble_audio_native.epoch);
    }
    return marble_tdm_pins_finish(0) && marble_pcm_lease.active && marble_pcm_route_active;
}
static int marble_audio_native_quiet(void)
{
    if(!marble_smp2p_stop_qualified || !marble_audio_native_complete())return 0;
    for(unsigned i=0;i<16;i++) {
        if(!marble_audio_native_service())return 0;
        MarbleAudioGlink *g=&marble_audio_native.endpoint.ring;uint32_t head,tail,local;
        if(mag_index(g,3,&head) && mag_index(g,2,&tail) && mag_index(g,1,&local) &&
           head==tail && tail==g->tail && local==g->head &&
           *(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)==0xffffffffu)return 1;
    }
    return 0;
}
static void marble_audio_native_stopped(void)
{
    MarbleAudioStream *s=&marble_audio_native.endpoint.stream;
    marble_audio_native.submitted=s->submitted;marble_audio_native.completed=s->completed;
    /* Called ONLY by the successful, bounded PAS adapter. Route restoration
     * precedes stream reset, which zeroes the still-owned page before free. */
    marble_audio_native.stopped=1;
    unsigned ok=marble_tdm_pins_finish(1) && marble_pcm_data_faults() && marble_pcm_route(0);
    if(ok)ok=marble_audio_stream_reset_after_pas(s,1,1);
    MarbleAudioMapResult result=marble_pcm_lease.result;
    if(ok)ok=marble_audio_map_end(&marble_pcm_lease,&result);
    marble_audio_map_log(&result,ok);
    marble_audio_native.restored=ok;marble_audio_native.active=0;
    if(!ok)marble_audio_native.failed=1;
    marble_audio_native_log("stopped");
}
#endif
