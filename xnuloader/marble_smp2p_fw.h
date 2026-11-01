/* Isolated APPS-owned SMP2P preparation. Optional single audited notification;
 * no DSP startup. The default preparation path performs no mailbox write.
 * Uses the exact initialized Mu private-partition allocator, before any DSP
 * execution. Its lock hooks are stubs: never use it concurrently with a DSP.
 * SMEM cannot free an individual item; every allocation requires cold reset. */
#ifndef MARBLE_SMP2P_FW_H
#define MARBLE_SMP2P_FW_H
#ifdef XNU_MARBLE
#include "marble_smp2p.h"
static unsigned marble_smp2p_attempted,marble_smp2p_dirty;
static MarbleSmemFirmware *marble_smp2p_provider;
static MarbleSmp2pItem *marble_smp2p_owned;
static uint64_t marble_smp2p_partition,marble_smp2p_partition_bytes;
static unsigned marble_smp2p_ipcc_ready,marble_smp2p_notifications,marble_smp2p_remote_seen;
static int marble_smp2p_ipcc_qualified_mode(unsigned verbose)
{
    /* Resident IPCCDxe e91f717f...bf30c1, protocol and initialized config
     * inventory only. No protocol call, register read, IRQ or clock change. */
    static const efi_guid guid={0x52858ad7,0x8b16,0x4137,{0x9c,0x64,0xdf,0xe0,0x4d,0x94,0x2e,0x80}};
    uint64_t *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table || table[0]!=0x10000)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x90a8;
    static const unsigned rvas[]={0x14ec,0x14f0,0x14f4,0x14f8,0x14fc};
    for(unsigned i=0;i<5;i++)if(table[i+1]!=base+rvas[i])return 0;
    uint64_t root=0,array=0,count=0,mmio=0,flags=0;
    if(!marble_adc_ram_word(base+0x9240,&root) || root!=base+0x91f0 ||
       !marble_adc_ram_word(root,&array) || array!=base+0x9130 ||
       !marble_adc_ram_word(root+8,&count) || (uint32_t)count!=4 || (uint32_t)(count>>32)!=8 ||
       !marble_adc_ram_word(array+0x10,&mmio) || mmio!=0xed18000 ||
       !marble_adc_ram_word(array+0x28,&flags) || (flags&0xffff)!=0x101)return 0;
    uint64_t par=marble_fw_translation(mmio);
    unsigned mapped=!(par&1) && (par&0x0000fffffffff000ULL)==mmio;
    marble_smp2p_ipcc_ready=mapped;
    if(verbose)print("marble-smp2p-ipcc: qualified PASS=%d exact_table=1 resident_base=%x APPS_client=8 target_client=3 signal=2 send_offset=c protocol_calls=0 MMIO_reads=0 MMIO_writes=0\n",(uint64_t)mapped,mmio);
    return mapped;
}
static int marble_smp2p_ipcc_qualified(void)
{return marble_smp2p_ipcc_qualified_mode(1);}
static int marble_smp2p_kick(void)
{
    if(!marble_smp2p_ipcc_ready || !marble_smp2p_owned || !marble_smp2p_dirty || marble_smp2p_notifications>=8)return 0;
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)0xed1800c=0x30002;
    __asm__ volatile("dsb sy" ::: "memory");marble_smp2p_notifications++;
    return 1;
}
static int marble_smp2p_prepare(unsigned notify)
{
    if(marble_smp2p_attempted || marble_smp2p_dirty)return 0;
    marble_smp2p_attempted=1;
    if(notify && !marble_smp2p_ipcc_qualified())return 0;
    static const efi_guid guid={0xf4e5c7d0,0xd239,0x47cb,{0xaa,0xcd,0x7f,0x66,0xef,0x76,0x32,0x38}};
    MarbleSmemFirmware *c=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&c)) || !c)return 0;
    uint64_t base=(uint64_t)(uintptr_t)c-0x8098,region=0,region_geometry=0,ready=0,backend=0;
    if(c->revision!=0x10001 || (uint64_t)(uintptr_t)c->allocate!=base+0x1444 ||
       (uint64_t)(uintptr_t)c->get!=base+0x1470 || (uint64_t)(uintptr_t)c->allocate_ex!=base+0x14b4 ||
       (uint64_t)(uintptr_t)c->get_ex!=base+0x14c8 ||
       !marble_adc_ram_word(base+0x81e0,&region) || !marble_adc_ram_word(base+0x81f0,&region_geometry) ||
       !marble_adc_ram_word(base+0x8208,&ready) || (uint32_t)ready!=3 ||
       !marble_adc_ram_word(base+0x8200,&backend) || backend!=base+0x6448)return 0;
    static const unsigned rvas[]={0x2af0,0x2b50,0x2ba4,0x2df0};
    for(unsigned i=0;i<4;i++) {uint64_t value=0;if(!marble_adc_ram_word(backend+8*i,&value) || value!=base+rvas[i])return 0;}
    /* Match the allocator and the explicit no-op lock/unlock hooks. All reads
     * here target ordinary driver image/objects, not shared metadata pages. */
    uint64_t code=0,locks=0;
    if(!marble_npa_ram_word(base+0x2ba4,&code) || code!=0xa9015ff8a9bc7bfdULL ||
       !marble_adc_ram_word(base+0x35a0,&locks) || locks!=0xd65f03c0d65f03c0ULL)return 0;
    uint64_t private_object=base+0x8210+2*0x30,partition=0,geometry=0,offsets=0,counts=0;
    if(!marble_adc_ram_word(private_object,&partition) || !marble_adc_ram_word(private_object+8,&geometry))return 0;
    uint64_t region_bytes=(uint32_t)region_geometry,part_bytes=(uint32_t)geometry;
    if(region!=0x80900000 || !region_bytes || region_bytes>0x300000 ||
       partition<region+0x1000 || !part_bytes || part_bytes>0x100000 || partition>~0ULL-part_bytes ||
       partition+part_bytes>region+region_bytes-0x1000 || (partition&4095))return 0;
    MarbleSmemGetParams absent={2,MARBLE_SMP2P_OUTBOUND,0,0,NULL,0x40000000,0};
    efi_status missing=c->get_ex(&absent);
    if(missing!=(EFI_ERROR_BIT|14) || absent.address || absent.size)return 0;
    MarbleSmemGetParams incoming={2,MARBLE_SMP2P_INBOUND,0,0,NULL,0x40000000,0};
    if(c->get_ex(&incoming)!=(EFI_ERROR_BIT|14) || incoming.address || incoming.size)return 0;
    if(!marble_adc_ram_word(private_object+0x10,&offsets) || !marble_adc_ram_word(private_object+0x20,&counts))return 0;
    uint64_t free_before=(uint32_t)offsets,cached_before=(uint32_t)(offsets>>32);
    if(free_before<32 || (free_before&7) || cached_before>part_bytes || free_before+360>cached_before)return 0;
    MarbleSmemGetParams peers[2]={{2,478,0,0,NULL,0x40000000,0},{2,479,0,0,NULL,0x40000000,0}};
    for(unsigned i=0;i<2;i++)if(EFI_ERROR(c->get_ex(&peers[i])) || !peers[i].address || peers[i].size!=(i?16384:32))return 0;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38)return 0;
    MarbleThermalSample thermal;
    if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=50000)return 0;
    print("marble-smp2p: qualified exact_allocator=1 lock_hooks_stub=1 DSP_off_profile=1 local_item=429 remote_item=443 region=%x region_bytes=%d partition=%x partition_bytes=%d free_before=%d cached_before=%d payload_reads=0\n",region,region_bytes,partition,part_bytes,free_before,cached_before);
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    marble_smp2p_dirty=marble_firmware_reset_required=1;
    MarbleSmemGetParams q={2,MARBLE_SMP2P_OUTBOUND,sizeof(MarbleSmp2pItem),0,NULL,0x40000000,0};
    typedef efi_status (EFIAPI *Allocate)(MarbleSmemGetParams *);
    uint64_t began=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    efi_status allocated=((Allocate)c->allocate_ex)(&q);
    print("marble-smp2p: allocated status=%x item=%d bytes=%d address=%x flags=%x DSP_start=0 ipcc_writes=0\n",allocated,(uint64_t)q.item,(uint64_t)q.size,(uint64_t)(uintptr_t)q.address,(uint64_t)q.flags);
    if(EFI_ERROR(allocated) || q.item!=429 || q.remote!=2 || q.size!=344 || q.flags!=0x40000000 ||
       (uint64_t)(uintptr_t)q.address!=partition+free_before+16)return 0;
    MarbleSmemGetParams get={2,429,0,0,NULL,0x40000000,0};
    efi_status fetched=c->get_ex(&get);
    uint64_t after=0,after_counts=0;
    if(EFI_ERROR(fetched) || get.address!=q.address || get.size!=344 || get.flags!=0x40000000 ||
       !marble_adc_ram_word(private_object+0x10,&after) || (uint32_t)after!=free_before+360 ||
       (uint32_t)(after>>32)!=cached_before || !marble_adc_ram_word(private_object+0x20,&after_counts) ||
       (uint32_t)after_counts!=(uint32_t)counts+1 || (uint32_t)(after_counts>>32)!=(uint32_t)(counts>>32))return 0;
    for(uint64_t at=(uint64_t)(uintptr_t)q.address&~4095ULL;at<(uint64_t)(uintptr_t)q.address+340;at+=4096) {
        uint64_t par=marble_fw_translation(at);
        if((par&1) || (par&0x0000fffffffff000ULL)!=at || (par>>56)!=0x44)return 0;
    }
    MarbleSmp2pItem expected;marble_smp2p_initialize(&expected);
    uint32_t published=expected.version_features;expected.version_features&=~255u;
    memcpy(q.address,&expected,sizeof(expected));
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)((uint8_t *)q.address+4)=published;
    __asm__ volatile("dsb sy" ::: "memory");
    flush_dcache((uint64_t)(uintptr_t)q.address,(uint64_t)(uintptr_t)q.address+340);
    expected.version_features=published;
    MarbleSmp2pItem actual;memcpy(&actual,q.address,sizeof(actual));
    unsigned valid=!memcmp(&actual,&expected,sizeof(actual)) && marble_smp2p_valid(&actual,0,2);
    for(unsigned i=0;i<2;i++) {
        MarbleSmemGetParams peer={2,478+i,0,0,NULL,0x40000000,0};
        if(EFI_ERROR(c->get_ex(&peer)) || peer.address!=peers[i].address || peer.size!=peers[i].size)valid=0;
    }
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    print("marble-smp2p: prepared PASS=%d magic=%x version=%d local=%d remote=%d total=%d valid=%d master_value=%x free_delta=360 glink_items_unchanged=%d owned_bytes=340 payload_reads=340 payload_write_bytes=344\n",(uint64_t)valid,(uint64_t)actual.magic,(uint64_t)(actual.version_features&255),(uint64_t)actual.local_pid,(uint64_t)actual.remote_pid,(uint64_t)actual.total_entries,(uint64_t)actual.valid_entries,(uint64_t)actual.entries[0].value,(uint64_t)valid);
    unsigned kicks=0;
    if(valid) {
        marble_smp2p_provider=c;marble_smp2p_owned=q.address;
        marble_smp2p_partition=partition;marble_smp2p_partition_bytes=part_bytes;
    }
    if(valid && notify) {
        /* Exact marble DT client3/signal2 and pinned Linux qcom-ipcc.c.
         * SEND_ID is write-only; never invent a readback/delivery verdict. */
        kicks=marble_smp2p_kick();
        BS->stall(50000);
        memcpy(&actual,q.address,sizeof(actual));
        unsigned unchanged=!memcmp(&actual,&expected,sizeof(actual));
        unsigned cool=marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) && thermal.maximum_mc<55000;
        valid=kicks && unchanged && cool;
        print("marble-smp2p: notified PASS=%d issued=%d target_client=3 signal=2 register=ed1800c value=30002 owned_header_unchanged=%d thermal_ok=%d die_mc=%d delivery_verified=0 DSP_start=0 IRQ_changes=0\n",(uint64_t)valid,(uint64_t)kicks,(uint64_t)unchanged,(uint64_t)cool,(uint64_t)thermal.maximum_mc);
    }
    us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    print("marble-smp2p: complete PASS=%d attempted=%d dirty=%d elapsed_us=%d reset_required=1 watchdog_retained=1 DSP_start=0 ipcc_writes=%d audio_acceptance=0\n",(uint64_t)(valid && us<1000000),(uint64_t)marble_smp2p_attempted,(uint64_t)marble_smp2p_dirty,us,(uint64_t)kicks);
    return valid && us<1000000;
}
/* After startup use only native GET, never its lockless allocator. Follow only
 * the requested remote-owned item returned within the qualified APPS/ADSP
 * partition. Two equal bounded copies qualify a protocol header, not SMEM as
 * a whole. A changing/unpublished header is retried within the outer deadline. */
