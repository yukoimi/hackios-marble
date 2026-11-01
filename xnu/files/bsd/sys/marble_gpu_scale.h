/* RAM-only GLES presentation handoff. Inspired by UniXNU GopFramebuffer's
 * sequence-bound offer/complete and hakios-apollo's separate GL blit context.
 * No scanout address, DPU, clock or panel programming is part of this ABI. */
#ifndef MARBLE_GPU_SCALE_H
#define MARBLE_GPU_SCALE_H
#include <stdint.h>
#define MARBLE_SCALE_SELECTOR 0x7c
#define MARBLE_SCALE_VERSION 1
enum {MarbleScaleQuery, MarbleScaleStart, MarbleScaleWait, MarbleScaleComplete};
enum {MarbleScaleIdle, MarbleScaleOffered, MarbleScaleDelivered, MarbleScaleDone, MarbleScaleAbandoned};
struct marble_scale_wire {
    uint32_t version,op;
    uint64_t sequence;
    uint32_t source,output,width,height,stride,status,source_width,source_height;
};
struct marble_scale_state {uint64_t sequence;uint32_t phase,status;};
static inline int marble_scale_offer(struct marble_scale_state *s)
{
    if(s->phase!=MarbleScaleIdle || s->sequence==UINT64_MAX)return 0;
    ++s->sequence;s->phase=MarbleScaleOffered;s->status=0;return 1;
}
static inline int marble_scale_deliver(struct marble_scale_state *s)
{
    if(s->phase!=MarbleScaleOffered)return 0;
    s->phase=MarbleScaleDelivered;return 1;
}
static inline int marble_scale_complete(struct marble_scale_state *s,uint64_t sequence,uint32_t status)
{
    if((s->phase!=MarbleScaleDelivered && s->phase!=MarbleScaleAbandoned) || s->sequence!=sequence)return 0;
    s->status=status;s->phase=MarbleScaleDone;return 1;
}
static inline void marble_scale_retire(struct marble_scale_state *s)
{s->phase=MarbleScaleIdle;s->status=0;} /* retain sequence across timeout/close */
/* Return 1 when no worker saw the ID, 2 when its late ack is still required.
 * No new job can reuse the output while an abandoned job is in flight. */
static inline int marble_scale_abandon(struct marble_scale_state *s)
{
    if(s->phase==MarbleScaleOffered){marble_scale_retire(s);return 1;}
    if(s->phase==MarbleScaleDelivered){s->phase=MarbleScaleAbandoned;return 2;}
    return 0;
}
static inline int marble_scale_layout(uint32_t w,uint32_t h,uint32_t stride,uint64_t bytes)
{
    return w && h && w<=4096 && h<=4096 && stride>=4*w && !(stride&63) &&
        bytes>=((uint64_t)stride*h) && bytes<=0x1000000;
}
#ifdef __cplusplus
static_assert(sizeof(marble_scale_wire)==48,"scale wire ABI");
#ifdef KERNEL
class MiniIOSurface;
void MarbleScaleConfigure(uint32_t width,uint32_t height);
MiniIOSurface *MarbleScaleFrame(MiniIOSurface *source,uint32_t swap);
#endif
#else
_Static_assert(sizeof(struct marble_scale_wire)==48,"scale wire ABI");
#endif
#endif
