/* Marble-only, opt-in EPSS request lease. Protocol: pinned MiCode
 * qcom-cpufreq-hw.c and marble DT (see cpufreq-reference/sources.json).
 * Hardware owns voltage/PLL sequencing. Never write LUT, PMIC, clocks or
 * domain0 (the resident EFI/USB core). This limits requests, not measured Hz. */
#ifndef XNULOADER_MARBLE_CPUCAP_H
#define XNULOADER_MARBLE_CPUCAP_H
#include <stdint.h>

typedef int (*MarbleCPURead)(void *,uint64_t,uint32_t *);
typedef int (*MarbleCPUWrite)(void *,uint64_t,uint32_t);
typedef struct {
    uint32_t saved[2], mirror[2], row[2], khz[2], changed;
    unsigned prepared, active, dirty;
} MarbleCPUCap;

static uint64_t marble_cpucap_base(unsigned d) { return 0x17d92000ULL+d*0x1000; }
static uint64_t marble_cpucap_mirror(unsigned d) { return 0x17d09808ULL+d*4; }
static uint32_t marble_cpucap_khz(uint32_t word)
{ return (word>>30)?19200*(word&255):300000; }

/* Validate the complete normal OPP sequence observed in Android, termination,
 * selected core-count qualification (DT max_cores=4), and inherited row.
 * Both domains must validate before the first write. No upward requests. */
static int marble_cpucap_prepare(MarbleCPURead read,void *ctx,MarbleCPUCap *p)
{
    static const uint32_t big[]={633600,768000,883200,998400,1113600,1209600,
        1324800,1440000,1555200,1651200,1766400,1881600,1996800,2112000,
        2227200,2342400,2496000};
    static const uint32_t prime[]={787200,921600,1036800,1171200,1286400,
        1401600,1536000,1651200,1766400,1881600,1996800,2131200,2246400,
        2361600,2476800,2592000,2707200,2822400,2918400};
    if(p->active || p->dirty)return 0;
    p->prepared=0;p->changed=0;
    for(unsigned d=0;d<2;d++) {
        uint64_t base=marble_cpucap_base(d);
        const uint32_t *expected=d?prime:big;
        unsigned n=d?sizeof(prime)/sizeof(prime[0]):sizeof(big)/sizeof(big[0]);
        uint32_t selected=d?3:5,word=0;
        if(read(ctx,base+0x320,&p->saved[d]) || p->saved[d]>=n ||
           read(ctx,marble_cpucap_mirror(d),&p->mirror[d]))return 0;
        for(unsigned i=0;i<=n;i++) {
            if(read(ctx,base+0x100+4*i,&word) ||
               marble_cpucap_khz(word)!=expected[i<n?i:n-1])return 0;
            if(i==selected && ((word>>16)&7)!=4)return 0;
        }
        p->row[d]=p->saved[d]<selected?p->saved[d]:selected;
        if(read(ctx,base+0x100+4*p->row[d],&word) || ((word>>16)&7)!=4)return 0;
        p->khz[d]=marble_cpucap_khz(word);
    }
    p->prepared=1;return 1;
}

/* Reverse write order, verify every restored register. Preserve dirty bits on
 * partial failure so no later run can silently reuse an uncertain lease. */
static int marble_cpucap_restore(MarbleCPURead read,MarbleCPUWrite write,void *ctx,MarbleCPUCap *p)
{
    int ok=1;
    for(int d=1;d>=0;d--) {
        uint32_t bit=1u<<(2*d),value=0;
        if(p->changed&bit) {
            if(write(ctx,marble_cpucap_base((unsigned)d)+0x320,p->saved[d]) ||
               read(ctx,marble_cpucap_base((unsigned)d)+0x320,&value) || value!=p->saved[d])ok=0;
            else p->changed&=~bit;
        }
        if(p->changed&(bit<<1)) {
            if(write(ctx,marble_cpucap_mirror((unsigned)d),p->mirror[d]) ||
               read(ctx,marble_cpucap_mirror((unsigned)d),&value) || value!=p->mirror[d])ok=0;
            else p->changed&=~(bit<<1);
        }
    }
    /* Even an unchanged domain must read back the snapshot. CPU_ON/firmware
     * ownership changes must never be mislabeled as a successful restore. */
    for(unsigned d=0;d<2;d++) {
        uint32_t value=0,bit=1u<<(2*d);
        if(read(ctx,marble_cpucap_base(d)+0x320,&value) || value!=p->saved[d]) { p->changed|=bit;ok=0; }
        if(read(ctx,marble_cpucap_mirror(d),&value) || value!=p->mirror[d]) { p->changed|=bit<<1;ok=0; }
    }
    p->active=0;p->prepared=0;p->dirty=!!p->changed;
    return ok && !p->dirty;
}

static int marble_cpucap_apply(MarbleCPURead read,MarbleCPUWrite write,void *ctx,MarbleCPUCap *p)
{
    if(!p->prepared || p->active || p->dirty)return 0;
    for(unsigned d=0;d<2;d++) {
        uint32_t now=0;
        /* Detect firmware changing ownership since the snapshot, before any
         * write. A lower inherited row remains lower; never force the cap up. */
        if(read(ctx,marble_cpucap_base(d)+0x320,&now) || now!=p->saved[d] ||
           read(ctx,marble_cpucap_mirror(d),&now) || now!=p->mirror[d])return 0;
    }
    for(unsigned d=0;d<2;d++) {
        uint32_t value=0,bit=1u<<(2*d);
        if(p->saved[d]==p->row[d])continue;
        p->changed|=bit<<1;
        if(write(ctx,marble_cpucap_mirror(d),p->row[d]) ||
           read(ctx,marble_cpucap_mirror(d),&value) || value!=p->row[d])goto fail;
        p->changed|=bit;
        if(write(ctx,marble_cpucap_base(d)+0x320,p->row[d]) ||
           read(ctx,marble_cpucap_base(d)+0x320,&value) || value!=p->row[d])goto fail;
    }
    p->active=1;return 1;
fail:
    marble_cpucap_restore(read,write,ctx,p);return 0;
}

static int marble_cpucap_check(MarbleCPURead read,void *ctx,const MarbleCPUCap *p)
{
    if(!p->active || p->dirty)return 0;
    for(unsigned d=0;d<2;d++) {
        uint32_t value;
        if(read(ctx,marble_cpucap_base(d)+0x320,&value) || value!=p->row[d])return 0;
    }
    return 1;
}
#endif
