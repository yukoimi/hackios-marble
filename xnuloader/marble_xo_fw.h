/* XO command database qualification only. Reuse the GPU's already tested
 * bounds parser on two RAM snapshots; never issue a command/vote or MMIO. */
#ifndef MARBLE_XO_FW_H
#define MARBLE_XO_FW_H
#ifdef XNU_MARBLE
#define MARBLE_CMD_DB_FREESTANDING
#define MARBLE_CMD_DB_ONLY
#include "../xnu/files/bsd/dev/marble_gpu/marble_gmu_preflight.h"
#undef MARBLE_CMD_DB_ONLY
#undef MARBLE_CMD_DB_FREESTANDING
static int marble_adsp_xo_inventory(void)
{
    if(marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted ||
       marble_qmp_native.attempted || marble_glink_observer.attempted || marble_cap.active || marble_cap.dirty)return 0;
    unsigned mapped=0;
    for(unsigned off=0;off<MARBLE_CMD_DB_BYTES;off+=4096) {
        uint64_t address=MARBLE_CMD_DB_PHYS+off,par=marble_fw_translation(address);
        if((par&1) || (par&0x0000fffffffff000ULL)!=address)return 0;
        mapped++;
    }
    uint8_t *data=NULL;
    efi_status allocated=BS->allocate_pool(EfiLoaderData,2*MARBLE_CMD_DB_BYTES,(void **)&data);
    print("marble-adsp-xo: snapshot_begin address=%x bytes=%d pages=%d allocated=%x snapshots=2 votes=0 MMIO_writes=0 DSP_start=0\n",MARBLE_CMD_DB_PHYS,(uint64_t)MARBLE_CMD_DB_BYTES,(uint64_t)mapped,allocated);
    if(EFI_ERROR(allocated) || !data)return 0;
    uint64_t start=READ_SYSREG(CNTVCT_EL0),freq=READ_SYSREG(CNTFRQ_EL0);
    for(unsigned pass=0;pass<2;pass++) {
        const volatile uint64_t *src=(const volatile uint64_t *)(uintptr_t)MARBLE_CMD_DB_PHYS;
        uint64_t *dst=(uint64_t *)(void *)(data+pass*MARBLE_CMD_DB_BYTES);
        for(unsigned i=0;i<MARBLE_CMD_DB_BYTES/8;i++)dst[i]=src[i];
        __asm__ volatile("dmb sy" ::: "memory");
        if(!pass)BS->stall(100);
    }
    unsigned stable=!memcmp(data,data+MARBLE_CMD_DB_BYTES,MARBLE_CMD_DB_BYTES);
    MarbleCmdDbEntry entry={0};MarbleCmdDbIssue issue={0};
    unsigned parsed=stable && marble_cmd_db_find_diagnostic(data,MARBLE_CMD_DB_BYTES,"xo.lvl",&entry,&issue);
    unsigned qualified=parsed && entry.slave==3 && !(entry.address&3);
    print("marble-adsp-xo: resource name=xo.lvl stable=%d parsed=%d qualified=%d address=%x slave=%d version=%d aux_bytes=%d issue=%d,%d,%d checksum_authenticated=0\n",
        (uint64_t)stable,(uint64_t)parsed,(uint64_t)qualified,(uint64_t)entry.address,(uint64_t)entry.slave,(uint64_t)entry.version,(uint64_t)entry.bytes,(uint64_t)issue.code,(uint64_t)issue.slot,(uint64_t)issue.entry);
    /* Print only bounded metadata bytes, never a firmware pointer or load. */
    if(qualified && entry.bytes<=64) {
        char hex[129];
        for(unsigned i=0;i<entry.bytes;i++) {hex[2*i]="0123456789abcdef"[entry.aux[i]>>4];hex[2*i+1]="0123456789abcdef"[entry.aux[i]&15];}
        hex[2*entry.bytes]=0;
        print("marble-adsp-xo: aux bytes=%d hex=%s\n",(uint64_t)entry.bytes,hex);
    }
    efi_status freed=BS->free_pool(data);
    uint64_t us=(READ_SYSREG(CNTVCT_EL0)-start)*1000000/freq;
    unsigned ok=qualified && !EFI_ERROR(freed) && us<1000000;
    print("marble-adsp-xo: complete PASS=%d read_bytes=%d free_status=%x elapsed_us=%d resource_votes=0 native_clock_calls=0 RPMh_calls=0 NPA_calls=0 QMP_calls=0 PAS_calls=0 DSP_start=0 storage_access=0 lease_accepted=0\n",
        (uint64_t)ok,(uint64_t)(2*MARBLE_CMD_DB_BYTES),freed,us);
    return ok;
}
#endif
#endif
