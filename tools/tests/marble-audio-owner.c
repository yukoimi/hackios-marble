#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnu/files/bsd/sys/marble_audio_stream.h"
#include "../../xnu/files/bsd/sys/marble_audio_owner.h"
static void put(uint8_t *p,unsigned n,unsigned v){marble_gpr_put32(p+n,v);}
static MarbleAudioProof proof(unsigned phase)
{
    MarbleAudioProof p={.epoch=19,.phase=phase,.handle=0x80001234,.start_cookie=4,
        .unmap_cookie=phase==2?9:0,.submitted=phase==2?2:1,.completed=phase==2?2:0,.flags=phase==2?63:15};
    put(p.map,0,0x1c60);put(p.map,4,0x203);put(p.map,8,1);put(p.map,12,3);put(p.map,16,MARBLE_PCM_MEMORY_TOKEN);
    put(p.map,20,0x02001001);put(p.map,24,p.handle);
    put(p.start,0,0x2060);put(p.start,4,0x203);put(p.start,8,1);put(p.start,12,3);
    put(p.start,16,MARBLE_TDM_RUN_TOKEN+2);put(p.start,20,MARBLE_GPR_BASIC_REPLY);put(p.start,24,0x01001002);
    if(phase==2) {
        put(p.unmap,0,0x2060);put(p.unmap,4,0x203);put(p.unmap,8,1);put(p.unmap,12,3);
        put(p.unmap,16,MARBLE_PCM_MEMORY_TOKEN+1);put(p.unmap,20,MARBLE_GPR_BASIC_REPLY);put(p.unmap,24,0x0100100d);
    }
    return p;
}
int main(void)
{
    MarbleAudioOwnerInfo info={.magic=MAOWNER_MAGIC,.epoch=19,.version=1,.bytes=320,.smem_pa=0x80900000,
        .smem_bytes=0x200000,.periods=2,.ipcc_pa=0xed18000};assert(marble_audio_owner_info_valid(&info));
    for(unsigned i=0;i<sizeof(info);i++) {MarbleAudioOwnerInfo bad=info;((uint8_t *)&bad)[i]^=128;
        /* Epoch can be any nonzero session value. All other ABI bytes fixed. */
        if(i<8 || i>=16)assert(!marble_audio_owner_info_valid(&bad));}
    MarbleAudioOwnerGrant g={.epoch=19,.sequence=2,.state=MAOWNER_PREPARING};assert(marble_audio_owner_grant_valid(&g,19));
    g.state=MAOWNER_RUNNING;g.page_pa=0xf1234000;g.deadline_us=900000;g.acknowledged=2;assert(marble_audio_owner_grant_valid(&g,19));
    g.sequence=3;assert(!marble_audio_owner_grant_valid(&g,19));g.sequence=4;
    g.state=MAOWNER_CONFIGURED;g.acknowledged=4;assert(marble_audio_owner_grant_valid(&g,19));
    g.state=MAOWNER_PAS_OFF;g.flags=3;g.acknowledged=6;g.submitted=g.completed=2;assert(marble_audio_owner_grant_valid(&g,19));
    g.state=MAOWNER_RELEASED;g.flags=7;g.page_pa=0;assert(!marble_audio_owner_grant_valid(&g,19));
    g.acknowledged=8;assert(marble_audio_owner_grant_valid(&g,19));assert(!marble_audio_owner_grant_valid(&g,20));
    for(unsigned command=1;command<=4;command++) {
        MarbleAudioOwnerRequest r={.epoch=19,.sequence=2*command,.command=command};MarbleAudioProof p={0};
        if(command==2)p=proof(1);if(command==3)p=proof(2);
        assert(marble_audio_owner_request_valid(&r,&p,19));
        for(unsigned i=0;i<sizeof(r);i++) {
            MarbleAudioOwnerRequest bad=r;((uint8_t *)&bad)[i]^=128;
            if(i<16 || i>=32)assert(!marble_audio_owner_request_valid(&bad,&p,19));
        }
        assert(!marble_audio_owner_request_valid(&r,&p,20));
        if(command==2 || command==3) {p.start[16]^=1;assert(!marble_audio_owner_request_valid(&r,&p,19));}
    }
    info.periods=8;assert(!marble_audio_owner_info_valid(&info));
    assert(marble_audio_owner_info_valid_periods(&info,8));
    assert(!marble_audio_owner_info_valid_periods(&info,2));
    g.submitted=g.completed=8;
    assert(!marble_audio_owner_grant_valid(&g,19));
    assert(marble_audio_owner_grant_valid_periods(&g,19,8));
    g.completed=7;assert(!marble_audio_owner_grant_valid_periods(&g,19,8));g.completed=8;
    MarbleAudioOwnerRequest r={.epoch=19,.sequence=6,.command=MAOWNER_RETIRE};MarbleAudioProof p=proof(2);
    p.submitted=p.completed=8;
    assert(marble_audio_owner_request_valid_periods(&r,&p,19,8));
    assert(!marble_audio_owner_request_valid(&r,&p,19));
    for(unsigned n=0;n<=9;n++)if(n!=8) {
        assert(!marble_audio_owner_info_valid_periods(&info,n));
        assert(!marble_audio_owner_request_valid_periods(&r,&p,19,n));
    }
    puts("PASS owner ABI: producer cachelines, fixed windows, even sequences, epoch, configure/retire raw proof, PAS+route before unmap ACK, release requires ACK8; no hardware acceptance");
}
