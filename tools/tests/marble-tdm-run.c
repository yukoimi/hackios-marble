#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_tdm_run.h"
int main(void)
{
    uint8_t b[440],r[32]={0};MarbleTdmGraphReply out;
    for(unsigned stage=0;stage<5;stage++) {
        unsigned n=stage?64:432;memset(b,0xa5,sizeof(b));assert(marble_tdm_run_command(b,n,stage)==n);
        assert(marble_gpr_u32(b)==(n<<8|0x60) && marble_gpr_u32(b+16)==MARBLE_TDM_RUN_TOKEN+stage && marble_gpr_u32(b+20)==0x01001000+stage);
        if(!stage) {assert(marble_gpr_u32(b+404)==0x0800113c && marble_gpr_u32(b+408)==16 && marble_gpr_u32(b+416)==0x204 && marble_gpr_u32(b+420)==12288000 && marble_gpr_u32(b+424)==4 && !marble_gpr_u32(b+428));}
        else assert(marble_gpr_u32(b+44)==0x08001005 && marble_gpr_u32(b+60)==0x4001);
        for(unsigned i=n;i<440;i++)assert(b[i]==0xa5);
        assert(!marble_tdm_run_command(b,n-1,stage));
        marble_gpr_put32(r,0x2060);marble_gpr_put32(r+4,0xa80203);marble_gpr_put32(r+8,1);marble_gpr_put32(r+12,3);
        marble_gpr_put32(r+16,MARBLE_TDM_RUN_TOKEN+stage);marble_gpr_put32(r+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(r+24,0x01001000+stage);
        assert(marble_tdm_run_reply(r,32,stage,&out) && !out.error && out.client_data==0xa8);
        for(unsigned i=0;i<32;i++)assert(!marble_tdm_run_reply(r,i,stage,&out));
        for(unsigned w=0;w<7;w++)for(unsigned bit=0;bit<32;bit++) {
            if(w==1 && bit>=16 && bit<24)continue;
            uint8_t bad[32];memcpy(bad,r,32);bad[w*4+bit/8]^=1u<<(bit%8);assert(!marble_tdm_run_reply(bad,32,stage,&out));
        }
    }
    assert(!marble_tdm_run_command(b,440,5) && !marble_tdm_run_reply(r,32,5,&out));
    puts("PASS five TDM lifecycle opcodes/tokens, board clock calibration, bounds and strict replies");
}
