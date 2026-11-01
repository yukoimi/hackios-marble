/* Pinned AudioReach wr_sh_mem_ep_api.h V2 wire format. One zero-filled page,
 * 512 stereo S32 frames at96kHz; no timestamp or metadata. No amplifier. */
#ifndef MARBLE_PCM_DATA_H
#define MARBLE_PCM_DATA_H
#include "marble_gpr.h"
#define MARBLE_PCM_DATA_TOKEN 0x4d504401u
#ifdef XNU_MARBLE_AUDIO_MFC
#define MARBLE_PCM_DATA_BYTES 1024u
#define MARBLE_PCM_DATA_FRAMES 256u
#define MARBLE_PCM_DATA_RATE 48000u
#define MARBLE_PCM_DATA_BITS 16u
#else
#define MARBLE_PCM_DATA_BYTES 4096u
#define MARBLE_PCM_DATA_FRAMES 512u
#define MARBLE_PCM_DATA_RATE 96000u
#define MARBLE_PCM_DATA_BITS 32u
#endif
#ifndef XNU_MARBLE_AUDIO_PCM_PERIODS
#define XNU_MARBLE_AUDIO_PCM_PERIODS 2
#endif
#if XNU_MARBLE_AUDIO_PCM_PERIODS != 2 && XNU_MARBLE_AUDIO_PCM_PERIODS != 8
#error Only the bounded two/eight-period profiles are supported
#endif
#define MARBLE_PCM_REPEAT_COUNT XNU_MARBLE_AUDIO_PCM_PERIODS
#ifndef MARBLE_PCM_REQUEST_LIMIT
#define MARBLE_PCM_REQUEST_LIMIT MARBLE_PCM_REPEAT_COUNT
#endif
#if MARBLE_PCM_REQUEST_LIMIT < 1 || MARBLE_PCM_REQUEST_LIMIT > 2048
#error PCM token range must remain below the MAP/UNMAP token range
#endif
typedef struct {unsigned kind;uint32_t error,md_error,client;} MarblePcmDataReply;
static unsigned marble_pcm_data_command_index(uint8_t *out,unsigned capacity,uint32_t handle,unsigned index)
{
    if(!out || capacity<68 || !handle || index>=MARBLE_PCM_REQUEST_LIMIT)return 0;
    for(unsigned i=0;i<68;i++)out[i]=0;
    marble_gpr_put32(out,0x4460);marble_gpr_put32(out+4,0x302);
    marble_gpr_put32(out+8,3);marble_gpr_put32(out+12,0x6002);
    marble_gpr_put32(out+16,MARBLE_PCM_DATA_TOKEN+index);marble_gpr_put32(out+20,0x0400100a);
    marble_gpr_put32(out+24,0x10000000);marble_gpr_put32(out+28,1);
    marble_gpr_put32(out+32,handle);marble_gpr_put32(out+36,MARBLE_PCM_DATA_BYTES);
    return 68;
}
static int marble_pcm_data_reply_index(const uint8_t *p,unsigned n,uint32_t handle,unsigned index,MarblePcmDataReply *out)
{
    if(!p || !out || !handle || index>=MARBLE_PCM_REQUEST_LIMIT || (n!=56 && n!=32) || marble_gpr_u32(p)!=(n<<8|0x60) ||
       (marble_gpr_u32(p+4)&0xff00ffffu)!=0x203 || marble_gpr_u32(p+8)!=0x6002 ||
       marble_gpr_u32(p+12)!=3 || marble_gpr_u32(p+16)!=MARBLE_PCM_DATA_TOKEN+index)return 0;
    uint32_t op=marble_gpr_u32(p+20);MarblePcmDataReply r={0};r.client=p[6];
    if(n==56 && op==0x05001004 && marble_gpr_u32(p+24)==0x10000000 &&
       marble_gpr_u32(p+28)==1 && marble_gpr_u32(p+32)==handle &&
       !marble_gpr_u32(p+40) && !marble_gpr_u32(p+44) && !marble_gpr_u32(p+48)) {
        r.kind=1;r.error=marble_gpr_u32(p+36);r.md_error=marble_gpr_u32(p+52);
    } else if(n==32 && op==MARBLE_GPR_BASIC_REPLY && marble_gpr_u32(p+24)==0x0400100a && marble_gpr_u32(p+28)) {
        r.kind=2;r.error=marble_gpr_u32(p+28);
    } else return 0;
    *out=r;return 1;
}
static unsigned marble_pcm_data_command(uint8_t *out,unsigned capacity,uint32_t handle)
{return marble_pcm_data_command_index(out,capacity,handle,0);}
static int marble_pcm_data_reply(const uint8_t *p,unsigned n,uint32_t handle,MarblePcmDataReply *out)
{return marble_pcm_data_reply_index(p,n,handle,0,out);}
#endif
