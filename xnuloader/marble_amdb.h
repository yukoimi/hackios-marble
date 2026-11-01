/* One bounded AMDB GET_CFG query for TDM sink 0700100e. Pinned AudioReach
 * graphservices 558ddfc2 / engine b38cb85a APIs + implementation under
 * out/audioreach-tdm-v121-audit. No graph/register/load/unload/audio commands.
 * AMDB may internally inspect a library; no CAPI instance is created here. */
#ifndef MARBLE_AMDB_H
#define MARBLE_AMDB_H
#include "marble_gpr.h"
#define MARBLE_AMDB_GET_CFG 0x01001007u
#define MARBLE_AMDB_CFG_REPLY 0x02001000u
#define MARBLE_AMDB_VERSION_PARAM 0x080013d6u
#define MARBLE_AMDB_TDM_SINK 0x0700100eu
#ifdef XNU_MARBLE_AMDB_MFC
#define MARBLE_AMDB_QUERY_MODULE 0x07001015u
#else
#define MARBLE_AMDB_QUERY_MODULE MARBLE_AMDB_TDM_SINK
#endif
typedef struct {
    uint32_t kind,error,param_error,module_error,present,major,minor,client_data;
    uint16_t year,month,day,hour,minute,second;
} MarbleAmdbReply;
static unsigned marble_amdb_command(uint8_t *out,unsigned capacity,uint32_t token)
{
    if(!out || capacity<96 || !token)return 0;
    for(unsigned i=0;i<96;i++)out[i]=0;
    marble_gpr_put32(out,96u<<8|6u<<4);out[4]=2;out[5]=3;
    marble_gpr_put32(out+8,3);marble_gpr_put32(out+12,3);
    marble_gpr_put32(out+16,token);marble_gpr_put32(out+20,MARBLE_AMDB_GET_CFG);
    /* In-band command header: no address or mapping handle. */
    marble_gpr_put32(out+36,56);
    marble_gpr_put32(out+40,3);marble_gpr_put32(out+44,MARBLE_AMDB_VERSION_PARAM);
    marble_gpr_put32(out+48,40);marble_gpr_put32(out+56,2);marble_gpr_put32(out+60,1);
    marble_gpr_put32(out+64,MARBLE_AMDB_QUERY_MODULE);return 96;
}
static int marble_amdb_reply(const uint8_t *data,unsigned bytes,uint32_t token,MarbleAmdbReply *out)
{
    if(!data || !out || !token || (bytes!=84 && bytes!=32) ||
       marble_gpr_u32(data)!=(bytes<<8|6u<<4) || (marble_gpr_u32(data+4)&0xff00ffffu)!=0x203 ||
       marble_gpr_u32(data+8)!=3 || marble_gpr_u32(data+12)!=3 || marble_gpr_u32(data+16)!=token)return 0;
    MarbleAmdbReply r={0};uint32_t opcode=marble_gpr_u32(data+20);
    if(bytes==32 && opcode==MARBLE_GPR_BASIC_REPLY && marble_gpr_u32(data+24)==MARBLE_AMDB_GET_CFG) {
        /* Legacy DSP basic-error replies carry allocator residue in client_data
         * (v124: a8). Preserve it as an explicit protocol warning. This
         * compatibility applies only to a nonzero basic error and can never
         * establish module metadata/presence. Domains/reserved top byte,
         * ports, length, token and original opcode remain exact. */
        r.kind=2;r.error=marble_gpr_u32(data+28);r.client_data=data[6];
        if(r.client_data && !r.error)return 0;
    } else if(bytes==84 && opcode==MARBLE_AMDB_CFG_REPLY) {
        if(marble_gpr_u32(data+4)!=0x203 || marble_gpr_u32(data+28)!=3 || marble_gpr_u32(data+32)!=MARBLE_AMDB_VERSION_PARAM ||
           marble_gpr_u32(data+36)!=40 || marble_gpr_u32(data+44)!=2 || marble_gpr_u32(data+48)!=1 ||
           marble_gpr_u32(data+52)!=MARBLE_AMDB_QUERY_MODULE)return 0;
        r.kind=1;r.error=marble_gpr_u32(data+24);r.param_error=marble_gpr_u32(data+40);
        r.module_error=marble_gpr_u32(data+56);r.present=marble_gpr_u32(data+60);
        r.major=marble_gpr_u32(data+64);r.minor=marble_gpr_u32(data+68);
        if(r.present>1)return 0;
        uint16_t ts[6];
        for(unsigned i=0;i<6;i++)ts[i]=(uint16_t)(data[72+2*i]|data[73+2*i]<<8);
        r.year=ts[0];r.month=ts[1];r.day=ts[2];r.hour=ts[3];r.minute=ts[4];r.second=ts[5];
        if(r.month>12 || r.day>31 || r.hour>23 || r.minute>59 || r.second>60)return 0;
    } else return 0;
    *out=r;return 1;
}
#endif
