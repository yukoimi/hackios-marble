/* Thin C bridge to the pinned UniXNU NativeAdreno ring. Local extensions:
 * adoptIdle() preserves bring-up state; writePointer() reports exact length. */
#include "../../bsd/dev/marble_gpu/upstream/NativeAdrenoRing.cpp"
static NativeAdrenoRing marbleRing;
static uint64_t marbleSelectedClient,marbleLastClient;
static uint32_t marbleSelectedQueue,marbleLastQueue;
static uint32_t marbleContextResets,marbleContextReuses;
/* Called under gGPULock. IDs never come from userspace or reused pointers. */
static void marble_ring_select_context(uint64_t client,uint32_t queue)
{marbleSelectedClient=client;marbleSelectedQueue=queue;}
extern "C" bool marble_ring_adopt(void *cookie,void *ring,void *fence,uint32_t position,uint32_t timestamp,
    int (*write32)(void *,uint32_t,uint32_t),int (*read32)(void *,uint32_t,uint32_t *),int (*barrier)(void *))
{
    NativeAdrenoRingIo io{cookie,write32,read32,barrier};
    NativeAdrenoRingCache cache{cookie,
        [](void *,const volatile uint32_t *,size_t)->int {__asm__ volatile("dsb sy":::"memory");return 0;},
        [](void *,volatile uint32_t *,size_t)->int {__asm__ volatile("dsb sy":::"memory");return 0;}};
    return marbleRing.adoptIdle(&io,&cache,ring,fence,position,timestamp)==kIOReturnSuccess;
}
extern "C" bool marble_ring_submit(const uint64_t *addresses,const uint32_t *sizes,uint32_t count,
    uint32_t *position,uint32_t *timestamp)
{
    if(!count || count>128)return false;
    uint32_t ib[512];
    for(uint32_t i=0;i<count;i++) {
        if(AdrenoPM4Type7(0x3f,3,ib+4*i)!=AdrenoPM4Success)return false;
        ib[4*i+1]=(uint32_t)addresses[i];ib[4*i+2]=(uint32_t)(addresses[i]>>32);ib[4*i+3]=sizes[i];
    }
    /* Preserve BR/BV state only for consecutive submissions of this exact
     * client and queue. Every client/queue switch still clears resource tables
     * and on-chip timestamps. The regular flush/invalidate prologue remains. */
    bool reset=!marbleSelectedClient || marbleSelectedClient!=marbleLastClient ||
        marbleSelectedQueue!=marbleLastQueue;
    if(reset)marbleRing.requestContextReset();
    if(marbleRing.submitFramed(ib,4*count,timestamp)!=kIOReturnSuccess)return false;
    *position=marbleRing.writePointer();
    marbleLastClient=marbleSelectedClient;marbleLastQueue=marbleSelectedQueue;
    if(reset)marbleContextResets++;else marbleContextReuses++;
    if(((marbleContextResets+marbleContextReuses)&1023)==0)
        IOLog("marble-gpu-context: resets=%u reuses=%u; ordered ring and cache prologue retained\n",
            marbleContextResets,marbleContextReuses);
    return true;
}
extern "C" bool marble_ring_retire(uint32_t timestamp)
{return marbleRing.wait(timestamp,nullptr,0,nullptr)==NativeAdrenoRingFenceComplete;}
