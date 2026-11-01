/* One disconnected TDM sink: GRAPH_OPEN followed by GRAPH_CLOSE only.
 * Linux audioreach.c and pinned AudioReach APM APIs define each payload.
 * No PREPARE/START, SET_CFG, mapping, PCM buffers, clocks, pins or amp writes.
 * Container construction may run module initialization inside the DSP. */
#ifndef MARBLE_TDM_GRAPH_H
#define MARBLE_TDM_GRAPH_H
#include "marble_gpr.h"
#define MARBLE_TDM_GRAPH_OPEN_TOKEN 0x4d475201u
#define MARBLE_TDM_GRAPH_CLOSE_TOKEN 0x4d475202u
#define MARBLE_TDM_GRAPH_ID 0x4001u
typedef struct {uint32_t error,client_data;} MarbleTdmGraphReply;
static unsigned marble_tdm_graph_command(uint8_t *out,unsigned capacity,unsigned close)
{
    static const uint32_t open_payload[]={
        /* One RX/playback low-latency subgraph. */
        1,0x08001001,48,0, 1,0x4001,3,
        0x0800100e,4,2, 0x0800100f,4,2, 0x08001010,4,1,
        /* One legacy endpoint-capability container (generic), device side. */
        1,0x08001000,64,0, 1,0x5001,4,
        0x08001011,8,1,3, 0x08001012,4,4,
        0x08001013,4,8192, 0x08001014,4,2,
        /* One already-registered TDM sink, private module instance. */
        1,0x08001002,24,0, 1,0x4001,0x5001,1,0x0700100e,0x6001,
        /* One input port, no output port, no connections. */
        1,0x08001003,32,0, 1,0x6001,1,0x08001015,8,1,0,0,
        1,0x08001004,8,0, 0,0
    };
    static const uint32_t close_payload[]={1,0x08001005,8,0,1,0x4001};
    _Static_assert(sizeof(open_payload)==256,"APM graph open layout");
    _Static_assert(sizeof(close_payload)==24,"APM graph close layout");
    unsigned payload=close?24:256,bytes=40+payload;
    if(!out || close>1 || capacity<bytes)return 0;
    for(unsigned i=0;i<bytes;i++)out[i]=0;
    marble_gpr_put32(out,bytes<<8|0x60);marble_gpr_put32(out+4,0x302);
    marble_gpr_put32(out+8,3);marble_gpr_put32(out+12,1);
    marble_gpr_put32(out+16,close?MARBLE_TDM_GRAPH_CLOSE_TOKEN:MARBLE_TDM_GRAPH_OPEN_TOKEN);
    marble_gpr_put32(out+20,close?0x01001004:0x01001000);
    marble_gpr_put32(out+36,payload);
    const uint32_t *words=close?close_payload:open_payload;
    for(unsigned i=0;i<payload/4;i++)marble_gpr_put32(out+40+4*i,words[i]);
    return bytes;
}
static int marble_tdm_graph_reply(const uint8_t *data,unsigned bytes,unsigned close,MarbleTdmGraphReply *out)
{
    if(!data || !out || bytes!=32 || close>1 || marble_gpr_u32(data)!=0x2060 ||
       (marble_gpr_u32(data+4)&0xff00ffffu)!=0x203 || marble_gpr_u32(data+8)!=1 || marble_gpr_u32(data+12)!=3 ||
       marble_gpr_u32(data+16)!=(close?MARBLE_TDM_GRAPH_CLOSE_TOKEN:MARBLE_TDM_GRAPH_OPEN_TOKEN) ||
       marble_gpr_u32(data+20)!=MARBLE_GPR_BASIC_REPLY || marble_gpr_u32(data+24)!=(close?0x01001004u:0x01001000u))return 0;
    /* Basic replies from this DSP leave client_data dirty; retain the warning.
     * It never participates in routing, result interpretation or IDs. */
    MarbleTdmGraphReply r={marble_gpr_u32(data+28),data[6]};*out=r;return 1;
}
#endif
