#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_plan.h"
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_tcm.h"
static uint32_t memory[MARBLE_TCM_WORDS],original[MARBLE_TCM_WORDS],snapshot[MARBLE_TCM_WORDS];
static unsigned reads,writes,guards,barriers,fail_read,fail_write,fail_guard,fail_barrier,after,corrupt_read;
static unsigned stage=10;static int work_status;
static int work(void *c)
{
    (void)c;
    for(unsigned i=0;i<MARBLE_TCM_WORDS;i++)memory[i]^=0x01020304;
    return work_status;
}
static int rd(void *c,unsigned offset,uint32_t *v)
{
    (void)c;assert(marble_tcm_read_allowed(offset,4));reads++;
    if(reads==fail_read)return -1;
    *v=memory[(offset-0x5000)/4];if(reads==corrupt_read)*v^=1;return 0;
}
static int wr(void *c,unsigned offset,uint32_t v)
{
    (void)c;assert(marble_tcm_write_allowed(offset,4,stage,1));writes++;
    if(writes==fail_write && !after)return -1;
    memory[(offset-0x5000)/4]=v;return writes==fail_write?-1:0;
}
static int gd(void *c) {(void)c;return ++guards==fail_guard?-1:0;}
static int br(void *c) {(void)c;return ++barriers==fail_barrier?-1:0;}
static void reset(void)
{
    for(unsigned i=0;i<MARBLE_TCM_WORDS;i++)original[i]=0xa73942cd^((i*17)<<2);
    memcpy(memory,original,sizeof(memory));reads=writes=guards=barriers=0;
    fail_read=fail_write=fail_guard=fail_barrier=after=corrupt_read=0;
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t *fw=malloc(57272);assert(fw);
    assert(fread(fw,1,57272,f)==57272);fclose(f);
    AdrenoFirmwareBlock blocks[32],copy[32];size_t count;MarbleTcmPlan plan;
    assert(AdrenoFirmwarePlanGmu(fw,57272,marble_gmu_regions,5,blocks,32,&count)==AdrenoFirmwareSuccess);
    assert(marble_tcm_plan(blocks,count,57272,&plan) && plan.words==135 && plan.count==3);
    for(size_t i=0;i<count;i++)if(blocks[i].bytes && (blocks[i].region_identifier==0 || blocks[i].region_identifier==2)) {
        memcpy(copy,blocks,sizeof(copy));copy[i].address^=4;assert(!marble_tcm_plan(copy,count,57272,&plan));
        memcpy(copy,blocks,sizeof(copy));copy[i].bytes+=4;assert(!marble_tcm_plan(copy,count,57272,&plan));
        memcpy(copy,blocks,sizeof(copy));copy[i].payload_offset=57272;assert(!marble_tcm_plan(copy,count,57272,&plan));
    }
    assert(marble_tcm_plan(blocks,count,57272,&plan));
    for(unsigned at=0;at<0x37000;at++)for(unsigned width=0;width<=8;width++) {
        int readable=width==4 && !(at&3) && at>=0x5000 && at<0xd000;
        assert(marble_tcm_read_allowed(at,width)==readable);
        int writable=width==4 && !(at&3) && ((at>=0x5000 && at<0x51dc) ||
            (at>=0xcf98 && at<0xcfc0) || (at>=0xcfdc && at<0xcff4));
        assert(marble_tcm_write_allowed(at,width,10,1)==writable);
        assert(!marble_tcm_write_allowed(at,width,9,1));
        assert(!marble_tcm_write_allowed(at,width,10,0));
        assert(!marble_tcm_write_allowed(at,width,11,1));
    }
    MarbleTcmIo io={NULL,rd,wr,br,gd};MarbleTcmResult r;
    reset();assert(marble_tcm_transaction(&io,&plan,fw,57272,9,snapshot,&r));
    assert(r.stable && !r.loaded && !r.retain && !writes && reads==16384);
    reset();assert(marble_tcm_transaction(&io,&plan,fw,57272,10,snapshot,&r));
    assert(r.stable && r.loaded && r.restored && !r.retain && writes==270 && reads==32768);
    assert(!memcmp(memory,original,sizeof(memory)));
    unsigned guard_count=guards;
    /* Every load/restore word can fail either before or after bus delivery. */
    unsigned cases=0;
    for(unsigned i=1;i<=270;i++)for(unsigned effect=0;effect<2;effect++) {
        reset();fail_write=i;after=effect;
        int ok=marble_tcm_transaction(&io,&plan,fw,57272,10,snapshot,&r);
        if(i<=135)assert(!ok && r.restored && !r.retain);
        else if(effect)assert(ok && r.restored && !r.retain);
        else assert(!ok && !r.restored && r.retain);
        if(!r.retain)assert(!memcmp(memory,original,sizeof(memory)));cases++;
    }
    const unsigned samples[]={1,8192,8193,16384,16385,24576,24577,32768};
    for(unsigned k=0;k<sizeof(samples)/sizeof(samples[0]);k++)for(unsigned corrupt=0;corrupt<2;corrupt++) {
        reset();if(corrupt)corrupt_read=samples[k];else fail_read=samples[k];
        assert(!marble_tcm_transaction(&io,&plan,fw,57272,10,snapshot,&r));
        if(samples[k]<=16384)assert(!writes && !r.retain);
        else if(samples[k]<=24576)assert(r.restored && !r.retain);
        else assert(r.retain);
        if(!r.retain)assert(!memcmp(memory,original,sizeof(memory)));cases++;
    }
    for(unsigned i=1;i<=3;i++) {
        reset();fail_barrier=i;assert(!marble_tcm_transaction(&io,&plan,fw,57272,10,snapshot,&r));
        assert(i==3?r.retain:!r.retain);cases++;
    }
    for(unsigned i=1;i<=guard_count;i++) {
        reset();fail_guard=i;assert(!marble_tcm_transaction(&io,&plan,fw,57272,10,snapshot,&r));
        if(writes)assert(r.retain);else assert(!r.retain);cases++;
    }
    reset();plan.spans[0].offset+=4;
    assert(!marble_tcm_transaction(&io,&plan,fw,57272,10,snapshot,&r) && !reads && !writes);
    assert(marble_tcm_plan(blocks,count,57272,&plan));stage=17;
    reset();assert(marble_tcm_transaction_work(&io,&plan,fw,57272,17,snapshot,&r,work,NULL));
    assert(r.loaded && r.restored && !r.retain && !memcmp(memory,original,sizeof(memory)));
    unsigned lease_writes=writes,lease_reads=reads;
    for(unsigned effect=0;effect<2;effect++)for(unsigned n=1;n<=lease_writes;n++) {
        reset();fail_write=n;after=effect;
        int ok=marble_tcm_transaction_work(&io,&plan,fw,57272,17,snapshot,&r,work,NULL);
        if(n<=135)assert(!ok);
        if(!r.retain)assert(!memcmp(memory,original,sizeof(memory)));
        else assert(!r.restored);cases++;
    }
    for(unsigned n=1;n<=lease_reads;n+=63) {
        reset();fail_read=n;
        assert(!marble_tcm_transaction_work(&io,&plan,fw,57272,17,snapshot,&r,work,NULL));
        if(!r.retain && writes)assert(!memcmp(memory,original,sizeof(memory)));cases++;
    }
    reset();work_status=-1;assert(!marble_tcm_transaction_work(&io,&plan,fw,57272,17,snapshot,&r,work,NULL));
    assert(r.restored && !r.retain && !memcmp(memory,original,sizeof(memory)));
    reset();work_status=-2;assert(!marble_tcm_transaction_work(&io,&plan,fw,57272,17,snapshot,&r,work,NULL));
    assert(r.retain && !r.restored && writes==135);
    printf("TCM fixture PASS: real firmware, exact fences, full-bank stability/load/restore, %u injected failures; no hardware claim\n",cases);
    free(fw);return 0;
}
