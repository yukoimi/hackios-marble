/* Bounded SMCCC observations, based on Xiaomi commit 48952ed3 qcom_scm-smc.c.
 * Query entry point allows only IS_CALL_AVAILABLE(target PAS shutdown).
 * No WAITQ resume/ACK, PAS operation, hardware voltage or storage operation. */
#ifndef MARBLE_SCM_RAW_FW_H
#define MARBLE_SCM_RAW_FW_H
#include "marble_smccc.h"
#ifdef XNU_MARBLE
static void (*marble_smccc_progress)(const char *,uint64_t);
static void marble_smccc_once(const uint64_t request[8],uint64_t result[8])
{
    register uint64_t x0 __asm__("x0")=request[0],x1 __asm__("x1")=request[1];
    register uint64_t x2 __asm__("x2")=request[2],x3 __asm__("x3")=request[3];
    register uint64_t x4 __asm__("x4")=request[4],x5 __asm__("x5")=request[5];
    register uint64_t x6 __asm__("x6")=request[6],x7 __asm__("x7")=request[7];
    __asm__ volatile("smc #0" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),"+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7)
        : : "x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    result[0]=x0;result[1]=x1;result[2]=x2;result[3]=x3;
    result[4]=x4;result[5]=x5;result[6]=x6;result[7]=x7;
}
static int marble_scm_raw_query(unsigned index)
{
    if(index>3 || !marble_smccc_progress || !marble_scm_info())return 0;
    /* Same availability operation as native preflight; additionally exercise
     * Linux's standard-call variant. x6 and x7 start at zero as in Linux. */
    uint64_t fn=(index&1)?0x42000601:0xc2000601;
    uint64_t request[8]={fn,1,0x02000206,0,0,0,0,0},result[8]={0};
    typedef uint64_t (EFIAPI *Raise)(uint64_t);
    typedef void (EFIAPI *Restore)(uint64_t);
    if(!BS->raise_tpl || !BS->restore_tpl)return 0;
    marble_smccc_progress("SMCCC-query-before",index);
    uint64_t daif=READ_SYSREG(DAIF),begin=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    uint64_t old=((Raise)BS->raise_tpl)(8);unsigned calls=0;int action;
    do {
        marble_smccc_once(request,result);calls++;
        uint64_t us=(READ_SYSREG(CNTVCT_EL0)-begin)*1000000/freq;
        action=marble_smccc_next(request,result,calls,us);
        /* The baseline TPL8 USB trace was qualified by v84. These are
         * observations only; no SLEEP/WAKE token is ever resubmitted. */
        marble_smccc_progress("SMCCC-raw-a0",result[0]);
    } while(action==MARBLE_SMC_RESUME);
    ((Restore)BS->restore_tpl)(old);
    marble_smccc_progress("SMCCC-query-return",index);
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-begin)*1000000/freq,after=READ_SYSREG(DAIF);
    unsigned ok=action==MARBLE_SMC_DONE && result[1]==1 && old<=8 && daif==after && us<250000;
    print("marble-smccc: query index=%d PASS=%d fn=%x arginfo=1 target=2000206 initial_a6=0 initial_a7=0 calls=%d outcome=%d raw_a0=%x raw_a1=%x raw_a2=%x raw_a3=%x raw_a6=%x previous_TPL=%d DAIF_before=%x DAIF_after=%x elapsed_us=%d WAITQ_replays=0 PAS_calls=0 DSP_start=0\n",
        (uint64_t)index,(uint64_t)ok,fn,(uint64_t)calls,(uint64_t)(int64_t)action,result[0],result[1],result[2],result[3],result[6],old,daif,after,us);
    return ok;
}
static unsigned marble_qmp_smccc_attempted;
static unsigned marble_scm_raw_pas_authorized;
static struct {
    uint64_t request[8],result[8];unsigned attempted,calls,samples,services;int outcome;
    unsigned thermal_samples,thermal_ok;int32_t die_max;
    struct {uint32_t before,after,service;} interrupted[128];
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    struct {uint64_t smc_us,thermal_us,other_us,a0;} timing[128];
#endif
} marble_scm_raw_stop;
static efi_status marble_scm_raw_pas_shutdown(const uint64_t *params,uint64_t *results)
{
    /* Only an explicitly owned coordinator can stop PAS1. No AUTH/reset,
     * guessed WAITQ resume, table patch or unsupported secure-call replay. */
    unsigned closed_irq=marble_scm_raw_pas_authorized==2 && marble_qmp_ipcc.passed && marble_qmp_ipcc.writes==1 && marble_qmp_wire_preserved();
    unsigned running=marble_scm_raw_pas_authorized==4;
    unsigned held_irq=(marble_scm_raw_pas_authorized==3 || running) && marble_qmp_hold.on_acked && !marble_qmp_hold.stop_verified &&
        marble_qmp_hold.clears==1 && marble_qmp_wire.acks==1 && marble_qmp_hold_open();
    if(running && (!marble_pil_auth_attempted || !marble_glink_control_quiet()))return EFI_ERROR_BIT|3;
    if(running && !marble_irq_observer_snapshot(1))return EFI_ERROR_BIT|3;
    if((!closed_irq && !held_irq) ||
       *(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)!=0xffffffffu ||
       marble_scm_raw_stop.attempted || !params || !results || params[0]!=1 ||
       !marble_pil_header_only || marble_pil_auth_attempted!=running || !marble_pil_dsp_dirty ||
       !marble_pil_deferred.saved || !marble_pil_deferred.verified || marble_npa_active!=2 ||
       !marble_icb_scm_owned_authorized || !marble_pil_progress)return EFI_ERROR_BIT|3;
    for(unsigned i=1;i<10;i++)if(params[i])return EFI_ERROR_BIT|3;
    typedef uint64_t (EFIAPI *Raise)(uint64_t);
    typedef void (EFIAPI *Restore)(uint64_t);
    if(!BS->raise_tpl || !BS->restore_tpl)return EFI_ERROR_BIT|3;
    marble_scm_raw_stop.attempted=1;
    marble_scm_raw_stop.thermal_ok=1;
    unsigned max_calls=running?128:64;uint64_t max_us=running?1000000:250000;
    uint64_t *request=marble_scm_raw_stop.request,*raw=marble_scm_raw_stop.result;
    request[0]=0x42000206;request[1]=1;request[2]=1;
#ifdef XNU_MARBLE_AUDIO_TPL_EVENTS
    marble_tpl_events_capture(0);
#endif
    uint64_t daif=READ_SYSREG(DAIF),begin=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    marble_pil_progress("PAS-bounded-raw-before",1);
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    uint64_t entry_end=READ_SYSREG(CNTVCT_EL0);
#endif
    uint64_t old=((Raise)BS->raise_tpl)(8);int action;
#ifdef XNU_MARBLE_AUDIO_USB_POLL_LEASE
    /* Both timer calls stay INSIDE the original whole-operation PAS limit.
     * A rejected pause does not skip the essential secure shutdown. */
    unsigned usb_poll_ok=marble_usb_poll_pause();
#endif
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    uint64_t raise_end=READ_SYSREG(CNTVCT_EL0);
#endif
    do {
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
        uint64_t call_at=READ_SYSREG(CNTVCT_EL0);
#endif
        marble_smccc_once(request,raw);marble_scm_raw_stop.calls++;
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
        unsigned timing_index=marble_scm_raw_stop.calls-1;
        uint64_t smc_at=READ_SYSREG(CNTVCT_EL0),thermal_at=smc_at;
        marble_scm_raw_stop.timing[timing_index].smc_us=(smc_at-call_at)*1000000/freq;
        marble_scm_raw_stop.timing[timing_index].a0=raw[0];
#endif
        uint64_t us=(READ_SYSREG(CNTVCT_EL0)-begin)*1000000/freq;
        action=marble_smccc_next_limited(request,raw,marble_scm_raw_stop.calls,us,max_calls,max_us);
        if(running && ((marble_scm_raw_stop.calls-1)%4==0)) {
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
            thermal_at=READ_SYSREG(CNTVCT_EL0);
#endif
            MarbleThermalSample thermal={0};marble_scm_raw_stop.thermal_samples++;
            if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=55000) {
                marble_scm_raw_stop.thermal_ok=0;action=MARBLE_SMC_ERROR;
            }
            if(thermal.maximum_mc>marble_scm_raw_stop.die_max)marble_scm_raw_stop.die_max=thermal.maximum_mc;
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
            marble_scm_raw_stop.timing[timing_index].thermal_us=(READ_SYSREG(CNTVCT_EL0)-thermal_at)*1000000/freq;
#endif
        }
        /* Keep USB progress outside the resume window: producing a USB INFO
         * packet here can itself create another pending interrupt. Preserve
         * every interrupted receive observation in RAM instead. Only service
         * our two qualified DSP notifications, using the already acquired
         * fixed queues; no allocator, native dispatcher or unknown clear. */
        if(running && raw[0]==1 && marble_scm_raw_stop.samples<128) {
            if(!marble_scm_raw_stop.samples && !marble_irq_observer_snapshot(2))action=MARBLE_SMC_ERROR;
            unsigned i=marble_scm_raw_stop.samples++;
            uint32_t before=*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10),after=before,service=0;
            if(action==MARBLE_SMC_RESUME && (before==0x30000 || before==0x30002)) {
                service=marble_glink_control_service()?1:2;marble_scm_raw_stop.services++;
                after=*(volatile uint32_t *)(uintptr_t)(0xed18000+0x10);
                if(service!=1)action=MARBLE_SMC_ERROR;
            } else if(before!=0xffffffffu)action=MARBLE_SMC_ERROR;
            marble_scm_raw_stop.interrupted[i].before=before;marble_scm_raw_stop.interrupted[i].after=after;
            marble_scm_raw_stop.interrupted[i].service=service;
        }
        marble_scm_raw_stop.outcome=action;
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
        uint64_t call_us=(READ_SYSREG(CNTVCT_EL0)-call_at)*1000000/freq;
        uint64_t parts=marble_scm_raw_stop.timing[timing_index].smc_us+marble_scm_raw_stop.timing[timing_index].thermal_us;
        marble_scm_raw_stop.timing[timing_index].other_us=call_us>=parts?call_us-parts:0;
