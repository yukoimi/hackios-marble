/* Optional RAM-only installation of the pinned marble native PIL provider.
 * AUTO is skipped in a local derivative. Explicit PAS metadata/memory probes
 * always shut down and never authenticate or execute the DSP. The failed full
 * ADSP cycle stays disabled; installation/config/file discovery never starts DSP.
 * A loader which attempted installation must reboot before any XNU run. */
#ifndef XNULOADER_MARBLE_PIL_FW_H
#define XNULOADER_MARBLE_PIL_FW_H
#ifdef XNU_MARBLE
#include "marble_pil_ramfs.h"
#include "marble_pil_segment.h"
#include "marble_pil_memory.h"
#ifdef XNU_MARBLE_AUDIO_OWNER
#include "marble_audio_runtime_bound.h"
#endif
#ifdef XNU_MARBLE_AUDIO_RUNTIME_SAMPLING
static int marble_tpl_sampler_arm(void);
static int marble_tpl_sampler_release(void);
static void marble_tpl_sampler_log(void);
#endif
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
static void marble_audio_owner_failure_wire(void);
#endif
static unsigned marble_pil_attempted;
static unsigned marble_pil_header_only;
static unsigned marble_pil_dsp_dirty;
static unsigned marble_pil_auth_attempted;
/* Only the new owned-dependency coordinator can arm secure DSP execution.
 * Historical v34/v38 entry points remain unreachable and unarmed. */
static unsigned marble_pil_owned_dependencies_ready;
/* Optional owned coordinator gate after PAS metadata/memory and ROM load.
 * Never enabled by the historical firmware probes. */
static int (*marble_pil_post_load_hook)(void);
/* Owned load_state lifecycle closes only after a verified PAS shutdown. */
static int (*marble_pil_post_shutdown_hook)(void);
#ifdef XNU_MARBLE_AUDIO_OWNER_NATIVE_LINK
static int marble_audio_owner_link_gate(void);
#endif
/* Only an explicitly qualified owned coordinator may install a bounded
 * shutdown adapter; legacy probes keep their original native Call2. */
static efi_status (*marble_pil_shutdown_adapter)(const uint64_t *,uint64_t *);
#ifdef XNU_MARBLE_AUDIO_USB_POLL_LEASE
static int marble_usb_poll_complete(void);
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
static void marble_qmp_shutdown_deadline(uint64_t ticks);
static void marble_qmp_timeline(const char *phase);
#endif
static void marble_usb_poll_log(void);
#endif
static unsigned marble_pil_reset_boot_qualified;
static void (*marble_pil_progress)(const char *,uint64_t);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
static void (*marble_pil_runtime_probe)(void);
static int (*marble_pil_runtime_complete)(void);
#ifdef XNU_MARBLE_AUDIO_DIGITAL_CAPTURE
static void (*marble_pil_log_capture)(void);
#endif
#endif
/* Only the new isolated powered-stage path defers restoration until PAS has
 * stopped. Retain this backup and all recovery state on uncertain shutdown. */
