#ifndef MARBLE_XO_RPMH_FW_H
#define MARBLE_XO_RPMH_FW_H
#ifdef XNU_MARBLE
#include "marble_xo_rpmh.h"
#include "marble_xo_rpmh_code.h"
static void (*marble_xo_rpmh_progress)(const char *);
static unsigned marble_xo_rpmh_attempted;
static const char marble_xo_rpmh_name[]="marble-xo-hold-reset";
static int marble_xo_rpmh_hold_mode(unsigned run_owned)
{
    if(run_owned>1 || marble_rpmh_attempted || ((!run_owned) && (marble_icb_attempted || marble_npa_attempted || marble_pil_attempted || marble_smp2p_attempted)) || marble_xo_npa.attempted ||
       marble_pil_dsp_dirty || marble_pil_auth_attempted || (run_owned && marble_pil_attempted && !marble_pil_header_only) ||
       marble_qmp_native.attempted || marble_glink_observer.attempted ||
       marble_cap.active || marble_cap.dirty)return 0;
    if(run_owned) {
        static const efi_guid ig={0x5824f9de,0x17d2,0x4b1f,{0xa5,0xfe,0x5e,0xb7,0x7f,0xa5,0x30,0x93}};
        uint64_t *icb=NULL,state=0;
        if(!marble_icb_attempted || !marble_icb_dirty || !marble_npa_attempted || !marble_npa_dirty || marble_npa_active!=2 ||
           EFI_ERROR(BS->locate_protocol(&ig,NULL,(void **)&icb)) || !icb || icb[0]!=0x10000 ||
           !marble_npa_ram_word((uint64_t)(uintptr_t)icb-0xc108+0xc328,&state) || (uint32_t)state!=4)return 0;
    }
    uint64_t *table=NULL,base=0;
    MarbleXoRpmhProtocol p;
    if(!marble_rpmh_qualify(&table,&base) ||
       !marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)table,(uint64_t *)(void *)&p,20) ||
       !marble_xo_rpmh_binding(&p,base))return 0;
    for(unsigned i=0;i<sizeof(marble_xo_rpmh_code)/sizeof(marble_xo_rpmh_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_xo_rpmh_code[i].rva,
            marble_xo_rpmh_code[i].bytes,marble_xo_rpmh_code[i].digest))return 0;
    MarbleRpmhSnapshot *before=&marble_rpmh_before,*after=&marble_rpmh_after;
    if(!marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,before) || before->pool.count ||
       before->active.count>=MARBLE_RPMH_CLIENTS)return 0;
    for(unsigned i=0;i<before->resource_count;i++)if((uint32_t)before->resources[i][0]==0x300b0)return 0;
    BS->stall(100);
    if(!marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after) ||
       !marble_rpmh_same_resources(before,after) || !marble_rpmh_same_clients(before,after))return 0;
    MarbleThermalSample sample;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&sample) || sample.maximum_mc>=50000)return 0;
    print("marble-xo-rpmh: binding create_offset=30 create_rva=16ec issue_offset=98 issue_rva=1890 churn_offset=58 churn_rva=17b4 code_ranges=%d\n",
        (uint64_t)(sizeof(marble_xo_rpmh_code)/sizeof(marble_xo_rpmh_code[0])));
    print("marble-xo-rpmh: inventory stable=1 xo_cached=0 original_resources=%d original_votes=%d original_clients=%d original_hardware_vote_known=0\n",
        (uint64_t)before->resource_count,(uint64_t)before->vote_count,(uint64_t)before->active.count);
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_rpmh_attempted=1;marble_rpmh_dirty=1;marble_xo_rpmh_attempted=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    typedef efi_status (EFIAPI *Create)(uint32_t,const char *,void **);
    typedef efi_status (EFIAPI *Issue)(void *,uint32_t,uint8_t,uint32_t,uint32_t,uint32_t *);
    typedef efi_status (EFIAPI *Churn)(void *,uint32_t);
    void *handle=NULL;
    efi_status created=((Create)(uintptr_t)p.create)(2,marble_xo_rpmh_name,&handle);
    unsigned valid=!EFI_ERROR(created) && handle &&
        marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after) &&
        after->active.count==before->active.count+1 && after->active.nodes[before->active.count]==(uint64_t)(uintptr_t)handle &&
        marble_rpmh_client_empty(marble_npa_ram_word,(uint64_t)(uintptr_t)handle,(uint64_t)(uintptr_t)marble_xo_rpmh_name) &&
        marble_rpmh_same_resources(before,after);
    print("marble-xo-rpmh: client status=%x owned_empty=%d\n",created,(uint64_t)valid);
    if(!valid)return 0;
    /* No zero vote: firmware's pre-cache hardware state is unknown. The
     * initial test raises CXO to its documented ON level and retains every
     * allocation/vote until mandatory reset. No sleep transition is entered. */
    static const uint32_t sets[]={1,2,0};uint32_t last=0;
    for(unsigned i=0;i<3;i++) {
        uint32_t request=~0u;
        if(marble_xo_rpmh_progress)marble_xo_rpmh_progress(i==0?"xo-sleep-before":i==1?"xo-wake-before":"xo-active-before");
        efi_status status=((Issue)(uintptr_t)p.issue)(handle,sets[i],sets[i]==0,0x300b0,3,&request);
        if(marble_xo_rpmh_progress)marble_xo_rpmh_progress("xo-issue-returned");
        print("marble-xo-rpmh: issue index=%d set=%d status=%x data=3 request=%d\n",(uint64_t)i,(uint64_t)sets[i],status,(uint64_t)request);
        if(EFI_ERROR(status) || (sets[i]!=0 && request) || (sets[i]==0 && (!request || request==~0u)))return 0;
        last=request;
    }
    if(marble_xo_rpmh_progress)marble_xo_rpmh_progress("xo-churn-before");
    efi_status churned=((Churn)(uintptr_t)p.churn_all)(handle,last);
    if(marble_xo_rpmh_progress)marble_xo_rpmh_progress("xo-churn-returned");
    unsigned snap=marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after);
    unsigned originals=snap && marble_xo_rpmh_originals(before,after);
    unsigned vote=snap && marble_xo_rpmh_vote(after,1),drained=snap;
    if(snap) {
        unsigned n=before->active.count;
        drained=after->active.nodes[n]==(uint64_t)(uintptr_t)handle &&
            (uint32_t)after->clients[n][6]==last && (uint32_t)(after->clients[n][6]>>32)==UINT32_MAX &&
            !after->clients[n][7] && !after->clients[n][8] && !after->clients[n][9];
        for(unsigned j=0;drained && j<4;j++) {
            if(j==1 || j==3)continue; /* Free-node queues retain allocations. */
            uint64_t *q=after->client_queues[n][j];
            if(q[0]!=(j<2?0x10u:0x18u) || q[1] || q[2])drained=0;
        }
    }
    BS->stall(10000);
    unsigned thermal=marble_adc_channels(1) && marble_adc_last_temperature<38 &&
        marble_thermal_snapshot(marble_thermal_read,NULL,&sample) && sample.maximum_mc<55000;
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    unsigned ok=!EFI_ERROR(churned) && originals && vote && drained && thermal && us<1000000;
    /* Keep watchdog armed even on success: there is deliberately no local
     * release/restoration claim. Host must cold-reset this isolated image. */
    print("marble-xo-rpmh: held PASS=%d churn_status=%x original_owners_unchanged=%d xo_vote_acknowledged=%d queues_drained=%d thermal_ok=%d elapsed_us=%d issue_calls=3 zero_vote_calls=0 raw_writes=0 retained_client=1 dirty=1 watchdog_armed=1 cold_reset_required=1 physical_release_verified=0 original_hardware_restored=0 DSP_start=0 storage_access=0 audio_acceptance=0\n",
        (uint64_t)ok,churned,(uint64_t)originals,(uint64_t)vote,(uint64_t)drained,(uint64_t)thermal,us);
    return ok;
}
static int marble_xo_rpmh_hold(void){return marble_xo_rpmh_hold_mode(0);}
#endif
#endif
