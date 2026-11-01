/* One actual display swap at a time, serviced by a separate GLES process.
 * A timed-out job is abandoned, never shown, and must complete before the next
 * offer. Three timeouts or any GL/fence failure disable this session. The worker
 * may reuse its output only after the previous staging copy or late ack. */
#include "../../bsd/sys/marble_gpu_scale.h"
static IOLock *gScaleLock;
static uint32_t gScaleWidth,gScaleHeight;
static uint64_t gScaleOwner,gScaleOffered,gScaleFinished,gScaleFailures;
static uint64_t gScaleOfferAt,gScaleDeliverAt,gScaleCompleteAt,gScaleTimeouts,gScaleLate;
static bool gScaleDisabled;
static MiniIOSurface *gScaleOutput,*gScaleSource;
static marble_scale_state gScaleState;
static uint64_t marbleScaleUS(uint64_t ticks)
{uint64_t ns;absolutetime_to_nanoseconds(ticks,&ns);return ns/1000;}

void MarbleScaleConfigure(uint32_t width,uint32_t height)
{
    if(gScaleLock || !width || !height || width>4096 || height>4096)return;
    gScaleWidth=width;gScaleHeight=height;gScaleLock=IOLockAlloc();
}
static void marbleScaleClose(uint64_t owner)
{
    if(!gScaleLock)return;
    IOLockLock(gScaleLock);
    if(gScaleOwner==owner) {
        gScaleDisabled=true;gScaleOwner=0;
        if(gScaleSource){MiniIOSurfaceDrop(gScaleSource);gScaleSource=nullptr;}
        if(gScaleOutput){MiniIOSurfaceDrop(gScaleOutput);gScaleOutput=nullptr;}
        marble_scale_retire(&gScaleState);IOLockWakeup(gScaleLock,&gScaleState,false);
    }
    IOLockUnlock(gScaleLock);
}
static IOReturn marbleScaleCall(uint64_t owner,marble_scale_wire *q)
{
    if(q->version!=MARBLE_SCALE_VERSION)return kIOReturnBadArgument;
    if(!gScaleLock)return kIOReturnNotReady;
    IOReturn ret=kIOReturnSuccess;
    IOLockLock(gScaleLock);
    switch(q->op) {
    case MarbleScaleQuery:
        q->width=gScaleWidth;q->height=gScaleHeight;
        q->stride=(gScaleWidth*4+63)&~63u;break;
    case MarbleScaleStart: {
        if(gScaleOwner || gScaleDisabled){ret=kIOReturnBusy;break;}
        MiniIOSurface *s=MiniIOSurfaceCopy(q->output);
        if(!s || s->info.width!=gScaleWidth || s->info.height!=gScaleHeight ||
           s->info.pixelFormat!=0x42475241 || s->info.planeCount || s->info.bytesPerElement!=4 ||
           s->info.offset || !marble_scale_layout(s->info.width,s->info.height,s->info.bytesPerRow,s->info.allocSize)) {
            if(s)MiniIOSurfaceDrop(s);ret=kIOReturnBadArgument;break;
        }
        gScaleOutput=s;gScaleOwner=owner;
        IOLog("marble-gpu-scale: MSCALE-MARBLE-GLES-v1 worker=1 output=%u size=%ux%u stride=%u RAM_only=1\n",
            s->sid,gScaleWidth,gScaleHeight,s->info.bytesPerRow);break;
    }
    case MarbleScaleWait: {
        if(gScaleOwner!=owner || gScaleDisabled){ret=kIOReturnNotReady;break;}
        uint64_t deadline;clock_interval_to_deadline(50,kMillisecondScale,&deadline);
        while(gScaleOwner==owner && !gScaleDisabled && gScaleState.phase!=MarbleScaleOffered) {
            if(IOLockSleepDeadline(gScaleLock,&gScaleState,deadline,THREAD_UNINT)==THREAD_TIMED_OUT) {
                ret=kIOReturnTimeout;break;
            }
        }
        if(ret!=kIOReturnSuccess)break;
        if(gScaleDisabled || gScaleOwner!=owner || !gScaleSource || !marble_scale_deliver(&gScaleState)) {
            ret=kIOReturnNotReady;break;
        }
        q->sequence=gScaleState.sequence;q->source=gScaleSource->sid;q->output=gScaleOutput->sid;
        gScaleDeliverAt=mach_absolute_time();
        q->source_width=gScaleSource->info.width;q->source_height=gScaleSource->info.height;
        q->width=gScaleWidth;q->height=gScaleHeight;q->stride=gScaleOutput->info.bytesPerRow;break;
    }
    case MarbleScaleComplete: {
        bool late=gScaleState.phase==MarbleScaleAbandoned;
        if(gScaleOwner!=owner || gScaleDisabled || !gScaleOutput || q->output!=gScaleOutput->sid ||
           !marble_scale_complete(&gScaleState,q->sequence,q->status)) {ret=kIOReturnBadArgument;break;}
        gScaleCompleteAt=mach_absolute_time();
        if(late) {
            ++gScaleLate;
            IOLog("marble-gpu-scale: late_seq=%llu discarded=1 late=%llu queue_us=%llu work_us=%llu status=%u\n",
                q->sequence,gScaleLate,marbleScaleUS(gScaleDeliverAt-gScaleOfferAt),
                marbleScaleUS(gScaleCompleteAt-gScaleDeliverAt),q->status);
            if(gScaleSource){MiniIOSurfaceDrop(gScaleSource);gScaleSource=nullptr;}
            if(q->status)gScaleDisabled=true;
            marble_scale_retire(&gScaleState);
        }
        IOLockWakeup(gScaleLock,&gScaleState,false);break;
    }
    default:ret=kIOReturnUnsupported;break;
    }
    IOLockUnlock(gScaleLock);return ret;
}
MiniIOSurface *MarbleScaleFrame(MiniIOSurface *source,uint32_t swap)
{
    if(!gScaleLock || !source)return nullptr;
    IOLockLock(gScaleLock);
    if(!gScaleOwner || gScaleDisabled || gScaleState.phase!=MarbleScaleIdle || !marble_scale_offer(&gScaleState)) {
        IOLockUnlock(gScaleLock);return nullptr;
    }
    gScaleSource=MiniIOSurfaceCopy(source->sid);
    if(!gScaleSource) {
        marble_scale_retire(&gScaleState);IOLockUnlock(gScaleLock);return nullptr;
    }
    ++gScaleOffered;
    gScaleOfferAt=mach_absolute_time();gScaleDeliverAt=gScaleCompleteAt=0;
    const uint64_t sequence=gScaleState.sequence;
    uint64_t deadline;clock_interval_to_deadline(50,kMillisecondScale,&deadline);
    IOLockWakeup(gScaleLock,&gScaleState,false);
    while(!gScaleDisabled && gScaleState.phase!=MarbleScaleDone) {
        if(IOLockSleepDeadline(gScaleLock,&gScaleState,deadline,THREAD_UNINT)==THREAD_TIMED_OUT)break;
    }
    MiniIOSurface *output=nullptr;
    if(!gScaleDisabled && gScaleState.phase==MarbleScaleDone && !gScaleState.status && gScaleOutput)
        output=MiniIOSurfaceCopy(gScaleOutput->sid);
    if(output) {
        MiniIOSurfaceDrop(gScaleSource);gScaleSource=nullptr;
        /* Do not hold this gate across GPU waits: the Mesa client uses its own
         * connection and the normal GPU lifetime/cache rules. */
        IOLockUnlock(gScaleLock);
        bool ready=redwood_gpu_ready() && output->drawn && MiniIOSurfaceDrawn(output,50);
        IOLockLock(gScaleLock);
        if(!ready || gScaleDisabled){MiniIOSurfaceDrop(output);output=nullptr;}
    }
    if(output)++gScaleFinished;
    else {
        ++gScaleFailures;
        if(!gScaleDisabled && marble_scale_abandon(&gScaleState)) {
            /* An offered but undelivered ID can be released immediately.
             * A delivered ID stays pinned until the exact late ack/close. */
            if(++gScaleTimeouts>=3)gScaleDisabled=true;
            if(gScaleState.phase==MarbleScaleIdle && gScaleSource) {
                MiniIOSurfaceDrop(gScaleSource);gScaleSource=nullptr;
            }
        } else gScaleDisabled=true;
    }
    if(output || gScaleDisabled)marble_scale_retire(&gScaleState);
    if(gScaleOffered<=8 || !(gScaleOffered%120) || !output)
        IOLog("marble-gpu-scale: seq=%llu swap=%u offers=%llu finished=%llu failures=%llu accepted=%u disabled=%u queue_us=%llu work_us=%llu total_us=%llu timeouts=%llu\n",
            sequence,swap,gScaleOffered,gScaleFinished,gScaleFailures,!!output,gScaleDisabled,
            gScaleDeliverAt?marbleScaleUS(gScaleDeliverAt-gScaleOfferAt):0,
            gScaleCompleteAt?marbleScaleUS(gScaleCompleteAt-gScaleDeliverAt):0,
            marbleScaleUS(mach_absolute_time()-gScaleOfferAt),gScaleTimeouts);
    IOLockWakeup(gScaleLock,&gScaleState,false);
    IOLockUnlock(gScaleLock);return output;
}
