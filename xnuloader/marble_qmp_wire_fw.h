/* Adapt the pinned Linux qcom_aoss.c mailbox protocol to an already-UP link.
 * Borrow only the empty AP channel/mailbox. No GLink open/close/event callback,
 * function-table patch, remote-owned descriptor write, PAS or DSP execution.
 * The original Mu doorbell routine is reused with its fixed configuration. */
#ifndef MARBLE_QMP_WIRE_FW_H
#define MARBLE_QMP_WIRE_FW_H
#ifdef XNU_MARBLE
static struct {
    uint64_t base,clock,transport[38],core[19],clock_flags,start,freq,daif;
    MarbleQmpNativeContext context;
    MarbleQmpDescriptor original;
    unsigned attempted,passed,writes,kicks,acks,close_acks;
} marble_qmp_wire;
static unsigned marble_qmp_wire_owned_authorized;
static int (*marble_qmp_pre_open_hook)(void);
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
static uint64_t marble_qmp_outer_deadline;
static void marble_qmp_shutdown_deadline(uint64_t ticks){marble_qmp_outer_deadline=ticks;}
static int marble_qmp_outer_time_left(void)
{return !marble_qmp_outer_deadline || READ_SYSREG(CNTVCT_EL0)<marble_qmp_outer_deadline;}
#else
#define marble_qmp_outer_time_left() 1
#endif
#ifdef XNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT
#include "marble_qmp_close_snapshot.h"
static unsigned marble_qmp_close_reads,marble_qmp_close_unstable,marble_qmp_close_failed;
static int marble_qmp_wire_same(void);
static int marble_qmp_close_read(void *ctx,MarbleQmpDescriptor *out,unsigned *empty)
{
    (void)ctx;marble_qmp_close_reads++;
    if(!marble_qmp_wire_same())return 0;
    *empty=marble_qmp_native_descriptor(out);
    __asm__ volatile("dmb sy":::"memory");
    return marble_qmp_wire_same()?1:0;
}
#endif
static int marble_qmp_wire_heap_same(void)
{
    if(!marble_qmp_wire.base || !marble_qmp_wire.context.transport || !marble_qmp_wire.context.core)return 0;
    uint64_t words[38],transport=0;
    if(!marble_npa_ram_word(marble_qmp_wire.base+0xd6b0,&transport) || transport!=marble_qmp_wire.context.transport ||
       !marble_rpmh_words(marble_npa_ram_word,transport,words,38) || memcmp(words,marble_qmp_wire.transport,sizeof(words)) ||
       !marble_rpmh_words(marble_npa_ram_word,marble_qmp_wire.context.core,words,19) || memcmp(words,marble_qmp_wire.core,sizeof(marble_qmp_wire.core)))return 0;
    return 1;
}
static int marble_qmp_wire_same(void)
{
    uint64_t clock=0;
    return marble_qmp_wire_heap_same() && marble_npa_ram_word(marble_qmp_wire.clock+0x2ece0,&clock) &&
        clock==marble_qmp_wire.clock_flags && READ_SYSREG(DAIF)==marble_qmp_wire.daif;
}
static int marble_qmp_wire_preserved(void)
{
    MarbleQmpDescriptor current;
    return marble_qmp_wire.passed && marble_qmp_wire_same() && marble_qmp_native_descriptor(&current) &&
        !memcmp(&current,&marble_qmp_wire.original,sizeof(current));
}
static void marble_qmp_wire_kick(void)
{
    typedef void (EFIAPI *Kick)(const void *);
    __asm__ volatile("dsb sy" ::: "memory");
    ((Kick)(uintptr_t)(marble_qmp_wire.base+0x4e90))((const void *)(uintptr_t)(marble_qmp_wire.base+0xbb28));
    marble_qmp_wire.kicks++;
}
static uint64_t marble_qmp_wire_us(void)
{return (READ_SYSREG(CNTVCT_EL0)-marble_qmp_wire.start)*1000000/marble_qmp_wire.freq;}
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
static void marble_qmp_timeline(const char *phase)
{print("aud-qmp-timeline: phase=%s elapsed_us=%d read_only_counter=1\n",phase,marble_qmp_wire_us());}
#endif
static void marble_qmp_wire_descriptor_log(const char *phase)
{
    MarbleQmpDescriptor d={0};
    unsigned empty=marble_qmp_native_descriptor(&d);
    print("marble-qmp-wire: descriptor phase=%s valid_empty=%d local_link=%x local_link_ack=%x local_channel=%x local_channel_ack=%x remote_link=%x remote_link_ack=%x remote_channel=%x remote_channel_ack=%x\n",
        phase,(uint64_t)empty,(uint64_t)d.local_link,(uint64_t)d.local_link_ack,
        (uint64_t)d.local_channel,(uint64_t)d.local_channel_ack,(uint64_t)d.remote_link,
        (uint64_t)d.remote_link_ack,(uint64_t)d.remote_channel,(uint64_t)d.remote_channel_ack);
}
static int marble_qmp_wire_wait(unsigned opened,uint64_t deadline)
{
    MarbleQmpDescriptor current;
    while(marble_qmp_wire_us()<deadline && marble_qmp_outer_time_left()) {
        if(marble_qmp_native_descriptor(&current) &&
           (opened?marble_qmp_open(&current):!memcmp(&current,&marble_qmp_wire.original,sizeof(current))))return 1;
        BS->stall(1000);
    }
    return 0;
}
static int marble_qmp_wire_send(unsigned index)
{
    MarbleQmpDescriptor current;
    if(index>1 || marble_qmp_wire.acks!=index || !marble_qmp_wire_same() ||
       !marble_qmp_native_descriptor(&current) || !marble_qmp_open(&current))return 0;
    volatile uint32_t *mailbox=(volatile uint32_t *)(uintptr_t)(0xc300000+current.local_offset);
    const uint32_t *message=(const uint32_t *)(const void *)marble_qmp_load_message[index];
    uint64_t begin=marble_qmp_wire_us();
    print("marble-qmp-wire: send_before index=%d value=%s bytes=64 AP_mailbox=%x\n",(uint64_t)index,index?"off":"on",(uint64_t)(uintptr_t)mailbox);
    /* Message RAM only supports 32-bit accesses. Publish the length last;
     * read back the length exactly as Linux qmp_send does before ringing. */
    for(unsigned i=0;i<16;i++) {mailbox[i+1]=message[i];marble_qmp_wire.writes++;}
    __asm__ volatile("dsb sy" ::: "memory");
    mailbox[0]=64;marble_qmp_wire.writes++;
    uint32_t published=mailbox[0];
    marble_qmp_wire_kick();
    unsigned ack=published==64 && marble_qmp_wire_wait(1,begin+250000);
    if(ack)marble_qmp_wire.acks++;
    uint64_t us=marble_qmp_wire_us()-begin;
    print("marble-qmp-wire: send_complete index=%d PASS=%d published=%d ACKs=%d elapsed_us=%d physical_readback=0 GLink_TX_calls=0\n",
        (uint64_t)index,(uint64_t)ack,(uint64_t)published,(uint64_t)marble_qmp_wire.acks,us);
    return ack;
}
static int marble_qmp_wire_close(void)
{
    uint64_t deadline=marble_qmp_wire_us()+750000;
    while(marble_qmp_wire_us()<deadline && marble_qmp_outer_time_left()) {
        MarbleQmpDescriptor d;
#ifdef XNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT
        int snapshot=marble_qmp_close_snapshot(marble_qmp_close_read,NULL,&d);
        if(snapshot<0){marble_qmp_close_failed=2;return 0;}
        if(!snapshot){marble_qmp_close_unstable++;BS->stall(1000);continue;}
#else
        if(!marble_qmp_wire_same() || !marble_qmp_native_descriptor(&d))return 0;
#endif
        int step=marble_qmp_borrowed_close_step(&d,&marble_qmp_wire.original,marble_qmp_wire.close_acks);
        if(step<0) {
#ifdef XNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT
            marble_qmp_close_failed=3;
            print("aud-qmp-close-reject: acks=%d local_channel=%x local_ack=%x remote_channel=%x remote_ack=%x writes=0\n",
                (uint64_t)marble_qmp_wire.close_acks,(uint64_t)d.local_channel,(uint64_t)d.local_channel_ack,
                (uint64_t)d.remote_channel,(uint64_t)d.remote_channel_ack);
#endif
            return 0;
        }
        if(step==3)return !memcmp(&d,&marble_qmp_wire.original,sizeof(d));
        if(step) {
            /* The ACK at 0x18 is AP-owned, despite its remote_channel name.
             * Mirror only the AOP state just observed, as the native QMP
             * event at 7460/7480 and 7650/7660 does, without core callbacks. */
            volatile uint32_t *ack=(volatile uint32_t *)(uintptr_t)(0xc300000+offsetof(MarbleQmpDescriptor,remote_channel_ack));
            *ack=d.remote_channel;marble_qmp_wire.writes++;marble_qmp_wire.close_acks++;
            marble_qmp_wire_kick();
            print("marble-qmp-wire: close_ack index=%d observed_remote=%x AP_ack=%x offset=18 remote_state_writes=0\n",
                (uint64_t)(marble_qmp_wire.close_acks-1),(uint64_t)d.remote_channel,(uint64_t)*ack);
        }
        BS->stall(1000);
    }
#ifdef XNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT
    marble_qmp_close_failed=4;
#endif
    return 0;
}
static int marble_qmp_wire_open_mode(unsigned owned,unsigned messages)
{
    if(owned>1 || messages>1 || (!owned && !messages) || marble_qmp_wire_owned_authorized!=(owned?(messages?1u:2u):0u))return 0;
    if(owned && (!marble_pil_header_only || !marble_pil_attempted || !marble_pil_dsp_dirty ||
       !marble_pil_deferred.saved || !marble_pil_deferred.verified || marble_pil_auth_attempted ||
       !marble_smp2p_owned || !marble_smp2p_notifications || marble_npa_active!=2 || !marble_npa_dirty ||
       !marble_icb_scm_owned_authorized || !marble_icb_attempted || !marble_icb_dirty ||
       !marble_rpmh_attempted || !marble_rpmh_dirty || !marble_xo_rpmh_attempted))return 0;
    if(marble_qmp_wire.attempted || marble_qmp_native.attempted ||
       (!owned && (marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted || marble_icb_attempted || marble_rpmh_attempted)) || marble_glink_observer.attempted ||
       marble_cap.active || marble_cap.dirty || marble_display_bus.active || marble_display_bus.dirty || marble_thermal_latched)return 0;
    if(!marble_qmp_inventory())return 0;
    static const efi_guid guid={0x3bfcf64d,0x00cd,0x41cb,{0xbe,0xa5,0xa7,0x6c,0x3a,0x24,0x3d,0xed}};
    MarbleGlinkFirmware *provider=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&provider)) || !provider || provider->revision!=0x10001)return 0;
    uint64_t base=(uint64_t)(uintptr_t)provider-0xd0a8,word=0;
    if((uint64_t)(uintptr_t)provider->slot[2]!=base+0x14bc || (uint64_t)(uintptr_t)provider->slot[13]!=base+0x17ac)return 0;
    for(unsigned i=0;i<sizeof(marble_qmp_glink_code)/sizeof(*marble_qmp_glink_code);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_qmp_glink_code[i].rva,
            marble_qmp_glink_code[i].bytes,marble_qmp_glink_code[i].digest))return 0;
    MarbleQmpNativeContext *context=&marble_qmp_wire.context;
    MarbleQmpDescriptor *original=&marble_qmp_wire.original;
    if(!marble_qmp_native_context(base,context,"wire-before") || context->low_state!=1 || context->core_state!=1 || context->count ||
       !marble_qmp_native_descriptor(original) || original->features ||
       original->local_link!=MARBLE_QMP_UP || original->local_link_ack!=MARBLE_QMP_UP ||
       original->remote_link!=MARBLE_QMP_UP || original->remote_link_ack!=MARBLE_QMP_UP ||
       original->local_channel!=MARBLE_QMP_DOWN || original->local_channel_ack!=MARBLE_QMP_DOWN ||
       original->remote_channel!=MARBLE_QMP_UP || original->remote_channel_ack!=MARBLE_QMP_UP ||
       !marble_qmp_native_passive_listeners(base) || !marble_qmp_clock_module)return 0;
    marble_qmp_wire.base=base;marble_qmp_wire.clock=marble_qmp_clock_module;
    if(!marble_npa_ram_word(base+0xbb28,&word) || word ||
       !marble_npa_ram_word(base+0xd688,&word) || !word ||
       !marble_npa_ram_word(base+0xd690,&word) || !word ||
       !marble_rpmh_words(marble_npa_ram_word,context->transport,marble_qmp_wire.transport,38) ||
       !marble_rpmh_words(marble_npa_ram_word,context->core,marble_qmp_wire.core,19) ||
       !marble_npa_ram_word(marble_qmp_wire.clock+0x2ece0,&marble_qmp_wire.clock_flags))return 0;
    MarbleThermalSample thermal;
    if(!marble_adc_channels(1) || marble_adc_last_temperature>=38 ||
       !marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=50000)return 0;
    marble_qmp_wire.daif=READ_SYSREG(DAIF);
    if(!marble_qmp_wire_same())return 0;
    if(marble_qmp_pre_open_hook) {
        int (*hook)(void)=marble_qmp_pre_open_hook;marble_qmp_pre_open_hook=NULL;
        if(!hook() || !marble_qmp_wire_same() ||
           !marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=50000)return 0;
    }
    print("marble-qmp-wire: mode load_state_messages=%d owned=%d PAS_inside_wire=0\n",(uint64_t)(messages?2:0),(uint64_t)owned);
    print("marble-qmp-wire: qualified native_code_ranges=13 features=0 local_offset=%x local_bytes=%d remote_offset=%x remote_bytes=%d native_low=1 native_core=1 native_channels=0 Clock_flags=%x DAIF=%x GLink_calls=0\n",
        (uint64_t)original->local_offset,(uint64_t)original->local_size,(uint64_t)original->remote_offset,(uint64_t)original->remote_size,marble_qmp_wire.clock_flags,marble_qmp_wire.daif);
    if(EFI_ERROR(BS->set_watchdog_timer(10,0,0,NULL)))return 0;
    marble_qmp_wire.attempted=marble_qmp_native.attempted=marble_qmp_native.dirty=marble_firmware_reset_required=1;
    marble_qmp_wire.start=READ_SYSREG(CNTVCT_EL0);marble_qmp_wire.freq=READ_SYSREG(CNTFRQ_EL0);
    volatile uint32_t *local_channel=(volatile uint32_t *)(uintptr_t)(0xc300000+offsetof(MarbleQmpDescriptor,local_channel));
    *local_channel=MARBLE_QMP_UP;marble_qmp_wire.writes++;
    marble_qmp_wire_kick();
    unsigned ok=marble_qmp_wire_wait(1,750000) && marble_qmp_wire_same();
    print("marble-qmp-wire: opened PASS=%d native_context_unchanged=%d\n",(uint64_t)ok,(uint64_t)marble_qmp_wire_same());
    return ok;
}
static int marble_qmp_wire_finish_mode(unsigned messages,unsigned ok)
{
    if(messages>1 || !marble_qmp_wire.attempted || marble_qmp_wire.passed ||
       !marble_qmp_wire_same())return 0;
    MarbleQmpDescriptor *original=&marble_qmp_wire.original;
    volatile uint32_t *local_channel=(volatile uint32_t *)(uintptr_t)(0xc300000+offsetof(MarbleQmpDescriptor,local_channel));
    MarbleThermalSample thermal;
    if(ok) {
        marble_qmp_wire_descriptor_log("close-before");
        *local_channel=MARBLE_QMP_DOWN;marble_qmp_wire.writes++;
        marble_qmp_wire_kick();
#ifdef XNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT
        uint64_t close_begin=marble_qmp_wire_us();
#endif
        ok=marble_qmp_wire_close();
#ifdef XNU_MARBLE_AUDIO_QMP_CLOSE_SNAPSHOT
        print("aud-qmp-close-snapshot: PASS=%d reads=%d unstable=%d failed=%d close_acks=%d elapsed_us=%d limit_us=750000 double_read=1 transition_validator=unchanged remote_owned_writes=0\n",
            (uint64_t)ok,(uint64_t)marble_qmp_close_reads,(uint64_t)marble_qmp_close_unstable,(uint64_t)marble_qmp_close_failed,
            (uint64_t)marble_qmp_wire.close_acks,marble_qmp_wire_us()-close_begin);
#endif
        marble_qmp_wire_descriptor_log("close-after");
    }
    /* Observe late callbacks before accepting the borrowed-channel return.
     * Host also reads the preservation getvar after command/log boundaries. */
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
    marble_qmp_timeline("close-before-dwell");
#endif
    if(ok)BS->stall(100000);
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
    marble_qmp_timeline("close-after-dwell");
#endif
    MarbleQmpDescriptor after;
    unsigned descriptor=marble_qmp_native_descriptor(&after) && !memcmp(original,&after,sizeof(after));
    unsigned cool=marble_adc_channels(1) && marble_adc_last_temperature<38 &&
        marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) && thermal.maximum_mc<55000;
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
    marble_qmp_timeline("close-after-ADC");
