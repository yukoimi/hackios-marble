/* Xiaomi 48952ed3 Gen7 cold start, before HFI/CORE_FW_START/GX.
 * Hold CX, measured clocks, sufficient ARC/BCM and SID5/CB0 until stopped.
 * retain forbids releasing ANY enclosing resource lease. */
#ifndef MARBLE_GMU_BOOT_H
#define MARBLE_GMU_BOOT_H
#include <stdint.h>
#define MARBLE_GMU_BOOT_ABI "MGPU-MARBLE-GMU-BOOT-v1"
#define MARBLE_BOOT_REGS 20
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,unsigned,uint32_t *);
    int (*write)(void *,unsigned,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*health)(void *);
    /* Optional GX cleanup after CM3 reset: phase0 halt GX requests, then
     * phase1 after the enclosing CX GBIF drain. Failure retains all leases. */
    int (*stop)(void *,unsigned);
} MarbleBootIo;
typedef struct {
    uint32_t before[MARBLE_BOOT_REGS],after[MARBLE_BOOT_REGS],fw_result,last_ack;
    uint32_t irq_before[2],irq_after[2],rsc_before[4],rsc_after[4];
    unsigned configured,reset_released,initialized,stopped,restored,retain,refused,phase;
    unsigned worked,work_ok;
} MarbleBootResult;
/* 0 GMU 3d68000, 1 GPU 3d00000, 2 CX_MISC 3d9e000, 3 RSCC 3d50000. */
static const unsigned marble_boot_blocks[MARBLE_BOOT_REGS]={0,0,2,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0};
static const unsigned marble_boot_regs[MARBLE_BOOT_REGS]={0x163c4,0x163c0,0xe4,0x16070,0x16004,
    0x16614,0x16610,0x26c44,0x160b4,0xf114,0x16738,0x1673c,0x16734,
    0x1503c,0x15000,0x15004,0x1664c,0x26c18,0x26c40,0x16618};
static const uint32_t marble_boot_values[MARBLE_BOOT_REGS]={1,1,1,0,2,0x60006000,1,
    0x80c808a0,0x4052,0,0,0x07030200,0x60002003,1,1,1,0xffffffff,0xffffffff,0,0};
static const unsigned marble_boot_rsc[4]={0x346*4,0x3ee*4,0x496*4,0x53e*4};
static const unsigned marble_boot_irq_status[2]={0x16648,0x26c14};
static const unsigned marble_boot_irq_clear[2]={0x16644,0x26c10};
static int marble_boot_access(unsigned block,unsigned reg,unsigned bytes,unsigned write)
{
    if(bytes!=4 || (reg&3))return 0;
    if(block==0 && reg==0x16000)return 1;
    if(!write && block==1 && reg==0xf118)return 1;
    for(unsigned i=0;i<4;i++)if(!write && block==3 && reg==marble_boot_rsc[i])return 1;
    for(unsigned i=0;i<2;i++)if(block==0 && reg==(write?marble_boot_irq_clear[i]:marble_boot_irq_status[i]))return 1;
    if(write && block==0 && reg==0x26c50)return 1;
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)if(block==marble_boot_blocks[i] && reg==marble_boot_regs[i])return 1;
    return 0;
}
static int marble_boot_poll(const MarbleBootIo *io,unsigned block,unsigned reg,uint32_t mask,
    uint32_t want,unsigned limit,uint32_t *last)
{
    for(unsigned us=0;us<=limit;us+=10) {
        if(io->read(io->cookie,block,reg,last))return 0;
        if((*last&mask)==want)return 1;
        io->delay(io->cookie,10);
    }
    return 0;
}
static int marble_boot_transaction_work(const MarbleBootIo *io,MarbleBootResult *r,unsigned execute,
    int (*work)(void *),void *work_cookie)
{
    if(!r)return 0;
    *r=(MarbleBootResult){0};
    if(!io || !io->read || !io->write || !io->delay || !io->health || execute>1) {r->refused=1;return 0;}
    void *c=io->cookie;uint32_t reset;
    if(io->health(c) || io->read(c,0,0x16000,&reset) || reset!=1) {r->refused=1;return 0;}
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)if(io->read(c,marble_boot_blocks[i],marble_boot_regs[i],r->before+i)) {r->refused=1;return 0;}
    for(unsigned i=0;i<2;i++)if(io->read(c,0,marble_boot_irq_status[i],r->irq_before+i) || r->irq_before[i]) {r->refused=1;return 0;}
    for(unsigned i=0;i<4;i++)if(io->read(c,3,marble_boot_rsc[i],r->rsc_before+i) || !(r->rsc_before[i]&1)) {r->refused=1;return 0;}
    /* Stock marble reports GBIF_HALT=8 with GMU reset and GX collapsed.
     * Accept only that observed parked value (or zero), never a client/
     * arbiter halt owner. KGSL clears the whole register for cold start. */
    if(r->before[3] || (r->before[9]!=0 && r->before[9]!=8) || r->before[18] || r->before[19] || io->health(c)) {r->refused=1;return 0;}
    r->phase=1;r->retain=1;
    if(io->write(c,0,0x1664c,0xffffffff) || io->write(c,0,0x26c18,0xffffffff))goto stop;
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)if(io->write(c,marble_boot_blocks[i],marble_boot_regs[i],marble_boot_values[i]))goto stop;
    r->configured=1;
    if(io->health(c))goto stop;
    if(!execute)goto stop;
    r->phase=2;r->reset_released=1;
    if(io->write(c,0,0x16000,0))goto stop;
    r->initialized=marble_boot_poll(io,0,0x16070,0x1ff,0x100,100000,&r->fw_result);
    if(io->health(c))r->initialized=0;
    if(r->initialized && work) {r->worked=1;r->work_ok=work(work_cookie)==0;}
