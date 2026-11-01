#define main legacy_endpoint_main
#include "marble-audio-endpoint.c"
#undef main
#include "../../xnu/files/bsd/sys/marble_audio_refill.h"
#include "../../xnu/files/bsd/sys/marble_audio_board_gate.h"
static unsigned waiting,ready,reject,quiet;
static MarbleAudioProof configured;
static int live_publish(void *ctx,const void *p,unsigned bytes)
{
 if(!published)assert(marble_audio_stream_export_proof(&endpoint.stream,MAPROOF_CONFIGURE,&configured));
 return publish(ctx,p,bytes);
}
static int before_data(void *ctx,const MarbleAudioStream *s)
{
 (void)ctx;
 if(s->completed!=3)return 1;
 if(reject)return -1;
 if(ready)return 1;
 assert(!s->pending && s->tx && s->reply && queue.head==queue.tail && queue.consumed==512 && queue.silence==256);
 waiting++;return 0;
}
static int before_stop(void *ctx,const MarbleAudioStream *s)
{
 (void)ctx;MarbleAudioProof drained;
 assert(ready && s->completed==8 && marble_audio_board_gate_export(s,&configured,&drained));
 return quiet?1:0;
}
static void start(void)
{
 init(8,0);defer_refill=1;waiting=ready=reject=quiet=0;
 endpoint.publish=live_publish;endpoint.before_data=before_data;endpoint.before_stop=before_stop;
}
int main(void)
{
 MarbleAudioRefillStatus status={.magic=MAREFILL_MAGIC,.version=1,.bytes=64};
 assert(marble_audio_refill_request_valid(&status));
 for(unsigned i=12;i<64;i++) {((uint8_t *)&status)[i]=1;assert(!marble_audio_refill_request_valid(&status));((uint8_t *)&status)[i]=0;}
 start();unsigned tick=0;int result=0;
 for(;tick<200 && !waiting;tick++){assert(!marble_audio_endpoint_poll(&endpoint,tick*100));firmware();}
 assert(waiting && periods==3 && published==3);
 unsigned writes=write_count;
 for(unsigned i=0;i<20;i++,tick++){assert(!marble_audio_endpoint_poll(&endpoint,tick*100));firmware();}
 assert(periods==3 && write_count==writes && queue.silence==256);
 pattern(3);ready=1;
 for(;tick<300 && periods<8;tick++){assert(!marble_audio_endpoint_poll(&endpoint,tick*100));firmware();}
 for(unsigned i=0;i<4;i++,tick++){assert(!marble_audio_endpoint_poll(&endpoint,tick*100));firmware();}
 assert(periods==8 && published==8 && endpoint.stream.phase==MAS_STOP && !endpoint.stream.pending && !endpoint.stop_permitted);
 quiet=1;
 for(;tick<400 && !result;tick++){result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
 assert(result==1 && endpoint.stop_permitted && queue.consumed==1792 && queue.silence==256);
 start();reject=1;result=0;
 for(tick=0;tick<200 && !result;tick++){result=marble_audio_endpoint_poll(&endpoint,tick*100);firmware();}
 assert(result==-1 && periods==3 && endpoint.stream.live && !endpoint.stream.pending && !endpoint.stop_permitted);
 start();
 for(tick=0;tick<200 && !waiting;tick++){assert(!marble_audio_endpoint_poll(&endpoint,tick*100));firmware();}
 assert(waiting && marble_audio_endpoint_poll(&endpoint,2000000)==-1 && endpoint.stream.live);
 puts("PASS live refill framing: three DATA incl. zero gap, no submission while waiting, later producer recovery, quiet-before-STOP, rejected/late refill retains DMA, fixed readonly ioctl ABI; no hardware acceptance");
 return 0;
}
