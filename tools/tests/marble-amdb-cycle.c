#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_amdb_cycle.h"
int main(void)
{
    uint8_t b[68],r[48]={0};const uint64_t h=0x123456789abcdef0ULL;
    memset(b,0xa5,sizeof(b));assert(marble_amdb_cycle_command(b,64,0,0)==64);
    assert(marble_gpr_u32(b)==0x4060 && marble_gpr_u32(b+4)==0x302 && marble_gpr_u32(b+8)==3 && marble_gpr_u32(b+12)==3);
    assert(marble_gpr_u32(b+16)==MARBLE_AMDB_LOAD_TOKEN && marble_gpr_u32(b+20)==MARBLE_AMDB_LOAD);
    for(unsigned i=24;i<36;i++)assert(!b[i]);assert(marble_gpr_u32(b+36)==24 && marble_gpr_u32(b+40)==2 && !marble_gpr_u32(b+44) && marble_gpr_u32(b+48)==1 && marble_gpr_u32(b+52)==MARBLE_AMDB_QUERY_MODULE);
    for(unsigned i=56;i<64;i++)assert(!b[i]);for(unsigned i=64;i<68;i++)assert(b[i]==0xa5);
    assert(marble_amdb_cycle_command(b,64,1,h)==64 && marble_gpr_u32(b+16)==MARBLE_AMDB_UNLOAD_TOKEN && marble_gpr_u32(b+20)==MARBLE_AMDB_UNLOAD && marble_gpr_u32(b+56)==(uint32_t)h && marble_gpr_u32(b+60)==(uint32_t)(h>>32));
    assert(!marble_amdb_cycle_command(b,63,0,0) && !marble_amdb_cycle_command(NULL,64,0,0) && !marble_amdb_cycle_command(b,64,1,0) && !marble_amdb_cycle_command(b,64,0,h) && !marble_amdb_cycle_command(b,64,2,h));
    marble_gpr_put32(r,0x3060);marble_gpr_put32(r+4,0x203);marble_gpr_put32(r+8,3);marble_gpr_put32(r+12,3);marble_gpr_put32(r+16,MARBLE_AMDB_LOAD_TOKEN);marble_gpr_put32(r+20,MARBLE_AMDB_LOAD_REPLY);marble_gpr_put32(r+24,2);marble_gpr_put32(r+32,1);marble_gpr_put32(r+36,MARBLE_AMDB_QUERY_MODULE);marble_gpr_put32(r+40,(uint32_t)h);marble_gpr_put32(r+44,(uint32_t)(h>>32));
    MarbleAmdbCycleReply out={0};assert(marble_amdb_cycle_reply(r,48,0,&out) && out.handle==h && !out.error);
    const unsigned fixed[]={0,4,8,12,16,20,24,32,36};
    for(unsigned j=0;j<sizeof(fixed)/sizeof(*fixed);j++)for(unsigned bit=0;bit<32;bit++) {
        uint8_t bad[48];memcpy(bad,r,48);bad[fixed[j]+bit/8]^=(uint8_t)(1u<<(bit%8));MarbleAmdbCycleReply saved=out;
        assert(!marble_amdb_cycle_reply(bad,48,0,&out) && !memcmp(&out,&saved,sizeof(out)));
    }
    for(unsigned n=0;n<48;n++)assert(!marble_amdb_cycle_reply(r,n,0,&out));
    marble_gpr_put32(r+40,0);marble_gpr_put32(r+44,0);assert(!marble_amdb_cycle_reply(r,48,0,&out));marble_gpr_put32(r+28,2);assert(marble_amdb_cycle_reply(r,48,0,&out) && !out.handle && out.error==2);
    marble_gpr_put32(r,0x2060);marble_gpr_put32(r+16,MARBLE_AMDB_UNLOAD_TOKEN);marble_gpr_put32(r+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(r+24,MARBLE_AMDB_UNLOAD);marble_gpr_put32(r+28,0);
    assert(marble_amdb_cycle_reply(r,32,1,&out) && out.kind==2 && !out.error && !out.handle);
    assert(!marble_amdb_cycle_reply(r,32,0,&out));marble_gpr_put32(r+4,0xa80203);assert(marble_amdb_cycle_reply(r,32,1,&out) && out.client_data==0xa8 && !out.error);marble_gpr_put32(r+28,2);assert(marble_amdb_cycle_reply(r,32,1,&out) && out.error==2 && out.client_data==0xa8);
    puts("PASS AMDB library codec: fixed single TDM module; exact 64B acquire/release, 48B opaque handle or 32B error; no graph/register/clock/amp commands; fixed-field mutations/truncations and stale-stage tokens rejected");
}
