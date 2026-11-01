#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_smem.h"
static uint8_t data[16384],good[16384];
static unsigned calls;
static void w16(unsigned at,unsigned n){data[at]=n;data[at+1]=n>>8;}
static void w32(unsigned at,uint32_t n){w16(at,n);w16(at+2,n>>16);}
static void visit(void *ctx,const MarbleSmemPartition *p,unsigned item,unsigned at,unsigned n,unsigned cached)
{
    (void)ctx;(void)cached;
    assert(p->offset<=at && at<=p->offset+p->size && n<=p->offset+p->size-at);
    assert(item==~0u || item<=65535);calls++;
}
static int parse(void){unsigned partitions,items;return marble_smem_parse(data,sizeof(data),visit,0,&partitions,&items);}
static void reset(void){memcpy(data,good,sizeof(data));calls=0;}
int main(void)
{
    unsigned toc=sizeof(data)-4096,t=toc+32,p=4096;
    memcpy(data+toc,"$TOC",4);w32(toc+4,1);w32(toc+8,1);
    w32(t,p);w32(t+4,4096);w16(t+12,0);w16(t+14,2);w32(t+16,64);
    memcpy(data+p,"$PRT",4);w16(p+6,2);w32(p+8,4096);w32(p+12,64);w32(p+16,4096);
    w16(p+32,0xa5a5);w16(p+34,443);w32(p+36,16);
    memcpy(good,data,sizeof(data));assert(parse() && calls==2);
    reset();w16(p+40,17);assert(!parse()); /* payload padding underflow */
    reset();w32(p+36,0xffffffff);assert(!parse());
    reset();w16(p+42,65535);assert(!parse());
    reset();w32(p+12,63);assert(!parse());
    reset();w32(p+16,63);assert(!parse());
    reset();w32(toc+8,0xffffffff);assert(!parse());
    reset();w32(t,0xfffffff0);assert(!parse());
    reset();w16(p+6,3);assert(!parse());
    reset();w32(t+16,3);assert(!parse());
    reset();w32(p+16,3968);w16(p+4032,0xa5a5);w16(p+4034,429);w32(p+4036,64);
    assert(parse() && calls==3);
    w32(p+4036,65);assert(!parse()); /* crossing cached frontier */
    /* Deterministic malformed-metadata corpus. The callback checks every
     * exposed span, including parses that later reject a different header. */
    uint32_t seed=0x7350;
    for(unsigned i=0;i<100000;i++){
        reset();seed=seed*1664525u+1013904223u;
        unsigned at=(seed&1)?t+(seed>>8)%48:p+(seed>>8)%64;
        data[at]^=(uint8_t)(seed>>16);parse();
    }
    puts("SMEM: valid uncached/cached items, malformed boundaries, 100000 mutations PASS");
    return 0;
}
