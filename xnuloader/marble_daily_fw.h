/* Read-only discovery for marble. Protocol ABI references, hashes and licenses
 * are retained in out/daily-v1-plan/references.json. Never initialize a charger,
 * take a charging action, change PMIC settings, or infer temperature from TSENS. */
#ifndef XNULOADER_MARBLE_DAILY_FW_H
#define XNULOADER_MARBLE_DAILY_FW_H
#ifdef XNU_MARBLE
#include "marble_smem.h"
#include "marble_glink_fw.h"
#if 0 /* FAILED: whole-region SMEM snapshots cross remote security partitions.
       * Kept for source audit only. AT success does not qualify a physical read.
       * Never re-enable or expose this through the USB command dispatcher. */
static void marble_smem_visit(void *ctx,const MarbleSmemPartition *p,
                              unsigned item,unsigned at,unsigned size,unsigned cached)
{
    (void)ctx;
    if(!((p->host0==0 && (p->host1==2 || p->host1==3)) ||
         (p->host1==0 && (p->host0==2 || p->host0==3)) ||
         (p->host0==0xfffe && p->host1==0xfffe)))return;
    if(item==~0u)
        print("marble-smem: partition hosts=%d,%d offset=%x size=%x cacheline=%d\n",
              (uint64_t)p->host0,(uint64_t)p->host1,(uint64_t)p->offset,(uint64_t)p->size,(uint64_t)p->cacheline);
    else
        print("marble-smem: item hosts=%d,%d id=%d offset=%x bytes=%d cached=%d\n",
              (uint64_t)p->host0,(uint64_t)p->host1,(uint64_t)item,(uint64_t)at,(uint64_t)size,(uint64_t)cached);
}
static int marble_smem_probe(void)
{
    const uint64_t base=0x80900000;const unsigned bytes=0x200000;
    print("marble-smem: discovery-v1 pa=%x bytes=%x writes=0 dsp_start=0 ipcc=0\n",base,(uint64_t)bytes);
    for(unsigned off=0;off<bytes;off+=4096) {
        uint64_t par=marble_fw_translation(base+off);
        if((par&1) || (par&0x0000fffffffff000ULL)!=base+off) {
            print("marble-smem: unmapped/nonidentity offset=%x par=%x; no target reads\n",(uint64_t)off,par);
            return 0;
        }
    }
    uint64_t allocation=0;
    efi_status status=BS->allocate_pages(AllocateAnyPages,EfiLoaderData,bytes/4096,&allocation);
    if(EFI_ERROR(status) || !allocation)return 0;
    uint8_t *snapshot=(uint8_t *)(uintptr_t)allocation;
    const volatile uint8_t *shared=(const volatile uint8_t *)(uintptr_t)base;
    for(unsigned i=0;i<bytes;i++)snapshot[i]=shared[i];
    unsigned partitions=0,items=0;
    int ok=marble_smem_parse(snapshot,bytes,marble_smem_visit,NULL,&partitions,&items);
    status=BS->free_pages(allocation,bytes/4096);
    print("marble-smem: complete metadata_valid=%d partitions=%d items=%d free_status=%x live_consistency_unknown=1 functional_acceptance=0\n",
          (uint64_t)ok,(uint64_t)partitions,(uint64_t)items,status);
    return ok && !EFI_ERROR(status);
}
#endif
typedef struct {
    uint32_t remote,item,size,pad;
    void *address;
    uint32_t flags,reserved;
} MarbleSmemGetParams;
_Static_assert(sizeof(MarbleSmemGetParams)==32,"Mu SMEM extended parameter size");
_Static_assert(offsetof(MarbleSmemGetParams,address)==16,"Mu SMEM extended address offset");
typedef struct {
    uint64_t revision;
    void *allocate; /* Never called. */
    efi_status (EFIAPI *get)(int,unsigned *,void **);
    void *allocate_ex; /* Never called. */
    efi_status (EFIAPI *get_ex)(MarbleSmemGetParams *);
} MarbleSmemFirmware;
typedef struct { uint32_t device,channel; } MarbleAdcChannel;
typedef struct { uint32_t status;int32_t physical,percent,microvolts,code; } MarbleAdcResult;
typedef struct {
    uint64_t revision;
    efi_status (EFIAPI *get_channel)(const char *,uint32_t,MarbleAdcChannel *);
    efi_status (EFIAPI *read)(MarbleAdcChannel *,MarbleAdcResult *);
    void *recalibrate,*synchronous,*threshold,*forced_batt_id;
} MarbleAdcFirmware;
_Static_assert(sizeof(MarbleAdcChannel)==8,"ADC channel ABI");
_Static_assert(sizeof(MarbleAdcResult)==20,"ADC result ABI");
/* Valid only after a successful conversions=1 call. BSS storage also avoids
 * an initialized word preceding the 8KiB-aligned legacy battery mailbox. */
