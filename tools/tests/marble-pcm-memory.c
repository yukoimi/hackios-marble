#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_pcm_memory.h"
int main(void)
{
    uint8_t p[64],copy[64];memset(p,0xa5,sizeof(p));
    assert(marble_pcm_memory_command(p,44,0,0)==44);
    assert(marble_gpr_u32(p)==0x2c60 && marble_gpr_u32(p+20)==0x0100100c && marble_gpr_u32(p+24)==0x10003 && !marble_gpr_u32(p+28));
    assert(marble_gpr_u32(p+32)==0x10000000 && marble_gpr_u32(p+36)==1 && marble_gpr_u32(p+40)==4096 && p[44]==0xa5);
    assert(!marble_pcm_memory_command(p,43,0,0) && !marble_pcm_memory_command(p,64,0,1) && !marble_pcm_memory_command(p,64,1,0));
    assert(marble_pcm_memory_command(p,28,1,0x80001234)==28 && marble_gpr_u32(p+24)==0x80001234);
    memset(p,0,64);marble_gpr_put32(p,0x1c60);marble_gpr_put32(p+4,0x600203);marble_gpr_put32(p+8,1);marble_gpr_put32(p+12,3);marble_gpr_put32(p+16,MARBLE_PCM_MEMORY_TOKEN);marble_gpr_put32(p+20,0x02001001);marble_gpr_put32(p+24,0x80001234);
    MarblePcmMemoryReply r={0};assert(marble_pcm_memory_reply(p,28,0,&r) && r.kind==1 && r.handle==0x80001234 && r.client==0x60);
    memcpy(copy,p,64);for(unsigned i=0;i<28;i++)if(i!=6) {p[i]^=1;assert(!marble_pcm_memory_reply(p,28,0,&r) || (i>=24 && r.handle));memcpy(p,copy,64);}
    marble_gpr_put32(p+24,0);assert(!marble_pcm_memory_reply(p,28,0,&r));
    marble_gpr_put32(p,0x2060);marble_gpr_put32(p+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(p+24,0x0100100c);assert(!marble_pcm_memory_reply(p,32,0,&r));
    marble_gpr_put32(p+28,1);assert(marble_pcm_memory_reply(p,32,0,&r) && r.error==1);
    marble_gpr_put32(p+16,MARBLE_PCM_MEMORY_TOKEN+1);marble_gpr_put32(p+24,0x0100100d);marble_gpr_put32(p+28,0);assert(marble_pcm_memory_reply(p,32,1,&r) && r.kind==2 && !r.error);
    puts("PASS canonical MAP44/UNMAP28, pool/SID/IOVA bounds, opaque typed handle, error and retirement reply identities");
}