static struct { void *saved;uint64_t target,length;unsigned verified; } marble_pil_deferred;
static const efi_guid marble_pil_ram_partition={0x77a17e4a,0xe67f,0x438b,{0xb0,0x65,0xe9,0x54,0xb2,0x13,0xab,0x12}};
static int marble_pil_adsp_memory(void)
{
    const uint64_t first=0x9fd00000,last=0xa2e00000;
    static uint8_t map[65536];uint64_t bytes=sizeof(map),key,stride;uint32_t version;
    if(marble_pil_progress)marble_pil_progress("get-map",first);
    if(EFI_ERROR(BS->get_memory_map(&bytes,map,&key,&stride,&version)) || stride<40 || stride>bytes || bytes>sizeof(map))return 0;
    if(first<PAYLOAD_BASE+PAYLOAD_MAX_SIZE && PAYLOAD_BASE<last)return 0;
    uint64_t cursor=first;
    if(marble_pil_progress)marble_pil_progress("map-ready",bytes);
    while(cursor<last) {
        uint64_t next=cursor;
        for(uint64_t off=0;off+stride<=bytes;off+=stride) {
            const efi_memory_descriptor *d=(const void *)(map+off);
            uint64_t end=d->phys_start+(d->pages<<12);
            if(d->phys_start<=cursor && cursor<end) {
                if(d->type!=0)return 0;
                next=end<last?end:last;break;
            }
        }
        if(next==cursor)return 0;cursor=next;
    }
    unsigned attributes[256]={0};
    for(uint64_t pa=first;pa<last;pa+=4096) {
        if(marble_pil_progress && !((pa-first)&0x7fffff))marble_pil_progress("AT-checkpoint",pa);
        uint64_t par=marble_fw_translation(pa);
        if((par&1) || (par&0x0000fffffffff000ULL)!=pa)return 0;
        attributes[par>>56]++;
    }
    for(unsigned i=0;i<256;i++)if(attributes[i])print("marble-pil-adsp: PAR_attr=%x pages=%d raw_reads=0\n",(uint64_t)i,(uint64_t)attributes[i]);
    print("marble-pil-adsp: carveout=%x bytes=%x reserved_only=1 identity_mapped=1 disjoint_payload=1 raw_reads=0\n",first,last-first);
    return 1;
}
/* FAILED v10b: kept for offline audit; no USB dispatcher reaches this path. */
static __attribute__((unused)) int marble_pil_adsp_cycle(uint64_t *table)
{
    uint64_t base=(uint64_t)(uintptr_t)table-0x18198;
    if(table[0]!=0x10003 || table[2]!=base+0x171c || table[4]!=base+0x175c || table[6]!=base+0x17d8)return 0;
    if(!marble_pil_adsp_memory() || !marble_adc_channels(1) || marble_adc_last_temperature>=38)return 0;
    MarbleThermalSample thermal;
    if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=50000)return 0;
    typedef efi_status (EFIAPI *GetConfig)(const char16 *,void *);
    typedef efi_status (EFIAPI *LoadConfig)(void *);
    typedef efi_status (EFIAPI *Shutdown)(uint32_t);
    uint64_t words[35];memset(words,0,sizeof(words));uint8_t *config=(void *)words;
    efi_status status=((GetConfig)(uintptr_t)table[6])(u"FULL_ADSP2",config);
    static const efi_guid proxy={0x36fe27e1,0x33e9,0x45ad,{0x98,0xda,0xc8,0x84,0x38,0xca,0x88,0x16}};
    static const char16 expected_path[]=u"\\image\\adsp2";
    if(EFI_ERROR(status) || *(uint32_t *)(config+8)!=2 || *(uint32_t *)(config+0xec)!=1 ||
       *(uint64_t *)(config+0xf0)!=0x9fd00000 || *(uint64_t *)(config+0xf8)!=0x3100000 ||
       memcmp(config+0xac,expected_path,sizeof(expected_path)) || memcmp(config+0x104,&proxy,sizeof(proxy)))return 0;
    for(unsigned i=0x4c;i<0x9c;i++)if(config[i])return 0; /* No physical partition label/root override. */
    memcpy(config+0x9c,&marble_pil_ram_partition,sizeof(marble_pil_ram_partition));
    config[0x100]=0; /* No IMEM image-load-info publication for a RAM-only cycle. */
    uint64_t ticks=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    print("marble-pil-adsp: begin native_loader=1 firmware_source=verified_ROM_RAM partition_writes=0 hold_ms=2000 battery_c=%d die_mc=%d\n",(uint64_t)marble_adc_last_temperature,(uint64_t)thermal.maximum_mc);
    marble_pil_dsp_dirty=1;
    status=((LoadConfig)(uintptr_t)table[2])(config);
    unsigned records=*(volatile uint32_t *)(uintptr_t)(base+0x183b4);
    uint64_t state=records==1?*(volatile uint64_t *)(uintptr_t)(base+0x183c0):0;
    unsigned running=state?*(volatile uint32_t *)(uintptr_t)(state+0x2c):0;
    print("marble-pil-adsp: load_status=%x native_records=%d native_running=%d elapsed_us=%d\n",status,(uint64_t)records,(uint64_t)running,(READ_SYSREG(CNTVCT_EL0)-ticks)*1000000/frequency);
    if(!records) {
        /* Native 1828 enters 3838 (which creates a state record) before any
         * ELF/PAS/proxy step. Shutdown-by-ID asserts if no such record exists. */
        marble_pil_dsp_dirty=0;
        print("marble-pil-adsp: rejected_before_state no_pas_path=1 shutdown_calls=0\n");return 0;
    }
    unsigned stable=!EFI_ERROR(status) && records==1 && running==1;
    if(stable) {
        for(unsigned i=0;i<20;i++) {
            BS->stall(100000);
            if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=55000) {stable=0;break;}
        }
        if(stable)marble_glink_probe(); /* Observer only; no channel open/TX. */
    }
    efi_status stopped=records==1?((Shutdown)(uintptr_t)table[4])(1):(EFI_ERROR_BIT|7);
    unsigned after=state?*(volatile uint32_t *)(uintptr_t)(state+0x2c):2;
    if(!EFI_ERROR(stopped) && after==0)marble_pil_dsp_dirty=0;
    print("marble-pil-adsp: shutdown_status=%x native_running=%d dirty=%d stable=%d die_mc=%d native_lifecycle_only=1 audio_acceptance=0\n",stopped,(uint64_t)after,(uint64_t)marble_pil_dsp_dirty,(uint64_t)stable,(uint64_t)thermal.maximum_mc);
    return stable && !marble_pil_dsp_dirty;
}
static int marble_pil_first_segment(uint64_t base,uint64_t state)
{
    /* Only called after successful PAS init + memory setup, never auth-reset.
     * Preserve the original 8KiB, native relocation fields, and pool pointer.
     * The authentication metadata is not altered. A separate, bounded header
     * view limits the exact native 3020 loader to phdr0 + phdr1. */
    uint8_t *s=(void *)(uintptr_t)state;
    uint64_t metadata=*(uint64_t *)(s+0x30);
    MarblePILSegmentPlan plan;
    if(!marble_pil_dsp_dirty || !metadata || *(uint32_t *)(s+0x38)!=5964 ||
       *(uint32_t *)(s+0x2c) || *(volatile uint32_t *)(uintptr_t)(base+0x3020)!=0xd10343ff ||
       !marble_pil_segment_plan((const void *)(uintptr_t)metadata,5964,&plan))return 0;
    const MarblePILFileEntry *entries=(const void *)(marble_pil_ramfs.data+32),*file=NULL;
    for(unsigned i=0;i<marble_pil_ramfs.count;i++)if(marble_pil_equal(entries[i].name,"adsp2.b01"))file=&entries[i];
    if(!file || file->size!=plan.file_bytes || file->fingerprint!=0x457e1cd91c45779dULL)return 0;
    for(uint64_t pa=plan.target;pa<plan.target+plan.memory_bytes;pa+=4096) {
        uint64_t par=marble_fw_translation(pa),attr=par>>56;
        if((par&1) || (par&0x0000fffffffff000ULL)!=pa || (attr!=0x44 && attr!=0xff))return 0;
    }
    static uint64_t saved_words[1024];
    uint64_t view_words[15]={0},relocation[3];uint32_t mode=*(uint32_t *)(s+0x28);
    memcpy(view_words,(const void *)(uintptr_t)metadata,116);
    ((uint8_t *)view_words)[44]=2;((uint8_t *)view_words)[45]=0;
    memcpy(relocation,s,sizeof(relocation));
    if(marble_pil_progress)marble_pil_progress("segment-save-before",plan.target);
    flush_dcache(plan.target,plan.target+plan.memory_bytes);
    memcpy(saved_words,(const void *)(uintptr_t)plan.target,plan.memory_bytes);
    uint64_t saved_hash=marble_pil_fnv(saved_words,sizeof(saved_words));
    *(uint64_t *)(s+0)=plan.target;*(uint64_t *)(s+8)=0x3100000;
    *(uint64_t *)(s+0x10)=plan.origin;*(uint32_t *)(s+0x28)=1;
    *(uint64_t *)(s+0x30)=(uint64_t)(uintptr_t)view_words;
    typedef int (EFIAPI *NativeSegments)(void *);
    unsigned reads=marble_pil_ramfs.reads;
    uint64_t began=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    if(marble_pil_progress)marble_pil_progress("native-first-segment-before",base+0x3020);
    int rc=((NativeSegments)(uintptr_t)(base+0x3020))((void *)(uintptr_t)(base+0x183b8));
    if(marble_pil_progress)marble_pil_progress("native-first-segment-return",(uint64_t)(int64_t)rc);
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    *(uint64_t *)(s+0x30)=metadata;memcpy(s,relocation,sizeof(relocation));*(uint32_t *)(s+0x28)=mode;
    flush_dcache(plan.target,plan.target+plan.memory_bytes);
    unsigned match=!memcmp((const void *)(uintptr_t)plan.target,marble_pil_ramfs.data+file->offset,plan.file_bytes),zero=1;
    for(uint64_t i=plan.file_bytes;i<plan.memory_bytes;i++)if(*(volatile uint8_t *)(uintptr_t)(plan.target+i))zero=0;
    memcpy((void *)(uintptr_t)plan.target,saved_words,plan.memory_bytes);
    flush_dcache(plan.target,plan.target+plan.memory_bytes);
    unsigned restored=marble_pil_fnv((const void *)(uintptr_t)plan.target,plan.memory_bytes)==saved_hash;
    memset(saved_words,0,sizeof(saved_words));
    unsigned passed=!rc && match && zero && restored && marble_pil_ramfs.reads==reads+1 && us<1000000;
    print("marble-pil-segment: index=1 native_rc=%d bytes=%d zero_bytes=%d ROM_match=%d tail_zero=%d original_restored=%d file_reads=%d elapsed_us=%d PASS=%d auth_reset=0 dsp_start=0\n",(uint64_t)(int64_t)rc,plan.file_bytes,plan.memory_bytes-plan.file_bytes,(uint64_t)match,(uint64_t)zero,(uint64_t)restored,(uint64_t)(marble_pil_ramfs.reads-reads),us,(uint64_t)passed);
    return passed;
}
static __attribute__((noinline)) int marble_pil_all_segments(uint64_t base,uint64_t state,unsigned defer_restore)
{
    uint8_t *s=(void *)(uintptr_t)state;
    const uint8_t *metadata=(const void *)(uintptr_t)*(uint64_t *)(s+0x30);
    MarblePILSegmentPlan plan;
    if(marble_pil_deferred.saved || !marble_pil_dsp_dirty || *(uint32_t *)(s+0x38)!=5964 || *(uint32_t *)(s+0x2c) ||
       *(volatile uint32_t *)(uintptr_t)(base+0x3020)!=0xd10343ff ||
       !marble_pil_segment_plan(metadata,5964,&plan)) {
        print("marble-pil-segments: state_or_plan_refused=1 AP_access=0\n");return 0;
    }
    const MarblePILFileEntry *entries=(const void *)(marble_pil_ramfs.data+32),*files[27]={0};
    unsigned loads=0,file_count=0;uint64_t file_bytes=0,zero_bytes=0;
    for(unsigned i=0;i<27;i++) {
        const uint8_t *p=metadata+52+32*i;
        if(marble_pil_u32(p)!=1 || (marble_pil_u32(p+24)&0x07000000)==0x02000000 || !marble_pil_u32(p+20))continue;
        loads++;uint32_t bytes=marble_pil_u32(p+16);zero_bytes+=marble_pil_u32(p+20)-bytes;
        if(!bytes)continue;
        char name[]="adsp2.b00";name[7]=(char)('0'+i/10);name[8]=(char)('0'+i%10);
        for(unsigned j=0;j<marble_pil_ramfs.count;j++)if(marble_pil_equal(entries[j].name,name))files[i]=&entries[j];
        if(!files[i] || files[i]->size!=bytes) {
            print("marble-pil-segments: file_refused=%s expected=%d present=%d actual=%d AP_access=0\n",name,(uint64_t)bytes,(uint64_t)(files[i]!=NULL),files[i]?files[i]->size:0);return 0;
        }
        file_count++;file_bytes+=bytes;
    }
    print("marble-pil-segments: planned_loads=%d planned_files=%d file_bytes=%d zero_bytes=%d AP_access=0\n",(uint64_t)loads,(uint64_t)file_count,file_bytes,zero_bytes);
    if(loads!=25 || file_count!=24 || file_bytes!=22712520 || zero_bytes!=27444032)return 0;
    const uint64_t length=0x3100000;
    for(uint64_t pa=plan.target;pa<plan.target+length;pa+=4096) {
        uint64_t par=marble_fw_translation(pa),attr=par>>56;
        if((par&1) || (par&0x0000fffffffff000ULL)!=pa ||
           attr!=(pa<0xa1400000?0x44u:0xbau) || !marble_display_memory_qualified) {
            print("marble-pil-segments: mapping_refused=%x PAR=%x AP_access=0\n",pa,par);return 0;
        }
    }
    void *saved=NULL;
    efi_status allocated=BS->allocate_pool(EfiLoaderData,length,&saved);
    print("marble-pil-segments: backup_allocation_status=%x bytes=%d present=%d AP_access=0\n",allocated,length,(uint64_t)(saved!=NULL));
    if(EFI_ERROR(allocated) || !saved)return 0;
    uint64_t save_pa=(uint64_t)(uintptr_t)saved;
    if(save_pa>~0ULL-length || (save_pa<plan.target+length && plan.target<save_pa+length)) {
        BS->free_pool(saved);return 0;
    }
    if(marble_pil_progress)marble_pil_progress("all-segments-save-before",plan.target);
    uint64_t total_began=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    flush_dcache(plan.target,plan.target+length);memcpy(saved,(const void *)(uintptr_t)plan.target,length);
    uint64_t saved_at=READ_SYSREG(CNTVCT_EL0);
    uint64_t relocation[3];uint32_t mode=*(uint32_t *)(s+0x28);memcpy(relocation,s,sizeof(relocation));
    *(uint64_t *)(s+0)=plan.target;*(uint64_t *)(s+8)=length;
    *(uint64_t *)(s+0x10)=plan.origin;*(uint32_t *)(s+0x28)=1;
    typedef int (EFIAPI *NativeSegments)(void *);
    unsigned reads=marble_pil_ramfs.reads;
    if(marble_pil_progress)marble_pil_progress("native-all-segments-before",base+0x3020);
    int rc=((NativeSegments)(uintptr_t)(base+0x3020))((void *)(uintptr_t)(base+0x183b8));
    if(marble_pil_progress)marble_pil_progress("native-all-segments-return",(uint64_t)(int64_t)rc);
    uint64_t loaded_at=READ_SYSREG(CNTVCT_EL0);
    memcpy(s,relocation,sizeof(relocation));*(uint32_t *)(s+0x28)=mode;
    /* Native 3020 skips filesz=0. Complete ELF BSS semantics explicitly,
     * matching the retained marble Linux mdt_loader.c, without running DSP. */
    for(unsigned i=0;i<27;i++) {
        const uint8_t *p=metadata+52+32*i;
        uint32_t mem=marble_pil_u32(p+20);
        if(marble_pil_u32(p)==1 && (marble_pil_u32(p+24)&0x07000000)!=0x02000000 && mem && !marble_pil_u32(p+16))
            marble_pil_memory_zero((void *)(uintptr_t)(plan.target+marble_pil_u32(p+12)-plan.origin),mem);
    }
    flush_dcache(plan.target,plan.target+length);
    unsigned matched=0,zeroed=0;
    for(unsigned i=0;i<27;i++) {
        const uint8_t *p=metadata+52+32*i;
        uint32_t mem=marble_pil_u32(p+20),bytes=marble_pil_u32(p+16);
        if(marble_pil_u32(p)!=1 || (marble_pil_u32(p+24)&0x07000000)==0x02000000 || !mem)continue;
        const uint8_t *target=(const void *)(uintptr_t)(plan.target+marble_pil_u32(p+12)-plan.origin);
        if(files[i] && marble_pil_memory_equal(target,marble_pil_ramfs.data+files[i]->offset,bytes))matched++;
        unsigned zero=marble_pil_memory_is_zero(target+bytes,mem-bytes);
        if(zero)zeroed++;
    }
    uint64_t checked_at=READ_SYSREG(CNTVCT_EL0);
    if(defer_restore) {
        unsigned verified=!rc && matched==file_count && zeroed==loads && marble_pil_ramfs.reads==reads+file_count;
        marble_pil_deferred.saved=saved;marble_pil_deferred.target=plan.target;
        marble_pil_deferred.length=length;marble_pil_deferred.verified=verified;
        print("marble-pil-held: loaded PASS=%d native_rc=%d files=%d ROM_matches=%d zero_regions=%d loads=%d backup_retained=1 original_restored=0 auth_reset=0 dsp_start=0\n",(uint64_t)verified,(uint64_t)(int64_t)rc,(uint64_t)file_count,(uint64_t)matched,(uint64_t)zeroed,(uint64_t)loads);
        return verified;
    }
    memcpy((void *)(uintptr_t)plan.target,saved,length);flush_dcache(plan.target,plan.target+length);
    unsigned restored=marble_pil_memory_equal((const void *)(uintptr_t)plan.target,saved,length);
    marble_pil_memory_zero(saved,length);efi_status freed=BS->free_pool(saved);
    uint64_t restored_at=READ_SYSREG(CNTVCT_EL0);
    unsigned display_ok=marble_display_memory_info(marble_display_memory_gop,marble_display_memory_bytes) && marble_display_memory_qualified;
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-total_began)*1000000/frequency;
    unsigned passed=!rc && matched==file_count && zeroed==loads && restored && display_ok && !EFI_ERROR(freed) &&
        marble_pil_ramfs.reads==reads+file_count && us<15000000;
    print("marble-pil-segments: save_us=%d native_load_us=%d readback_us=%d restore_us=%d total_limit_us=15000000 watchdog_seconds=30\n",(saved_at-total_began)*1000000/frequency,(loaded_at-saved_at)*1000000/frequency,(checked_at-loaded_at)*1000000/frequency,(restored_at-checked_at)*1000000/frequency);
    print("marble-pil-segments: native_rc=%d files=%d ROM_matches=%d zero_regions=%d loads=%d original_restored=%d free_status=%x elapsed_us=%d PASS=%d auth_reset=0 dsp_start=0\n",(uint64_t)(int64_t)rc,(uint64_t)file_count,(uint64_t)matched,(uint64_t)zeroed,(uint64_t)loads,(uint64_t)restored,freed,us,(uint64_t)passed);
    return passed;
}
static int marble_pil_restore_deferred(unsigned stopped)
{
    if(!marble_pil_deferred.saved)return 0;
    /* Never touch target or backup while PAS shutdown is uncertain. AT alone
     * does not qualify a payload access; the display ownership audit must pass. */
    unsigned display=stopped && marble_display_memory_info(marble_display_memory_gop,marble_display_memory_bytes) && marble_display_memory_qualified;
    print("marble-pil-held: restore-gate shutdown_verified=%d display_disjoint=%d target_access=%d\n",(uint64_t)stopped,(uint64_t)display,(uint64_t)display);
    if(!display)return 0;
    uint64_t target=marble_pil_deferred.target,length=marble_pil_deferred.length;
    for(uint64_t pa=target;pa<target+length;pa+=4096) {
        uint64_t par=marble_fw_translation(pa);
        if((par&1) || (par&0x0000fffffffff000ULL)!=pa || (par>>56)!=(pa<0xa1400000?0x44u:0xbau))return 0;
    }
    memcpy((void *)(uintptr_t)target,marble_pil_deferred.saved,length);flush_dcache(target,target+length);
    unsigned restored=marble_pil_memory_equal((const void *)(uintptr_t)target,marble_pil_deferred.saved,length);
    if(!restored)return 0;
    marble_pil_memory_zero(marble_pil_deferred.saved,length);
    efi_status freed=BS->free_pool(marble_pil_deferred.saved);
    if(!EFI_ERROR(freed))memset(&marble_pil_deferred,0,sizeof(marble_pil_deferred));
    print("marble-pil-held: restored PASS=%d original_restored=%d free_status=%x backup_retained=%d shutdown_before_restore=1 DSP_start=%d\n",(uint64_t)(restored && !EFI_ERROR(freed)),(uint64_t)restored,freed,(uint64_t)(marble_pil_deferred.saved!=NULL),(uint64_t)marble_pil_auth_attempted);
    return restored && !EFI_ERROR(freed);
}
static int marble_pil_pas_metadata_cycle(uint64_t auth,unsigned memory_setup,uint64_t pil_base,uint64_t pil_state)
{
    /* Separate from native 23d0/5f30: record secure errors and always attempt
     * PAS shutdown, without entering the provider's fatal-error dump path.
     * Optional PAS memory setup uses only the reserved ADSP carveout. The
     * separately selected first-segment probe restores its original 8KiB.
     * No authentication/reset or DSP execution in any of these stages. */
    if((memory_setup>=5 && !marble_pil_owned_dependencies_ready) || marble_scm_available_mask!=15 || marble_pil_dsp_dirty || !auth || (auth&4095) ||
       !marble_adc_channels(1) || marble_adc_last_temperature>=38)return 0;
    MarbleThermalSample thermal;
    if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=50000)return 0;
    if(memory_setup>=3 && (!marble_display_memory_qualified ||
       !marble_display_memory_info(marble_display_memory_gop,marble_display_memory_bytes) ||
       !marble_display_memory_qualified))return 0;
    static const efi_guid guid={0x77ed108d,0x8524,0x4b8b,{0x9d,0x2e,0x34,0x98,0x7a,0xec,0xb9,0xc1}};
    uint64_t *scm=NULL;
    if(!marble_scm_info() || EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&scm)) || !scm)return 0;
    uint64_t base=(uint64_t)(uintptr_t)scm-0xd130;
    if(scm[0]!=0x40001 || scm[1]!=base+0x3db0 || scm[7]!=base+0x3cb4 || scm[10]!=base+0x3d34)return 0;
    if(memory_setup && !marble_pil_adsp_memory())return 0;
    typedef efi_status (EFIAPI *Call2)(void *,uint32_t,uint32_t,const uint64_t *,uint64_t *);
    uint64_t params[10]={1,auth},result[4]={0},stop_params[10]={1},stop_result[4]={0};
    uint64_t began=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    marble_pil_dsp_dirty=1;
    if(marble_pil_progress)marble_pil_progress("PAS-metadata-init-before",auth);
    efi_status status=((Call2)(uintptr_t)scm[7])(scm,0x42000201,0x82,params,result);
    if(marble_pil_progress)marble_pil_progress("PAS-metadata-init-return",status);
    uint64_t init_us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    print("marble-pil-PAS: phase=metadata-init status=%x transport=%x secure=%x elapsed_us=%d PAS_id=1 auth_reset=0 dsp_start=0\n",status,result[0],result[1],init_us);
    unsigned memory_ok=!memory_setup,segment_ok=memory_setup<2,auth_ok=memory_setup<5;
    unsigned observer_requested=0,thermal_ok=1,runtime_ok=1;
    if(memory_setup && !EFI_ERROR(status) && result[0]==1 && result[1]==0) {
        uint64_t memory_params[10]={1,0x9fd00000,0x3100000},memory_result[4]={0};
        began=READ_SYSREG(CNTVCT_EL0);
        if(marble_pil_progress)marble_pil_progress("PAS-memory-setup-before",0x9fd00000);
        efi_status memory_status=((Call2)(uintptr_t)scm[7])(scm,0x42000202,3,memory_params,memory_result);
        if(marble_pil_progress)marble_pil_progress("PAS-memory-setup-return",memory_status);
        uint64_t us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
        memory_ok=!EFI_ERROR(memory_status) && memory_result[0]==1 && memory_result[1]==0 && us<1000000;
        print("marble-pil-PAS: phase=memory-setup status=%x transport=%x secure=%x elapsed_us=%d PASS=%d AP_payload_reads=0 AP_payload_writes=0 auth_reset=0 dsp_start=0\n",memory_status,memory_result[0],memory_result[1],us,(uint64_t)memory_ok);
        if(memory_ok && memory_setup==2)segment_ok=marble_pil_first_segment(pil_base,pil_state);
        if(memory_ok && memory_setup>=3)segment_ok=marble_pil_all_segments(pil_base,pil_state,memory_setup>=4);
        if(memory_ok && segment_ok && marble_pil_post_load_hook) {
            unsigned gate=memory_setup>=4 && marble_pil_post_load_hook();
            print("marble-pil-held: post_load_gate PASS=%d ROM_loaded=1 PAS_initialized=1 DSP_start=0\n",(uint64_t)gate);
            if(!gate)return 0; /* retain metadata, backup, outer leases and watchdog */
        }
        if(memory_ok && segment_ok && memory_setup>=5 && marble_npa_active==2 && marble_npa_dirty && marble_npa_held_action) {
            unsigned observed;
#ifdef XNU_MARBLE_AUDIO_OWNER_NATIVE_LINK
            /* The native endpoint is the sole GLINK owner. This gate proves
             * only the pre-AUTH kernel READY/cap handshake, never DSP link-up.
             * Real MAP/START/DATA/UNMAP evidence is still mandatory later. */
            observer_requested=0;
            observed=marble_audio_owner_link_gate();
#else
            observer_requested=1;
            observed=marble_glink_begin();
#endif
            if(observed && marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) && thermal.maximum_mc<50000) {
                uint64_t auth_result[4]={0};began=READ_SYSREG(CNTVCT_EL0);
                marble_pil_auth_attempted=marble_npa_dsp_attempted=1;
                if(marble_pil_progress)marble_pil_progress("PAS-auth-reset-before",1);
                efi_status auth_status=((Call2)(uintptr_t)scm[7])(scm,0x42000205,1,stop_params,auth_result);
                if(marble_pil_progress)marble_pil_progress("PAS-auth-reset-return",auth_status);
                if(marble_pil_progress)marble_pil_progress("PAS-auth-transport",auth_result[0]);
                if(marble_pil_progress)marble_pil_progress("PAS-auth-secure",auth_result[1]);
                uint64_t us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
                auth_ok=!EFI_ERROR(auth_status) && auth_result[0]==1 && !auth_result[1] && us<1000000;
                unsigned hold_limit_ms=500;
#ifdef XNU_MARBLE_AUDIO_RUNTIME_600MS
                hold_limit_ms=600;
                print("aud-budget: version=1 maximum_us=600000 poll_rounds=60 thermal_samples=6 IRQ_masks_unchanged=1 frequency_requests=0 voltage_requests=0 audio_acceptance=0\n");
#endif
#ifdef XNU_MARBLE_AUDIO_DIGITAL_EARLY_STOP
                hold_limit_ms=600;
                print("aud-typed-budget: version=1 minimum_us=300000 maximum_us=600000 poll_rounds=60 completion_required=1 final_thermal_required=1 audio_acceptance=0\n");
#endif
                print("marble-pil-boot: auth status=%x transport=%x secure=%x elapsed_us=%d accepted=%d hold_limit_ms=%d payload_access=0\n",auth_status,auth_result[0],auth_result[1],us,(uint64_t)auth_ok,(uint64_t)hold_limit_ms);
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
                marble_qmp_timeline("AUTH-return");
#endif
                uint64_t held=READ_SYSREG(CNTVCT_EL0);unsigned samples=0;
#ifdef XNU_MARBLE_AUDIO_RUNTIME_SAMPLING
                if(!marble_tpl_sampler_arm())runtime_ok=0;
#endif
#ifdef XNU_MARBLE_TDM_RUN_ONLY
                /* Five sequential graph transactions must finish before the
                 * SMP2P stop request. Keep the same 500ms/5 thermal samples;
                 * retain the proven initial 100ms handshake cadence, then
                 * service graph replies every 10ms after the first 300ms. */
#if defined(XNU_MARBLE_AUDIO_RUNTIME_600MS) || defined(XNU_MARBLE_AUDIO_DIGITAL_EARLY_STOP)
                const unsigned rounds=60,poll_us=10000,thermal_interval=10;
#else
                const unsigned rounds=50,poll_us=10000,thermal_interval=10;
#endif
#else
                const unsigned rounds=5,poll_us=100000,thermal_interval=1;
#endif
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
                print("aud-owner-handshake: service_us=%d warmup_us=%d initial_ms=300 later_service_us=2000 minimum_runtime_us=300000 maximum_runtime_us=600000 real_replies_required=1 FIFO_owner=XNU IRQ_changes=0\n",(uint64_t)XNU_MARBLE_AUDIO_HANDSHAKE_ROUNDS*10000,(uint64_t)XNU_MARBLE_AUDIO_HANDSHAKE_WARMUP_ROUNDS*10000);
#endif
                if(auth_ok && runtime_ok)for(unsigned i=0;i<rounds;i++) {
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
                    if(i>=30 && marble_pil_progress)marble_pil_progress("audio-amp-round-before",i);
#endif
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
                    if(i>=30 || !(i%(i<XNU_MARBLE_AUDIO_HANDSHAKE_WARMUP_ROUNDS?10:XNU_MARBLE_AUDIO_HANDSHAKE_ROUNDS)))
#elif defined(XNU_MARBLE_TDM_RUN_ONLY)
                    if(i>=30 || !(i%10))
#endif
                    if(marble_smp2p_runtime_service && !marble_smp2p_runtime_service()) {runtime_ok=0;break;}
#ifdef XNU_MARBLE_AUDIO_DIGITAL_EARLY_STOP
                    /* Diagnostic ends on typed PCM/graph/UNMAP retirement,
                     * not an extra idle dwell after digital PLL activity.
                     * Keep the original maximum deadline; sample thermal
                     * state immediately before requesting stop. */
                    if(i>=30 && marble_pil_runtime_complete && marble_pil_runtime_complete()) {
                        samples++;
                        if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=55000)thermal_ok=0;
#ifdef XNU_MARBLE_AUDIO_OWNER
                        uint64_t elapsed=(READ_SYSREG(CNTVCT_EL0)-held)*1000000/frequency;
                        unsigned within=marble_audio_runtime_bound(elapsed,1,thermal_ok);
                        if(!within)runtime_ok=0;
                        print("aud-runtime-deadline: phase=typed-complete elapsed_us=%d limit_us=600000 within=%d includes_final_thermal=1 round_only=0\n",elapsed,(uint64_t)within);
#endif
                        print("aud-early: typed_complete=1 round=%d minimum_us=300000 maximum_us=750000 thermal_samples=%d final_thermal_ok=%d idle_dwell_omitted=1 PAS_required=1 audio_acceptance=0\n",(uint64_t)i,(uint64_t)samples,(uint64_t)thermal_ok);
                        break;
                    }
#endif
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
                    if(i>=30 && marble_pil_progress)marble_pil_progress("audio-amp-round-service",i);
#endif
#ifdef XNU_MARBLE_AUDIO_RUNTIME_DEADLINE
                    if(frequency<1000000 || frequency>1000000000) {runtime_ok=0;break;}
#if (defined(XNU_MARBLE_AUDIO_PCM_REPEAT) && XNU_MARBLE_AUDIO_PCM_PERIODS == 8) || defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
                    /* Keep the original 600ms deadline and initial handshake
                     * cadence. Once streaming, service at 2ms boundaries so
                     * eight periods leave time for STOP/CLOSE/UNMAP. No
                     * extra dwell, clock request or interrupt mask change. */
                    if(i>=30)for(unsigned sub=1;sub<5;sub++) {
                        uint64_t target=frequency*(i*5+sub)/500;
                        uint64_t now=READ_SYSREG(CNTVCT_EL0),elapsed=now-held;
                        if(elapsed<target) {
                            uint64_t wait=((target-elapsed)*1000000+frequency-1)/frequency;
                            if(wait>2000) {runtime_ok=0;break;}
                            BS->stall(wait);
                        }
                        if(marble_smp2p_runtime_service && !marble_smp2p_runtime_service()) {runtime_ok=0;break;}
                    }
                    if(!runtime_ok)break;
#endif
                    /* Fifty deadlines over the original 500ms interval.
                     * I2C work consumes elapsed time instead of extending
                     * DSP runtime by another full fifty sleeps. */
                    uint64_t now=READ_SYSREG(CNTVCT_EL0),elapsed=now-held,target=frequency*(i+1)/100;
                    if(elapsed<target) {
                        uint64_t us=((target-elapsed)*1000000+frequency-1)/frequency;
                        if(us>poll_us) {runtime_ok=0;break;}
                        BS->stall(us);
                    }
#elif defined(XNU_MARBLE_AUDIO_AMP_DIRECT_WAIT)
                    /* Isolate the native Stall boundary after GENI activity.
                     * Same elapsed polling interval and thermal cadence;
                     * timer is read only, IRQ/FIQ masks remain untouched. */
                    if(frequency<1000000 || frequency>1000000000) {runtime_ok=0;break;}
                    uint64_t wait_began=READ_SYSREG(CNTVCT_EL0),wait_ticks=frequency*poll_us/1000000;
                    while(READ_SYSREG(CNTVCT_EL0)-wait_began<wait_ticks)__asm__ volatile("yield" ::: "memory");
#else
                    BS->stall(poll_us);
#endif
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
                    if(i>=30 && marble_pil_progress)marble_pil_progress("audio-amp-round-stall",i);
#endif
                    if((i+1)%thermal_interval)continue;
                    samples++;
                    if(!marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) || thermal.maximum_mc>=55000) {thermal_ok=0;break;}
                }
