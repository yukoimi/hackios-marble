#ifndef MARBLE_ICB_BANDWIDTH_FW_H
#define MARBLE_ICB_BANDWIDTH_FW_H
#ifdef XNU_MARBLE
#include "marble_icb_bandwidth.h"
#include "marble_icb_bandwidth_code.h"
#define MARBLE_ICB_TRACE(s) do {if(marble_icb_progress)marble_icb_progress(s);} while(0)
static MarbleIcbHardware marble_icb_hw_before[128];
static unsigned marble_icb_hw_count;
static uint64_t marble_icb_hw_topology,marble_icb_rpmh_owner;
/* Set only by the new single-command owned-stage coordinator. A negative
 * result retains this bandwidth handle and the outer LPI/XO/watchdog leases. */
static int (*marble_icb_bandwidth_held_action)(void);
static unsigned marble_icb_scm_owned_authorized;
static int marble_icb_hardware_before(void)
{
    uint64_t data=0,rsc=0,count=0,array=0;
    if(!marble_npa_ram_word(marble_dep_first.definition+0x28,&data) ||
       !marble_npa_ram_word(data,&marble_icb_hw_topology) ||
       !marble_npa_ram_word(marble_icb_hw_topology,&rsc) || rsc!=2 ||
       !marble_npa_ram_word(marble_icb_hw_topology+8,&marble_icb_rpmh_owner) ||
       !marble_npa_ram_word(marble_icb_hw_topology+0x48,&count) || (count>>32) || !count || count>128 ||
       !marble_npa_ram_word(marble_icb_hw_topology+0x50,&array)) {
        print("marble-icb-bw: hardware_metadata_refused data=%x topology=%x rsc=%x owner=%x count=%x array=%x\n",data,marble_icb_hw_topology,rsc,marble_icb_rpmh_owner,count,array);return 0;
    }
    marble_icb_hw_count=0;
    for(unsigned i=0;i<count;i++) {
        uint64_t address=0;
        if(!marble_npa_ram_word(array+8*i,&address))return 0;
        unsigned duplicate=0;
        for(unsigned j=0;j<marble_icb_hw_count;j++)if(address==marble_icb_hw_before[j].address)duplicate=1;
        if(duplicate)continue; /* A topology array may reference the same node more than once. */
        MarbleIcbHardware *n=&marble_icb_hw_before[marble_icb_hw_count++];n->address=address;
        if(!marble_rpmh_words(marble_npa_ram_word,n->address,n->words,12)) {
            print("marble-icb-bw: hardware_node_refused index=%d node=%x count=%d array=%x\n",(uint64_t)i,n->address,count,array);return 0;
        }
    }
    print("marble-icb-bw: hardware_inventory raw_count=%d unique_nodes=%d owner=%x\n",count,(uint64_t)marble_icb_hw_count,marble_icb_rpmh_owner);
    return 1;
}
static int marble_icb_hardware_after(void)
{
    unsigned ok=1;
    for(unsigned i=0;i<marble_icb_hw_count;i++) {
        MarbleIcbHardware *a=&marble_icb_hw_before[i],b={0};b.address=a->address;
        if(!marble_rpmh_words(marble_npa_ram_word,b.address,b.words,12))return 0;
        unsigned same=!memcmp(a->words,b.words,10*sizeof(uint64_t));
        if(!same)ok=0;
        for(unsigned j=0;j<12;j++)if(a->words[j]!=b.words[j])print("marble-icb-bw: hardware_diff address=%x word=%d before=%x after=%x live_restored=%d\n",
            a->words[1]>>32,(uint64_t)j,a->words[j],b.words[j],(uint64_t)same);
    }
    return ok;
}
static int marble_icb_added_cache(const MarbleRpmhSnapshot *a,const MarbleRpmhSnapshot *b)
{
    unsigned added=0;
    for(unsigned i=0;i<b->resource_count;i++) {
        unsigned existed=0;
        for(unsigned j=0;j<a->resource_count;j++)if(a->resources[j][0]==b->resources[i][0])existed=1;
        if(existed)continue;
        unsigned matches=0;uint32_t expected=0;uint64_t vote=b->resources[i][1];
        for(unsigned j=0;j<marble_icb_hw_count;j++)if(marble_icb_hw_before[j].words[1]>>32==b->resources[i][0]) {
            if(!marble_icb_bandwidth_bcm(&marble_icb_hw_before[j],&expected))return 0;
            matches++;
        }
        unsigned valid=matches==1 && vote && !b->resources[i][2],found=0;uint32_t actual=0;
        for(unsigned j=0;j<b->vote_count;j++)if(b->vote_addresses[j]==vote) {
            found++;actual=(uint32_t)b->votes[j][3];
            if(!marble_icb_bandwidth_added_vote(b->votes[j],expected))valid=0;
        }
        valid=valid && found==1;
        print("marble-icb-bw: derived_cache address=%x expected=%x actual=%x PASS=%d reference=before_request_native_aggregate\n",
            b->resources[i][0],(uint64_t)expected,(uint64_t)actual,(uint64_t)valid);
        if(!valid)return 0;added++;
    }
    return added && added<=8 && b->vote_count==a->vote_count+added;
}
static int marble_icb_peak_completions(uint64_t *rpmh,uint64_t rb,const char *phase)
{
    typedef efi_status (EFIAPI *Finish)(uint32_t);
    unsigned drained=0,polls=0;
    for(;polls<50;polls++) {
        if(EFI_ERROR(((Finish)(uintptr_t)rpmh[13])(2)) ||
           !marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_after))break;
        drained=1;
        for(unsigned i=0;i<marble_rpmh_after.vote_count;i++)if(marble_rpmh_after.votes[i][0]>>32)drained=0;
        for(unsigned i=0;i<marble_rpmh_after.active.count;i++)for(unsigned j=0;j<4;j+=2)
            if(marble_rpmh_after.client_queues[i][j][1] || marble_rpmh_after.client_queues[i][j][2])drained=0;
        if(drained) {polls++;break;}BS->stall(100);
    }
    print("marble-icb-bw: peak_completions phase=%s PASS=%d polls=%d\n",phase,(uint64_t)drained,(uint64_t)polls);
    return drained;
}
static int marble_icb_bandwidth_cycle_peak(unsigned run_owned,uint64_t peak)
{
    unsigned scm_peak=peak==MARBLE_SCM_PEAK_BYTES;
    if(peak!=1000000 && !scm_peak)return 0;
    if(scm_peak) {
        if(run_owned==1) {
            if(marble_icb_bandwidth_held_action || marble_pil_attempted || marble_smp2p_attempted ||
               marble_npa_attempted || marble_qmp_native.attempted || marble_xo_rpmh_attempted || marble_icb_scm_owned_authorized)return 0;
        } else if(run_owned==3) {
            if(!marble_icb_scm_owned_authorized || !marble_icb_bandwidth_held_action || !marble_pil_header_only ||
               !marble_pil_attempted || marble_pil_dsp_dirty || marble_pil_auth_attempted ||
               !marble_smp2p_owned || !marble_xo_rpmh_attempted || marble_npa_active!=2 || marble_qmp_native.attempted)return 0;
        } else return 0;
    } else if(run_owned==3 || marble_icb_scm_owned_authorized)return 0;
    MARBLE_ICB_TRACE("metadata-before");
    if(run_owned>3 || (marble_icb_attempted && !run_owned) ||
       (run_owned<2 && !marble_adsp_dependency_inventory()) || !marble_icb_endpoint_identity())return 0;
    if(run_owned>=2 && (!marble_rpmh_attempted || !marble_rpmh_dirty || marble_npa_active!=2 || !marble_npa_dirty))return 0;
    MARBLE_ICB_TRACE("metadata-after");
    MarbleIcbNpaCalls calls={0};uint64_t base=0,*rpmh=NULL,rb=0;
    if(!marble_icb_zero_qualify(&calls,&base) || !marble_rpmh_qualify(&rpmh,&rb))return 0;
    static const efi_guid ng={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    uint64_t *npa=NULL;
    if(EFI_ERROR(BS->locate_protocol(&ng,NULL,(void **)&npa)) || !npa)return 0;
    uint64_t nb=(uint64_t)(uintptr_t)npa-0x120b8;MarbleNpaVectorProtocol protocol;
    if(!marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)npa,(uint64_t *)(void *)&protocol,19) ||
       !marble_icb_bandwidth_binding(&protocol,nb))return 0;
    for(unsigned i=0;i<sizeof(marble_icb_bandwidth_npa_code)/sizeof(marble_icb_bandwidth_npa_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,nb+marble_icb_bandwidth_npa_code[i].rva,
            marble_icb_bandwidth_npa_code[i].bytes,marble_icb_bandwidth_npa_code[i].digest))return 0;
    for(unsigned i=0;i<sizeof(marble_icb_bandwidth_icb_code)/sizeof(marble_icb_bandwidth_icb_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_icb_bandwidth_icb_code[i].rva,
            marble_icb_bandwidth_icb_code[i].bytes,marble_icb_bandwidth_icb_code[i].digest))return 0;
    uint64_t native_state=0;
    if(!marble_npa_ram_word(base+0xc328,&native_state) || (uint32_t)native_state!=(run_owned?4u:2u))return 0;
    print("marble-icb-bw: binding vector_offset=90 vector_rva=162c complete_offset=18 complete_rva=1474 code_ranges=12 contract=%s\n",
        run_owned?"run-request-reset-v1":"derived-cache-reset-v1");
    MARBLE_ICB_TRACE("abi-after");
    (void)rpmh;
    uint64_t resource=marble_dep_first.resource,resource_before[16],resource_after[16];
    MarbleIcbNode *route=NULL;
    for(unsigned i=0;i<marble_icb_first.count;i++)if(marble_icb_first.nodes[i].words[0]==(23ULL<<32))route=&marble_icb_first.nodes[i];
    if(!route || !marble_icb_clients(resource,&marble_icb_clients_before) || marble_icb_clients_before.count>=64 ||
       !marble_rpmh_words(marble_npa_ram_word,resource,resource_before,16) ||
       !marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_before) || !marble_icb_hardware_before())return 0;
    unsigned owner=0;
    for(unsigned i=0;i<marble_rpmh_before.active.count;i++)if(marble_rpmh_before.active.nodes[i]==marble_icb_rpmh_owner)owner++;
    if(owner!=1)return 0;
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
    unsigned watchdog=scm_peak && run_owned==1?5:30;
    if(EFI_ERROR(BS->set_watchdog_timer(watchdog,0,0,NULL)))return 0;
    marble_icb_attempted=1;marble_icb_dirty=1;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    print("marble-icb-bw: qualified exact_abi=1 route_names=8 hops=4 vector_type=400 pair_bytes=8 explicit_requests=1 request_type=4 request_words=8 peak_bytes_per_second=%d watchdog_seconds=%d\n",peak,(uint64_t)watchdog);
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
    print("marble-icb-bw: created status=%x owned=%d empty=%d lists_appended=%d clients_before=%d clients_during=%d\n",
        created,(uint64_t)owned,(uint64_t)empty,(uint64_t)lists,(uint64_t)marble_icb_clients_before.count,(uint64_t)marble_icb_clients_after.count);
    if(!lists)return 0; /* Retain watchdog; never destroy an unqualified handle. */
    typedef efi_status (EFIAPI *Vector)(void *,uint32_t,const uint32_t *);
    typedef efi_status (EFIAPI *Complete)(void *);
    MarbleIcbBandwidth request={4,0,peak,0,0,0};
    MARBLE_ICB_TRACE("bandwidth-request-before");
    efi_status issued=((Vector)(uintptr_t)protocol.vector)(handle,8,(const uint32_t *)(const void *)&request);
    MARBLE_ICB_TRACE("bandwidth-request-returned");
    unsigned requested=!EFI_ERROR(issued) && marble_rpmh_words(marble_npa_ram_word,vector[3],requests,4) &&
        marble_icb_bandwidth_request(requests,(uint64_t)(uintptr_t)handle,peak);
    print("marble-icb-bw: requested status=%x accepted=%d ib=%d ab=%d owner_match=%d\n",issued,(uint64_t)requested,
        requests[0],requests[1],(uint64_t)(requests[2]==(uint64_t)(uintptr_t)handle));
    if(!requested)return 0;
    BS->stall(run_owned>=2 || scm_peak?10000:1000);
    unsigned peak_hold_ok=1;
    if(scm_peak) {
        unsigned snapshot=marble_icb_peak_completions(rpmh,rb,"held"),saturated=0,verified=0;
        peak_hold_ok=snapshot;
        for(unsigned i=0;snapshot && i<marble_icb_hw_count;i++) {
            MarbleIcbHardware node={0};node.address=marble_icb_hw_before[i].address;uint32_t expected=0;
            if(!marble_rpmh_words(marble_npa_ram_word,node.address,node.words,12)) {peak_hold_ok=0;break;}
            if((uint32_t)node.words[1]!=0)continue;
            unsigned computed=marble_icb_bandwidth_bcm(&node,&expected),matches=0;uint32_t actual=0;
#ifdef XNU_MARBLE_QUP_CORE_KEEPALIVE
            if(marble_qup_keepalive_held && (node.words[1]>>32)==marble_qup_keepalive_address) {
                uint32_t logical=expected;
                computed=computed && marble_qup_keepalive_peak(&marble_rpmh_before,&marble_rpmh_after,
                    marble_qup_keepalive_address,logical,&expected);
                print("qk: peak address=%x logical=%x expected=%x retained_floor_verified=%d\n",
                    (uint64_t)marble_qup_keepalive_address,(uint64_t)logical,(uint64_t)expected,(uint64_t)computed);
            }
#endif
            for(unsigned j=0;j<marble_rpmh_after.resource_count;j++)if(marble_rpmh_after.resources[j][0]==node.words[1]>>32)
                for(unsigned k=0;k<marble_rpmh_after.vote_count;k++)if(marble_rpmh_after.vote_addresses[k]==marble_rpmh_after.resources[j][1]) {
                    matches++;actual=(uint32_t)marble_rpmh_after.votes[k][3];
                    uint32_t ack=(uint32_t)(marble_rpmh_after.votes[k][1]>>32);
                    if((actual&~0x40000000u)!=expected || (ack&~0x40000000u)!=expected)computed=0;
                }
            unsigned valid=computed && matches==1;
            if(!valid)peak_hold_ok=0;else verified++;
            if(valid && (expected&0x3fff)==0x3fff)saturated++;
            print("marble-icb-bw: peak_vote address=%x expected=%x actual=%x PASS=%d\n",node.words[1]>>32,(uint64_t)expected,(uint64_t)actual,(uint64_t)valid);
        }
        MarbleThermalSample held_thermal={0};
        unsigned cool=marble_thermal_snapshot(marble_thermal_read,NULL,&held_thermal) && held_thermal.maximum_mc<55000;
        peak_hold_ok=peak_hold_ok && saturated && verified && cool;
        print("marble-icb-bw: peak_hold PASS=%d verified=%d saturated=%d hold_us=10000 thermal_ok=%d die_mc=%d cpu_clock_calls=0 voltage_calls=0\n",
            (uint64_t)peak_hold_ok,(uint64_t)verified,(uint64_t)saturated,(uint64_t)cool,(uint64_t)held_thermal.maximum_mc);
        /* Even failed telemetry must fall through to Complete/Destroy for the
         * already-qualified owned request; never retain a peak for logging. */
    }
    if(run_owned>=2) {
        unsigned held=marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_after) &&
            marble_xo_rpmh_vote(&marble_rpmh_after,1);
        for(unsigned i=0;i<2;i++) {
            uint64_t state=0,aggregate=0;
            if(!marble_npa_ram_word(marble_npa_lpi[i].resource+0x30,&state) || (uint32_t)(state>>32)!=256 ||
               !marble_npa_ram_word(marble_npa_lpi[i].resource+0x40,&aggregate) || (uint32_t)aggregate!=256)held=0;
        }
        print("marble-adsp-dependency: simultaneous PASS=%d XO_ack=1 bandwidth_ib=%d LPI_clients=%d LPI_level=256 hold_us=10000 DSP_start=0\n",
            (uint64_t)held,peak,(uint64_t)marble_npa_active);
        if(!held)return 0;
    }
    unsigned held_action_ok=1;
    if(marble_icb_bandwidth_held_action) {
        if(run_owned<2)return 0;
        int outcome=marble_icb_bandwidth_held_action();
        print("marble-adsp-owned: action_return outcome=%d bandwidth_release_allowed=%d\n",(uint64_t)(int64_t)outcome,(uint64_t)(outcome>=0));
        if(outcome<0)return 0;
        held_action_ok=outcome==1;
    }
    MARBLE_ICB_TRACE("bandwidth-complete-before");
    efi_status completed=((Complete)(uintptr_t)protocol.complete)(handle);
    MARBLE_ICB_TRACE("bandwidth-complete-returned");
    unsigned released=!EFI_ERROR(completed) && marble_rpmh_words(marble_npa_ram_word,vector[3],requests,4) &&
        marble_icb_bandwidth_request(requests,(uint64_t)(uintptr_t)handle,0);
    print("marble-icb-bw: completed status=%x zero=%d ib=%d ab=%d owner_match=%d\n",completed,(uint64_t)released,
        requests[0],requests[1],(uint64_t)(requests[2]==(uint64_t)(uintptr_t)handle));
    if(!released)return 0;
    if(scm_peak && !marble_icb_peak_completions(rpmh,rb,"released"))peak_hold_ok=0;
    MARBLE_ICB_TRACE("destroy-before");
    efi_status destroyed=calls.destroy(handle);
    MARBLE_ICB_TRACE("destroy-returned");
    unsigned restored=1;
    for(unsigned i=0;i<8;i++) {
        unsigned read=marble_icb_requests(marble_npa_ram_word,route->pairs[i],!(i%2),&marble_icb_lists_after[i]);
        unsigned same=read && marble_icb_bandwidth_lists(&marble_icb_lists_before[i],&marble_icb_lists_after[i],peak);
        if(read)for(unsigned j=0;j<marble_icb_lists_before[i].head_words;j++)
            if(marble_icb_lists_before[i].head[j]!=marble_icb_lists_after[i].head[j])
                print("marble-icb-bw: endpoint_diff index=%d word=%d before=%x after=%x\n",(uint64_t)i,(uint64_t)j,
                    marble_icb_lists_before[i].head[j],marble_icb_lists_after[i].head[j]);
        print("marble-icb-bw: list index=%d restored=%d before=%d after=%d old_capacity=%d native_capacity=%d\n",
            (uint64_t)i,(uint64_t)same,(uint64_t)marble_icb_lists_before[i].count,(uint64_t)marble_icb_lists_after[i].count,
            (uint64_t)marble_icb_lists_before[i].capacity,(uint64_t)marble_icb_lists_after[i].capacity);
        if(!same)restored=0;
    }
    unsigned clients=marble_icb_clients(resource,&marble_icb_clients_after) && marble_icb_clients_same(&marble_icb_clients_before,&marble_icb_clients_after);
    unsigned state=marble_rpmh_words(marble_npa_ram_word,resource,resource_after,16) && !memcmp(resource_before,resource_after,sizeof(resource_before));
    for(unsigned i=0;i<16;i++)if(resource_before[i]!=resource_after[i])
        print("marble-icb-bw: resource_diff word=%d before=%x after=%x\n",(uint64_t)i,resource_before[i],resource_after[i]);
    unsigned cache_snapshot=marble_rpmh_snapshot(marble_npa_ram_word,rb+0xe3a8,&marble_rpmh_after);
    unsigned cache=cache_snapshot &&
        marble_rpmh_same_resources(&marble_rpmh_before,&marble_rpmh_after) && marble_rpmh_same_clients(&marble_rpmh_before,&marble_rpmh_after);
    MarbleRpmhSnapshot *a=&marble_rpmh_before,*b=&marble_rpmh_after;
    unsigned original_cache=cache_snapshot && marble_icb_bandwidth_originals(a,b,marble_icb_rpmh_owner);
    unsigned hardware=marble_icb_hardware_after(),derived=cache_snapshot && (run_owned?
        (a->resource_count==b->resource_count && a->vote_count==b->vote_count):marble_icb_added_cache(a,b)),queues=1;
    static const unsigned queue_offsets[]={0xc320,0xc330,0xc388,0xc390};
    for(unsigned i=0;i<4;i++){uint64_t head=~0ULL;if(!marble_npa_ram_word(base+queue_offsets[i],&head) || head)queues=0;}
    print("marble-icb-bw: cache_counts resources_before=%d resources_after=%d votes_before=%d votes_after=%d clients_before=%d clients_after=%d\n",
        (uint64_t)a->resource_count,(uint64_t)b->resource_count,(uint64_t)a->vote_count,(uint64_t)b->vote_count,(uint64_t)a->active.count,(uint64_t)b->active.count);
    for(unsigned i=0;i<4;i++)if(a->global[i]!=b->global[i])print("marble-icb-bw: global_diff word=%d before=%x after=%x\n",(uint64_t)i,a->global[i],b->global[i]);
    for(unsigned i=0;i<b->resource_count;i++) {
        unsigned old=a->resource_count;
        for(unsigned j=0;j<a->resource_count;j++)if(a->resources[j][0]==b->resources[i][0])old=j;
        if(old==a->resource_count)print("marble-icb-bw: retained_resource address=%x votes=%x pdc=%x\n",b->resources[i][0],b->resources[i][1],b->resources[i][2]);
        else for(unsigned j=0;j<3;j++)if(a->resources[old][j]!=b->resources[i][j])print("marble-icb-bw: resource_cache_diff index=%d word=%d before=%x after=%x\n",(uint64_t)i,(uint64_t)j,a->resources[old][j],b->resources[i][j]);
    }
    for(unsigned i=0;i<b->vote_count;i++) {
        unsigned old=a->vote_count;
        for(unsigned j=0;j<a->vote_count;j++)if(a->vote_addresses[j]==b->vote_addresses[i])old=j;
        for(unsigned j=0;j<9;j++) {
            if(old==a->vote_count)print("marble-icb-bw: retained_vote address=%x word=%d value=%x\n",b->vote_addresses[i],(uint64_t)j,b->votes[i][j]);
            else if(a->votes[old][j]!=b->votes[i][j])print("marble-icb-bw: vote_diff index=%d word=%d before=%x after=%x\n",(uint64_t)i,(uint64_t)j,a->votes[old][j],b->votes[i][j]);
        }
    }
    if(a->active.count==b->active.count)for(unsigned i=0;i<a->active.count;i++) {
        for(unsigned j=0;j<11;j++)if(a->clients[i][j]!=b->clients[i][j])print("marble-icb-bw: client_diff index=%d word=%d before=%x after=%x\n",(uint64_t)i,(uint64_t)j,a->clients[i][j],b->clients[i][j]);
        for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)if(a->client_queues[i][j][k]!=b->client_queues[i][j][k])
            print("marble-icb-bw: queue_diff index=%d queue=%d word=%d before=%x after=%x\n",(uint64_t)i,(uint64_t)j,(uint64_t)k,a->client_queues[i][j][k],b->client_queues[i][j][k]);
    }
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    uint64_t limit_us=marble_icb_bandwidth_held_action?15000000:1000000;
    unsigned ok=peak_hold_ok && held_action_ok && !EFI_ERROR(destroyed) && restored && clients && state && original_cache && hardware && derived && queues && us<limit_us;
    print("marble-icb-bw: complete PASS=%d destroy_status=%x attempted=%d dirty=%d lists_restored=%d clients_restored=%d resource_restored=%d RPMh_cache_restored=%d original_cache_unchanged=%d hardware_aggregate_restored=%d added_cache_derived=%d queues_drained=%d elapsed_us=%d watchdog_armed=1 cold_reset_required=1 native_history_retained=1 cache_allocations_retained=1 original_hardware_restored=0 create_calls=1 destroy_calls=1 explicit_requests=1 complete_calls=1 DSP_start=%d storage_access=0 bandwidth_request_released=1 audio_acceptance=0 mode_run=%d\n",
        (uint64_t)ok,destroyed,(uint64_t)marble_icb_attempted,(uint64_t)marble_icb_dirty,(uint64_t)restored,(uint64_t)clients,(uint64_t)state,(uint64_t)cache,
        (uint64_t)original_cache,(uint64_t)hardware,(uint64_t)derived,(uint64_t)queues,us,(uint64_t)marble_pil_auth_attempted,(uint64_t)run_owned);
    return ok;
}
static int marble_icb_bandwidth_cycle_mode(unsigned run_owned){return marble_icb_bandwidth_cycle_peak(run_owned,1000000);}
static int marble_icb_bandwidth_cycle(void){return marble_icb_bandwidth_cycle_mode(0);}
#undef MARBLE_ICB_TRACE
#endif
#endif
