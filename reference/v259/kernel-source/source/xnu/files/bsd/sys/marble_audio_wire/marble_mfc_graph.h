/* AudioReach WREP -> MFC -> TDM wire builder. The module and output-format
 * layouts are pinned in out/audio-format-reuse-v200-audit. This is an
 * unqualified graph candidate: constructing a packet does not start a DSP
 * or establish that this firmware can instantiate its MFC module.
 * Input remains the existing iOS sink's 48kHz/S16/stereo contract; the TDM
 * output retains the board-qualified 96kHz/S32/stereo clock and framing. */
#ifndef MARBLE_MFC_GRAPH_H
#define MARBLE_MFC_GRAPH_H
#include "marble_pcm_graph.h"
#define MARBLE_MFC_GRAPH_BYTES 608u
static unsigned marble_mfc_graph_open(uint8_t *out,unsigned capacity,const uint8_t *base)
{
    static const uint32_t graph[]={
        1,0x08001002,40,0, 1,0x4001,0x5001,3,
        0x0700100e,0x6001,0x07001000,0x6002,0x07001015,0x6003,
        1,0x08001003,80,0, 3,
        0x6002,1,0x08001015,8,0,1,
        0x6003,1,0x08001015,8,1,1,
        0x6001,1,0x08001015,8,1,0,0,
        1,0x08001004,40,0, 2,0x6002,1,0x6003,2,0x6003,1,0x6001,2,0
    };
    static const uint32_t output[]={
        0x6003,0x08001024,16,0, 96000,0x00020020,0x00020001,0
    };
    _Static_assert(sizeof(graph)==208,"three-module APM graph");
    _Static_assert(sizeof(output)==32,"MFC output media format with alignment");
    if(!out || !base || capacity<MARBLE_MFC_GRAPH_BYTES)return 0;
    uint8_t old[528];
    if(marble_pcm_graph_open(old,sizeof(old),base)!=sizeof(old))return 0;
    for(unsigned i=0;i<184;i++)out[i]=old[i];
    for(unsigned i=0;i<52;i++)marble_gpr_put32(out+184+4*i,graph[i]);
    for(unsigned i=0;i<136;i++)out[392+i]=old[344+i];
    for(unsigned i=0;i<48;i++)out[528+i]=old[480+i];
    marble_gpr_put32(out+556,48000);
    marble_gpr_put32(out+560,0x01010010); /* width16, LSB alignment, interleaved */
    marble_gpr_put32(out+564,0x000f0010); /* 16-bit storage, Q15 */
    for(unsigned i=0;i<8;i++)marble_gpr_put32(out+576+4*i,output[i]);
    marble_gpr_put32(out,MARBLE_MFC_GRAPH_BYTES<<8|0x60);
    marble_gpr_put32(out+36,MARBLE_MFC_GRAPH_BYTES-40);
    return MARBLE_MFC_GRAPH_BYTES;
}
#endif
