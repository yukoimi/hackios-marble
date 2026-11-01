/* A bounded group of ordered ring batches. Failure retains ownership until reset.
 * Caller serializes all access, including cache maintenance and page tables. */
#ifndef MARBLE_GPU_PENDING_H
#define MARBLE_GPU_PENDING_H
#include <stdint.h>
typedef struct {
    uint32_t position,fence;
    uint64_t began_us;
    unsigned active,failed,depth;
} MarbleGpuPending;
#define MARBLE_GPU_PENDING_MAX 4u
typedef struct {
    void *cookie;
    uint64_t (*now_us)(void *);
    /* 1 only after fence, RPTR/shadow, bus health and global idle all agree;
     * 0 still busy; -1 hardware fault. No mappings or resources are released. */
    int (*sample)(void *,uint32_t position,uint32_t fence);
    int (*retire)(void *,uint32_t fence);
} MarbleGpuPendingIo;
static inline int marble_pending_begin(MarbleGpuPending *p,uint32_t position,uint32_t fence,uint64_t now)
{
    if(p->active || p->failed || position>=4096 || !fence)return 0;
    p->position=position;p->fence=fence;p->began_us=now;p->active=1;p->depth=1;return 1;
}
/* Only after successful ring publication, under the same global lock. Never
 * restart the oldest outstanding batch's timeout when more work is appended.
 * No intermediate fence retires memory: sample must establish idle at the
 * newest position/fence before the whole group may be released. */
static inline int marble_pending_append(MarbleGpuPending *p,uint32_t position,uint32_t fence,uint64_t now)
{
    if(!p->active)return marble_pending_begin(p,position,fence,now);
    if(p->failed || p->depth>=MARBLE_GPU_PENDING_MAX || position>=4096 ||
        !fence || fence<=p->fence || now<p->began_us)return 0;
    p->position=position;p->fence=fence;p->depth++;return 1;
}
/* 1 drained, 0 pending, -1 poisoned. Timeout also applies to nonblocking users. */
static inline int marble_pending_observe(MarbleGpuPending *p,const MarbleGpuPendingIo *io)
{
    if(p->failed)return -1;
    if(!p->active)return 1;
    uint64_t now=io->now_us(io->cookie);
    if(now<p->began_us) {p->failed=1;return -1;}
    int status=io->sample(io->cookie,p->position,p->fence);
    if(status<0 || (status>0 && !io->retire(io->cookie,p->fence))) {
        p->failed=1;return -1;
    }
    /* An already complete batch may have sat idle without a CPU observer.
     * Apply the timeout only to work still incomplete at this observation. */
    if(!status) {
        if(now-p->began_us>=1000000){p->failed=1;return -1;}
        return 0;
    }
    p->active=0;p->depth=0;return 1;
}
#endif
