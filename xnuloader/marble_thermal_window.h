/* Explicit short sleeping-init experiment: die readings are not battery
 * readings. Keep old profiles intact. Observe the span of consecutive hot
 * samples; this is not proof of temperature between samples. */
#ifndef XNULOADER_MARBLE_THERMAL_WINDOW_H
#define XNULOADER_MARBLE_THERMAL_WINDOW_H
#include <stdint.h>
typedef struct {
    uint64_t frequency, last, hot_since, maximum_hot_ticks;
    unsigned initialized, hot, reason;
} MarbleThermalWindow;

static int marble_thermal_window_duration(MarbleThermalWindow *p, uint64_t now, uint64_t frequency,
    int valid, int maximum_mc, unsigned starting, unsigned hot_seconds)
{
    p->reason=0;
    if(!valid || !frequency || (hot_seconds!=10 && hot_seconds!=60)) { p->reason=1;return 0; }
    if(starting) {
        p->frequency=frequency;p->last=now;p->initialized=1;
        p->hot=0;p->hot_since=0;p->maximum_hot_ticks=0;
        if(maximum_mc>=50000) { p->reason=2;return 0; }
        return 1;
    }
    if(!p->initialized || p->frequency!=frequency || now-p->last>frequency*7/2) {
        p->reason=3;return 0;
    }
    p->last=now;
    if(maximum_mc>=55000) { p->reason=4;return 0; }
    if(maximum_mc>=50000) {
        if(!p->hot) { p->hot=1;p->hot_since=now; }
        uint64_t elapsed=now-p->hot_since;
        if(elapsed>p->maximum_hot_ticks)p->maximum_hot_ticks=elapsed;
        if(elapsed>=frequency*hot_seconds) { p->reason=5;return 0; }
    } else {
        p->hot=0;
    }
    return 1;
}
static int marble_thermal_window(MarbleThermalWindow *p, uint64_t now, uint64_t frequency,
    int valid, int maximum_mc, unsigned starting)
{
    return marble_thermal_window_duration(p,now,frequency,valid,maximum_mc,starting,10);
}
#endif