static int marble_adc_last_temperature,marble_adc_last_voltage;
static int marble_adc_ram_word(uint64_t address,uint64_t *value)
{
    /* Follow only audited PmicDxe DAL heap/image objects, never peripheral
     * register windows or SMEM. All inspected words must be identity mapped. */
    if(address<0x80000000ULL || address>=0x500000000ULL || (address&7))return 0;
    uint64_t par=marble_fw_translation(address);
    if((par&1) || (par&0x0000fffffffff000ULL)!=(address&~4095ULL))return 0;
    *value=*(volatile uint64_t *)(uintptr_t)address;return 1;
}
static int marble_scm_info(void)
{
    /* Supplied Mu ScmDxeLA e6ff7055...95812. Inventory only: neither
     * protocol callbacks nor SMC are issued. The same revision has two ABIs. */
    static const efi_guid guid={0x77ed108d,0x8524,0x4b8b,{0x9d,0x2e,0x34,0x98,0x7a,0xec,0xb9,0xc1}};
    static const uint32_t legacy[]={0x17e8,0x1a4c,0x1aa8,0x2b7c,0x1e78,0x15a0,0x1858,0x2d94};
    static const uint32_t modern[]={0x3db0,0x3db8,0x3dc0,0x42e8,0x3dc8,0x35fc,0x3cb4,0x4454,0x452c,0x3d34};
    void *table=NULL;efi_status status=BS->locate_protocol(&guid,NULL,&table);
    print("marble-scm: inventory-v1 status=%x present=%d calls=0 SMC=0\n",status,(uint64_t)(!EFI_ERROR(status) && table));
    if(EFI_ERROR(status) || !table)return 0;
    uint64_t address=(uint64_t)(uintptr_t)table,revision;
    if(!marble_adc_ram_word(address,&revision) || revision!=0x40001)return 0;
    for(unsigned mode=0;mode<2;mode++) {
        const uint32_t *rvas=mode?modern:legacy;
        unsigned count=mode?10:8;uint64_t base=address-(mode?0xd130:0xd0d8);unsigned match=1;
        for(unsigned i=0;i<count;i++) {
            uint64_t pointer;
            if(!marble_adc_ram_word(address+8*(i+1),&pointer) || pointer!=base+rvas[i]) {match=0;break;}
        }
        if(!match)continue;
        print("marble-scm: exact_table=1 ABI=%s revision=%x image=%x call2_rva=%x slots=%d calls=0 SMC=0 DSP_start=0\n",mode?"modern64":"legacy32",revision,base,(uint64_t)rvas[6],(uint64_t)count);
        return 1;
    }
    print("marble-scm: exact_table=0 revision=%x calls=0 SMC=0\n",revision);return 0;
}
static unsigned marble_scm_available_mask;
static int marble_scm_availability(void)
{
    marble_scm_available_mask=0;
    if(!marble_scm_info())return 0;
    static const efi_guid guid={0x77ed108d,0x8524,0x4b8b,{0x9d,0x2e,0x34,0x98,0x7a,0xec,0xb9,0xc1}};
    uint64_t *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0xd130;
    if(table[0]!=0x40001 || table[1]!=base+0x3db0 || table[7]!=base+0x3cb4 || table[10]!=base+0x3d34)return 0;
    /* Audited 3cb4 -> 3880 -> 4734: This, uint32 fn, uint32 arginfo,
     * ten uint64 params, four uint64 results. Result[0] is a transport
     * sentinel, result[1] is secure x1. Do not issue the queried functions. */
    typedef efi_status (EFIAPI *Call2)(void *,uint32_t,uint32_t,const uint64_t *,uint64_t *);
    static const uint32_t targets[]={0x02000201,0x02000202,0x02000205,0x02000206};
    if(EFI_ERROR(BS->set_watchdog_timer(30,0,0,NULL)))return 0;
    unsigned passed=1;
    for(unsigned i=0;i<4;i++) {
        uint64_t params[10]={targets[i]},results[4]={0,0,0,0};
        uint64_t began=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
        efi_status status=((Call2)(uintptr_t)table[7])(table,0xc2000601,1,params,results);
        uint64_t us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/freq;
        unsigned ok=!EFI_ERROR(status) && results[0]==1 && results[1]<=1 && us<1000000;
        print("marble-scm: availability target=%x status=%x transport=%x available=%d extra=%x,%x elapsed_us=%d PASS=%d PAS_init=0 auth_reset=0 dsp_start=0\n",(uint64_t)targets[i],status,results[0],results[1],results[2],results[3],us,(uint64_t)ok);
        if(!ok) {passed=0;break;}
        if(results[1])marble_scm_available_mask|=1u<<i;
    }
    efi_status clear=BS->set_watchdog_timer(0,0,0,NULL);
    print("marble-scm: availability_complete PASS=%d watchdog_clear=%x mutating_calls=0\n",(uint64_t)(passed && !EFI_ERROR(clear)),clear);
    return passed && !EFI_ERROR(clear);
}
static int marble_adc_read_guard(uint64_t base,unsigned device)
{
    uint64_t root,count,handle,table,entry,client,dev,hardware,config,timeout;
    if(device>3 || !marble_adc_ram_word(base+0x2d8e0,&root) ||
       !marble_adc_ram_word(root+8,&count) || device>=(uint32_t)count || (uint32_t)count>4 ||
       !marble_adc_ram_word(base+0x2d8f0+16*device,&handle) ||
       !marble_adc_ram_word(handle+8,&table) ||
       !marble_adc_ram_word(table+0x68,&entry) || entry!=base+0x1ced0 ||
       !marble_adc_ram_word(handle+0x10,&client) ||
       !marble_adc_ram_word(client+0x10,&dev) ||
       !marble_adc_ram_word(dev+0x80,&hardware) ||
       !marble_adc_ram_word(hardware+8,&config) ||
       !marble_adc_ram_word(config+0x40,&timeout))return 0;
    /* 1d1c4 -> 24e28: configure, bounded status polling at24cb4, result
     * read, disable on success/error. SPMI read retry count is five. There
     * are normal ADC mux/enable writes, no charger/JEITA/threshold changes. */
    unsigned us=(uint32_t)timeout;
    print("marble-adc: read_backend=exact-VADC configured_timeout_us=%d conversion_enable_writes=1 charger_changes=0\n",(uint64_t)us);
    return us && us<=500000;
}
static int marble_adc_channels(unsigned conversions)
{
    if(conversions) {marble_adc_last_temperature=-273;marble_adc_last_voltage=-1;}
    static const efi_guid guid={0x61740cc4,0x916f,0x4471,{0xb0,0xdc,0x00,0xc6,0xf4,0x3a,0x7f,0xc6}};
    MarbleAdcFirmware *c=NULL;
    efi_status status=BS->locate_protocol(&guid,NULL,(void **)&c);
    print("marble-adc: channel-discovery-v2 status=%x present=%d conversion_requested=%d init_calls=0 charger_changes=0\n",status,(uint64_t)(!EFI_ERROR(status) && c),(uint64_t)conversions);
    if(EFI_ERROR(status) || !c)return 0;
    /* Exact supplied Mu PmicDxe SHA256 c19051e7...f5f75. The matching
     * EFIAdc.h revision is 0x10004. No generic/older FG ABI is used. */
    uint64_t base=(uint64_t)(uintptr_t)c-0x2b938;
    if(c->revision!=0x10004 || (uint64_t)(uintptr_t)c->get_channel!=base+0x4074 ||
       (uint64_t)(uintptr_t)c->read!=base+0x40d8 ||
       (uint64_t)(uintptr_t)c->recalibrate!=base+0x41cc ||
       (uint64_t)(uintptr_t)c->synchronous!=base+0x42b8 ||
       (uint64_t)(uintptr_t)c->threshold!=base+0x42c0 ||
       (uint64_t)(uintptr_t)c->forced_batt_id!=base+0x42c8) {
        print("marble-adc: exact_table=0 lookup_calls=0\n");return 0;
    }
    uint64_t par=marble_fw_translation(base+0x2c9f8);
    if((par&1) || (par&0x0000fffffffff000ULL)!=((base+0x2c9f8)&~4095ULL))return 0;
    uint64_t handle=*(volatile uint64_t *)(uintptr_t)(base+0x2c9f8);
    print("marble-adc: exact_table=1 initialized_handle=%d\n",(uint64_t)(handle!=0));
    if(!handle) {print("marble-adc: initialization_refused lookup_calls=0\n");return 0;}
    static const char *names[]={"BATT_THERM","BATT_THERM_PU_30K","BATT_THERM_PU_400K","BATT_THERM_OHMS","VBATT","VBATT_2S","VPH_PWR"};
    unsigned found=0;
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        MarbleAdcChannel q={~0u,~0u};uint32_t length=0;
        while(names[i][length])length++;
        status=c->get_channel(names[i],length+1,&q);
        unsigned present=!EFI_ERROR(status);found+=present;
        print("marble-adc: name=%s status=%x device=%d channel=%d present=%d conversion_calls=0\n",names[i],status,(uint64_t)q.device,(uint64_t)q.channel,(uint64_t)present);
        if(conversions && present && (i==0 || i==4)) {
            if(!marble_adc_read_guard(base,q.device)) {print("marble-adc: conversion_refused backend_or_timeout=0\n");return 0;}
            MarbleAdcResult result={0,-273,0,0,0};
            uint64_t ticks=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
            status=c->read(&q,&result);
            uint64_t elapsed=(READ_SYSREG(CNTVCT_EL0)-ticks)*1000000/frequency;
            unsigned valid=!EFI_ERROR(status) && result.status==1 && elapsed<600000 &&
                (i==0?(result.physical>=-20 && result.physical<=70):(result.physical>=2500 && result.physical<=5000));
            print("marble-adc: sample=%s status=%x result_status=%d physical=%d microvolts=%d code=%d elapsed_us=%d plausible=%d units=%s soc_telemetry=0\n",names[i],status,(uint64_t)result.status,(uint64_t)(int64_t)result.physical,(uint64_t)(int64_t)result.microvolts,(uint64_t)(int64_t)result.code,elapsed,(uint64_t)valid,i==0?"degC":"mV");
            if(!valid)return 0;
            if(i==0)marble_adc_last_temperature=result.physical;
            if(i==4)marble_adc_last_voltage=result.physical;
        }
    }
    print("marble-adc: complete found=%d conversion_requested=%d soc_telemetry=0\n",(uint64_t)found,(uint64_t)conversions);
    return !conversions || (marble_adc_last_temperature>=-20 && marble_adc_last_temperature<=70 &&
        marble_adc_last_voltage>=2500 && marble_adc_last_voltage<=5000);
}
static int marble_smem_info(unsigned channels)
{
    static const efi_guid guid={0xf4e5c7d0,0xd239,0x47cb,{0xaa,0xcd,0x7f,0x66,0xef,0x76,0x32,0x38}};
    MarbleSmemFirmware *c=NULL;
    efi_status status=BS->locate_protocol(&guid,NULL,(void **)&c);
    print("marble-smem-fw: protocol status=%x present=%d raw_region_reads=0 allocate_calls=0 dsp_start=0\n",
          status,(uint64_t)(!EFI_ERROR(status) && c));
    if(EFI_ERROR(status) || !c)return 0;
    print("marble-smem-fw: revision=%x\n",c->revision);
    /* Match the exact Mu SmemDxe table and code locations recorded in the
     * offline audit. GetFunc can lazily initialize SMEM, so inspect its known
     * ordinary-image state first and refuse unless firmware already did it.
     * This avoids the init/version allocation path at RVA 0x1ffc entirely. */
    uint64_t base=(uint64_t)(uintptr_t)c-0x8098;
    if(c->revision!=0x10001 || (uint64_t)(uintptr_t)c->get!=base+0x1470 ||
       (uint64_t)(uintptr_t)c->allocate!=base+0x1444 ||
       (uint64_t)(uintptr_t)c->allocate_ex!=base+0x14b4 ||
       (uint64_t)(uintptr_t)c->get_ex!=base+0x14c8) {
        print("marble-smem-fw: ABI differs; get_calls=0\n");return 0;
    }
    uint64_t par=marble_fw_translation(base+0x8208);
    if(par&1)return 0;
    unsigned state=*(volatile uint32_t *)(uintptr_t)(base+0x8208);
    print("marble-smem-fw: initialized_state=%d expected=3\n",(uint64_t)state);
    if(state!=3) {print("marble-smem-fw: lazy initialization refused; get_calls=0\n");return 0;}
    static const int items[]={3,137,429,443,430,481};
    for(unsigned i=0;i<sizeof(items)/sizeof(items[0]);i++) {
        unsigned bytes=0;void *address=NULL;
        status=c->get(items[i],&bytes,&address);
        print("marble-smem-fw: item=%d status=%x address=%x bytes=%d present=%d payload_reads=0\n",
              (uint64_t)items[i],status,(uint64_t)(uintptr_t)address,(uint64_t)bytes,
              (uint64_t)(!EFI_ERROR(status) && address && bytes && bytes<=0x200000));
    }
    if(channels) {
        /* Mu's exact SmemDxe: get_ex wrapper 0x14c8 -> helper 0x2348.
         * Fields read at 0(remote),4(item),8(size),16(address),24(flags).
         * Bit30 at 0x2448 suppresses fallback to the global lookup. AllocEx
         * is a different slot (0x14b4) and is never entered here. */
        static const unsigned ids[]={423,429,443,430,481,478,479,480};
        for(unsigned remote=2;remote<=3;remote++)for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) {
            MarbleSmemGetParams q={remote,ids[i],0,0,NULL,0x40000000,0};
            status=c->get_ex(&q);
            print("marble-smem-fw: remote=%d item=%d status=%x address=%x bytes=%d flags=%x payload_reads=0\n",
                  (uint64_t)remote,(uint64_t)q.item,status,(uint64_t)(uintptr_t)q.address,
                  (uint64_t)q.size,(uint64_t)q.flags);
        }
    }
    print("marble-smem-fw: complete; global lookup does not enumerate remote partitions or prove DSP service readiness\n");
    return 1;
}
#endif
#ifdef XNU_MARBLE
static int marble_npa_info(void)
{
    /* Supplied Mu NpaDxe SHA256 9ced898c...e9c09. Table 120b8,
     * revision 10003. Validate every published entry before using any ABI.
     * Inventory only: even client creation can invoke resource callbacks. */
    static const efi_guid guid={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    static const uint32_t rvas[]={
        0x1430,0x1448,0x1474,0x148c,0x14a4,0x14bc,0x14d4,0x14f8,
        0x151c,0x1540,0x1564,0x1588,0x15a0,0x15b8,0x15e4,0x15fc,
        0x1614,0x162c,0x1644,0x1670,0x1688,0x16b4,0x16e0,0x170c,
        0x1738,0x1764,0x177c,0x1794,0x17ac,0x17c4,0x17dc,0x1808,
        0x1820,0x1838,0x1864,0x187c,0x1894,0x18ac,0x18c4,0x18dc,
        0x18f4,0x1920,0x1938,0x1950,0x197c,0x1994,0x19ac,0x19d8,
        0x1a04,0x1a1c,0x1a34,0x1a60,0x1a8c,0x1ab8};
    void *table=NULL;efi_status status=BS->locate_protocol(&guid,NULL,&table);
    print("marble-npa: inventory-v1 status=%x present=%d protocol_calls=0 resource_votes=0 DSP_start=0\n",status,(uint64_t)(!EFI_ERROR(status) && table));
    if(EFI_ERROR(status) || !table)return 0;
    uint64_t address=(uint64_t)(uintptr_t)table,revision=0;
    if(address<0x800120b8ULL || !marble_adc_ram_word(address,&revision) || revision!=0x10003)return 0;
    uint64_t base=address-0x120b8;
    for(unsigned i=0;i<sizeof(rvas)/sizeof(rvas[0]);i++) {
        uint64_t pointer=0;
        if(!marble_adc_ram_word(address+8*(i+1),&pointer) || pointer!=base+rvas[i]) {
            print("marble-npa: exact_table=0 mismatch_slot=%d protocol_calls=0\n",(uint64_t)(i+1));return 0;
        }
    }
    print("marble-npa: exact_table=1 revision=%x image=%x slots=%d create_rva=1448 complete_rva=1474 issue_rva=14a4 destroy_rva=15e4 protocol_calls=0 resource_votes=0 DSP_start=0\n",revision,base,(uint64_t)(sizeof(rvas)/sizeof(rvas[0])));
    return 1;
}
static int marble_proxy_ram_string(uint64_t address,char out[97])
{
    if(!address || address>~0ULL-96)return 0;
    uint64_t cached=~0ULL,word=0;
    for(unsigned offset=0;offset<96;offset++) {
        uint64_t at=address+offset,aligned=at&~7ULL;
        if(aligned!=cached) {if(!marble_adc_ram_word(aligned,&word))return 0;cached=aligned;}
        unsigned c=(unsigned)(word>>(8*(at&7)))&255;out[offset]=(char)c;
        if(!c)return 1;
        if(c<32 || c>126)return 0;
    }
    out[96]=0;return 0;
}
static int marble_npa_ram_word(uint64_t address,uint64_t *out)
{
    /* Native DAL graph allocations may be four-byte aligned (v27b).
     * Reassemble from checked aligned ordinary-RAM reads, including page
     * boundaries. Keep the stricter ADC/MMIO-related helper unchanged. */
    uint64_t low=0,high=0,aligned=address&~7ULL;unsigned shift=(unsigned)(address&7)*8;
    if(address>~0ULL-7 || !marble_adc_ram_word(aligned,&low))return 0;
    if(!shift) {*out=low;return 1;}
    if(!marble_adc_ram_word(aligned+8,&high))return 0;
    *out=(low>>shift)|(high<<(64-shift));return 1;
}
typedef struct {
    uint64_t resource,plugin,clients,aggregates;
    uint32_t state,maximum;
} MarbleNpaLpi;
static MarbleNpaLpi marble_npa_lpi[2];
static unsigned marble_npa_graph_qualified,marble_npa_attempted,marble_npa_dirty,marble_npa_active,marble_npa_config_qualified;
static void *marble_npa_clients[2];
static uint64_t marble_npa_vcs_rails[2];
static unsigned marble_npa_vcs_default_mask;
static int marble_npa_resources(void)
{
    marble_npa_graph_qualified=0;
    if(!marble_npa_info())return 0;
    static const efi_guid guid={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    void *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,&table)) || !table)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x120b8,link=0;
    /* Native 5f4c traverses this already initialized resource graph. Reuse
     * its layout with bounded mapped reads, no native query/plugin calls.
     * Link: next+0, name+10, resource+18. Resource: definition+0,
     * clients+18, plugin+28, active_state+34, active_max+68. */
    if(!marble_adc_ram_word(base+0x129e8,&link)) {print("marble-npa-resource: refused stage=graph_root mapped=0\n");return 0;}
    print("marble-npa-resource: graph_head=%x reads_only=1\n",link);
    static const char *names[]={"/vcs/vdd_lpi_mx","/vcs/vdd_lpi_cx"};
    unsigned found=0,visited=0;
    for(;link && visited<512;visited++) {
        uint64_t next=0,name=0,resource=0;
        char text[97];
        if(!marble_npa_ram_word(link,&next) || !marble_npa_ram_word(link+0x10,&name) ||
           !marble_npa_ram_word(link+0x18,&resource)) {print("marble-npa-resource: refused stage=link index=%d link=%x\n",(uint64_t)visited,link);return 0;}
        if(!marble_proxy_ram_string(name,text)) {print("marble-npa-resource: refused stage=name index=%d link=%x name=%x\n",(uint64_t)visited,link,name);return 0;}
        for(unsigned i=0;i<2;i++)if(str_eq(text,names[i])) {
            uint64_t definition=0,clients=0,plugin=0,state=0,maximum=0,supported=0,create=0,destroy=0,definition_name=0;
            if(found&(1u<<i))return 0;
            print("marble-npa-resource: matched=%s resource=%x\n",names[i],resource);
            if(!resource || !marble_npa_ram_word(resource,&definition) ||
               !marble_npa_ram_word(definition,&definition_name) || !marble_proxy_ram_string(definition_name,text) || !str_eq(text,names[i]) ||
               !marble_npa_ram_word(resource+0x18,&clients) || !marble_npa_ram_word(resource+0x28,&plugin) ||
               !marble_npa_ram_word(resource+0x30,&state) || !marble_npa_ram_word(resource+0x68,&maximum) ||
               !marble_npa_ram_word(plugin+8,&supported) || !marble_npa_ram_word(plugin+0x20,&create) ||
               !marble_npa_ram_word(plugin+0x28,&destroy)) {print("marble-npa-resource: refused stage=resource definition=%x plugin=%x\n",definition,plugin);return 0;}
            print("marble-npa-resource: name=%s resource=%x client_head=%x state=%d max=%d supported_clients=%x create_callback=%x destroy_callback=%x reads_only=1\n",names[i],resource,clients,(uint64_t)(uint32_t)(state>>32),(uint64_t)(uint32_t)maximum,(uint64_t)(uint32_t)supported,create,destroy);
            if(!((uint32_t)supported&0x40))return 0;
            MarbleNpaLpi *lpi=&marble_npa_lpi[i];
            lpi->resource=resource;lpi->plugin=plugin;lpi->clients=clients;
            lpi->state=(uint32_t)(state>>32);lpi->maximum=(uint32_t)maximum;
            if(!marble_npa_ram_word(resource+0x40,&lpi->aggregates))return 0;
            found|=1u<<i;
        }
        link=next;
    }
    print("marble-npa-resource: complete PASS=%d found_mask=%x visited=%d terminated=%d protocol_calls=0 client_creations=0 resource_votes=0 DSP_start=0\n",(uint64_t)(found==3 && !link),(uint64_t)found,(uint64_t)visited,(uint64_t)(!link));
    marble_npa_graph_qualified=found==3 && !link;
    return marble_npa_graph_qualified;
}
static int marble_pil_proxy_config_info(void);
static int marble_npa_vcs_info(void)
{
    marble_npa_vcs_default_mask=0;
    if(!marble_npa_resources())return 0;
    static const uint64_t driver_code[]={0xa9027bfdd10203ffULL,0xf9001bfb910083fdULL,
        0xa9055ff8a90467faULL,0xa9074ff4a90657f6ULL};
    unsigned matched=0;
    for(unsigned i=0;i<2;i++) {
        MarbleNpaLpi *r=&marble_npa_lpi[i];
        uint64_t definition=0,node=0,rail=0,node_data=0,driver=0,bound=0,corner=0,config=0;
        uint64_t base=r->plugin-0x7130;
        if(!marble_npa_ram_word(r->resource,&definition) || !marble_npa_ram_word(r->resource+0x10,&node) ||
           !marble_npa_ram_word(definition+0x28,&rail) || !marble_npa_ram_word(node+0x18,&node_data) ||
           !rail || rail!=node_data || definition!=rail+0x68 || node!=rail+0x28 ||
           !marble_npa_ram_word(node+8,&driver) || driver!=base+0x2190 ||
           !marble_npa_ram_word(rail+0xa8,&bound) || bound!=r->resource ||
           !marble_npa_ram_word(rail+0x20,&corner) || !marble_npa_ram_word(rail+0xd0,&config)) {
            print("marble-npa-vcs: refused index=%d binding_unqualified=1 calls=0\n",(uint64_t)i);return 0;
        }
        for(unsigned j=0;j<4;j++) {
            uint64_t word=0;if(!marble_npa_ram_word(driver+8*j,&word) || word!=driver_code[j])return 0;
        }
        uint32_t minimum=0,maximum=480,initial=256;
        if(config) {
            uint64_t range=0,init=0;
            if(!marble_npa_ram_word(config,&range) || !marble_npa_ram_word(config+8,&init))return 0;
            minimum=(uint32_t)range;maximum=(uint32_t)(range>>32);initial=(uint32_t)init;
        }
        print("marble-npa-vcs: index=%d exact_binding=1 rail_cached_corner=%d resource_cached_state=%d corner_config=%d min=%d max=%d init=%d physical_voltage_unmeasured=1 protocol_calls=0 DSP_start=0\n",(uint64_t)i,(uint64_t)(uint32_t)corner,(uint64_t)r->state,(uint64_t)(config!=0),(uint64_t)minimum,(uint64_t)maximum,(uint64_t)initial);
        marble_npa_vcs_rails[i]=rail;
        if(!config && (uint32_t)corner==256 && r->state==256)marble_npa_vcs_default_mask|=1u<<i;
        matched++;
    }
    print("marble-npa-vcs: complete PASS=%d matched=%d read_only=1 actual_requests=0 DSP_start=0\n",(uint64_t)(matched==2),(uint64_t)matched);
    return matched==2;
}
/* An isolated PIL action can hold the already qualified native requests.
 * Negative return means stop/ownership is uncertain: never release power or
 * clear the watchdog in that case. No other command can install this action. */
