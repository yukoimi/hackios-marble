#ifndef MARBLE_ICB_LEASE_FW_H
#define MARBLE_ICB_LEASE_FW_H
#ifdef XNU_MARBLE
#include "marble_icb_lease.h"
#include "marble_code_digest.h"
#include "marble_icb_lease_code.h"
static unsigned marble_icb_attempted,marble_icb_dirty;
static void (*marble_icb_progress)(const char *);
#define MARBLE_ICB_TRACE(s) do {if(marble_icb_progress)marble_icb_progress(s);} while(0)
static MarbleIcbRequestList marble_icb_lists_before[8],marble_icb_lists_after[8];
typedef struct {uint64_t head,nodes[64],words[64][19];unsigned count;} MarbleIcbClients;
static MarbleIcbClients marble_icb_clients_before,marble_icb_clients_after;
static const char marble_icb_client_name[]="marble-crypto-zero";
static const uint32_t marble_icb_pair[2]={23,0};
typedef efi_status (EFIAPI *MarbleIcbCreate)(const char *,const char *,uint32_t,uint32_t,void *,void **);
typedef efi_status (EFIAPI *MarbleIcbDestroy)(void *);
typedef struct {MarbleIcbCreate create;MarbleIcbDestroy destroy;} MarbleIcbNpaCalls;
static int marble_icb_clients(uint64_t resource,MarbleIcbClients *s)
{
    s->count=0;
    if(!marble_npa_ram_word(resource+0x18,&s->head))return 0;
    uint64_t at=s->head,previous=0;
    while(at && s->count<64) {
        for(unsigned j=0;j<s->count;j++)if(s->nodes[j]==at)return 0;
        unsigned i=s->count++;s->nodes[i]=at;
        if(!marble_rpmh_words(marble_npa_ram_word,at,s->words[i],19) ||
           s->words[i][0]!=previous || s->words[i][4]!=resource)return 0;
        previous=at;at=s->words[i][1];
    }
    return !at;
}
static int marble_icb_clients_same(const MarbleIcbClients *a,const MarbleIcbClients *b)
{
    return a->head==b->head && a->count==b->count &&
        !memcmp(a->nodes,b->nodes,a->count*sizeof(uint64_t)) &&
        !memcmp(a->words,b->words,a->count*sizeof(a->words[0]));
}
static int marble_icb_endpoint_identity(void)
{
    static const char *names[]={"qxm_crypto","qns_a2noc_snoc","qnm_aggre2_noc","qns_gemnoc_sf",
        "qnm_snoc_sf","qns_llcc","llcc_mc","ebi"};
    static const unsigned ids[]={23,143,112,104,76,243,180,0};
    static const unsigned widths[]={8,16,16,16,16,16,4,4};
    for(unsigned i=0;i<8;i++) {
        MarbleIcbEndpointMetadata *e=&marble_dep_first.hop[i/2][i%2];
        if(!str_eq(e->name,names[i]) || (uint32_t)e->head[1]!=ids[i] || (uint32_t)e->head[3]!=widths[i])return 0;
    }
    return 1;
}
static int marble_icb_zero_qualify(MarbleIcbNpaCalls *calls,uint64_t *ib)
{
    if(!marble_npa_info())return 0;
    static const efi_guid ng={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    static const efi_guid ig={0x5824f9de,0x17d2,0x4b1f,{0xa5,0xfe,0x5e,0xb7,0x7f,0xa5,0x30,0x93}};
    uint64_t *npa=NULL,*icb=NULL,word=0;
    if(EFI_ERROR(BS->locate_protocol(&ng,NULL,(void **)&npa)) || !npa ||
       EFI_ERROR(BS->locate_protocol(&ig,NULL,(void **)&icb)) || !icb)return 0;
    uint64_t nb=(uint64_t)(uintptr_t)npa-0x120b8,base=(uint64_t)(uintptr_t)icb-0xc108;
    if(!marble_adc_ram_word((uint64_t)(uintptr_t)icb,&word) || word!=0x10000 ||
       !marble_adc_ram_word((uint64_t)(uintptr_t)icb+8,&word) || word!=base+0x13f4)return 0;
    for(unsigned i=0;i<sizeof(marble_icb_lease_npa_code)/sizeof(marble_icb_lease_npa_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,nb+marble_icb_lease_npa_code[i].rva,
            marble_icb_lease_npa_code[i].bytes,marble_icb_lease_npa_code[i].digest))return 0;
    for(unsigned i=0;i<sizeof(marble_icb_lease_icb_code)/sizeof(marble_icb_lease_icb_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_icb_lease_icb_code[i].rva,
            marble_icb_lease_icb_code[i].bytes,marble_icb_lease_icb_code[i].digest))return 0;
    uint64_t *p=marble_dep_first.plugin_words;
    if(marble_dep_first.plugin!=base+0xc158 || p[0]!=base+0x2bac || p[1]!=0x1400 || p[2] ||
       p[3]!=base+0x2e00 || p[4]!=base+0x2ee8 || p[5])return 0;
    MarbleNpaClientProtocol protocol;uint64_t create=0,destroy=0;
    if(!marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)npa,(uint64_t *)(void *)&protocol,16) ||
       !marble_npa_client_binding(&protocol,nb,&create,&destroy))return 0;
    calls->create=(MarbleIcbCreate)(uintptr_t)create;calls->destroy=(MarbleIcbDestroy)(uintptr_t)destroy;
    print("marble-icb-zero: binding create_offset=10 create_rva=1448 destroy_offset=78 destroy_rva=15e4 explicit=1\n");
    *ib=base;return 1;
}
static int marble_icb_zero_cycle(void)
{
    MARBLE_ICB_TRACE("metadata-before");
    if(marble_icb_attempted || !marble_adsp_dependency_inventory() || !marble_icb_endpoint_identity())return 0;
    MARBLE_ICB_TRACE("metadata-after");
    MarbleIcbNpaCalls calls={0};uint64_t base=0,*rpmh=NULL,rb=0;
    if(!marble_icb_zero_qualify(&calls,&base) || !marble_rpmh_qualify(&rpmh,&rb))return 0;
    MARBLE_ICB_TRACE("abi-after");
    (void)rpmh;
    uint64_t resource=marble_dep_first.resource,resource_before[16],resource_after[16];
    MarbleIcbNode *route=NULL;
    for(unsigned i=0;i<marble_icb_first.count;i++)if(marble_icb_first.nodes[i].words[0]==(23ULL<<32))route=&marble_icb_first.nodes[i];
    if(!route || !marble_icb_clients(resource,&marble_icb_clients_before) || marble_icb_clients_before.count>=64 ||
       !marble_rpmh_words(marble_npa_ram_word,resource,resource_before,16) ||
       !marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_before))return 0;
    MARBLE_ICB_TRACE("clients-cache-after");
    for(unsigned i=0;i<8;i++) {
        for(unsigned j=0;j<i;j++)if(route->pairs[i]==route->pairs[j])return 0;
        if(!marble_icb_requests(marble_npa_ram_word,route->pairs[i],!(i%2),&marble_icb_lists_before[i]) ||
           marble_icb_lists_before[i].count>=64)return 0;
    }
    MARBLE_ICB_TRACE("node-lists-after");
    MarbleThermalSample sample;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&sample) || sample.maximum_mc>=50000)return 0;
    MARBLE_ICB_TRACE("thermal-after");
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_icb_attempted=1;marble_icb_dirty=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    print("marble-icb-zero: qualified exact_abi=1 route_names=8 hops=4 vector_type=400 pair_bytes=8 explicit_requests=0 watchdog_seconds=30\n");
    void *handle=NULL;
    MARBLE_ICB_TRACE("create-before");
    efi_status created=calls.create("/icb/arbiter",marble_icb_client_name,0x400,8,(void *)marble_icb_pair,&handle);
    MARBLE_ICB_TRACE("create-returned");
    uint64_t client[19],vector[5],r[2],requests[4],query[4];
    unsigned owned=!EFI_ERROR(created) && handle &&
        marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)handle,client,19) &&
        !client[0] && client[1]==marble_icb_clients_before.head && client[2]==(uint64_t)(uintptr_t)marble_icb_client_name &&
        client[4]==resource && (uint32_t)client[6]==0x400 && !(uint32_t)client[7] && !(uint32_t)client[10] &&
        marble_icb_clients(resource,&marble_icb_clients_after) &&
        marble_icb_clients_after.count==marble_icb_clients_before.count+1 && marble_icb_clients_after.head==(uint64_t)(uintptr_t)handle;
    unsigned empty=owned && marble_rpmh_words(marble_npa_ram_word,client[5],vector,5) && vector[0]==1 && !vector[1] &&
        vector[2]==client[5]+0x28 && vector[3]==client[5]+0x38 && vector[4]==client[5]+0x58 &&
        marble_rpmh_words(marble_npa_ram_word,vector[2],r,2) && r[0]==23 && r[1]==route->words[1] &&
        marble_rpmh_words(marble_npa_ram_word,vector[3],requests,4) && !requests[0] && !requests[1] && requests[2]==(uint64_t)(uintptr_t)handle && !requests[3] &&
        marble_rpmh_words(marble_npa_ram_word,vector[4],query,4) && !query[0] && !query[1] && !query[2] && !query[3];
    unsigned lists=empty;
    for(unsigned i=0;lists && i<8;i++)lists=marble_icb_requests(marble_npa_ram_word,route->pairs[i],!(i%2),&marble_icb_lists_after[i]) &&
        marble_icb_requests_same(&marble_icb_lists_before[i],&marble_icb_lists_after[i],1,vector[3]);
    print("marble-icb-zero: created status=%x owned=%d empty=%d lists_appended=%d clients_before=%d clients_during=%d\n",
        created,(uint64_t)owned,(uint64_t)empty,(uint64_t)lists,(uint64_t)marble_icb_clients_before.count,(uint64_t)marble_icb_clients_after.count);
    if(!lists)return 0; /* Retain watchdog; never destroy an unqualified handle. */
    MARBLE_ICB_TRACE("destroy-before");
    efi_status destroyed=calls.destroy(handle);
    MARBLE_ICB_TRACE("destroy-returned");
    unsigned restored=1;
    for(unsigned i=0;i<8;i++) {
        unsigned read=marble_icb_requests(marble_npa_ram_word,route->pairs[i],!(i%2),&marble_icb_lists_after[i]);
        unsigned same=read && marble_icb_requests_same(&marble_icb_lists_before[i],&marble_icb_lists_after[i],0,0);
        print("marble-icb-zero: list index=%d restored=%d before=%d after=%d old_capacity=%d native_capacity=%d\n",
            (uint64_t)i,(uint64_t)same,(uint64_t)marble_icb_lists_before[i].count,(uint64_t)marble_icb_lists_after[i].count,
            (uint64_t)marble_icb_lists_before[i].capacity,(uint64_t)marble_icb_lists_after[i].capacity);
        if(!same)restored=0;
    }
    unsigned clients=marble_icb_clients(resource,&marble_icb_clients_after) && marble_icb_clients_same(&marble_icb_clients_before,&marble_icb_clients_after);
    unsigned state=marble_rpmh_words(marble_npa_ram_word,resource,resource_after,16) && !memcmp(resource_before,resource_after,sizeof(resource_before));
    unsigned cache=marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_after) &&
        marble_rpmh_same_resources(&marble_rpmh_before,&marble_rpmh_after) && marble_rpmh_same_clients(&marble_rpmh_before,&marble_rpmh_after);
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    unsigned ok=!EFI_ERROR(destroyed) && restored && clients && state && cache && us<1000000;
    efi_status cleared=~0ULL;
    if(ok){cleared=BS->set_watchdog_timer(0,0,0,NULL);ok=!EFI_ERROR(cleared);}
    if(ok)marble_icb_dirty=0;
    print("marble-icb-zero: complete PASS=%d destroy_status=%x attempted=%d dirty=%d lists_restored=%d clients_restored=%d resource_restored=%d RPMh_cache_restored=%d elapsed_us=%d watchdog_clear=%x create_calls=1 destroy_calls=1 explicit_requests=0 DSP_start=0 storage_access=0 bandwidth_lease=0\n",
        (uint64_t)ok,destroyed,(uint64_t)marble_icb_attempted,(uint64_t)marble_icb_dirty,(uint64_t)restored,(uint64_t)clients,(uint64_t)state,(uint64_t)cache,us,cleared);
    return ok;
}
#undef MARBLE_ICB_TRACE
#endif
#endif