stop:
    r->phase=3;
    for(unsigned i=0;i<4;i++)if(!marble_boot_poll(io,3,marble_boot_rsc[i],1,1,1000,r->rsc_after+i))return 0;
    if(io->write(c,0,0x26c50,7) || io->write(c,0,0x26c40,0) ||
        io->write(c,0,0x16000,1) || io->read(c,0,0x16000,&reset) || reset!=1)return 0;
    if(io->stop && io->stop(c,0))return 0;
    if(r->reset_released && (io->write(c,1,0xf114,2) ||
        !marble_boot_poll(io,1,0xf118,2,2,100000,&r->last_ack)))return 0;
    if(io->stop && io->stop(c,1))return 0;
    io->delay(c,100);
    if(io->health(c))return 0;
    r->stopped=1;r->phase=4;unsigned safe=1;
    for(unsigned i=0;i<2;i++) {
        uint32_t pending;
        if(io->read(c,0,marble_boot_irq_status[i],&pending) ||
            io->write(c,0,marble_boot_irq_clear[i],pending) ||
            io->read(c,0,marble_boot_irq_status[i],r->irq_after+i) || r->irq_after[i])safe=0;
    }
    for(unsigned i=MARBLE_BOOT_REGS;i-->0;)if(i!=16 && i!=17 &&
        io->write(c,marble_boot_blocks[i],marble_boot_regs[i],r->before[i]))safe=0;
    if(safe)for(unsigned i=16;i<18;i++)if(io->write(c,0,marble_boot_regs[i],r->before[i]))safe=0;
    for(unsigned i=0;i<MARBLE_BOOT_REGS;i++)if(io->read(c,marble_boot_blocks[i],marble_boot_regs[i],r->after+i) || r->after[i]!=r->before[i])safe=0;
    for(unsigned i=0;i<4;i++)if(io->read(c,3,marble_boot_rsc[i],r->rsc_after+i) || !(r->rsc_after[i]&1))safe=0;
    if(io->read(c,0,0x16000,&reset) || reset!=1 || io->health(c))safe=0;
    r->restored=safe;r->retain=!safe;r->phase=5;
    return r->configured && (!execute || r->initialized) && (!work || r->work_ok) && r->stopped && r->restored;
}
static int marble_boot_transaction_mode(const MarbleBootIo *io,MarbleBootResult *r,unsigned execute)
{return marble_boot_transaction_work(io,r,execute,0,0);}
static int marble_boot_transaction(const MarbleBootIo *io,MarbleBootResult *r)
{return marble_boot_transaction_mode(io,r,1);}
#endif
