/* RAM-only scanout publication. One cache line, single XNU writer protected
 * by the framebuffer lock; EFI copies to private, pre-reserved memory before
 * USB transfer. Never stream a framebuffer that may change during USB I/O. */
#ifndef MARBLE_SCANOUT_H
#define MARBLE_SCANOUT_H
#define MARBLE_SCANOUT_MAGIC 0x3150414e53434dULL
typedef struct {
    uint64_t magic;
    volatile uint64_t ready,sequence;
    volatile uint64_t completed,bytes,captured_sequence;
    volatile uint64_t output_pa;
    uint64_t reserved;
    /* Separate cache line: EFI writes request, XNU writes the first line. */
    volatile uint64_t request;
    uint64_t buffer_pa,capacity,reserved_request[5];
} MarbleScanoutShared;
static int marble_snapshot_complete(uint64_t requested,uint64_t completed,uint64_t bytes,uint64_t wanted)
{return requested && requested==completed && bytes==wanted;}
static int marble_scanout_stable(uint64_t ready,uint64_t before,uint64_t after)
{return ready==1 && !(before&1) && before==after;}
#endif
