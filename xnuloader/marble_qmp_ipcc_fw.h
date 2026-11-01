/* Receive acknowledgement only for the QMP client0/signal0 from marble DT.
 * No IRQ enable/disable, GIC acknowledgement, native callback or foreign clear. */
#ifndef MARBLE_QMP_IPCC_FW_H
#define MARBLE_QMP_IPCC_FW_H
#ifdef XNU_MARBLE
#include "marble_ipcc_code.h"
static struct {unsigned prepared,attempted,writes,passed;uint32_t before,observed,after;uint64_t base;} marble_qmp_ipcc;
static unsigned marble_qmp_ipcc_owned_authorized;
static int marble_qmp_ipcc_prepare(unsigned owned)
{
    if(owned>1 || owned!=marble_qmp_ipcc_owned_authorized)return 0;
    if(owned && (!marble_pil_header_only || marble_pil_auth_attempted || !marble_pil_dsp_dirty ||
       !marble_pil_deferred.saved || !marble_pil_deferred.verified || marble_npa_active!=2 || !marble_icb_scm_owned_authorized))return 0;
    if(marble_qmp_ipcc.prepared || marble_qmp_native.attempted ||
       (!owned && (marble_pil_attempted || marble_smp2p_attempted || marble_npa_attempted || marble_icb_attempted || marble_rpmh_attempted)) || marble_glink_observer.attempted ||
       !marble_smp2p_ipcc_qualified_mode(0))return 0;
    static const efi_guid guid={0x52858ad7,0x8b16,0x4137,{0x9c,0x64,0xdf,0xe0,0x4d,0x94,0x2e,0x80}};
    uint64_t *table=NULL;
    if(EFI_ERROR(BS->locate_protocol(&guid,NULL,(void **)&table)) || !table)return 0;
    uint64_t base=(uint64_t)(uintptr_t)table-0x90a8;
    for(unsigned i=0;i<sizeof(marble_ipcc_code)/sizeof(*marble_ipcc_code);i++)
        if(!marble_code_digest(marble_npa_ram_word,base+marble_ipcc_code[i].rva,marble_ipcc_code[i].bytes,marble_ipcc_code[i].digest))return 0;
    marble_qmp_ipcc.base=0xed18000;
    marble_qmp_ipcc.before=*(volatile uint32_t *)(uintptr_t)(marble_qmp_ipcc.base+0x10);
    marble_qmp_ipcc.prepared=1;
    print("marble-qmp-ipcc: qualified APPS_client=8 source_client=0 signal=0 base=ed18000 receive_offset=10 clear_offset=1c code_ranges=2 before=%x IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0\n",(uint64_t)marble_qmp_ipcc.before);
    return 1;
}
static int marble_qmp_ipcc_drain(void)
{
    if(!marble_qmp_ipcc.prepared || marble_qmp_ipcc.attempted || !marble_qmp_wire_preserved())return 0;
    marble_qmp_ipcc.attempted=1;
    marble_qmp_ipcc.observed=*(volatile uint32_t *)(uintptr_t)(marble_qmp_ipcc.base+0x10);
    unsigned owned=marble_qmp_ipcc.before==0xffffffffu && marble_qmp_ipcc.observed==0;
    /* A pre-existing or foreign notification is never silently consumed.
     * The entire QMP descriptor/mailbox handshake has already been handled. */
    if(owned) {
        __asm__ volatile("dsb sy" ::: "memory");
        *(volatile uint32_t *)(uintptr_t)(marble_qmp_ipcc.base+0x1c)=marble_qmp_ipcc.observed;
        __asm__ volatile("dsb sy" ::: "memory");marble_qmp_ipcc.writes=1;
    }
    marble_qmp_ipcc.after=*(volatile uint32_t *)(uintptr_t)(marble_qmp_ipcc.base+0x10);
    unsigned preserved=marble_qmp_wire_preserved();
    unsigned ok=owned && marble_qmp_ipcc.after==0xffffffffu && preserved;
    marble_qmp_ipcc.passed=ok;
    print("marble-qmp-ipcc: complete PASS=%d before=%x observed=%x after=%x owned=%d clear_writes=%d firmware_preserved=%d IRQ_enable_writes=0 GIC_writes=0 native_callbacks=0 foreign_clears=0 PAS_calls=0 DSP_start=0 watchdog_retained=1 cold_reset_required=1 original_irq_state_restored=%d\n",
        (uint64_t)ok,(uint64_t)marble_qmp_ipcc.before,(uint64_t)marble_qmp_ipcc.observed,(uint64_t)marble_qmp_ipcc.after,
        (uint64_t)owned,(uint64_t)marble_qmp_ipcc.writes,(uint64_t)preserved,(uint64_t)ok);
    return ok;
}
static int marble_qmp_ipcc_cycle(void)
{return marble_qmp_ipcc_prepare(0) && marble_qmp_wire_cycle() && marble_qmp_ipcc_drain();}
#endif
#endif