#ifdef XNU_MARBLE_AUDIO_RUNTIME_SAMPLING
                if(!marble_tpl_sampler_release())runtime_ok=0;
                marble_tpl_sampler_log();
#endif
#ifdef XNU_MARBLE_AUDIO_OWNER
                unsigned sampler_required=0;
#ifdef XNU_MARBLE_AUDIO_RUNTIME_SAMPLING
                sampler_required=1;
#endif
                unsigned final_complete=marble_pil_runtime_complete && marble_pil_runtime_complete();
                uint64_t final_elapsed=(READ_SYSREG(CNTVCT_EL0)-held)*1000000/frequency;
                unsigned final_within=marble_audio_runtime_bound(final_elapsed,final_complete,thermal_ok);
                if(!final_within)runtime_ok=0;
                print("aud-runtime-deadline: phase=runtime-exit elapsed_us=%d limit_us=600000 within=%d typed_complete=%d sampler_closed_before_stop=1 sampler_required=%d includes_final_thermal=1 round_only=0\n",final_elapsed,(uint64_t)final_within,(uint64_t)final_complete,(uint64_t)sampler_required);
#endif
                print("marble-pil-boot: observed hold_us=%d thermal_samples=%d thermal_ok=%d die_mc=%d link_up=%d link_down=%d link_invalid=%d service_acceptance=0 audio_acceptance=0 runtime_ok=%d\n",(READ_SYSREG(CNTVCT_EL0)-held)*1000000/frequency,(uint64_t)samples,(uint64_t)thermal_ok,(uint64_t)thermal.maximum_mc,(uint64_t)marble_glink_observer.up,(uint64_t)marble_glink_observer.down,(uint64_t)marble_glink_observer.invalid,(uint64_t)runtime_ok);
