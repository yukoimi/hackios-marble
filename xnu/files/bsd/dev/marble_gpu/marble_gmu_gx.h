/* Gen7 GPU OOB handshake; teardown follows Xiaomi pwrctrl_suspend and the
 * marble DT's GX gdsc-regulator reset-AON/clamp/software-collapse path.
 * No GPU memory or CP commands are issued. All mappings/clocks/CX retained
 * until CM3 reset, BOTH GBIF drains and GX collapse are actually observed. */
#ifndef MARBLE_GMU_GX_H
#define MARBLE_GMU_GX_H
#include <stdint.h>
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,unsigned,uint32_t *);
    int (*write)(void *,unsigned,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*health)(void *);
} MarbleGxIo;
typedef struct {
    uint32_t before[5],after[5],intr,gfx_status,gdscr,rbbm_status,halt_ack;
    unsigned prepared,requested,acknowledged,powered,halted,collapsed,restored,phase,held;
} MarbleGxResult;
static const unsigned marble_gx_saved[5]={0x905c,0x9504,0x9058,0x9358,0x958c};
static int marble_gx_access(unsigned block,unsigned reg,unsigned write)
{
    if(block==0) return write?(reg==0x16650 || reg==0x16644):
        (reg==0x16000 || reg==0x16648 || reg==0x16340 || reg==0x163b0);
    if(block==1)return write?(reg==0x58 || reg==0x10c):(reg==0x5c || reg==0x840);
    if(block==2)for(unsigned i=0;i<5;i++)if(reg==marble_gx_saved[i])return 1;
    return 0;
}
static int marble_gx_poll(const MarbleGxIo *io,unsigned block,unsigned reg,uint32_t mask,
    uint32_t want,unsigned limit,uint32_t *out)
{
    for(unsigned n=0;n<=limit;n+=10) {
        if(io->read(io->cookie,block,reg,out))return 0;
        if((*out&mask)==want)return 1;
        io->delay(io->cookie,10);
    }
    return 0;
}
static int marble_gx_prepare(const MarbleGxIo *io,MarbleGxResult *r)
{
    if(!r)return 0;*r=(MarbleGxResult){0};
    if(!io || !io->read || !io->write || !io->delay || !io->health || io->health(io->cookie))return 0;
    uint32_t reset;
    if(io->read(io->cookie,0,0x16000,&reset) || reset!=1)return 0;
    for(unsigned i=0;i<5;i++)if(io->read(io->cookie,2,marble_gx_saved[i],r->before+i))return 0;
    /* Software-controlled parked GX, clamp asserted; do not steal HW control. */
    if((r->before[0]&0x80000007u)!=1 || !(r->before[1]&1))return 0;
    r->prepared=1;return 1;
}
static int marble_gx_release(const MarbleGxIo *io,MarbleGxResult *r)
{
    if(!r || !r->requested)return 0;
    if(io->write(io->cookie,0,0x16650,0x80000000u))return 0;
    r->held=0;r->phase=2;return 1;
}
static int marble_gx_acquire(const MarbleGxIo *io,MarbleGxResult *r)
{
    if(!r || !r->prepared || r->requested || io->health(io->cookie) || io->read(io->cookie,0,0x16648,&r->intr) ||
        (r->intr&0x80000000u))return 0;
    r->phase=1;r->requested=1;r->held=1;
    int ok=!io->write(io->cookie,0,0x16650,0x40000000) &&
        marble_gx_poll(io,0,0x16648,0x80000000u,0x80000000u,100000,&r->intr);
    r->acknowledged=ok;
    /* ACK clear acknowledges the interrupt; the separate CLEAR force below
     * must wait until the caller finishes accessing the powered GPU. */
    if(io->write(io->cookie,0,0x16644,0x80000000u))ok=0;
    if(ok && !io->health(io->cookie) && !io->read(io->cookie,0,0x16340,&r->gfx_status) &&
        !io->read(io->cookie,2,0x905c,&r->gdscr) && !(r->gfx_status&3) &&
        (r->gdscr&0x80000000u)) {
        r->powered=1;
        if(io->read(io->cookie,1,0x840,&r->rbbm_status))ok=0;
    } else ok=0;
    if(!ok)marble_gx_release(io,r);
    return ok && r->powered;
}
static int marble_gx_oob(const MarbleGxIo *io,MarbleGxResult *r)
{
    int ok=marble_gx_acquire(io,r);
    if(r->requested && !marble_gx_release(io,r))ok=0;
    return ok;
}
static int marble_gx_stop(const MarbleGxIo *io,MarbleGxResult *r,unsigned phase)
{
    if(!r->prepared)return 0;
    uint32_t reset,gx;
    if(io->read(io->cookie,0,0x16000,&reset) || reset!=1 ||
        io->read(io->cookie,2,0x905c,&gx))return -1;
    if(!phase) {
        if((gx&0x80000000u) && (io->write(io->cookie,1,0x58,1) ||
            !marble_gx_poll(io,1,0x5c,1,1,100000,&r->halt_ack)))return -1;
        r->halted=1;return 0;
    }
    if(phase!=1 || !r->halted)return -1;
    if(gx&0x80000000u) {
        if(io->write(io->cookie,1,0x10c,1))return -1;
        io->delay(io->cookie,100);
        if(gx&2)return -1;
        for(unsigned i=2;i<5;i++)if(io->write(io->cookie,2,marble_gx_saved[i],r->before[i]|1))return -1;
        io->delay(io->cookie,1);
        for(unsigned i=2;i<5;i++)if(io->write(io->cookie,2,marble_gx_saved[i],r->before[i]&~1u))return -1;
        if(io->write(io->cookie,2,0x9504,r->before[1]|0x10))return -1;
        io->delay(io->cookie,1);
        if(io->write(io->cookie,2,0x9504,r->before[1]&~0x10u) ||
            io->write(io->cookie,2,0x9504,r->before[1]&~0x11u) ||
            io->write(io->cookie,2,0x905c,gx&~1u))return -1;
        io->delay(io->cookie,1);
        if(!marble_gx_poll(io,2,0x905c,0x80000000u,0x80000000u,500,&gx))return -1;
        if(io->write(io->cookie,2,0x905c,gx|0x801u))return -1;
        io->delay(io->cookie,1);
        if(!marble_gx_poll(io,2,0x905c,0x80000000u,0,500,&gx) ||
            io->write(io->cookie,2,0x9504,r->before[1]|1))return -1;
    }
    r->collapsed=!(gx&0x80000000u);
    if(!r->collapsed)return -1;
    for(unsigned i=0;i<5;i++)if(io->write(io->cookie,2,marble_gx_saved[i],r->before[i]))return -1;
    for(unsigned i=0;i<5;i++)if(io->read(io->cookie,2,marble_gx_saved[i],r->after+i) ||
        ((r->before[i]^r->after[i])&(i?~0u:0x7fffffffu)))return -1;
    if(r->after[0]&0x80000000u)return -1;
    r->restored=1;r->phase=3;return 0;
}
#endif
