/* Read-only GIC diagnostics from the pinned marble DT. ISPENDR/ISACTIVER
 * reads have no acknowledge side effects. Never read IAR or write any GIC. */
#ifndef MARBLE_IRQ_OBSERVER_FW_H
#define MARBLE_IRQ_OBSERVER_FW_H
#ifdef XNU_MARBLE
#ifdef XNU_MARBLE_GPR_ONLY
/* Dedicated GPR image omits the completed read-only GIC diagnostic for FAT space. */
static int marble_irq_observer_qualify(void){return 1;}
static int marble_irq_observer_snapshot(unsigned phase){return phase<4;}
static void marble_irq_observer_log(void){print("marble-irq-observer: omitted profile=GPR-only IRQ_writes=0 IAR_reads=0\n");}
#else
static struct {
    uint64_t redistributor;unsigned qualified,banks;
    struct {unsigned seen;uint32_t ppi_enable,ppi_pending,ppi_active,pending[32],active[32],enabled[32];} sample[4];
} marble_irq_observer;
static int marble_irq_observer_page(uint64_t address)
{
    uint64_t par=marble_fw_translation(address);
    return !(par&1) && (par&0x0000fffffffff000ULL)==(address&~4095ULL);
}
static int marble_irq_observer_snapshot(unsigned phase)
{
    if(!marble_irq_observer.qualified || phase>=4 || marble_irq_observer.sample[phase].seen)return 0;
    uint64_t sgi=marble_irq_observer.redistributor+0x10000;
    marble_irq_observer.sample[phase].ppi_enable=*(volatile uint32_t *)(uintptr_t)(sgi+0x100);
    marble_irq_observer.sample[phase].ppi_pending=*(volatile uint32_t *)(uintptr_t)(sgi+0x200);
    marble_irq_observer.sample[phase].ppi_active=*(volatile uint32_t *)(uintptr_t)(sgi+0x300);
    for(unsigned i=1;i<marble_irq_observer.banks;i++) {
        marble_irq_observer.sample[phase].enabled[i]=*(volatile uint32_t *)(uintptr_t)(0x17100100+4*i);
        marble_irq_observer.sample[phase].pending[i]=*(volatile uint32_t *)(uintptr_t)(0x17100200+4*i);
        marble_irq_observer.sample[phase].active[i]=*(volatile uint32_t *)(uintptr_t)(0x17100300+4*i);
    }
    marble_irq_observer.sample[phase].seen=1;return 1;
}
static int marble_irq_observer_qualify(void)
{
    if(marble_irq_observer.qualified || marble_pil_auth_attempted ||
       !marble_irq_observer_page(0x17100000) || !marble_irq_observer_page(0x1710f000))return 0;
    uint32_t typer=*(volatile uint32_t *)(uintptr_t)0x17100004;
    uint32_t architecture=(*(volatile uint32_t *)(uintptr_t)0x1710ffe8>>4)&15;
    unsigned banks=(typer&31)+1;
    if((architecture!=3 && architecture!=4) || banks<9 || banks>32)return 0;
    uint64_t mpidr=READ_SYSREG(MPIDR_EL1);
    uint32_t affinity=(uint32_t)(((mpidr>>32)&255)<<24|(mpidr&0xffffff));
    for(unsigned i=0;i<8;i++) {
        uint64_t frame=0x17180000ULL+0x40000ULL*i;
        if(!marble_irq_observer_page(frame))return 0;
        uint64_t rtyper=*(volatile uint64_t *)(uintptr_t)(frame+8);
        if((uint32_t)(rtyper>>32)==affinity) {
            if(!marble_irq_observer_page(frame+0x10000))return 0;
            marble_irq_observer.redistributor=frame;break;
        }
        if(rtyper&16)break;
    }
    if(!marble_irq_observer.redistributor)return 0;
    marble_irq_observer.banks=banks;marble_irq_observer.qualified=1;
    print("marble-irq-observer: qualified distributor=17100000 redistributor=%x stride=40000 banks=%d architecture=%d MPIDR=%x IPCC_SPI=261 IRQ_writes=0 IAR_reads=0\n",
        marble_irq_observer.redistributor,(uint64_t)banks,(uint64_t)architecture,mpidr);
    return marble_irq_observer_snapshot(0);
}
static void marble_irq_observer_log(void)
{
    for(unsigned phase=0;phase<4;phase++)if(marble_irq_observer.sample[phase].seen) {
        print("marble-irq-observer: snapshot phase=%d PPI_enabled=%x PPI_pending=%x PPI_active=%x IRQ_writes=0 IAR_reads=0\n",
            (uint64_t)phase,(uint64_t)marble_irq_observer.sample[phase].ppi_enable,
            (uint64_t)marble_irq_observer.sample[phase].ppi_pending,(uint64_t)marble_irq_observer.sample[phase].ppi_active);
        for(unsigned i=1;i<marble_irq_observer.banks;i++)if(i==8 || marble_irq_observer.sample[phase].pending[i] || marble_irq_observer.sample[phase].active[i])
            print("marble-irq-observer: SPI phase=%d bank=%d enabled=%x pending=%x active=%x\n",(uint64_t)phase,(uint64_t)i,
                (uint64_t)marble_irq_observer.sample[phase].enabled[i],(uint64_t)marble_irq_observer.sample[phase].pending[i],
                (uint64_t)marble_irq_observer.sample[phase].active[i]);
    }
}
#endif
#endif
#endif