#ifdef XNU_MARBLE_AUDIO_FAST_HANDSHAKE
                if(!runtime_ok && thermal_ok)marble_audio_owner_failure_wire();
#endif
#ifdef XNU_MARBLE_AUDIO_RUNTIME_DEADLINE
                print("aw: runtime deadline_us=%d poll_rounds=%d initial_service_cadence_us=100000 thermal_samples=%d includes_I2C_work=1 IRQ_masks_unchanged=1 frequency_requests=0\n",(uint64_t)hold_limit_ms*1000,(uint64_t)rounds,(uint64_t)samples);
#endif
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
                if(marble_pil_progress)marble_pil_progress("audio-amp-runtime-end",((uint64_t)runtime_ok<<32)|thermal_ok);
#endif
#ifdef XNU_MARBLE_AUDIO_DIGITAL_PLL
                if(marble_pil_progress)marble_pil_progress("aud-run-us",(READ_SYSREG(CNTVCT_EL0)-held)*1000000/frequency);
                if(marble_pil_runtime_probe)marble_pil_runtime_probe();
#endif
#ifdef XNU_MARBLE_AUDIO_DIGITAL_CAPTURE
                /* Diagnostic only: return while owned power/ROM/watchdog
                 * remain live. Host captures RAM log, then observes the
                 * mandatory cold reset. Never claim PAS or local restore. */
                if(auth_ok && thermal_ok && runtime_ok) {
                    print("aud-capture: returned_before_STOP=1 DSP_retained=1 ROM_retained=1 power_leases_retained=1 watchdog_retained=1 cold_reset_required=1 PAS_shutdown_calls=0 audio_acceptance=0\n");
                    if(marble_pil_log_capture)marble_pil_log_capture();
                    return 0;
                }
