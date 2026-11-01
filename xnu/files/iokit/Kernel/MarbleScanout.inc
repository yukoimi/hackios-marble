/* Same ABI as xnuloader/marble_scanout.h. EFI never reads an actively
 * changing scanout into USB's multi-packet transfer stream. The XNU writer
 * publishes odd while copying and even after the final store completes. */
extern "C" vm_offset_t ml_io_map_wcomb(vm_offset_t,vm_size_t);
static volatile uint64_t *gMarbleScanout;
static uint8_t *gMarbleSnapshot;
static IOBufferMemoryDescriptor *gMarbleSnapshotMemory;
extern "C" void flush_dcache64(uint64_t,unsigned,int);
static void marbleScanoutInitialize(void)
{
    IORegistryEntry *node=IORegistryEntry::fromPath("/arm-io/marble-display-timing",gIODTPlane);
    if(!node)return;
    OSData *data=OSDynamicCast(OSData,node->getProperty("scanout-sequence-pa"));
    uint64_t pa=0;
    if(data && data->getLength()==sizeof(pa))memcpy(&pa,data->getBytesNoCopy(),sizeof(pa));
    node->release();
    if(!pa || (pa&0x3fff)!=64)return;
    vm_offset_t base=ml_io_map_wcomb(pa&~0x3fffULL,0x4000);
    if(!base)return;
    volatile uint64_t *shared=(volatile uint64_t *)(base+64);
    if(shared[0]!=0x3150414e53434dULL || shared[1] || shared[2] ||
       shared[3] || shared[8] || shared[9] || shared[10]!=0x1000000)return;
    /* Use XNU-owned RAM like the existing recording buffer. Do not create a
     * large IO alias of an external firmware allocation. */
    gMarbleSnapshotMemory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut|kIOMemoryPhysicallyContiguous,0x1000000,~0ULL);
    if(!gMarbleSnapshotMemory)return;
    mach_vm_size_t length=0;
    uint64_t output=gMarbleSnapshotMemory->getPhysicalSegment(0,&length);
    if(!output || length<0x1000000) {gMarbleSnapshotMemory->release();gMarbleSnapshotMemory=NULL;return;}
    gMarbleSnapshot=(uint8_t *)gMarbleSnapshotMemory->getBytesNoCopy();
    shared[6]=output;
    gMarbleScanout=shared;
    __asm__ volatile("dsb sy" ::: "memory");
    shared[1]=2;
    __asm__ volatile("dsb sy" ::: "memory");
    IOLog("marble-scanout: snapshot-v3 ready=1 sequence=0 RAM-only=1 kernel_mutex=1 own_ram=1\n");
}
static bool marbleSnapshotPending(void)
{return gMarbleScanout && gMarbleScanout[8]!=gMarbleScanout[3];}
/* Caller holds the same lock as show()/displayPower(). Source is completed
 * staging, or zeros while powered off. No reads of slowly mapped scanout. */
static void marbleSnapshotCapture(const uint8_t *source,size_t bytes,bool on)
{
    if(!marbleSnapshotPending() || bytes>0x1000000 || !source)return;
    uint64_t requested=gMarbleScanout[8];
    __asm__ volatile("dsb sy" ::: "memory");
    if(on)memcpy(gMarbleSnapshot,source,bytes);
    else bzero(gMarbleSnapshot,bytes);
    flush_dcache64((uint64_t)(uintptr_t)gMarbleSnapshot,(unsigned)bytes,0);
    __asm__ volatile("dsb sy" ::: "memory");
    gMarbleScanout[4]=bytes;gMarbleScanout[5]=gMarbleScanout[2];
    __asm__ volatile("dsb sy" ::: "memory");
    gMarbleScanout[3]=requested;
    __asm__ volatile("dsb sy" ::: "memory");
}
/* All callers hold the framebuffer's writer lock. */
static void marbleScanoutBegin(void)
{
    if(!gMarbleScanout)return;
    gMarbleScanout[2]=gMarbleScanout[2]+1;
    __asm__ volatile("dsb sy" ::: "memory");
}
static void marbleScanoutEnd(void)
{
    if(!gMarbleScanout)return;
    __asm__ volatile("dsb sy" ::: "memory");
    gMarbleScanout[2]=gMarbleScanout[2]+1;
    __asm__ volatile("dsb sy" ::: "memory");
}
