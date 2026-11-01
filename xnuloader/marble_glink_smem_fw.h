/* Read-only qualification of the preallocated native lpass transport.
 * Does not dispatch RX, allocate SMEM, advance a queue, open a channel or boot DSP. */
#ifndef MARBLE_GLINK_SMEM_FW_H
#define MARBLE_GLINK_SMEM_FW_H
#ifdef XNU_MARBLE
#include "marble_glink_smem_code.h"
static struct {
    unsigned qualified;
    uint64_t base,transport,core,descriptor,local_fifo,partition,partition_bytes,native[46];
    MarbleSmemFirmware *provider;
} marble_glink_smem;
static int marble_glink_smem_info(void)
{
    if(marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted || marble_icb_attempted ||
       marble_rpmh_attempted || marble_qmp_native.attempted || marble_glink_observer.attempted ||
       marble_cap.active || marble_cap.dirty || marble_thermal_latched)return 0;
    static const efi_guid guid={0x3bfcf64d,0x00cd,0x41cb,{0xbe,0xa5,0xa7,0x6c,0x3a,0x24,0x3d,0xed}};
    MarbleGlinkFirmware *provider=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&provider)) || !provider || provider->revision!=0x10001)return 0;
    uint64_t base=(uint64_t)(uintptr_t)provider-0xd0a8,t=0,config=0,count=0,word=0,core=0;
    if((uint64_t)(uintptr_t)provider->slot[2]!=base+0x14bc || (uint64_t)(uintptr_t)provider->slot[13]!=base+0x17ac ||
       !marble_npa_ram_word(base+0xb91c,&count) || (uint32_t)count!=5 ||
       !marble_npa_ram_word(base+0xd698,&t) || !t ||
       !marble_npa_ram_word(t+0xb8,&config) || config!=base+0xd2a8)return 0;
    uint64_t initial[46];
    if(!marble_rpmh_words(marble_npa_ram_word,t,initial,46))return 0;
#ifndef XNU_MARBLE_GPR_ONLY
    print("marble-glink-smem: observed transport=%x config=%x core=%x TX_descriptor=%x RX_descriptor=%x TX_FIFO=%x RX_FIFO=%x TX_size=%d RX_size=%d MTU=%d cached_RX_tail=%d cached_TX_head=%d\n",
        t,config,initial[0x98/8],initial[0xd0/8],initial[0xd8/8],initial[0xe0/8],initial[0xe8/8],
        (uint64_t)(uint32_t)initial[0xf0/8],(uint64_t)(uint32_t)(initial[0xf0/8]>>32),
        (uint64_t)(uint32_t)initial[0xf8/8],(uint64_t)(uint32_t)(initial[0xf8/8]>>32),(uint64_t)(uint32_t)initial[0x100/8]);
#endif
    for(unsigned i=0;i<sizeof(marble_glink_smem_glink_code)/sizeof(*marble_glink_smem_glink_code);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_glink_smem_glink_code[i].rva,
            marble_glink_smem_glink_code[i].bytes,marble_glink_smem_glink_code[i].digest)) {
            print("marble-glink-smem: refused step=code_digest range=%d rva=%x\n",(uint64_t)i,(uint64_t)marble_glink_smem_glink_code[i].rva);return 0;
        }
    static const unsigned offsets[]={8,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x48,0x50,0x58,0x60,0x68,0x70,0x78,0x80,0x88};
    static const unsigned rvas[]={0x53fc,0x5410,0x5b38,0x542c,0x547c,0x5494,0x54ac,0x59ac,0x5a48,0x5504,0x554c,0x5574,0x54c4,0x54e0,0x5990,0x5a64,0x5c30};
    for(unsigned i=0;i<17;i++)if(!marble_npa_ram_word(t+offsets[i],&word) || word!=base+rvas[i]) {
        print("marble-glink-smem: refused step=transport_slot slot=%d observed=%x expected=%x\n",(uint64_t)i,word,base+rvas[i]);return 0;
    }
#ifndef XNU_MARBLE_GPR_ONLY
    print("marble-glink-smem: progress step=code_and_slots PASS=1\n");
