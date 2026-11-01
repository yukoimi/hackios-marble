#ifndef MARBLE_ICB_RUN_FW_H
#define MARBLE_ICB_RUN_FW_H
#ifdef XNU_MARBLE
#include "marble_icb_run_code.h"
static unsigned marble_icb_run_attempted;
static int marble_icb_run_cycle(void)
{
    if(marble_icb_attempted || marble_xo_npa.attempted || !marble_adsp_dependency_inventory() || !marble_icb_endpoint_identity())return 0;
    MarbleIcbNpaCalls calls={0};uint64_t base=0,*rpmh=NULL,rb=0;
    if(!marble_icb_zero_qualify(&calls,&base) || !marble_rpmh_qualify(&rpmh,&rb))return 0;
    uint64_t table[2]={0},state=0,config=0,root_lock=0,standalone_count=0,standalone_array=0;
    if(!marble_rpmh_words(marble_npa_ram_word,base+0xc108,table,2) || table[0]!=0x10000 || table[1]!=base+0x13f4 ||
       !marble_npa_ram_word(base+0xc328,&state) || (uint32_t)state!=2 ||
       !marble_npa_ram_word(base+0xc310,&root_lock) || !root_lock ||
       !marble_npa_ram_word(base+0xc308,&config) ||
       !marble_npa_ram_word(config+0x18,&standalone_count) || (standalone_count>>32) || standalone_count>64 ||
       !marble_npa_ram_word(config+0x20,&standalone_array))return 0;
    for(unsigned i=0;i<sizeof(marble_icb_run_icb_code)/sizeof(marble_icb_run_icb_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_icb_run_icb_code[i].rva,
            marble_icb_run_icb_code[i].bytes,marble_icb_run_icb_code[i].digest))return 0;
    if(rpmh[13]!=rb+0x17e4)return 0;
    for(unsigned i=0;i<sizeof(marble_icb_run_rpmh_code)/sizeof(marble_icb_run_rpmh_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,rb+marble_icb_run_rpmh_code[i].rva,
            marble_icb_run_rpmh_code[i].bytes,marble_icb_run_rpmh_code[i].digest))return 0;
    for(unsigned i=0;i<standalone_count;i++) {
        uint64_t node=0,definition=0,data=0,words[6];
        if(!marble_npa_ram_word(standalone_array+8*i,&node) || !marble_npa_ram_word(node+0x38,&definition) ||
           !marble_npa_ram_word(definition+0x28,&data) || !marble_rpmh_words(marble_npa_ram_word,data,words,6))return 0;
        print("marble-icb-run: standalone index=%d type=%d state=%d native_decides_flush=1\n",(uint64_t)i,(uint64_t)(uint32_t)words[0],(uint64_t)(uint32_t)words[5]);
    }
    if(!marble_icb_hardware_before() || !marble_icb_clients(marble_dep_first.resource,&marble_icb_clients_before) ||
       !marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_before))return 0;
    MarbleThermalSample thermal;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=50000)return 0;
    print("marble-icb-run: binding protocol_offset=8 protocol_rva=13f4 implementation_rva=2aa4 post_init_rva=4088 finish_offset=68 finish_rva=17e4 code_ranges=10\n");
    print("marble-icb-run: before state=2 hardware_nodes=%d standalone_nodes=%d rpmh_resources=%d rpmh_clients=%d direct_state_writes=0\n",
        (uint64_t)marble_icb_hw_count,standalone_count,(uint64_t)marble_rpmh_before.resource_count,(uint64_t)marble_rpmh_before.active.count);
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_icb_attempted=marble_icb_dirty=marble_icb_run_attempted=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    typedef efi_status (EFIAPI *PostInit)(void);
    typedef efi_status (EFIAPI *Finish)(uint32_t);
    if(marble_icb_progress)marble_icb_progress("icb-post-init-before");
    efi_status status=((PostInit)(uintptr_t)table[1])();
    if(marble_icb_progress)marble_icb_progress("icb-post-init-returned");
    unsigned snapshot=0,settled=0,polls=0;
    while((READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq<50000) {
        efi_status finish=((Finish)(uintptr_t)rpmh[13])(2);polls++;
        if(EFI_ERROR(finish))break;
        snapshot=marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_after);
        settled=snapshot;
        for(unsigned i=0;settled && i<marble_rpmh_after.vote_count;i++)if(marble_rpmh_after.votes[i][0]>>32)settled=0;
        for(unsigned i=0;settled && i<marble_rpmh_after.active.count;i++)for(unsigned j=0;j<4;j+=2)
            if(marble_rpmh_after.client_queues[i][j][0]!=(j<2?0x10u:0x18u) || marble_rpmh_after.client_queues[i][j][1] || marble_rpmh_after.client_queues[i][j][2])settled=0;
        if(settled)break;BS->stall(100);
    }
    unsigned clients=marble_icb_clients(marble_dep_first.resource,&marble_icb_clients_after) &&
        marble_icb_clients_same(&marble_icb_clients_before,&marble_icb_clients_after);
    unsigned hardware=marble_icb_hardware_after();
    unsigned ran=marble_npa_ram_word(base+0xc328,&state) && (uint32_t)state==4;
    unsigned cool=marble_adc_channels(1) && marble_adc_last_temperature<38 &&
        marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) && thermal.maximum_mc<55000;
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    unsigned ok=!EFI_ERROR(status) && ran && settled && clients && hardware && cool && us<1000000;
    print("marble-icb-run: complete PASS=%d status=%x state=%d rpmh_snapshot=%d completion_polls=%d queues_drained=%d NPA_clients_unchanged=%d hardware_aggregates_unchanged=%d thermal_ok=%d elapsed_us=%d watchdog_armed=1 cold_reset_required=1 firmware_RUN_retained=1 original_hardware_restored=0 direct_state_writes=0 explicit_bandwidth_requests=0 DSP_start=0 storage_access=0 audio_acceptance=0\n",
        (uint64_t)ok,status,(uint64_t)(uint32_t)state,(uint64_t)snapshot,(uint64_t)polls,(uint64_t)settled,(uint64_t)clients,(uint64_t)hardware,(uint64_t)cool,us);
    return ok;
}
static int marble_icb_run_bandwidth_cycle(void)
{
    print("marble-icb-run-bandwidth: begin contract=native-run-request-reset-v1\n");
    if(!marble_icb_run_cycle() || !marble_icb_run_attempted || !marble_icb_dirty)return 0;
    print("marble-icb-run-bandwidth: phase=bandwidth baseline=after_native_post_init original_firmware_restoration=0\n");
    return marble_icb_bandwidth_cycle_mode(1);
}
static int marble_icb_run_scm_bandwidth_cycle(void)
{
    print("marble-icb-run-bandwidth: begin contract=native-run-request-reset-v1\n");
    if(!marble_icb_run_cycle() || !marble_icb_run_attempted || !marble_icb_dirty)return 0;
    print("marble-icb-run-bandwidth: phase=bandwidth baseline=after_native_post_init original_firmware_restoration=0\n");
    return marble_icb_bandwidth_cycle_peak(1,MARBLE_SCM_PEAK_BYTES);
}
#endif
#endif
