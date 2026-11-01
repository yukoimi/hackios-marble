/* Opt-in source-kernel worker. Owns GLINK only; EFI owns power, firmware,
 * SMMU, board routing and PAS. First cross-core session uses a private zero
 * queue, so the existing userland queue stress patterns cannot reach DSP.
 * This is not registered as an iOS playback sink yet. */
#include <kern/thread.h>
extern kern_return_t thread_terminate(thread_t thread);
#include <sys/marble_audio_endpoint.h>
#include <sys/marble_audio_owner.h>
#include <sys/marble_audio_board_gate.h>
#include <sys/marble_audio_pcm_probe.h>
#include <sys/marble_audio_refill.h>
#include <sys/marble_audio_stream_window.h>
static struct {
    vm_offset_t mailbox,smem,ipcc,dma;
    MarbleAudioOwnerInfo info;
    MarbleAudioOwnerGrant grant;
    MarbleAudioEndpoint endpoint;
    MarbleAudioQueue zero_queue;
    unsigned command,failed,retired,unmapped,published;
    unsigned pcm_probe,feed_frames,feed_peak;
    unsigned board_gate,drained_published;
    unsigned refill,refill_waiting,refill_resumed;
    unsigned stream_window,stream_max_queued,stream_writes;
    MarbleAudioProof configured_proof;
} marble_audio_kernel;
/* Called only under the existing audio_lock. The original RAM queue keeps
 * its own unmodified samples/stats; this private diagnostic copy is guarded
 * by the original project protection and then bounded to 64 signed LSB. */
static void marble_audio_owner_tap(const int16_t *frames,unsigned count)
{
    unsigned target=marble_audio_kernel.stream_window?MAWINDOW_INPUT_FRAMES:marble_audio_kernel.refill?MAREFILL_FRAMES:2048;
    if(!marble_audio_kernel.pcm_probe || marble_audio_kernel.failed || marble_audio_kernel.feed_frames>=target || !frames || !count || count>1024)return;
    int16_t copy[2048];unsigned n=count;
    if(n>target-marble_audio_kernel.feed_frames)n=target-marble_audio_kernel.feed_frames;
    memcpy(copy,frames,n*4);redwood_guard_process(copy,n);
    for(unsigned i=0;i<n*2;i++) {
        copy[i]=marble_audio_pcm_probe_sample(copy[i]);
        unsigned peak=copy[i]<0?-(int32_t)copy[i]:copy[i];
        if(peak>marble_audio_kernel.feed_peak)marble_audio_kernel.feed_peak=peak;
    }
    unsigned accepted=marble_audio_queue_push(&marble_audio_kernel.zero_queue,copy,n);
    if(accepted!=n)marble_audio_kernel.failed=1;
    marble_audio_kernel.feed_frames+=accepted;
    if(marble_audio_kernel.stream_window) {
        unsigned queued=marble_audio_kernel.zero_queue.head-marble_audio_kernel.zero_queue.tail;
        if(queued>marble_audio_kernel.stream_max_queued)marble_audio_kernel.stream_max_queued=queued;
        marble_audio_kernel.stream_writes++;
    }
    if(marble_audio_kernel.feed_frames==target)printf("marble-audio-owner-feed: source=redwood-device-write frames=%u peak=%u guard=original HPF=1 gain_db=0 extra_divisor=16 peak_bound=64 HAL_sink=0 audible=0\n",target,marble_audio_kernel.feed_peak);
}
/* audio_lock held by ioctl and by the sole endpoint worker. This exposes
 * queue progress only; it never reads SMEM/MMIO or changes stream state. */
