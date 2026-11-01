/* Opt-in observation of generic ARM idle. No timer/GIC/clock writes.
 * Included by cpu.c after cpu_data and machine routines are declared. */
#if defined(GENERIC_PSCI_SMP)
struct marble_idle_sample {
    uint64_t entries, disallowed, proximate, waits, ticks, longest, pending;
};
static struct marble_idle_sample marble_idle_samples[MAX_CPUS];
static uint32_t marble_idle_enabled;

void marble_idle_probe_init(void);
void marble_idle_probe_report(void);
extern void redwood_tl(const char *format, ...) __printflike(1, 2);

void marble_idle_probe_init(void)
{
    PE_parse_boot_argn("marble_idle_probe", &marble_idle_enabled, sizeof(marble_idle_enabled));
}

/* Called in the existing one-second sleeping load thread. Single-core tests
 * read their own counters; multi-core snapshots are approximate, not atomic. */
void marble_idle_probe_report(void)
{
    if (!marble_idle_enabled) return;
    const ml_topology_info_t *t = ml_get_topology_info();
    for (unsigned i = 0; i < t->num_cpus && i < MAX_CPUS; i++) {
        struct marble_idle_sample s = marble_idle_samples[i];
        redwood_tl("marble-idle: cpu=%u entries=%llu disallowed=%llu proximate=%llu wfi=%llu ticks=%llu longest=%llu pending=%llu\n",
            i, s.entries, s.disallowed, s.proximate, s.waits, s.ticks, s.longest, s.pending);
    }
    /* Physical timer registers trap under this phone's hypervisor. Read only
     * the virtual timer and registers already used by the working kernel. */
    redwood_tl("marble-idle-regs: mpidr=%llx isr=%llx vctl=%llx cntfrq=%llu enabled=%u signal=%x\n",
        __builtin_arm_rsr64("MPIDR_EL1"), __builtin_arm_rsr64("ISR_EL1"),
        __builtin_arm_rsr64("CNTV_CTL_EL0"), __builtin_arm_rsr64("CNTFRQ_EL0"),
        (unsigned)idle_enable, getCpuDatap()->cpu_signal);
}
#endif