static int marble_smp2p_remote_snapshot(MarbleSmp2pItem *result,uint32_t *value)
{
    if(!marble_smp2p_provider || !marble_smp2p_owned || !result || !value)return -1;
    MarbleSmemGetParams q={2,443,0,0,NULL,0x40000000,0};
    efi_status status=marble_smp2p_provider->get_ex(&q);
    if(status==(EFI_ERROR_BIT|14) && !q.address && !q.size)return 0;
    uint64_t pa=(uint64_t)(uintptr_t)q.address;
    if(EFI_ERROR(status) || q.size!=344 || q.flags!=0x40000000 || (pa&7) ||
       pa<marble_smp2p_partition+32 || pa>marble_smp2p_partition+marble_smp2p_partition_bytes-344) {
        print("marble-smp2p-boot: remote_rejected status=%x bytes=%d flags=%x address=%x payload_reads=0\n",status,(uint64_t)q.size,(uint64_t)q.flags,pa);return -1;
    }
    for(uint64_t at=pa&~4095ULL;at<pa+340;at+=4096) {
        uint64_t par=marble_fw_translation(at);
        if((par&1) || (par&0x0000fffffffff000ULL)!=at || (par>>56)!=0x44)return -1;
    }
    MarbleSmp2pItem other;
    flush_dcache(pa,pa+340);memcpy(result,q.address,340);
    __asm__ volatile("dmb sy" ::: "memory");memcpy(&other,q.address,340);
    if(memcmp(result,&other,340) || !marble_smp2p_slave_value(result,value))return 0;
    if(!marble_smp2p_remote_seen) {
        marble_smp2p_remote_seen=1;
        print("marble-smp2p-boot: remote item=443 bytes=344 address=%x local=2 remote=0 stable_copy=1 bounded_reads=340 allocator_calls=0\n",pa);
    }
    return 1;
}
static int marble_smp2p_negotiate(const MarbleSmp2pItem *remote)
{
    MarbleSmp2pItem *out=marble_smp2p_owned;
    if(!out || !marble_smp2p_valid(out,0,2) || !marble_smp2p_valid(remote,2,0))return 0;
    uint32_t features=((out->version_features&remote->version_features)>>8)&1u;
    uint32_t next=1|(features<<8),flags=(out->flags&~2u)|((features && (remote->flags&1))?2u:0);
    if(out->version_features==next && out->flags==flags)return 1;
    out->version_features=next;out->flags=flags;
    clean_dcache((uint64_t)(uintptr_t)out,(uint64_t)(uintptr_t)out+340);
    int notified=marble_smp2p_kick();
    print("marble-smp2p-boot: negotiated features=%x restart_done=%d ack_bit=%d notified=%d\n",(uint64_t)features,(uint64_t)(remote->flags&1),(uint64_t)((flags>>1)&1),(uint64_t)notified);
    return notified;
}
static int marble_smp2p_thermal_guard(void)
{
    MarbleThermalSample t;
    return marble_thermal_snapshot(marble_thermal_read,NULL,&t) && t.maximum_mc<55000;
}
static int (*marble_smp2p_runtime_service)(void);
static unsigned marble_smp2p_stop_qualified;
static int marble_smp2p_request_stop(void (*trace)(const char *,uint64_t))
{
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(trace)trace("audio-amp-stop-enter",1);
#endif
    if(!marble_smp2p_owned || !marble_smp2p_notifications || !marble_smp2p_thermal_guard())return 0;
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
    if(trace)trace("audio-amp-stop-thermal",1);
#endif
    MarbleSmp2pStop state={0};MarbleSmp2pItem remote;uint32_t value=0;
    uint64_t frequency=READ_SYSREG(CNTFRQ_EL0),began=READ_SYSREG(CNTVCT_EL0);unsigned loops=0;
    while((READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency<2000000) {
        if(marble_smp2p_runtime_service && !marble_smp2p_runtime_service())return 0;
        int present=marble_smp2p_remote_snapshot(&remote,&value);
        if(present<0)return 0;
        if(present) {
            if(!marble_smp2p_negotiate(&remote) || !marble_smp2p_observe(&state,value))return 0;
            if(state.phase==1)break;
        }
        if(!(loops++%10) && !marble_smp2p_thermal_guard())return 0;
        BS->stall(10000);
    }
    uint64_t ready_us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    print("marble-smp2p-boot: ready PASS=%d ready_seen=%d value=%x wait_us=%d fatal=%d stale_ack=%d\n",(uint64_t)(state.phase==1),(uint64_t)state.ready_seen,(uint64_t)value,ready_us,(uint64_t)!!(value&1),(uint64_t)!!(value&8));
    if(state.phase!=1 || !marble_smp2p_mark_stop(&state))return 0;
    if(trace)trace("SMP2P-ready",value);
    volatile uint32_t *master=&marble_smp2p_owned->entries[0].value;
    if(*master || !marble_smp2p_name(marble_smp2p_owned->entries[0].name,"master-kernel"))return 0;
    *master=1;clean_dcache((uint64_t)(uintptr_t)master,(uint64_t)(uintptr_t)master+4);
    if(!marble_smp2p_kick())return 0;
    if(trace)trace("SMP2P-stop-requested",1);
    began=READ_SYSREG(CNTVCT_EL0);loops=0;
#ifdef XNU_MARBLE_AUDIO_FAST_STOP
    if(frequency<1000000 || frequency>1000000000)return 0;
    uint64_t thermal_at=began;
    print("aud-stop-policy: poll_us=100 timeout_us=5000000 thermal_interval_us=100000 counter_read_only=1 IRQ_masks_unchanged=1 fresh_ACK_required=1\n");
#endif
    while((READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency<5000000) {
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
#ifndef XNU_MARBLE_AUDIO_FAST_STOP
        if(!(loops%10) && trace)trace("aud-stop-poll",loops);
#endif
#endif
        if(marble_smp2p_runtime_service && !marble_smp2p_runtime_service())return 0;
        int present=marble_smp2p_remote_snapshot(&remote,&value);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
#ifndef XNU_MARBLE_AUDIO_FAST_STOP
        if(!(loops%10) && trace)trace("aud-stop-value",value);
#endif
#endif
        if(present<0)return 0;
        if(present) {
            if(!marble_smp2p_negotiate(&remote) || !marble_smp2p_observe(&state,value))return 0;
            if(state.phase==3)break;
        }
#ifdef XNU_MARBLE_AUDIO_FAST_STOP
        uint64_t now=READ_SYSREG(CNTVCT_EL0);
        if(!loops || now-thermal_at>=frequency/10) {
            if(!marble_smp2p_thermal_guard())return 0;
            thermal_at=READ_SYSREG(CNTVCT_EL0);
        }
        loops++;
        uint64_t poll_at=READ_SYSREG(CNTVCT_EL0),ticks=frequency/10000;
        while(READ_SYSREG(CNTVCT_EL0)-poll_at<ticks)__asm__ volatile("yield" ::: "memory");
#else
        if(!(loops++%10) && !marble_smp2p_thermal_guard())return 0;
        BS->stall(10000);
#endif
    }
    uint64_t stop_us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    print("marble-smp2p-boot: stop-ack PASS=%d ready_seen=%d stop_ack_seen=%d value=%x wait_us=%d\n",(uint64_t)(state.phase==3),(uint64_t)state.ready_seen,(uint64_t)state.stop_ack_seen,(uint64_t)value,stop_us);
    if(state.phase!=3)return 0;
    if(trace)trace("SMP2P-stop-ack",value);
    *master=0;clean_dcache((uint64_t)(uintptr_t)master,(uint64_t)(uintptr_t)master+4);
    unsigned cleared=marble_smp2p_kick() && !*master;
    marble_smp2p_stop_qualified=cleared;
    print("marble-smp2p-boot: stopped PASS=%d ready_seen=1 stop_ack_seen=1 stop_clear=%d notifications=%d PAS_shutdown_allowed=%d payload_access=0\n",(uint64_t)cleared,(uint64_t)cleared,(uint64_t)marble_smp2p_notifications,(uint64_t)cleared);
    return cleared;
}
#endif
#endif