#endif
            }
        }
    }
    if(memory_setup>=6 && (!auth_ok || !thermal_ok || !runtime_ok || !marble_smp2p_request_stop(marble_pil_progress))) {
        print("marble-pil-boot: stop_not_qualified=1 PAS_shutdown_calls=0 payload_restore=0 power_release=0 metadata_retained=1 watchdog_retained=1 reset_required=1\n");
        if(marble_pil_progress)marble_pil_progress("SMP2P-stop-unqualified",0);
        return 0;
    }
    if(memory_setup==7) {
        /* v38 proved ready/stop-ack but PAS shutdown still reset USB/SOC.
         * This separate experiment deliberately retains every DSP resource
         * and uses a verified whole-firmware cold reset. Never fake local
         * restoration or call the failed shutdown path here. */
        marble_pil_reset_boot_qualified=auth_ok && thermal_ok && marble_pil_deferred.saved &&
            marble_pil_deferred.verified && marble_npa_active==2 && marble_npa_dirty;
        print("marble-pil-reset-boot: complete PASS=%d auth_accepted=%d ready_stop_ack=1 PAS_shutdown_calls=0 payload_restore=0 metadata_retained=1 backup_retained=1 power_clients=2 observer_active=%d watchdog_retained=1 reset_required=1 original_restore=0 audio_acceptance=0\n",(uint64_t)marble_pil_reset_boot_qualified,(uint64_t)auth_ok,(uint64_t)marble_glink_observer.active);
        return 0;
    }
    began=READ_SYSREG(CNTVCT_EL0);
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
    /* Reserve110ms for the existing100ms dwell, thermal ADC and poll resume. */
    marble_qmp_timeline("metadata-stop-begin");
    marble_qmp_shutdown_deadline(began+frequency*890/1000);
#endif
    const volatile uint64_t *stop_guard=stop_params,*scm_guard=scm;
    unsigned stop_args=stop_guard[0]==1 && scm_guard[0]==0x40001 && scm_guard[7]==base+0x3cb4;
    for(unsigned i=1;i<10;i++)if(stop_guard[i])stop_args=0;
    if(marble_pil_progress)marble_pil_progress("PAS-stop-args-valid",stop_args);
    if(!stop_args)return 0;
    if(marble_pil_progress)marble_pil_progress("PAS-shutdown-DAIF",READ_SYSREG(DAIF));
    if(marble_pil_progress)marble_pil_progress("PAS-metadata-shutdown-before",1);
    efi_status stopped=marble_pil_shutdown_adapter?marble_pil_shutdown_adapter(stop_params,stop_result):
        ((Call2)(uintptr_t)scm[7])(scm,0x42000206,1,stop_params,stop_result);
#if defined(XNU_MARBLE_AUDIO_USB_POLL_LEASE) && !defined(XNU_MARBLE_AUDIO_USB_QMP_LEASE)
    if(!marble_usb_poll_complete())stopped=EFI_ERROR_BIT|7;
#endif
    if(marble_pil_progress)marble_pil_progress("PAS-metadata-shutdown-return",stopped);
    uint64_t stop_us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
#if defined(XNU_MARBLE_AUDIO_USB_POLL_LEASE) && !defined(XNU_MARBLE_AUDIO_USB_QMP_LEASE)
    marble_usb_poll_log();
