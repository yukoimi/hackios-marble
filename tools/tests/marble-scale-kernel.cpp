/* Execute the production handoff with a userspace lock/IOSurface shim. This
 * tests its reference/timeout branches, not Darwin scheduling or GPU hardware. */
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
using IOReturn=int;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2,kIOReturnBusy=3,
      kIOReturnTimeout=4,kIOReturnUnsupported=5,kMillisecondScale=1000000,THREAD_UNINT=0,THREAD_TIMED_OUT=1};
struct IOLock {std::mutex mutex;std::condition_variable wake;};
static IOLock *IOLockAlloc(){return new IOLock;}
static void IOLockLock(IOLock *l){l->mutex.lock();}
static void IOLockUnlock(IOLock *l){l->mutex.unlock();}
static uint64_t mach_absolute_time(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *ns){*ns=ticks;}
static void clock_interval_to_deadline(unsigned interval,unsigned scale,uint64_t *deadline){*deadline=mach_absolute_time()+(uint64_t)interval*scale;}
static int IOLockSleepDeadline(IOLock *l,void *,uint64_t deadline,int)
{
    std::unique_lock<std::mutex> lock(l->mutex,std::adopt_lock);
    auto result=l->wake.wait_until(lock,std::chrono::steady_clock::time_point(std::chrono::nanoseconds(deadline)));
    lock.release();return result==std::cv_status::timeout?THREAD_TIMED_OUT:0;
}
static void IOLockWakeup(IOLock *l,void *,bool){l->wake.notify_all();}
static void IOLog(const char *,...){/* production log strings do not affect the state */}
struct MiniIOSurface {
    uint32_t sid,drawn=1;std::atomic<int> refs{1};
    struct {uint32_t width=1080,height=2400,pixelFormat=0x42475241,planeCount=0,bytesPerElement=4,
        offset=0,bytesPerRow=4352,allocSize=4352*2400;} info;
};
static std::unordered_map<uint32_t,MiniIOSurface *> surfaces;
static MiniIOSurface *MiniIOSurfaceCopy(uint32_t sid)
{auto at=surfaces.find(sid);if(at==surfaces.end())return nullptr;++at->second->refs;return at->second;}
static void MiniIOSurfaceDrop(MiniIOSurface *s){assert(s);assert(--s->refs>=1);}
static bool redwood_gpu_ready(){return true;}
static bool MiniIOSurfaceDrawn(MiniIOSurface *,uint64_t){return true;}
#include "../../xnu/files/iokit/Kernel/MarbleGPUScale.inc"
static constexpr uint64_t owner=19;
static MiniIOSurface source,output;
static void setup()
{
    assert(source.refs==1 && output.refs==1);
    gScaleOwner=0;gScaleDisabled=false;gScaleSource=gScaleOutput=nullptr;
    gScaleState={};gScaleOffered=gScaleFinished=gScaleFailures=gScaleTimeouts=gScaleLate=0;
    marble_scale_wire q={};q.version=MARBLE_SCALE_VERSION;q.op=MarbleScaleStart;q.output=output.sid;
    assert(marbleScaleCall(owner,&q)==0);assert(output.refs==2);
}
static void offered()
{
    uint64_t deadline=mach_absolute_time()+1000000000;
    for(;;) {
        IOLockLock(gScaleLock);bool yes=gScaleState.phase==MarbleScaleOffered;IOLockUnlock(gScaleLock);
        if(yes)return;assert(mach_absolute_time()<deadline);std::this_thread::yield();
    }
}
static marble_scale_wire take()
{
    marble_scale_wire q={};q.version=MARBLE_SCALE_VERSION;q.op=MarbleScaleWait;
    assert(marbleScaleCall(owner,&q)==0);assert(q.source==source.sid && q.output==output.sid);return q;
}
static IOReturn complete(marble_scale_wire q)
{q.op=MarbleScaleComplete;return marbleScaleCall(owner,&q);}
static void close_check()
{marbleScaleClose(owner);assert(source.refs==1 && output.refs==1);}
int main()
{
    source.sid=31;output.sid=32;surfaces[source.sid]=&source;surfaces[output.sid]=&output;
    MarbleScaleConfigure(1080,2400);
    setup();MiniIOSurface *answer=nullptr;
    std::thread first([&]{answer=MarbleScaleFrame(&source,100);});offered();auto q=take();
    auto stale=q;stale.sequence++;assert(complete(stale)==kIOReturnBadArgument);
    assert(complete(q)==0);assert(complete(q)==kIOReturnBadArgument);
    first.join();assert(answer==&output);MiniIOSurfaceDrop(answer);assert(gScaleFinished==1);close_check();
    /* A delivered timeout must pin source, reject another frame, discard the
     * exact late result, then permit the next sequence. */
    setup();std::thread timeout([&]{answer=MarbleScaleFrame(&source,101);});offered();q=take();
    timeout.join();assert(!answer && gScaleState.phase==MarbleScaleAbandoned && source.refs==2 && !gScaleDisabled);
    assert(MarbleScaleFrame(&source,102)==nullptr);assert(gScaleOffered==1);
    stale=q;stale.sequence++;assert(complete(stale)==kIOReturnBadArgument);
    assert(complete(q)==0);assert(gScaleLate==1 && gScaleState.phase==MarbleScaleIdle && source.refs==1);
    std::thread next([&]{answer=MarbleScaleFrame(&source,103);});offered();auto fresh=take();
    assert(fresh.sequence!=q.sequence);assert(complete(q)==kIOReturnBadArgument);assert(complete(fresh)==0);
    next.join();assert(answer==&output);MiniIOSurfaceDrop(answer);close_check();
    /* No worker saw an offered timeout: no lingering source pin. */
    setup();assert(MarbleScaleFrame(&source,104)==nullptr);
    assert(source.refs==1 && gScaleState.phase==MarbleScaleIdle && gScaleTimeouts==1);close_check();
    /* Close may wake a display thread at any phase without double-drop. */
    setup();std::thread closing([&]{answer=MarbleScaleFrame(&source,105);});offered();q=take();
    close_check();closing.join();assert(!answer);assert(complete(q)==kIOReturnBadArgument);
    /* A GL failure disables the session; its retained source dies at close. */
    setup();std::thread failed([&]{answer=MarbleScaleFrame(&source,106);});offered();q=take();q.status=7;
    assert(complete(q)==0);failed.join();assert(!answer && gScaleDisabled);close_check();
    /* Three undelivered deadlines terminate rather than retry forever. */
    setup();for(unsigned i=0;i<3;i++)assert(!MarbleScaleFrame(&source,107+i));
    assert(gScaleDisabled && gScaleTimeouts==3 && source.refs==1);close_check();
    delete gScaleLock;gScaleLock=nullptr;
    puts("PASS production handoff: success, duplicate/stale ack, delivered/undelivered timeout, late discard, resume, close race, GL failure, retry cap; pins balanced");
}
