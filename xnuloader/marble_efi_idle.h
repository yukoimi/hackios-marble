/* Local-PE CNTVCT event stream. No CNTP/CNTV timer, GIC, EPSS or PMIC writes.
 * ARM CNTKCTL_EL1 EVNTEN/EVNTDIR/EVNTI/EVNTIS, reference archived in out.
 * The event period bounds an architectural WFE sleep even with DAIF masked.
 * A missing/misimplemented hardware event cannot be timed out by this PE;
 * a short loader-only hardware probe is mandatory before any XNU run.
 */
#ifndef XNULOADER_MARBLE_EFI_IDLE_H
#define XNULOADER_MARBLE_EFI_IDLE_H
#include <stdint.h>

#define MARBLE_EFI_IDLE_MASK ((UINT64_C(1)<<17) | UINT64_C(0xfc))
typedef struct {
    uint64_t saved, control, frequency, period;
    uint64_t waits, ticks, maximum, immediate, late;
    uint64_t last_poll, max_poll_gap;
    unsigned active, dirty, failed, have_poll;
} MarbleEFIIdle;

typedef struct {
    uint64_t (*control)(void *);
    int (*write)(void *, uint64_t);
    uint64_t (*counter)(void *);
    void (*wait)(void *);
    void *context;
} MarbleEFIIdleOps;

static int marble_efi_idle_plan(uint64_t saved, uint64_t frequency,
    uint64_t *control, uint64_t *period)
{
    /* Bound arithmetic and refuse implausible/unknown counter frequencies. */
    if (frequency < 1000000 || frequency > 1000000000) return 0;
    unsigned bit = 0;
    while (bit < 15 && (UINT64_C(1) << (bit+2))*1000000 <= frequency*500) bit++;
    *period = UINT64_C(1) << (bit+1);
    *control = (saved & ~MARBLE_EFI_IDLE_MASK) | UINT64_C(4) | ((uint64_t)bit << 4);
    return *period*1000000 <= frequency*500;
}

static int marble_efi_idle_restore(MarbleEFIIdle *p, const MarbleEFIIdleOps *ops)
{
    if (!p->active && !p->dirty) return !p->failed;
    int wrote = ops->write(ops->context, p->saved);
    p->dirty = !wrote || ops->control(ops->context) != p->saved;
    p->active = 0;
    if (p->dirty) p->failed = 1;
    return !p->dirty;
}

static int marble_efi_idle_begin(MarbleEFIIdle *p, const MarbleEFIIdleOps *ops, uint64_t frequency)
{
    if (p->active || p->dirty || p->failed) return 0;
    p->saved = ops->control(ops->context);
    if (!marble_efi_idle_plan(p->saved, frequency, &p->control, &p->period)) return 0;
    p->frequency = frequency;
    p->waits = p->ticks = p->maximum = p->immediate = p->late = 0;
    p->last_poll = p->max_poll_gap = p->have_poll = 0;
    /* A failed write can still have changed hardware: always attempt rollback. */
    p->dirty = 1;
    if (!ops->write(ops->context, p->control) || ops->control(ops->context) != p->control) {
        p->failed = 1;
        marble_efi_idle_restore(p, ops);
        return 0;
    }
    p->active = 1;
    p->dirty = 0;
    return 1;
}

/* Caller masks local interrupts around control readback + WFE, preventing an
 * EFI interrupt handler from changing the event source between those steps.
 * Do not SEVL before each wait: that would make every WFE immediately return.
 */
static int marble_efi_idle_wait(MarbleEFIIdle *p, const MarbleEFIIdleOps *ops)
{
    if (!p->active || p->dirty || p->failed) return 0;
    if (ops->control(ops->context) != p->control) { p->failed = 1; return 0; }
    uint64_t began = ops->counter(ops->context);
    ops->wait(ops->context);
    uint64_t ticks = ops->counter(ops->context) - began;
    p->waits++;
    p->ticks += ticks;
    if (ticks > p->maximum) p->maximum = ticks;
    if (ticks <= p->frequency/1000000) p->immediate++;
    if (ticks > p->period*4) { p->late++; p->failed = 1; }
    return !p->failed;
}

static void marble_efi_idle_poll(MarbleEFIIdle *p, uint64_t now)
{
    if (p->have_poll && now-p->last_poll > p->max_poll_gap) p->max_poll_gap = now-p->last_poll;
    p->last_poll = now;
    p->have_poll = 1;
}

static int marble_efi_idle_probe_ok(const MarbleEFIIdle *p)
{
    /* Reject a trapped/no-op WFE or an event storm masquerading as residency.
     * Elapsed WFE time is not a hardware power-state residency measurement. */
    return !p->active && !p->dirty && !p->failed && p->waits == 64 &&
        p->ticks >= p->period*8 && p->late == 0;
}
#endif
