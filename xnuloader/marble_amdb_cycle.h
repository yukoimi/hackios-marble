/* Pinned AudioReach amdb_api.h: acquire/release one existing TDM library.
 * No module registration, CAPI instance, graph, clocks or amplifier access. */
#ifndef MARBLE_AMDB_CYCLE_H
#define MARBLE_AMDB_CYCLE_H
#include "marble_amdb.h"
#define MARBLE_AMDB_LOAD 0x01001020u
#define MARBLE_AMDB_UNLOAD 0x01001022u
#define MARBLE_AMDB_LOAD_REPLY 0x02001008u
#define MARBLE_AMDB_LOAD_TOKEN 0x4d4c4401u
#define MARBLE_AMDB_UNLOAD_TOKEN 0x4d4c4402u
typedef struct {uint32_t kind,error,client_data;uint64_t handle;} MarbleAmdbCycleReply;
static unsigned marble_amdb_cycle_command(uint8_t *out,unsigned capacity,unsigned unload,uint64_t handle)
{
    if(!out || capacity<64 || unload>1 || (!!handle!=!!unload))return 0;
    for(unsigned i=0;i<64;i++)out[i]=0;
    marble_gpr_put32(out,64u<<8|6u<<4);marble_gpr_put32(out+4,0x302);
    marble_gpr_put32(out+8,3);marble_gpr_put32(out+12,3);
    marble_gpr_put32(out+16,unload?MARBLE_AMDB_UNLOAD_TOKEN:MARBLE_AMDB_LOAD_TOKEN);
    marble_gpr_put32(out+20,unload?MARBLE_AMDB_UNLOAD:MARBLE_AMDB_LOAD);
    marble_gpr_put32(out+36,24);marble_gpr_put32(out+40,2);
    marble_gpr_put32(out+48,1);marble_gpr_put32(out+52,MARBLE_AMDB_QUERY_MODULE);
    marble_gpr_put32(out+56,(uint32_t)handle);marble_gpr_put32(out+60,(uint32_t)(handle>>32));return 64;
}
static int marble_amdb_cycle_reply(const uint8_t *data,unsigned bytes,unsigned unload,MarbleAmdbCycleReply *out)
{
    if(!data || !out || unload>1 || (bytes!=32 && bytes!=48) ||
       marble_gpr_u32(data)!=(bytes<<8|6u<<4) || (marble_gpr_u32(data+4)&0xff00ffffu)!=0x203 ||
       marble_gpr_u32(data+8)!=3 || marble_gpr_u32(data+12)!=3 ||
       marble_gpr_u32(data+16)!=(unload?MARBLE_AMDB_UNLOAD_TOKEN:MARBLE_AMDB_LOAD_TOKEN))return 0;
    MarbleAmdbCycleReply r={0};uint32_t op=marble_gpr_u32(data+20);
    if(bytes==32 && op==MARBLE_GPR_BASIC_REPLY && marble_gpr_u32(data+24)==(unload?MARBLE_AMDB_UNLOAD:MARBLE_AMDB_LOAD)) {
        r.kind=2;r.error=marble_gpr_u32(data+28);r.client_data=data[6];
        /* Some legacy basic replies leave the opaque client byte dirty.
         * Record it; identity, token, opcode and result remain strict. A
         * load basic success still cannot grant an unreturned handle. */
        if(!unload && !r.error)return 0;
    } else if(!unload && bytes==48 && op==MARBLE_AMDB_LOAD_REPLY && marble_gpr_u32(data+4)==0x203 &&
              marble_gpr_u32(data+24)==2 && marble_gpr_u32(data+32)==1 && marble_gpr_u32(data+36)==MARBLE_AMDB_QUERY_MODULE) {
        r.kind=1;r.error=marble_gpr_u32(data+28);
        r.handle=(uint64_t)marble_gpr_u32(data+40)|(uint64_t)marble_gpr_u32(data+44)<<32;
        if(!r.error && !r.handle)return 0;
    } else return 0;
    *out=r;return 1;
}
#endif
