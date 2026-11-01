/* GPRv0 six-word header and the read-only SPF state command. Layout/opcodes
 * are pinned in MiCode audio-kernel 7d174adf gpr-lite.h and spf-core.c.
 * No APR, close-all, graph, audio, clock, memory-map or calibration command. */
#ifndef MARBLE_GPR_H
#define MARBLE_GPR_H
#include <stdint.h>
#define MARBLE_GPR_GET_STATE 0x01001021u
#define MARBLE_GPR_STATE_REPLY 0x02001007u
#define MARBLE_GPR_BASIC_REPLY 0x02001005u
typedef struct {uint32_t kind,state,error;} MarbleGprStateReply;
static uint32_t marble_gpr_u32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void marble_gpr_put32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static unsigned marble_gpr_state_command(uint8_t *out,unsigned capacity,uint32_t token)
{
    if(!out || capacity<24 || !token)return 0;
    for(unsigned i=0;i<24;i++)out[i]=0;
    marble_gpr_put32(out,24u<<8|6u<<4);out[4]=2;out[5]=3;
    marble_gpr_put32(out+8,3);marble_gpr_put32(out+12,1);
    marble_gpr_put32(out+16,token);marble_gpr_put32(out+20,MARBLE_GPR_GET_STATE);
    return 24;
}
/* kind=1: a state reply (0 not ready, 1 ready); kind=2: a basic command
 * result, which alone cannot establish SPF readiness. Invalid input leaves
 * the result untouched. Complete GLINK packets only; no fragment reads here. */
static int marble_gpr_state_reply(const uint8_t *data,unsigned bytes,uint32_t token,MarbleGprStateReply *out)
{
    if(!data || !out || !token || (bytes!=28 && bytes!=32) ||
       marble_gpr_u32(data)!=(bytes<<8|6u<<4) || marble_gpr_u32(data+4)!=0x203 ||
       marble_gpr_u32(data+8)!=1 || marble_gpr_u32(data+12)!=3 || marble_gpr_u32(data+16)!=token)return 0;
    uint32_t opcode=marble_gpr_u32(data+20);MarbleGprStateReply result={0};
    if(opcode==MARBLE_GPR_STATE_REPLY && bytes==28) {
        result.state=marble_gpr_u32(data+24);if(result.state>1)return 0;result.kind=1;
    } else if(opcode==MARBLE_GPR_BASIC_REPLY && bytes==32 && marble_gpr_u32(data+24)==MARBLE_GPR_GET_STATE) {
        result.kind=2;result.error=marble_gpr_u32(data+28);
    } else return 0;
    *out=result;return 1;
}
#endif
