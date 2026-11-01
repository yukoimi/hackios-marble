#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_pcm_memory.h"
#include "../../xnuloader/marble_glink_data.h"
typedef struct {unsigned freed;} MarbleAudioMapResult;
static struct {unsigned active;MarbleAudioMapResult result;} marble_pcm_lease;
static unsigned marble_pcm_route_active,cleanup,retired,sends,fail_send;
static struct {unsigned sent,tx,reply,id[5];} marble_tdm_run;
static struct {unsigned app_remote,app_intents;MarbleGlinkRemoteIntent app_intent[8];} marble_glink_control_state;
static uint8_t last[44];
static void print(const char *f,...){(void)f;}
static int marble_pcm_route(unsigned enable){assert(!enable && marble_pcm_route_active);marble_pcm_route_active=0;return 1;}
static int marble_audio_map_end(void *l,MarbleAudioMapResult *r){assert(l==&marble_pcm_lease);marble_pcm_lease.active=0;r->freed=1;cleanup++;return 1;}
static void marble_audio_map_log(const MarbleAudioMapResult *r,unsigned ok){assert(r->freed);(void)ok;}
static int marble_glink_control_send_message(unsigned cmd,unsigned p1,unsigned p2,const uint8_t *p,unsigned n)
{assert(cmd==9 && p1==1 && (n==52 || n==36) && p2);if(fail_send)return 0;memcpy(last,p+8,n-8);sends++;return 1;}
static int marble_glink_control_send(unsigned c,unsigned a,unsigned b){assert(c==14 && a==1 && b==1);retired++;return 1;}
#include "../../xnuloader/marble_pcm_memory_fw.h"
static void reset(void)
{
    memset(&marble_pcm_memory,0,sizeof(marble_pcm_memory));memset(&marble_tdm_run,0,sizeof(marble_tdm_run));
    marble_pcm_lease.active=marble_pcm_route_active=1;cleanup=retired=sends=fail_send=0;
    marble_glink_control_state.app_remote=3;marble_glink_control_state.app_intents=8;
    for(unsigned i=0;i<8;i++)marble_glink_control_state.app_intent[i]=(MarbleGlinkRemoteIntent){128,i+1};
    marble_glink_control_state.app_intent[0].bytes=1024;
}
static int done(unsigned stage)
{MarbleGlinkControl m={.command=14,.channel=3,.parameter=marble_pcm_memory.id[stage],.bytes=8};return marble_pcm_memory_receive(&m,0);}
static int reply(unsigned stage,unsigned bad)
{
    uint8_t f[48]={0};unsigned n=stage?32:28;marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,n);uint8_t *p=f+16;
    marble_gpr_put32(p,n<<8|0x60);marble_gpr_put32(p+4,0x600203);marble_gpr_put32(p+8,1);marble_gpr_put32(p+12,3);
    marble_gpr_put32(p+16,(MARBLE_PCM_MEMORY_TOKEN+stage)^bad);marble_gpr_put32(p+20,stage?MARBLE_GPR_BASIC_REPLY:0x02001001);
    marble_gpr_put32(p+24,stage?0x0100100d:0x80001234);
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=48};return marble_pcm_memory_receive(&m,f);
}
int main(void)
{
    reset();assert(marble_pcm_memory_progress(0) && sends==1 && marble_pcm_memory.id[0]==2 && !marble_pcm_memory_ready());
    assert(reply(0,0)==1 && marble_pcm_memory_progress(0) && sends==1 && !marble_pcm_memory_ready());
    assert(done(0)==1 && marble_pcm_memory_ready() && marble_pcm_memory_progress(4) && sends==1 && !cleanup);
    /* Retire five distinct graph intents between map and unmap. */
    marble_tdm_run.sent=marble_tdm_run.reply=31;marble_tdm_run.tx=15;for(unsigned i=0;i<5;i++)marble_tdm_run.id[i]=i?i+2:1;
    assert(marble_pcm_memory_progress(5) && sends==1 && !cleanup);
    marble_tdm_run.tx=31;
    assert(marble_pcm_memory_progress(5) && sends==2 && marble_pcm_memory.id[1]==7 && marble_gpr_u32(last+24)==0x80001234);
    assert(done(1)==1 && marble_pcm_memory_progress(5) && !cleanup);
    assert(reply(1,0)==1 && marble_pcm_memory_progress(5) && cleanup==1 && retired==2 && !marble_pcm_lease.active);
    marble_pcm_memory_finish(1);assert(cleanup==1);
    reset();assert(marble_pcm_memory_progress(0) && reply(0,1)==1 && marble_pcm_memory.rejected && !cleanup);
    assert(marble_pcm_memory_progress(5) && sends==1 && !cleanup);marble_pcm_memory_finish(1);assert(cleanup==1);
    reset();fail_send=1;assert(!marble_pcm_memory_progress(0) && !sends && !cleanup);marble_pcm_memory_finish(1);assert(cleanup==1);
    puts("PASS actual asynchronous MAP/UNMAP: separate assembly/retirement, reserve OPEN intent, opaque handle, no early free, malformed reply and PAS-only fallback cleanup");
}
