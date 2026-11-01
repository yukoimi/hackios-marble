/* AOP changes the descriptor asynchronously. Never issue an AP ACK from a
 * torn or busy snapshot. The caller keeps the original close deadline and
 * validates every stable transition with marble_qmp_borrowed_close_step. */
#ifndef MARBLE_QMP_CLOSE_SNAPSHOT_H
#define MARBLE_QMP_CLOSE_SNAPSHOT_H
#include "marble_qmp.h"
typedef int (*MarbleQmpCloseRead)(void *,MarbleQmpDescriptor *,unsigned *);
static int marble_qmp_close_snapshot(MarbleQmpCloseRead read,void *ctx,MarbleQmpDescriptor *out)
{
    if(!read || !out)return -1;
    MarbleQmpDescriptor a,b;unsigned ae=0,be=0;
    int ar=read(ctx,&a,&ae);if(ar<0)return -1;if(!ar)return 0;
    int br=read(ctx,&b,&be);if(br<0)return -1;if(!br)return 0;
    const uint32_t *ap=(const uint32_t *)&a,*bp=(const uint32_t *)&b;
    for(unsigned i=0;i<sizeof(a)/4;i++)if(ap[i]!=bp[i])return 0;
    if(!marble_qmp_valid(&a,1024) || ae>1 || be>1)return -1;
    if(!ae || !be)return 0;
    *out=a;return 1;
}
#endif
