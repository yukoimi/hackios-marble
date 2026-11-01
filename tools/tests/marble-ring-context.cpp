/* Host model: publication length, reset packet isolation and wrap. No hardware claim. */
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/NativeAdrenoRing.cpp"
struct Device {
    uint32_t ring[4096]{},fence[4096]{},rptr=0,wptr=0;
    bool fail=false;
};
static int wr(void *p,uint32_t reg,uint32_t value) {
    auto &d=*static_cast<Device *>(p);
    if(d.fail)return -1;
    if(reg==0x807)d.wptr=value;
    return 0;
}
static int rd(void *p,uint32_t reg,uint32_t *value) {
    auto &d=*static_cast<Device *>(p);
    switch(reg) {
    case 0x806:*value=d.rptr;break;
    case 0x807:*value=d.wptr;break;
    case 0x800:*value=0x200000;break;
    case 0x802:*value=0x20b;break;
    case 0x804:*value=0x300000;break;
    default:*value=0;break;
    }
    return 0;
}
static int barrier(void *) {return 0;}
static int publish(void *,const volatile uint32_t *,size_t) {return 0;}
static int acquire(void *,volatile uint32_t *,size_t) {return 0;}
int main() {
    Device d;NativeAdrenoRing ring;d.rptr=d.wptr=d.fence[0]=4080;d.fence[1]=77;
    NativeAdrenoRingIo io{&d,wr,rd,barrier};
    NativeAdrenoRingCache cache{&d,publish,acquire};
    assert(ring.adoptIdle(&io,&cache,d.ring,d.fence,4080,77)==kIOReturnSuccess);
    uint32_t header;assert(AdrenoPM4Type7(0x3f,3,&header)==AdrenoPM4Success);
    uint32_t ib[]={header,0x4000000,0,16};
    uint32_t previous=4080,ts=77;
    for(unsigned n=0;n<1000;n++) {
        bool reset=n==0 || n%7==0;
        if(reset)ring.requestContextReset();
        assert(ring.submitFramed(ib,4,&ts)==kIOReturnSuccess);
        uint32_t words=4+11+31+(reset?26:0);
        assert(ring.writePointer()==((previous+words)&4095));
        assert(ring.writePointer()==d.wptr);
        uint32_t linear[72];
        for(unsigned i=0;i<words;i++)linear[i]=d.ring[(previous+i)&4095];
        AdrenoPM4Packet packets[32];size_t count=0;
        assert(AdrenoPM4Plan(linear,words,packets,32,&count)==AdrenoPM4Success);
        unsigned resets=0,flush=0,inv=0;
        for(size_t i=0;i<count;i++) {
            auto &p=packets[i];
            resets+=p.type==7 && p.identifier==0x1f;
            if(p.type==7 && p.identifier==0x46) {
                flush+=linear[p.header_word+1]==0x32;
                inv+=linear[p.header_word+1]==0x33;
            }
        }
        assert(resets==unsigned(reset) && flush==1 && inv==1);
        assert(ring.wait(ts,nullptr,0,nullptr)==NativeAdrenoRingFenceTimeout);
        d.rptr=d.fence[0]=d.wptr;d.fence[1]=ts;
        assert(ring.wait(ts,nullptr,0,nullptr)==NativeAdrenoRingFenceComplete);
        assert(!ring.outstanding());previous=d.wptr;
    }
    /* Four maximum-size submissions may be published before any progress.
     * Their disjoint ranges must survive wrap; earlier fences are not done. */
    uint32_t largest[512];for(unsigned i=0;i<128;i++)memcpy(largest+4*i,ib,sizeof(ib));
    for(unsigned group=0;group<100;group++) {
        uint32_t saved[4][580],start[4],len[4];
        for(unsigned i=0;i<4;i++) {
            start[i]=ring.writePointer();
            ring.requestContextReset();
            assert(ring.submitFramed(largest,512,&ts)==kIOReturnSuccess);
            len[i]=(ring.writePointer()-start[i])&4095;assert(len[i]==580);
            for(unsigned j=0;j<len[i];j++)saved[i][j]=d.ring[(start[i]+j)&4095];
            assert(ring.wait(ts,nullptr,0,nullptr)==NativeAdrenoRingFenceTimeout);
        }
        for(unsigned i=0;i<4;i++)for(unsigned j=0;j<len[i];j++)
            assert(saved[i][j]==d.ring[(start[i]+j)&4095]);
        assert(ring.outstanding()==4);
        d.rptr=d.fence[0]=d.wptr;d.fence[1]=ts;
        assert(ring.wait(ts,nullptr,0,nullptr)==NativeAdrenoRingFenceComplete);
        assert(!ring.outstanding());previous=d.wptr;
    }
    d.fail=true;uint32_t failed=123;
    assert(ring.submitFramed(ib,4,&failed)!=kIOReturnSuccess && failed==0);
    assert(ring.poisoned() && ring.writePointer()==previous);
    assert(ring.detach()==kIOReturnBusy);
    puts("ring context: 1000 reset/reuse + 100 depth-four maximum groups, wrap, no overwrite, completion and uncertain-write retention PASS");
}
