/* Software fixtures only; responses below are deliberately synthetic. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_hfi.h"
static uint32_t shared[4096] __attribute__((aligned(16384)));
static AdrenoHfi h;
typedef struct {unsigned mode,us,notify,writes,reads,fail_write,fail_read;uint32_t ctrl,irq;} Device;
static Device d;
static int read_reg(void *c,unsigned a,uint32_t *v)
{
    Device *p=c;assert(marble_hfi_access(a,0));if(++p->reads==p->fail_read)return -1;
    *v=a==0x16600?p->ctrl:a==0x16070?0x100:p->irq;
    if(p->mode==2 && p->notify && a==0x16070)*v=0x300;
    return 0;
}
static int write_reg(void *c,unsigned a,uint32_t v)
{
    Device *p=c;assert(marble_hfi_access(a,1));if(++p->writes==p->fail_write)return -1;
    if(a==0x16618){assert(v==1);if(p->mode!=1)p->ctrl=1;}
    if(a==0x16644){assert(v==1);p->irq&=~v;}
    return 0;
}
static int notify(void *c)
{Device *p=c;++p->notify;return write_reg(c,0x16650,1);}
static void delay_us(void *c,unsigned us)
{
    Device *p=c;p->us+=us;assert(p->us<=300030);
    if(!h.pending_header || p->mode==3)return;
    unsigned id=h.pending_header&255;
    assert(id==4 || id==3 || id==14 || id==30 || id==33 || id==10);
    assert((h.pending_header&255)==id);
    assert(((h.pending_header>>8)&255)==(id==4?59:id==3?160:id==14?2:id==30?4:id==10?1:3));
    volatile uint32_t *cmd=shared+6,*msg=shared+18;
    cmd[10]=cmd[11];unsigned at=msg[11];volatile uint32_t *q=shared+0x1350/4;
    q[at]=0x10300|id;q[at+1]=h.pending_header;q[at+2]=p->mode==4?5:0;
    if(p->mode==5)q[at]&=~0x10000u;
    msg[11]=(at+4)&1023;p->irq|=1;
}
static int health(void *c){(void)c;return 0;}
static void init(unsigned mode)
{
    memset(&d,0,sizeof(d));d.mode=mode;
    assert(!AdrenoHfiInitializeStoppedProfile(&h,shared,sizeof(shared),0x60006000,&d,notify,AdrenoHfiProfileMarbleGen7));
}
int main(void)
{
    uint32_t perf[58]={0},bw[159]={0};MarbleHfiResult r;
    MarbleHfiIo io={&d,read_reg,write_reg,delay_us,health};
    init(0);assert(marble_hfi_tables(&io,&h,perf,bw,&r));
    assert(r.ready && r.acks==2 && r.commands==2 && !r.failed_stage && d.notify==2);
    assert(r.ack_return[0]==0x103b04 && r.ack_return[1]==0x20a003);
    unsigned nr=d.reads,nw=d.writes;
    assert(marble_hfi_core_start(&io,&h,&r));
    assert(r.acks==1 && r.header==0x30020e && r.ack_return[0]==0x30020e && d.notify==3);
    assert(marble_hfi_low_vote(&io,&h,0,&r) && r.header==0x40041e && r.acks==1);
    assert(marble_hfi_low_vote(&io,&h,1,&r) && r.header==0x500321 && r.acks==1);
    init(0);h.sequence=4;h.last_ack_return=0x40041e;
    assert(!marble_hfi_gpu_start(&io,&h,0,&r) && !d.writes);
    assert(marble_hfi_gpu_start(&io,&h,1,&r) && r.header==0x50010a && r.acks==1);
    unsigned start_nr=d.reads,start_nw=d.writes;
    assert(marble_hfi_low_vote(&io,&h,1,&r) && r.header==0x600321 && r.acks==1);
    for(unsigned n=1;n<=start_nr;n++){init(0);h.sequence=4;h.last_ack_return=0x40041e;d.fail_read=n;assert(!marble_hfi_gpu_start(&io,&h,1,&r));}
    for(unsigned n=1;n<=start_nw;n++){init(0);h.sequence=4;h.last_ack_return=0x40041e;d.fail_write=n;assert(!marble_hfi_gpu_start(&io,&h,1,&r));}
    for(unsigned mode=2;mode<=5;mode++){init(mode);h.sequence=4;h.last_ack_return=0x40041e;assert(!marble_hfi_gpu_start(&io,&h,1,&r));}
    init(0);h.sequence=5;assert(!marble_hfi_low_vote(&io,&h,1,&r) && !d.writes);
    init(0);assert(!marble_hfi_core_start(&io,&h,&r) && !d.notify);
    for(unsigned n=1;n<=nr;n++){init(0);d.fail_read=n;assert(!marble_hfi_tables(&io,&h,perf,bw,&r));}
    for(unsigned n=1;n<=nw;n++){init(0);d.fail_write=n;assert(!marble_hfi_tables(&io,&h,perf,bw,&r));}
    for(unsigned mode=1;mode<=5;mode++) {init(mode);assert(!marble_hfi_tables(&io,&h,perf,bw,&r));}
    init(0);h.sequence=1;assert(!marble_hfi_tables(&io,&h,perf,bw,&r) && !d.writes);
    for(unsigned a=0;a<0x37000;a+=4) {
        assert(marble_hfi_access(a,1)==(a==0x16618 || a==0x16650 || a==0x16644));
        assert(marble_hfi_access(a,0)==(a==0x16600 || a==0x16070 || a==0x16648 || a==0x16658));
    }
    printf("HFI fixtures: two synthetic ACKs, %u read/%u write failures, init/ACK timeouts, NMI, firmware error, non-ACK rejection, MMIO fences PASS; no hardware claim\n",nr,nw);
}