#endif
    } while(action==MARBLE_SMC_RESUME);
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    uint64_t loop_end=READ_SYSREG(CNTVCT_EL0);
#endif
    if(running && !marble_irq_observer_snapshot(3))action=MARBLE_SMC_ERROR;
#ifdef XNU_MARBLE_AUDIO_TPL_EVENTS
    /* Read only, counted in post_us and the original end-to-end PAS limit. */
    marble_tpl_events_capture(1);
#endif
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    uint64_t post_end=READ_SYSREG(CNTVCT_EL0);
#endif
    unsigned sampler_ok=1;
#ifdef XNU_MARBLE_AUDIO_TPL_SAMPLING
    sampler_ok=marble_tpl_sampler_arm();
#endif
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    uint64_t restore_at=READ_SYSREG(CNTVCT_EL0);
#endif
    ((Restore)BS->restore_tpl)(old);
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    uint64_t restored=READ_SYSREG(CNTVCT_EL0);
#endif
#ifdef XNU_MARBLE_AUDIO_TPL_SAMPLING
    sampler_ok=marble_tpl_sampler_release() && sampler_ok;
#endif
    uint64_t finished=READ_SYSREG(CNTVCT_EL0),us=(finished-begin)*1000000/freq,after=READ_SYSREG(DAIF);
