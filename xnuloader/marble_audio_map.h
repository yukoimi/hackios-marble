/* Isolated context mapping/hardware translation test. No PCM submission.
 * Call only while the DSP is held and no native IOMMU driver can run. */
#ifndef MARBLE_AUDIO_MAP_H
#define MARBLE_AUDIO_MAP_H
#include "marble_audio_context.h"
#include "../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KProgram.h"
typedef struct {
    ArmSmmu4KIo io;
    int (*allocate)(void *,uint64_t *,uint64_t **);
    int (*release)(void *,uint64_t);
    int (*publish)(void *,uint64_t,uint64_t);
    int (*hardware_translate)(void *,const ArmSmmu4KIo *,const MarbleAudioDmaSnapshot *,unsigned,uint64_t,uint64_t *,unsigned *);
} MarbleAudioMapOps;
typedef struct {
    unsigned stage,status,context,allocated,programmed,translated,disabled,restored,freed,retained,translation_mode;
    uint64_t allocation,physical,par;
} MarbleAudioMapResult;
typedef struct {
    unsigned active;MarbleAudioMapOps ops;MarbleAudioDmaSnapshot snapshot;uint32_t original[15];
    ArmSmmu4KConfig config;ArmSmmu4KResult built;ArmSmmu4KContextConfig context;
    MarbleAudioMapResult result;
} MarbleAudioMapLease;
static int marble_audio_map_routes(const ArmSmmu4KIo *io,const MarbleAudioDmaSnapshot *s)
{
    uint32_t v;
    const unsigned at[]={0,0x20,0x24,0x48};const uint32_t expected[]={s->scr0,s->id0,s->id1,s->gfsr};
    for(unsigned i=0;i<4;i++)if(io->read32(io->cookie,at[i],&v) || v!=expected[i])return 0;
    for(unsigned i=0;i<(s->id0&255);i++) {
        if(io->read32(io->cookie,0x800+4*i,&v) || v!=s->smr[i] ||
           io->read32(io->cookie,0xc00+4*i,&v) || v!=s->s2cr[i])return 0;
    }
    return 1;
}
static int marble_audio_map_words(const ArmSmmu4KIo *io,unsigned cb,uint32_t *v)
{
    const unsigned regs[]={0,0x10,0x20,0x24,0x28,0x2c,0x30,0x38,0x3c,0x58,0x7f4};
    for(unsigned i=0;i<11;i++)if(io->read32(io->cookie,0x80000+cb*4096+regs[i],v+i))return 0;
    return !io->read32(io->cookie,0x1000+4*cb,v+11) &&
        !io->read32(io->cookie,0x1800+4*cb,v+12) &&
        !io->read32(io->cookie,0x80004+cb*4096,v+13) && !io->read32(io->cookie,0x28,v+14);
}
static int marble_audio_map_restore(const ArmSmmu4KIo *io,unsigned cb,const uint32_t *v)
{
    unsigned base=0x80000+cb*4096;uint32_t now;
    if(io->read32(io->cookie,base,&now) || (now&1))return 0;
    /* Restore TCR before TTBR, exact original platform ACTLR is never written. */
    if(io->write32(io->cookie,base+0x10,v[1]) || io->write32(io->cookie,base+0x30,v[6]) ||
       io->write64(io->cookie,base+0x20,(uint64_t)v[3]<<32|v[2]) ||
       io->write64(io->cookie,base+0x28,(uint64_t)v[5]<<32|v[4]) ||
       io->write32(io->cookie,base+0x38,v[7]) || io->write32(io->cookie,base+0x3c,v[8]) ||
       io->write32(io->cookie,0x1000+4*cb,v[11]) || io->write32(io->cookie,0x1800+4*cb,v[12]) ||
       io->write32(io->cookie,base,v[0]) || io->barrier(io->cookie))return 0;
    uint32_t readback[15];if(!marble_audio_map_words(io,cb,readback))return 0;
    for(unsigned i=0;i<15;i++)if(v[i]!=readback[i])return 0;
    return 1;
}
static int marble_audio_map_end(MarbleAudioMapLease *lease,MarbleAudioMapResult *r)
{
    if(!lease || !r || !lease->active)return 0;
    *r=lease->result;const ArmSmmu4KIo *io=&lease->ops.io;
    if(r->context!=5 || !r->allocation || (r->allocation&4095) || r->allocation>=((1ULL<<36)-16384))return 0;
    if(!marble_audio_map_routes(io,&lease->snapshot))return 0;
    /* Reconstruct the single board-qualified profile from owned allocation,
     * rather than accepting mutable context-config pointers across callbacks. */
    ArmSmmu4KConfig config={32,36,r->allocation,1};
    ArmSmmu4KResult built={r->allocation,0xff44,3,1,1,0};
    ArmSmmu4KContextConfig context={&config,0,&built,0,lease->snapshot.id0,0x5111,5,0,0,0};
    ArmSmmu4KProgramResult disabled={0};r->stage=5;
    r->disabled=ArmSmmu4KDisableOwnedContext(io,r->context,&context,64,&disabled)==ArmSmmu4KSuccess && !disabled.must_retain_tables;
    if(!r->disabled)return 0;
    r->stage=6;r->restored=marble_audio_map_restore(io,r->context,lease->original) && marble_audio_map_routes(io,&lease->snapshot);
    if(!r->restored)return 0;
    r->stage=9;
    if(!lease->ops.release(io->cookie,r->allocation)) {r->freed=1;r->retained=0;lease->active=0;}
    return r->freed;
}
static int marble_audio_map_execute(const MarbleAudioMapOps *ops,const MarbleAudioDmaSnapshot *s,
    const uint32_t *original,MarbleAudioMapResult *r,MarbleAudioMapLease *lease)
{
    if(!r)return 0;*r=(MarbleAudioMapResult){0};r->context=0xffffffffu;
    if(!ops || !s || !original || !ops->allocate || !ops->release || !ops->publish || !ops->hardware_translate)return 0;
    const ArmSmmu4KIo *io=&ops->io;
    if(!io->read32 || !io->read64 || !io->write32 || !io->write64 || !io->barrier || io->register_bytes!=0x100000)return 0;
    unsigned cb=marble_audio_context_candidate(s);r->context=cb;r->stage=1;
    if(cb==0xffffffffu || !(s->id0&(1u<<26)) || original[14]!=0x5111 || original[13]!=3 ||
       (original[0]&~0xe0u) || original[2] || original[3] || original[4] || original[5] ||
       (original[9]&0xc00001feu) || (original[10]&1) || !marble_audio_map_routes(io,s))return 0;
    uint32_t words[15];if(!marble_audio_map_words(io,cb,words))return 0;
    for(unsigned i=0;i<15;i++)if(words[i]!=original[i])return 0;
    uint64_t *tables=0;r->stage=2;
    if(ops->allocate(io->cookie,&r->allocation,&tables))return 0;
    r->allocated=r->retained=1;r->physical=r->allocation+3*4096;
    if(!tables || (r->allocation&4095) || r->allocation>=((1ULL<<36)-16384))goto free_only;
    ArmSmmu4KConfig cfg={32,36,r->allocation,1};ArmSmmu4KResult built;
    ArmSmmu4KRange range={0x10000000,r->physical,4096,ArmSmmu4KRead|ArmSmmu4KWrite,1,3};
    r->status=ArmSmmu4KBuild(&cfg,&range,1,tables,3,&built);
    if(r->status || built.table_pages!=3 || built.leaf_pages!=1 ||
       ops->publish(io->cookie,r->allocation,16384) || !marble_audio_map_routes(io,s))goto free_only;
    ArmSmmu4KContextConfig context={&cfg,0,&built,0,s->id0,original[14],cb,0,0,0};
    ArmSmmu4KProgramResult program={0};r->stage=3;
    r->status=ArmSmmu4KProgramDisabledContext(io,cb,&context,64,&program);
    r->programmed=!r->status;
    if(!r->status) {
        r->stage=4;
        /* ATOSNS=1 forbids architected nonsecure ATS. Board backend uses ECATS. */
        unsigned retain=1;r->translation_mode=2;
        r->translated=!ops->hardware_translate(io->cookie,io,s,cb,r->physical,&r->par,&retain);
        if(retain)return 0;
        if(r->translated && lease) {
            *lease=(MarbleAudioMapLease){0};lease->active=1;lease->ops=*ops;lease->snapshot=*s;
            for(unsigned i=0;i<15;i++)lease->original[i]=original[i];
            lease->config=cfg;lease->built=built;lease->context=context;
            lease->context.lower=&lease->config;lease->context.lower_tables=&lease->built;
            r->stage=8;lease->result=*r;return 1;
        }
        r->stage=5;
        ArmSmmu4KProgramResult disabled={0};
        r->disabled=ArmSmmu4KDisableOwnedContext(io,cb,&context,64,&disabled)==ArmSmmu4KSuccess && !disabled.must_retain_tables;
    } else r->disabled=program.disabled_verified && program.tlb_synchronized && !program.must_retain_tables;
    if(!r->disabled)return 0;
    r->stage=6;r->restored=marble_audio_map_restore(io,cb,original) && marble_audio_map_routes(io,s);
    if(!r->restored)return 0;
free_only:
    r->stage=7;
    if(!ops->release(io->cookie,r->allocation)) {r->freed=1;r->retained=0;}
    return r->programmed && r->translated && r->disabled && r->restored && r->freed;
}
static int marble_audio_map_cycle(const MarbleAudioMapOps *ops,const MarbleAudioDmaSnapshot *s,
    const uint32_t *original,MarbleAudioMapResult *r)
{return marble_audio_map_execute(ops,s,original,r,0);}
static int marble_audio_map_begin(const MarbleAudioMapOps *ops,const MarbleAudioDmaSnapshot *s,
    const uint32_t *original,MarbleAudioMapResult *r,MarbleAudioMapLease *lease)
{
    if(!lease || lease->active)return 0;
    return marble_audio_map_execute(ops,s,original,r,lease);
}
#endif
