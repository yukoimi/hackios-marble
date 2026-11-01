#ifndef MARBLE_ICB_FW_H
#define MARBLE_ICB_FW_H
#ifdef XNU_MARBLE
#include "marble_icb.h"
#include "marble_icb_code.h"
static MarbleIcbSnapshot marble_icb_first,marble_icb_second;
static int marble_adsp_icb_inventory(void)
{
    if(marble_rpmh_attempted || marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted ||
       marble_qmp_native.attempted || marble_glink_observer.attempted || marble_cap.active || marble_cap.dirty)return 0;
    static const efi_guid guid={0x5824f9de,0x17d2,0x4b1f,{0xa5,0xfe,0x5e,0xb7,0x7f,0xa5,0x30,0x93}};
    uint64_t *table=NULL,v=0,config=0;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table)return 0;
    uint64_t address=(uint64_t)(uintptr_t)table;
    if(address<0x8000c108 || !marble_adc_ram_word(address,&v) || v!=0x10000)return 0;
    uint64_t base=address-0xc108;
    /* Only protocol slot is Init. Deliberately never call it. Lookup at
     * 42a0 loads c338 -> cfg+8 count / +10 resources -> entry+10 tree.
     * 43fc reads {key,route,left,right}; key=(master<<32)|slave. */
    if(!marble_adc_ram_word(address+8,&v) || v!=base+0x13f4)return 0;
    for(unsigned i=0;i<14;i++)if(!marble_npa_ram_word(base+0x42a0+4*i,&v) || (uint32_t)v!=marble_icb_code_42a0[i])return 0;
    for(unsigned i=0;i<13;i++)if(!marble_npa_ram_word(base+0x43fc+4*i,&v) || (uint32_t)v!=marble_icb_code_43fc[i])return 0;
    if(!marble_adc_ram_word(base+0xc338,&config))return 0;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    memset(&marble_icb_first,0,sizeof(marble_icb_first));memset(&marble_icb_second,0,sizeof(marble_icb_second));
    unsigned first=marble_icb_snapshot(marble_npa_ram_word,config,&marble_icb_first);
    BS->stall(100);
    unsigned second=first && marble_icb_snapshot(marble_npa_ram_word,config,&marble_icb_second);
    unsigned stable=second && !memcmp(&marble_icb_first,&marble_icb_second,sizeof(marble_icb_first));
    print("marble-adsp-icb: snapshot exact_abi=1 Init_calls=0 config=%x first=%d second=%d stable=%d trees=%d nodes=%d\n",
        config,(uint64_t)first,(uint64_t)second,(uint64_t)stable,(uint64_t)marble_icb_first.trees,(uint64_t)marble_icb_first.count);
    if(!stable)return 0;
    for(unsigned i=0;i<marble_icb_first.count;i++) {
        MarbleIcbNode *n=&marble_icb_first.nodes[i];
        print("marble-adsp-icb: route index=%d tree=%d master=%d slave=%d hops=%d hardware_identity_verified=0\n",
            (uint64_t)i,(uint64_t)n->tree,n->words[0]>>32,(uint64_t)(uint32_t)n->words[0],(uint64_t)(uint32_t)n->route[0]);
    }
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    unsigned ok=us<1000000;
    print("marble-adsp-icb: complete PASS=%d elapsed_us=%d native_calls=0 clients_created=0 votes=0 MMIO_writes=0 NPA_calls=0 RPMh_calls=0 PAS_calls=0 DSP_start=0 storage_access=0 bandwidth_lease=0\n",(uint64_t)ok,us);
    return ok;
}
typedef struct {
    uint64_t head[8],bus[5];
    char name[97],bus_name[97];
} MarbleIcbEndpointMetadata;
typedef struct {
    MarbleIcbEndpointMetadata hop[4][2];
    uint64_t resource,definition,plugin,resource_words[16],plugin_words[8],clock_words[20];
} MarbleAdspDependencyMetadata;
static MarbleAdspDependencyMetadata marble_dep_first,marble_dep_second;
static int marble_adsp_dependency_metadata(MarbleIcbNode *route,MarbleAdspDependencyMetadata *m)
{
    for(unsigned hop=0;hop<4;hop++)for(unsigned side=0;side<2;side++) {
        MarbleIcbEndpointMetadata *e=&m->hop[hop][side];
        if(!marble_rpmh_words(marble_npa_ram_word,route->pairs[2*hop+side],e->head,8) ||
           !marble_proxy_ram_string(e->head[0],e->name) ||
           !marble_rpmh_words(marble_npa_ram_word,e->head[2],e->bus,5))return 0;
        if(e->bus[0]) {if(!marble_proxy_ram_string(e->bus[0],e->bus_name))return 0;}
        else memcpy(e->bus_name,"virtual",8);
    }
    static const efi_guid ng={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    uint64_t *npa=NULL,at=0,seen[96];unsigned count=0,found=0;
    if(EFI_ERROR(BS->locate_protocol(&ng,NULL,(void **)&npa)) || !npa ||
       !marble_adc_ram_word((uint64_t)(uintptr_t)npa-0x120b8+0x129e8,&at))return 0;
    while(at && count<96) {
        for(unsigned i=0;i<count;i++)if(seen[i]==at)return 0;
        seen[count++]=at;uint64_t next=0,name=0,r=0;char text[97];
        if(!marble_npa_ram_word(at,&next) || !marble_npa_ram_word(at+0x10,&name) ||
           !marble_proxy_ram_string(name,text) || !marble_npa_ram_word(at+0x18,&r))return 0;
        if(str_eq(text,"/icb/arbiter")) {
            if(found++ || !marble_rpmh_words(marble_npa_ram_word,r,m->resource_words,16))return 0;
            m->resource=r;m->definition=m->resource_words[0];m->plugin=m->resource_words[5];
            if(!marble_rpmh_words(marble_npa_ram_word,m->plugin,m->plugin_words,8))return 0;
        }
        at=next;
    }
    if(at || found!=1)return 0;
    static const efi_guid cg={0x241afae6,0x885f,0x4f6c,{0xa7,0xea,0xc2,0x8e,0xab,0x79,0xc3,0xe5}};
    uint64_t *clock=NULL,word=0;
    if(EFI_ERROR(BS->locate_protocol(&cg,NULL,(void **)&clock)) || !clock)return 0;
    uint64_t base=(uint64_t)(uintptr_t)clock-0x1b0f8;
    if(!marble_adc_ram_word((uint64_t)(uintptr_t)clock,&word) || word!=0x10009 ||
       !marble_adc_ram_word((uint64_t)(uintptr_t)clock+8,&word) || word!=base+0x14e4 ||
       !marble_rpmh_words(marble_npa_ram_word,base+0x2a640,m->clock_words,20))return 0;
    char name[97];
    return marble_proxy_ram_string(m->clock_words[0],name) && str_eq(name,"/pmic/client/xo");
}
static int marble_adsp_dependency_inventory(void)
{
    if(!marble_npa_info() || !marble_adsp_icb_inventory())return 0;
    MarbleIcbNode *route=NULL;
    for(unsigned i=0;i<marble_icb_first.count;i++)if(marble_icb_first.nodes[i].words[0]==(23ULL<<32)) {
        if(route)return 0;route=&marble_icb_first.nodes[i];
    }
    if(!route || (uint32_t)route->route[0]!=4)return 0;
    memset(&marble_dep_first,0,sizeof(marble_dep_first));memset(&marble_dep_second,0,sizeof(marble_dep_second));
    unsigned first=marble_adsp_dependency_metadata(route,&marble_dep_first);
    BS->stall(100);
    unsigned second=first && marble_adsp_dependency_metadata(route,&marble_dep_second);
    unsigned stable=second && !memcmp(&marble_dep_first,&marble_dep_second,sizeof(marble_dep_first));
    print("marble-adsp-dependencies: snapshot first=%d second=%d stable=%d firmware_master=23 firmware_slave=0 hops=4\n",
        (uint64_t)first,(uint64_t)second,(uint64_t)stable);
    if(!stable)return 0;
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<2;j++) {
        MarbleIcbEndpointMetadata *e=&marble_dep_first.hop[i][j];
        print("marble-adsp-hop: index=%d side=%d name=%s id=%d bus=%s bus_base=%x width=%d metadata_only=1\n",
            (uint64_t)i,(uint64_t)j,e->name,(uint64_t)(uint32_t)e->head[1],e->bus_name,e->bus[1],(uint64_t)(uint32_t)e->head[3]);
    }
    print("marble-adsp-dependencies: npa resource=%x definition=%x plugin=%x state=%d clients=%x\n",
        marble_dep_first.resource,marble_dep_first.definition,marble_dep_first.plugin,
        marble_dep_first.resource_words[6]>>32,marble_dep_first.resource_words[3]);
    for(unsigned i=0;i<8;i++)print("marble-adsp-dependencies: plugin_word index=%d value=%x\n",(uint64_t)i,marble_dep_first.plugin_words[i]);
    for(unsigned i=0;i<20;i++)print("marble-adsp-dependencies: clock_word index=%d value=%x\n",(uint64_t)i,marble_dep_first.clock_words[i]);
    print("marble-adsp-dependencies: complete PASS=1 native_calls=0 clients_created=0 votes=0 MMIO_writes=0 DSP_start=0 storage_access=0 lease_accepted=0\n");
    return 1;
}
#endif
#endif