static int marble_audio_refill_status(MarbleAudioRefillStatus *p)
{
    if(!marble_audio_kernel.refill || !marble_audio_refill_request_valid(p))return 0;
    MarbleAudioStream *s=&marble_audio_kernel.endpoint.stream;
    MarbleAudioQueue *q=&marble_audio_kernel.zero_queue;
    *p=(MarbleAudioRefillStatus){.magic=MAREFILL_MAGIC,.version=1,.bytes=64,
        .state=marble_audio_kernel.failed?MAREFILL_FAILED:marble_audio_kernel.retired?MAREFILL_RETIRED:
            marble_audio_kernel.refill_waiting && !marble_audio_kernel.refill_resumed?MAREFILL_WAITING:
            marble_audio_kernel.published?MAREFILL_RUNNING:MAREFILL_PREBUFFER,
        .epoch=marble_audio_kernel.info.epoch,.feed=marble_audio_kernel.feed_frames,
        .published=marble_audio_kernel.published,.submitted=s->submitted,.completed=s->completed,
        .queued=q->head-q->tail,.pending=s->pending,.consumed=(uint32_t)q->consumed,.silence=(uint32_t)q->silence};
    return 1;
}
static int mak_stream_before_data(const MarbleAudioStream *stream)
{
    if(stream->completed!=3 || marble_audio_kernel.refill_resumed)return 1;
    MarbleAudioQueue *q=&marble_audio_kernel.zero_queue;
    if(!marble_audio_kernel.refill_waiting) {
        if(marble_audio_kernel.feed_frames!=512 || q->head!=q->tail || q->consumed!=512 || q->silence!=256)return -1;
        marble_audio_kernel.refill_waiting=1;
        printf("marble-audio-stream: phase=waiting ABI=MAUDIO-STREAM24-MUTED-v2 submitted=3 completed=3 pending=0 feed=512 consumed=512 silence=256 queued=0 audible=0\n");
    }
    if(marble_audio_kernel.feed_frames<1536)return 0;
    unsigned queued=q->head-q->tail;
    if(marble_audio_kernel.feed_frames>MAWINDOW_INPUT_FRAMES || queued<1024 || queued>2048 || q->dropped ||
       q->consumed!=512 || q->silence!=256 || marble_audio_kernel.feed_frames!=512+queued)return -1;
    marble_audio_kernel.refill_resumed=1;
    printf("marble-audio-stream: phase=resumed feed=%u queued=%u consumed=512 silence=256 pending=0 audible=0\n",
        marble_audio_kernel.feed_frames,queued);
    return 1;
}
static int mak_before_data(void *cookie,const MarbleAudioStream *stream)
{
    (void)cookie;
    if(!marble_audio_kernel.refill || stream->failed || stream->pending)return -1;
    if(marble_audio_kernel.stream_window)return mak_stream_before_data(stream);
    if(stream->completed!=3)return 1;
    if(marble_audio_kernel.refill_resumed)return 1;
    MarbleAudioQueue *q=&marble_audio_kernel.zero_queue;
    if(!marble_audio_kernel.refill_waiting) {
        if(marble_audio_kernel.feed_frames!=512 || q->head!=q->tail || q->consumed!=512 || q->silence!=256)return -1;
        marble_audio_kernel.refill_waiting=1;
        printf("marble-audio-refill: phase=waiting ABI=MAUDIO-REFILL8-MUTED-v1 submitted=3 completed=3 pending=0 feed=512 consumed=512 silence=256 queued=0 audible=0\n");
    }
    if(marble_audio_kernel.feed_frames<MAREFILL_FRAMES)return 0;
    if(marble_audio_kernel.feed_frames!=MAREFILL_FRAMES || q->head-q->tail!=1280 || q->dropped ||
       q->consumed!=512 || q->silence!=256 || marble_audio_kernel.refill_resumed)return -1;
    marble_audio_kernel.refill_resumed=1;
    printf("marble-audio-refill: phase=resumed feed=1792 queued=1280 consumed=512 silence=256 pending=0 audible=0\n");
    return 1;
}
static uint64_t mak_now(void)
{
    uint64_t t,f;__asm__ volatile("mrs %0,cntvct_el0":"=r"(t));
    __asm__ volatile("mrs %0,cntfrq_el0":"=r"(f));
    return t/f*1000000+t%f*1000000/f;
}
static void mak_copy(void *to,vm_offset_t from,unsigned n)
{for(unsigned i=0;i<n;i++)((uint8_t *)to)[i]=((volatile uint8_t *)from)[i];}
static void mak_store(vm_offset_t to,const void *from,unsigned n)
{for(unsigned i=0;i<n;i++)((volatile uint8_t *)to)[i]=((const uint8_t *)from)[i];}
static void mak_unmap(vm_offset_t address,vm_size_t bytes)
{
    /* io_map returns the physical subpage offset in its virtual pointer.
     * ml_io_unmap does not normalize it: pmap_remove requires native pages. */
    vm_offset_t base=trunc_page(address);
    vm_size_t extent=round_page(address-base+bytes);
    ml_io_unmap(base,extent);
}
static int mak_grant(void)
{
    MarbleAudioOwnerGrant a,b;
    mak_copy(&a,marble_audio_kernel.mailbox+64,64);__asm__ volatile("dmb sy":::"memory");
    if(!a.sequence || (a.sequence&1))return 0;
    mak_copy(&b,marble_audio_kernel.mailbox+64,64);__asm__ volatile("dmb sy":::"memory");
    if(memcmp(&a,&b,64))return 0;
    if(!(marble_audio_kernel.stream_window?mawindow_grant_valid(&a,marble_audio_kernel.info.epoch):
        marble_audio_owner_grant_valid_periods(&a,marble_audio_kernel.info.epoch,marble_audio_kernel.info.periods)) || a.state==MAOWNER_FAILED)return -1;
    marble_audio_kernel.grant=a;return 1;
}
static int mak_request(unsigned command,const MarbleAudioProof *proof)
{
    MarbleAudioOwnerRequest r={.epoch=marble_audio_kernel.info.epoch,.sequence=command*2,.command=command,
        .head=marble_audio_kernel.endpoint.ring.head,.tail=marble_audio_kernel.endpoint.ring.tail,
        .tx=marble_audio_kernel.endpoint.ring.tx_messages,.rx=marble_audio_kernel.endpoint.ring.rx_messages};
    MarbleAudioProof zero={0};if(!proof)proof=&zero;
    if(command!=marble_audio_kernel.command+1 || !(marble_audio_kernel.stream_window?mawindow_request_valid(&r,proof,r.epoch):
        marble_audio_owner_request_valid_periods(&r,proof,r.epoch,marble_audio_kernel.info.periods)))return 0;
    *(volatile uint32_t *)(marble_audio_kernel.mailbox+128+8)=r.sequence-1;
    __asm__ volatile("dsb sy":::"memory");
    mak_store(marble_audio_kernel.mailbox+192,proof,128);
    r.sequence--;mak_store(marble_audio_kernel.mailbox+128,&r,64);
    __asm__ volatile("dsb sy":::"memory");
    *(volatile uint32_t *)(marble_audio_kernel.mailbox+128+8)=command*2;
    __asm__ volatile("dsb sy":::"memory");
    marble_audio_kernel.command=command;return 1;
}
static int mak_read(void *cookie,uint32_t at,void *out,uint32_t bytes)
{
    (void)cookie;if(!marble_audio_kernel.smem || at>MASMEM_BYTES || bytes>MASMEM_BYTES-at)return 0;
    mak_copy(out,marble_audio_kernel.smem+at,bytes);__asm__ volatile("dmb sy":::"memory");return 1;
}
static int mak_write(void *cookie,uint32_t at,const void *in,uint32_t bytes)
{
    (void)cookie;MarbleAudioGlink *g=&marble_audio_kernel.endpoint.ring;
    if(!g->live || !bytes || ((at|bytes)&3) || !marble_audio_kernel.smem ||
       !((bytes==4 && (at==g->memory.item[0]+4 || at==g->memory.item[0]+8)) ||
         (at>=g->memory.item[1] && bytes<=16384 && at-g->memory.item[1]<=16384-bytes)))return 0;
    for(unsigned i=0;i<bytes/4;i++)((volatile uint32_t *)(marble_audio_kernel.smem+at))[i]=marble_glink_le32((const uint8_t *)in+4*i);
    __asm__ volatile("dmb sy":::"memory");return 1;
}
static void mak_barrier(void *cookie){(void)cookie;__asm__ volatile("dsb sy":::"memory");}
static int mak_kick(void *cookie)
{
    (void)cookie;if(!marble_audio_kernel.ipcc || marble_audio_kernel.failed || marble_audio_kernel.unmapped)return 0;
    __asm__ volatile("dsb sy":::"memory");
    *(volatile uint32_t *)(marble_audio_kernel.ipcc+0xc)=0x30000;
    __asm__ volatile("dsb sy":::"memory");return 1;
}
static int mak_before_stop(void *cookie,const MarbleAudioStream *stream)
{
    (void)cookie;
    if(!marble_audio_kernel.board_gate || !marble_audio_kernel.mailbox ||
       mak_now()>=marble_audio_kernel.grant.deadline_us)return -1;
    vm_offset_t base=marble_audio_kernel.mailbox+MABG_OFFSET;
    if(!marble_audio_kernel.drained_published) {
        MarbleAudioProof proof;
        if(!(marble_audio_kernel.stream_window?mawindow_drained_export(stream,&marble_audio_kernel.configured_proof,&proof):
            marble_audio_board_gate_export(stream,&marble_audio_kernel.configured_proof,&proof)))return -1;
        proof.phase=0;mak_store(base+64,&proof,128);
        __asm__ volatile("dsb sy":::"memory");
        *(volatile uint32_t *)(base+64+8)=MABG_DRAINED;
        __asm__ volatile("dsb sy":::"memory");
        marble_audio_kernel.drained_published=1;
        printf("marble-audio-board-gate: phase=drained submitted=%u completed=%u pending=0 STOP_sent=0 ABI=%s audible=0\n",
            marble_audio_kernel.info.periods,marble_audio_kernel.info.periods,marble_audio_kernel.stream_window?"MAUDIO-BOARD-QUIET24-v2":"MAUDIO-BOARD-QUIET-v1");
    }
    MarbleAudioBoardGateQuiet a,b;
    mak_copy(&a,base+192,64);__asm__ volatile("dmb sy":::"memory");
    if(!a.sequence || (a.sequence&1))return 0;
    mak_copy(&b,base+192,64);__asm__ volatile("dmb sy":::"memory");
    if(memcmp(&a,&b,64))return 0;
    if(!(marble_audio_kernel.stream_window?mawindow_quiet_valid(&a,marble_audio_kernel.info.epoch):
        marble_audio_board_gate_quiet_valid(&a,marble_audio_kernel.info.epoch)))return -1;
    printf("marble-audio-board-gate: phase=quiet-ACK submitted=%u completed=%u flags=7 STOP_allowed=1 audible=0\n",
        marble_audio_kernel.info.periods,marble_audio_kernel.info.periods);
    return 1;
}
static int mak_publish(void *cookie,const void *page,unsigned bytes)
{
    (void)cookie;
    if((uintptr_t)page!=marble_audio_kernel.dma || bytes!=4096 || !marble_audio_kernel.dma || marble_audio_kernel.published>=marble_audio_kernel.info.periods)return 0;
    MarbleAudioPcmCheck check;
    if(!marble_audio_pcm_probe_page(page,bytes,marble_audio_kernel.pcm_probe,&check))return 0;
    if(!marble_audio_kernel.published) {
        MarbleAudioProof proof;
        if(!marble_audio_stream_export_proof(&marble_audio_kernel.endpoint.stream,MAPROOF_CONFIGURE,&proof) ||
           !mak_request(MAOWNER_CONFIGURE,&proof))return 0;
        marble_audio_kernel.configured_proof=proof;
        uint64_t until=mak_now()+1000000;unsigned ready=0;
        do {
            int received=mak_grant();if(received<0)return 0;
            if(received && marble_audio_kernel.grant.state==MAOWNER_CONFIGURED && marble_audio_kernel.grant.acknowledged==4) {ready=1;break;}
            delay_for_interval(100,NSEC_PER_USEC);
        } while(mak_now()<until);
        if(!ready)return 0;
    }
    if(marble_audio_kernel.grant.page_pa==0 || mak_now()>=marble_audio_kernel.grant.deadline_us)return 0;
    /* WCOMB mapping does not allocate dirty CPU cache lines. */
    __asm__ volatile("dsb sy":::"memory");
    if(marble_audio_kernel.pcm_probe)printf("marble-audio-owner-PCM: period=%u peak=%u nonzero=%u hash=%x bytes=1024 tail_zero=1 audible=0\n",marble_audio_kernel.published,check.peak,check.nonzero,check.hash);
    marble_audio_kernel.published++;return 1;
}
static void mak_worker(void *argument,wait_result_t wait_result)
{
    (void)argument;(void)wait_result;
    uint64_t until=mak_now()+10000000;unsigned initialized=0,success=0;
    if(marble_audio_kernel.pcm_probe) {
        uint64_t feed_until=mak_now()+1500000;unsigned ready=0;
        do {
            lck_mtx_lock(&audio_lock);ready=marble_audio_kernel.feed_frames==(marble_audio_kernel.refill?512u:2048u) && !marble_audio_kernel.failed;lck_mtx_unlock(&audio_lock);
            if(ready)break;delay_for_interval(100,NSEC_PER_USEC);
        }while(mak_now()<feed_until);
        if(!ready)goto fail;
    }
    if(!mak_request(MAOWNER_READY,0))goto fail;
    printf("marble-audio-owner-kernel: ABI=%s phase=ready epoch=%llx GLINK_owner=XNU zero=%u HAL_sink=0\n",marble_audio_kernel.stream_window?"MAUDIO-OWNER-XNU-STREAM24-MUTED-v2":marble_audio_kernel.pcm_probe?"MAUDIO-OWNER-XNU-PCM8-MUTED-v1":marble_audio_kernel.info.periods==8?"MAUDIO-OWNER-XNU-ZERO8-v1":"MAUDIO-OWNER-XNU-ZERO-v1",marble_audio_kernel.info.epoch,!marble_audio_kernel.pcm_probe);
    while(mak_now()<until) {
        int received=mak_grant();if(received<0)goto fail;
        if(!received)goto wait;
        MarbleAudioOwnerGrant *g=&marble_audio_kernel.grant;
        if(g->state==MAOWNER_RELEASED) {success=marble_audio_kernel.unmapped && g->acknowledged==8;break;}
        if(g->state==MAOWNER_PAS_OFF) {
            if(!marble_audio_kernel.retired || !initialized || g->acknowledged!=6)goto fail;
            if(!marble_audio_kernel.unmapped) {
                /* No page access or stream_reset_after_pas after this ACK:
                 * the EFI owner may immediately zero and free its allocation. */
                mak_unmap(marble_audio_kernel.dma,4096);marble_audio_kernel.dma=0;
                mak_unmap(marble_audio_kernel.smem,MASMEM_BYTES);marble_audio_kernel.smem=0;
                mak_unmap(marble_audio_kernel.ipcc,4096);marble_audio_kernel.ipcc=0;
                marble_audio_kernel.unmapped=1;
                if(!mak_request(MAOWNER_UNMAPPED,0))goto fail;
            }
            goto wait;
        }
        if(g->state==MAOWNER_PREPARING)goto wait;
        if(g->state!=MAOWNER_RUNNING && g->state!=MAOWNER_CONFIGURED)goto fail;
        if(!marble_audio_kernel.retired && mak_now()>=g->deadline_us)goto fail;
        if(!initialized) {
            if(g->state!=MAOWNER_RUNNING || g->acknowledged!=2)goto fail;
            marble_audio_kernel.smem=ml_io_map_unmappable(0x80900000,MASMEM_BYTES,VM_WIMG_WCOMB);
            marble_audio_kernel.ipcc=ml_io_map_unmappable(0xed18000,4096,VM_WIMG_WCOMB);
            marble_audio_kernel.dma=ml_io_map_unmappable(g->page_pa,4096,VM_WIMG_WCOMB);
            if(!marble_audio_kernel.smem || !marble_audio_kernel.ipcc || !marble_audio_kernel.dma)goto fail;
            MarbleAudioGlinkIO io={0,mak_read,mak_write,mak_barrier,mak_kick};
            if(!marble_audio_endpoint_init(&marble_audio_kernel.endpoint,&io,mak_publish,&marble_audio_kernel.zero_queue,
               (void *)marble_audio_kernel.dma,marble_audio_kernel.info.periods,mak_now()) ||
               !marble_audio_stream_bind_epoch(&marble_audio_kernel.endpoint.stream,marble_audio_kernel.info.epoch))goto fail;
            if(marble_audio_kernel.board_gate)marble_audio_kernel.endpoint.before_stop=mak_before_stop;
            if(marble_audio_kernel.refill)marble_audio_kernel.endpoint.before_data=mak_before_data;
            initialized=1;
        }
        if(marble_audio_kernel.pcm_probe)lck_mtx_lock(&audio_lock);
        int result=marble_audio_endpoint_poll(&marble_audio_kernel.endpoint,mak_now());
        if(marble_audio_kernel.pcm_probe)lck_mtx_unlock(&audio_lock);
        if(result<0)goto fail;
        if(result==1 && !marble_audio_kernel.retired) {
            if(marble_audio_kernel.stream_window) {
                MarbleAudioQueue *q=&marble_audio_kernel.zero_queue;
                if(marble_audio_kernel.feed_frames!=5888 || q->consumed!=5888 || q->silence!=256 || q->dropped ||
                   q->head!=5888 || q->tail!=5888 || marble_audio_kernel.stream_writes!=19 ||
                   marble_audio_kernel.stream_max_queued<1024 || marble_audio_kernel.stream_max_queued>2048)goto fail;
                printf("marble-audio-stream: phase=retired input_frames=5888 output_frames=6144 periods=24 consumed=5888 silence=256 queued=0 head=5888 tail=5888 wraps=1 writes=19 max_queued=%u HAL_sink=0 audible=0\n",marble_audio_kernel.stream_max_queued);
            }
            MarbleAudioProof proof;
            if(!marble_audio_stream_export_proof(&marble_audio_kernel.endpoint.stream,MAPROOF_RETIRED,&proof) ||
               !mak_request(MAOWNER_RETIRE,&proof))goto fail;
            marble_audio_kernel.retired=1;
            printf("marble-audio-owner-kernel: phase=retired submitted=%u completed=%u published=%u RX=%u TX=%u zero=%u\n",
                proof.submitted,proof.completed,marble_audio_kernel.published,marble_audio_kernel.endpoint.ring.rx_messages,marble_audio_kernel.endpoint.ring.tx_messages,!marble_audio_kernel.pcm_probe);
        }
wait:
        delay_for_interval(100,NSEC_PER_USEC);
    }
    if(!success)goto fail;
    printf("marble-audio-owner-kernel: phase=complete PASS=1 submitted=%u completed=%u DMA_unmapped=1 PAS_off=1 route_restored=1 DMA_owner_released=1 HAL_sink=0 audio_acceptance=0\n",marble_audio_kernel.info.periods,marble_audio_kernel.info.periods);
    mak_unmap(marble_audio_kernel.mailbox,MAOWNER_PAGE_BYTES);marble_audio_kernel.mailbox=0;
    thread_terminate(current_thread());return;
fail:
    marble_audio_kernel.failed=1;
    printf("marble-audio-owner-kernel: phase=failed retained=1 command=%u stream=%u pending=%u published=%u DMA_unmapped=%u owner_state=%u PAS_inferred=0 audio_acceptance=0\n",
        marble_audio_kernel.command,marble_audio_kernel.endpoint.stream.phase,marble_audio_kernel.endpoint.stream.pending,
        marble_audio_kernel.published,marble_audio_kernel.unmapped,marble_audio_kernel.grant.state);
    /* Invalid request makes the owner retain resources/watchdog. Do not
     * infer PAS-off or free the page on an endpoint/timeout failure. */
    *(volatile uint32_t *)(marble_audio_kernel.mailbox+128+12)=0xffffffffu;
    __asm__ volatile("dsb sy":::"memory");
    thread_terminate(current_thread());
}
static void marble_audio_owner_kernel_start(void)
{
    uint32_t enabled=0;
    if(!PE_parse_boot_argn("marble_audio_owner",&enabled,sizeof(enabled)) || enabled!=1)return;
    DTEntry entry;const void *prop;unsigned size;uint64_t pa=0;
    unsigned ok=SecureDTLookupEntry(NULL,"/arm-io/marble-audio-owner",&entry)==kSuccess &&
        SecureDTGetProperty(entry,"compatible",&prop,&size)==kSuccess && size==sizeof("marble,audio-owner-zero-v1") &&
        !memcmp(prop,"marble,audio-owner-zero-v1",size);
    if(ok && SecureDTGetProperty(entry,"reg",&prop,&size)==kSuccess && size==16 && ((const uint64_t *)prop)[1]==MAOWNER_PAGE_BYTES)pa=((const uint64_t *)prop)[0];
    if(!pa || (pa&(MAOWNER_PAGE_BYTES-1)) || pa<0x80000000 || pa>0x100000000ULL-MAOWNER_PAGE_BYTES)ok=0;
    if(ok)marble_audio_kernel.mailbox=ml_io_map_unmappable(pa,MAOWNER_PAGE_BYTES,VM_WIMG_WCOMB);
    if(!ok || !marble_audio_kernel.mailbox) {printf("marble-audio-owner-kernel: invalid DT mailbox; no DSP operation\n");return;}
    mak_copy(&marble_audio_kernel.info,marble_audio_kernel.mailbox,64);
    uint32_t window=0;PE_parse_boot_argn("marble_audio_stream",&window,sizeof(window));
    if(window>1 || !(window?mawindow_info_valid(&marble_audio_kernel.info):
        marble_audio_owner_info_valid_periods(&marble_audio_kernel.info,marble_audio_kernel.info.periods))) {
        mak_unmap(marble_audio_kernel.mailbox,MAOWNER_PAGE_BYTES);marble_audio_kernel.mailbox=0;return;
    }
    marble_audio_kernel.stream_window=window;
    uint32_t pcm=0;PE_parse_boot_argn("marble_audio_owner_pcm",&pcm,sizeof(pcm));
    uint32_t gate=0;PE_parse_boot_argn("marble_audio_board_gate",&gate,sizeof(gate));
    MarbleAudioBoardGate sideband;
    mak_copy(&sideband,marble_audio_kernel.mailbox+MABG_OFFSET,sizeof(sideband));
    if((gate && (gate!=1 || pcm!=1 ||
        !(window?mawindow_board_info_valid(&sideband.info,marble_audio_kernel.info.epoch):
            marble_audio_board_gate_info_valid(&sideband.info,marble_audio_kernel.info.epoch)) ||
        !maowner_zero(&sideband.drained,sizeof(sideband.drained)) || !maowner_zero(&sideband.quiet,sizeof(sideband.quiet)))) ||
       (!gate && !maowner_zero(&sideband,sizeof(sideband)))) {
        printf("marble-audio-board-gate: incompatible opt-in; no DSP operation\n");
        mak_unmap(marble_audio_kernel.mailbox,MAOWNER_PAGE_BYTES);marble_audio_kernel.mailbox=0;return;
    }
    marble_audio_kernel.board_gate=gate;
    uint32_t refill=0;PE_parse_boot_argn("marble_audio_refill",&refill,sizeof(refill));
    if((refill && (refill!=1 || window || !gate || pcm!=1)) || (window && (!gate || pcm!=1))) {
        mak_unmap(marble_audio_kernel.mailbox,MAOWNER_PAGE_BYTES);marble_audio_kernel.mailbox=0;return;
    }
    marble_audio_kernel.refill=refill || window;
    if(pcm) {
        if(pcm!=1 || (marble_audio_kernel.info.periods!=8 && !window)) {mak_unmap(marble_audio_kernel.mailbox,MAOWNER_PAGE_BYTES);marble_audio_kernel.mailbox=0;return;}
        redwood_guard_configure(true,0);redwood_guard_attenuation(0);redwood_guard_monitor(0);
        marble_audio_kernel.pcm_probe=1;
    }
    thread_t thread;
    if(kernel_thread_start(mak_worker,0,&thread)!=KERN_SUCCESS) {ml_io_unmap(marble_audio_kernel.mailbox,MAOWNER_PAGE_BYTES);marble_audio_kernel.mailbox=0;return;}
    thread_deallocate(thread);
}
