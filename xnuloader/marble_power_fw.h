/* Isolated ADSP power dependency inventory: only mapped metadata reads and
 * existing Clock GetID lookups. No enable/disable, NPA clients/votes, PAS,
 * mailbox/IPCC writes, channel open, payload read or DSP execution. */
#ifndef MARBLE_POWER_FW_H
#define MARBLE_POWER_FW_H
#include "marble_qmp.h"
#ifdef XNU_MARBLE
static int marble_qmp_inventory(void)
{
    static const efi_guid guid={0x3bfcf64d,0x00cd,0x41cb,{0xbe,0xa5,0xa7,0x6c,0x3a,0x24,0x3d,0xed}};
    uint64_t *c=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&c)) || !c)return 0;
    uint64_t base=(uint64_t)(uintptr_t)c-0xd0a8,word=0;
    /* Supplied Mu GlinkDxe SHA 9fbbf72b...: exact QMP get-shmem code and
     * static configuration agree with this phone's DT, c300000/400.
     * Do not call its protocol's final RPM teardown slot as initialization. */
    if(c[0]!=0x10001 || c[1]!=base+0x144c || c[14]!=base+0x17ac ||
       !marble_adc_ram_word(base+0x8300,&word) || word!=0x52808008d65f03c0ULL ||
       !marble_adc_ram_word(base+0x8308,&word) || word!=0xb900002852a18600ULL ||
       !marble_adc_ram_word(base+0xbb38,&word) || word!=0xc300000)return 0;
    uint64_t par=marble_fw_translation(0xc300000);
    print("marble-qmp: qualified native_exact=1 address=c300000 bytes=1024 PAR=%x identity=%d protocol_calls=0\n",par,(uint64_t)(!(par&1) && (par&0x0000fffffffff000ULL)==0xc300000));
    if((par&1) || (par&0x0000fffffffff000ULL)!=0xc300000)return 0;
    MarbleQmpDescriptor first,last;
    const volatile uint32_t *p=(const volatile uint32_t *)(uintptr_t)0xc300000;
    for(unsigned i=0;i<sizeof(first)/4;i++)((uint32_t *)&first)[i]=p[i];
    __asm__ volatile("dmb sy" ::: "memory");BS->stall(100);
    for(unsigned i=0;i<sizeof(last)/4;i++)((uint32_t *)&last)[i]=p[i];
    __asm__ volatile("dmb sy" ::: "memory");
    unsigned stable=!memcmp(&first,&last,sizeof(first)),valid=stable && marble_qmp_valid(&last,0x400);
    print("marble-qmp: descriptor stable=%d valid=%d magic=%x version=%d features=%x local_offset=%x local_bytes=%d remote_offset=%x remote_bytes=%d\n",(uint64_t)stable,(uint64_t)valid,(uint64_t)last.magic,(uint64_t)last.version,(uint64_t)last.features,(uint64_t)last.local_offset,(uint64_t)last.local_size,(uint64_t)last.remote_offset,(uint64_t)last.remote_size);
    print("marble-qmp: states local=%x,%x,%x,%x remote=%x,%x,%x,%x all_up=%d ownership_unknown=1\n",(uint64_t)last.local_link,(uint64_t)last.local_link_ack,(uint64_t)last.local_channel,(uint64_t)last.local_channel_ack,(uint64_t)last.remote_link,(uint64_t)last.remote_link_ack,(uint64_t)last.remote_channel,(uint64_t)last.remote_channel_ack,(uint64_t)(valid && marble_qmp_open(&last)));
    if(!valid)return 0;
    uint32_t local=p[last.local_offset/4],remote=p[last.remote_offset/4];
    unsigned sizes=local<=last.local_size-4 && remote<=last.remote_size-4;
    print("marble-qmp: complete PASS=%d local_message_bytes=%d remote_message_bytes=%d descriptor_reads=120 length_reads=8 payload_reads=0 MMIO_writes=0 IPCC_writes=0 channel_open_calls=0 DSP_start=0\n",(uint64_t)sizes,(uint64_t)local,(uint64_t)remote);
    return sizes;
}
static int marble_power_resource_inventory(void)
{
    if(!marble_npa_info())return 0;
    static const efi_guid guid={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    uint64_t *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x120b8,link=0,seen[96];
    if(!marble_adc_ram_word(base+0x129e8,&link))return 0;
    unsigned count=0,xo=0;
    while(link && count<96) {
        for(unsigned i=0;i<count;i++)if(seen[i]==link)return 0;
        seen[count++]=link;
        uint64_t next=0,name=0,resource=0,definition=0,defname=0,state=0,maximum=0,clients=0;
        char text[97],canonical[97];
        if(!marble_npa_ram_word(link,&next) || !marble_npa_ram_word(link+0x10,&name) ||
           !marble_npa_ram_word(link+0x18,&resource) || !marble_proxy_ram_string(name,text)) {
            print("marble-power-resource: refused stage=link index=%d link=%x name=%x\n",(uint64_t)(count-1),link,name);return 0;
        }
        print("marble-power-resource: found index=%d name=%s resource=%x\n",(uint64_t)(count-1),text,resource);
        if(!resource || !marble_npa_ram_word(resource,&definition))return 0;
        if(!definition) {
            /* NPA legitimately keeps placeholder nodes for dependencies not
             * yet instantiated. Do not invent state/max or follow NULL. */
            print("marble-power-resource: placeholder index=%d name=%s definition=0 read_mask=1\n",(uint64_t)(count-1),text);
            link=next;continue;
        }
        if(!marble_npa_ram_word(definition,&defname) || !marble_proxy_ram_string(defname,canonical) ||
           !marble_npa_ram_word(resource+0x30,&state) || !marble_npa_ram_word(resource+0x68,&maximum) ||
           !marble_npa_ram_word(resource+0x18,&clients)) {
            print("marble-power-resource: refused stage=resource index=%d name=%s definition=%x\n",(uint64_t)(count-1),text,definition);return 0;
        }
        if(str_eq(text,"/pmic/client/xo"))xo++;
        print("marble-power-resource: index=%d name=%s canonical=%s state=%d max=%d clients=%x read_mask=f\n",(uint64_t)(count-1),text,canonical,(uint64_t)(uint32_t)(state>>32),(uint64_t)(uint32_t)maximum,clients);
        link=next;
    }
    print("marble-power-resource: complete PASS=%d visited=%d terminated=%d xo_matches=%d protocol_calls=0 clients_created=0 resource_votes=0 DSP_start=0\n",(uint64_t)(!link && xo<=1),(uint64_t)count,(uint64_t)(!link),(uint64_t)xo);
    return !link && xo<=1; /* an absent XO is an inventory result, not a lease */
}
static int marble_adsp_power_inventory(void)
{
    if(marble_npa_attempted || marble_npa_dirty || marble_npa_active || marble_smp2p_attempted ||
       marble_smp2p_dirty || marble_pil_attempted || marble_cap.active || marble_cap.dirty)return 0;
    unsigned qmp=marble_qmp_inventory(),npa=marble_power_resource_inventory();
    print("marble-adsp-power: complete PASS=%d qmp=%d resources=%d clock_enable=0 clock_disable=0 votes=0 PAS_calls=0 DSP_start=0 storage_access=0\n",(uint64_t)(qmp && npa),(uint64_t)qmp,(uint64_t)npa);
    return qmp && npa;
}
#endif
#endif
