/* Run the real drawLayer/drawRows bodies with guarded host allocations.
 * The generated include comes directly from the current production source.
 * This verifies pixels and bounds, not IOKit scheduling or GPU hardware. */
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static std::map<void *, size_t> allocations;
static unsigned allocationCalls, failAt;
template<class T> static T *allocate(size_t count) {
    if (++allocationCalls == failAt) return nullptr;
    T *p = new T[count]; allocations[p] = count * sizeof(T); return p;
}
template<class T> static void deallocate(T *p, size_t count) {
    assert(allocations.at(p) == count * sizeof(T)); allocations.erase(p); delete[] p;
}
#define IONew(T, N) allocate<T>(N)
#define IODelete(P, T, N) deallocate<T>(P, N)
static void IOLockLock(void *) {}
static void IOLockUnlock(void *) {}
static void IOLockWakeup(void *, void *, bool) { assert(false && "unexpected helper dispatch"); }
static void IOLockSleep(void *, void *, int) { assert(false && "unexpected helper wait"); }
enum { THREAD_UNINT = 0 };
static unsigned gFilter;
struct MiniIOSurface {
    struct { uint32_t offset=0, bytesPerRow=16, allocSize=64, width=4, height=4; } info;
    std::vector<uint32_t> storage = std::vector<uint32_t>(16);
    const uint8_t *pixels() const { return reinterpret_cast<const uint8_t *>(storage.data()) + info.offset; }
};
struct RowJob;
struct IOMobileFramebuffer {
    uint32_t fPanelWidth=8, fPanelHeight=8, fWidth=8, fHeight=8, fRowBytes=32;
    std::vector<uint32_t> panel = std::vector<uint32_t>(64, 0xdeadbeef);
    uint8_t *fBase = reinterpret_cast<uint8_t *>(panel.data());
    uint32_t columns[8]={}; uint16_t weights[8]={};
    uint32_t *fColumn=columns; uint16_t *fColumnWeight=weights;
    uint32_t *fBlendRow=nullptr, *fHelperBlend[3]={}, fBlendRowSize=0;
    unsigned fThreads=1, fHelpersStarted=0, fHelpersBusy=0, fHelperGeneration=0;
    bool fDiff=false; uint64_t fDiffPixels=0;
    uint32_t fDiffLeft=8,fDiffRight=0,fDiffTop=8,fDiffBottom=0;
    void *fHelperLock=nullptr; const RowJob *fHelperJob=nullptr;
    void drawRows(const RowJob *, int32_t, int32_t, uint32_t *);
    void drawLayer(MiniIOSurface *, const int32_t *, const int32_t *);
    ~IOMobileFramebuffer() {
        if(fBlendRow) IODelete(fBlendRow,uint32_t,fBlendRowSize);
        for(auto p:fHelperBlend) if(p) IODelete(p,uint32_t,fBlendRowSize);
    }
};
#include "display-production.inc"

static void reset(IOMobileFramebuffer &fb) { for(auto &v:fb.panel) v=0xdeadbeef; }
static void unchanged(const IOMobileFramebuffer &fb) { for(auto v:fb.panel) assert(v==0xdeadbeef); }

int main() {
    MiniIOSurface s;
    for(unsigned i=0;i<16;i++) s.storage[i]=0xff000000u+i*0x00010101u;
    const int32_t src[]={0,0,4,4};
    const int32_t full[]={0,0,8,8};
    assert(surfaceHolds(&s,4,4));
    s.info.allocSize=63; assert(!surfaceHolds(&s,4,4)); s.info.allocSize=64;
    {
        IOMobileFramebuffer fb;
        // Compare all crop positions with the independently positioned full render.
        for(unsigned filter=0;filter<2;filter++) {
            gFilter=filter; reset(fb); fb.drawLayer(&s,src,full); auto original=fb.panel;
            for(int dy=-7;dy<=7;dy++) for(int dx=-7;dx<=7;dx++) {
                reset(fb); const int32_t dst[]={dx,dy,8,8}; fb.drawLayer(&s,src,dst);
                for(int y=0;y<8;y++) for(int x=0;x<8;x++) {
                    int ox=x-dx, oy=y-dy;
                    auto expected=ox>=0 && ox<8 && oy>=0 && oy<8 ? original[oy*8+ox] : 0xdeadbeef;
                    assert(fb.panel[y*8+x]==expected);
                }
            }
            // Entirely off-panel layers and extreme signed rectangles must be cheap no-ops.
            const int32_t outside[][4]={{-20,0,8,8},{0,-20,8,8},{8,0,8,8},{0,8,8,8},
                {INT_MIN,0,INT_MAX,8},{INT_MAX,0,INT_MAX,8},{0,INT_MIN,8,INT_MAX},
                {0,INT_MAX,8,INT_MAX},{-1000000000,-1000000000,2000000000,2000000000}};
            for(auto &dst:outside) { reset(fb); fb.drawLayer(&s,src,dst); unchanged(fb); }
        }
        // A surface can claim logical rows larger than its actual allocation.
        s.info.allocSize=32; reset(fb); fb.drawLayer(&s,src,full); unchanged(fb); s.info.allocSize=64;
        s.info.offset=4; reset(fb); fb.drawLayer(&s,src,full); unchanged(fb); s.info.offset=0;
        s.info.bytesPerRow=UINT32_MAX; reset(fb); fb.drawLayer(&s,src,full); unchanged(fb); s.info.bytesPerRow=16;
        const int32_t broken[][4]={{INT_MAX,0,4,4},{0,INT_MAX,4,4},{1,1,INT_MAX,INT_MAX},{-1,0,4,4}};
        for(auto &source:broken) { reset(fb); fb.drawLayer(&s,source,full); unchanged(fb); }
        // Exact offset/end boundaries with valid backing memory still render.
        s.storage.insert(s.storage.begin(),0x99999999);s.info.offset=4;s.info.allocSize=68;
        gFilter=0;fb.drawLayer(&s,src,full); assert(fb.panel[0]==0xff000000 && fb.panel[63]==0xff0f0f0f);
        s.storage.erase(s.storage.begin());s.info.offset=0;s.info.allocSize=64;
    }
    assert(allocations.empty());
    // Fail each blend-buffer allocation, then retry: no leak or wrong-sized free.
    for(unsigned n=1;n<=4;n++) {
        IOMobileFramebuffer fb; fb.fThreads=4;gFilter=1;allocationCalls=0;failAt=n;
        fb.drawLayer(&s,src,full);assert(fb.fBlendRowSize==0 && allocations.empty());
        assert(!fb.fBlendRow);for(auto p:fb.fHelperBlend) assert(!p);
        failAt=0;fb.drawLayer(&s,src,full);assert(fb.fBlendRowSize==4 && allocations.size()==4);
    }
    assert(allocations.empty());
    puts("PASS real display functions: 450 clipped renders with pixel checks; offscreen/extreme rectangles; invalid source extents; offset/allocation bounds; four allocation-failure/retry paths");
}
