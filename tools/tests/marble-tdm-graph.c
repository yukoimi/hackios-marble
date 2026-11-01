#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_tdm_graph.h"
int main(void)
{
    uint8_t b[300],r[32]={0};memset(b,0xa5,sizeof(b));
    assert(marble_tdm_graph_command(b,296,0)==296);
    assert(marble_gpr_u32(b)==0x12860 && marble_gpr_u32(b+4)==0x302 && marble_gpr_u32(b+8)==3 && marble_gpr_u32(b+12)==1 && marble_gpr_u32(b+16)==MARBLE_TDM_GRAPH_OPEN_TOKEN && marble_gpr_u32(b+20)==0x01001000 && marble_gpr_u32(b+36)==256);
    for(unsigned i=24;i<36;i++)assert(!b[i]);for(unsigned i=296;i<300;i++)assert(b[i]==0xa5);
    const unsigned starts[]={40,104,184,224,272},sizes[]={48,64,24,32,8},ids[]={0x08001001,0x08001000,0x08001002,0x08001003,0x08001004};
    unsigned at=40;
    for(unsigned i=0;i<5;i++) {assert(at==starts[i]);assert(marble_gpr_u32(b+at)==1 && marble_gpr_u32(b+at+4)==ids[i] && marble_gpr_u32(b+at+8)==sizes[i] && !marble_gpr_u32(b+at+12));at+=16+((sizes[i]+7)&~7u);}
    assert(at==296);
    assert(marble_gpr_u32(b+60)==0x4001 && marble_gpr_u32(b+64)==3);
    assert(marble_gpr_u32(b+76)==2 && marble_gpr_u32(b+88)==2 && marble_gpr_u32(b+100)==1);
    assert(marble_gpr_u32(b+124)==0x5001 && marble_gpr_u32(b+128)==4 && marble_gpr_u32(b+144)==3 && marble_gpr_u32(b+156)==4 && marble_gpr_u32(b+168)==8192 && marble_gpr_u32(b+180)==2);
    assert(marble_gpr_u32(b+204)==0x4001 && marble_gpr_u32(b+208)==0x5001 && marble_gpr_u32(b+212)==1 && marble_gpr_u32(b+216)==0x0700100e && marble_gpr_u32(b+220)==0x6001);
    assert(marble_gpr_u32(b+244)==0x6001 && marble_gpr_u32(b+248)==1 && marble_gpr_u32(b+252)==0x08001015 && marble_gpr_u32(b+256)==8 && marble_gpr_u32(b+260)==1 && !marble_gpr_u32(b+264) && !marble_gpr_u32(b+268));
    assert(!marble_gpr_u32(b+288) && !marble_gpr_u32(b+292));
    assert(!marble_tdm_graph_command(b,295,0) && !marble_tdm_graph_command(b,63,1) && !marble_tdm_graph_command(b,300,2) && !marble_tdm_graph_command(NULL,300,0));
    assert(marble_tdm_graph_command(b,64,1)==64 && marble_gpr_u32(b+16)==MARBLE_TDM_GRAPH_CLOSE_TOKEN && marble_gpr_u32(b+20)==0x01001004 && marble_gpr_u32(b+36)==24 && marble_gpr_u32(b+44)==0x08001005 && marble_gpr_u32(b+48)==8 && marble_gpr_u32(b+56)==1 && marble_gpr_u32(b+60)==0x4001);
    for(unsigned stage=0;stage<2;stage++) {
        marble_gpr_put32(r,0x2060);marble_gpr_put32(r+4,0x203);marble_gpr_put32(r+8,1);marble_gpr_put32(r+12,3);marble_gpr_put32(r+16,stage?MARBLE_TDM_GRAPH_CLOSE_TOKEN:MARBLE_TDM_GRAPH_OPEN_TOKEN);marble_gpr_put32(r+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(r+24,stage?0x01001004:0x01001000);marble_gpr_put32(r+28,0);
        MarbleTdmGraphReply out={0};assert(marble_tdm_graph_reply(r,32,stage,&out) && !out.error);
        for(unsigned n=0;n<32;n++)assert(!marble_tdm_graph_reply(r,n,stage,&out));
        for(unsigned word=0;word<7;word++)for(unsigned bit=0;bit<32;bit++) {
            if(word==1 && bit>=16 && bit<24)continue;
            uint8_t bad[32];memcpy(bad,r,32);bad[4*word+bit/8]^=(uint8_t)(1u<<(bit%8));MarbleTdmGraphReply saved=out;
            assert(!marble_tdm_graph_reply(bad,32,stage,&out) && !memcmp(&out,&saved,sizeof(out)));
        }
        marble_gpr_put32(r+4,0xa80203);marble_gpr_put32(r+28,2);assert(marble_tdm_graph_reply(r,32,stage,&out) && out.error==2 && out.client_data==0xa8);
    }
    puts("PASS TDM graph codec: exact aligned 296B single disconnected sink open and 64B private-subgraph close; no START/PREPARE/clock/amp/data mappings; mutations/truncations/stale tokens rejected");
}
