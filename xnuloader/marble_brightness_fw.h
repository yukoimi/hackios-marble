/* Opt-in 4Hz bounded brightness bridge. Identity/readback and exact restoration
 * use the same DSI lease as the independently tested runtime dim command. */
#ifndef MARBLE_BRIGHTNESS_FW_H
#define MARBLE_BRIGHTNESS_FW_H
#include "marble_brightness_mailbox.h"
static volatile MarbleBrightnessMailbox *marble_bl_box;
static uint64_t marble_bl_poll_at;
static uint64_t marble_bl_ack;
static unsigned marble_bl_failed;
static int marble_bl_attach(void)
{
    marble_bl_box=NULL;marble_bl_poll_at=0;marble_bl_ack=0;marble_bl_failed=0;
    if(!mt_box || !marble_display_bus.active || marble_display_bus.dirty || !marble_display_qualified)return 0;
    volatile MarbleBrightnessMailbox *b=(void *)((uint8_t *)mt_box+MB_OFFSET-MT_MAILBOX_OFFSET);
    for(unsigned i=0;i<sizeof(*b)/8;i++)if(((volatile uint64_t *)b)[i])return 0;
    MarbleDsiOps o={NULL,marble_dsi_reg_read,marble_dsi_reg_write,marble_dsi_delay,marble_dsi_wait_blank};
    if(!marble_brightness_prepare(&o,&marble_brightness)) {
        if(marble_brightness.dirty)marble_display_bus.dirty=1;
        return 0;
    }
    unsigned original=marble_brightness.original;
    b->magic=MB_MAGIC;b->version=1;b->original=original;
    b->minimum=original>327?328:8;b->maximum=original;
    b->actual=original;b->accepted_milli=1000;
    __asm__ volatile("dsb sy" ::: "memory");b->ready=1;
    clean_dcache((uint64_t)(uintptr_t)b,(uint64_t)(uintptr_t)b+sizeof(*b));
    marble_bl_box=b;
    print("marble-brightness-mailbox: ready=1 original=%d minimum=%d maximum=%d panel_writes=0 rate_hz=4\n",
        (uint64_t)original,(uint64_t)b->minimum,(uint64_t)b->maximum);
    return 1;
}
static void marble_bl_poll(void)
{
    volatile MarbleBrightnessMailbox *b=marble_bl_box;
    if(!b || marble_bl_failed)return;
    uint64_t now=READ_SYSREG(CNTVCT_EL0),frequency=READ_SYSREG(CNTFRQ_EL0);
    if(now-marble_bl_poll_at<frequency/4)return;
    marble_bl_poll_at=now;
    flush_dcache((uint64_t)(uintptr_t)b+64,(uint64_t)(uintptr_t)b+128);
    uint64_t seq=b->sequence;unsigned milli=b->milli,reserved=b->request_reserved;
    __asm__ volatile("dsb sy" ::: "memory");
    flush_dcache((uint64_t)(uintptr_t)b+64,(uint64_t)(uintptr_t)b+128);
    if(!marble_brightness_request_valid(seq,b->sequence,marble_bl_ack,milli,reserved))return;
    unsigned original=marble_brightness.original,minimum=original>327?328:8;
    unsigned target=marble_brightness_target(original,minimum,milli);
    MarbleDsiOps o={NULL,marble_dsi_reg_read,marble_dsi_reg_write,marble_dsi_delay,marble_dsi_wait_blank};
    int ok=target && marble_display_bus.active && !marble_display_bus.dirty &&
        marble_brightness_set(&o,&marble_brightness,target);
    if(marble_brightness.dirty)marble_display_bus.dirty=1;
    b->result=ok?1:2;b->actual=marble_brightness.level;
    if(ok)b->accepted_milli=milli;else {b->failed=1;marble_bl_failed=1;}
    marble_bl_ack=seq;
    __asm__ volatile("dsb sy" ::: "memory");b->acknowledged=seq;
    clean_dcache((uint64_t)(uintptr_t)b+128,(uint64_t)(uintptr_t)b+sizeof(*b));
    print("marble-brightness-mailbox: request=%d milli=%d target=%d actual=%d PASS=%d failed=%d dirty=%d\n",
        seq,(uint64_t)milli,(uint64_t)target,(uint64_t)b->actual,(uint64_t)ok,(uint64_t)b->failed,(uint64_t)marble_brightness.dirty);
}
#endif
