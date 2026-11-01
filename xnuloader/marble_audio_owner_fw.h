/* EFI resource owner for one explicit XNU zero2 session. Never consumes or
 * writes the GLINK FIFO. The kernel's endpoint is the only FIFO producer/
 * consumer. Board/PAS operations remain in the original held call stack. */
#ifndef MARBLE_AUDIO_OWNER_FW_H
#define MARBLE_AUDIO_OWNER_FW_H
#include "../xnu/files/bsd/sys/marble_audio_owner.h"
#include "../xnu/files/bsd/sys/marble_audio_pcm_probe.h"
#ifdef XNU_MARBLE_AUDIO_OWNER_STREAM
#include "../xnu/files/bsd/sys/marble_audio_stream_window.h"
#endif
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
#include "../xnu/files/bsd/sys/marble_audio_board_gate.h"
static unsigned marble_audio_board_quiet;
static int marble_audio_board_held_action(void);
#endif
#ifndef MARBLE_AUDIO_OWNER_PERIODS
#define MARBLE_AUDIO_OWNER_PERIODS 2u
#endif
#ifdef XNU_MARBLE_AUDIO_OWNER_STREAM
_Static_assert(MARBLE_AUDIO_OWNER_PERIODS==24,"explicit stream24 period count");
#else
_Static_assert(MARBLE_AUDIO_OWNER_PERIODS==2 || MARBLE_AUDIO_OWNER_PERIODS==8,"qualified owner period count");
#endif
#include "../xnu/files/bsd/sys/marble_audio_smem.h"
static uint8_t *marble_audio_owner_page;
static MarbleAudioOwnerMailbox *marble_audio_owner_box;
static struct {
    unsigned attempted,active,failed,stopped,restored,published,configured;
    unsigned sequence_seen,submitted,completed,glink_clears,smp2p_clears;
    uint64_t epoch;
    uint64_t thermal_at;
    MarbleAudioOwnerGrant grant;
    MarbleAudioOwnerRequest request;
    MarbleAudioProof configure_proof,retired_proof;
    MarbleAudioSmem smem;
} marble_audio_native;
static unsigned marble_audio_owner_requested;
static const void *marble_audio_owner_package;
static uint64_t marble_audio_owner_package_bytes;
static int (*marble_audio_owner_launch)(void);
static uint64_t marble_audio_native_now(void)
{uint64_t t=READ_SYSREG(CNTVCT_EL0),f=READ_SYSREG(CNTFRQ_EL0);return t/f*1000000+t%f*1000000/f;}
static int marble_audio_owner_request_check(const MarbleAudioOwnerRequest *r,const MarbleAudioProof *p)
{
#ifdef XNU_MARBLE_AUDIO_OWNER_STREAM
    return mawindow_request_valid(r,p,marble_audio_native.epoch);
#else
    return marble_audio_owner_request_valid_periods(r,p,marble_audio_native.epoch,MARBLE_AUDIO_OWNER_PERIODS);
#endif
}
static int marble_audio_owner_grant_check(void)
{
#ifdef XNU_MARBLE_AUDIO_OWNER_STREAM
    return mawindow_grant_valid(&marble_audio_native.grant,marble_audio_native.epoch);
#else
    return marble_audio_owner_grant_valid_periods(&marble_audio_native.grant,marble_audio_native.epoch,MARBLE_AUDIO_OWNER_PERIODS);
#endif
}
static void marble_audio_owner_publish(void)
{
    MarbleAudioOwnerGrant *out=&marble_audio_owner_box->owner,*g=&marble_audio_native.grant;
    uint32_t next=g->sequence+2;out->sequence=next-1;
    clean_dcache((uint64_t)(uintptr_t)out,(uint64_t)(uintptr_t)out+64);
    __asm__ volatile("dsb sy":::"memory");
    g->sequence=next-1;memcpy(out,g,64);
    clean_dcache((uint64_t)(uintptr_t)out,(uint64_t)(uintptr_t)out+64);
    __asm__ volatile("dsb sy":::"memory");
    g->sequence=out->sequence=next;
    clean_dcache((uint64_t)(uintptr_t)out,(uint64_t)(uintptr_t)out+64);
}
static int marble_audio_owner_initialize(void)
{
    print("aud-owner-mapping: XNU_native_page_bytes=16384 DMA_leaf_bytes=4096 unmap_rounding=required protocol=zero2-v2\n");
    if(marble_audio_native.epoch || marble_audio_native.attempted)return 0;
    uint64_t allocated=0xffffffffULL;
    if(EFI_ERROR(BS->allocate_pages(AllocateMaxAddress,EfiLoaderData,8,&allocated)))return 0;
    uint64_t aligned=(allocated+16383)&~16383ULL;
    for(unsigned i=0;i<4;i++) {
        uint64_t par=marble_fw_translation(aligned+i*4096);
        if((par&1) || (par&0x0000fffffffff000ULL)!=aligned+i*4096)return 0;
    }
    marble_audio_owner_page=(void *)(uintptr_t)aligned;marble_audio_owner_box=(void *)marble_audio_owner_page;
    memset(marble_audio_owner_page,0,16384);
    uint64_t epoch=READ_SYSREG(CNTVCT_EL0);if(!epoch)return 0;
    marble_audio_native.epoch=epoch;
    marble_audio_owner_box->info=(MarbleAudioOwnerInfo){.magic=MAOWNER_MAGIC,.epoch=epoch,.version=MARBLE_AUDIO_OWNER_PERIODS==24?2:1,.bytes=320,
        .smem_pa=0x80900000,.smem_bytes=0x200000,.periods=MARBLE_AUDIO_OWNER_PERIODS,.ipcc_pa=0xed18000};
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
    MarbleAudioBoardGate *gate=(void *)(marble_audio_owner_page+MABG_OFFSET);
    gate->info=(MarbleAudioBoardGateInfo){.magic=MABG_MAGIC,.epoch=epoch,.version=MARBLE_AUDIO_OWNER_PERIODS==24?2:1,.bytes=256,.periods=MARBLE_AUDIO_OWNER_PERIODS,.muted=1};
#endif
    marble_audio_native.grant=(MarbleAudioOwnerGrant){.epoch=epoch,.state=MAOWNER_PREPARING};
    clean_dcache((uint64_t)(uintptr_t)marble_audio_owner_page,(uint64_t)(uintptr_t)marble_audio_owner_page+16384);
    marble_audio_owner_publish();return 1;
}
/* 0: publication in progress/no new request, 1: stable new request, -1: fault. */
static int marble_audio_owner_request(MarbleAudioOwnerRequest *r,MarbleAudioProof *p)
{
    MarbleAudioOwnerRequest again;
    uint64_t at=(uint64_t)(uintptr_t)&marble_audio_owner_box->client;
    flush_dcache(at,at+64);memcpy(r,(const void *)(uintptr_t)at,64);
    if(!r->sequence || (r->sequence&1))return 0;
    if(r->sequence==marble_audio_native.sequence_seen)return 0;
    flush_dcache(at+64,at+192);memcpy(p,(const void *)(uintptr_t)(at+64),128);
    __asm__ volatile("dsb sy":::"memory");flush_dcache(at,at+64);memcpy(&again,(const void *)(uintptr_t)at,64);
    if(memcmp(r,&again,64) || (again.sequence&1))return 0;
    if(r->sequence!=marble_audio_native.sequence_seen+2 || !marble_audio_owner_request_check(r,p))return -1;
    return 1;
}
static void marble_audio_native_log(const char *phase)
{
    print("aud-owner-fw: phase=%s epoch=%x state=%d active=%d failed=%d accepted_sequence=%d submitted=%d completed=%d configured=%d PAS_stopped=%d restored=%d GLINK_owner=XNU EFI_FIFO_writes=0 audio_acceptance=0\n",
        phase,marble_audio_native.epoch,(uint64_t)marble_audio_native.grant.state,(uint64_t)marble_audio_native.active,
        (uint64_t)marble_audio_native.failed,(uint64_t)marble_audio_native.sequence_seen,(uint64_t)marble_audio_native.submitted,
        (uint64_t)marble_audio_native.completed,(uint64_t)marble_audio_native.configured,(uint64_t)marble_audio_native.stopped,(uint64_t)marble_audio_native.restored);
}
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
static const char *marble_audio_owner_first_failure="no-owner-failure";
#endif
static int marble_audio_owner_fail(const char *phase)
{
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
    if(!marble_audio_native.failed)marble_audio_owner_first_failure=phase;
#endif
    marble_audio_native.failed=1;marble_audio_native.grant.state=MAOWNER_FAILED;marble_audio_native.grant.flags=0;
    marble_audio_owner_publish();marble_audio_native_log(phase);return 0;
}
static unsigned marble_audio_native_request_count(void){return marble_audio_native.submitted;}
static int marble_audio_owner_thermal_service(void)
{
    uint64_t now=marble_audio_native_now();
    if(now-marble_audio_native.thermal_at>=1000000) {
        marble_audio_native.thermal_at=now;
        if(!marble_thermal_check(0))marble_thermal_latched=1;
#ifdef XNU_MARBLE_AUDIO_RUNTIME_SAMPLING
        uint64_t thermal_us=marble_audio_native_now()-now;
        if(thermal_us>=10000)print("aud-runtime-thermal: elapsed_us=%d latched=%d read_only_timing=1\n",thermal_us,(uint64_t)marble_thermal_latched);
#endif
    }
    return !marble_thermal_latched;
}
static int marble_audio_owner_cap_service(void)
{
    if(!marble_cap_requested)return 0;
    uint64_t at=(uint64_t)(uintptr_t)marble_cap_mailbox;flush_dcache(at,at+64);
    if(!marble_cap_mailbox->result && marble_cap_mailbox->request==1) {
        int ok=marble_cpucap_prepare(marble_thermal_read,NULL,&marble_cap) &&
            marble_cpucap_apply(marble_thermal_read,marble_cap_write,NULL,&marble_cap);
        marble_cap_log("acquired",ok);marble_cap_mailbox->result=ok?2:3;clean_dcache(at,at+64);
    }
    return marble_cap_mailbox->result!=3;
}
static int marble_audio_native_begin(void)
{
    if(marble_audio_native.attempted || !marble_audio_native.epoch || !marble_audio_owner_launch || !marble_audio_owner_thermal_service() ||
       marble_pil_auth_attempted || !marble_qmp_hold.on_acked || !marble_glink_control_native_same())return 0;
    marble_audio_native.attempted=marble_audio_native.active=1;
    /* Resources passed all original cap-inactive preflights. Start the
     * prepared XNU core only now, before AUTH and the 600ms DSP window. */
    if(!marble_audio_owner_launch())return marble_audio_owner_fail("CPU-launch-failed");
    uint64_t until=marble_audio_native_now()+2000000,thermal_at=0;
    while(marble_audio_native_now()<until) {
        if(!marble_audio_owner_thermal_service() || !marble_audio_owner_cap_service())return marble_audio_owner_fail("cap-or-thermal-failed");
        MarbleAudioOwnerRequest request;MarbleAudioProof proof;
        int received=marble_audio_owner_request(&request,&proof);
        if(received<0)return marble_audio_owner_fail("ready-invalid");
        if(received && request.command==MAOWNER_READY && marble_cap_mailbox->result==2) {
            marble_audio_native.request=request;marble_audio_native.sequence_seen=request.sequence;
            marble_audio_native.grant.acknowledged=2;marble_audio_owner_publish();marble_audio_native_log("kernel-ready-before-AUTH");return 1;
        }
        uint64_t now=marble_audio_native_now();
        if(now-thermal_at>=100000) {if(!marble_smp2p_thermal_guard())return marble_audio_owner_fail("ready-thermal");thermal_at=now;}
        __asm__ volatile("yield":::"memory");
    }
    return marble_audio_owner_fail("kernel-ready-timeout");
}
static int marble_audio_owner_smem_read(void *cookie,uint32_t at,void *out,uint32_t bytes)
{
    (void)cookie;if(!out || at>MASMEM_BYTES || bytes>MASMEM_BYTES-at)return 0;
    for(uint64_t page=(0x80900000ULL+at)&~4095ULL;page<0x80900000ULL+at+bytes;page+=4096) {
        uint64_t par=marble_fw_translation(page);if((par&1) || (par&0x0000fffffffff000ULL)!=page)return 0;
    }
    const volatile uint8_t *src=(const volatile uint8_t *)(uintptr_t)(0x80900000ULL+at);
    for(unsigned i=0;i<bytes;i++)((uint8_t *)out)[i]=src[i];
    __asm__ volatile("dmb sy":::"memory");return 1;
}
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
static void marble_audio_owner_failure_wire(void)
{
    MarbleAudioSmem view={0};uint8_t desc[16]={0};uint32_t index[4];
    uint64_t began=marble_audio_native_now();
    if(!marble_audio_native.active || marble_audio_native.grant.state!=MAOWNER_RUNNING ||
       !marble_audio_smem_find(marble_audio_owner_smem_read,0,&view) || view.present!=7 ||
       view.partition!=marble_audio_native.smem.partition ||
       memcmp(view.item,marble_audio_native.smem.item,sizeof(view.item)) ||
       !marble_audio_owner_smem_read(0,view.item[0],desc,16))return;
    for(unsigned i=0;i<4;i++){index[i]=masmem32(desc+4*i);if(index[i]>=16384 || (index[i]&7))return;}
    print("aud-owner-wire-diagnostic: TX_tail=%x TX_head=%x RX_tail=%x RX_head=%x RAM_only=1 index_writes=0 FIFO_consumed=0 acceptance=0\n",
        (uint64_t)index[0],(uint64_t)index[1],(uint64_t)index[2],(uint64_t)index[3]);
    for(unsigned ring=1;ring<=2;ring++) {
        uint32_t anchor=index[ring==1?1:2];
        for(unsigned j=0;j<192;j+=4) {
            uint8_t word[4]={0};uint32_t at=(anchor+16384-128+j)&16383;
            if(!marble_audio_owner_smem_read(0,view.item[ring]+at,word,4))return;
            print("aud-owner-wire-word: ring=%d relative=%d offset=%x value=%x\n",(uint64_t)ring,(uint64_t)j,(uint64_t)at,(uint64_t)masmem32(word));
        }
    }
    print("aud-owner-wire-diagnostic-end: elapsed_us=%d max_bytes=384 RAM_only=1 index_writes=0 IPCC_writes=0\n",marble_audio_native_now()-began);
}
#endif
static int marble_audio_native_start_qualified(void)
{
    return marble_audio_native.active && !marble_audio_native.failed && marble_audio_native.sequence_seen==4 &&
        marble_audio_native.request.command==MAOWNER_CONFIGURE && marble_pcm_lease.active && marble_pcm_route_active &&
        marble_audio_proof_valid(&marble_audio_native.configure_proof,marble_audio_native.epoch,MAPROOF_CONFIGURE);
}
#ifdef XNU_MARBLE_AUDIO_OWNER_NATIVE_LINK
static int marble_audio_owner_link_gate(void)
{
    MarbleAudioProof zero={0};
    int ready=marble_audio_native.active && !marble_audio_native.failed && marble_audio_native.attempted &&
        !marble_pil_auth_attempted && marble_qmp_hold.on_acked && marble_glink_control_native_same() &&
        marble_audio_native.sequence_seen==2 && marble_audio_native.request.command==MAOWNER_READY &&
        marble_audio_native.grant.state==MAOWNER_PREPARING && marble_audio_native.grant.acknowledged==2 &&
        marble_audio_owner_grant_check() && marble_audio_owner_request_check(&marble_audio_native.request,&zero) &&
        marble_cap_requested && marble_cap_mailbox->result==2;
    print("aud-owner-link-gate: PASS=%d phase=pre-AUTH XNU_READY=%d EFI_register_calls=0 EFI_deregister_calls=0 FIFO_owner=XNU DSP_link_inferred=0\n",
        (uint64_t)ready,(uint64_t)ready);
    return ready;
}
#endif
static int marble_audio_native_service(void)
{
    if(!marble_audio_native.active || marble_audio_native.failed || !marble_pil_auth_attempted || !marble_glink_control_native_same() ||
       !marble_audio_owner_thermal_service())return 0;
    if(marble_audio_native.grant.state==MAOWNER_PREPARING) {
        MarbleAudioSmem view={0};
        if(!marble_audio_smem_find(marble_audio_owner_smem_read,0,&view) || (view.present!=3 && view.present!=7))return marble_audio_owner_fail("SMEM-invalid");
        if(view.present==7) {
            if(!marble_audio_clock_guard(0) || !marble_audio_dma_inventory() || !marble_pcm_lease.active || !marble_pcm_route_active)return marble_audio_owner_fail("DMA-grant-failed");
            marble_audio_native.smem=view;marble_audio_native.grant.state=MAOWNER_RUNNING;
            marble_audio_native.grant.page_pa=marble_pcm_lease.result.physical;
            marble_audio_native.grant.deadline_us=marble_audio_native_now()+600000;
            marble_audio_owner_publish();marble_audio_native_log("DMA-granted");
        }
    }
    MarbleAudioOwnerRequest request;MarbleAudioProof proof;
    int received=marble_audio_owner_request(&request,&proof);
    if(received<0)return marble_audio_owner_fail("request-invalid");
    if(received) {
        if(request.command==MAOWNER_CONFIGURE && marble_audio_native.grant.state==MAOWNER_RUNNING) {
            marble_audio_native.configure_proof=proof;marble_audio_native.request=request;marble_audio_native.sequence_seen=request.sequence;
            if(!marble_audio_native_start_qualified() || !marble_pcm_data_faults())return marble_audio_owner_fail("configure-proof-failed");
#ifdef XNU_MARBLE_AUDIO_OWNER_PCM
            /* XNU has filled the first page but cannot submit DATA until ACK4.
             * Independently check its bounded samples and unused tail. */
            uint64_t pa=marble_pcm_lease.result.physical;
            flush_dcache(pa,pa+4096);__asm__ volatile("dsb sy":::"memory");
            MarbleAudioPcmCheck pcm;
            if(!marble_audio_pcm_probe_page((void *)(uintptr_t)pa,4096,1,&pcm))return marble_audio_owner_fail("PCM-first-page-invalid");
            print("aud-owner-PCM-first: peak=%d nonzero=%d hash=%x bytes=1024 tail_zero=1 audible=0\n",(uint64_t)pcm.peak,(uint64_t)pcm.nonzero,(uint64_t)pcm.hash);
#endif
#ifdef XNU_MARBLE_AUDIO_OWNER_BOARD
            /* The proof belongs to this XNU session, not legacy EFI counters.
             * Keep HMUTE/90dB and the existing bounded AMPPD/restore flow. */
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
            marble_i2c_board_action=marble_audio_board_held_action;
            marble_i2c_board_held_requested=1;
#endif
            unsigned board_ok=marble_tdm_pins_begin() && marble_pcm_i2c_window(3);
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
            marble_i2c_board_held_requested=0;marble_i2c_board_action=NULL;
#endif
            if(!board_ok ||
               !marble_i2c_window_passed || marble_i2c_amp_dirty || !marble_pcm_data_faults() ||
               !marble_audio_owner_thermal_service())return marble_audio_owner_fail("board-muted-configure-failed");
#ifdef XNU_MARBLE_AUDIO_OWNER_PCM
#ifdef XNU_MARBLE_AUDIO_OWNER_STREAM
            print("aud-owner-board: profile=muted-Music-PCM24 zero_only=0 TDM_pin_writes=1 amplifier_commands=1 audible=0\n");
#else
            print("aud-owner-board: profile=muted-Music-PCM8 zero_only=0 TDM_pin_writes=1 amplifier_commands=1 audible=0\n");
#endif
#else
            print("aud-owner-board: profile=muted-Music zero_only=1 TDM_pin_writes=1 amplifier_commands=1 audible=0\n");
#endif
#else
            /* First XNU owner test deliberately keeps physical routing and
             * both amplifiers untouched. CONFIGURED grants zero DATA only;
             * it is not a claim of completed pins/Music/amp initialization. */
            print("aud-owner-board: profile=disconnected zero_only=1 TDM_pin_writes=0 amplifier_commands=0 audible=0\n");
#endif
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
            /* ACK4 was published with the config/clock lease held. Quiet ACK
             * follows only after the enclosing window restored both leases. */
            if(!marble_audio_board_quiet)return marble_audio_owner_fail("board-quiet-missing");
            MarbleAudioBoardGate *gate=(void *)(marble_audio_owner_page+MABG_OFFSET);
            MarbleAudioBoardGateQuiet quiet={.epoch=marble_audio_native.epoch,.sequence=1,.flags=7,.submitted=MARBLE_AUDIO_OWNER_PERIODS,.completed=MARBLE_AUDIO_OWNER_PERIODS};
            memcpy(&gate->quiet,&quiet,64);
            uint64_t qa=(uint64_t)(uintptr_t)&gate->quiet;clean_dcache(qa,qa+64);__asm__ volatile("dsb sy":::"memory");
            gate->quiet.sequence=2;clean_dcache(qa,qa+64);__asm__ volatile("dsb sy":::"memory");
            print("aud-board-held: phase=quiet-ACK flags=7 submitted=%d completed=%d STOP_allowed=1 config_active=0 clock_active=0 audible=0\n",
                (uint64_t)MARBLE_AUDIO_OWNER_PERIODS,(uint64_t)MARBLE_AUDIO_OWNER_PERIODS);
#else
            marble_audio_native.configured=1;marble_audio_native.grant.state=MAOWNER_CONFIGURED;marble_audio_native.grant.acknowledged=4;
            marble_audio_owner_publish();marble_audio_native_log("configured-real-START-proof");
#endif
        } else if(request.command==MAOWNER_RETIRE && marble_audio_native.grant.state==MAOWNER_CONFIGURED &&
            proof.handle==marble_audio_native.configure_proof.handle && proof.start_cookie==marble_audio_native.configure_proof.start_cookie &&
            !memcmp(proof.map,marble_audio_native.configure_proof.map,28) && !memcmp(proof.start,marble_audio_native.configure_proof.start,32)) {
            marble_audio_native.retired_proof=proof;marble_audio_native.request=request;marble_audio_native.sequence_seen=request.sequence;
            marble_audio_native.submitted=proof.submitted;marble_audio_native.completed=proof.completed;
            marble_audio_native_log("retired-real-UNMAP-proof");
        } else return marble_audio_owner_fail("request-order-failed");
    }
    if(marble_pcm_lease.active && !marble_pcm_data_faults())return marble_audio_owner_fail("SMMU-fault");
    for(unsigned i=0;i<16;i++) {
        uint32_t pending=*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10);
        if(pending==0xffffffffu)return 1;
        if(pending!=0x30000 && pending!=0x30002)return marble_audio_owner_fail("foreign-IPCC");
        if(pending==0x30002) {
            MarbleSmp2pItem remote;uint32_t value=0;int present=marble_smp2p_remote_snapshot(&remote,&value);
            if(present<0 || (present && !marble_smp2p_negotiate(&remote)))return marble_audio_owner_fail("SMP2P-invalid");
            if(!present)return 1;
        }
        if(*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)!=pending)return marble_audio_owner_fail("IPCC-changed");
        __asm__ volatile("dsb sy":::"memory");*(volatile uint32_t *)(uintptr_t)(0xed18000+0x1c)=pending;__asm__ volatile("dsb sy":::"memory");
        if(pending==0x30000)marble_audio_native.glink_clears++;else marble_audio_native.smp2p_clears++;
    }
    return marble_audio_owner_fail("IPCC-budget");
}
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
static int marble_audio_board_held_action(void)
{
    unsigned begun=marble_i2c_amp_id(),drained=0,ok=0;
    if(begun && marble_i2c_board_check() && marble_pcm_data_faults() && marble_audio_owner_thermal_service()) {
        marble_audio_native.configured=1;marble_audio_native.grant.state=MAOWNER_CONFIGURED;
        marble_audio_native.grant.acknowledged=4;marble_audio_owner_publish();
        marble_audio_native_log("configured-real-START-proof");
        print("aud-board-held: phase=DATA-granted devices=2 HMUTE=1 attenuation_db=90 STOP_allowed=0 audible=0\n");
        uint64_t at=(uint64_t)(uintptr_t)(marble_audio_owner_page+MABG_OFFSET+64);
        while(marble_audio_native_now()<marble_audio_native.grant.deadline_us) {
            if(!marble_audio_owner_thermal_service() || !marble_pcm_data_faults() || !marble_smp2p_thermal_guard())break;
            MarbleAudioProof a,b;
            flush_dcache(at,at+128);memcpy(&a,(void *)(uintptr_t)at,128);__asm__ volatile("dsb sy":::"memory");
            if(a.phase) {
                flush_dcache(at,at+128);memcpy(&b,(void *)(uintptr_t)at,128);__asm__ volatile("dsb sy":::"memory");
                if(!memcmp(&a,&b,128)) {
#ifdef XNU_MARBLE_AUDIO_OWNER_STREAM
                    drained=mawindow_drained_valid(&a,&marble_audio_native.configure_proof,marble_audio_native.epoch);
#else
                    drained=marble_audio_board_gate_drained_valid(&a,&marble_audio_native.configure_proof,marble_audio_native.epoch);
#endif
                    break;
                }
            }
            /* Clear only the qualified GLINK notification. The XNU worker
             * still owns every FIFO read/write; never recurse into service. */
            uint32_t pending=*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10);
            if(pending!=0xffffffffu) {
                if(pending!=0x30000 || *(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)!=pending)break;
                __asm__ volatile("dsb sy":::"memory");*(volatile uint32_t *)(uintptr_t)(0xed18000+0x1c)=pending;
                __asm__ volatile("dsb sy":::"memory");marble_audio_native.glink_clears++;
            }
            BS->stall(100);
        }
        if(drained)ok=marble_i2c_board_check();
    }
    print("aud-board-held: phase=drained PASS=%d submitted=%d completed=%d STOP_allowed=0 audible=0\n",
        (uint64_t)drained,(uint64_t)(drained?MARBLE_AUDIO_OWNER_PERIODS:0),(uint64_t)(drained?MARBLE_AUDIO_OWNER_PERIODS:0));
    /* Always attempt both devices' shutdown, including timeout/bad proof.
     * Any ambiguous bus/power state retains leases and forces cold reset. */
    unsigned finished=marble_i2c_board_finish();
    marble_audio_board_quiet=begun && drained && ok && finished;
    return marble_audio_board_quiet;
}
#endif
static int marble_audio_native_complete(void)
{
    return marble_audio_native.active && !marble_audio_native.failed && marble_audio_native.configured &&
        marble_audio_native.sequence_seen==6 && marble_audio_native.submitted==MARBLE_AUDIO_OWNER_PERIODS && marble_audio_native.completed==MARBLE_AUDIO_OWNER_PERIODS &&
        marble_audio_proof_valid(&marble_audio_native.retired_proof,marble_audio_native.epoch,MAPROOF_RETIRED) &&
        marble_pcm_lease.active && marble_pcm_route_active && marble_pcm_data_faults() && marble_tdm_pins_finish(0);
}
static int marble_audio_native_quiet(void)
{
    if(!marble_smp2p_stop_qualified || !marble_audio_native_complete())return 0;
    if(!marble_audio_native_service())return 0;
    uint8_t bytes[16];if(!marble_audio_owner_smem_read(0,marble_audio_native.smem.item[0],bytes,16))return 0;
    uint32_t tx_tail=masmem32(bytes),tx_head=masmem32(bytes+4),rx_tail=masmem32(bytes+8),rx_head=masmem32(bytes+12);
    return tx_head==tx_tail && rx_head==rx_tail && tx_head==marble_audio_native.request.head &&
        !((tx_head|rx_tail)&7) && tx_head<16384 && rx_tail<16384 && *(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)==0xffffffffu;
}
static void marble_audio_native_stopped(void)
{
    marble_audio_native.stopped=1;
    if(!marble_audio_native_complete() || !marble_pcm_route(0)) {marble_audio_owner_fail("PAS-route-restore-failed");return;}
    marble_audio_native.grant.state=MAOWNER_PAS_OFF;marble_audio_native.grant.flags=3;marble_audio_native.grant.acknowledged=6;
    marble_audio_native.grant.submitted=marble_audio_native.submitted;marble_audio_native.grant.completed=marble_audio_native.completed;
    marble_audio_owner_publish();marble_audio_native_log("PAS-off-wait-XNU-unmap");
    uint64_t until=marble_audio_native_now()+100000;unsigned unmapped=0;
    do {
        MarbleAudioOwnerRequest request;MarbleAudioProof proof;int received=marble_audio_owner_request(&request,&proof);
        if(received<0)break;
        if(received && request.command==MAOWNER_UNMAPPED) {marble_audio_native.sequence_seen=request.sequence;unmapped=1;break;}
        __asm__ volatile("yield":::"memory");
    } while(marble_audio_native_now()<until);
    if(!unmapped) {marble_audio_owner_fail("XNU-unmap-ACK-missing-retain");return;}
    memset((void *)(uintptr_t)marble_pcm_lease.result.physical,0,4096);
    clean_dcache(marble_pcm_lease.result.physical,marble_pcm_lease.result.physical+4096);
    MarbleAudioMapResult result=marble_pcm_lease.result;
    unsigned ok=marble_audio_map_end(&marble_pcm_lease,&result);marble_audio_map_log(&result,ok);
    if(!ok) {marble_audio_owner_fail("table-restore-failed-retain");return;}
    marble_audio_native.restored=1;marble_audio_native.active=0;marble_audio_native.grant.state=MAOWNER_RELEASED;
    marble_audio_native.grant.page_pa=0;marble_audio_native.grant.flags=7;marble_audio_native.grant.acknowledged=8;
    marble_audio_owner_publish();marble_audio_native_log("DMA-released-after-XNU-unmap");
}
#endif
