/* Marble APSS DRV2/TCS0 diagnostic lease. Register layout: pinned Xiaomi
 * rpmh-rsc.c; read MSGID=0x108 also checked against RPMh HAL ReadRequest.
 * Only ARC reads are issued. A response is not a voltage measurement. */
#ifndef MARBLE_GMU_RPMH_H
#define MARBLE_GMU_RPMH_H
#include <stdint.h>
#define MARBLE_GMU_RPMH_ABI "MGPU-MARBLE-GMU-RPMH-v1"
#define MARBLE_RPMH_PHYS UINT64_C(0x17a20d00)
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,uint32_t *);
    int (*write)(void *,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*guard)(void *);
} MarbleRpmhIo;
typedef struct {
    uint32_t before[7],after[7],response[8],cmd_status[8];
    unsigned reads,completed,changed,restored,retain,refused,phase;
} MarbleRpmhResult;
static const unsigned marble_rpmh_saved[7]={0,0x10,0x14,0x1c,0x30,0x34,0x38};
static int marble_rpmh_access(unsigned reg,unsigned bytes,unsigned write)
{
    if(bytes!=4)return 0;
    if(write && reg==8)return 1;
    if(!write && (reg==4 || reg==0x18 || reg==0x3c || reg==0x40))return 1;
    for(unsigned i=0;i<7;i++)if(reg==marble_rpmh_saved[i])return 1;
    return 0;
}
static int marble_rpmh_poll(const MarbleRpmhIo *io,unsigned reg,uint32_t mask,uint32_t want)
{
    uint32_t value;
    for(unsigned i=0;i<=2000;i++) {
        if(io->read(io->cookie,reg,&value))return 0;
        if((value&mask)==want)return 1;
        io->delay(io->cookie,10);
    }
    return 0;
}
/* Four command-DB ARC addresses, each read twice. Do not expose arbitrary
 * packets or allow a caller to switch the command to WRITE. */
static int marble_rpmh_snapshot_class(const MarbleRpmhIo *io,const uint32_t address[4],unsigned slave,MarbleRpmhResult *r)
{
    if(!r)return 0;*r=(MarbleRpmhResult){0};
    if(!io || !io->read || !io->write || !io->delay || !io->guard || !address || (slave!=3 && slave!=5)) {r->refused=1;return 0;}
    void *c=io->cookie;uint32_t idle,irq;
    for(unsigned i=0;i<4;i++) {
        if((address[i]&0xffff0003u)!=(slave<<16)) {r->refused=1;return 0;}
        for(unsigned j=0;j<i;j++)if(address[i]==address[j]) {r->refused=1;return 0;}
    }
    if(io->guard(c) || io->read(c,0x18,&idle) || idle!=1 || io->read(c,4,&irq) || (irq&1)) {r->refused=1;return 0;}
    for(unsigned i=0;i<7;i++)if(io->read(c,marble_rpmh_saved[i],r->before+i)) {r->refused=1;return 0;}
    /* Loader touch supply initialization is finished before a kernel runs.
     * Refuse any active/armed owner, even when STATUS happens to be idle. */
    if(r->before[2] || io->guard(c)) {r->refused=1;return 0;}
    r->changed=1;r->retain=1;r->phase=1;
    /* IRQ_STATUS is gated by IRQ_ENABLE on this RSC. Poll it with TCS0
     * enabled, as Xiaomi's ACTIVE_TCS path does; the guest has no RSC ISR. */
    if(io->write(c,0,r->before[0]|1u))goto cleanup;
    for(unsigned i=0;i<8;i++) {
        if(io->guard(c) || !marble_rpmh_poll(io,0x18,1,1))goto cleanup;
        if(io->write(c,0x14,0) || io->write(c,0x1c,0) || io->write(c,8,1) ||
           io->write(c,0x10,1) || io->write(c,0x30,0x108) ||
           io->write(c,0x34,address[i/2]) || io->write(c,0x38,0) ||
           io->write(c,0x1c,1) || io->write(c,0x14,0x10000))goto cleanup;
        r->phase=2;
        /* Assume a failed trigger write may have reached the device. Never
         * restore packet registers while its execution remains uncertain. */
        if(io->write(c,0x14,0x1010000) || !marble_rpmh_poll(io,4,1,1) ||
           !marble_rpmh_poll(io,0x18,1,1))return 0;
        r->phase=3;
        if(io->read(c,0x3c,r->cmd_status+i) || io->read(c,0x40,r->response+i))goto cleanup;
        r->reads++;
        if((r->cmd_status[i]&0x10100)!=0x10100)goto cleanup;
        r->completed++;
    }
cleanup:
    if(r->phase==2)return 0;
    r->phase=4;unsigned safe=1;
    if(!marble_rpmh_poll(io,0x18,1,1))return 0;
    if(io->write(c,0x14,0x10000) || io->write(c,0x14,0) ||
       io->write(c,0x1c,0) || io->write(c,8,1))return 0;
    for(unsigned i=4;i<7;i++)if(io->write(c,marble_rpmh_saved[i],r->before[i]))safe=0;
    if(io->write(c,0x10,r->before[1]) || io->write(c,0x1c,r->before[3]) ||
       io->write(c,0,r->before[0]))safe=0;
    for(unsigned i=0;i<7;i++) {
        if(io->read(c,marble_rpmh_saved[i],r->after+i) || r->before[i]!=r->after[i])safe=0;
    }
    if(io->read(c,4,&irq) || (irq&1) || io->guard(c))safe=0;
    r->restored=safe;r->retain=!safe;r->phase=5;
    unsigned stable=1;
    for(unsigned i=0;i<4;i++)if(r->response[2*i]!=r->response[2*i+1])stable=0;
    return r->completed==8 && stable && safe;
}
static int marble_rpmh_snapshot(const MarbleRpmhIo *io,const uint32_t address[4],MarbleRpmhResult *r)
{return marble_rpmh_snapshot_class(io,address,3,r);}
#endif
