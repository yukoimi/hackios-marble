#ifndef MARBLE_XO_NPA_FW_H
#define MARBLE_XO_NPA_FW_H
#ifdef XNU_MARBLE
#include "marble_xo_npa.h"
#include "marble_xo_npa_code.h"
typedef uint32_t (EFIAPI *MarbleXoUpdate)(void *,void *);
typedef uint32_t (EFIAPI *MarbleXoDriver)(void *,void *,uint32_t);
typedef struct {
    MarbleXoUpdate update;uint32_t supported,pad;
    void *create,*destroy,*create_ex,*cancel;
} MarbleXoPlugin;
typedef struct {
    const char *name,*units;uint32_t max,pad;
    const MarbleXoPlugin *plugin;uint32_t attributes,pad2;
    void *data,*query,*link_query,*handle;
} MarbleXoResourceDefinition;
typedef struct {
    const char *name;MarbleXoDriver driver;uint32_t attributes,pad;
    void *data;uint32_t dependencies,pad2;void *dependency;
    uint32_t resources,pad3;MarbleXoResourceDefinition *resource;
} MarbleXoNodeDefinition;
_Static_assert(sizeof(MarbleXoPlugin)==48,"native NPA plugin size");
_Static_assert(sizeof(MarbleXoResourceDefinition)==72,"native NPA resource definition size");
_Static_assert(offsetof(MarbleXoResourceDefinition,handle)==0x40,"native NPA resource handle");
_Static_assert(sizeof(MarbleXoNodeDefinition)==64,"native NPA node definition size");
_Static_assert(offsetof(MarbleXoNodeDefinition,resource)==0x38,"native NPA resource definition pointer");
static struct {
    unsigned attempted,dirty,failed,registered,driver_calls,update_calls;
    uint64_t resource;uint32_t active,required,suppressible;
} marble_xo_npa;
static MarbleXoResourceDefinition marble_xo_npa_definition;
static MarbleXoNodeDefinition marble_xo_npa_node;
static void (*marble_xo_npa_progress)(const char *);
static uint32_t EFIAPI marble_xo_npa_update(void *resource,void *current)
{
    uint64_t r=(uint64_t)(uintptr_t)resource,at=0,definition=0,seen[16],previous=0;
    MarbleXoRequest requests[16];unsigned count=0,found=0;
    uint32_t pending=0;uint64_t pending_at=0;
    marble_xo_npa.update_calls++;
    if(!marble_npa_ram_word(r,&definition) || definition!=(uint64_t)(uintptr_t)&marble_xo_npa_definition ||
       !marble_npa_ram_word(r+0x18,&at))goto fail;
    while(at && count<16) {
        for(unsigned i=0;i<count;i++)if(seen[i]==at)goto fail;
        uint64_t words[14],value=0;
        if(!marble_rpmh_words(marble_npa_ram_word,at,words,14) || words[0]!=previous || words[4]!=r ||
           (uint32_t)words[13]>1)goto fail;
        unsigned index=(uint32_t)words[13];
        if(at==(uint64_t)(uintptr_t)current) {
            found++;index^=1;pending_at=at+0x38+24*index;
        }
        if(!marble_npa_ram_word(at+0x38+24*index,&value))goto fail;
        if(at==(uint64_t)(uintptr_t)current)pending=(uint32_t)value;
        requests[count]=(MarbleXoRequest){(uint32_t)words[6],(uint32_t)value};
        seen[count++]=at;previous=at;at=words[1];
    }
    uint32_t required=0,suppressible=0;
    if(at || found!=1 || !marble_xo_aggregate(requests,count,1,&required,&suppressible))goto fail;
    /* Native plugin-owned scalar state; these are checked ordinary RAM,
     * never peripheral addresses. Match NPA_REMAP_MAX_REQUEST semantics. */
    if(pending==UINT32_MAX)*(volatile uint32_t *)(uintptr_t)pending_at=1;
    *(volatile uint32_t *)(uintptr_t)(r+0x40)=required;
    *(volatile uint32_t *)(uintptr_t)(r+0x44)=suppressible;
    return required>suppressible?required:suppressible;
fail:
    marble_xo_npa.failed=1;return marble_xo_npa.active;
}
static uint32_t EFIAPI marble_xo_npa_driver(void *resource,void *client,uint32_t state)
{
    (void)client;
    uint64_t r=(uint64_t)(uintptr_t)resource,definition=0,aggregates=0;
    marble_xo_npa.driver_calls++;
    if(!marble_npa_ram_word(r,&definition) || definition!=(uint64_t)(uintptr_t)&marble_xo_npa_definition ||
       !marble_npa_ram_word(r+0x40,&aggregates) || state>1 ||
       (marble_xo_npa.resource && marble_xo_npa.resource!=r)) {marble_xo_npa.failed=1;return marble_xo_npa.active;}
    marble_xo_npa.resource=r;marble_xo_npa.active=state;
    marble_xo_npa.required=(uint32_t)aggregates;marble_xo_npa.suppressible=(uint32_t)(aggregates>>32);
    return state; /* Software-only provider qualification; zero RPMh calls. */
}
static const MarbleXoPlugin marble_xo_npa_plugin={marble_xo_npa_update,0x844,0,NULL,NULL,NULL,NULL};
static const char marble_xo_npa_test_name[]="/marble/clock/xo-test";
static int marble_xo_npa_test(void)
{
    if(marble_xo_npa.attempted || marble_icb_attempted || marble_rpmh_attempted || marble_npa_attempted ||
       marble_pil_attempted || marble_smp2p_attempted || marble_qmp_native.attempted || marble_glink_observer.attempted ||
       marble_cap.active || marble_cap.dirty || !marble_npa_info())return 0;
    static const efi_guid guid={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    uint64_t *table=NULL,*rpmh=NULL,rb=0;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table || !marble_rpmh_qualify(&rpmh,&rb))return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x120b8,create=0,destroy=0;
    MarbleNpaClientProtocol clients;MarbleNpaProviderProtocol provider;
    if(!marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)table,(uint64_t *)(void *)&clients,16) ||
       !marble_npa_client_binding(&clients,base,&create,&destroy) ||
       !marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)table,(uint64_t *)(void *)&provider,6) ||
       provider.define_node!=base+0x148c || provider.scalar!=base+0x14a4 ||
       !marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_before))return 0;
    for(unsigned i=0;i<4;i++)if(!marble_code_digest(marble_npa_ram_word,base+marble_xo_npa_code[i].rva,
        marble_xo_npa_code[i].bytes,marble_xo_npa_code[i].digest))return 0;
    print("marble-xo-npa: binding define_offset=20 define_rva=148c scalar_offset=28 scalar_rva=14a4 destroy_offset=78 code_ranges=4\n");
    /* Reject an existing name before DefineNode: duplicate definitions are
     * fatal in native NPA. No replacement of /pmic/client/xo in this test. */
    uint64_t at=0,seen[96];unsigned count=0;
    if(!marble_adc_ram_word(base+0x129e8,&at))return 0;
    while(at && count<96) {
        for(unsigned i=0;i<count;i++)if(seen[i]==at)return 0;
        seen[count++]=at;uint64_t name=0,next=0;char text[97];
        if(!marble_npa_ram_word(at,&next) || !marble_npa_ram_word(at+0x10,&name) ||
           !marble_proxy_ram_string(name,text) || str_eq(text,marble_xo_npa_test_name))return 0;
        at=next;
    }
    if(at)return 0;
    MarbleThermalSample sample;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&sample) || sample.maximum_mc>=50000)return 0;
    marble_xo_npa_definition=(MarbleXoResourceDefinition){marble_xo_npa_test_name,"state",1,0,&marble_xo_npa_plugin,0x81,0,NULL,NULL,NULL,NULL};
    marble_xo_npa_node=(MarbleXoNodeDefinition){marble_xo_npa_test_name,marble_xo_npa_driver,0,0,NULL,0,0,NULL,1,0,&marble_xo_npa_definition};
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_xo_npa.attempted=1;marble_xo_npa.dirty=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    typedef efi_status (EFIAPI *Define)(MarbleXoNodeDefinition *,uint32_t *,void *,void *);
    typedef efi_status (EFIAPI *Scalar)(void *,uint32_t);
    uint32_t initial=0;
    if(marble_xo_npa_progress)marble_xo_npa_progress("define-before");
    efi_status defined=((Define)(uintptr_t)provider.define_node)(&marble_xo_npa_node,&initial,NULL,NULL);
    if(marble_xo_npa_progress)marble_xo_npa_progress("define-returned");
    unsigned ready=!EFI_ERROR(defined) && marble_xo_npa_definition.handle &&
        (uint64_t)(uintptr_t)marble_xo_npa_definition.handle==marble_xo_npa.resource && !marble_xo_npa.failed && !marble_xo_npa.active;
    print("marble-xo-npa: defined status=%x ready=%d driver_calls=%d name=/marble/clock/xo-test resource_retained=1 hardware_calls=0\n",
        defined,(uint64_t)ready,(uint64_t)marble_xo_npa.driver_calls);
    if(!ready)return 0;
    marble_xo_npa.registered=1;
    void *handles[3]={0};static const char *names[]={"marble-xo-required-a","marble-xo-required-b","marble-xo-suppressible"};
    for(unsigned i=0;i<3;i++) {
        efi_status status=((MarbleIcbCreate)(uintptr_t)create)(marble_xo_npa_test_name,names[i],i==2?0x800:0x40,0,NULL,&handles[i]);
        print("marble-xo-npa: client index=%d status=%x created=%d\n",(uint64_t)i,status,(uint64_t)(handles[i]!=NULL));
        if(EFI_ERROR(status) || !handles[i])return 0;
    }
    static const unsigned who[]={0,1,0,2,1,2},values[]={UINT32_MAX,1,0,1,0,0},active[]={1,1,1,1,1,0},required[]={1,1,1,1,0,0},supp[]={0,0,0,1,1,0};
    for(unsigned i=0;i<6;i++) {
        efi_status status=((Scalar)(uintptr_t)provider.scalar)(handles[who[i]],values[i]);
        unsigned ok=!EFI_ERROR(status) && !marble_xo_npa.failed && marble_xo_npa.active==active[i] &&
            marble_xo_npa.required==required[i] && marble_xo_npa.suppressible==supp[i];
        print("marble-xo-npa: request index=%d status=%x PASS=%d active=%d required=%d suppressible=%d\n",(uint64_t)i,status,(uint64_t)ok,
            (uint64_t)marble_xo_npa.active,(uint64_t)marble_xo_npa.required,(uint64_t)marble_xo_npa.suppressible);
        if(!ok)return 0;
    }
    for(unsigned i=3;i;i--) {
        efi_status status=((MarbleIcbDestroy)(uintptr_t)destroy)(handles[i-1]);
        print("marble-xo-npa: destroy index=%d status=%x\n",(uint64_t)(i-1),status);
        if(EFI_ERROR(status))return 0;
    }
    uint64_t head=~0ULL;
    unsigned empty=marble_npa_ram_word(marble_xo_npa.resource+0x18,&head) && !head && !marble_xo_npa.active && !marble_xo_npa.failed;
    unsigned cache=marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_after) &&
        marble_rpmh_same_resources(&marble_rpmh_before,&marble_rpmh_after) && marble_rpmh_same_clients(&marble_rpmh_before,&marble_rpmh_after);
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    unsigned ok=empty && cache && us<1000000;efi_status cleared=~0ULL;
    if(ok){cleared=BS->set_watchdog_timer(0,0,0,NULL);ok=!EFI_ERROR(cleared);}
    if(ok)marble_xo_npa.dirty=0;
    print("marble-xo-npa: complete PASS=%d attempted=1 dirty=%d clients_released=%d RPMh_cache_restored=%d elapsed_us=%d watchdog_clear=%x provider_retained=1 graph_restored=0 cold_reset_required=1 hardware_calls=0 DSP_start=0 storage_access=0 lease_accepted=0\n",
        (uint64_t)ok,(uint64_t)marble_xo_npa.dirty,(uint64_t)empty,(uint64_t)cache,us,cleared);
    return ok;
}
#endif
#endif
