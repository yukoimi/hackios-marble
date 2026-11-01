/* Execute the authenticated ZAP transition through CP; never write TRUST_CNTL
 * from the CPU. RAM remains leased until the caller's real GX/CX drain. */
#ifndef MARBLE_GPU_SECURE_H
#define MARBLE_GPU_SECURE_H
#include "marble_gpu_cp.h"
typedef struct {
    uint32_t before,after,rptr,wptr,shadow,fence,status,status3,irq,fault;
    unsigned submitted,completed,polls,phase;
} MarbleSecureResult;
static int marble_secure_run(const MarbleCpIo *io,volatile uint32_t *ring,
    volatile uint32_t *mem,const MarbleCpResult *cp,unsigned authenticated,
    MarbleSecureResult *r)
{
    if(!r)return 0;*r=(MarbleSecureResult){0};
    if(!io || !io->read || !io->write || !io->barrier || !io->health || !io->delay ||
        !ring || !mem || !cp || !authenticated || !cp->completed || cp->halt_requested ||
        cp->rptr!=21 || cp->wptr!=21 || cp->fence!=1 || io->health(io->cookie))return 0;
    /* TRUST_CNTL may remain zero across a warm GX collapse. Still submit the
     * authenticated command and require its fence; only before=1 proves a
     * fresh transition. Neither state permits a CPU write to this register. */
    if(io->read(io->cookie,0xf400,&r->before) || r->before>1)return 0;
    uint32_t p[7];
    if(AdrenoPM4Type7(0x66,1,p) || AdrenoPM4Type7(0x46,4,p+2))return 0;
    p[1]=0;p[3]=0x08000031;p[4]=0x300004;p[5]=0;p[6]=2;
    AdrenoPM4Packet plan[2];size_t count=0;
    if(AdrenoPM4Plan(p,7,plan,2,&count) || count!=2)return 0;
    for(unsigned i=0;i<7;i++)ring[21+i]=p[i];
    io->barrier(io->cookie);r->submitted=1;r->phase=1;
    if(io->write(io->cookie,0x807,28))return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;io->barrier(io->cookie);r->shadow=mem[0];r->fence=mem[1];
        if(io->health(io->cookie) || io->read(io->cookie,0x806,&r->rptr) ||
            io->read(io->cookie,0x807,&r->wptr) || io->read(io->cookie,0x201,&r->irq) ||
            io->read(io->cookie,0x821,&r->fault))return 0;
        if(r->fault || (r->irq&0x138003c2u))return 0;
        if(r->rptr==28 && r->wptr==28 && r->shadow==28 && r->fence==2) {
            r->completed=1;r->phase=2;break;
        }
        io->delay(io->cookie,10);
    }
    if(io->read(io->cookie,0xf400,&r->after) || io->read(io->cookie,0x210,&r->status) ||
        io->read(io->cookie,0x213,&r->status3))return 0;
    return r->completed && !r->after && !(r->status&~1u) && !r->status3 && !io->health(io->cookie);
}
#endif
