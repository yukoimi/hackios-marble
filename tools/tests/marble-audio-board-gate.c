/* Exercise the real endpoint framing and both DONE/TX orders. */
#define main legacy_endpoint_main
#include "marble-audio-endpoint.c"
#undef main
#include "../../xnu/files/bsd/sys/marble_audio_board_gate.h"
static unsigned gate_calls,allow_stop,reject_stop;
static MarbleAudioProof configure,drained;
static int gated_publish(void *cookie,const void *data,unsigned bytes)
{
    if(!published)assert(marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_CONFIGURE,&configure));
    return publish(cookie,data,bytes);
}
static int gate(void *cookie,const MarbleAudioStream *s)
{
    (void)cookie;gate_calls++;
    assert(periods==8 && s->completed==8 && !s->pending && s->tx && s->reply);
    assert(marble_audio_board_gate_export(s,&configure,&drained));
    assert(!marble_audio_proof_valid(&drained,drained.epoch,MAPROOF_CONFIGURE));
    assert(!marble_audio_proof_valid(&drained,drained.epoch,MAPROOF_RETIRED));
    return reject_stop?-1:allow_stop?1:0;
}
static void start_gate(void)
{
    init(8,0);gate_calls=allow_stop=reject_stop=0;
    configure=(MarbleAudioProof){0};drained=(MarbleAudioProof){0};
    endpoint.publish=gated_publish;endpoint.before_stop=gate;
}
int main(void)
{
    assert(legacy_endpoint_main()==0);
    start_gate();int result=0;unsigned tick=0;
    while(!gate_calls && tick<200) {result=marble_audio_endpoint_poll(&endpoint,tick++*100);firmware();assert(!result);}
    assert(gate_calls && periods==8 && endpoint.stream.phase==MAS_STOP && !endpoint.stream.pending);
    unsigned writes=write_count;
    for(unsigned i=0;i<8;i++) {assert(!marble_audio_endpoint_poll(&endpoint,tick++*100));firmware();}
    assert(write_count==writes && !endpoint.stop_permitted && !endpoint.stream.pending);
    MarbleAudioProof bad=drained;bad.completed=7;
    assert(!marble_audio_board_gate_drained_valid(&bad,&configure,configure.epoch));
    bad=drained;bad.epoch++;assert(!marble_audio_board_gate_drained_valid(&bad,&configure,configure.epoch));
    bad=drained;bad.flags|=MAPROOF_UNMAP_TX;assert(!marble_audio_board_gate_drained_valid(&bad,&configure,configure.epoch));
    bad=drained;bad.start_cookie++;assert(!marble_audio_board_gate_drained_valid(&bad,&configure,configure.epoch));
    bad=drained;bad.map[24]^=1;assert(!marble_audio_board_gate_drained_valid(&bad,&configure,configure.epoch));
    MarbleAudioStream invalid=endpoint.stream;invalid.pending=1;assert(!marble_audio_board_gate_export(&invalid,&configure,&bad));
    invalid=endpoint.stream;invalid.tx=0;assert(!marble_audio_board_gate_export(&invalid,&configure,&bad));
    invalid=endpoint.stream;invalid.reply=0;assert(!marble_audio_board_gate_export(&invalid,&configure,&bad));
    MarbleAudioBoardGateInfo info={.magic=MABG_MAGIC,.epoch=configure.epoch,.version=1,.bytes=256,.periods=8,.muted=1};
    assert(marble_audio_board_gate_info_valid(&info,configure.epoch));
    info.muted=0;assert(!marble_audio_board_gate_info_valid(&info,configure.epoch));
    MarbleAudioBoardGateQuiet quiet={.epoch=configure.epoch,.sequence=2,.flags=7,.submitted=8,.completed=8};
    assert(marble_audio_board_gate_quiet_valid(&quiet,configure.epoch));
    for(unsigned f=0;f<7;f++) {quiet.flags=f;assert(!marble_audio_board_gate_quiet_valid(&quiet,configure.epoch));}
    quiet.flags=7;quiet.sequence=1;assert(!marble_audio_board_gate_quiet_valid(&quiet,configure.epoch));
    allow_stop=1;unsigned calls=gate_calls;
    for(;tick<300 && !result;tick++) {result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
    assert(result==1 && endpoint.stop_permitted && gate_calls==calls+1);
    start_gate();reject_stop=1;result=0;
    for(tick=0;tick<200 && !result;tick++) {result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
    assert(result==-1 && periods==8 && !endpoint.stream.pending && endpoint.stream.failed && !endpoint.stop_permitted);
    start_gate();result=0;
    for(tick=0;tick<200 && !gate_calls;tick++) {assert(!marble_audio_endpoint_poll(&endpoint,tick*100));firmware();}
    assert(gate_calls && marble_audio_endpoint_poll(&endpoint,2000000)==-1 && !endpoint.stop_permitted);
    puts("PASS board gate: DATA drained, STOP blocked until quiet, stale/malformed/partial ACK rejected, fault/timeout retains DMA; no hardware acceptance");
    return 0;
}
