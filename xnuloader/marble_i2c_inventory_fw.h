/* Isolated I2C discovery/reference/configuration profiles. No native I2C
 * Open/Read/Write/Transfer/Close, bus commands, amp reset or amplifier writes. */
#ifndef MARBLE_I2C_INVENTORY_FW_H
#define MARBLE_I2C_INVENTORY_FW_H
#if defined(XNU_MARBLE_I2C_ONLY) || defined(XNU_MARBLE_AUDIO_I2C_HOLD)
#include "marble_i2c_code.h"
#include "marble_i2c_dt_code.h"
#include "marble_i2c_clock_code.h"
#include "marble_i2c_board.h"
#include "marble_i2c_clock_lease.h"
#include "marble_i2c_registers.h"
#include "marble_i2c_route.h"
#include "marble_i2c_config_lease.h"
static unsigned marble_i2c_identity_qualified,marble_i2c_route_qualified,marble_i2c_config_writes;
static unsigned marble_i2c_config_gpio_writes;
static uint32_t marble_i2c_route_initial[MARBLE_I2C_ROUTE_WORDS];
static const char *marble_i2c_config_phase;
static MarbleI2cConfigLease marble_i2c_config_lease;
static marble_fw_clock *marble_i2c_clock_provider;
static uint64_t marble_i2c_clock_ids[5];
static const char *marble_i2c_clock_phase;
static MarbleI2cClockLease marble_i2c_clock_lease;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
static unsigned marble_i2c_window_qualified,marble_i2c_window_attempted,marble_i2c_window_passed;
#endif
static unsigned marble_i2c_inventory_attempted;
static uint64_t marble_i2c_inventory_base;
static uint64_t marble_i2c_dt_words[33];
static uint8_t marble_i2c_dt_blob[65536];
static unsigned marble_i2c_dt_blob_bytes;
static int marble_i2c_inventory(void)
{
    static const efi_guid guid={0xb27ae8b1,0x3e10,0x4d07,{0xab,0x5c,0xeb,0x9a,0x6d,0xc6,0xfa,0x8f}};
    static const unsigned rvas[]={0x27f8,0x2a8c,0x2ea4,0x2b9c,0x2ff8};
    uint64_t *provider=NULL,words[6],after[6],head=0,last=0;
    if(marble_i2c_inventory_attempted || marble_pil_attempted || marble_npa_attempted || marble_icb_attempted ||
       marble_rpmh_attempted || marble_cap.active || marble_cap.dirty || marble_thermal_latched)return 0;
    marble_i2c_inventory_attempted=1;
    efi_status status=BS->locate_protocol(&guid,NULL,(void **)&provider);
    uint64_t table=(uint64_t)(uintptr_t)provider,base=table-0xa0d8;
    if(EFI_ERROR(status) || table<0x8000a0d8 || !marble_rpmh_words(marble_npa_ram_word,table,words,6) || words[0]!=0x10000)return 0;
    for(unsigned i=0;i<5;i++)if(words[i+1]!=base+rvas[i])return 0;
    if(!marble_code_digest(marble_npa_ram_word,base+0x27f8,0x894,marble_i2c_code_digest) ||
       !marble_npa_ram_word(base+0xa850,&head))return 0;
    print("marble-i2c: qualified base=%x table=%x table_rva=a0d8 revision=10000 slots=5 code_rva=27f8 code_bytes=2196 code_digest=1 protocol_calls=0\n",base,table);
    for(unsigned i=0;i<5;i++)print("marble-i2c: slot index=%d pointer=%x rva=%x invoked=0\n",
        (uint64_t)i,words[i+1],(uint64_t)rvas[i]);
    unsigned stable=marble_rpmh_words(marble_npa_ram_word,table,after,6) && !memcmp(words,after,sizeof(words)) &&
        marble_npa_ram_word(base+0xa850,&last) && head==last;
    if(stable)marble_i2c_inventory_base=base;
    print("marble-i2c: complete PASS=%d table_unchanged=%d client_head=%x client_head_unchanged=%d protocol_calls=0 instance_selected=0 controller_qualified=0 MMIO_reads=0 MMIO_writes=0 GPIO_writes=0 clock_calls=0 DSP_start=0 storage_access=0 audio_acceptance=0\n",
        (uint64_t)stable,(uint64_t)stable,head,(uint64_t)stable);
    return stable;
}
static int marble_i2c_dt_inventory(void)
{
    if(!marble_i2c_inventory())return 0;
    static const efi_guid guid={0x0389b776,0x625f,0x11eb,{0x83,0xbe,0xc7,0x41,0xa9,0x13,0xde,0x34}};
    void *provider=NULL;uint64_t before[33],after[33],cache=0,last=0;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,&provider)) || !provider ||
       !marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)provider,before,33) ||
       !marble_npa_ram_word(marble_i2c_inventory_base+0xa848,&cache))return 0;
    print("marble-i2c-DT: observed table=%x words=33 cached_provider=%x calls=0 ABI_qualified=0\n",(uint64_t)(uintptr_t)provider,cache);
    for(unsigned i=0;i<33;i++)print("marble-i2c-DT: word index=%d value=%x\n",(uint64_t)i,before[i]);
    static const unsigned indices[]={6,20};
    for(unsigned i=0;i<2;i++) {
        uint64_t pointer=before[indices[i]],words[4];
        if((pointer&3) || !marble_rpmh_words(marble_npa_ram_word,pointer,words,4))return 0;
        print("marble-i2c-DT: code index=%d pointer=%x bytes=32 words=%x,%x,%x,%x invoked=0\n",
            (uint64_t)indices[i],pointer,words[0],words[1],words[2],words[3]);
    }
    unsigned stable=marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)provider,after,33) &&
        !memcmp(before,after,sizeof(before)) && marble_npa_ram_word(marble_i2c_inventory_base+0xa848,&last) && last==cache;
    if(stable)memcpy(marble_i2c_dt_words,before,sizeof(before));
    print("marble-i2c-DT: complete PASS=%d table_unchanged=%d cached_provider_unchanged=%d DT_queries=0 I2C_calls=0 MMIO_reads=0 MMIO_writes=0 GPIO_writes=0 allocation_calls=0 ABI_qualified=0 controller_qualified=0 audio_acceptance=0\n",
        (uint64_t)stable,(uint64_t)stable,(uint64_t)stable);
    return stable;
}
static int marble_i2c_config_snapshot(void)
{
    if(!marble_i2c_dt_inventory() || marble_i2c_dt_words[0]!=0x10001 ||
       marble_i2c_dt_words[6]!=0xa70149f8 || marble_i2c_dt_words[20]!=0xa702620c ||
       !marble_code_digest(marble_npa_ram_word,0xa70149f8,0x78,marble_i2c_dt_code_0) ||
       !marble_code_digest(marble_npa_ram_word,0xa702620c,0x14,marble_i2c_dt_code_1))return 0;
    /* The verified GetNode body loads this pointer. Capture the bounded blob
     * without invoking it. Runtime fixups are audited on the host; capture is
     * not qualification of the configuration or a controller. */
    uint64_t pointer=0,last=0;
    if(!marble_npa_ram_word(0xa7043aa0,&pointer) || pointer<0x80000000 || pointer>0x500000000ULL-65536)return 0;
    print("marble-i2c-config: observed pointer=%x pointer_slot=a7043aa0 code_ranges=2 DT_queries=0\n",pointer);
    uint64_t header=0;
    if(!marble_npa_ram_word(pointer,&header) || (uint32_t)header!=0xedfe0dd0)return 0;
    unsigned size=__builtin_bswap32((uint32_t)(header>>32));
    print("marble-i2c-config: header bytes=%d magic=d00dfeed payload_read=0\n",(uint64_t)size);
    if(size!=41865 || size>sizeof(marble_i2c_dt_blob))return 0;
    for(uint64_t page=pointer&~4095ULL;page<pointer+size;page+=4096) {
        uint64_t par=marble_fw_translation(page);
        if((par&1) || (par&0x0000fffffffff000ULL)!=page)return 0;
    }
    for(unsigned i=0;i<size;i++)marble_i2c_dt_blob[i]=*(volatile uint8_t *)(uintptr_t)(pointer+i);
    MarbleSha256Context ctx;uint8_t digest[32];marble_sha256_init(&ctx);
    marble_sha256_update(&ctx,marble_i2c_dt_blob,size);marble_sha256_final(&ctx,digest);
    unsigned same=!memcmp(digest,marble_i2c_dt_blob_digest,32),read=marble_npa_ram_word(0xa7043aa0,&last);
    unsigned stable=read && last==pointer;
    if(stable)marble_i2c_dt_blob_bytes=size;
    print("marble-i2c-config: complete PASS=%d bytes=%d ROM_blob_match=%d pointer_read=%d pointer_unchanged=%d capture_only=1 configuration_qualified=0 DT_queries=0 I2C_calls=0 MMIO_reads=0 MMIO_writes=0 GPIO_writes=0 original_blob_writes=0 audio_acceptance=0\n",
        (uint64_t)stable,(uint64_t)size,(uint64_t)same,(uint64_t)read,(uint64_t)stable);
    return stable;
}
static int marble_i2c_clock_lookup_cached(void)
{
    if(!marble_i2c_inventory_base || marble_i2c_clock_provider)return 0;
    MarbleI2cBoardConfig candidate=marble_i2c_board_candidate();
    if(!marble_i2c_board_candidate_valid(&candidate))return 0;
    static const efi_guid guid={0x241afae6,0x885f,0x4f6c,{0xa7,0xea,0xc2,0x8e,0xab,0x79,0xc3,0xe5}};
    marble_fw_clock *clock=NULL;uint64_t before[8],after[8],head=0,last=0;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&clock)) || !clock ||
       (uint64_t)(uintptr_t)clock<0x8001b0f8 || !marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)clock,before,8))return 0;
    uint64_t base=(uint64_t)(uintptr_t)clock-0x1b0f8;
    if(before[0]!=0x10009 || before[1]!=base+0x14e4 || before[2]!=base+0x1528 || before[3]!=base+0x1570 ||
       !marble_code_digest(marble_npa_ram_word,base+0x14e4,68,marble_i2c_clock_id_code) ||
       !marble_npa_ram_word(marble_i2c_inventory_base+0xa850,&head) || head)return 0;
    print("marble-i2c-board: candidate native_bytes=88 controller=880000 common=8c0000 QUP=2 core_index=16 se_index=0 pins=56,57 function=1 bus_khz=100 timing_se_khz=19200 hardware_calls=0\n");
    print("marble-i2c-clock: qualified base=%x table=%x table_rva=1b0f8 revision=10009 get_id_rva=14e4 code_bytes=68 code_digest=1\n",base,(uint64_t)(uintptr_t)clock);
    uint64_t ids[5];
    for(unsigned i=0;i<5;i++) {
        const char *name=i<4?marble_i2c_board_common[i]:marble_i2c_board_se[0];ids[i]=~0ULL;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
        if(marble_pil_progress)marble_pil_progress("audio-clock-ID-before",i);
#endif
        efi_status status=clock->get_id(clock,name,&ids[i]);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
        if(marble_pil_progress)marble_pil_progress("audio-clock-ID-return",status);
#endif
        print("marble-i2c-clock: lookup index=%d name=%s status=%x id=%x clock_writes=0\n",(uint64_t)i,name,status,ids[i]);
        if(EFI_ERROR(status) || !ids[i] || ids[i]==~0ULL)return 0;
        for(unsigned j=0;j<i;j++)if(ids[i]==ids[j])return 0;
    }
    unsigned stable=marble_rpmh_words(marble_npa_ram_word,(uint64_t)(uintptr_t)clock,after,8) && !memcmp(before,after,sizeof(before)) &&
        marble_npa_ram_word(marble_i2c_inventory_base+0xa850,&last) && head==last;
    print("marble-i2c-clock: complete PASS=%d lookups=5 table_unchanged=%d I2C_head_unchanged=%d clock_writes=0 frequency_requests=0 I2C_calls=0 GPIO_writes=0 controller_qualified=0 audio_acceptance=0\n",
        (uint64_t)stable,(uint64_t)stable,(uint64_t)stable);
    if(stable){marble_i2c_clock_provider=clock;memcpy(marble_i2c_clock_ids,ids,sizeof(ids));}
    return stable;
}
static int marble_i2c_clock_info(void)
{return marble_i2c_inventory() && marble_i2c_clock_lookup_cached();}
static int marble_i2c_clock_state(void *ctx,unsigned i,unsigned *enabled,unsigned *on,uint32_t *hz)
{
    marble_fw_clock *c=ctx;uint8_t e=255,n=255;uint32_t f=~0u;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-enabled-before",i);
#endif
    efi_status a=c->is_enabled(c,marble_i2c_clock_ids[i],&e);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-enabled-return",a);
#endif
    efi_status b=c->is_on(c,marble_i2c_clock_ids[i],&n);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-on-return",b);
#endif
    efi_status d=c->get_frequency(c,marble_i2c_clock_ids[i],&f);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-frequency-return",d);
    if(marble_pil_progress)marble_pil_progress("audio-clock-state-bits",((uint64_t)i<<16)|((uint64_t)e<<8)|n);
#endif
    print("marble-i2c-lease: state phase=%s index=%d id=%x enabled=%d on=%d hz=%d status=%x,%x,%x\n",
        marble_i2c_clock_phase,(uint64_t)i,marble_i2c_clock_ids[i],(uint64_t)e,(uint64_t)n,(uint64_t)f,a,b,d);
    /* The exact 4458..44e8 implementation returns -8
     * when the branch's domain has no active frequency-plan pointer; wrapper
     * maps -8 to EFI_NOT_STARTED and writes zero. Preserve that observation
     * as an unavailable sentinel, never as a measured zero-Hz clock. */
    unsigned unavailable=d==(EFI_ERROR_BIT|19) && !f;
    *enabled=e;*on=n;*hz=unavailable?~0u:f;
    return EFI_ERROR(a) || EFI_ERROR(b) || (EFI_ERROR(d) && !unavailable);
}
static int marble_i2c_clock_enable(void *ctx,unsigned i)
{
    marble_fw_clock *c=ctx;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-enable-before",i);
#endif
    efi_status s=c->enable(c,marble_i2c_clock_ids[i]);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-enable-return",s);
#endif
    print("marble-i2c-lease: enable index=%d id=%x status=%x\n",(uint64_t)i,marble_i2c_clock_ids[i],s);return EFI_ERROR(s);
}
static int marble_i2c_clock_disable(void *ctx,unsigned i)
{
    marble_fw_clock *c=ctx;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-disable-before",i);
#endif
    efi_status s=c->disable(c,marble_i2c_clock_ids[i]);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-clock-disable-return",s);
#endif
    print("marble-i2c-lease: disable index=%d id=%x status=%x\n",(uint64_t)i,marble_i2c_clock_ids[i],s);return EFI_ERROR(s);
}
static int marble_i2c_register_snapshot(void)
{
    if(!marble_i2c_clock_lease.active || marble_i2c_clock_lease.held!=31 || marble_i2c_clock_lease.dirty)return 0;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    /* GCC remains accessible while a downstream QUP wrapper may be gated.
     * Read only; fixed W9 follows the already qualified Mu trap convention. */
    static const uint64_t gcc[]={0x162010,0x133280,0x13328c,0x12e00c,0x12e004,0x12e008,0x12e014,0x12e018};
    for(unsigned j=0;j<8;j++) {
        uint64_t a=gcc[j],par=marble_fw_translation(a);uint32_t v=0;
        if((par&1) || (par&0x0000fffffffff000ULL)!=(a&~4095ULL))return 0;
        if(marble_pil_progress)marble_pil_progress("audio-GCC-read-before",a);
        __asm__ volatile("ldr w9, [%1]\nmov %w0, w9\ndmb sy":"=r"(v):"r"(a):"x9","memory");
        if(marble_pil_progress)marble_pil_progress("audio-GCC-read-value",v);
        print("aw: GCC index=%d address=%x value=%x writes=0\n",(uint64_t)j,a,(uint64_t)v);
    }
    /* Core, core2x and serial leaf must physically run before GENI access.
     * The gated AHB branch is deliberately not required to report ON. */
    static const unsigned required_on[]={0,1,4};
    for(unsigned j=0;j<3;j++) {
        uint8_t enabled=0,on=0;unsigned i=required_on[j];
        efi_status a=marble_i2c_clock_provider->is_enabled(marble_i2c_clock_provider,marble_i2c_clock_ids[i],&enabled);
        efi_status b=marble_i2c_clock_provider->is_on(marble_i2c_clock_provider,marble_i2c_clock_ids[i],&on);
        if(marble_pil_progress)marble_pil_progress("audio-register-clock-ready",((uint64_t)i<<16)|((uint64_t)enabled<<8)|on);
        if(EFI_ERROR(a) || EFI_ERROR(b) || enabled!=1 || on!=1)return 0;
    }
#endif
    /* All translations are checked before any register load. No new mapping
     * and no shared DT/provider pointer changes. HWCG AHB stays firmware owned. */
    for(unsigned i=0;i<8;i++) {
        uint64_t address=marble_i2c_ro_addresses[i];
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
        if(marble_pil_progress)marble_pil_progress("audio-register-AT-before",address);
#endif
        uint64_t par=marble_fw_translation(address);
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
        if(marble_pil_progress)marble_pil_progress("audio-register-AT-return",par);
#endif
        unsigned mapped=!(par&1) && (par&0x0000fffffffff000ULL)==(address&~4095ULL);
        print("marble-i2c-registers: translation index=%d address=%x par=%x identity=%d\n",(uint64_t)i,address,par,(uint64_t)mapped);
        if(!mapped)return 0;
    }
    uint32_t samples[2][8];
    for(unsigned round=0;round<2;round++) {
        for(unsigned i=0;i<8;i++) {
            uint64_t address=marble_i2c_ro_addresses[i];
            if(!marble_i2c_ro_address_allowed(address))return 0;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
            if(marble_pil_progress)marble_pil_progress("audio-register-read-before",address);
#endif
            samples[round][i]=*(volatile uint32_t *)(uintptr_t)address;
            __asm__ volatile("dmb sy" ::: "memory");
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
            if(marble_pil_progress)marble_pil_progress("audio-register-read-return",samples[round][i]);
#endif
            print("marble-i2c-registers: read round=%d index=%d address=%x value=%x writes=0\n",(uint64_t)round,(uint64_t)i,address,(uint64_t)samples[round][i]);
        }
        if(!round)BS->stall(100);
    }
    unsigned stable=!memcmp(samples[0],samples[1],sizeof(samples[0]));
    unsigned identity=stable && marble_i2c_ro_identity(samples[0]);
    marble_i2c_identity_qualified=identity;
    print("marble-i2c-registers: complete PASS=%d snapshot_captured=%d stable=%d protocol=%d idle=%d FIFO=%d identity_qualified=%d MMIO_reads=16 MMIO_writes=0 I2C_transactions=0 GPIO_writes=0 firmware_loads=0 controller_writes=0 physical_chip_id_verified=0 audio_acceptance=0\n",
        (uint64_t)stable,(uint64_t)stable,(uint64_t)stable,(uint64_t)((samples[0][4]>>8)&255),(uint64_t)!(samples[0][1]&0x1001),(uint64_t)!samples[0][3],(uint64_t)identity);
    return stable;
}
static int marble_i2c_route_snapshot(void)
{
    if(!marble_i2c_clock_lease.active || marble_i2c_clock_lease.held!=31 || marble_i2c_clock_lease.dirty)return 0;
    for(unsigned i=0;i<MARBLE_I2C_ROUTE_WORDS;i++) {
        uint64_t address=marble_i2c_route_addresses[i],par=marble_fw_translation(address);
        unsigned mapped=!(par&1) && (par&0x0000fffffffff000ULL)==(address&~4095ULL);
        print("marble-i2c-route: translation index=%d address=%x par=%x identity=%d\n",(uint64_t)i,address,par,(uint64_t)mapped);
        if(!mapped)return 0;
    }
    uint32_t samples[2][MARBLE_I2C_ROUTE_WORDS];
    for(unsigned r=0;r<2;r++) {
        for(unsigned i=0;i<MARBLE_I2C_ROUTE_WORDS;i++) {
            uint64_t address=marble_i2c_route_addresses[i];if(!marble_i2c_route_allowed(address))return 0;
            samples[r][i]=*(volatile uint32_t *)(uintptr_t)address;
            __asm__ volatile("dmb sy" ::: "memory");
            print("marble-i2c-route: read round=%d index=%d address=%x value=%x mask=%x writes=0\n",(uint64_t)r,(uint64_t)i,address,(uint64_t)samples[r][i],(uint64_t)marble_i2c_route_preserve_mask(i));
        }
        if(!r)BS->stall(100);
    }
    unsigned stable=marble_i2c_route_stable(samples[0],samples[1]);
    if(stable){memcpy(marble_i2c_route_initial,samples[0],sizeof(samples[0]));marble_i2c_route_qualified=1;}
    print("marble-i2c-route: complete PASS=%d words=41 snapshots=2 MMIO_reads=82 MMIO_writes=0 I2C_transactions=0 GPIO_writes=0 clock_frequency_requests=0 bus_ready=0 physical_chip_id_verified=0 audio_acceptance=0\n",(uint64_t)stable);
    return stable;
}
static int marble_i2c_config_read(void *ctx,uint64_t address,uint32_t *v)
{
    (void)ctx;if(!marble_i2c_config_allowed(address))return 1;
    *v=*(volatile uint32_t *)(uintptr_t)address;__asm__ volatile("dmb sy" ::: "memory");return 0;
}
static int marble_i2c_config_write(void *ctx,uint64_t address,uint32_t v)
{
    (void)ctx;if(!marble_i2c_config_allowed(address))return 1;
    *(volatile uint32_t *)(uintptr_t)address=v;__asm__ volatile("dsb sy" ::: "memory");
    uint32_t observed=*(volatile uint32_t *)(uintptr_t)address;__asm__ volatile("dmb sy" ::: "memory");marble_i2c_config_writes++;
    if(address>=0xf100000)marble_i2c_config_gpio_writes++;
    print("marble-i2c-setup: write phase=%s address=%x value=%x observed=%x\n",marble_i2c_config_phase,address,(uint64_t)v,(uint64_t)observed);
    return observed!=v;
}
#include "marble_i2c_amp_fw.h"
#ifdef XNU_MARBLE_AUDIO_AMP_PROFILE
static unsigned marble_i2c_irq_state(const char *phase)
{
    /* Actual FDT: SPI373, level-high => GIC INTID405, bank12 bit21.
     * Passive reads only. Never acknowledge, mask or reroute interrupts. */
    static const uint64_t regs[]={0x17100130,0x17100230,0x17100330};
    uint32_t words[3]={0};unsigned bits=0;
    for(unsigned i=0;i<3;i++) {
        uint64_t a=regs[i],par=marble_fw_translation(a);
        if((par&1) || (par&0x0000fffffffff000ULL)!=(a&~4095ULL))return 0;
        __asm__ volatile("ldr w9, [%1]\nmov %w0, w9\ndmb sy":"=r"(words[i]):"r"(a):"x9","memory");
        bits|=((words[i]>>21)&1u)<<i;
    }
    print("aw: IRQ phase=%s intid=405 enable=%x pending=%x active=%x bits=%d writes=0 IAR_reads=0\n",
        phase,(uint64_t)words[0],(uint64_t)words[1],(uint64_t)words[2],(uint64_t)bits);
    if(marble_pil_progress)marble_pil_progress(phase,bits);
    return 1;
}
#endif
#ifdef XNU_MARBLE_AUDIO_I2C_HOLD
static int (*marble_i2c_held_action)(void);
#endif
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
/* DATA-only action: DSP is intentionally running on return. Unlike the
 * outer PAS holder, DSP_dirty is not a reason to retain this quiet bus. */