#ifdef XNU_MARBLE_AUDIO_TPL_EVENTS
    marble_tpl_events_capture(2);marble_tpl_events_log();
#endif
#ifdef XNU_MARBLE_AUDIO_TPL_SAMPLING
    marble_tpl_sampler_log();
#endif
#ifdef XNU_MARBLE_AUDIO_PAS_TIMING
    print("aud-pas-restore-parts: arm_us=%d restore_us=%d release_us=%d whole_limit_us=1000000\n",
        (restore_at-post_end)*1000000/freq,(restored-restore_at)*1000000/freq,(finished-restored)*1000000/freq);
    uint64_t smc_total=0,thermal_total=0,other_total=0,max_smc=0;
    for(unsigned i=0;i<marble_scm_raw_stop.calls;i++) {
        uint64_t s=marble_scm_raw_stop.timing[i].smc_us,t=marble_scm_raw_stop.timing[i].thermal_us,o=marble_scm_raw_stop.timing[i].other_us;
        smc_total+=s;thermal_total+=t;other_total+=o;if(s>max_smc)max_smc=s;
        print("aud-pas-time: index=%d smc_us=%d thermal_us=%d other_us=%d raw_a0=%x\n",(uint64_t)i,s,t,o,marble_scm_raw_stop.timing[i].a0);
    }
    print("aud-pas-timing: version=2 calls=%d total_us=%d smc_us=%d thermal_us=%d other_us=%d max_smc_us=%d entry_us=%d raise_us=%d post_us=%d restore_us=%d limit_us=1000000 limit_calls=128 IRQ_masks_unchanged=1 USB_inside_calls=0 audio_acceptance=0\n",(uint64_t)marble_scm_raw_stop.calls,us,smc_total,thermal_total,other_total,max_smc,
        (entry_end-begin)*1000000/freq,(raise_end-entry_end)*1000000/freq,(post_end-loop_end)*1000000/freq,(restored-post_end)*1000000/freq);
