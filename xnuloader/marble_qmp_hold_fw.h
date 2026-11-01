/* Keep load_state ON across PAS shutdown. No DSP execution in this profile.
 * Its two receive acknowledgements belong to two handled mailbox messages. */
#ifndef MARBLE_QMP_HOLD_FW_H
#define MARBLE_QMP_HOLD_FW_H
#ifdef XNU_MARBLE
static struct {unsigned begun,on_acked,stop_verified,off_acked,passed,clears;} marble_qmp_hold;
static int marble_qmp_hold_open(void)
{
    MarbleQmpDescriptor d;
    return marble_qmp_wire.attempted && !marble_qmp_wire.passed && marble_qmp_wire_same() &&
        marble_qmp_native_descriptor(&d) && marble_qmp_open(&d);
}
static int marble_qmp_hold_ack(unsigned stage)
{
    if(stage<1 || stage>2 || !marble_qmp_hold.begun || !marble_qmp_ipcc.prepared ||
       marble_qmp_ipcc.before!=0xffffffffu || marble_qmp_wire.acks!=stage || marble_qmp_hold.clears!=stage-1 ||
       (stage==1?(!marble_qmp_hold_open() || marble_qmp_hold.stop_verified):
        (!marble_qmp_hold.stop_verified || !marble_qmp_wire_preserved())))return 0;
    volatile uint32_t *receive=(volatile uint32_t *)(uintptr_t)(0xed18000+0x10);
    uint32_t before=*receive,after=before;
    if(before==0) {
        __asm__ volatile("dsb sy" ::: "memory");
        *(volatile uint32_t *)(uintptr_t)(0xed18000+0x1c)=before;
        __asm__ volatile("dsb sy" ::: "memory");marble_qmp_hold.clears++;
        after=*receive;
    }
    unsigned ok=before==0 && after==0xffffffffu && marble_qmp_hold.clears==stage;
    print("marble-qmp-hold: notify stage=%d PASS=%d observed=%x after=%x clear_writes=%d foreign_clears=0 IRQ_enable_writes=0 GIC_writes=0\n",
        (uint64_t)stage,(uint64_t)ok,(uint64_t)before,(uint64_t)after,(uint64_t)marble_qmp_hold.clears);
    return ok;
}
static int marble_qmp_hold_begin(void)
{
    if(marble_qmp_hold.begun || marble_pil_auth_attempted)return 0;
    marble_qmp_ipcc_owned_authorized=1;
    unsigned prepared=marble_qmp_ipcc_prepare(1);
    marble_qmp_ipcc_owned_authorized=0;
    if(!prepared || marble_qmp_ipcc.before!=0xffffffffu)return 0;
    marble_qmp_hold.begun=1;
    marble_qmp_wire_owned_authorized=1;
    unsigned ok=marble_qmp_wire_open_mode(1,1);
    marble_qmp_wire_owned_authorized=0;
    ok=ok && marble_qmp_wire_send(0) && marble_qmp_hold_ack(1);
    marble_qmp_hold.on_acked=ok;
    print("marble-qmp-hold: on PASS=%d mailbox_ACKs=%d IPCC_clears=%d channel_open=%d PAS_stop_calls=0 DSP_start=0\n",
        (uint64_t)ok,(uint64_t)marble_qmp_wire.acks,(uint64_t)marble_qmp_hold.clears,(uint64_t)marble_qmp_hold_open());
    return ok;
}
static int marble_qmp_hold_finish(void)
{
    if(!marble_qmp_hold.on_acked || !marble_qmp_hold.stop_verified || marble_qmp_hold.passed ||
       !marble_qmp_hold_open() || marble_qmp_wire.acks!=1 || marble_qmp_hold.clears!=1 ||
       *(volatile uint32_t *)(uintptr_t)(0xed18000+0x10)!=0xffffffffu)return 0;
    unsigned sent=marble_qmp_wire_send(1);
    unsigned closed=sent && marble_qmp_wire_finish_mode(1,1);
    unsigned acked=closed && marble_qmp_hold_ack(2);
    marble_qmp_hold.off_acked=acked;
    marble_qmp_hold.passed=sent && closed && acked && marble_qmp_wire_preserved();
    print("marble-qmp-hold: complete PASS=%d ON_before_PAS=1 PAS_before_OFF=%d mailbox_ACKs=%d IPCC_clears=%d wire_preserved=%d DSP_start=%d audio_acceptance=0\n",
        (uint64_t)marble_qmp_hold.passed,(uint64_t)marble_qmp_hold.stop_verified,(uint64_t)marble_qmp_wire.acks,
        (uint64_t)marble_qmp_hold.clears,(uint64_t)marble_qmp_wire_preserved(),(uint64_t)marble_pil_auth_attempted);
    return marble_qmp_hold.passed;
}
#endif
#endif