static int (*marble_i2c_board_action)(void);
#endif
static int marble_i2c_config_cycle(unsigned amp)
{
    const uint32_t *v=marble_i2c_route_initial;
    if(!marble_i2c_identity_qualified || !marble_i2c_route_qualified || !marble_i2c_clock_lease.active ||
       v[3] || v[11] || v[12] || v[14] || v[15] || v[17] || v[18] || (v[25]&0x3fff) ||
       v[31]!=1 || v[33]!=1 || v[35]!=1 || v[37]!=1 || v[39]!=1 || (v[36]&3) || (v[38]&3) || (v[40]&2))return 0;
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++) {
        uint64_t a=marble_i2c_config_addresses[i],par=marble_fw_translation(a);
        if((par&1) || (par&0x0000fffffffff000ULL)!=(a&~4095ULL))return 0;
    }
    MarbleI2cConfigOps o={NULL,marble_i2c_config_read,marble_i2c_config_write};
    if(!marble_i2c_config_prepare(&o,&marble_i2c_config_lease))return 0;
    for(unsigned i=0;i<MARBLE_I2C_CONFIG_WORDS;i++)
        print("marble-i2c-setup: saved index=%d address=%x before=%x desired=%x\n",(uint64_t)i,marble_i2c_config_addresses[i],(uint64_t)marble_i2c_config_lease.saved[i],(uint64_t)marble_i2c_config_values[i]);
    marble_i2c_config_phase="apply";int applied=marble_i2c_config_apply(&o,&marble_i2c_config_lease);
    unsigned changed=marble_i2c_config_lease.touched;uint32_t ios=0,status=~0u;
    if(applied) {
        BS->stall(1000);ios=*(volatile uint32_t *)(uintptr_t)0x880908;status=*(volatile uint32_t *)(uintptr_t)0x880040;
        __asm__ volatile("dmb sy" ::: "memory");
    }
    print("marble-i2c-setup: applied PASS=%d changed=%x active=%d IOS=%x status=%x I2C_transactions=0\n",(uint64_t)applied,(uint64_t)changed,(uint64_t)marble_i2c_config_lease.active,(uint64_t)ios,(uint64_t)status);
    unsigned ready=applied && (ios&3)==3 && !(status&0x1001);
    int identified;