#endif
    for(unsigned i=0;i<marble_scm_raw_stop.samples;i++)
        print("marble-smccc: interrupted index=%d before=%x after=%x service=%d USB_progress_calls=0\n",(uint64_t)i,
            (uint64_t)marble_scm_raw_stop.interrupted[i].before,(uint64_t)marble_scm_raw_stop.interrupted[i].after,
            (uint64_t)marble_scm_raw_stop.interrupted[i].service);
    if(running)print("marble-smccc: resume_audit samples=%d owned_service_calls=%d USB_progress_calls=0 foreign_clears=0 allocator_calls=0 native_dispatch=0 limit_calls=%d limit_us=%d thermal_samples=%d thermal_ok=%d die_max_mc=%d\n",
        (uint64_t)marble_scm_raw_stop.samples,(uint64_t)marble_scm_raw_stop.services,(uint64_t)max_calls,max_us,
        (uint64_t)marble_scm_raw_stop.thermal_samples,(uint64_t)marble_scm_raw_stop.thermal_ok,(uint64_t)(int64_t)marble_scm_raw_stop.die_max);
    if(running)marble_irq_observer_log();
    unsigned ok=action==MARBLE_SMC_DONE && raw[1]==0 && old<=8 && daif==after && us<max_us && marble_scm_raw_stop.thermal_ok && sampler_ok;
#ifdef XNU_MARBLE_AUDIO_USB_POLL_LEASE
    ok=ok && usb_poll_ok;
#endif
    if(held_irq) {
        if(ok)marble_qmp_hold.stop_verified=1;
        print("marble-qmp-hold: stop PASS=%d on_held=%d mailbox_ACKs=%d IPCC_clears=%d DSP_start=%d\n",
            (uint64_t)ok,(uint64_t)marble_qmp_hold_open(),(uint64_t)marble_qmp_wire.acks,(uint64_t)marble_qmp_hold.clears,(uint64_t)running);
    }
    results[0]=action==MARBLE_SMC_DONE?1:raw[0];results[1]=raw[1];results[2]=raw[2];results[3]=raw[3];
    print("marble-smccc: PAS_stop PASS=%d fn=42000206 arginfo=1 PAS_id=1 initial_a6=0 initial_a7=0 calls=%d outcome=%d raw_a0=%x raw_a1=%x raw_a2=%x raw_a3=%x raw_a6=%x previous_TPL=%d DAIF_before=%x DAIF_after=%x elapsed_us=%d WAITQ_replays=0 auth_reset=0 DSP_start=%d\n",
        (uint64_t)ok,(uint64_t)marble_scm_raw_stop.calls,(uint64_t)(int64_t)action,raw[0],raw[1],raw[2],raw[3],raw[6],old,daif,after,us,(uint64_t)running);
    if(ok && running)marble_glink_control_stopped();
#ifdef XNU_MARBLE_AUDIO_OWNER
    if(ok && running && (!marble_audio_native.restored || marble_audio_native.failed))return EFI_ERROR_BIT|7;
#endif
    return ok?0:EFI_ERROR_BIT|7;
}
static int marble_qmp_smccc_cycle(void)
{
    if(marble_qmp_smccc_attempted || marble_qmp_native.attempted || marble_pil_attempted || marble_npa_attempted ||
       marble_icb_attempted || marble_rpmh_attempted || marble_smp2p_attempted || marble_glink_observer.attempted ||
       marble_cap.active || marble_cap.dirty || marble_thermal_latched)return 0;
    if(EFI_ERROR(BS->set_watchdog_timer(10,0,0,NULL)))return 0;
    marble_qmp_smccc_attempted=marble_firmware_reset_required=1;
    unsigned before=marble_scm_raw_query(0) && marble_scm_raw_query(1);
    unsigned wire=before && marble_qmp_wire_cycle();
    unsigned after=wire && marble_scm_raw_query(2) && marble_scm_raw_query(3);
    unsigned preserved=after && marble_qmp_wire_preserved();
    print("marble-smccc: complete PASS=%d baseline=%d wire=%d post=%d firmware_preserved=%d availability_queries=4 WAITQ_replays=0 PAS_calls=0 NPA_calls=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1\n",
        (uint64_t)(before && wire && after && preserved),(uint64_t)before,(uint64_t)wire,(uint64_t)after,(uint64_t)preserved);
    return before && wire && after && preserved;
}
#endif
#endif