#endif
    char name[97];
    if(!marble_npa_ram_word(config,&word) || !marble_proxy_ram_string(word,name) || !str_eq(name,"lpass") ||
       !marble_npa_ram_word(config+8,&word) || (uint32_t)word!=2 || (uint32_t)(word>>32)!=16384 ||
       !marble_npa_ram_word(config+0x10,&word) || (uint32_t)word!=4096 || (uint32_t)(word>>32)!=3 ||
       !marble_npa_ram_word(config+0x18,&word) || word ||
       !marble_npa_ram_word(t+0x98,&core) || !core) {
        print("marble-glink-smem: refused step=config last_word=%x\n",word);return 0;
    }
    /* Native init at 0x6904 supplies the case-sensitive string at 0xb871. */
    if(!marble_npa_ram_word(core,&word) || word!=base+0xb871 || !marble_proxy_ram_string(word,name) || !str_eq(name,"SMEM") ||
       !marble_npa_ram_word(core+8,&word) || !marble_proxy_ram_string(word,name) || !str_eq(name,"lpass") ||
       !marble_npa_ram_word(core+0x30,&word) || word) {
        print("marble-glink-smem: refused step=core last_word=%x\n",word);return 0;
    }
#ifndef XNU_MARBLE_GPR_ONLY
    print("marble-glink-smem: progress step=config_and_core PASS=1\n");
