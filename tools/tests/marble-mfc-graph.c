#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_tdm_run.h"
#include "../../xnuloader/marble_mfc_graph.h"
int main(void)
{
    uint8_t base[432],out[616];
    assert(marble_tdm_run_base_command(base,sizeof(base),0)==432);
    memset(out,0xa5,sizeof(out));
    assert(marble_mfc_graph_open(out,608,base)==608);
    assert(marble_gpr_u32(out)==0x26060 && marble_gpr_u32(out+36)==568);
    /* Parse the complete aligned parameter chain, including source and MFC. */
    const uint32_t sizes[]={48,64,40,80,40,28,12,4,16,32,16};
    const uint32_t ids[]={0x8001001,0x8001000,0x8001002,0x8001003,0x8001004,0x800101b,0x8001017,0x8001018,0x800113c,0x800100c,0x8001024};
    unsigned at=40;
    for(unsigned i=0;i<11;i++) {
        assert(!(at&7) && marble_gpr_u32(out+at+4)==ids[i] && marble_gpr_u32(out+at+8)==sizes[i] && !marble_gpr_u32(out+at+12));
        assert(marble_gpr_u32(out+at)==(i<5?1:i<9?0x6001:i==9?0x6002:0x6003));
        at+=(16+sizes[i]+7)&~7u;
    }
    assert(at==608 && marble_gpr_u32(out+212)==3 && marble_gpr_u32(out+232)==0x07001015);
    assert(marble_gpr_u32(out+352)==2 && marble_gpr_u32(out+356)==0x6002 && marble_gpr_u32(out+364)==0x6003);
    assert(marble_gpr_u32(out+372)==0x6003 && marble_gpr_u32(out+380)==0x6001);
    assert(marble_gpr_u32(out+556)==48000 && marble_gpr_u32(out+560)==0x01010010 && marble_gpr_u32(out+564)==0xf0010);
    assert(marble_gpr_u32(out+592)==96000 && marble_gpr_u32(out+596)==0x20020 && marble_gpr_u32(out+600)==0x20001);
    /* Board TDM configuration is copied byte for byte. */
    assert(!memcmp(out+392,base+296,136));
    for(unsigned i=608;i<sizeof(out);i++)assert(out[i]==0xa5);
    for(unsigned n=0;n<608;n++)assert(!marble_mfc_graph_open(out,n,base));
    assert(!marble_mfc_graph_open(0,608,base) && !marble_mfc_graph_open(out,608,0));
    base[0]^=1;assert(!marble_mfc_graph_open(out,608,base));
    puts("PASS offline AudioReach WREP48/S16 -> MFC -> board TDM96/S32: aligned parameters, exact ports/media/channel mapping, unchanged board configuration, bounds; DSP support not established");
}