#endif
    unsigned context_same=marble_qmp_wire_heap_same();uint64_t clock_after=~0ULL,daif_after=READ_SYSREG(DAIF);
    unsigned clock_same=marble_npa_ram_word(marble_qmp_wire.clock+0x2ece0,&clock_after) && clock_after==marble_qmp_wire.clock_flags;
    unsigned daif_same=daif_after==marble_qmp_wire.daif;
    uint64_t us=marble_qmp_wire_us();
    ok=ok && descriptor && context_same && clock_same && daif_same && cool && us<2500000 && marble_qmp_wire.writes==(messages?38u:4u) && marble_qmp_wire.kicks==(messages?6u:4u) && marble_qmp_wire.acks==(messages?2u:0u) && marble_qmp_wire.close_acks==2;
    marble_qmp_wire.passed=ok;
    if(ok)marble_qmp_native.dirty=0;
    print("marble-qmp-wire: context native_transport_words=38 native_core_words=19 Clock_before=%x Clock_after=%x DAIF_before=%x DAIF_after=%x\n",marble_qmp_wire.clock_flags,clock_after,marble_qmp_wire.daif,daif_after);
    print("marble-qmp-wire: complete PASS=%d attempted=1 dirty=%d descriptor_restored=%d native_context_unchanged=%d Clock_unchanged=%d DAIF_unchanged=%d AP_writes=%d kicks=%d ACKs=%d elapsed_us=%d thermal_ok=%d watchdog_seconds=10 watchdog_retained=1 cold_reset_required=1 GLink_calls=0 native_event_calls=0 remote_owned_writes=0 table_writes=0 PAS_calls=0 NPA_calls=0 DSP_start=0 storage_access=0 AP_payload_history_retained=%d physical_load_state_readback=0\n",
        (uint64_t)ok,(uint64_t)marble_qmp_native.dirty,(uint64_t)descriptor,(uint64_t)context_same,(uint64_t)clock_same,(uint64_t)daif_same,
        (uint64_t)marble_qmp_wire.writes,(uint64_t)marble_qmp_wire.kicks,(uint64_t)marble_qmp_wire.acks,us,(uint64_t)cool,(uint64_t)messages);
    return ok;
}
static int marble_qmp_wire_cycle_mode(unsigned owned,unsigned messages)
{
    unsigned ok=marble_qmp_wire_open_mode(owned,messages);
    if(ok && messages)ok=marble_qmp_wire_send(0);
    if(ok && messages) {BS->stall(10000);ok=marble_qmp_wire_send(1);}
    return marble_qmp_wire.attempted?marble_qmp_wire_finish_mode(messages,ok):0;
}
static int marble_qmp_wire_cycle(void)
{return marble_qmp_wire_cycle_mode(0,1);}
static void (*marble_qmp_tpl_progress)(const char *,uint64_t);
static unsigned marble_qmp_tpl_attempted;
static int marble_qmp_tpl_roundtrip(unsigned index)
{
    /* Exact Call2 wrapper 3cb4/3d14 uses RaiseTPL(8)/RestoreTPL(old).
     * Exercise that event boundary alone; do not call SCM or alter any table. */
    typedef uint64_t (EFIAPI *Raise)(uint64_t);
    typedef void (EFIAPI *Restore)(uint64_t);
    if(!BS->raise_tpl || !BS->restore_tpl || !marble_qmp_tpl_progress)return 0;
    marble_qmp_tpl_progress("QMP-TPL-before",index);
    uint64_t begin=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0),daif=READ_SYSREG(DAIF);
    uint64_t old=((Raise)BS->raise_tpl)(8);
    marble_qmp_tpl_progress(index?"QMP-TPL-post-raised-old":"QMP-TPL-baseline-raised-old",old);
    ((Restore)BS->restore_tpl)(old);
    marble_qmp_tpl_progress("QMP-TPL-restored",index);
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-begin)*1000000/freq,after=READ_SYSREG(DAIF);
    unsigned ok=old<=8 && after==daif && us<250000;
    print("marble-qmp-tpl: roundtrip index=%d PASS=%d requested=8 previous=%d DAIF_before=%x DAIF_after=%x elapsed_us=%d raises=1 restores=1 direct_DAIF_writes=0 SMC=0 PAS=0\n",
        (uint64_t)index,(uint64_t)ok,old,daif,after,us);
    return ok;
}
static int marble_qmp_tpl_cycle(void)
{
    if(marble_qmp_tpl_attempted || marble_qmp_native.attempted || marble_pil_attempted || marble_npa_attempted ||
       marble_icb_attempted || marble_rpmh_attempted || marble_smp2p_attempted || marble_glink_observer.attempted ||
       marble_cap.active || marble_cap.dirty || marble_thermal_latched)return 0;
    if(EFI_ERROR(BS->set_watchdog_timer(10,0,0,NULL)))return 0;
    marble_qmp_tpl_attempted=marble_firmware_reset_required=1;
    unsigned baseline=marble_qmp_tpl_roundtrip(0);
    unsigned wire=baseline && marble_qmp_wire_cycle();
    unsigned post=wire && marble_qmp_tpl_roundtrip(1);
    unsigned preserved=post && marble_qmp_wire_preserved();
    print("marble-qmp-tpl: complete PASS=%d baseline=%d wire=%d post=%d firmware_preserved=%d raises=%d restores=%d SMC=0 PAS=0 NPA=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1\n",
        (uint64_t)(baseline && wire && post && preserved),(uint64_t)baseline,(uint64_t)wire,(uint64_t)post,(uint64_t)preserved,
        (uint64_t)(wire?2:1),(uint64_t)(wire?2:1));
    return baseline && wire && post && preserved;
}
#endif
#endif
