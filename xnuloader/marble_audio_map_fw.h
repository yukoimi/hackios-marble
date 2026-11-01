#include "../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4K.c"
#include "../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KContext.c"
#include "../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KProgram.c"
#include "marble_audio_map.h"
static unsigned marble_audio_map_cb,marble_audio_map_writes;
#ifdef XNU_MARBLE_PCM_DATA_ONLY
static unsigned marble_pcm_submitted;
#endif
static int marble_audio_map_read32(void *cookie,size_t at,uint32_t *v)
{(void)cookie;return !MARBLE_AUDIO_CONTEXT_READ32((unsigned)at,v);}
static int marble_audio_map_read64(void *cookie,size_t at,uint64_t *v)
{
    (void)cookie;uint32_t low,high;
    if(!v || (at&7) || !MARBLE_AUDIO_CONTEXT_READ32((unsigned)at,&low) ||
       !MARBLE_AUDIO_CONTEXT_READ32((unsigned)at+4,&high))return 1;
    /* Actual aligned 64-bit read after qualifying the page. */
    *v=*(volatile uint64_t *)(uintptr_t)(0x15000000ULL+at);
    __asm__ volatile("dmb sy":::"memory");return 0;
}
static int marble_audio_map_write_allowed(size_t at,unsigned bytes)
{
    size_t base=0x80000+4096*marble_audio_map_cb;
    if(bytes==4 && (at==0x1000+4*marble_audio_map_cb || at==0x1800+4*marble_audio_map_cb))return 1;
    if(bytes==4 && (at==0x814 || at==0xc14 || at==0x75f8))return 1;
    if(at<base)return 0;
    if(bytes==8)return at-base==0x20 || at-base==0x28;
    if(bytes!=4)return 0;
    switch(at-base) {case 0:case 0x10:case 0x30:case 0x38:case 0x3c:case 0x58:case 0x610:case 0x7f0:return 1;}
    return 0;
}
static int marble_audio_map_write32(void *cookie,size_t at,uint32_t v)
{
    (void)cookie;if(!marble_audio_map_write_allowed(at,4))return 1;
    uint64_t page=0x15000000ULL+(at&~4095ULL),par=marble_fw_translation(page);
    if((par&1) || (par&0x0000fffffffff000ULL)!=page)return 1;
    uint64_t address=0x15000000ULL+at;
    /* Qualcomm's trap decoder requires an explicit general register, even 0. */
    __asm__ volatile("mov w9, %w0\nstr w9, [%1]\ndsb sy"::"r"(v),"r"(address):"x9","memory");
    marble_audio_map_writes++;return 0;
}
static int marble_audio_map_write64(void *cookie,size_t at,uint64_t v)
{
    (void)cookie;if(!marble_audio_map_write_allowed(at,8))return 1;
    uint64_t page=0x15000000ULL+(at&~4095ULL),par=marble_fw_translation(page);
    if((par&1) || (par&0x0000fffffffff000ULL)!=page)return 1;
    uint64_t address=0x15000000ULL+at;
    __asm__ volatile("mov x9, %0\nstr x9, [%1]\ndsb sy"::"r"(v),"r"(address):"x9","memory");
    marble_audio_map_writes++;return 0;
}
static int marble_audio_map_barrier(void *cookie)
{(void)cookie;__asm__ volatile("dsb sy":::"memory");return 0;}
static int marble_audio_map_allocate(void *cookie,uint64_t *physical,uint64_t **tables)
{
    (void)cookie;uint64_t address=(1ULL<<36)-1;
    efi_status rc=BS->allocate_pages(AllocateMaxAddress,EfiLoaderData,4,&address);
    if(EFI_ERROR(rc))return 1;
    for(unsigned i=0;i<4;i++) {
        uint64_t par=marble_fw_translation(address+i*4096);
        if((par&1) || (par&0x0000fffffffff000ULL)!=address+i*4096) {
            BS->free_pages(address,4);return 1;
        }
    }
    memset((void *)(uintptr_t)address,0,16384);*physical=address;*tables=(uint64_t *)(uintptr_t)address;return 0;
}
static int marble_audio_map_release(void *cookie,uint64_t address)
{(void)cookie;return EFI_ERROR(BS->free_pages(address,4));}
static int marble_audio_map_publish(void *cookie,uint64_t address,uint64_t bytes)
{(void)cookie;clean_dcache(address,address+bytes);return 0;}
#include "marble_audio_tbu_fw.h"
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
static MarbleAudioMapLease marble_pcm_lease;
static unsigned marble_pcm_route_active;
static int marble_pcm_route(unsigned enable)
{
    MarbleAudioMapLease *l=&marble_pcm_lease;const ArmSmmu4KIo *io=&l->ops.io;
    if(!l->active)return 0;
    MarbleAudioDmaSnapshot expected=l->snapshot;
    if(enable) {
        if(marble_pcm_route_active || !marble_audio_map_routes(io,&expected))return 0;
        marble_pcm_route_active=1;
        if(io->write32(io->cookie,0xc14,5) || io->write32(io->cookie,0x814,0x80001801) || io->barrier(io->cookie))return 0;
        expected.smr[5]=0x80001801;expected.s2cr[5]=5;
    } else {
        if(!marble_pcm_route_active)return 0;
        if(io->write32(io->cookie,0x814,expected.smr[5]) || io->write32(io->cookie,0xc14,expected.s2cr[5]) || io->barrier(io->cookie))return 0;
        marble_pcm_route_active=0;
    }
    marble_audio_stream_writes+=2;
    return marble_audio_map_routes(io,&expected);
}
#endif
static void marble_audio_map_log(const MarbleAudioMapResult *result,unsigned ok)
{
    const MarbleAudioMapResult r=*result;
    unsigned pcm_requests=0;
#ifdef XNU_MARBLE_PCM_DATA_ONLY
#if defined(XNU_MARBLE_AUDIO_NATIVE_ENDPOINT) || defined(XNU_MARBLE_AUDIO_OWNER)
    pcm_requests=marble_audio_native_request_count();
#else
    pcm_requests=marble_pcm_submitted;
#endif
#endif
    print("marble-audio-map: result PASS=%d stage=%d status=%d context=%d allocation=%x physical=%x par=%x allocated=%d programmed=%d translated=%d disabled=%d restored=%d freed=%d retained=%d writes=%d translation_mode=%d stream_writes=%d PCM_requests=%d\n",
        (uint64_t)ok,(uint64_t)r.stage,(uint64_t)r.status,(uint64_t)r.context,r.allocation,r.physical,r.par,
        (uint64_t)r.allocated,(uint64_t)r.programmed,(uint64_t)r.translated,(uint64_t)r.disabled,
        (uint64_t)r.restored,(uint64_t)r.freed,(uint64_t)r.retained,(uint64_t)marble_audio_map_writes,
        (uint64_t)r.translation_mode,(uint64_t)marble_audio_stream_writes,(uint64_t)pcm_requests);
}
static int marble_audio_map_probe(const MarbleAudioDmaSnapshot *s,const uint32_t *words)
{
    marble_audio_map_cb=marble_audio_context_candidate(s);marble_audio_map_writes=0;
    marble_audio_stream_writes=marble_audio_tbu_writes=0;
    MarbleAudioMapOps ops={{0,0x100000,marble_audio_map_read32,marble_audio_map_read64,
        marble_audio_map_write32,marble_audio_map_write64,marble_audio_map_barrier},
        marble_audio_map_allocate,marble_audio_map_release,marble_audio_map_publish,marble_audio_tbu_translate};
    MarbleAudioMapResult r;
#ifdef XNU_MARBLE_PCM_MEMORY_ONLY
    unsigned ok=marble_audio_map_begin(&ops,s,words,&r,&marble_pcm_lease);
    if(ok)ok=marble_pcm_route(1);
    print("pm: lease PASS=%d active=%d route=%d bytes=4096 address=110000000\n",(uint64_t)ok,(uint64_t)marble_pcm_lease.active,(uint64_t)marble_pcm_route_active);
    if(!ok && !marble_pcm_lease.active)marble_audio_map_log(&r,0);
#else
    unsigned ok=marble_audio_map_cycle(&ops,s,words,&r);marble_audio_map_log(&r,ok);
#endif
    return ok;
}
