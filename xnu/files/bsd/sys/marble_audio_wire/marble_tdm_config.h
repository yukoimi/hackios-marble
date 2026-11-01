/* APM graph-open permits module calibration payloads after graph metadata.
 * Board input: tools/marble-tdm-board.py, actual ROM/runtime FDT and AW ACF.
 * Configure the disconnected sink only; no PREPARE/START, clock ID request,
 * pinctrl, amplifier programming, shared mappings or PCM data. */
#ifndef MARBLE_TDM_CONFIG_H
#define MARBLE_TDM_CONFIG_H
#include "marble_tdm_graph.h"
static unsigned marble_tdm_config_command(uint8_t *out,unsigned capacity,unsigned close)
{
    static const uint32_t params[]={
        /* Tertiary LPAIF: internal sync, data_out=0, active slots0/1,
         * four 32-bit wire slots, long inverted sync, one-bit delay. */
        0x6001,0x0800101b,28,0, 0,2,1,3,0x00200004,0x00010001,1,0,
        /* ROM/ACF endpoint media format: 96kHz, stereo, 32-bit PCM. */
        0x6001,0x08001017,12,0, 96000,0x00020020,1,0,
        /* One-millisecond frame-size factor. */
        0x6001,0x08001018,4,0, 1,0
    };
    _Static_assert(sizeof(params)==104,"TDM calibration layout");
    if(close>1 || !out || capacity<(close?64u:400u))return 0;
    unsigned n=marble_tdm_graph_command(out,capacity,close);
    if(close)return n;
    if(n!=296)return 0;
    for(unsigned i=0;i<sizeof(params)/4;i++)marble_gpr_put32(out+296+4*i,params[i]);
    marble_gpr_put32(out,400u<<8|0x60);marble_gpr_put32(out+36,360);return 400;
}
#endif
