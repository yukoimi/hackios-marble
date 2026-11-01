#ifndef MARBLE_QUP_KEEPALIVE_FW_H
#define MARBLE_QUP_KEEPALIVE_FW_H
#ifdef XNU_MARBLE_QUP_CORE_KEEPALIVE
#include "marble_qup_keepalive.h"
static unsigned marble_qup_keepalive_attempted;
static unsigned marble_qup_keepalive_held;
static uint32_t marble_qup_keepalive_address;
static const char marble_qup_keepalive_name[]="marble-qup2-keepalive";
static int marble_qup_keepalive_hold(void)
{
    if(marble_qup_keepalive_attempted || !marble_xo_rpmh_attempted || !marble_rpmh_attempted || !marble_rpmh_dirty ||
       marble_npa_active!=2 || !marble_pil_header_only || marble_pil_auth_attempted || marble_pil_dsp_dirty)return 0;
    marble_qup_keepalive_attempted=1;
    uint64_t *table=NULL,base=0;
    MarbleXoRpmhProtocol p;
    if(!marble_rpmh_qualify(&table,&base) || !marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)table,(uint64_t *)(void *)&p,20) || !marble_xo_rpmh_binding(&p,base))return 0;
    for(unsigned i=0;i<sizeof(marble_xo_rpmh_code)/sizeof(marble_xo_rpmh_code[0]);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_xo_rpmh_code[i].rva,marble_xo_rpmh_code[i].bytes,marble_xo_rpmh_code[i].digest))return 0;
    for(unsigned off=0;off<MARBLE_CMD_DB_BYTES;off+=4096) {
        uint64_t a=MARBLE_CMD_DB_PHYS+off,par=marble_fw_translation(a);
        if((par&1) || (par&0x0000fffffffff000ULL)!=a)return 0;
    }
    uint8_t *data=NULL;
    if(EFI_ERROR(BS->allocate_pool(EfiLoaderData,2*MARBLE_CMD_DB_BYTES,(void **)&data)) || !data)return 0;
    for(unsigned pass=0;pass<2;pass++) {
        const volatile uint64_t *src=(const volatile uint64_t *)(uintptr_t)MARBLE_CMD_DB_PHYS;
        uint64_t *dst=(uint64_t *)(void *)(data+pass*MARBLE_CMD_DB_BYTES);
        for(unsigned i=0;i<MARBLE_CMD_DB_BYTES/8;i++)dst[i]=src[i];
        __asm__ volatile("dmb sy" ::: "memory");if(!pass)BS->stall(100);
    }
    MarbleCmdDbEntry e={0};MarbleCmdDbIssue issue={0};
    unsigned stable=!memcmp(data,data+MARBLE_CMD_DB_BYTES,MARBLE_CMD_DB_BYTES);
    unsigned parsed=stable && marble_cmd_db_find_diagnostic(data,MARBLE_CMD_DB_BYTES,"QUP2",&e,&issue);
    unsigned qualified=parsed && e.slave==5 && e.bytes==8 && marble_db_u32(e.aux)>0 && marble_db_u16(e.aux+4)>0;
    uint32_t address=e.address,unit=qualified?marble_db_u32(e.aux):0;
    uint16_t width=qualified?marble_db_u16(e.aux+4):0;
    efi_status freed=BS->free_pool(data);
    print("qk: begin name=QUP2 stable=%d qualified=%d address=%x unit=%d width=%d free_status=%x data=60004001 sets=2,0 zero_votes=0 sleep_votes=0\n",
        (uint64_t)stable,(uint64_t)qualified,(uint64_t)address,(uint64_t)unit,(uint64_t)width,freed);
    if(!qualified || EFI_ERROR(freed))return 0;
    MarbleRpmhSnapshot *before=&marble_rpmh_before,*after=&marble_rpmh_after;
    if(!marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,before) || before->pool.count || before->active.count>=MARBLE_RPMH_CLIENTS)return 0;
    for(unsigned i=0;i<before->resource_count;i++)if((uint32_t)before->resources[i][0]==address) {
        print("qk: cached address=%x header=%x pointer=%x pending=%x resources=%d votes=%d clients=%d read_only=1\n",
            (uint64_t)address,before->resources[i][0],before->resources[i][1],before->resources[i][2],
            (uint64_t)before->resource_count,(uint64_t)before->vote_count,(uint64_t)before->active.count);
        uint64_t node=before->resources[i][1];
        for(unsigned depth=0;node && depth<8;depth++) {
            unsigned found=0;
            for(unsigned j=0;j<before->vote_count;j++)if(before->vote_addresses[j]==node) {
                const uint64_t *v=before->votes[j];
                print("qk: cached_vote depth=%d pointer=%x words=%x,%x,%x,%x,%x,%x,%x,%x,%x read_only=1\n",
                    (uint64_t)depth,node,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);
                node=v[8];found=1;break;
            }
            if(!found)break;
        }
    }
    unsigned baseline=marble_qup_keepalive_baseline(before,address);
    print("qk: baseline mode=%d exact_idle_zero=%d votes=0\n",(uint64_t)baseline,(uint64_t)(baseline==2));
    if(!baseline)return 0;
    typedef efi_status (EFIAPI *Create)(uint32_t,const char *,void **);
    typedef efi_status (EFIAPI *Issue)(void *,uint32_t,uint8_t,uint32_t,uint32_t,uint32_t *);
    typedef efi_status (EFIAPI *Churn)(void *,uint32_t);
    void *handle=NULL;uint32_t last=0;
    efi_status created=((Create)(uintptr_t)p.create)(2,marble_qup_keepalive_name,&handle);
    unsigned valid=!EFI_ERROR(created) && handle && marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after) &&
        after->active.count==before->active.count+1 && after->active.nodes[before->active.count]==(uint64_t)(uintptr_t)handle &&
        marble_rpmh_client_empty(marble_npa_ram_word,(uint64_t)(uintptr_t)handle,(uint64_t)(uintptr_t)marble_qup_keepalive_name) && marble_rpmh_same_resources(before,after);
    print("qk: client status=%x owned_empty=%d\n",created,(uint64_t)valid);
    if(!valid)return 0;
    static const uint32_t sets[]={2,0};
    for(unsigned i=0;i<2;i++) {
        last=UINT32_MAX;
        efi_status status=((Issue)(uintptr_t)p.issue)(handle,sets[i],sets[i]==0,address,MARBLE_QUP_KEEPALIVE_DATA,&last);
        print("qk: issue set=%d status=%x id=%d address=%x data=60004001\n",(uint64_t)sets[i],status,(uint64_t)last,(uint64_t)address);
        if(EFI_ERROR(status) || (sets[i]!=0 && last) || (sets[i]==0 && (!last || last==UINT32_MAX)))return 0;
    }
    efi_status churned=((Churn)(uintptr_t)p.churn_all)(handle,last);
    unsigned snapshot=marble_rpmh_snapshot(marble_npa_ram_word,base+0xe3a8,after);
    unsigned owners=snapshot && marble_qup_keepalive_others(before,after,address,baseline),vote=snapshot && marble_qup_keepalive_vote(after,address);
    for(unsigned i=0;snapshot && i<after->resource_count;i++)if(after->resources[i][0]==address)
        for(unsigned j=0;j<after->vote_count;j++)if(after->vote_addresses[j]==after->resources[i][1]) {
            const uint64_t *v=after->votes[j];
            print("qk: result_vote words=%x,%x,%x,%x,%x,%x,%x,%x,%x\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);
        }
    unsigned owned=owners && marble_qup_keepalive_drained(after,before->active.count,(uint64_t)(uintptr_t)handle,
        (uint64_t)(uintptr_t)marble_qup_keepalive_name,last);
    if(snapshot && after->active.count==before->active.count+1) {
        unsigned n=before->active.count;
        print("qk: completion last=%d completed=%x pending=%x callbacks=%x barrier=%x\n",(uint64_t)last,
            after->clients[n][6],after->clients[n][7],after->clients[n][8],after->clients[n][9]);
        for(unsigned j=0;j<4;j++)print("qk: queue index=%d header=%x first=%x last=%x free_pool=%d\n",(uint64_t)j,
            after->client_queues[n][j][0],after->client_queues[n][j][1],after->client_queues[n][j][2],(uint64_t)(j==1 || j==3));
    }
    print("qk: held PASS=%d churn_status=%x other_resources_clients_unchanged=%d vote_acknowledged=%d owned_drained=%d retained_client=1 data=60004001 issue_calls=2 zero_votes=0 sleep_votes=0 cold_reset_required=1 CPU_GPU_frequency_requests=0 voltage_requests=0 audio_acceptance=0\n",
        (uint64_t)(!EFI_ERROR(churned)&&owners&&vote&&owned),churned,(uint64_t)owners,(uint64_t)vote,(uint64_t)owned);
    marble_qup_keepalive_held=!EFI_ERROR(churned) && owners && vote && owned;
    if(marble_qup_keepalive_held)marble_qup_keepalive_address=address;
    return marble_qup_keepalive_held;
}
#endif
#endif
