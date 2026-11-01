#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../xnuloader/marble_pcm_data.h"
#include "../../xnuloader/marble_glink_data.h"
static struct {unsigned sent,tx,reply,id[5];} marble_tdm_run;
static struct {unsigned sent,id[2];uint32_t handle;} marble_pcm_memory;
static struct {unsigned app_remote,app_intents;MarbleGlinkRemoteIntent app_intent[8];} marble_glink_control_state;
static unsigned marble_pcm_data_remote_used,marble_pcm_submitted,marble_pcm_data_remote_id,sends,retire,zero=1,faults=1;
static void print(const char *f,...){(void)f;}
static int prepare(void){return zero;}static int faults_ok(void){return faults;}
#define MARBLE_PCM_DATA_PREPARE prepare
#define MARBLE_PCM_DATA_FAULTS faults_ok
static unsigned marble_pcm_memory_ready(void){return 1;}
static int marble_glink_control_send_message(unsigned c,unsigned a,unsigned b,const uint8_t *p,unsigned n)
{assert(c==9 && a==1 && b==5 && n==76 && marble_gpr_u32(p+8+32)==0x80001234);sends++;return 1;}
static int marble_glink_control_send(unsigned c,unsigned a,unsigned b){assert(c==14 && a==1 && b==1);retire++;return 1;}
#include "../../xnuloader/marble_pcm_data_fw.h"
static void reset(void)
{
    memset(&marble_pcm_data,0,sizeof(marble_pcm_data));sends=retire=marble_pcm_submitted=marble_pcm_data_remote_used=0;zero=faults=1;
    marble_tdm_run.sent=marble_tdm_run.tx=marble_tdm_run.reply=7;
    for(unsigned i=0;i<3;i++)marble_tdm_run.id[i]=i+1;
    marble_pcm_memory.sent=1;marble_pcm_memory.id[0]=4;marble_pcm_memory.handle=0x80001234;
    marble_glink_control_state.app_remote=3;marble_glink_control_state.app_intents=8;
    for(unsigned i=0;i<8;i++)marble_glink_control_state.app_intent[i]=(MarbleGlinkRemoteIntent){128,i+1};
    marble_glink_control_state.app_intent[0].bytes=1024;
}
static int reply(unsigned bad)
{
    uint8_t f[72]={0};marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,56);
    uint8_t *p=f+16;marble_gpr_put32(p,0x3860);marble_gpr_put32(p+4,0x203);marble_gpr_put32(p+8,0x6002);marble_gpr_put32(p+12,3);
    marble_gpr_put32(p+16,MARBLE_PCM_DATA_TOKEN^bad);marble_gpr_put32(p+20,0x05001004);marble_gpr_put32(p+24,0x10000000);marble_gpr_put32(p+28,1);marble_gpr_put32(p+32,0x80001234);
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=72};return marble_pcm_data_receive(&m,f);
}
int main(void)
{
    reset();assert(marble_pcm_data_progress(2) && !sends);marble_tdm_run.tx=3;assert(marble_pcm_data_progress(3) && !sends);
    marble_tdm_run.tx=7;assert(marble_pcm_data_progress(3) && sends==1 && marble_pcm_submitted==1 && marble_pcm_data_remote_id==5);
    assert(reply(0)==1 && retire==1 && !marble_pcm_data_ready() && !marble_pcm_data_can_stop());assert(marble_pcm_data_progress(3) && sends==1 && !marble_pcm_data.logged);
    MarbleGlinkControl done={.command=14,.channel=3,.parameter=5,.bytes=8};assert(marble_pcm_data_receive(&done,0)==1);
    assert(marble_pcm_data_progress(3) && marble_pcm_data_ready() && marble_pcm_data.logged && marble_pcm_data.zero && marble_pcm_data.faults);
    assert(marble_pcm_data_can_stop());
    reset();assert(marble_pcm_data_progress(3) && reply(1)==1 && marble_pcm_data.bad && !marble_pcm_data_ready() && !marble_pcm_data_can_stop());marble_pcm_data_log(1);assert(marble_pcm_data.logged);
    reset();assert(!marble_pcm_data_can_stop());zero=0;assert(marble_pcm_data_progress(3) && !sends && marble_pcm_data.bad && marble_pcm_data_can_stop());
    assert(!marble_pcm_submitted && !marble_pcm_data_remote_used);
    puts("PASS actual PCM state: START reply+TX gate, zero-page only, distinct intent, typed DONE+retirement gate STOP, malformed retirement and PAS fallback");
}