static int (*marble_npa_held_action)(void);
static unsigned marble_npa_dsp_attempted;
static unsigned marble_npa_held_limit_ok;
static unsigned marble_firmware_reset_required;
static efi_status marble_firmware_watchdog_clear(void)
{
    return marble_firmware_reset_required?(EFI_ERROR_BIT|7):BS->set_watchdog_timer(0,0,0,NULL);
}
static __attribute__((noinline)) int marble_npa_client_cycle(unsigned requests)
{
    if(marble_npa_attempted || marble_npa_dirty || marble_npa_active)return 0;
    if(requests==2) {
        if(!marble_npa_vcs_info() || marble_npa_vcs_default_mask!=3)return 0;
    } else if(!marble_npa_resources())return 0;
    static const efi_guid guid={0x79d6c870,0x725e,0x489e,{0xa0,0xa1,0x27,0xe7,0xa5,0xd0,0xcb,0x35}};
    uint64_t *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table)return 0;
    /* Exact VcsDxe plugin at 7130, update 24b8; no create/destroy
     * callbacks. Native NPA DestroyClient implicitly completes its request.
     * Do not enter the proxy's fatal-on-error acquire/release routines. */
    static const uint64_t vcs_code[]={0xf9000bf9a9bb7bfdULL,0xa9025ff8910003fdULL,
        0xa9044ff4a90357f6ULL,0xaa0003f3f9401028ULL};
    for(unsigned i=0;i<2;i++) {
        MarbleNpaLpi *r=&marble_npa_lpi[i];uint64_t update=0,supported=0,create=0,destroy=0;
        if(r->clients || r->state!=256 || r->maximum!=480 || r->aggregates || r->plugin<0x80007130ULL ||
           !marble_npa_ram_word(r->plugin,&update) || !marble_npa_ram_word(r->plugin+8,&supported) ||
           !marble_npa_ram_word(r->plugin+0x20,&create) || !marble_npa_ram_word(r->plugin+0x28,&destroy) ||
           update!=r->plugin-0x7130+0x24b8 || (uint32_t)supported!=0x844 || create || destroy) {
            print("marble-npa-client: refused profile=%d initial_state=%d max=%d aggregates=%x clients=%x\n",(uint64_t)i,(uint64_t)r->state,(uint64_t)r->maximum,r->aggregates,r->clients);return 0;
        }
        for(unsigned j=0;j<4;j++) {
            uint64_t word=0;if(!marble_npa_ram_word(update+8*j,&word) || word!=vcs_code[j])return 0;
        }
    }
    efi_status armed=BS->set_watchdog_timer(30,0,0,NULL);if(EFI_ERROR(armed))return 0;
    if(requests && (!marble_pil_proxy_config_info() || marble_npa_config_qualified!=3)) {
        marble_firmware_watchdog_clear();return 0;
    }
    unsigned ready=marble_adc_channels(1) && marble_adc_last_temperature<38;
    MarbleThermalSample thermal;
    if(ready)ready=marble_thermal_snapshot(marble_thermal_read,NULL,&thermal) && thermal.maximum_mc<50000;
    if(!ready) {marble_firmware_watchdog_clear();print("marble-npa-client: temperature_guard_refusal=1 create_calls=0\n");return 0;}
    typedef efi_status (EFIAPI *Create)(const char *,const char *,uint32_t,uint32_t,void *,void **);
    typedef efi_status (EFIAPI *Destroy)(void *);
    typedef efi_status (EFIAPI *Required)(void *,uint32_t);
    static const char *names[]={"/vcs/vdd_lpi_mx","/vcs/vdd_lpi_cx"};
    static const char *clients[]={"MARBLE_TEST_MX","MARBLE_TEST_CX"};
    uint64_t began=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    unsigned ok=1,created=0,destroyed=0,restored=0,issued=0,native_released=0;
    unsigned held_action=marble_npa_held_action!=NULL;
    uint64_t limit=held_action?15000000:5000000;
    marble_npa_attempted=1;marble_npa_dirty=1;
    for(unsigned i=0;i<2;i++) {
        efi_status status=((Create)(uintptr_t)table[2])(names[i],clients[i],0x40,0,NULL,&marble_npa_clients[i]);
        if(marble_npa_clients[i]) {created++;marble_npa_active++;}
        print("marble-npa-client: create index=%d status=%x handle=%x required_type=40 explicit_requests=0\n",(uint64_t)i,status,(uint64_t)(uintptr_t)marble_npa_clients[i]);
        if(EFI_ERROR(status) || !marble_npa_clients[i]) {ok=0;break;}
        uint64_t at=(uint64_t)(uintptr_t)marble_npa_clients[i],resource=0,type=0,pending0=0,pending1=0,head=0;
        if(!marble_npa_ram_word(at+0x20,&resource) || resource!=marble_npa_lpi[i].resource ||
           !marble_npa_ram_word(at+0x30,&type) || (uint32_t)type!=0x40 ||
           !marble_npa_ram_word(at+0x38,&pending0) || (uint32_t)pending0 ||
           !marble_npa_ram_word(at+0x50,&pending1) || (uint32_t)pending1 ||
           !marble_npa_ram_word(resource+0x18,&head) || head!=at) {ok=0;break;}
    }
    if(ok && requests)for(unsigned i=0;i<2;i++) {
        /* This exact level comes from the resident PIL_ADSP DT metadata;
         * the qualified resource was already at 256 before our request. */
        efi_status status=((Required)(uintptr_t)table[5])(marble_npa_clients[i],256);
        issued++;
        uint64_t aggregate=0,state=0;
        unsigned valid=!EFI_ERROR(status) && marble_npa_ram_word(marble_npa_lpi[i].resource+0x40,&aggregate) &&
            (uint32_t)aggregate==256 && marble_npa_ram_word(marble_npa_lpi[i].resource+0x30,&state) && (uint32_t)(state>>32)==256;
        print("marble-npa-client: request index=%d configured_level=256 status=%x aggregate=%d actual_state=%d PASS=%d DSP_start=0\n",(uint64_t)i,status,(uint64_t)(uint32_t)aggregate,(uint64_t)(uint32_t)(state>>32),(uint64_t)valid);
        if(!valid) {ok=0;break;}
    }
    if(ok && requests)BS->stall(100000);
    if(ok && held_action) {
        if(requests!=2 || issued!=2 || marble_npa_active!=2)return 0;
        print("marble-npa-held: begin active=2 requests=2 watchdog_seconds=30 limit_us=15000000 DSP_start=0\n");
        int outcome=marble_npa_held_action();
        uint64_t held_us=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/freq;
        marble_npa_held_limit_ok=held_us<limit;
        print("marble-npa-held: returned outcome=%d release_allowed=%d active=%d watchdog_retained=1 elapsed_us=%d limit_us=%d limit_ok=%d\n",(uint64_t)(int64_t)outcome,(uint64_t)(outcome>=0),(uint64_t)marble_npa_active,held_us,limit,(uint64_t)marble_npa_held_limit_ok);
        if(outcome<0)return 0;
        if(!outcome)ok=0;
    }
    /* Clean up every returned handle even when another create/validation
     * fails. A failed native destroy is never retried on a possibly freed
     * handle. Retain the watchdog and dirty state on uncertain cleanup. */
    for(unsigned i=2;i>0;i--)if(marble_npa_clients[i-1]) {
        efi_status status=((Destroy)(uintptr_t)table[15])(marble_npa_clients[i-1]);
        print("marble-npa-client: destroy index=%d status=%x implicit_complete=1 explicit_requests=0\n",(uint64_t)(i-1),status);
        if(EFI_ERROR(status)) {ok=0;continue;}
        marble_npa_clients[i-1]=NULL;marble_npa_active--;destroyed++;
    }
    for(unsigned i=0;i<2;i++) {
        MarbleNpaLpi *r=&marble_npa_lpi[i];uint64_t head=~0ULL,state=0,maximum=0,aggregates=~0ULL;
        /* Read every field independently even after a mismatch. Defaults
         * for unvisited fields must never masquerade as hardware evidence. */
        unsigned read_mask=0;
        if(marble_npa_ram_word(r->resource+0x18,&head))read_mask|=1;
        if(marble_npa_ram_word(r->resource+0x30,&state))read_mask|=2;
        if(marble_npa_ram_word(r->resource+0x68,&maximum))read_mask|=4;
        if(marble_npa_ram_word(r->resource+0x40,&aggregates))read_mask|=8;
        unsigned same=read_mask==15 && head==r->clients && (uint32_t)(state>>32)==r->state &&
            (uint32_t)maximum==r->maximum && aggregates==r->aggregates;
        restored+=same;
        print("marble-npa-client: restore index=%d PASS=%d client_head=%x state=%d max=%d aggregates=%x read_mask=%x\n",(uint64_t)i,(uint64_t)same,head,(uint64_t)(uint32_t)(state>>32),(uint64_t)(uint32_t)maximum,aggregates,(uint64_t)read_mask);
        if(requests==2) {
            uint64_t corner=~0ULL;
            unsigned mapped=marble_npa_ram_word(marble_npa_vcs_rails[i]+0x20,&corner);
            unsigned released=read_mask==15 && !head && !(uint32_t)(state>>32) && (uint32_t)maximum==480 &&
                !aggregates && mapped && !(uint32_t)corner;
            native_released+=released;
            print("marble-npa-native-release: index=%d PASS=%d rail_read=%d rail_cached_corner=%d resource_cached_state=%d empty_clients=%d aggregate_zero=%d physical_voltage_unmeasured=1\n",(uint64_t)i,(uint64_t)released,(uint64_t)mapped,(uint64_t)(uint32_t)corner,(uint64_t)(uint32_t)(state>>32),(uint64_t)(!head),(uint64_t)(!aggregates));
        }
    }
    marble_npa_dirty=marble_npa_active || restored!=2;
    uint64_t elapsed=(READ_SYSREG(CNTVCT_EL0)-began)*1000000/freq;
    efi_status clear=marble_npa_dirty?(EFI_ERROR_BIT|7):marble_firmware_watchdog_clear();
    unsigned native_ok=requests==2 && ok && created==2 && destroyed==2 && issued==2 &&
        native_released==2 && !restored && !marble_npa_active && marble_npa_dirty && elapsed<limit;
    ok=ok && created==2 && destroyed==2 && restored==2 && !marble_npa_dirty && !EFI_ERROR(clear) && elapsed<limit;
    print("marble-npa-client: complete PASS=%d created=%d destroyed=%d restored=%d active=%d dirty=%d elapsed_us=%d watchdog_clear=%x explicit_requests=%d DSP_start=%d audio_acceptance=0\n",(uint64_t)ok,(uint64_t)created,(uint64_t)destroyed,(uint64_t)restored,(uint64_t)marble_npa_active,(uint64_t)marble_npa_dirty,elapsed,clear,(uint64_t)issued,(uint64_t)marble_npa_dsp_attempted);
    if(requests==2)print("marble-npa-native-release: complete PASS=%d released=%d original_restore=%d reset_required=%d watchdog_retained=%d active=%d dirty=%d DSP_start=%d audio_acceptance=0\n",(uint64_t)native_ok,(uint64_t)native_released,(uint64_t)(restored==2),(uint64_t)(marble_npa_dirty!=0),(uint64_t)(marble_npa_dirty!=0),(uint64_t)marble_npa_active,(uint64_t)marble_npa_dirty,(uint64_t)marble_npa_dsp_attempted);
    return requests==2?native_ok:ok;
}
static int marble_pil_proxy_config_info(void)
{
    marble_npa_config_qualified=0;
    static const efi_guid dt_guid={0x0389b776,0x625f,0x11eb,{0x83,0xbe,0xc7,0x41,0xa9,0x13,0xde,0x34}};
    static const efi_guid adsp={0x36fe27e1,0x33e9,0x45ad,{0x98,0xda,0xc8,0x84,0x38,0xca,0x88,0x16}};
    void *dt=NULL;uint64_t *table=NULL;
    efi_status found=BS->locate_protocol(&dt_guid,NULL,&dt);
    print("marble-proxy-config: DT_protocol_status=%x present=%d vote_calls=0 DSP_start=0\n",found,(uint64_t)(!EFI_ERROR(found) && dt));
    if(EFI_ERROR(found) || !dt)return 1; /* A diagnosed missing provider is not working DSP support. */
    if(EFI_ERROR(BS->locate_protocol(&adsp,NULL,(void **)&table)) || !table)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x8160;
    if(table[0]!=0x10000 || table[1]!=base+0x1750 || *(volatile uint32_t *)(uintptr_t)(base+0x1dcc)!=0xd10343ff)return 0;
    /* Exact native 1dcc reads /soc/pil/pil_proxy_vote/pil_proxy_vote_N through
     * the DT protocol and caches pool metadata. It does not enter 19bc/1b8c
     * (resource acquire/release), PAS or the proxy event callbacks. */
    typedef void *(EFIAPI *GetConfig)(uint32_t);
    static const unsigned ids[]={1,12};
    for(unsigned i=0;i<2;i++) {
        void *config=((GetConfig)(uintptr_t)(base+0x1dcc))(ids[i]);
        print("marble-proxy-config: PAS_id=%d config_present=%d native_DT_query=1 vote_calls=0 DSP_start=0\n",(uint64_t)ids[i],(uint64_t)(config!=NULL));
        if(!config)continue;
        uint64_t words[9];unsigned mapped=1;
        for(unsigned j=0;j<9;j++)if(!marble_adc_ram_word((uint64_t)(uintptr_t)config+8*j,&words[j])) {mapped=0;break;}
        if(!mapped || (uint32_t)words[0]!=ids[i] || (uint32_t)words[2]>16 || (uint32_t)words[6]>32)return 0;
        print("marble-proxy-config: PAS_id=%d resource_count=%d vector_count=%d resource_array=%x client_name=%x cached_metadata_only=1\n",(uint64_t)ids[i],(uint64_t)(uint32_t)words[2],(uint64_t)(uint32_t)words[6],words[3],words[1]);
        char name[97];
        if(!marble_proxy_ram_string(words[1],name))return 0;
        unsigned qualify=ids[i]==1 && (uint32_t)words[2]==2 && !(uint32_t)words[6] && str_eq(name,"PIL_ADSP"),mask=0;
        print("marble-proxy-config: PAS_id=%d client=%s DT_metadata_only=1\n",(uint64_t)ids[i],name);
        for(unsigned j=0;j<(uint32_t)words[2];j++) {
            uint64_t pointer=0,levels=0;
            if(!marble_adc_ram_word(words[3]+8*j,&pointer) || !marble_proxy_ram_string(pointer,name) ||
               !marble_adc_ram_word(words[4]+8*(j/2),&levels))return 0;
            uint32_t vote=(uint32_t)(levels>>(32*(j&1)));
            if(qualify && vote==256) {
                if(str_eq(name,"/vcs/vdd_lpi_mx"))mask|=1;
                if(str_eq(name,"/vcs/vdd_lpi_cx"))mask|=2;
            }
            print("marble-proxy-config: PAS_id=%d resource_index=%d name=%s configured_vote=%d actual_votes=0\n",(uint64_t)ids[i],(uint64_t)j,name,(uint64_t)vote);
        }
        if(qualify)marble_npa_config_qualified=mask;
    }
    return 1;
}
#endif
static void marble_daily_info(void)
{
    static const struct { const char *name;efi_guid guid; } list[]={
        {"ChargerEx",QCOM_CHARGER_EX_GUID},
        {"QcomCharger",QCOM_CHARGER_GUID},
        {"SimpleTextIn",{0x387477c1,0x69c7,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}}},
        {"SimpleTextInEx",{0xdd9e7534,0x7762,0x4698,{0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}}},
        {"KeypadDevice",{0xa412c1ac,0x0295,0x4ae3,{0xb5,0x9f,0x52,0x23,0x1f,0x26,0x55,0x4d}}},
        {"DisplayPower",{0xf352021d,0x9593,0x4432,{0xbf,4,0x67,0xb9,0xf3,0xb7,0x60,8}}},
        {"DisplayUtils",{0xc0dd69ac,0x76ba,0x11e6,{0xab,0x24,0x1f,0xc7,0xf5,0x57,0x5f,0x19}}},
        /* Locate only: presence is not ABI compatibility or a running DSP. */
        {"PIL",{0x3bec87db,0x4e76,0x491c,{0x96,0x6c,0x7c,0xa5,0x81,0x2a,0x64,0xc9}}},
        {"GLink",{0x3bfcf64d,0x00cd,0x41cb,{0xbe,0xa5,0xa7,0x6c,0x3a,0x24,0x3d,0xed}}},
        {"PIL-LA",{0x1f4f1da3,0x4d8d,0x4894,{0xba,0x2a,0x13,0xd3,0x20,0x7f,0x97,0x39}}},
        {"FuelGauge",{0x9aed0312,0x724a,0x4209,{0x86,0x20,0xf9,0x3d,0xe0,0xbb,0xd8,0xfb}}},
        {"ADC",{0x61740cc4,0x916f,0x4471,{0xb0,0xdc,0x00,0xc6,0xf4,0x3a,0x7f,0xc6}}},
        {"ADSPProxy",{0x36fe27e1,0x33e9,0x45ad,{0x98,0xda,0xc8,0x84,0x38,0xca,0x88,0x16}}},
        {"SLPIProxy",{0xb2dcfc34,0xb2a0,0x4bb7,{0xbd,0xb0,0x31,0xdb,0x01,0xe3,0xcc,0x2a}}}
    };
    print("marble-daily: discovery-v1 charger_init=0 charger_actions=0 pmic_writes=0 block_io=0\n");
    for(unsigned i=0;i<sizeof(list)/sizeof(list[0]);i++) {
        void *p=NULL;efi_status s=BS->locate_protocol(&list[i].guid,NULL,&p);
        print("marble-daily: protocol=%s status=%x present=%d\n",list[i].name,s,(uint64_t)(!EFI_ERROR(s) && p));
#ifdef XNU_MARBLE
        if((i==12 || i==13) && !EFI_ERROR(s) && p) {
            /* Resident PILProxyDxe matches the pinned supplied Mu image.
             * InstallMultipleProtocolInterfaces at 1934 binds the ADSP/SLPI
             * GUIDs at 8078/8088 to tables 8160/8170 (not 8130/8140).
             * Each table is revision + event callback,
             * without a This argument. Inventory only; no NPA/clock vote. */
            uint64_t address=(uint64_t)(uintptr_t)p,revision=0,callback=0;
            uint64_t offset=i==12?0x8160:0x8170,expected=i==12?0x1750:0x1874;
            unsigned match=marble_adc_ram_word(address,&revision) && marble_adc_ram_word(address+8,&callback) &&
                revision==0x10000 && callback==address-offset+expected;
            print("marble-daily: proxy=%s exact_table=%d revision=%x table=%x callback=%x event_calls=0 clock_votes=0 dsp_start=0\n",list[i].name,(uint64_t)match,revision,address,callback);
        }
#endif
        if(i>=9 && i<=11 && !EFI_ERROR(s) && p) {
            const uint64_t *words=p;uint64_t revision=words[0];
            print("marble-daily: protocol=%s revision=%x getter_calls=0 init_calls=0 dsp_start=0\n",list[i].name,revision);
            if(i==9 && revision==0x10003) {
                static const uint32_t rvas[]={0x16f8,0x171c,0x173c,0x175c,0x177c,0x17d8};
                uint64_t base=(uint64_t)(uintptr_t)p-0x18198;unsigned match=1;
                for(unsigned j=0;j<6;j++)if(words[j+1]!=base+rvas[j])match=0;
                print("marble-daily: modern_pil exact_table=%d calls=0 auto_override=0\n",(uint64_t)match);
            }
            if(i==10 && revision==0x10018) {
                uint64_t base=(uint64_t)(uintptr_t)p-0x2c3d8;
                unsigned match=words[3]==base+0x2016c && words[1]==base+0x2015c && words[2]==base+0x20164;
                print("marble-daily: fuel_gauge exact_stub_table=%d getter_calls=0 soc_telemetry=0 temperature_telemetry=0\n",(uint64_t)match);
            }
        }
        if(i<2 && !EFI_ERROR(s) && p) {
            uint64_t revision=*(uint64_t *)p;
            print("marble-daily: protocol=%s revision=%x\n",list[i].name,revision);
            /* Source firmware's ChargerExDxe table at RVA 0x70e8 has the
             * same five entries for 0x10004. Disassembly is retained. */
            if(i==0 && (revision==0x10003 || revision==0x10004)) {
                efi_charger_ex *c=p;
                uint8_t present=255,plugged=255;uint32_t mv=0;
                efi_status a=c->get_battery_presence(&present);
                efi_status b=c->get_charger_presence(&plugged);
                efi_status v=c->get_battery_voltage(&mv);
                print("marble-daily: battery presence=%d plugged=%d mv=%d status=%x,%x,%x voltage_valid=%d temperature_known=0 soc_known=0\n",
                    (uint64_t)present,(uint64_t)plugged,(uint64_t)mv,a,b,v,
                    (uint64_t)(!EFI_ERROR(v) && mv>=2500 && mv<=5000));
            }
        }
    }
    print("marble-daily: complete; capability discovery is not functional acceptance\n");
}
#endif
