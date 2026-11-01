#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_tdm_config.h"
int main(void)
{
    uint8_t b[404],base[296];memset(b,0xa5,sizeof(b));
    assert(marble_tdm_graph_command(base,296,0)==296 && marble_tdm_config_command(b,400,0)==400);
    assert(marble_gpr_u32(b)==0x19060 && marble_gpr_u32(b+36)==360 && !memcmp(b+4,base+4,32) && !memcmp(b+40,base+40,256));
    for(unsigned i=400;i<404;i++)assert(b[i]==0xa5);
    const unsigned sizes[]={28,12,4},pids[]={0x0800101b,0x08001017,0x08001018};unsigned at=296;
    for(unsigned i=0;i<3;i++) {assert(marble_gpr_u32(b+at)==0x6001 && marble_gpr_u32(b+at+4)==pids[i] && marble_gpr_u32(b+at+8)==sizes[i] && !marble_gpr_u32(b+at+12));at+=16+((sizes[i]+7)&~7u);}
    assert(at==400);
    assert(!marble_gpr_u32(b+312) && marble_gpr_u32(b+316)==2 && marble_gpr_u32(b+320)==1 && marble_gpr_u32(b+324)==3 && marble_gpr_u32(b+328)==0x00200004 && marble_gpr_u32(b+332)==0x00010001 && marble_gpr_u32(b+336)==1 && !marble_gpr_u32(b+340));
    assert(marble_gpr_u32(b+360)==96000 && marble_gpr_u32(b+364)==0x00020020 && marble_gpr_u32(b+368)==1 && !marble_gpr_u32(b+372) && marble_gpr_u32(b+392)==1 && !marble_gpr_u32(b+396));
    assert(!marble_tdm_config_command(b,399,0) && !marble_tdm_config_command(b,63,1) && !marble_tdm_config_command(b,400,2) && !marble_tdm_config_command(NULL,400,0));
    assert(marble_tdm_config_command(b,64,1)==64 && marble_gpr_u32(b+20)==0x01001004 && marble_gpr_u32(b+60)==0x4001);
    puts("PASS TDM board calibration: three exact aligned module params in 400B OPEN, tertiary four 32-bit slots/mask3/inverted long sync/delay1, 96k stereo32 and frame1; no clock parameter or START; original graph unchanged");
}
