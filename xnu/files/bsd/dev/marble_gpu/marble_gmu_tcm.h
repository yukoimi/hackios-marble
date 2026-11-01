/* Gen7_4_0 TCM transaction. Addresses: pinned Xiaomi gen7_reg.h/load_tcm.
 * GMU stays reset. No reset/clock/IRQ/HFI writes are permitted here. */
#ifndef MARBLE_GMU_TCM_H
#define MARBLE_GMU_TCM_H
#include "upstream/AdrenoFirmware.h"
#define MARBLE_GMU_TCM_ABI "MGPU-MARBLE-GMU-TCM-v1"
#define MARBLE_TCM_WORDS 8192u
typedef struct { unsigned offset,bytes;size_t payload; } MarbleTcmSpan;
typedef struct { MarbleTcmSpan spans[3];unsigned count,words; } MarbleTcmPlan;
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,uint32_t *);
    int (*write)(void *,unsigned,uint32_t);
    int (*barrier)(void *);
    int (*guard)(void *);
} MarbleTcmIo;
typedef struct {
    unsigned stable,loaded,restored,retain,reads,writes,attempted,failure_offset;
    unsigned phase; /* 1 snapshot, 2 stable, 3 load, 4 verify, 5 restore, 6 original, 7 done */
} MarbleTcmResult;
static int marble_tcm_read_allowed(unsigned offset,unsigned bytes)
{ return bytes==4 && !(offset&3) && offset>=0x5000 && offset<0xd000; }
static int marble_tcm_write_allowed(unsigned offset,unsigned bytes,unsigned stage,unsigned lease)
{
    if(stage==17 && lease)return marble_tcm_read_allowed(offset,bytes);
    return stage==10 && lease && bytes==4 && !(offset&3) &&
        ((offset>=0x5000 && offset<0x51dc) || (offset>=0xcf98 && offset<0xcfc0) ||
         (offset>=0xcfdc && offset<0xcff4));
}
static int marble_tcm_plan(const AdrenoFirmwareBlock *blocks,size_t count,size_t fwbytes,MarbleTcmPlan *out)
{
    static const uint32_t addresses[]={0,0x10007f98,0x10007fdc};
    static const unsigned sizes[]={0x1dc,0x28,0x18},offsets[]={0x5000,0xcf98,0xcfdc};
    if(!out)return 0;
    *out=(MarbleTcmPlan){0};MarbleTcmPlan p={0};
    if(!blocks || !count || count>32)return 0;
    for(size_t i=0;i<count;i++) {
        const AdrenoFirmwareBlock *b=blocks+i;
        if(!b->bytes || (b->region_identifier!=0 && b->region_identifier!=2))continue;
        unsigned j=p.count;
        if(j>=3 || b->address!=addresses[j] || b->bytes!=sizes[j] ||
            b->region_identifier!=(j?2u:0u) || (b->payload_offset&3) ||
            b->payload_offset>fwbytes || b->bytes>fwbytes-b->payload_offset)return 0;
        p.spans[j]=(MarbleTcmSpan){offsets[j],b->bytes,b->payload_offset};p.count++;p.words+=b->bytes/4;
    }
    if(p.count!=3 || p.words!=135)return 0;
    *out=p;return 1;
}
static uint32_t marble_tcm_word(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int marble_tcm_read(const MarbleTcmIo *io,MarbleTcmResult *r,unsigned offset,uint32_t *v)
{
    r->reads++;r->failure_offset=offset;
    return marble_tcm_read_allowed(offset,4) && !io->read(io->cookie,offset,v);
}
/* snapshot is caller-owned 32K BSS, never a kernel-stack allocation. */
static int marble_tcm_transaction_work(const MarbleTcmIo *io,const MarbleTcmPlan *p,
    const uint8_t *firmware,size_t fwbytes,unsigned stage,uint32_t *snapshot,MarbleTcmResult *r,
    int (*work)(void *),void *work_cookie)
{
    if(!r)return 0;
    *r=(MarbleTcmResult){0};
    if(!io || !io->read || !io->write || !io->barrier || !io->guard || !p ||
        !firmware || !snapshot || (stage!=9 && stage!=10 && stage!=17) ||
        ((stage==17)!=!!work) || p->count!=3 || p->words!=135)return 0;
    int work_ok=1;
    /* Revalidate the complete plan before touching hardware, even if caller mutated it. */
    static const unsigned starts[]={0x5000,0xcf98,0xcfdc},sizes[]={0x1dc,0x28,0x18};
    for(unsigned j=0;j<3;j++)if(p->spans[j].offset!=starts[j] || p->spans[j].bytes!=sizes[j] ||
        (p->spans[j].payload&3) || p->spans[j].payload>fwbytes ||
        p->spans[j].bytes>fwbytes-p->spans[j].payload)return 0;
    if(io->guard(io->cookie))return 0;
    r->phase=1;
    for(unsigned i=0;i<MARBLE_TCM_WORDS;i++) {
        if(!(i&63) && io->guard(io->cookie))return 0;
        if(!marble_tcm_read(io,r,0x5000+i*4,snapshot+i))return 0;
    }
    if(io->barrier(io->cookie))return 0;
    r->phase=2;
    for(unsigned i=0;i<MARBLE_TCM_WORDS;i++) {
        uint32_t v;if(!(i&63) && io->guard(io->cookie))return 0;
        if(!marble_tcm_read(io,r,0x5000+i*4,&v) || v!=snapshot[i])return 0;
    }
    if(io->guard(io->cookie))return 0;
    r->stable=1;
    if(stage==9) {r->phase=7;r->failure_offset=0;return 1;}
    r->phase=3;
    for(unsigned j=0;j<3;j++)for(unsigned k=0;k<p->spans[j].bytes;k+=4) {
        unsigned offset=p->spans[j].offset+k;
        if(io->guard(io->cookie))return 0; /* A lost quiescence guard forbids restoring too. */
        if(!marble_tcm_write_allowed(offset,4,stage,1))goto restore;
        r->retain=1;r->attempted++;r->writes++;r->failure_offset=offset;
        if(io->write(io->cookie,offset,marble_tcm_word(firmware+p->spans[j].payload+k)))goto restore;
    }
    if(io->barrier(io->cookie))goto restore;
    r->phase=4;
    for(unsigned i=0;i<MARBLE_TCM_WORDS;i++) {
        unsigned offset=0x5000+i*4;uint32_t v,expected=snapshot[i];
        if(!(i&63) && io->guard(io->cookie))return 0;
        for(unsigned j=0;j<3;j++)if(offset>=p->spans[j].offset && offset-p->spans[j].offset<p->spans[j].bytes)
            expected=marble_tcm_word(firmware+p->spans[j].payload+offset-p->spans[j].offset);
        if(!marble_tcm_read(io,r,offset,&v) || v!=expected)goto restore;
    }
    if(io->guard(io->cookie))return 0;
    r->loaded=1;
    if(work) {
        int status=work(work_cookie);work_ok=status==0;
        if(status==-2)return 0;
    }
restore:
    r->phase=5;
    if(stage==17) {
        /* Firmware may change ANY DTCM/ITCM word. Only after confirmed
         * reset+GBIF drain may the full original image be restored. */
        for(unsigned i=0;i<MARBLE_TCM_WORDS;i++) {
            unsigned offset=0x5000+i*4;uint32_t v;
            if(io->guard(io->cookie) || !marble_tcm_read(io,r,offset,&v))return 0;
            if(v!=snapshot[i]) {
                r->writes++;
                (void)io->write(io->cookie,offset,snapshot[i]);
            }
        }
    } else
    /* Include failed/possibly delivered writes. Reverse the exact attempted prefix. */
    for(unsigned n=r->attempted;n;n--) {
        unsigned word=n-1,offset=0;
        for(unsigned j=0;j<3;j++) {
            if(word<p->spans[j].bytes/4) {offset=p->spans[j].offset+word*4;break;}
            word-=p->spans[j].bytes/4;
        }
        if(io->guard(io->cookie) || !marble_tcm_write_allowed(offset,4,stage,1))return 0;
        r->writes++;r->failure_offset=offset;
        /* A callback failure can be before or after delivery. Full readback decides. */
        (void)io->write(io->cookie,offset,snapshot[(offset-0x5000)/4]);
    }
    if(io->barrier(io->cookie))return 0;
    r->phase=6;
    for(unsigned i=0;i<MARBLE_TCM_WORDS;i++) {
        uint32_t v;if(!(i&63) && io->guard(io->cookie))return 0;
        if(!marble_tcm_read(io,r,0x5000+i*4,&v) || v!=snapshot[i])return 0;
    }
    if(io->guard(io->cookie))return 0;
    r->restored=1;r->retain=0;r->phase=7;r->failure_offset=0;
    return r->loaded && work_ok;
}
static int marble_tcm_transaction(const MarbleTcmIo *io,const MarbleTcmPlan *p,
    const uint8_t *firmware,size_t fwbytes,unsigned stage,uint32_t *snapshot,MarbleTcmResult *r)
{return marble_tcm_transaction_work(io,p,firmware,fwbytes,stage,snapshot,r,0,0);}
#endif