#endif
    unsigned clean=!EFI_ERROR(stopped) && stop_result[0]==1 && stop_result[1]==0;
    if(memory_setup>=4)print("marble-pil-held: shutdown status=%x transport=%x secure=%x verified=%d target_access=0\n",stopped,stop_result[0],stop_result[1],(uint64_t)clean);
    if(clean && marble_pil_post_shutdown_hook) {
        clean=marble_pil_post_shutdown_hook();
        print("marble-pil-held: post_shutdown_gate PASS=%d PAS_stopped=1 payload_restore=0\n",(uint64_t)clean);
#ifndef XNU_MARBLE_AUDIO_USB_QMP_LEASE
        if(!clean)return 0;
#endif
    }
#ifdef XNU_MARBLE_AUDIO_USB_QMP_LEASE
    /* Resume even on close failure; never leave the qualified poll paused.
     * Include close and resume in the unchanged outer metadata-stop 1s. */
    unsigned poll_restored=marble_usb_poll_complete();
    marble_usb_poll_log();
    stop_us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/frequency;
    clean=clean && poll_restored && stop_us<1000000;
    if(!clean)return 0;
#endif
    unsigned observer_released=!observer_requested;
    if(observer_requested)observer_released=marble_glink_release(marble_pil_auth_attempted);
    if(memory_setup>=4 && marble_pil_deferred.saved) {
        unsigned restored=marble_pil_restore_deferred(clean);
        clean=clean && restored;
    }
    clean=clean && observer_released;
    if(clean)marble_pil_dsp_dirty=0;
    unsigned passed=clean && memory_ok && segment_ok && auth_ok && thermal_ok && !EFI_ERROR(status) && result[0]==1 && result[1]==0 && init_us<1000000 && stop_us<1000000;
    print("marble-pil-PAS: phase=metadata-shutdown status=%x transport=%x secure=%x elapsed_us=%d dirty=%d PASS=%d auth_reset=%d dsp_start=%d\n",stopped,stop_result[0],stop_result[1],stop_us,(uint64_t)marble_pil_dsp_dirty,(uint64_t)passed,(uint64_t)marble_pil_auth_attempted,(uint64_t)marble_pil_auth_attempted);
    return passed;
}
static int marble_pil_header_probe(uint64_t *table,unsigned parse_elf)
{
    uint64_t base=(uint64_t)(uintptr_t)table-0x18198;
    if(!marble_pil_header_only || table[2]!=base+0x171c || table[6]!=base+0x17d8 ||
       *(volatile uint64_t *)(uintptr_t)(base+0x12dc0)!=base+0x15dc ||
       *(volatile uint32_t *)(uintptr_t)(base+0x15dc)!=0x528000e0 ||
       *(volatile uint32_t *)(uintptr_t)(base+0x15e0)!=0xd65f03c0)return 0;
    typedef efi_status (EFIAPI *GetConfig)(const char16 *,void *);
    typedef efi_status (EFIAPI *LoadConfig)(void *);
    typedef int (EFIAPI *ClearMetadata)(void *,void *);
    uint64_t words[35];uint8_t *config=(void *)words;memset(config,0,sizeof(words));
    efi_status status=((GetConfig)(uintptr_t)table[6])(u"FULL_ADSP2",config);
    if(EFI_ERROR(status) || *(uint32_t *)(config+0xec)!=1 || *(uint32_t *)(config+8)!=2)return 0;
    for(unsigned i=0x4c;i<0x9c;i++)if(config[i])return 0;
    memcpy(config+0x9c,&marble_pil_ram_partition,sizeof(marble_pil_ram_partition));config[0x100]=0;
    if(marble_pil_progress)marble_pil_progress("native-header-begin",base+0x171c);
    status=((LoadConfig)(uintptr_t)table[2])(config);
    if(marble_pil_progress)marble_pil_progress("native-header-return",status);
    unsigned count=*(volatile uint32_t *)(uintptr_t)(base+0x183b4);
    uint64_t native_config=count==1?*(volatile uint64_t *)(uintptr_t)(base+0x183b8):0;
    uint64_t state=count==1?*(volatile uint64_t *)(uintptr_t)(base+0x183c0):0;
    uint64_t metadata=state?*(volatile uint64_t *)(uintptr_t)(state+0x30):0;
    unsigned size=state?*(volatile uint32_t *)(uintptr_t)(state+0x38):0;
    unsigned running=state?*(volatile uint32_t *)(uintptr_t)(state+0x2c):2;
    const MarblePILFileEntry *entries=(const void *)(marble_pil_ramfs.data+32),*entry=NULL;
    for(unsigned i=0;i<marble_pil_ramfs.count;i++)if(marble_pil_equal(entries[i].name,"adsp2.mdt"))entry=&entries[i];
    unsigned match=metadata && entry && size==entry->size &&
        !memcmp((const void *)(uintptr_t)metadata,marble_pil_ramfs.data+entry->offset,size);
    print("marble-pil-header: load_status=%x records=%d metadata_bytes=%d ROM_match=%d native_running=%d PAS_callback=refuse_before_auth dsp_start=0\n",status,(uint64_t)count,(uint64_t)size,(uint64_t)match,(uint64_t)running);
    unsigned elf_ok=!parse_elf;
    if(parse_elf && count==1 && match && !running) {
        typedef int (EFIAPI *ParseElf)(void *,const void *);
        if(marble_pil_progress)marble_pil_progress("ELF-parse-begin",base+0x1ed8);
        int parsed=((ParseElf)(uintptr_t)(base+0x1ed8))((void *)(uintptr_t)(base+0x183b8),(const void *)(uintptr_t)metadata);
        if(marble_pil_progress)marble_pil_progress("ELF-parse-return",(uint64_t)(int64_t)parsed);
        uint64_t phdrs=*(volatile uint64_t *)(uintptr_t)(state+0x18),header=*(volatile uint64_t *)(uintptr_t)(state+0x20);
        unsigned phnum=header?*(volatile uint16_t *)(uintptr_t)(header+0x2c):0;
        elf_ok=!parsed && phdrs && header && phnum && phnum<=64;
        print("marble-pil-elf: parse_rc=%d program_headers=%d header_copy=%d phdr_copy=%d PASS=%d PAS_init=0 auth_reset=0 dsp_start=0\n",(uint64_t)(int64_t)parsed,(uint64_t)phnum,(uint64_t)(header!=0),(uint64_t)(phdrs!=0),(uint64_t)elf_ok);
        /* The original PAS callback uses state+0x40, not the pool copy at
         * +0x30. Audit its allocated authentication buffer separately before
         * considering any future PAS call. Never inspect DSP payload memory. */
        uint64_t auth=*(volatile uint64_t *)(uintptr_t)(state+0x40);
        unsigned mapped=auth && !(auth&4095) && size==5964;
        uint64_t attr=0;
        if(mapped)for(uint64_t at=auth;at<auth+size;at+=4096) {
            uint64_t par=marble_fw_translation(at);
            if((par&1) || (par&0x0000fffffffff000ULL)!=at) {mapped=0;break;}
            if(at==auth)attr=par>>56;
            else if(attr!=(par>>56)) {mapped=0;break;}
        }
        unsigned auth_match=mapped && !memcmp((const void *)(uintptr_t)auth,marble_pil_ramfs.data+entry->offset,size);
        print("marble-pil-metadata: address=%x bytes=%d aligned4096=%d identity_mapped=%d PAR_attr=%x ROM_match=%d PAS_init=0 auth_reset=0 dsp_start=0\n",auth,(uint64_t)size,(uint64_t)(auth && !(auth&4095)),(uint64_t)mapped,attr,(uint64_t)auth_match);
        elf_ok=elf_ok && auth_match;
        if(parse_elf>=2)elf_ok=elf_ok && attr==0x44 && marble_pil_pas_metadata_cycle(auth,parse_elf>=3?parse_elf-2:0,base,state);
    }
    if(marble_pil_dsp_dirty) {
        print("marble-pil-header: metadata_retained=1 PAS_shutdown_unverified=1 reboot_required=1 dsp_start=%d\n",(uint64_t)marble_pil_auth_attempted);return 0;
    }
    unsigned cleared=0;int rc=-1;
    if(native_config && state && !running) {
        uint64_t pair=0;
        /* Exact native 3838 reuses a non-running state and frees its four
         * temporary metadata buffers. No clock/PAS callback on this path. */
        rc=((ClearMetadata)(uintptr_t)(base+0x3838))((void *)(uintptr_t)native_config,&pair);
        cleared=rc==10 && !*(volatile uint64_t *)(uintptr_t)(state+0x18) &&
            !*(volatile uint64_t *)(uintptr_t)(state+0x20) && !*(volatile uint64_t *)(uintptr_t)(state+0x30) &&
            !*(volatile uint64_t *)(uintptr_t)(state+0x40);
    }
    unsigned pass=EFI_ERROR(status) && count==1 && match && !running && cleared && elf_ok;
    print("marble-pil-header: complete PASS=%d metadata_clear=%d clear_rc=%d PAS_metadata_requested=%d auth_reset=%d dsp_start=%d\n",(uint64_t)pass,(uint64_t)cleared,(uint64_t)(int64_t)rc,(uint64_t)(parse_elf>=2),(uint64_t)marble_pil_auth_attempted,(uint64_t)marble_pil_auth_attempted);
    return pass;
}
static struct {
    efi_handle handle;unsigned installed,dirty,block_reads,block_writes;
    struct {uint64_t revision;efi_status (EFIAPI *open)(void *,MarblePILFile **);} fs;
    struct {uint32_t id;uint8_t removable,present,logical,readonly,caching,pad[3];uint32_t block_size,align,pad2;uint64_t last_block;} media;
    struct {uint64_t revision;void *media,*reset,*read,*write,*flush;} block;
    uint8_t path[46];
} marble_pil_ram_device;
static efi_status EFIAPI marble_pil_ram_open_volume(void *self,MarblePILFile **root)
{
    if(self!=&marble_pil_ram_device.fs || !root)return MPFS_INVALID;
    *root=marble_pil_file_new(NULL,1);return *root?0:(EFI_ERROR_BIT|9);
}
static efi_status EFIAPI marble_pil_ram_block_reset(void *self,uint8_t extended)
{ (void)self;(void)extended;return MPFS_UNSUPPORTED; }
static efi_status EFIAPI marble_pil_ram_block_read(void *self,uint32_t media,uint64_t lba,uint64_t bytes,void *data)
{ (void)self;(void)media;(void)lba;(void)bytes;(void)data;marble_pil_ram_device.block_reads++;return MPFS_UNSUPPORTED; }
static efi_status EFIAPI marble_pil_ram_block_write(void *self,uint32_t media,uint64_t lba,uint64_t bytes,const void *data)
{ (void)self;(void)media;(void)lba;(void)bytes;(void)data;marble_pil_ram_device.block_writes++;return MPFS_WRITE_PROTECTED; }
static efi_status EFIAPI marble_pil_ram_block_flush(void *self)
{ (void)self;return 0; }
static __attribute__((noinline)) int marble_pil_file_probe(const void *raw,uint64_t bytes,unsigned adsp_cycle)
{
    if(!marble_pil_attempted || marble_pil_ram_device.installed || marble_pil_ram_device.dirty ||
       bytes!=30057264 || !raw || marble_pil_fnv(raw,bytes)!=0xe41e8a04729dcffaULL ||
       !marble_pil_ramfs_init(raw,bytes))return 0;
    static const efi_guid pil={0x1f4f1da3,0x4d8d,0x4894,{0xba,0x2a,0x13,0xd3,0x20,0x7f,0x97,0x39}};
    static const efi_guid block={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
    static const efi_guid fs={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
    static const efi_guid path={0x9576e91,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
    uint64_t *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&pil,NULL,(void **)&table)) || !table || table[0]!=0x10003)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x18198;
    if(table[6]!=base+0x17d8 || table[2]!=base+0x171c)return 0;
    typedef efi_status (EFIAPI *Install)(efi_handle *,const efi_guid *,unsigned,void *);
    typedef efi_status (EFIAPI *Uninstall)(efi_handle,const efi_guid *,void *);
    typedef efi_status (EFIAPI *NativeOpen)(const char16 *,const efi_guid *,const efi_guid *,const char16 *,uint8_t,const char16 *,uint64_t,MarblePILFile **);
    if(!BS->install_protocol_interface || !BS->uninstall_protocol_interface)return 0;
    memset(&marble_pil_ram_device,0,sizeof(marble_pil_ram_device));
    marble_pil_ram_device.fs.revision=0x10000;marble_pil_ram_device.fs.open=marble_pil_ram_open_volume;
    marble_pil_ram_device.media.id=1;marble_pil_ram_device.media.present=1;
    marble_pil_ram_device.media.logical=marble_pil_ram_device.media.readonly=1;
    marble_pil_ram_device.media.block_size=512;marble_pil_ram_device.media.align=1;
    marble_pil_ram_device.media.last_block=(bytes+511)/512-1;
    marble_pil_ram_device.block.revision=0x10000;marble_pil_ram_device.block.media=&marble_pil_ram_device.media;
    marble_pil_ram_device.block.reset=(void *)marble_pil_ram_block_reset;
    marble_pil_ram_device.block.read=(void *)marble_pil_ram_block_read;
    marble_pil_ram_device.block.write=(void *)marble_pil_ram_block_write;
    marble_pil_ram_device.block.flush=(void *)marble_pil_ram_block_flush;
    uint8_t *dp=marble_pil_ram_device.path;dp[0]=4;dp[1]=1;dp[2]=42;dp[4]=1;
    uint64_t sectors=(bytes+511)/512;memcpy(dp+16,&sectors,8);
    memcpy(dp+24,&marble_pil_ram_partition,16);dp[40]=dp[41]=2;dp[42]=0x7f;dp[43]=0xff;dp[44]=4;
    const efi_guid *guids[]={&block,&fs,&path,&marble_pil_ram_partition};
    void *interfaces[]={&marble_pil_ram_device.block,&marble_pil_ram_device.fs,dp,(void *)&marble_pil_ram_partition};
    efi_status watchdog=BS->set_watchdog_timer(30,0,0,NULL);
    if(EFI_ERROR(watchdog))return 0;
    unsigned passed=1;
    for(unsigned i=0;i<4;i++) {
        efi_status s=((Install)BS->install_protocol_interface)(&marble_pil_ram_device.handle,guids[i],0,interfaces[i]);
        print("marble-pil-ramfs: install slot=%d status=%x\n",(uint64_t)i,s);
        if(EFI_ERROR(s)) {passed=0;break;}marble_pil_ram_device.installed|=1u<<i;
    }
    if(passed) {
        static const struct {const char *name;const char16 *path;} tests[]={
            {"adsp2.mdt",u"\\image\\adsp2.mdt"},{"slpi2.mdt",u"\\image\\slpi2.mdt"}};
        for(unsigned n=0;n<2;n++) {
            MarblePILFile *file=NULL;
            efi_status s=((NativeOpen)(uintptr_t)(base+0x75a8))(tests[n].path,NULL,&marble_pil_ram_partition,NULL,1,NULL,1,&file);
            unsigned owned=0;
            for(unsigned i=0;i<8;i++)if(file==&marble_pil_ramfs.handles[i] && file->used)owned=1;
            print("marble-pil-ramfs: native_open=%s status=%x owned=%d dsp_start=0\n",tests[n].name,s,(uint64_t)owned);
            if(EFI_ERROR(s) || !owned) {passed=0;continue;}
            static uint64_t info[42],data[1024];uint64_t info_bytes=sizeof(info),got=sizeof(data);
            efi_status a=file->get_info(file,&marble_pil_file_info_guid,&info_bytes,info);
            efi_status b=file->read(file,&got,data);
            unsigned equal=!EFI_ERROR(a) && !EFI_ERROR(b) && file->entry && got==file->entry->size &&
                marble_pil_fnv(data,got)==file->entry->fingerprint;
            print("marble-pil-ramfs: native_read=%s bytes=%d info_status=%x read_status=%x content_match=%d\n",tests[n].name,got,a,b,(uint64_t)equal);
            if(!equal)passed=0;
            if(EFI_ERROR(file->close(file)))passed=0;
        }
    }
    if(passed && adsp_cycle)passed=(adsp_cycle>=2 && adsp_cycle<=11)?marble_pil_header_probe(table,adsp_cycle-2):0;
    if(marble_pil_dsp_dirty) {
        print("marble-pil-ramfs: retained installed=%d dirty=%d metadata_retained=1 backup_retained=%d watchdog_retained=1 dsp_start=%d reset_required=1\n",(uint64_t)marble_pil_ram_device.installed,(uint64_t)marble_pil_ram_device.dirty,(uint64_t)(marble_pil_deferred.saved!=NULL),(uint64_t)marble_pil_auth_attempted);
        return 0;
    }
    for(unsigned i=4;i-->0;)if(marble_pil_ram_device.installed&(1u<<i)) {
        efi_status s=((Uninstall)BS->uninstall_protocol_interface)(marble_pil_ram_device.handle,guids[i],interfaces[i]);
        print("marble-pil-ramfs: uninstall slot=%d status=%x\n",(uint64_t)i,s);
        if(EFI_ERROR(s))marble_pil_ram_device.dirty=1;
        else marble_pil_ram_device.installed&=~(1u<<i);
    }
    for(unsigned i=0;i<8;i++)if(marble_pil_ramfs.handles[i].used)marble_pil_file_close(&marble_pil_ramfs.handles[i]);
    /* Keep the 30s recovery watchdog armed after any unverified DSP stop. */
    unsigned outer_watchdog=marble_npa_held_action && marble_npa_active==2 && marble_npa_dirty;
    efi_status clear=(marble_pil_dsp_dirty || outer_watchdog)?(EFI_ERROR_BIT|7):BS->set_watchdog_timer(0,0,0,NULL);
    passed=passed && !marble_pil_ram_device.installed && !marble_pil_ram_device.dirty && !marble_pil_ramfs.writes &&
        !marble_pil_ram_device.block_reads && !marble_pil_ram_device.block_writes && !marble_pil_dsp_dirty && (outer_watchdog || !EFI_ERROR(clear));
    print("marble-pil-ramfs: released PASS=%d installed=%d dirty=%d reads=%d file_writes=%d block_reads=%d block_writes=%d opens=%d closes=%d watchdog_clear=%x dsp_start=%d\n",(uint64_t)passed,(uint64_t)marble_pil_ram_device.installed,(uint64_t)marble_pil_ram_device.dirty,(uint64_t)marble_pil_ramfs.reads,(uint64_t)marble_pil_ramfs.writes,(uint64_t)marble_pil_ram_device.block_reads,(uint64_t)marble_pil_ram_device.block_writes,(uint64_t)marble_pil_ramfs.opens,(uint64_t)marble_pil_ramfs.closes,clear,(uint64_t)marble_pil_auth_attempted);
    return passed;
}
static int marble_pil_powered_action(void)
{
    int passed=marble_pil_file_probe((const void *)(uintptr_t)PAYLOAD_BASE,marble_payload_bytes,8);
    if(marble_pil_dsp_dirty || marble_pil_deferred.saved || marble_pil_ram_device.dirty || marble_pil_ram_device.installed)return -1;
    return passed;
}
/* FAILED v34b: offline audit only; dispatcher/host refuse this path. */
static __attribute__((unused)) int marble_pil_bounded_boot_action(void)
{
    int passed=marble_pil_file_probe((const void *)(uintptr_t)PAYLOAD_BASE,marble_payload_bytes,9);
    if(marble_pil_dsp_dirty || marble_pil_deferred.saved || marble_pil_ram_device.dirty || marble_pil_ram_device.installed || marble_glink_observer.active || marble_glink_observer.dirty)return -1;
    return passed;
}
/* FAILED v38 PAS shutdown: retained for offline audit, never dispatched. */
static __attribute__((unused)) int marble_pil_smp2p_boot_action(void)
{
    if(!marble_smp2p_owned || !marble_smp2p_notifications)return -1;
    int passed=marble_pil_file_probe((const void *)(uintptr_t)PAYLOAD_BASE,marble_payload_bytes,10);
    if(marble_pil_dsp_dirty || marble_pil_deferred.saved || marble_pil_ram_device.dirty || marble_pil_ram_device.installed || marble_glink_observer.active || marble_glink_observer.dirty)return -1;
    return passed;
}
/* Offline retained-resource candidate; not exposed by the dispatcher. */
static __attribute__((unused)) int marble_pil_smp2p_reset_boot_action(void)
{
    if(!marble_smp2p_owned || !marble_smp2p_notifications)return -1;
    (void)marble_pil_file_probe((const void *)(uintptr_t)PAYLOAD_BASE,marble_payload_bytes,11);
    return -1; /* Planned or failed reset boundary: never destroy power clients. */
}
static int marble_pil_config(void)
{
    static const efi_guid guid={0x1f4f1da3,0x4d8d,0x4894,{0xba,0x2a,0x13,0xd3,0x20,0x7f,0x97,0x39}};
    uint64_t *table=NULL;
    if(!marble_pil_attempted || EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table || table[0]!=0x10003)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x18198;
    if(table[6]!=base+0x17d8)return 0;
    /* Exact native slot6: CHAR16 name -> cached config copy, 0x118 bytes.
     * Slot5 instead takes a numeric subsystem ID; never confuse these ABIs.
     * 17d8 -> 3a8c parses a named RAM configuration; no image/PAS call. */
    typedef efi_status (EFIAPI *GetConfig)(const char16 *,void *);
    static const struct {const char *name;const char16 *wide;} names[]={
        {"FULL_ADSP2",u"FULL_ADSP2"},{"FULL_SLPI",u"FULL_SLPI"}};
    unsigned passed=1;
    efi_status watchdog=BS->set_watchdog_timer(30,0,0,NULL);
    if(EFI_ERROR(watchdog))return 0;
    for(unsigned n=0;n<sizeof(names)/sizeof(names[0]);n++) {
        uint64_t config[35];memset(config,0,sizeof(config));
        efi_status status=((GetConfig)(uintptr_t)table[6])(names[n].wide,config);
        print("marble-pil: config=%s status=%x bytes=%d load_calls=0 pas_calls=0\n",names[n].name,status,(uint64_t)sizeof(config));
        if(EFI_ERROR(status)) {passed=0;continue;}
        for(unsigned i=0;i<35;i++)print("marble-pil-config: name=%s offset=%x word=%x\n",names[n].name,(uint64_t)i*8,config[i]);
    }
    efi_status clear=BS->set_watchdog_timer(0,0,0,NULL);
    print("marble-pil: config_complete PASS=%d watchdog_clear=%x dsp_start=0 image_load=0\n",(uint64_t)passed,clear);
    return passed && !EFI_ERROR(clear);
}
static int marble_pil_install_mode(const uint8_t *data,uint64_t bytes,unsigned retain_watchdog)
{
    static const efi_guid guid={0x1f4f1da3,0x4d8d,0x4894,{0xba,0x2a,0x13,0xd3,0x20,0x7f,0x97,0x39}};
    static const uint32_t rvas[]={0x16f8,0x171c,0x173c,0x175c,0x177c,0x17d8};
    if(marble_pil_attempted || bytes!=118784 || !data || retain_watchdog>1 ||
       (retain_watchdog && !marble_firmware_reset_required))return 0;
    uint64_t fingerprint=0xcbf29ce484222325ULL;
    for(uint64_t i=0;i<bytes;i++)fingerprint=(fingerprint^data[i])*0x100000001b3ULL;
    if((fingerprint!=0x9ebf57396f783d6dULL && fingerprint!=0x663099bf3f2c5867ULL) || *(const uint32_t *)(data+0x15d8)!=0x1400001a) {
        print("marble-pil: derivative_fingerprint=0 calls=0\n");return 0;
    }
    void *existing=NULL;
    if(!EFI_ERROR(BS->locate_protocol(&guid,NULL,&existing)) && existing) {
        print("marble-pil: provider_already_present install_calls=0\n");return 0;
    }
    typedef efi_status (EFIAPI *LoadImage)(uint8_t,efi_handle,const void *,const void *,uint64_t,efi_handle *);
    typedef efi_status (EFIAPI *StartImage)(efi_handle,uint64_t *,char16 **);
    typedef efi_status (EFIAPI *UnloadImage)(efi_handle);
    if(!BS->load_image || !BS->start_image || !BS->unload_image)return 0;
    /* Set a firmware watchdog around the synchronous OEM constructors. This
     * is a fresh idle loader with the EFI watchdog previously disabled. */
    efi_status status=BS->set_watchdog_timer(30,0,0,NULL);
    print("marble-pil: noauto-v1 watchdog_status=%x bytes=%d dsp_start=0 pas_calls=0 partition_writes=0\n",status,bytes);
    if(EFI_ERROR(status))return 0;
    marble_pil_attempted=1;
    marble_pil_header_only=fingerprint==0x663099bf3f2c5867ULL;
    efi_handle image=NULL;
    status=((LoadImage)BS->load_image)(0,IMAGE,NULL,data,bytes,&image);
    print("marble-pil: load_image status=%x handle=%x\n",status,(uint64_t)(uintptr_t)image);
    int passed=0;
    if(!EFI_ERROR(status) && image) {
        status=((StartImage)BS->start_image)(image,NULL,NULL);
        print("marble-pil: start_image status=%x automatic_images=0\n",status);
        if(!EFI_ERROR(status)) {
            uint64_t *table=NULL;
            status=BS->locate_protocol(&guid,NULL,(void **)&table);
            if(!EFI_ERROR(status) && table && table[0]==0x10003) {
                uint64_t base=(uint64_t)(uintptr_t)table-0x18198;unsigned match=1;
                for(unsigned i=0;i<6;i++)if(table[i+1]!=base+rvas[i])match=0;
                passed=match;
                print("marble-pil: protocol_present=1 revision=%x exact_table=%d load_calls=0 config_calls=0 dsp_start=0\n",table[0],(uint64_t)match);
            }
        } else {
            efi_status unloaded=((UnloadImage)BS->unload_image)(image);
            print("marble-pil: failed_image unload_status=%x\n",unloaded);
        }
    }
    efi_status cleared=retain_watchdog?~0ULL:BS->set_watchdog_timer(0,0,0,NULL);
    print("marble-pil: install_complete PASS=%d watchdog_clear=%x reboot_required=1 functional_acceptance=0 watchdog_retained=%d\n",(uint64_t)passed,cleared,(uint64_t)retain_watchdog);
    return passed && (retain_watchdog || !EFI_ERROR(cleared));
}
static int marble_pil_install(const uint8_t *data,uint64_t bytes){return marble_pil_install_mode(data,bytes,0);}
#endif
#endif
