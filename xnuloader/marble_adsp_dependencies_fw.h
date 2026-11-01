#ifndef MARBLE_ADSP_DEPENDENCIES_FW_H
#define MARBLE_ADSP_DEPENDENCIES_FW_H
#ifdef XNU_MARBLE
static unsigned marble_adsp_dependency_attempted;
static int marble_adsp_dependency_action(void)
{
    print("marble-adsp-dependency: phase=xo LPI_clients=2 LPI_level=256\n");
    if(!marble_xo_rpmh_hold_mode(1))return -1;
    print("marble-adsp-dependency: phase=bandwidth baseline=RUN_XO_LPI_held\n");
    return marble_icb_bandwidth_cycle_mode(2)?1:-1;
}
static int marble_adsp_dependency_cycle(void)
{
    if(marble_icb_attempted || marble_rpmh_attempted || marble_npa_attempted || marble_pil_attempted ||
       marble_smp2p_attempted || marble_cap.active || marble_cap.dirty || marble_adsp_dependency_attempted)return 0;
    print("marble-adsp-dependency: begin contract=RUN-XO-LPI-bandwidth-reset-v1 DSP_start=0\n");
    if(!marble_icb_run_cycle())return 0;
    marble_adsp_dependency_attempted=marble_firmware_reset_required=1;
    print("marble-adsp-dependency: phase=lpi configured_level=256\n");
    marble_npa_held_action=marble_adsp_dependency_action;
    int released=marble_npa_client_cycle(2);
    marble_npa_held_action=NULL;
    print("marble-npa-client-state: attempted=%d active=%d dirty=%d\n",(uint64_t)marble_npa_attempted,(uint64_t)marble_npa_active,(uint64_t)marble_npa_dirty);
    uint64_t *rpmh=NULL,base=0;
    unsigned held=marble_rpmh_qualify(&rpmh,&base) && marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,&marble_rpmh_after) &&
        marble_xo_rpmh_vote(&marble_rpmh_after,1);
    MarbleThermalSample t;
    unsigned cool=marble_adc_channels(1) && marble_adc_last_temperature<38 &&
        marble_thermal_snapshot(marble_thermal_read,NULL,&t) && t.maximum_mc<55000;
    unsigned ok=released && marble_npa_held_limit_ok && !marble_npa_active && held && cool && marble_icb_dirty;
    print("marble-adsp-dependency: complete PASS=%d LPI_released=%d LPI_active=%d XO_retained=%d thermal_ok=%d ICB_RUN_retained=1 watchdog_armed=1 cold_reset_required=1 original_hardware_restored=0 DSP_start=0 storage_access=0 audio_acceptance=0\n",
        (uint64_t)ok,(uint64_t)released,(uint64_t)marble_npa_active,(uint64_t)held,(uint64_t)cool);
    return ok;
}
#endif
#endif
