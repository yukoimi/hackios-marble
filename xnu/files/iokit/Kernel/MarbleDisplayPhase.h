/* Admission into the measured DSI video blank; no I/O or invented refresh. */
#ifndef MARBLE_DISPLAY_PHASE_H
#define MARBLE_DISPLAY_PHASE_H
/* A wakeup hint only: admission must reread both frame counters and line.
 * Sleep through the active scan, or a frame already presented, rather than
 * waking every 250us. Leave a guard before the next possible blank opening.
 * Late scheduler wakeups cannot authorize a copy. */
static uint64_t marble_blank_poll_delay(uint32_t frame,uint32_t line,
    uint32_t last,unsigned presented,unsigned active_last,unsigned total,
    uint64_t period,uint64_t budget,uint64_t poll,uint64_t guard)
{
    if(!poll || total>65535 || total<=8 || active_last>=total-8 || line>=total ||
       !period || period>UINT64_MAX/131070 || !budget || budget>=period ||
       guard>UINT64_MAX-poll)return poll;
    unsigned target=active_last+5,lines=0;
    if(presented && frame==last)lines=total-line+target;
    else if(line<target)lines=target-line;
    else if(line>=total-4 || period*(total-line-4)/total<budget)
        lines=total-line+target;
    uint64_t remaining=period*lines/total;
    return remaining>guard+poll?remaining-guard:poll;
}
static int marble_blank_admit(uint32_t before,uint32_t line,uint32_t after,
    uint32_t last,unsigned presented,unsigned active_last,unsigned total,
    uint64_t period,uint64_t budget)
{
    if(total>65535 || active_last+8>=total || !period || period>UINT64_MAX/65535 ||
       !budget || budget>=period || before!=after || (presented && after==last))return 0;
    return line>active_last+4 && line<total-4 &&
        period*(total-line-4)/total>=budget;
}
static int marble_blank_copy_completed(uint32_t before,uint32_t begin_line,
    uint32_t after,uint32_t end_line,unsigned active_last,unsigned total)
{
    return before==after && begin_line>active_last && end_line>=begin_line && end_line<total;
}
#endif
