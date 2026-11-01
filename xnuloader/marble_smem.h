/* Bounded, read-only parser for Qualcomm's SMEM partition format. The caller
 * supplies a snapshot, never a writable hardware mapping. No item payload is
 * interpreted here. Layout reference: the pinned MiCode smem.c and the existing
 * redwood_adsp.c; this parser additionally checks every cursor and subtraction. */
#ifndef MARBLE_SMEM_H
#define MARBLE_SMEM_H
typedef struct {
    unsigned host0,host1,offset,size,cacheline;
} MarbleSmemPartition;
typedef void (*MarbleSmemVisit)(void *,const MarbleSmemPartition *,
                               unsigned,unsigned,unsigned,unsigned);
static unsigned msm_u16(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static uint32_t msm_u32(const uint8_t *p) {return msm_u16(p)|(uint32_t)msm_u16(p+2)<<16;}
static int msm_magic(const uint8_t *p,const char *s)
{return p[0]==(uint8_t)s[0] && p[1]==(uint8_t)s[1] && p[2]==(uint8_t)s[2] && p[3]==(uint8_t)s[3];}
static int msm_span(unsigned at,unsigned n,unsigned end)
{return at<=end && n<=end-at;}

/* On malformed metadata, stop before the affected entry is exposed. A caller
 * must keep the failure even if earlier partitions yielded valid entries. */
static int marble_smem_parse(const uint8_t *data,unsigned bytes,
                             MarbleSmemVisit visit,void *ctx,unsigned *partitions,unsigned *items)
{
    *partitions=*items=0;
    if(bytes<4096)return 0;
    unsigned toc=bytes-4096;
    if(!msm_magic(data+toc,"$TOC") || msm_u32(data+toc+4)!=1)return 0;
    unsigned count=msm_u32(data+toc+8);
    if(count>(4096-32)/48)return 0;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *t=data+toc+32+i*48;
        MarbleSmemPartition p={msm_u16(t+12),msm_u16(t+14),msm_u32(t),msm_u32(t+4),msm_u32(t+16)};
        if(!p.offset && !p.size)continue;
        if(!p.offset || p.size<32 || !msm_span(p.offset,p.size,toc))return 0;
        if(p.cacheline && (p.cacheline<16 || p.cacheline>4096 || (p.cacheline&(p.cacheline-1))))return 0;
        const uint8_t *h=data+p.offset;
        if(!msm_magic(h,"$PRT") || msm_u16(h+4)!=p.host0 || msm_u16(h+6)!=p.host1 || msm_u32(h+8)!=p.size)return 0;
        unsigned uncached=msm_u32(h+12),cached=msm_u32(h+16);
        if(uncached<32 || uncached>cached || cached>p.size)return 0;
        (*partitions)++;
        if(visit)visit(ctx,&p,~0u,p.offset,p.size,0);
        unsigned at=32;
        while(at<uncached) {
            if(!msm_span(at,16,uncached))return 0;
            const uint8_t *e=h+at;
            unsigned size=msm_u32(e+4),pd=msm_u16(e+8),ph=msm_u16(e+10);
            if(msm_u16(e)!=0xa5a5 || pd>size || !msm_span(at+16,ph,uncached))return 0;
            unsigned start=at+16+ph;
            if(!size || !msm_span(start,size,uncached) || ++*items>4096)return 0;
            if(visit)visit(ctx,&p,msm_u16(e+2),p.offset+start,size-pd,0);
            at=start+size;
        }
        if(cached==p.size)continue;
        if(!p.cacheline)return 0;
        unsigned step=(16+p.cacheline-1)&~(p.cacheline-1);
        unsigned end=p.size;
        while(end>cached) {
            if(step>end || end-step<cached)return 0;
            unsigned header=end-step;
            const uint8_t *e=h+header;
            unsigned size=msm_u32(e+4),pd=msm_u16(e+8),ph=msm_u16(e+10);
            if(msm_u16(e)!=0xa5a5 || !size || pd>size || ph || size>header-cached || ++*items>4096)return 0;
            unsigned start=header-size;
            if(visit)visit(ctx,&p,msm_u16(e+2),p.offset+start,size-pd,1);
            end=start;
        }
    }
    return 1;
}
#endif
