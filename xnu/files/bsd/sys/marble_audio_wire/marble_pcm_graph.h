/* Connect the registered write-shared-memory endpoint to the TDM sink.
 * APM module/calibration layouts are pinned in the local AudioReach audit.
 * This creates no host PCM mapping and writes no amplifier or GPIO. */
#ifndef MARBLE_PCM_GRAPH_H
#define MARBLE_PCM_GRAPH_H
#include "marble_gpr.h"
static unsigned marble_pcm_graph_open(uint8_t *out,unsigned capacity,const uint8_t *base)
{
    static const uint32_t graph[]={
        /* Two modules in the same subgraph/container. */
        1,0x08001002,32,0, 1,0x4001,0x5001,2,0x0700100e,0x6001,0x07001000,0x6002,
        /* Source: zero inputs/one output; sink: one input/zero outputs. */
        1,0x08001003,56,0, 2,
        0x6002,1,0x08001015,8,0,1,
        0x6001,1,0x08001015,8,1,0,0,
        /* Output port1 to input port2. No feedback/control links. */
        1,0x08001004,24,0, 1,0x6002,1,0x6001,2,0
    };
    static const uint32_t media[]={
        0x6002,0x0800100c,32,0, /* parameter includes its 32-bit alignment */
        1,0x09001000,18, /* FIXED_POINT, PCM, 18-byte PCM format */
        96000,0x01010020,0x001f0020,0x00020001,0x00000201
    };
    _Static_assert(sizeof(graph)==160,"connected module graph layout");
    _Static_assert(sizeof(media)==48,"aligned stereo source media format");
    if(!out || !base || capacity<528 || marble_gpr_u32(base)!=(432u<<8|0x60))return 0;
    for(unsigned i=0;i<184;i++)out[i]=base[i];
    /* Legacy capability EP=3 selects GC, which supports the host endpoint.
     * PP=1 selects the specialized container and must not be substituted. */
    for(unsigned i=0;i<40;i++)marble_gpr_put32(out+184+4*i,graph[i]);
    for(unsigned i=0;i<136;i++)out[344+i]=base[296+i];
    for(unsigned i=0;i<12;i++)marble_gpr_put32(out+480+4*i,media[i]);
    marble_gpr_put32(out,528u<<8|0x60);marble_gpr_put32(out+36,488);return 528;
}
#endif
