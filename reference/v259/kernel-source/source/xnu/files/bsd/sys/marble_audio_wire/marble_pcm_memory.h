/* AudioReach apm_memmap_api.h: MAP payload20, UNMAP payload4 directly after
 * GPR header. These commands do not carry apm_cmd_header_t. SID1801 uses MSW1. */
#ifndef MARBLE_PCM_MEMORY_H
#define MARBLE_PCM_MEMORY_H
#include "marble_gpr.h"
#define MARBLE_PCM_MEMORY_TOKEN 0x4d504d01u
typedef struct {unsigned kind;uint32_t handle,error,client;} MarblePcmMemoryReply;
static unsigned marble_pcm_memory_command(uint8_t *out,unsigned capacity,unsigned unmap,uint32_t handle)
{
    unsigned n=unmap?28:44;
    if(!out || unmap>1 || capacity<n || (unmap && !handle) || (!unmap && handle))return 0;
    for(unsigned i=0;i<n;i++)out[i]=0;
    marble_gpr_put32(out,n<<8|0x60);marble_gpr_put32(out+4,0x302);
    marble_gpr_put32(out+8,3);marble_gpr_put32(out+12,1);
    marble_gpr_put32(out+16,MARBLE_PCM_MEMORY_TOKEN+unmap);
    marble_gpr_put32(out+20,0x0100100c+unmap);
    if(unmap)marble_gpr_put32(out+24,handle);
    else {
        marble_gpr_put32(out+24,0x00010003); /* pool3, one region */
        marble_gpr_put32(out+32,0x10000000);marble_gpr_put32(out+36,1);
        marble_gpr_put32(out+40,4096);
    }
    return n;
}
static int marble_pcm_memory_reply(const uint8_t *data,unsigned n,unsigned unmap,MarblePcmMemoryReply *out)
{
    if(!data || !out || unmap>1 || (n!=28 && n!=32) || marble_gpr_u32(data)!=(n<<8|0x60) ||
       (marble_gpr_u32(data+4)&0xff00ffffu)!=0x203 || marble_gpr_u32(data+8)!=1 ||
       marble_gpr_u32(data+12)!=3 || marble_gpr_u32(data+16)!=MARBLE_PCM_MEMORY_TOKEN+unmap)return 0;
    MarblePcmMemoryReply r={0};r.client=data[6];uint32_t op=marble_gpr_u32(data+20);
    if(!unmap && n==28 && op==0x02001001) {
        r.kind=1;r.handle=marble_gpr_u32(data+24);if(!r.handle)return 0;
    } else if(n==32 && op==MARBLE_GPR_BASIC_REPLY && marble_gpr_u32(data+24)==0x0100100c+unmap) {
        r.kind=2;r.error=marble_gpr_u32(data+28);
        /* MAP success must provide a typed nonzero handle. */
        if(!unmap && !r.error)return 0;
    } else return 0;
    *out=r;return 1;
}
#endif
