#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_pcm_data.h"
int main(void)
{
    uint8_t p[80];memset(p,0xa5,sizeof(p));uint32_t handle=0x80001234;
    assert(marble_pcm_data_command(p,68,handle)==68 && p[68]==0xa5);
    assert(marble_gpr_u32(p)==0x4460 && marble_gpr_u32(p+12)==0x6002 && marble_gpr_u32(p+20)==0x0400100a);
    assert(marble_gpr_u32(p+24)==0x10000000 && marble_gpr_u32(p+28)==1 && marble_gpr_u32(p+32)==handle && marble_gpr_u32(p+36)==4096);
    for(unsigned i=40;i<68;i++)assert(!p[i]);
    assert(!marble_pcm_data_command(p,67,handle) && !marble_pcm_data_command(p,80,0));
    memset(p,0,80);marble_gpr_put32(p,0x3860);marble_gpr_put32(p+4,0x600203);marble_gpr_put32(p+8,0x6002);marble_gpr_put32(p+12,3);marble_gpr_put32(p+16,MARBLE_PCM_DATA_TOKEN);marble_gpr_put32(p+20,0x05001004);marble_gpr_put32(p+24,0x10000000);marble_gpr_put32(p+28,1);marble_gpr_put32(p+32,handle);
    MarblePcmDataReply r={0};assert(marble_pcm_data_reply(p,56,handle,&r) && r.kind==1 && !r.error && !r.md_error);
    uint8_t copy[80];memcpy(copy,p,80);
    for(unsigned i=0;i<56;i++)if(i!=6 && !(i>=36 && i<40) && i<52) {p[i]^=1;assert(!marble_pcm_data_reply(p,56,handle,&r));memcpy(p,copy,80);}
    marble_gpr_put32(p+36,1);assert(marble_pcm_data_reply(p,56,handle,&r) && r.error==1);
    marble_gpr_put32(p,0x2060);marble_gpr_put32(p+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(p+24,0x0400100a);marble_gpr_put32(p+28,1);assert(marble_pcm_data_reply(p,32,handle,&r) && r.kind==2);
    marble_gpr_put32(p+28,0);assert(!marble_pcm_data_reply(p,32,handle,&r));
    puts("PASS WRITE68/DONE56: exact source/endpoint/token/SID/opaque handle, metadata absent, typed nonzero errors, no basic success substitute");
}
