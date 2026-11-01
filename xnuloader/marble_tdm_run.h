/* Silent, disconnected TDM lifecycle. The pins stay GPIO inputs and both
 * amplifiers stay reset. No sample buffers or physical speaker output. */
#ifndef MARBLE_TDM_RUN_H
#define MARBLE_TDM_RUN_H
#include "marble_tdm_config.h"
#include "marble_audio_clock.h"
#define MARBLE_TDM_RUN_TOKEN 0x4d545201u
#ifdef XNU_MARBLE_TDM_PCM_GRAPH_ONLY
#ifdef XNU_MARBLE_AUDIO_MFC
#include "marble_mfc_graph.h"
#define MARBLE_TDM_RUN_OPEN_BYTES MARBLE_MFC_GRAPH_BYTES
#else
#include "marble_pcm_graph.h"
#define MARBLE_TDM_RUN_OPEN_BYTES 528u
#endif
#else
#define MARBLE_TDM_RUN_OPEN_BYTES 432u
#endif
static unsigned marble_tdm_run_base_command(uint8_t *out,unsigned capacity,unsigned stage)
{
    if(stage>4 || !out || capacity<(stage?64u:432u))return 0;
    unsigned n=marble_tdm_config_command(out,capacity,stage!=0);
    if(!stage) {
        static const uint32_t clk[]={0x6001,0x0800113c,16,0,0x204,12288000,4,0};
        if(n!=400)return 0;
        for(unsigned i=0;i<8;i++)marble_gpr_put32(out+400+4*i,clk[i]);
        n=432;marble_gpr_put32(out,n<<8|0x60);marble_gpr_put32(out+36,n-40);
    }
    marble_gpr_put32(out+16,MARBLE_TDM_RUN_TOKEN+stage);
    marble_gpr_put32(out+20,0x01001000+stage);return n;
}
static unsigned marble_tdm_run_command(uint8_t *out,unsigned capacity,unsigned stage)
{
#ifdef XNU_MARBLE_TDM_PCM_GRAPH_ONLY
    if(!stage) {
        uint8_t base[432];
        if(marble_tdm_run_base_command(base,sizeof(base),0)!=sizeof(base))return 0;
#ifdef XNU_MARBLE_AUDIO_MFC
        return marble_mfc_graph_open(out,capacity,base);
#else
        return marble_pcm_graph_open(out,capacity,base);
#endif
    }
#endif
    return marble_tdm_run_base_command(out,capacity,stage);
}
static int marble_tdm_run_reply(const uint8_t *data,unsigned bytes,unsigned stage,MarbleTdmGraphReply *out)
{
    if(!data || !out || stage>4 || bytes!=32 || marble_gpr_u32(data)!=0x2060 ||
       (marble_gpr_u32(data+4)&0xff00ffffu)!=0x203 || marble_gpr_u32(data+8)!=1 ||
       marble_gpr_u32(data+12)!=3 || marble_gpr_u32(data+16)!=MARBLE_TDM_RUN_TOKEN+stage ||
       marble_gpr_u32(data+20)!=MARBLE_GPR_BASIC_REPLY || marble_gpr_u32(data+24)!=0x01001000+stage)return 0;
    *out=(MarbleTdmGraphReply){marble_gpr_u32(data+28),data[6]};return 1;
}
#endif
