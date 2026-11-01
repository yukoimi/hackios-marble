#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_amdb.h"
int main(void)
{
    uint8_t b[100],r[84]={0};const uint32_t token=0x4d414d01;
    memset(b,0xa5,sizeof(b));assert(marble_amdb_command(b,96,token)==96);
    assert(marble_gpr_u32(b)==0x6060 && marble_gpr_u32(b+4)==0x302 && marble_gpr_u32(b+8)==3 && marble_gpr_u32(b+12)==3);
    assert(marble_gpr_u32(b+20)==0x01001007 && marble_gpr_u32(b+36)==56 && marble_gpr_u32(b+40)==3 && marble_gpr_u32(b+44)==0x080013d6 && marble_gpr_u32(b+48)==40 && marble_gpr_u32(b+56)==2 && marble_gpr_u32(b+60)==1 && marble_gpr_u32(b+64)==MARBLE_AMDB_QUERY_MODULE);
    for(unsigned i=24;i<36;i++)assert(!b[i]);for(unsigned i=68;i<96;i++)assert(!b[i]);for(unsigned i=96;i<100;i++)assert(b[i]==0xa5);
    assert(!marble_amdb_command(b,95,token) && !marble_amdb_command(b,96,0) && !marble_amdb_command(NULL,96,token));
    marble_gpr_put32(r,84u<<8|6u<<4);marble_gpr_put32(r+4,0x203);marble_gpr_put32(r+8,3);marble_gpr_put32(r+12,3);marble_gpr_put32(r+16,token);marble_gpr_put32(r+20,0x02001000);
    marble_gpr_put32(r+28,3);marble_gpr_put32(r+32,0x080013d6);marble_gpr_put32(r+36,40);marble_gpr_put32(r+44,2);marble_gpr_put32(r+48,1);marble_gpr_put32(r+52,0x0700100e);marble_gpr_put32(r+60,1);marble_gpr_put32(r+64,2);marble_gpr_put32(r+68,9);
    marble_gpr_put32(r+52,MARBLE_AMDB_QUERY_MODULE);
    MarbleAmdbReply out={0};assert(marble_amdb_reply(r,84,token,&out));assert(out.kind==1 && out.present==1 && out.major==2 && out.minor==9 && !out.error);
    for(unsigned n=0;n<84;n++) {MarbleAmdbReply saved=out;assert(!marble_amdb_reply(r,n,token,&out));assert(!memcmp(&out,&saved,sizeof(out)));}
    const unsigned immutable[]={0,4,8,12,16,20,28,32,36,44,48,52};
    for(unsigned j=0;j<sizeof(immutable)/sizeof(*immutable);j++)for(unsigned bit=0;bit<32;bit++) {
        uint8_t bad[84];memcpy(bad,r,84);bad[immutable[j]+bit/8]^=(uint8_t)(1u<<(bit%8));MarbleAmdbReply saved=out;
        assert(!marble_amdb_reply(bad,84,token,&out));assert(!memcmp(&out,&saved,sizeof(out)));
    }
    marble_gpr_put32(r+60,2);assert(!marble_amdb_reply(r,84,token,&out));marble_gpr_put32(r+60,0);assert(marble_amdb_reply(r,84,token,&out) && !out.present);
    marble_gpr_put32(r+56,1);assert(marble_amdb_reply(r,84,token,&out) && out.module_error==1);
    r[74]=13;assert(!marble_amdb_reply(r,84,token,&out));r[74]=0;
    marble_gpr_put32(r,32u<<8|6u<<4);marble_gpr_put32(r+20,0x02001005);marble_gpr_put32(r+24,0x01001007);marble_gpr_put32(r+28,2);
    assert(marble_amdb_reply(r,32,token,&out) && out.kind==2 && out.error==2);
    marble_gpr_put32(r+4,0xa80203);assert(marble_amdb_reply(r,32,token,&out) && out.client_data==0xa8 && out.error==2);
    marble_gpr_put32(r+28,0);assert(!marble_amdb_reply(r,32,token,&out));marble_gpr_put32(r+28,2);
    marble_gpr_put32(r+4,0x1a80203);assert(!marble_amdb_reply(r,32,token,&out));
    puts("PASS AMDB codec: exact 96B one-module in-band query, no mappings/registration/graph; bounded 84B metadata or 32B basic response, all fixed-field bit mutations/truncations, absent module/error/date bounds");
}
