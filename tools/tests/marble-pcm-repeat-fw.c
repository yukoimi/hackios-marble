#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../xnuloader/marble_pcm_data.h"
#include "../../xnuloader/marble_glink_data.h"
static struct {unsigned sent,tx,reply,id[5];} marble_tdm_run;
static struct {unsigned sent,id[2];uint32_t handle;} marble_pcm_memory;
static struct {unsigned app_remote,app_intents;MarbleGlinkRemoteIntent app_intent[8];} marble_glink_control_state;
static unsigned marble_pcm_data_remote_used,marble_pcm_submitted,marble_pcm_data_remote_id,sends,retire,zero=1,faults=1,checks,fail_send;
static void print(const char *f,...){(void)f;}
static int prepare(void){return zero;}
static int faults_ok(void){return faults;}
static int recheck(void){checks++;return zero && faults;}
#define XNU_MARBLE_AUDIO_PCM_REPEAT 1
#define MARBLE_PCM_DATA_PREPARE prepare
#define MARBLE_PCM_DATA_FAULTS faults_ok
#define MARBLE_PCM_REPEAT_RECHECK recheck
static unsigned marble_pcm_memory_ready(void){return 1;}
static int marble_glink_control_send_message(unsigned c,unsigned a,unsigned b,const uint8_t *p,unsigned n)
{
    assert(c==9 && a==1 && b==5 && n==76 && marble_gpr_u32(p+40)==0x80001234);
    assert(marble_gpr_u32(p+24)==MARBLE_PCM_DATA_TOKEN+sends);
    assert(marble_gpr_u32(p+44)==MARBLE_PCM_DATA_BYTES);
    if(fail_send)return 0;sends++;return 1;
}
static int marble_glink_control_send(unsigned c,unsigned a,unsigned b)
{assert(c==14 && a==1 && b==1);retire++;return 1;}
#include "../../xnuloader/marble_pcm_data_fw.h"
static void reset(void)
{
    memset(&marble_pcm_data,0,sizeof(marble_pcm_data));memset(&marble_pcm_repeat,0,sizeof(marble_pcm_repeat));
    sends=retire=checks=fail_send=marble_pcm_submitted=marble_pcm_data_remote_used=0;zero=faults=1;
    marble_tdm_run.sent=marble_tdm_run.tx=marble_tdm_run.reply=7;
    for(unsigned i=0;i<3;i++)marble_tdm_run.id[i]=i+1;
    marble_pcm_memory.sent=1;marble_pcm_memory.id[0]=4;marble_pcm_memory.handle=0x80001234;
    marble_glink_control_state.app_remote=3;marble_glink_control_state.app_intents=8;
    for(unsigned i=0;i<8;i++)marble_glink_control_state.app_intent[i]=(MarbleGlinkRemoteIntent){128,i+1};
}
static int reply(unsigned index,unsigned error,unsigned bad_handle)
{
    uint8_t f[72]={0};marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,56);
    uint8_t *p=f+16;marble_gpr_put32(p,0x3860);marble_gpr_put32(p+4,0x203);marble_gpr_put32(p+8,0x6002);marble_gpr_put32(p+12,3);
    marble_gpr_put32(p+16,MARBLE_PCM_DATA_TOKEN+index);marble_gpr_put32(p+20,0x05001004);marble_gpr_put32(p+24,0x10000000);marble_gpr_put32(p+28,1);marble_gpr_put32(p+32,0x80001234^bad_handle);marble_gpr_put32(p+36,error);
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=72};return marble_pcm_data_receive(&m,f);
}
static int done(unsigned c)
{MarbleGlinkControl d={.command=c,.channel=3,.parameter=5,.bytes=8};return marble_pcm_data_receive(&d,0);}
int main(void)
{
#if XNU_MARBLE_AUDIO_PCM_PERIODS == 8
    for(unsigned order=0;order<256;order++) {
        reset();assert(marble_pcm_data_progress(3) && sends==1);
        for(unsigned i=0;i<8;i++) {
            assert(!marble_pcm_data_ready() && !marble_pcm_data_can_stop());
            assert((order&(1u<<i))?done(14):reply(i,0,0));
            assert(marble_pcm_data_progress(3) && sends==i+1 && checks==i);
            assert((order&(1u<<i))?reply(i,0,0):done(14));
            assert(marble_pcm_data_progress(3) && marble_pcm_repeat.completed==i+1);
        }
        assert(marble_pcm_data_ready() && marble_pcm_data_can_stop() && sends==8 && checks==7);
        assert(marble_pcm_data_progress(3) && sends==8);
    }
    for(unsigned at=0;at<8;at++)for(unsigned kind=0;kind<5;kind++) {
        reset();assert(marble_pcm_data_progress(3));
        for(unsigned i=0;i<at;i++)assert(reply(i,0,0) && done(14) && marble_pcm_data_progress(3));
        assert(reply(kind==0?(at+7)%8:at,kind==1,kind==2) && done(kind==3?6:14));
        if(kind==4)faults=0;
        assert(marble_pcm_data_progress(3) && !marble_pcm_data_ready() && sends==at+1);
    }
    uint8_t p8[68];assert(!marble_pcm_data_command_index(p8,68,1,8));
    assert(marble_pcm_data_command_index(p8,68,1,7) && marble_gpr_u32(p8+16)==MARBLE_PCM_DATA_TOKEN+7);
    puts("PASS eight bounded silent WREPs: 256 retirement-order combinations, every-position stale/error/handle/nonreuse/fault rejection, seven page reuses and final STOP gate");
    return 0;
#endif
    for(unsigned order=0;order<2;order++) {
        reset();assert(marble_pcm_data_progress(3) && sends==1 && !marble_pcm_data_can_stop());
        assert(order?done(14):reply(0,0,0));
        assert(marble_pcm_data_progress(3) && sends==1 && !checks && !marble_pcm_data_can_stop());
        assert(order?reply(0,0,0):done(14));
        assert(marble_pcm_data_progress(3) && sends==2 && checks==1 && marble_pcm_repeat.completed==1 && !marble_pcm_data_can_stop());
        assert(order?reply(1,0,0):done(14));assert(marble_pcm_data_progress(3) && !marble_pcm_data_ready());
        assert(order?done(14):reply(1,0,0));assert(marble_pcm_data_progress(3));
        assert(marble_pcm_data_ready() && marble_pcm_data_can_stop() && marble_pcm_data.logged && marble_pcm_submitted==2 && marble_pcm_repeat.completed==2);
        assert(marble_pcm_data_progress(3) && sends==2 && checks==1);
    }
    reset();assert(marble_pcm_data_progress(3) && reply(0,0,0) && done(6));
    assert(marble_pcm_data_progress(3) && sends==1 && !checks && marble_pcm_data.bad && !marble_pcm_data_ready());
    for(unsigned kind=0;kind<4;kind++) {
        reset();assert(marble_pcm_data_progress(3) && reply(0,0,0) && done(14));
        assert(marble_pcm_data_progress(3) && sends==2);
        assert(reply(kind==0?0:1,kind==1,kind==2) && done(14));
        if(kind==3)faults=0;
        assert(marble_pcm_data_progress(3) && !marble_pcm_data_ready());
        assert(marble_pcm_data.bad || marble_pcm_data.result.error);
    }
    reset();assert(marble_pcm_data_progress(3) && reply(0,0,0) && done(14));zero=0;
    assert(marble_pcm_data_progress(3) && sends==1 && marble_pcm_data.bad && !marble_pcm_data_ready());
    reset();assert(marble_pcm_data_progress(3) && done(14) && done(14) && reply(0,0,0));
    assert(marble_pcm_data_progress(3) && sends==1 && marble_pcm_data.bad);
    reset();fail_send=1;assert(!marble_pcm_data_progress(3) && !sends && !marble_pcm_submitted);
    uint8_t p[68];assert(!marble_pcm_data_command_index(p,68,1,2));
    assert(marble_pcm_data_command_index(p,68,1,1) && marble_gpr_u32(p+16)==MARBLE_PCM_DATA_TOKEN+1);
    puts("PASS two serial silent WREPs: distinct tokens, both retirement orders, no premature page reuse, RX_DONE reuse requirement, stale/malformed/error rejection, faults and final STOP gate");
}
