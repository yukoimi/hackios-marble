#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_amdb_cycle.h"
#include "../../xnuloader/marble_glink_data.h"
static struct {unsigned advertised,sent,done,replies;MarbleGlinkAssembly rx;} marble_gpr;
static struct {unsigned app_mode,app_ack,app_remote,app_intents;MarbleGlinkRemoteIntent app_intent[3];} marble_glink_control_state;
static unsigned sends[15],fail_send;
static uint8_t requests[2][64];
static void print(const char *fmt,...){(void)fmt;}
static int marble_glink_control_send_message(unsigned command,unsigned p1,unsigned p2,const uint8_t *p,unsigned n)
{
    if(fail_send)return 0;
    assert(p1==1 && command<15);sends[command]++;
    if(command==5)assert(p2==1 && n==8 && marble_gpr_u32(p)==512 && marble_gpr_u32(p+4)==1);
    else {assert(command==9 && n==72 && marble_gpr_u32(p)==64 && !marble_gpr_u32(p+4));assert(sends[9]<=2);memcpy(requests[sends[9]-1],p+8,64);}
    return 1;
}
static int marble_glink_control_send(unsigned c,unsigned p1,unsigned p2)
{return marble_glink_control_send_message(c,p1,p2,NULL,0);}
/* RX_DONE has no payload. */
#define marble_glink_control_send marble_cycle_rx_done
static int marble_cycle_rx_done(unsigned c,unsigned p1,unsigned p2)
{assert(c==14 && p1==1 && p2==1);if(fail_send)return 0;sends[14]++;return 1;}
#include "../../xnuloader/marble_amdb_cycle_fw.h"
static void reset(void)
{
    memset(&marble_gpr,0,sizeof(marble_gpr));memset(&marble_amdb_cycle,0,sizeof(marble_amdb_cycle));
    memset(&marble_glink_control_state,0,sizeof(marble_glink_control_state));memset(sends,0,sizeof(sends));fail_send=0;
    marble_glink_control_state.app_mode=2;marble_glink_control_state.app_ack=1;marble_glink_control_state.app_remote=3;
    marble_glink_control_state.app_intent[0]=(MarbleGlinkRemoteIntent){128,0};marble_glink_control_state.app_intent[1]=(MarbleGlinkRemoteIntent){128,1};
}
static int done(unsigned id)
{MarbleGlinkControl m={.command=14,.channel=3,.parameter=id,.bytes=8};return marble_gpr_receive(&m,NULL);}
static int reply(unsigned stage,unsigned error,uint64_t handle,unsigned corrupt)
{
    uint8_t f[64]={0};unsigned bytes=stage?32:48;
    marble_gpr_put32(f,9|3u<<16);marble_gpr_put32(f+4,1);marble_gpr_put32(f+8,bytes);
    uint8_t *r=f+16;marble_gpr_put32(r,bytes<<8|0x60);marble_gpr_put32(r+4,0x203);marble_gpr_put32(r+8,3);marble_gpr_put32(r+12,3);marble_gpr_put32(r+16,(stage?MARBLE_AMDB_UNLOAD_TOKEN:MARBLE_AMDB_LOAD_TOKEN)^corrupt);
    marble_gpr_put32(r+20,stage?MARBLE_GPR_BASIC_REPLY:MARBLE_AMDB_LOAD_REPLY);
    if(stage){marble_gpr_put32(r+24,MARBLE_AMDB_UNLOAD);marble_gpr_put32(r+28,error);}
    else {marble_gpr_put32(r+24,2);marble_gpr_put32(r+28,error);marble_gpr_put32(r+32,1);marble_gpr_put32(r+36,MARBLE_AMDB_QUERY_MODULE);marble_gpr_put32(r+40,(uint32_t)handle);marble_gpr_put32(r+44,(uint32_t)(handle>>32));}
    MarbleGlinkControl m={.command=9,.channel=3,.parameter=1,.bytes=(16+bytes+7)&~7u};return marble_gpr_receive(&m,f);
}
int main(void)
{
    reset();marble_glink_control_state.app_intents=1;assert(marble_gpr_progress() && sends[5]==1 && !sends[9]);
    marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && sends[9]==1);
    assert(reply(0,0,0x12345678,0) && sends[14]==1);assert(marble_gpr_progress() && sends[9]==1); /* no reuse before completion */
    assert(done(0) && marble_gpr_progress() && sends[9]==2);assert(marble_gpr_u32(requests[1]+56)==0x12345678);
    assert(done(1) && reply(1,0,0,0) && marble_amdb_cycle.unload_reply && marble_gpr_progress() && sends[9]==2 && sends[14]==2);
    assert(!done(1));
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && done(0) && reply(0,2,0,0) && marble_gpr_progress() && sends[9]==1 && !marble_amdb_cycle.handle);
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && done(0) && reply(0,2,0x1234,0) && marble_gpr_progress() && sends[9]==2); /* release even partial failure with handle */
    reset();marble_glink_control_state.app_intents=2;assert(marble_gpr_progress() && reply(0,0,0x1234,1));assert(marble_amdb_cycle.rejected && sends[14]==1 && marble_gpr_progress() && sends[9]==1); /* retire invalid owned packet; preserve stop service */
    reset();fail_send=1;assert(!marble_gpr_progress() && !marble_gpr.sent);
    puts("PASS AMDB actual firmware state machine: reserve distinct release intent, wait TX_DONE, exact opaque handle release, absent/partial failure, malformed reply retirement without guessed unload, no retries");
}
