#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define XNU_MARBLE_TDM_PCM_GRAPH_ONLY 1
#include "../../xnuloader/marble_tdm_run.h"
static unsigned check_param(const uint8_t *p,unsigned remaining,uint32_t module,uint32_t id,unsigned size)
{
    unsigned n=(16+size+7)&~7u;
    assert(n<=remaining && marble_gpr_u32(p)==module && marble_gpr_u32(p+4)==id && marble_gpr_u32(p+8)==size && !marble_gpr_u32(p+12));return n;
}
int main(void)
{
    uint8_t out[536];memset(out,0xa5,sizeof(out));assert(marble_tdm_run_command(out,528,0)==528);
    assert(marble_gpr_u32(out)==0x21060 && marble_gpr_u32(out+36)==488 && marble_gpr_u32(out+144)==3 && marble_gpr_u32(out+148)==0x08001012);
    const uint32_t sizes[]={48,64,32,56,24,28,12,4,16,32};
    const uint32_t ids[]={0x8001001,0x8001000,0x8001002,0x8001003,0x8001004,0x800101b,0x8001017,0x8001018,0x800113c,0x800100c};
    unsigned pos=40;
    for(unsigned i=0;i<10;i++)pos+=check_param(out+pos,528-pos,i<5?1:i<9?0x6001:0x6002,ids[i],sizes[i]);
    assert(pos==528);
    assert(marble_gpr_u32(out+212)==2 && marble_gpr_u32(out+224)==0x07001000 && marble_gpr_u32(out+228)==0x6002);
    assert(marble_gpr_u32(out+320)==1 && marble_gpr_u32(out+324)==0x6002 && marble_gpr_u32(out+328)==1 && marble_gpr_u32(out+332)==0x6001 && marble_gpr_u32(out+336)==2);
    /* Official PCM format: width16 + alignment8 + interleave8; Q31/S32,
     * little endian, two logical channels FL1/FR2, payload18 excluding pad. */
    assert(marble_gpr_u32(out+504)==18 && marble_gpr_u32(out+508)==96000 && marble_gpr_u32(out+512)==0x01010020 && marble_gpr_u32(out+516)==0x001f0020 && marble_gpr_u32(out+520)==0x00020001 && marble_gpr_u32(out+524)==0x201);
    for(unsigned i=528;i<sizeof(out);i++)assert(out[i]==0xa5);
    for(unsigned n=0;n<528;n++)assert(!marble_tdm_run_command(out,n,0));
    puts("PASS connected WREP->TDM graph: aligned APM parameters, static port IDs, GC compatibility, explicit PCM media format and exact bounds");
}
