#ifndef MARBLE_RPMH_FW_H
#define MARBLE_RPMH_FW_H
#ifdef XNU_MARBLE
#include "marble_rpmh.h"
#include "marble_rpmh_code.h"
static unsigned marble_rpmh_attempted,marble_rpmh_dirty;
static MarbleRpmhSnapshot marble_rpmh_before,marble_rpmh_after;
static const char marble_rpmh_name[]="marble-zero-command";
static int marble_rpmh_same_resources(const MarbleRpmhSnapshot *a,const MarbleRpmhSnapshot *b)
{
    return a->resource_count==b->resource_count && a->vote_count==b->vote_count &&
        !memcmp(a->global,b->global,sizeof(a->global)) &&
        !memcmp(a->manager,b->manager,sizeof(a->manager)) &&
        !memcmp(a->resources,b->resources,a->resource_count*sizeof(a->resources[0])) &&
        !memcmp(a->vote_addresses,b->vote_addresses,a->vote_count*sizeof(a->vote_addresses[0])) &&
        !memcmp(a->votes,b->votes,a->vote_count*sizeof(a->votes[0]));
}
static int marble_rpmh_same_clients(const MarbleRpmhSnapshot *a,const MarbleRpmhSnapshot *b)
{
    return a->active.count==b->active.count && a->active.address==b->active.address &&
        !memcmp(a->active.header,b->active.header,sizeof(a->active.header)) &&
        !memcmp(a->active.nodes,b->active.nodes,a->active.count*sizeof(uint64_t)) &&
        !memcmp(a->clients,b->clients,a->active.count*sizeof(a->clients[0])) &&
        !memcmp(a->client_queues,b->client_queues,a->active.count*sizeof(a->client_queues[0]));
}
static int marble_rpmh_qualify(uint64_t **out,uint64_t *base_out)
{
    static const efi_guid guid={0xe1d0b573,0xf29f,0x495d,{0x93,0x52,0xca,0x0d,0xd4,0x2a,0x0a,0x74}};
    static const unsigned rvas[]={0x15d4,0x15ec,0x1628,0x1664,0x16a8,0x16ec,0x1730,0x1748,
        0x1760,0x1778,0x17b4,0x17cc,0x17e4,0x17fc,0x1814,0x1858,0x1870,0x1888,0x1890,0x18cc,0x18e4,0x14e4,0x18fc,0x1904};
    uint64_t *table=NULL,v=0;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table)return 0;
    uint64_t address=(uint64_t)(uintptr_t)table;
    if(address<0x8000e0f8 || !marble_adc_ram_word(address,&v) || v!=0x10001)return 0;
    uint64_t base=address-0xe0f8;
    for(unsigned i=0;i<24;i++)if(!marble_adc_ram_word(address+8*(i+1),&v) || v!=base+rvas[i])return 0;
    unsigned words=0;
    for(unsigned i=0;i<sizeof(marble_rpmh_code)/sizeof(marble_rpmh_code[0]);i++)
        for(unsigned j=0;j<marble_rpmh_code[i].count;j++) {
            if(!marble_npa_ram_word(base+marble_rpmh_code[i].rva+4*j,&v) ||
               (uint32_t)v!=marble_rpmh_code[i].words[j])return 0;
            words++;
        }
    uint64_t config[5],drv[6];
    if(!marble_adc_ram_word(base+0xc2b8,&v) || v!=base+0xc290 ||
       !marble_rpmh_words(marble_adc_ram_word,v,config,5) || config[0]!=1 ||
       config[1] || config[2] || config[3] || config[4]!=base+0xe218 ||
       !marble_rpmh_words(marble_adc_ram_word,config[4],drv,6) ||
       drv[0]!=0x200000002ULL || drv[1] || drv[2] ||
       drv[3]!=0x1000000007ULL || drv[4]!=1 || drv[5]!=base+0xe210)return 0;
    print("marble-rpmh-native: qualified exact=1 slots=24 code_words=%d drv=2 hardware_drv=2 commands=16 Init_calls=0\n",(uint64_t)words);
    *out=table;*base_out=base;return 1;
}
static int marble_rpmh_zero_cycle(void)
{
    if(marble_rpmh_attempted || marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted ||
       marble_qmp_native.attempted || marble_glink_observer.attempted || marble_cap.active || marble_cap.dirty)return 0;
    uint64_t *table=NULL,base=0;
    if(!marble_rpmh_qualify(&table,&base))return 0;
    MarbleRpmhSnapshot *before=&marble_rpmh_before,*after=&marble_rpmh_after;
    if(!marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,before)) {
        print("marble-rpmh-native: snapshot_refused stage=%d index=%d global=%x,%x,%x,%x manager=%x,%x active_header=%x,%x,%x pool_header=%x,%x,%x\n",
            (uint64_t)marble_rpmh_stage,(uint64_t)marble_rpmh_index,
            before->global[0],before->global[1],before->global[2],before->global[3],
            before->manager[0],before->manager[1],before->active.header[0],before->active.header[1],before->active.header[2],
            before->pool.header[0],before->pool.header[1],before->pool.header[2]);
        return 0;
    }
    BS->stall(100);
    unsigned stable=marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after) &&
        marble_rpmh_same_resources(before,after) && marble_rpmh_same_clients(before,after) &&
        !memcmp(&before->pool,&after->pool,sizeof(before->pool));
    print("marble-rpmh-native: inventory stable=%d resources=%d votes=%d active=%d pool=%d pending_issue_calls=0\n",
        (uint64_t)stable,(uint64_t)before->resource_count,(uint64_t)before->vote_count,
        (uint64_t)before->active.count,(uint64_t)before->pool.count);
    /* This first lifecycle qualifies a newly allocated regular client, not a
     * pooled stale callback/explicit client. Refuse a nonempty native pool. */
    if(!stable || before->pool.count || before->active.count>=MARBLE_RPMH_CLIENTS)return 0;
    MarbleThermalSample sample;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&sample) || sample.maximum_mc>=50000)return 0;
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_rpmh_attempted=1;marble_rpmh_dirty=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    typedef efi_status (EFIAPI *Valid)(uint32_t,uint8_t *);
    typedef efi_status (EFIAPI *Num)(uint32_t,uint32_t *);
    typedef efi_status (EFIAPI *Find)(uint32_t,void **);
    typedef efi_status (EFIAPI *Create)(uint32_t,const char *,void **);
    typedef efi_status (EFIAPI *Delete)(void *);
    uint8_t valid=0;uint32_t commands=0;void *found=NULL,*handle=NULL;
    efi_status vs=((Valid)(uintptr_t)table[2])(2,&valid);
    if(EFI_ERROR(vs) || valid!=1)return 0; /* Num asserts on an invalid DRV. */
    efi_status ns=((Num)(uintptr_t)table[3])(2,&commands);
    if(EFI_ERROR(ns) || commands!=16)return 0;
    efi_status fs=((Find)(uintptr_t)table[10])(0x300b0,&found);
    uint64_t expected=0;
    for(unsigned i=0;i<before->resource_count;i++)if((uint32_t)before->resources[i][0]==0x300b0)
        expected=before->global[1]+24*i;
    unsigned lookup=!EFI_ERROR(fs) && (uint64_t)(uintptr_t)found==expected;
    print("marble-rpmh-native: queries valid_status=%x valid=%d num_status=%x commands=%d find_status=%x lookup_match=%d xo_cached=%d issue_calls=0\n",
        vs,(uint64_t)valid,ns,(uint64_t)commands,fs,(uint64_t)lookup,(uint64_t)(expected!=0));
    if(!lookup)return 0;
    efi_status created=((Create)(uintptr_t)table[6])(2,marble_rpmh_name,&handle);
    /* Do not delete an unqualified handle: native Delete fatally asserts on
     * nonmembers. Keep the watchdog if ownership or the empty queues differ. */
    unsigned captured=!EFI_ERROR(created) && handle &&
        marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after);
    unsigned appended=captured && after->active.count==before->active.count+1 && !after->pool.count &&
        after->active.nodes[before->active.count]==(uint64_t)(uintptr_t)handle;
    for(unsigned i=0;appended && i<before->active.count;i++)if(after->active.nodes[i]!=before->active.nodes[i])appended=0;
    unsigned empty=appended && marble_rpmh_client_empty(marble_npa_ram_word,(uint64_t)(uintptr_t)handle,(uint64_t)(uintptr_t)marble_rpmh_name);
    unsigned unchanged=captured && marble_rpmh_same_resources(before,after);
    print("marble-rpmh-native: created status=%x captured=%d appended=%d empty=%d resources_unchanged=%d active=%d pool=%d\n",
        created,(uint64_t)captured,(uint64_t)appended,(uint64_t)empty,(uint64_t)unchanged,(uint64_t)after->active.count,(uint64_t)after->pool.count);
    if(!empty || !unchanged)return 0;
    efi_status deleted=((Delete)(uintptr_t)table[9])(handle);
    unsigned snap=marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after);
    unsigned restored=snap && marble_rpmh_same_clients(before,after);
    unsigned resources=snap && marble_rpmh_same_resources(before,after);
    unsigned pooled=snap && after->pool.count==1 && after->pool.nodes[0]==(uint64_t)(uintptr_t)handle;
    /* No dereference through handle after deletion: inspect driver-owned pool
     * membership only. Native allocation retention is not heap restoration. */
    unsigned ok=!EFI_ERROR(deleted) && restored && resources && pooled;
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    ok=ok && us<1000000;
    print("marble-rpmh-native: released status=%x snapshot=%d clients_restored=%d resources_restored=%d native_pool_retained=%d active=%d pool=%d elapsed_us=%d\n",
        deleted,(uint64_t)snap,(uint64_t)restored,(uint64_t)resources,(uint64_t)pooled,
        (uint64_t)after->active.count,(uint64_t)after->pool.count,us);
    efi_status cleared=~0ULL;
    if(ok) {cleared=BS->set_watchdog_timer(0,0,0,NULL);ok=!EFI_ERROR(cleared);}
    if(ok)marble_rpmh_dirty=0;
    print("marble-rpmh-native: complete PASS=%d attempted=%d dirty=%d create_calls=1 delete_calls=1 query_calls=3 watchdog_clear=%x issue_calls=0 votes=0 raw_writes=0 NPA_calls=0 QMP_calls=0 PAS_calls=0 DSP_start=0 storage_access=0 lease_accepted=0\n",
        (uint64_t)ok,(uint64_t)marble_rpmh_attempted,(uint64_t)marble_rpmh_dirty,cleared);
    return ok;
}
#endif
#endif