#ifdef XNU_MARBLE_AUDIO_BOARD_HELD
    if(marble_i2c_board_action)identified=ready && amp && marble_i2c_config_lease.active &&
        marble_i2c_clock_lease.active && marble_i2c_board_action();
    else
#endif
    identified=!amp || (ready && marble_i2c_amp_id());
#ifdef XNU_MARBLE_AUDIO_I2C_HOLD
    if(marble_i2c_held_action) {
        unsigned entered=ready && !amp && marble_i2c_config_lease.active && marble_i2c_clock_lease.active;
        print("ai: work enter=%d configuration_active=%d clocks_active=%d amp_commands=0\n",(uint64_t)entered,(uint64_t)marble_i2c_config_lease.active,(uint64_t)marble_i2c_clock_lease.active);
        identified=entered && marble_i2c_held_action();
        /* A failed/ambiguous PAS stop must retain the supporting leases.
         * The existing dirty path forbids release and preserves cold reset. */
        if(marble_pil_dsp_dirty)marble_i2c_amp_dirty=1;
        print("ai: work returned=%d DSP_dirty=%d amp_commands=0\n",(uint64_t)identified,(uint64_t)marble_pil_dsp_dirty);
    }
#endif
    marble_i2c_config_phase="restore";
    int restored=0;
    if(!marble_i2c_amp_dirty)restored=marble_i2c_config_lease.prepared?marble_i2c_config_restore(&o,&marble_i2c_config_lease):!marble_i2c_config_lease.dirty;
    else marble_i2c_config_lease.dirty=1;
    uint32_t after[MARBLE_I2C_ROUTE_WORDS];
    unsigned preserved=1;
    for(unsigned i=0;i<MARBLE_I2C_ROUTE_WORDS;i++) {
        after[i]=*(volatile uint32_t *)(uintptr_t)marble_i2c_route_addresses[i];__asm__ volatile("dmb sy" ::: "memory");
        /* CMD0 is non-restorable command history; writing its old value
         * could submit another command. All configuration/status stays exact. */
        uint32_t mask=amp && i==11?0:amp && i==12?~0x80u:marble_i2c_route_preserve_mask(i);
        if((v[i]^after[i])&mask)preserved=0;
        print("marble-i2c-setup: preserved index=%d value=%x mask=%x\n",(uint64_t)i,(uint64_t)after[i],(uint64_t)mask);
    }
    unsigned ok=ready && restored && preserved && identified;
    print("marble-i2c-setup: complete PASS=%d configured=%d restored=%d route_preserved=%d held=%x active=%d dirty=%d writes=%d bus_khz=100 I2C_transactions=%d reset_writes=%d amplifier_register_writes=%d firmware_loads=0 frequency_requests=0 audio_acceptance=0\n",
        (uint64_t)ok,(uint64_t)ready,(uint64_t)restored,(uint64_t)preserved,(uint64_t)marble_i2c_config_lease.touched,(uint64_t)marble_i2c_config_lease.active,(uint64_t)marble_i2c_config_lease.dirty,(uint64_t)marble_i2c_config_writes,(uint64_t)marble_i2c_amp_commands,(uint64_t)marble_i2c_amp_gpio_writes,(uint64_t)marble_i2c_amp_data_writes);
    return ok;
}
static int marble_i2c_clock_cycle(unsigned registers,unsigned route,unsigned setup,unsigned amp)
{
    /* This isolated 1ms peripheral lease uses the established 50C start
     * ceiling, not the legacy generic-diagnostic 40C chip threshold. Battery
     * preflight remains below 38C; no CPU/GPU rates or voltage requests. */
    MarbleThermalSample sample;
    int valid=marble_thermal_snapshot(marble_thermal_read,NULL,&sample);
    int cool=valid && sample.maximum_mc<50000;
#ifdef XNU_MARBLE_AUDIO_I2C_WINDOW
    if(marble_pil_progress)marble_pil_progress("audio-I2C-temperature-return",cool);
    print("aw: temperature valid=%d maximum_mc=%d limit_mc=50000 PASS=%d\n",(uint64_t)valid,(uint64_t)sample.maximum_mc,(uint64_t)cool);
    if(!cool || !marble_i2c_window_qualified || !marble_i2c_clock_provider)return 0;
#else
    print("marble-i2c-temperature: phase=before valid=%d maximum_mc=%d limit_mc=50000 PASS=%d\n",(uint64_t)valid,(uint64_t)sample.maximum_mc,(uint64_t)cool);
    if(!cool || !marble_i2c_clock_info())return 0;
#endif
    marble_fw_clock *c=marble_i2c_clock_provider;uint64_t before[8],after[8],head=~0ULL;
    uint64_t table=(uint64_t)(uintptr_t)c,base=table-0x1b0f8;
    if(!marble_rpmh_words(marble_npa_ram_word,table,before,8) ||
       before[4]!=base+0x15b8 || before[5]!=base+0x15fc || before[7]!=base+0x1688 ||
       !marble_code_digest(marble_npa_ram_word,base+0x1528,420,marble_i2c_clock_cycle_code) ||
       !marble_code_digest(marble_npa_ram_word,base+0x4458,148,marble_i2c_frequency_metadata_code))return 0;
    print("marble-i2c-lease: qualified code_rva=1528 code_bytes=420 code_digest=1 metadata_rva=4458 metadata_bytes=148 metadata_digest=1 clocks=5 frequency_requests=0 controller_calls=0\n");
    MarbleI2cClockOps o={c,marble_i2c_clock_state,marble_i2c_clock_enable,marble_i2c_clock_disable};
    marble_i2c_clock_phase="before";
    if(!marble_i2c_clocks_prepare(&o,&marble_i2c_clock_lease))return 0;
    marble_i2c_clock_phase="acquire";
    int acquired=marble_i2c_clocks_acquire(&o,&marble_i2c_clock_lease);
    unsigned held=marble_i2c_clock_lease.held;
    print("marble-i2c-lease: acquired PASS=%d held=%x uncertain=%x active=%d register_access_qualified=0\n",(uint64_t)acquired,(uint64_t)held,
        (uint64_t)marble_i2c_clock_lease.uncertain,(uint64_t)marble_i2c_clock_lease.active);
    int snapshot=!registers || (acquired && marble_i2c_register_snapshot());
    int routed=!route || (snapshot && acquired && marble_i2c_route_snapshot());
    int configured=!setup || (routed && snapshot && acquired && marble_i2c_config_cycle(amp));
    if(acquired)BS->stall(1000);
    marble_i2c_clock_phase="restore";
    int restored=0;
    if(!marble_i2c_amp_dirty)restored=marble_i2c_clock_lease.prepared?marble_i2c_clocks_restore(&o,&marble_i2c_clock_lease):!marble_i2c_clock_lease.dirty;
    else marble_i2c_clock_lease.dirty=1;
    unsigned stable=marble_rpmh_words(marble_npa_ram_word,table,after,8) && !memcmp(before,after,sizeof(before)) &&
        marble_npa_ram_word(marble_i2c_inventory_base+0xa850,&head) && !head;
    valid=marble_thermal_snapshot(marble_thermal_read,NULL,&sample);cool=valid && sample.maximum_mc<55000;
    print("marble-i2c-temperature: phase=after valid=%d maximum_mc=%d limit_mc=55000 PASS=%d\n",(uint64_t)valid,(uint64_t)sample.maximum_mc,(uint64_t)cool);
    print("marble-i2c-lease: complete PASS=%d acquired=%d restored=%d table_unchanged=%d I2C_head_unchanged=%d held=%x uncertain=%x active=%d dirty=%d hold_us=1000 frequency_requests=0 frequency_measurement=0 register_access_qualified=0 I2C_calls=0 GPIO_writes=%d controller_MMIO_reads=%d controller_MMIO_writes=%d audio_acceptance=0\n",
        (uint64_t)(acquired && restored && stable && cool && snapshot && routed && configured),(uint64_t)acquired,(uint64_t)restored,(uint64_t)stable,(uint64_t)!head,
        (uint64_t)marble_i2c_clock_lease.held,(uint64_t)marble_i2c_clock_lease.uncertain,(uint64_t)marble_i2c_clock_lease.active,(uint64_t)marble_i2c_clock_lease.dirty,(uint64_t)(marble_i2c_config_gpio_writes+marble_i2c_amp_gpio_writes),(uint64_t)((registers && snapshot?16:0)+(route && routed?82:0)),(uint64_t)(marble_i2c_config_writes-marble_i2c_config_gpio_writes+marble_i2c_amp_controller_writes));
    return acquired && restored && stable && cool && snapshot && routed && configured;
}
#endif
#endif
