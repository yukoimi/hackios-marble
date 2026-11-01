#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/sys/marble_audio_stream.h"
static MarbleAudioStream s;
static MarbleAudioQueue q;
static _Alignas(4096) uint8_t page[4096];
static uint8_t packet[608],response[56];
static unsigned reply_packet(void)
{
    memset(response,0,sizeof(response));unsigned n=32;
    if(s.phase==MAS_MAP)n=28;else if(s.phase==MAS_DATA)n=56;
    marble_gpr_put32(response,n<<8|0x60);marble_gpr_put32(response+4,0x203);
    marble_gpr_put32(response+8,s.phase==MAS_DATA?0x6002:1);marble_gpr_put32(response+12,3);
    if(s.phase==MAS_MAP || s.phase==MAS_UNMAP) {
        marble_gpr_put32(response+16,MARBLE_PCM_MEMORY_TOKEN+(s.phase==MAS_UNMAP));
        marble_gpr_put32(response+20,s.phase==MAS_MAP?0x02001001:MARBLE_GPR_BASIC_REPLY);
        marble_gpr_put32(response+24,s.phase==MAS_MAP?0x80001234:0x0100100d);
    } else if(s.phase==MAS_DATA) {
        marble_gpr_put32(response+8,0x6002);
        marble_gpr_put32(response+16,MARBLE_PCM_DATA_TOKEN+s.submitted-1);
        marble_gpr_put32(response+20,0x05001004);marble_gpr_put32(response+24,0x10000000);
        marble_gpr_put32(response+28,1);marble_gpr_put32(response+32,s.handle);
    } else {
        unsigned stage=marble_audio_stream_stage(s.phase);
        marble_gpr_put32(response+16,MARBLE_TDM_RUN_TOKEN+stage);
        marble_gpr_put32(response+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(response+24,0x01001000+stage);
    }
    return n;
}
static void init(unsigned limit)
{
    memset(&s,0,sizeof(s));memset(&q,0,sizeof(q));memset(page,0xa5,sizeof(page));
    assert(marble_audio_stream_init(&s,&q,page,limit));
    assert(marble_audio_stream_bind_epoch(&s,0x1234567800000001ULL));
    assert(!marble_audio_stream_bind_epoch(&s,2));
    for(unsigned i=0;i<4096;i++)assert(!page[i]);
}
static void transact(unsigned order,unsigned cookie)
{
    assert(marble_audio_stream_next(&s,packet,sizeof(packet),cookie,100));
    unsigned phase=s.phase,n=reply_packet();uint8_t saved[4096];memcpy(saved,page,sizeof(page));
    assert(order?marble_audio_stream_tx_done(&s,cookie,1):marble_audio_stream_reply(&s,response,n));
    assert(!marble_audio_stream_next(&s,packet,sizeof(packet),cookie+1,200));
    assert(!memcmp(saved,page,sizeof(page)) && s.pending && s.phase==phase);
    if(phase==MAS_START)assert(!s.start_retired);
    assert(order?marble_audio_stream_reply(&s,response,n):marble_audio_stream_tx_done(&s,cookie,1));
    assert(!s.pending && !s.failed);
    if(phase==MAS_START)assert(s.start_retired==1 && s.phase==MAS_DATA);
}
int main(void)
{
    init(24);assert(marble_audio_stream_next(&s,packet,sizeof(packet),1,100));
    unsigned rejected_bytes=reply_packet();response[20]^=0x80;
    assert(!marble_audio_stream_reply(&s,response,rejected_bytes) && s.failed && s.pending && !s.submitted);
    assert(s.diagnostic_reply_bytes==rejected_bytes && !memcmp(s.diagnostic_reply,response,rejected_bytes));
    for(unsigned i=rejected_bytes;i<64;i++)assert(s.diagnostic_reply[i]==0);
    /* Full 2048-token run, alternating DONE/TX orders and deterministic
     * stereo data. Every underflow produces a full zero period. */
    for(unsigned order=0;order<2;order++) {
        init(2048);unsigned cookie=1;
        while(s.phase!=MAS_RETIRED) {
            if(s.phase==MAS_DATA) {
                int16_t frames[512];
                for(unsigned i=0;i<512;i++)frames[i]=(int16_t)(s.completed*257+i);
                if(s.completed&1)assert(marble_audio_queue_push(&q,frames,256)==256);
                transact((order+s.completed)&1,cookie++);
                for(unsigned i=0;i<512;i++) {
                    uint16_t v=(uint16_t)page[2*i]|(uint16_t)page[2*i+1]<<8;
                    assert(v==((s.completed&1)?0:(uint16_t)frames[i]));
                }
                for(unsigned i=1024;i<4096;i++)assert(!page[i]);
            } else transact(order,cookie++);
        }
        assert(marble_audio_stream_protocol_retired(&s) && s.completed==2048 && s.submitted==2048);
        MarbleAudioProof proof;
        assert(marble_audio_stream_export_proof(&s,MAPROOF_RETIRED,&proof));
        assert(marble_audio_proof_valid(&proof,0x1234567800000001ULL,MAPROOF_RETIRED));
        assert(!marble_audio_proof_valid(&proof,2,MAPROOF_RETIRED));
        for(unsigned bad=0;bad<8;bad++) {
            MarbleAudioProof broken=proof;
            if(bad==0)broken.map[16]^=1;
            if(bad==1)broken.map[24]^=1;
            if(bad==2)broken.start[28]=1;
            if(bad==3)broken.unmap[16]^=1;
            if(bad==4)broken.unmap[28]=1;
            if(bad==5)broken.flags&=~MAPROOF_UNMAP_TX;
            if(bad==6)broken.unmap_cookie=broken.start_cookie;
            if(bad==7)broken.completed--;
            assert(!marble_audio_proof_valid(&broken,proof.epoch,MAPROOF_RETIRED));
        }
        assert(q.consumed==1024*256 && q.silence==1024*256);
        assert(!marble_audio_stream_reset_after_pas(&s,0,1) && !marble_audio_stream_reset_after_pas(&s,1,0));
        assert(marble_audio_stream_reset_after_pas(&s,1,1));
    }
    /* Pending DMA cancellation never frees/reuses the page. */
    init(8);for(unsigned i=0;i<4;i++)transact(0,i+1);
    assert(s.phase==MAS_DATA && marble_audio_stream_next(&s,packet,sizeof(packet),10,0));
    marble_audio_stream_stop(&s);assert(s.pending && s.phase==MAS_DATA);
    unsigned n=reply_packet();assert(marble_audio_stream_reply(&s,response,n));
    assert(s.pending && marble_audio_stream_tx_done(&s,10,1) && s.phase==MAS_STOP);
    for(unsigned i=0;i<3;i++)transact(0,11+i);
    assert(marble_audio_stream_protocol_retired(&s) && s.completed==1);
    /* Faults at each phase latch ownership, block subsequent submissions
     * and require the caller's verified PAS/route closure to reset. */
    for(unsigned target=MAS_MAP;target<=MAS_UNMAP;target++)for(unsigned fault=0;fault<4;fault++) {
        init(1);unsigned cookie=1;
        while(s.phase!=target)transact(0,cookie++);
        assert(marble_audio_stream_next(&s,packet,sizeof(packet),cookie,5));
        unsigned n=reply_packet();
        if(fault==0){response[16]^=1;assert(!marble_audio_stream_reply(&s,response,n));}
        if(fault==1)assert(!marble_audio_stream_tx_done(&s,cookie+1,1));
        if(fault==2)assert(!marble_audio_stream_tick(&s,1000005));
        if(fault==3)assert(!marble_audio_stream_fail(&s));
        assert(s.failed && s.live && s.pending && !marble_audio_stream_protocol_retired(&s));
        assert(!marble_audio_stream_next(&s,packet,sizeof(packet),cookie+1,6));
        assert(!marble_audio_stream_init(&s,&q,page,1));
        assert(marble_audio_stream_reset_after_pas(&s,1,1));
    }
    init(1);assert(!marble_audio_stream_next(&s,packet,43,1,0) && !s.pending);
    assert(!marble_audio_stream_next(&s,packet,608,0,0));
    assert(!marble_audio_stream_next(&s,packet,608,1,UINT64_MAX));
    puts("PASS transport-independent XNU audio engine: MAP/MFC graph/2048 WREP tokens/STOP/CLOSE/UNMAP, both retirement orders, stereo+underflow, pending-stop and every-phase fault ownership; no DSP/hardware acceptance");
}
