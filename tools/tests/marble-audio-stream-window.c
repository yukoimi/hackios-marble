#define main legacy_endpoint_main
#include "marble-audio-endpoint.c"
#undef main
#include "../../xnu/files/bsd/sys/marble_audio_stream_window.h"
static MarbleAudioProof configured;
static int capture(void *ctx,const void *data,unsigned bytes)
{
 if(!published)assert(marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_CONFIGURE,&configured));
 return publish(ctx,data,bytes);
}
static int quiet(void *ctx,const MarbleAudioStream *s)
{
 (void)ctx;MarbleAudioProof p;
 assert(mawindow_drained_export(s,&configured,&p) && mawindow_drained_valid(&p,&configured,configured.epoch));
 assert(!marble_audio_board_gate_export(s,&configured,&p));
 assert(!marble_audio_board_gate_drained_valid(&p,&configured,configured.epoch));
 return 1;
}
int main(void)
{
 uint64_t epoch=0x1234567800000001ULL;
 MarbleAudioOwnerInfo info={.magic=MAOWNER_MAGIC,.epoch=epoch,.version=2,.bytes=320,
  .smem_pa=0x80900000,.smem_bytes=0x200000,.periods=24,.ipcc_pa=0xed18000};
 assert(mawindow_info_valid(&info) && !marble_audio_owner_info_valid_periods(&info,8));
 info.version=1;assert(!mawindow_info_valid(&info));info.version=2;info.reserved[0]=1;assert(!mawindow_info_valid(&info));
 MarbleAudioBoardGateInfo bi={.magic=MABG_MAGIC,.epoch=epoch,.version=2,.bytes=256,.periods=24,.muted=1};
 assert(mawindow_board_info_valid(&bi,epoch) && !marble_audio_board_gate_info_valid(&bi,epoch));
 bi.muted=0;assert(!mawindow_board_info_valid(&bi,epoch));
 MarbleAudioOwnerGrant g={.epoch=epoch,.sequence=2,.state=MAOWNER_PAS_OFF,.page_pa=0x80010000,.deadline_us=1000000,
  .acknowledged=6,.flags=3,.submitted=24,.completed=24};
 assert(mawindow_grant_valid(&g,epoch) && !marble_audio_owner_grant_valid_periods(&g,epoch,8));
 g.completed=23;assert(!mawindow_grant_valid(&g,epoch));g.completed=24;g.flags=7;assert(!mawindow_grant_valid(&g,epoch));
 MarbleAudioBoardGateQuiet q={.epoch=epoch,.sequence=2,.flags=7,.submitted=24,.completed=24};
 assert(mawindow_quiet_valid(&q,epoch) && !marble_audio_board_gate_quiet_valid(&q,epoch));
 q.completed=23;assert(!mawindow_quiet_valid(&q,epoch));
 init(24,0);endpoint.publish=capture;endpoint.before_stop=quiet;int result=0;
 for(unsigned tick=0;tick<500 && !result;tick++){result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
 assert(result==1 && periods==24 && endpoint.stream.completed==24 && endpoint.stop_permitted);
 MarbleAudioProof proof;assert(marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_RETIRED,&proof));
 MarbleAudioOwnerRequest r={.epoch=epoch,.sequence=6,.command=MAOWNER_RETIRE};
 assert(mawindow_request_valid(&r,&proof,epoch) && !marble_audio_owner_request_valid_periods(&r,&proof,epoch,8));
 proof.completed=23;assert(!mawindow_request_valid(&r,&proof,epoch));
 puts("PASS separate 24-period v2 window: real framed endpoint retirement/quiet gate, original epoch/typed replies/flags/addresses preserved, strict legacy rejection, short/partial proof rejected; no firmware binding or hardware acceptance");
 return 0;
}
