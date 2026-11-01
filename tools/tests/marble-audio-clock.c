#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_audio_clock.h"
int main(void)
{
    uint8_t b[80],r[32]={0};
    for(unsigned release=0;release<2;release++) {
        unsigned n=release?64:76;memset(b,0xa5,sizeof(b));assert(marble_audio_clock_command(b,n,release)==n);
        assert(marble_gpr_u32(b)==(n<<8|0x60) && marble_gpr_u32(b+4)==0x302 && marble_gpr_u32(b+8)==7 && marble_gpr_u32(b+12)==2);
        assert(marble_gpr_u32(b+16)==(release?MARBLE_AUDIO_CLOCK_RELEASE_TOKEN:MARBLE_AUDIO_CLOCK_REQUEST_TOKEN) && marble_gpr_u32(b+20)==(release?0x01001010u:0x0100100fu));
        for(unsigned i=24;i<36;i++)assert(!b[i]);assert(marble_gpr_u32(b+36)==n-40 && marble_gpr_u32(b+40)==2 && marble_gpr_u32(b+44)==0x0800102c && marble_gpr_u32(b+48)==n-56 && !marble_gpr_u32(b+52) && marble_gpr_u32(b+56)==1 && marble_gpr_u32(b+60)==0x204);
        if(!release)assert(marble_gpr_u32(b+64)==12288000 && marble_gpr_u32(b+68)==4 && !marble_gpr_u32(b+72));
        for(unsigned i=n;i<80;i++)assert(b[i]==0xa5);
        assert(!marble_audio_clock_command(b,n-1,release) && !marble_audio_clock_command(NULL,n,release));
        marble_gpr_put32(r,0x2060);marble_gpr_put32(r+4,0x203);marble_gpr_put32(r+8,2);marble_gpr_put32(r+12,7);marble_gpr_put32(r+16,release?MARBLE_AUDIO_CLOCK_RELEASE_TOKEN:MARBLE_AUDIO_CLOCK_REQUEST_TOKEN);marble_gpr_put32(r+20,release?0x02001003:0x02001002);marble_gpr_put32(r+24,0x0800102c);marble_gpr_put32(r+28,0);
        MarbleAudioClockReply out={0};assert(marble_audio_clock_reply(r,32,release,&out) && out.kind==1 && !out.error);
        for(unsigned n2=0;n2<32;n2++)assert(!marble_audio_clock_reply(r,n2,release,&out));
        for(unsigned word=0;word<7;word++)for(unsigned bit=0;bit<32;bit++) {
            if(word==1 && bit>=16 && bit<24)continue;
            uint8_t bad[32];memcpy(bad,r,32);bad[4*word+bit/8]^=(uint8_t)(1u<<(bit%8));MarbleAudioClockReply saved=out;
            assert(!marble_audio_clock_reply(bad,32,release,&out) && !memcmp(&out,&saved,sizeof(out)));
        }
        marble_gpr_put32(r+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(r+24,release?0x01001010:0x0100100f);
        assert(!marble_audio_clock_reply(r,32,release,&out));marble_gpr_put32(r+28,2);assert(marble_audio_clock_reply(r,32,release,&out) && out.kind==2 && out.error==2);
    }
    assert(!marble_audio_clock_command(b,80,2));
    for(unsigned i=0;i<7;i++) {
        assert(marble_audio_clock_pin_valid(i,1,0));
        assert(!marble_audio_clock_pin_valid(i,5,0) && !marble_audio_clock_pin_valid(i,0x201,0));
        if(i<3)assert(!marble_audio_clock_pin_valid(i,1,2) && !marble_audio_clock_pin_valid(i,1,1));
    }
    assert(!marble_audio_clock_pin_valid(7,1,0));
    puts("PASS audio clock: exact ROM PRM76/64 request/release204 at12.288MHz attr4; typed result required, mutations/truncations; reset-low/switch-low/input-pin guards; no graph/amp/pin writes");
}
