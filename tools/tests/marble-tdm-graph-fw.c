#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef TEST_TDM_CONFIG
#include "../../xnuloader/marble_tdm_config.h"
#define MARBLE_TDM_GRAPH_OPEN_BYTES 400u
#define MARBLE_TDM_GRAPH_ENCODER marble_tdm_config_command
#else
#include "../../xnuloader/marble_tdm_graph.h"
#define MARBLE_TDM_GRAPH_OPEN_BYTES 296u
#define MARBLE_TDM_GRAPH_ENCODER marble_tdm_graph_command
#endif
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
#include "../../xnuloader/marble_tdm_graph_fw.h"
static void reset(void)
{
    memset(&marble_gpr,0,sizeof(marble_gpr));memset(&marble_tdm_graph,0,sizeof(marble_tdm_graph));
    memset(&marble_glink_control_state,0,sizeof(marble_glink_control_state));memset(sends,0,sizeof(sends));fail_send=0;
    marble_glink_control_state.app_mode=2;marble_glink_control_state.app_ack=1;marble_glink_control_state.app_remote=3;
    marble_glink_control_state.app_intent[0]=(MarbleGlinkRemoteIntent){128,0};marble_glink_control_state.app_intent[1]=(MarbleGlinkRemoteIntent){512,1};
}
static int done(unsigned id)
{MarbleGlinkControl m={.command=14,.channel=3,.parameter=id,.bytes=8};return marble_gpr_receive(&m,NULL);}
static int reply(unsigned stage,unsigned error,unsigned corrupt)
{
    uint8_t f[48]={0};marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,32);
    uint8_t *r=f+16;marble_gpr_put32(r,0x2060);marble_gpr_put32(r+4,0xa80203);marble_gpr_put32(r+8,1);marble_gpr_put32(r+12,3);marble_gpr_put32(r+16,(stage?MARBLE_TDM_GRAPH_CLOSE_TOKEN:MARBLE_TDM_GRAPH_OPEN_TOKEN)^corrupt);
    marble_gpr_put32(r+20,MARBLE_GPR_BASIC_REPLY);marble_gpr_put32(r+24,stage?0x01001004:0x01001000);marble_gpr_put32(r+28,error);
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=48};return marble_gpr_receive(&m,f);
}
int main(void)
{
    reset();marble_glink_control_state.app_intents=1;assert(marble_gpr_progress() && sends[5]==1 && !sends[9]);
    marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && sends[9]==1);
    assert(reply(0,0,0) && sends[14]==1);assert(marble_gpr_progress() && sends[9]==1);
    assert(done(1) && marble_gpr_progress() && sends[9]==2);assert(marble_gpr_u32(requests[1]+60)==0x4001);
    assert(done(0) && reply(1,0,0) && marble_tdm_graph.close_reply && marble_gpr_progress() && sends[9]==2 && sends[14]==2);assert(!done(0));
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && done(1) && reply(0,2,0) && marble_gpr_progress() && sends[9]==2); /* close private ID even after open error */
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && reply(0,0,1));assert(marble_tdm_graph.rejected && sends[14]==1 && marble_gpr_progress() && sends[9]==1);
    reset();fail_send=1;assert(!marble_gpr_progress() && !marble_gpr.sent);
    puts("PASS actual TDM graph firmware state machine: wait for large open intent and reserve distinct close intent; wait TX_DONE; close even after open error; malformed reply retirement preserves stop service; no retry/START");
}
