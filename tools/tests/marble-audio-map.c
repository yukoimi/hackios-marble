#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4K.c"
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KContext.c"
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KProgram.c"
#include "../../xnuloader/marble_audio_map.h"
static uint32_t mmio[0x100000/4],saved[0x100000/4];
static uint64_t pool[2048];static unsigned allocations,frees,writes,wrong_ats,tlb_stuck,restore_failure;
static int r32(void *p,size_t at,uint32_t *v)
{(void)p;assert(at<0x100000 && !(at&3));*v=mmio[at/4];return 0;}
static int r64(void *p,size_t at,uint64_t *v)
{uint32_t a,b;if(r32(p,at,&a)||r32(p,at+4,&b))return 1;*v=(uint64_t)b<<32|a;return 0;}
static int w32(void *p,size_t at,uint32_t v)
{
    (void)p;assert(at==0x1014 || at==0x1814 || (at>=0x85000 && at<=0x857f0));writes++;
    if(restore_failure && at==0x85000 && v==0xe0)return 1;
    if(at==0x85058)return 0;
    mmio[at/4]=v;
    if(at==0x857f0)mmio[0x857f4/4]=tlb_stuck;
    return 0;
}
static int w64(void *p,size_t at,uint64_t v)
{
    (void)p;assert(at==0x85020 || at==0x85028);writes++;
    mmio[at/4]=(uint32_t)v;mmio[at/4+1]=(uint32_t)(v>>32);
    return 0;
}
static int barrier(void *p){(void)p;return 0;}
static int allocate(void *p,uint64_t *a,uint64_t **t)
{(void)p;allocations++;memset(pool,0,sizeof(pool));*a=0x90000000;*t=pool;return 0;}
static int release(void *p,uint64_t a){(void)p;assert(a==0x90000000);frees++;return 0;}
static int publish(void *p,uint64_t a,uint64_t n)
{(void)p;assert(a==0x90000000 && n==16384);assert(pool[0]==0x90001003 && pool[512+128]==0x90002003);assert((pool[1024]&0xfffffffff000ULL)==0x90003000);return 0;}
static int translate(void *p,const ArmSmmu4KIo *io,const MarbleAudioDmaSnapshot *s,unsigned cb,uint64_t expected,uint64_t *par,unsigned *retain)
{(void)p;(void)io;assert(s->id0&(1u<<26));assert(cb==5 && expected==0x90003000);assert(mmio[0x85000/4]&1);*par=expected<<12;*retain=0;return wrong_ats;}
static MarbleAudioDmaSnapshot setup(void)
{
    memset(mmio,0,sizeof(mmio));allocations=frees=writes=0;
    wrong_ats=tlb_stuck=restore_failure=0;
    MarbleAudioDmaSnapshot s={.scr0=0x220406,.id0=0x4c017e71,.id1=0x6000004b};
    mmio[0]=s.scr0;mmio[0x20/4]=s.id0;mmio[0x24/4]=s.id1;mmio[0x28/4]=0x5111;
    for(unsigned i=0;i<5;i++){s.smr[i]=0x80000000|i;s.s2cr[i]=i;mmio[0x800/4+i]=s.smr[i];mmio[0xc00/4+i]=i;}
    mmio[0x85000/4]=0xe0;mmio[0x85010/4]=0x60;mmio[0x85058/4]=0x400;
    mmio[0x1014/4]=0x100ff;mmio[0x1814/4]=1;mmio[0x85004/4]=3;
    memcpy(saved,mmio,sizeof(mmio));return s;
}
int main(void)
{
    MarbleAudioMapOps ops={{0,0x100000,r32,r64,w32,w64,barrier},allocate,release,publish,translate};
    MarbleAudioMapResult r;uint32_t old[15];MarbleAudioDmaSnapshot s=setup();
    assert(marble_audio_map_words(&ops.io,5,old));
    assert(marble_audio_map_cycle(&ops,&s,old,&r));
    assert(r.translated && r.disabled && r.restored && r.freed && !r.retained && allocations==1 && frees==1);
    /* ATS/PAR and command registers may change; all configuration and routes restore. */
    for(unsigned i=0;i<0x100000/4;i++)if(mmio[i]!=saved[i])assert(i==0x85050/4 || i==0x85800/4 || i==0x857f0/4 || i==0x85610/4);
    s=setup();wrong_ats=1;assert(!marble_audio_map_cycle(&ops,&s,old,&r) && r.restored && r.freed && !r.retained);
    s=setup();tlb_stuck=1;assert(!marble_audio_map_cycle(&ops,&s,old,&r) && r.retained && !frees);
    s=setup();restore_failure=1;assert(!marble_audio_map_cycle(&ops,&s,old,&r) && r.retained && !frees);
    s=setup();mmio[0x800/4]=0x80001801;assert(!marble_audio_map_cycle(&ops,&s,old,&r) && !writes && !allocations);
    s=setup();mmio[0x85020/4]=0x1000;assert(!marble_audio_map_cycle(&ops,&s,old,&r) && !writes && !allocations);
    s=setup();s.id0&=~(1u<<26);mmio[0x20/4]=s.id0;assert(!marble_audio_map_cycle(&ops,&s,old,&r) && !writes && !allocations);
    s=setup();MarbleAudioMapLease lease={0};
    assert(marble_audio_map_begin(&ops,&s,old,&r,&lease) && lease.active && r.retained && !frees);
    assert(lease.context.lower==&lease.config && lease.context.lower_tables==&lease.built);
    assert(!marble_audio_map_begin(&ops,&s,old,&r,&lease) && !frees);
    assert(marble_audio_map_end(&lease,&r) && !lease.active && r.restored && r.freed && r.stage==9);
    puts("PASS actual ArmSmmu4K mapping/program/ATS/disable/restore; foreign routes, wrong ATS, TLB timeout and restore failure");
}
