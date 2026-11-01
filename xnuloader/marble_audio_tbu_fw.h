/* Actual FDT: LPASS TBU151e9000, SID1800..1bff, micro-idle bit6.
 * Protocol: pinned MiCode48952ed3 arm-smmu{,-qcom}.{h,c}. */
#include "../xnu/files/bsd/dev/marble_gpu/marble_ecats.h"
static unsigned marble_audio_stream_writes,marble_audio_tbu_writes;
#ifndef MARBLE_AUDIO_TBU_TEST_IO
static int marble_audio_tbu_mapping(void)
{
    uint64_t par=marble_fw_translation(0x151e9000);
    return !(par&1) && (par&0x0000fffffffff000ULL)==0x151e9000;
}
static int marble_audio_tbu_read32(void *cookie,size_t at,uint32_t *v)
{
    (void)cookie;if(at!=0x20 || !v || !marble_audio_tbu_mapping())return 1;
    BS->stall(1);*v=*(volatile uint32_t *)(uintptr_t)(0x151e9000+at);
    __asm__ volatile("dmb sy":::"memory");return 0;
}
static int marble_audio_tbu_read64(void *cookie,size_t at,uint64_t *v)
{
    (void)cookie;if((at&7) || at>0x30 || !v || !marble_audio_tbu_mapping())return 1;
    *v=*(volatile uint64_t *)(uintptr_t)(0x151e9000+at);
    __asm__ volatile("dmb sy":::"memory");return 0;
}
static int marble_audio_tbu_write64(void *cookie,size_t at,uint64_t v)
{
    (void)cookie;if(!marble_tbu_write_allowed(at,8) || !marble_audio_tbu_mapping())return 1;
    uint64_t address=0x151e9000ULL+at;
    __asm__ volatile("mov x9, %0\nstr x9, [%1]\ndsb sy"::"r"(v),"r"(address):"x9","memory");
    marble_audio_tbu_writes++;return 0;
}
#endif
static int marble_audio_tbu_poll(const ArmSmmu4KIo *io,unsigned value)
{
    for(unsigned i=0;i<200;i++) {
        uint32_t ack;if(io->read32(io->cookie,0x75fc,&ack))return 0;
        if((ack&64)==value) {BS->stall(5);return 1;}
        BS->stall(1);
    }
    return 0;
}
static int marble_audio_tbu_translate(void *cookie,const ArmSmmu4KIo *io,
    const MarbleAudioDmaSnapshot *s,unsigned cb,uint64_t expected,uint64_t *par,unsigned *retain)
{
    (void)cookie;unsigned index=marble_audio_dma_inspect(s).first_free;
    uint32_t request,ack,now;*retain=0;
    if(index!=5 || cb!=5 || !marble_audio_tbu_mapping() || !marble_audio_map_routes(io,s) ||
       io->read32(io->cookie,0x75f8,&request) || io->read32(io->cookie,0x75fc,&ack) ||
       (request&64)!=(ack&64))return 1;
    /* Wake only LPASS's bit, preserving all unrelated TBU requests. */
    *retain=1;
    if(io->write32(io->cookie,0x75f8,request|64) || !marble_audio_tbu_poll(io,64))return 1;
    ArmSmmu4KIo tbu={0,4096,marble_audio_tbu_read32,marble_audio_tbu_read64,0,marble_audio_tbu_write64,marble_audio_map_barrier};
    uint64_t saved[4];const unsigned regs[]={0,8,0x18,0x30};uint32_t status;
    for(unsigned i=0;i<4;i++)if(tbu.read64(0,regs[i],saved+i))return 1;
    if(tbu.read32(0,0x20,&status))return 1;
    print("marble-audio-tbu: before base=151e9000 SID=1801 local=1 request=%x ack=%x halt=%x va=%x trigger=%x user=%x status=%x\n",
        (uint64_t)request,(uint64_t)ack,saved[0],saved[1],saved[2],saved[3],(uint64_t)status);
    unsigned idle=!(saved[0]&0x103ff) && saved[1]<=0xffffffffULL && !saved[2] && !(saved[3]&~(255ULL<<36)) && !(status&3);
    unsigned route=0,ok=0,restored=1;MarbleEcatsResult e={0};
    if(idle && marble_audio_map_routes(io,s)) {
        /* Fully configured context precedes publishing this exact unused SMR. */
        route=1;
        if(io->write32(io->cookie,0xc00+index*4,cb))return 1;
        marble_audio_stream_writes++;
        if(io->write32(io->cookie,0x800+index*4,0x80001801) || io->barrier(io->cookie))return 1;
        marble_audio_stream_writes++;
        if(io->read32(io->cookie,0x800+index*4,&now) || now!=0x80001801 ||
           io->read32(io->cookie,0xc00+index*4,&now) || now!=cb)return 1;
        int rc=marble_ecats_sid_access(&tbu,1,0x10000000,0,100,&e);*par=e.par;
        print("marble-audio-tbu: ecats status=%d phase=%d completed=%d restored=%d retain=%d par=%x physical=%x writes=%d polls=%d\n",
            (uint64_t)rc,(uint64_t)e.phase,(uint64_t)e.completed,(uint64_t)e.restored,(uint64_t)e.retain,e.par,e.physical,(uint64_t)e.writes,(uint64_t)e.polls);
        if(e.retain)return 1;
        ok=!rc && e.completed && e.restored && !(e.par&1) && e.physical==expected;
    }
    if(route) {
        if(io->write32(io->cookie,0x800+index*4,s->smr[index]) ||
           io->write32(io->cookie,0xc00+index*4,s->s2cr[index]) || io->barrier(io->cookie))return 1;
        marble_audio_stream_writes+=2;restored=marble_audio_map_routes(io,s);
    }
    if(!restored || io->read32(io->cookie,0x75f8,&now) || now!=(request|64) ||
       io->write32(io->cookie,0x75f8,request) || !marble_audio_tbu_poll(io,request&64) ||
       io->read32(io->cookie,0x75f8,&now) || now!=request)return 1;
    *retain=0;
    print("marble-audio-tbu: complete PASS=%d idle=%d route_restored=%d micro_idle_restored=1 stream_writes=%d TBU_writes=%d\n",
        (uint64_t)ok,(uint64_t)idle,(uint64_t)restored,(uint64_t)marble_audio_stream_writes,(uint64_t)marble_audio_tbu_writes);
    return !ok;
}