#endif
    static const efi_guid smem_guid={0xf4e5c7d0,0xd239,0x47cb,{0xaa,0xcd,0x7f,0x66,0xef,0x76,0x32,0x38}};
    MarbleSmemFirmware *smem=NULL;
    if(EFI_ERROR(BS->locate_protocol(&smem_guid,NULL,(void **)&smem)) || !smem || smem->revision!=0x10001) {
        print("marble-glink-smem: refused step=smem_protocol\n");return 0;
    }
    uint64_t sb=(uint64_t)(uintptr_t)smem-0x8098,partition=0,geometry=0;
    if((uint64_t)(uintptr_t)smem->get_ex!=sb+0x14c8 || (uint64_t)(uintptr_t)smem->allocate_ex!=sb+0x14b4 ||
       !marble_npa_ram_word(sb+0x8208,&word) || (uint32_t)word!=3 ||
       !marble_npa_ram_word(base+0xd6a8,&word) || word!=(uint64_t)(uintptr_t)smem ||
       !marble_npa_ram_word(sb+0x8270,&partition) || !marble_npa_ram_word(sb+0x8278,&geometry)) {
        print("marble-glink-smem: refused step=smem_state last_word=%x\n",word);return 0;
    }
    uint64_t bytes=(uint32_t)geometry;
    if(partition<0x80901000 || partition>0x80aff000 || bytes!=0x30000 || partition+bytes>0x80aff000) {
        print("marble-glink-smem: refused step=partition partition=%x bytes=%d geometry=%x\n",partition,bytes,geometry);return 0;
    }
    print("marble-glink-smem: qualified native_ranges=4 transport_slots=17 transport_count=5 stride=170 index=0 config_rva=d2a8 remote_pid=2 FIFO_config=16384 MTU=4096 IPCC_client=3 signal=0 partition=%x partition_bytes=%d allocator_calls=0 RX_dispatch=0\n",partition,bytes);
    MarbleSmemGetParams items[3]={{2,478,0,0,NULL,0x40000000,0},{2,479,0,0,NULL,0x40000000,0},{2,480,0,0,NULL,0x40000000,0}};
    for(unsigned i=0;i<3;i++) {
        efi_status status=smem->get_ex(&items[i]);uint64_t p=(uint64_t)(uintptr_t)items[i].address;
        unsigned valid=i<2?(!EFI_ERROR(status) && items[i].size==(i?16384u:32u) && p>=partition+32 &&
            p<=partition+bytes-items[i].size && !(p&7)):(status==(EFI_ERROR_BIT|14) && !p && !items[i].size);
        print("marble-glink-smem: get item=%d status=%x address=%x bytes=%d PASS=%d flags=%x allocator_calls=0 FIFO_reads=0\n",
            (uint64_t)items[i].item,status,p,(uint64_t)items[i].size,(uint64_t)valid,(uint64_t)items[i].flags);
        if(!valid || items[i].flags!=0x40000000)return 0;
        if(i<2)for(uint64_t at=p&~4095ULL;at<p+items[i].size;at+=4096) {
            uint64_t par=marble_fw_translation(at);
            if((par&1) || (par&0x0000fffffffff000ULL)!=at || (par>>56)!=0x44)return 0;
        }
    }
    uint64_t descriptor_address=(uint64_t)(uintptr_t)items[0].address,fifo_address=(uint64_t)(uintptr_t)items[1].address;
    if(!(descriptor_address+32<=fifo_address || fifo_address+16384<=descriptor_address))return 0;
    uint64_t snapshot[46];
    if(!marble_rpmh_words(marble_npa_ram_word,t,snapshot,46))return 0;
    unsigned descriptor=snapshot[0xd0/8]==(uint64_t)(uintptr_t)items[0].address &&
        snapshot[0xd8/8]==(uint64_t)(uintptr_t)items[0].address+8;
    unsigned fifo=snapshot[0xe0/8]==(uint64_t)(uintptr_t)items[1].address && !snapshot[0xe8/8] &&
        (uint32_t)snapshot[0xf0/8]==16384 && !(uint32_t)(snapshot[0xf0/8]>>32);
    uint32_t first[8],last[8];volatile uint32_t *wire=items[0].address;
    for(unsigned i=0;i<8;i++)first[i]=wire[i];
    __asm__ volatile("dmb sy" ::: "memory");BS->stall(100);
    for(unsigned i=0;i<8;i++)last[i]=wire[i];
    unsigned stable=!memcmp(first,last,sizeof(first));
    /* Only the AP's TX head and RX tail must be at their boot baseline.
     * The DSP-owned positions can contain history until DSP publishes its
     * new FIFO; recording that history does not qualify an RX queue. */
    unsigned own_zero=!first[1] && !first[2];
    uint32_t tx_head=(uint32_t)snapshot[0x100/8],rx_tail=(uint32_t)(snapshot[0xf8/8]>>32);
    unsigned cached=tx_head==first[1] && rx_tail==first[2];
    unsigned listeners=marble_qmp_native_passive_listeners(base);
    uint64_t after[46];unsigned unchanged=marble_rpmh_words(marble_npa_ram_word,t,after,46) && !memcmp(snapshot,after,sizeof(snapshot));
    unsigned ok=descriptor && fifo && stable && own_zero && cached && listeners && unchanged;
    if(ok) {
        marble_glink_smem.base=base;marble_glink_smem.transport=t;marble_glink_smem.core=core;
        marble_glink_smem.descriptor=descriptor_address;marble_glink_smem.local_fifo=fifo_address;
        marble_glink_smem.partition=partition;marble_glink_smem.partition_bytes=bytes;
        marble_glink_smem.provider=smem;memcpy(marble_glink_smem.native,after,sizeof(after));
        marble_glink_smem.qualified=1;
    }
    print("marble-glink-smem: geometry descriptor=%d FIFO_binding=%d TX_size=%d RX_size=%d RX_missing=1 descriptor_stable=%d own_indices_zero=%d remote_indices_zero=%d cached_indices=%d cached_TX_head=%d cached_RX_tail=%d indices=%x,%x,%x,%x transport_unchanged=%d listeners=%d runtime_RX_qualified=0\n",
        (uint64_t)descriptor,(uint64_t)fifo,(uint64_t)(uint32_t)snapshot[0xf0/8],(uint64_t)(uint32_t)(snapshot[0xf0/8]>>32),
        (uint64_t)stable,(uint64_t)own_zero,(uint64_t)(!first[0] && !first[3]),(uint64_t)cached,(uint64_t)tx_head,(uint64_t)rx_tail,
        (uint64_t)first[0],(uint64_t)first[1],(uint64_t)first[2],(uint64_t)first[3],(uint64_t)unchanged,(uint64_t)listeners);
    print("marble-glink-smem: complete PASS=%d initialized_SMEM=1 private_GET_calls=3 allocator_calls=0 descriptor_reads=64 FIFO_reads=0 RX_dispatch=0 channel_open=0 TX_calls=0 IRQ_writes=0 PAS_calls=0 DSP_start=0 storage_access=0 audio_acceptance=0\n",(uint64_t)ok);
    return ok;
}
#endif
#endif
