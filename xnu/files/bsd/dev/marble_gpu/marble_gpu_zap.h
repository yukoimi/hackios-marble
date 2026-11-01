/* Marble PAS13 lifecycle. Protocol follows locked Xiaomi qcom_scm.c and
 * mdt_loader.c; DT GPU microcode carveout 0x8b91a000/0x2000. The authenticated
 * a730 image's only PT_LOAD is b01 (2236 bytes), not the b02 hash segment.
 * A failed SCM call is NEVER interpreted as evidence of an already loaded ZAP. */
#ifndef MARBLE_GPU_ZAP_H
#define MARBLE_GPU_ZAP_H
#include <stdint.h>
#define MARBLE_ZAP_ABI "MGPU-MARBLE-ZAP-v1"
#define MARBLE_ZAP_PHYS UINT64_C(0x8b91a000)
#define MARBLE_ZAP_BYTES 0x1000u
typedef struct {
    void *cookie;
    int (*scm)(void *,uint32_t,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t *);
    int (*copy)(void *,uint64_t,unsigned);
    int (*health)(void *);
} MarbleZapIo;
typedef struct {
    unsigned available,old_unloaded,attempted,initialized,memory_ready,copied,authenticated,unloaded,retain,phase;
    int status;uint64_t answer;
} MarbleZapResult;
static int marble_zap_call(const MarbleZapIo *io,MarbleZapResult *r,uint32_t command,
    uint32_t args,uint64_t a,uint64_t b,uint64_t c,uint64_t want)
{
    r->answer=UINT64_MAX;
    r->status=io->scm(io->cookie,command,args,a,b,c,&r->answer);
    return !r->status && r->answer==want;
}
static int marble_zap_transaction(const MarbleZapIo *io,uint64_t metadata,MarbleZapResult *r)
{
    if(!r)return 0;*r=(MarbleZapResult){0};
    if(!io || !io->scm || !io->copy || !io->health || !metadata ||
        (metadata&0x3fff) || metadata>=(UINT64_C(1)<<36) || io->health(io->cookie))return 0;
    static const uint32_t commands[4]={0x201,0x202,0x205,0x206};
    for(unsigned i=0;i<4;i++) {
        if(!marble_zap_call(io,r,0x601,1,0x02000000|commands[i],0,0,1))return 0;
        r->available++;
    }
    r->phase=1;
    r->old_unloaded=marble_zap_call(io,r,0x206,1,13,0,0,0);
    /* Stock Linux starts with INIT_IMAGE; an initial SHUTDOWN can be refused
     * when nothing was loaded. Its failure proves neither state. Continue
     * only to INIT, and permit no carveout access without fresh successful
     * INIT + MEM_SETUP. Never reuse an image inferred from an error code. */
    if(io->health(io->cookie))return 0;
    r->attempted=1;r->retain=1;r->phase=2;
    if(!marble_zap_call(io,r,0x201,0x82,13,metadata,0,0))return 0;
    r->initialized=1;r->phase=3;
    if(io->health(io->cookie) || !marble_zap_call(io,r,0x202,3,13,MARBLE_ZAP_PHYS,MARBLE_ZAP_BYTES,0))return 0;
    r->memory_ready=1;r->phase=4;
    if(io->health(io->cookie) || io->copy(io->cookie,MARBLE_ZAP_PHYS,MARBLE_ZAP_BYTES))return 0;
    r->copied=1;r->phase=5;
    if(io->health(io->cookie) || !marble_zap_call(io,r,0x205,1,13,0,0,0))return 0;
    r->authenticated=1;
    return !io->health(io->cookie);
}
/* As in the existing board driver, unload only after GX is stopped. An
 * indeterminate INIT/AUTH also retains this lease until the same stop. */
static int marble_zap_shutdown(const MarbleZapIo *io,MarbleZapResult *r,unsigned drained_collapsed)
{
    if(!r->attempted)return 1;
    if(!drained_collapsed || !r->retain)return 0;
    r->phase=6;
    if(!marble_zap_call(io,r,0x206,1,13,0,0,0))return 0;
    r->unloaded=1;r->retain=0;r->phase=7;
    return 1;
}
#endif
