#ifndef MARBLE_ADSP_OWNED_FW_H
#define MARBLE_ADSP_OWNED_FW_H
#ifdef XNU_MARBLE
#include "marble_adsp_package.h"
static const uint8_t *marble_adsp_owned_files;
static unsigned marble_adsp_owned_attempted;
static unsigned marble_adsp_owned_execute;
static unsigned marble_adsp_owned_qmp;
static unsigned marble_adsp_owned_peak;
static unsigned marble_adsp_owned_wire;
static unsigned marble_adsp_owned_raw_shutdown;
static unsigned marble_adsp_owned_ipcc;
static unsigned marble_adsp_owned_hold;
static unsigned marble_adsp_owned_control;
static int marble_adsp_owned_post_load(void)
{
    if((!marble_adsp_owned_qmp && !marble_adsp_owned_wire) || (marble_adsp_owned_execute && !marble_adsp_owned_control) || !marble_pil_dsp_dirty ||
       !marble_pil_deferred.saved || !marble_pil_deferred.verified || marble_pil_auth_attempted)return 0;
    if(marble_adsp_owned_hold) {
        unsigned ok=marble_qmp_hold_begin();
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
        marble_qmp_timeline("on-ready-before-XNU");
#endif
        if(ok && marble_adsp_owned_control)ok=marble_glink_control_begin();
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
        marble_qmp_timeline("XNU-ready-return");
#endif
        if(marble_pil_progress)marble_pil_progress("QMP-held-on-return",ok);
        return ok;
    }
    if(marble_adsp_owned_wire) {
        if(marble_pil_progress)marble_pil_progress("AP-wire-post-load-before",1);
        if(marble_adsp_owned_ipcc) {
            marble_qmp_ipcc_owned_authorized=1;
            int qualified=marble_qmp_ipcc_prepare(1);
            marble_qmp_ipcc_owned_authorized=0;
            if(!qualified)return 0;
        }
        marble_qmp_wire_owned_authorized=marble_adsp_owned_wire;
        int ok=marble_qmp_wire_cycle_mode(1,marble_adsp_owned_wire==1);
        marble_qmp_wire_owned_authorized=0;
        if(ok)ok=marble_qmp_wire_preserved();
        if(ok && marble_adsp_owned_ipcc) {
            ok=marble_qmp_ipcc_drain();
            if(marble_pil_progress)marble_pil_progress("QMP-IPCC-owned-ack",ok);
        }
        if(marble_pil_progress)marble_pil_progress("AP-wire-noexec-unwind-return",ok);
        return ok;
    }
    marble_qmp_owned_authorized=2;
    uint64_t daif_before=READ_SYSREG(DAIF);
    if(marble_pil_progress)marble_pil_progress("QMP-DAIF-before",daif_before);
    if(marble_pil_progress)marble_pil_progress("QMP-post-load-before",1);
    int passed=marble_qmp_native_cycle(1,2,4);
    marble_qmp_owned_authorized=0;
    if(passed)passed=marble_qmp_native_cancel_noexec();
    if(marble_pil_progress)marble_pil_progress("QMP-noexec-off-close-return",passed);
    uint64_t daif_after=READ_SYSREG(DAIF);
    if(marble_pil_progress)marble_pil_progress("QMP-DAIF-after",daif_after);
    print("marble-qmp-load: CPU_context PASS=%d DAIF_before=%x DAIF_after=%x direct_mask_writes=0\n",(uint64_t)(daif_before==daif_after),daif_before,daif_after);
    if(daif_before!=daif_after)return 0;
    return passed;
}
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
#include "marble_audio_completion.h"
static int marble_adsp_runtime_complete(void)
{
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    return marble_audio_native_complete();
#else
#ifdef XNU_MARBLE_AUDIO_PCM_REPEAT
    if(marble_pcm_repeat.completed!=MARBLE_PCM_REPEAT_COUNT || marble_pcm_submitted!=MARBLE_PCM_REPEAT_COUNT || !marble_pcm_data_ready())return 0;
#endif
    MarbleAudioCompletion s={
        marble_tdm_run.next,marble_tdm_run.sent,marble_tdm_run.tx,marble_tdm_run.reply,marble_tdm_run.rejected,
        marble_pcm_data.sent,marble_pcm_data.tx,marble_pcm_data.reply,marble_pcm_data.bad,marble_pcm_data.rx.active,
        marble_pcm_data.result.kind,marble_pcm_data.result.error,marble_pcm_data.result.md_error,marble_pcm_data.zero,marble_pcm_data.faults,
        marble_pcm_memory.finished,marble_pcm_memory.sent,marble_pcm_memory.tx,marble_pcm_memory.reply,marble_pcm_memory.rejected,
        marble_pcm_memory.error[0],marble_pcm_memory.error[1],marble_pcm_memory.rx.active,
        marble_pcm_lease.active,marble_pcm_route_active,marble_tdm_pins.active,marble_tdm_pins.restored,marble_i2c_amp_dirty};
    return marble_audio_complete(&s);
#endif
}
static void marble_adsp_runtime_probe(void)
{
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    marble_audio_native_log("runtime");
#else
    if(!marble_pil_progress)return;
    marble_pil_progress("aud-gpr",((uint64_t)marble_tdm_run.next<<40)|((uint64_t)marble_tdm_run.sent<<32)|((uint64_t)marble_tdm_run.tx<<24)|((uint64_t)marble_tdm_run.reply<<16)|marble_tdm_run.rejected);
    marble_pil_progress("aud-PCM",marble_pcm_data.sent|((uint64_t)marble_pcm_data.tx<<8)|((uint64_t)marble_pcm_data.reply<<16)|((uint64_t)marble_pcm_data.bad<<24)|((uint64_t)marble_pcm_data.result.error<<32));
    marble_pil_progress("aud-map",marble_pcm_memory.sent|((uint64_t)marble_pcm_memory.tx<<8)|((uint64_t)marble_pcm_memory.reply<<16)|((uint64_t)marble_pcm_memory.rejected<<24)|((uint64_t)marble_pcm_memory.finished<<32));
    marble_pil_progress("aud-owned",marble_pcm_lease.active|((uint64_t)marble_tdm_pins.active<<8)|((uint64_t)marble_tdm_pins.restored<<16)|((uint64_t)marble_i2c_amp_dirty<<24));
#endif
}
#endif
static int marble_adsp_owned_rom_action(void)
{
    if(!marble_adsp_owned_files || !marble_pil_header_only || marble_pil_auth_attempted ||
       !marble_smp2p_owned || !marble_smp2p_notifications || marble_npa_active!=2)return -1;
    print("marble-adsp-owned: phase=%s bandwidth_held=1 XO_held=1 LPI_clients=2 DSP_start=0\n",marble_adsp_owned_execute?"ROM-boot":"ROM-noexec");
    marble_pil_owned_dependencies_ready=marble_adsp_owned_execute;
    if(marble_adsp_owned_execute && marble_pil_progress)marble_pil_progress("owned-dependencies-ready",1);
    if(marble_adsp_owned_qmp || marble_adsp_owned_wire)marble_pil_post_load_hook=marble_adsp_owned_post_load;
    if(marble_adsp_owned_hold)marble_pil_post_shutdown_hook=marble_qmp_hold_finish;
    if(marble_adsp_owned_raw_shutdown) {
        marble_scm_raw_pas_authorized=marble_adsp_owned_control?4:marble_adsp_owned_hold?3:marble_adsp_owned_ipcc?2:1;
        marble_pil_shutdown_adapter=marble_scm_raw_pas_shutdown;
    }
    if(marble_adsp_owned_control)marble_smp2p_runtime_service=marble_glink_control_service;
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
    marble_pil_runtime_probe=marble_adsp_runtime_probe;
    marble_pil_runtime_complete=marble_adsp_runtime_complete;
#endif
    int passed=marble_pil_file_probe(marble_adsp_owned_files,30057264,marble_adsp_owned_execute?10:8);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
    marble_pil_runtime_probe=NULL;
    marble_pil_runtime_complete=NULL;
#endif
    marble_smp2p_runtime_service=NULL;
    marble_pil_post_load_hook=NULL;
    marble_pil_post_shutdown_hook=NULL;
    marble_pil_shutdown_adapter=NULL;marble_scm_raw_pas_authorized=0;
    marble_pil_owned_dependencies_ready=0;
    if(marble_pil_dsp_dirty || marble_pil_deferred.saved || marble_pil_ram_device.dirty || marble_pil_ram_device.installed ||
       marble_pil_auth_attempted!=marble_adsp_owned_execute || marble_glink_observer.active || marble_glink_observer.dirty)return -1;
    if(marble_adsp_owned_qmp && (!passed || marble_qmp_native.active || marble_qmp_native.dirty ||
       marble_qmp_native.tx_acks!=2 || marble_qmp_native.tx_pending))return -1;
    if(marble_adsp_owned_wire && (!passed || !marble_qmp_wire_preserved()))return -1;
    if(marble_adsp_owned_hold && !marble_qmp_hold.passed)return -1;
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    if(marble_adsp_owned_control && (!marble_audio_native.stopped || !marble_audio_native.restored || marble_audio_native.failed || marble_audio_native.active))return -1;
#else
    if(marble_adsp_owned_control && (!marble_glink_control_state.stopped || marble_glink_control_state.failed || marble_glink_control_state.active))return -1;
#endif
#ifdef XNU_MARBLE_AUDIO_QUP_HOLD
    if(marble_audio_qup_hold.active && !marble_audio_qup_end())return -1;
    if(marble_audio_qup_hold.prepared || marble_audio_qup_hold.held || marble_audio_qup_hold.dirty || marble_audio_qup_hold.uncertain)return -1;
#endif
#if !defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) && !defined(XNU_MARBLE_AUDIO_OWNER)
    if(marble_glink_control_state.app_mode && (!marble_glink_control_state.app_requested || !marble_glink_control_state.app_ack || !marble_glink_control_state.app_intents))passed=0;
#endif
    return passed?1:0;
}
static int marble_adsp_owned_dependencies_action(void)
{
    print("marble-adsp-owned: phase=xo LPI_clients=2 LPI_level=256\n");
    if(!marble_xo_rpmh_hold_mode(1))return -1;
#ifdef XNU_MARBLE_QUP_CORE_KEEPALIVE
    if(!marble_qup_keepalive_hold())return -1;
#endif
#ifdef XNU_MARBLE_AUDIO_AMP_PREBOOT
    if(!marble_pcm_i2c_window(1))return -1;
#endif
    print("marble-adsp-owned: phase=bandwidth baseline=RUN_XO_LPI_held\n");
    marble_icb_bandwidth_held_action=marble_adsp_owned_rom_action;
    int passed;
    if(marble_adsp_owned_peak) {
        marble_icb_scm_owned_authorized=1;
        passed=marble_icb_bandwidth_cycle_peak(3,MARBLE_SCM_PEAK_BYTES);
        marble_icb_scm_owned_authorized=0;
    } else passed=marble_icb_bandwidth_cycle_mode(2);
    marble_icb_bandwidth_held_action=NULL;
    return passed?1:-1;
}
static int marble_adsp_owned_cycle_inner(const void *raw,uint64_t bytes,unsigned execute,unsigned qmp)
{
    const uint8_t *driver=NULL,*files=NULL;
    print("aud-owned-preflight: icb=%d rpmh=%d npa=%d pil=%d smp2p=%d cap_active=%d cap_dirty=%d owned=%d display=%d SCM_mask=%x execute=%d qmp=%d\n",
        (uint64_t)marble_icb_attempted,(uint64_t)marble_rpmh_attempted,(uint64_t)marble_npa_attempted,
        (uint64_t)marble_pil_attempted,(uint64_t)marble_smp2p_attempted,(uint64_t)marble_cap.active,
        (uint64_t)marble_cap.dirty,(uint64_t)marble_adsp_owned_attempted,(uint64_t)marble_display_memory_qualified,
        (uint64_t)marble_scm_available_mask,(uint64_t)execute,(uint64_t)qmp);
#ifndef XNU_MARBLE_GPR_ONLY
    if(qmp==510)return 0;
#endif
    if(marble_icb_attempted || marble_rpmh_attempted || marble_npa_attempted || marble_pil_attempted ||
       marble_smp2p_attempted || marble_cap.active || marble_cap.dirty || marble_adsp_owned_attempted ||
       execute>1 || (qmp>3 && qmp!=6 && qmp!=10 && qmp!=14 && qmp!=30 && qmp!=62 && qmp!=126 && qmp!=254 && qmp!=510) ||
       (qmp && execute && qmp!=126 && qmp!=254 && qmp!=510) || ((qmp==126 || qmp==254 || qmp==510) && execute!=1) || !marble_display_memory_qualified || marble_scm_available_mask!=15 || !marble_adsp_package_open(raw,bytes,&driver,&files))return 0;
    if(qmp==126 || qmp==254 || qmp==510) {
        unsigned irq=marble_irq_observer_qualify();
        unsigned smem=irq && marble_glink_smem_info();
        print("aud-owned-preflight: IRQ=%d SMEM=%d thermal_latched=%d qmp_attempted=%d glink_attempted=%d\n",
            (uint64_t)irq,(uint64_t)smem,(uint64_t)marble_thermal_latched,
            (uint64_t)marble_qmp_native.attempted,(uint64_t)marble_glink_observer.attempted);
        if(!irq || !smem)return 0;
    }
    marble_adsp_owned_execute=execute;
    marble_adsp_owned_qmp=(qmp&1)!=0;marble_adsp_owned_peak=(qmp&2)!=0;
    marble_adsp_owned_wire=qmp==6 || qmp==14 || qmp==30 || qmp==62 || qmp==126 || qmp==254 || qmp==510?1:qmp==10?2:0;
    marble_adsp_owned_raw_shutdown=qmp==14 || qmp==30 || qmp==62 || qmp==126 || qmp==254 || qmp==510;
    marble_adsp_owned_ipcc=qmp==30;
    marble_adsp_owned_hold=qmp==62 || qmp==126 || qmp==254 || qmp==510;
    marble_adsp_owned_control=qmp==126 || qmp==254 || qmp==510;
    marble_glink_control_state.app_mode=qmp==510?2:qmp==254;
    print("marble-adsp-owned: begin contract=%s package_bytes=30180144 package_fingerprints=2 DSP_start=0\n",
        execute?(marble_adsp_owned_control?"RUN-XO-LPI-SCM-BW-ROM-boot-stop-reset-v1":"RUN-XO-LPI-BW-ROM-boot-stop-reset-v1"):marble_adsp_owned_peak?"RUN-XO-LPI-SCM-BW-ROM-noexec-reset-v1":"RUN-XO-LPI-BW-ROM-noexec-reset-v1");
    if(!marble_icb_run_cycle())return 0;
    marble_adsp_owned_attempted=marble_firmware_reset_required=1;
    print("marble-adsp-owned: phase=install retained_watchdog=1\n");
    if(!marble_pil_install_mode(driver,118784,1) || !marble_pil_header_only || !marble_pil_config())return 0;
    print("marble-adsp-owned: phase=smp2p DSP_off=1\n");
    if(!marble_smp2p_prepare(1))return 0;
    marble_adsp_owned_files=files;
    print("marble-adsp-owned: phase=lpi configured_level=256\n");
    marble_npa_held_action=marble_adsp_owned_dependencies_action;
    int released=marble_npa_client_cycle(2);
    marble_npa_held_action=NULL;
    print("marble-npa-client-state: attempted=%d active=%d dirty=%d\n",(uint64_t)marble_npa_attempted,(uint64_t)marble_npa_active,(uint64_t)marble_npa_dirty);
    print("marble-smp2p-state: attempted=%d dirty=%d DSP_start=%d\n",(uint64_t)marble_smp2p_attempted,(uint64_t)marble_smp2p_dirty,(uint64_t)marble_pil_auth_attempted);
    unsigned memory_clear=!marble_pil_dsp_dirty && !marble_pil_deferred.saved && !marble_pil_ram_device.dirty && !marble_pil_ram_device.installed;
    uint64_t *rpmh=NULL,base=0;
    unsigned xo=marble_rpmh_qualify(&rpmh,&base) && marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,&marble_rpmh_after) &&
        marble_xo_rpmh_vote(&marble_rpmh_after,1);
    MarbleThermalSample t;unsigned cool=marble_adc_channels(1) && marble_adc_last_temperature<38 &&
        marble_thermal_snapshot(marble_thermal_read,NULL,&t) && t.maximum_mc<55000;
    unsigned ok=released && marble_npa_held_limit_ok && !marble_npa_active && memory_clear && xo && cool && marble_pil_auth_attempted==execute;
    print("marble-adsp-owned: complete PASS=%d ROM_stage_restored=%d LPI_released=%d LPI_active=%d XO_retained=%d thermal_ok=%d ICB_RUN_retained=1 SMEM_retained=1 watchdog_armed=1 cold_reset_required=1 original_hardware_restored=0 DSP_start=%d storage_access=0 audio_acceptance=0\n",
        (uint64_t)ok,(uint64_t)memory_clear,(uint64_t)released,(uint64_t)marble_npa_active,(uint64_t)xo,(uint64_t)cool,(uint64_t)marble_pil_auth_attempted);
    return ok;
}
#ifdef XNU_MARBLE_AUDIO_I2C_HOLD
static struct {const void *raw;uint64_t bytes;unsigned called,passed;} marble_audio_i2c_work;
static int marble_audio_i2c_action(void)
{
    if(marble_audio_i2c_work.called)return 0;
    marble_audio_i2c_work.called=1;
    marble_audio_i2c_work.passed=marble_adsp_owned_cycle_inner(marble_audio_i2c_work.raw,marble_audio_i2c_work.bytes,1,510);
    return marble_audio_i2c_work.passed;
}
#endif
static int marble_adsp_owned_cycle(const void *raw,uint64_t bytes,unsigned execute,unsigned qmp)
{
#ifdef XNU_MARBLE_AUDIO_I2C_HOLD
    const uint8_t *driver=NULL,*files=NULL;
    if(execute!=1 || qmp!=510 || marble_audio_i2c_work.raw || !marble_adsp_package_open(raw,bytes,&driver,&files))return 0;
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_firmware_reset_required=1;
    marble_audio_i2c_work.raw=raw;marble_audio_i2c_work.bytes=bytes;
    uint64_t begin=marble_i2c_amp_now(NULL);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    print("aw: begin DSP_start=0 amps_reset=1 frequency_requests=0\n");
    MarbleThermalSample t;unsigned valid=marble_thermal_snapshot(marble_thermal_read,NULL,&t);
    unsigned cool=valid && t.maximum_mc<50000;
    print("marble-i2c-temperature: phase=before valid=%d maximum_mc=%d limit_mc=50000 PASS=%d\n",(uint64_t)valid,(uint64_t)t.maximum_mc,(uint64_t)cool);
#ifdef XNU_MARBLE_AUDIO_OWNER_PREAUTH_TRACE
    if(marble_pil_progress)marble_pil_progress("owner-I2C-inventory-before",cool);
#endif
    if(!cool || !marble_i2c_inventory())return 0;
#ifdef XNU_MARBLE_AUDIO_OWNER_PREAUTH_TRACE
    if(marble_pil_progress)marble_pil_progress("owner-I2C-inventory-after",1);
#endif
    marble_i2c_window_qualified=1;
    int passed=marble_adsp_owned_cycle_inner(raw,bytes,execute,qmp);
    unsigned ok=passed && marble_i2c_window_attempted && marble_i2c_window_passed && !marble_pil_dsp_dirty;
    print("aw: end PASS=%d called=%d window_passed=%d DSP_passed=%d DSP_dirty=%d config_active=%d clock_active=%d dirty=%d elapsed_us=%d amp_commands=%d cold_reset_required=1 audio_acceptance=0\n",
        (uint64_t)ok,(uint64_t)marble_i2c_window_attempted,(uint64_t)marble_i2c_window_passed,(uint64_t)passed,(uint64_t)marble_pil_dsp_dirty,
        (uint64_t)marble_i2c_config_lease.active,(uint64_t)marble_i2c_clock_lease.active,
        (uint64_t)(marble_i2c_config_lease.dirty|marble_i2c_clock_lease.dirty|marble_i2c_amp_dirty),marble_i2c_amp_now(NULL)-begin,(uint64_t)marble_i2c_amp_commands);
    return ok;
#else
    print("ai: begin DSP_start=0 amps_reset=1 frequency_requests=0\n");
    marble_i2c_held_action=marble_audio_i2c_action;
    int passed=marble_i2c_clock_cycle(1,1,1,0);
    marble_i2c_held_action=NULL;
    unsigned ok=passed && marble_audio_i2c_work.called && marble_audio_i2c_work.passed && !marble_pil_dsp_dirty;
    print("ai: end PASS=%d called=%d DSP_passed=%d DSP_dirty=%d config_active=%d clock_active=%d dirty=%d elapsed_us=%d amp_commands=0 cold_reset_required=1 audio_acceptance=0\n",
        (uint64_t)ok,(uint64_t)marble_audio_i2c_work.called,(uint64_t)marble_audio_i2c_work.passed,(uint64_t)marble_pil_dsp_dirty,
        (uint64_t)marble_i2c_config_lease.active,(uint64_t)marble_i2c_clock_lease.active,
        (uint64_t)(marble_i2c_config_lease.dirty|marble_i2c_clock_lease.dirty|marble_i2c_amp_dirty),marble_i2c_amp_now(NULL)-begin);
    return ok;
#endif
#else
    return marble_adsp_owned_cycle_inner(raw,bytes,execute,qmp);
#endif
}
#endif
#endif
