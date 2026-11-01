#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_audio_clock.h"
#define MARBLE_TDM_GRAPH_OPEN_BYTES 76u
#include "../../xnuloader/marble_glink_data.h"
static struct {unsigned advertised,sent,done,replies;MarbleGlinkAssembly rx;} marble_gpr;
static struct {unsigned app_mode,app_ack,app_remote,app_intents;MarbleGlinkRemoteIntent app_intent[3];} marble_glink_control_state;
static unsigned sends[15],fail_send;
static uint8_t requests[2][MARBLE_TDM_GRAPH_OPEN_BYTES];
static void print(const char *fmt,...){(void)fmt;}
static int marble_glink_control_send_message(unsigned command,unsigned p1,unsigned p2,const uint8_t *p,unsigned n)
{
    if(fail_send)return 0;
    assert(p1==1 && command<15);sends[command]++;
    if(command==5)assert(p2==1 && n==8 && marble_gpr_u32(p)==512 && marble_gpr_u32(p+4)==1);
    else {assert(command==9 && (n==MARBLE_TDM_GRAPH_OPEN_BYTES+8 || n==72) && marble_gpr_u32(p)==n-8 && !marble_gpr_u32(p+4));assert(sends[9]<=2);memcpy(requests[sends[9]-1],p+8,n-8);}
    return 1;
}
static int marble_glink_control_send(unsigned c,unsigned p1,unsigned p2)
{return marble_glink_control_send_message(c,p1,p2,NULL,0);}
/* RX_DONE has no payload. */
#define marble_glink_control_send marble_cycle_rx_done
static int marble_cycle_rx_done(unsigned c,unsigned p1,unsigned p2)
{assert(c==14 && p1==1 && p2==1);if(fail_send)return 0;sends[14]++;return 1;}
static uint32_t pin_cfg[7],pin_io[7];
static unsigned read_count,read_fail;
static int read_pin(unsigned i,uint32_t *cfg,uint32_t *io)
{assert(i<7);read_count++;if(read_fail)return 0;*cfg=pin_cfg[i];*io=pin_io[i];return 1;}
#define MARBLE_AUDIO_CLOCK_READ_PIN read_pin
#include "../../xnuloader/marble_audio_clock_fw.h"
static void reset(void)
{
    memset(&marble_gpr,0,sizeof(marble_gpr));memset(&marble_audio_clock,0,sizeof(marble_audio_clock));
    memset(&marble_glink_control_state,0,sizeof(marble_glink_control_state));memset(sends,0,sizeof(sends));fail_send=0;read_fail=read_count=0;for(unsigned i=0;i<7;i++){pin_cfg[i]=1;pin_io[i]=0;}
    marble_glink_control_state.app_mode=2;marble_glink_control_state.app_ack=1;marble_glink_control_state.app_remote=3;
    marble_glink_control_state.app_intent[0]=(MarbleGlinkRemoteIntent){128,0};marble_glink_control_state.app_intent[1]=(MarbleGlinkRemoteIntent){512,1};
}
static int done(unsigned id)
{MarbleGlinkControl m={.command=14,.channel=3,.parameter=id,.bytes=8};return marble_gpr_receive(&m,NULL);}
static int reply(unsigned stage,unsigned error,unsigned corrupt)
{
    uint8_t f[48]={0};marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,32);
    uint8_t *r=f+16;marble_gpr_put32(r,0x2060);marble_gpr_put32(r+4,0xa80203);marble_gpr_put32(r+8,2);marble_gpr_put32(r+12,7);marble_gpr_put32(r+16,(stage?MARBLE_AUDIO_CLOCK_RELEASE_TOKEN:MARBLE_AUDIO_CLOCK_REQUEST_TOKEN)^corrupt);
    marble_gpr_put32(r+20,stage?0x02001003:0x02001002);marble_gpr_put32(r+24,0x0800102c);marble_gpr_put32(r+28,error);
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=48};return marble_gpr_receive(&m,f);
}
int main(void)
{
    reset();marble_glink_control_state.app_intents=1;assert(marble_gpr_progress() && sends[5]==1 && !sends[9]);
    marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && sends[9]==1);
    assert(reply(0,0,0) && sends[14]==1);assert(marble_gpr_progress() && sends[9]==1);
    assert(done(0) && marble_gpr_progress() && sends[9]==2);assert(marble_gpr_u32(requests[1]+60)==0x204);
    assert(done(1) && reply(1,0,0) && marble_audio_clock.release_reply && marble_gpr_progress() && sends[9]==2 && sends[14]==2);assert(!done(1));
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && done(0) && reply(0,2,0) && marble_gpr_progress() && sends[9]==2); /* release clock even after request error */
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && reply(0,0,1));assert(marble_audio_clock.rejected && sends[14]==1 && marble_gpr_progress() && sends[9]==1);
    reset();fail_send=1;assert(!marble_gpr_progress() && !marble_gpr.sent);
    assert(marble_audio_clock_guard(1));pin_io[3]=1;assert(marble_audio_clock_guard(1)); /* live input may change */
    pin_io[3]=2;assert(!marble_audio_clock_guard(1));
    reset();marble_glink_control_state.app_intents=2;pin_io[0]=2;assert(!marble_gpr_progress() && !marble_gpr.sent);
    reset();marble_glink_control_state.app_intents=2;pin_cfg[3]=5;assert(!marble_gpr_progress() && !marble_gpr.sent);
    reset();marble_glink_control_state.app_intents=2;read_fail=1;assert(!marble_gpr_progress() && !marble_gpr.sent);
    reset();marble_glink_control_state.app_intents=2;marble_glink_control_state.app_intent[1].bytes=64;marble_glink_control_state.app_intent[0].bytes=64;assert(marble_gpr_progress() && !read_count && !marble_gpr.sent);
    puts("PASS actual PRM clock state machine and passive GPIO guards: wait for large open intent and reserve distinct close intent; wait TX_DONE; close even after open error; malformed reply retirement preserves stop service; no retry/START");
}
