#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_gpr.h"
int main(void)
{
    static const uint8_t expected[]={0x60,0x18,0,0,2,3,0,0,3,0,0,0,1,0,0,0,1,0x52,0x41,0x4d,0x21,0x10,0,1};
    uint8_t cmd[32],response[512]={0};uint32_t token=0x4d415201;MarbleGprStateReply r,original;
    memset(cmd,0xa5,sizeof(cmd));assert(marble_gpr_state_command(cmd,sizeof(cmd),token)==24);
    assert(!memcmp(cmd,expected,24));assert(cmd[24]==0xa5);
    assert(!marble_gpr_state_command(cmd,23,token));assert(!marble_gpr_state_command(NULL,32,token));assert(!marble_gpr_state_command(cmd,32,0));
    marble_gpr_put32(response,28u<<8|6u<<4);marble_gpr_put32(response+4,0x203);
    marble_gpr_put32(response+8,1);marble_gpr_put32(response+12,3);marble_gpr_put32(response+16,token);
    marble_gpr_put32(response+20,MARBLE_GPR_STATE_REPLY);marble_gpr_put32(response+24,1);
    assert(marble_gpr_state_reply(response,28,token,&r) && r.kind==1 && r.state==1 && !r.error);
    for(unsigned bytes=0;bytes<=512;bytes++)if(bytes!=28)assert(!marble_gpr_state_reply(response,bytes,token,&r));
    for(unsigned offset=0;offset<28;offset++)for(unsigned bit=0;bit<8;bit++) {
        response[offset]^=(uint8_t)(1u<<bit);memset(&r,0xa5,sizeof(r));original=r;
        int valid=offset==24 && bit==0;
        assert(marble_gpr_state_reply(response,28,token,&r)==valid);
        if(!valid)assert(!memcmp(&r,&original,sizeof(r)));
        response[offset]^=(uint8_t)(1u<<bit);
    }
    assert(!marble_gpr_state_reply(response,28,token+1,&r));assert(!marble_gpr_state_reply(NULL,28,token,&r));assert(!marble_gpr_state_reply(response,28,token,NULL));
    marble_gpr_put32(response,32u<<8|6u<<4);marble_gpr_put32(response+20,MARBLE_GPR_BASIC_REPLY);
    marble_gpr_put32(response+24,MARBLE_GPR_GET_STATE);marble_gpr_put32(response+28,1);
    assert(marble_gpr_state_reply(response,32,token,&r) && r.kind==2 && r.error==1 && !r.state);
    marble_gpr_put32(response+24,0x01001013);assert(!marble_gpr_state_reply(response,32,token,&r));
    puts("PASS GPR: exact six-word read-only SPF command, all header bit mutations, length/domain/port/token/opcode binding, ready vs basic error, no output mutation on failure");
}
