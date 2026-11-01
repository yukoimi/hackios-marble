#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_tdm_run.h"
#include "../../xnuloader/marble_glink_data.h"
static struct {unsigned advertised,sent,done,replies;MarbleGlinkAssembly rx;} marble_gpr;
static struct {unsigned app_mode,app_ack,app_remote,app_intents;MarbleGlinkRemoteIntent app_intent[5];} marble_glink_control_state;
static uint32_t cfg[7],io[7];static unsigned sends,retire,fail_send;static uint8_t last[MARBLE_TDM_RUN_OPEN_BYTES];
static void print(const char *fmt,...){(void)fmt;}
static int read_pin(unsigned i,uint32_t *c,uint32_t *v){*c=cfg[i];*v=io[i];return 1;}
#define MARBLE_AUDIO_CLOCK_READ_PIN read_pin
static int marble_glink_control_send_message(unsigned command,unsigned p1,unsigned p2,const uint8_t *p,unsigned n)
{
    if(fail_send)return 0;assert(p1==1);
    if(command==5)assert(p2==1 && n==8 && marble_gpr_u32(p)==512);
    else {assert(command==9 && (n==MARBLE_TDM_RUN_OPEN_BYTES+8 || n==72));sends++;memcpy(last,p+8,n-8);}
    return 1;
}
static int marble_glink_control_send(unsigned c,unsigned p1,unsigned p2)
{assert(c==14 && p1==1 && p2==1);retire++;return !fail_send;}
#include "../../xnuloader/marble_tdm_run_fw.h"
static void reset(void)
{
    memset(&marble_gpr,0,sizeof(marble_gpr));memset(&marble_tdm_run,0,sizeof(marble_tdm_run));
    memset(&marble_glink_control_state,0,sizeof(marble_glink_control_state));sends=retire=fail_send=0;
    marble_glink_control_state.app_mode=2;marble_glink_control_state.app_ack=1;marble_glink_control_state.app_remote=3;marble_glink_control_state.app_intents=5;
    for(unsigned i=0;i<5;i++)marble_glink_control_state.app_intent[i]=(MarbleGlinkRemoteIntent){512,i};
#ifdef XNU_MARBLE_TDM_PCM_GRAPH_ONLY
    marble_glink_control_state.app_intent[0].bytes=1024;
#endif
    for(unsigned i=0;i<7;i++){cfg[i]=1;io[i]=0;}
}
static int done(unsigned stage)
{MarbleGlinkControl m={.command=14,.channel=3,.parameter=marble_tdm_run.id[stage],.bytes=8};return marble_gpr_receive(&m,NULL);}
static int reply(unsigned stage,unsigned error,unsigned corrupt)
{
    uint8_t f[48]={0};marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,32);
    uint8_t *r=f+16;marble_gpr_put32(r,0x2060);marble_gpr_put32(r+4,0xa80203);marble_gpr_put32(r+8,1);marble_gpr_put32(r+12,3);
    marble_gpr_put32(r+16,(MARBLE_TDM_RUN_TOKEN+stage)^corrupt);marble_gpr_put32(r+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(r+24,0x01001000+stage);marble_gpr_put32(r+28,error);
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=48};return marble_gpr_receive(&m,f);
}
int main(void)
{
    reset();
    for(unsigned i=0;i<5;i++) {
        assert(marble_gpr_progress() && sends==i+1 && marble_gpr_u32(last+20)==0x01001000+i);
        assert(reply(i,0,0) && marble_gpr_progress() && sends==i+1); /* must await TX_DONE */
        assert(done(i));assert(!done(i));
    }
    assert(marble_tdm_run.sent==31 && marble_tdm_run.reply==31 && marble_tdm_run.tx==31 && retire==5 && marble_audio_clock_guard(1));
    assert(marble_gpr_progress() && sends==5);
    for(unsigned fail=0;fail<4;fail++) {
        reset();
        for(unsigned i=0;i<=fail;i++){assert(marble_gpr_progress() && done(i) && reply(i,i==fail?2:0,0));}
        unsigned cleanup=fail<2?4:fail+1;
        for(unsigned i=cleanup;i<5;i++){assert(marble_gpr_progress() && marble_gpr_u32(last+20)==0x01001000+i && done(i) && reply(i,0,0));}
        assert(marble_tdm_run.next==5 && !marble_tdm_run.rejected);
    }
    reset();assert(marble_gpr_progress() && reply(0,0,1) && retire==1 && marble_tdm_run.rejected && marble_gpr_progress() && sends==1);
    reset();io[0]=2;assert(!marble_gpr_progress() && !sends);
    reset();marble_glink_control_state.app_intents=4;assert(marble_gpr_progress() && !sends);
    reset();fail_send=1;assert(!marble_gpr_progress() && !sends);
    puts("PASS actual silent TDM lifecycle state machine: distinct intents; replies and TX_DONE gate advancement; failure only STOP/CLOSE; malformed retirement; pin guards; no retries");
}
