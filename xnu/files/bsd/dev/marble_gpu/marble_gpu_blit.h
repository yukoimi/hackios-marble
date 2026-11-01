/* Gen7 solid 2D probe, derived from Mesa fd6_blitter.cc/fd6_barrier.cc and
 * a6xx.xml at c515ec0fe77da9f42f390e442379f2d0629d908c. A7xx offsets are
 * essential: SP_A2D_OUTPUT_INFO=a9bf, CCU_CACHE_CNTL=88e5, TPL1=b2d2.
 * Uses only the already leased scratch range, no scanout or arbitrary DMA.
 * Success proves a bounded 2D draw, NOT iOS GPU composition or shaders. */
#ifndef MARBLE_GPU_BLIT_H
#define MARBLE_GPU_BLIT_H
#include "marble_gpu_secure.h"
#define MARBLE_BLIT_SENTINEL 0xa5c39e71u
typedef struct {
    uint32_t words,rptr,wptr,shadow,fence,status,status3,irq,fault;
    uint32_t pixel[3],bad_index,bad_value;
    unsigned submitted,completed,polls,pixels,guards,phase;
} MarbleBlitResult;
typedef struct {uint32_t words[256];unsigned n,ok;} MarbleBlitPackets;
static void marble_blit_reg(MarbleBlitPackets *p,unsigned reg,uint32_t v)
{
    if(p->n>253 || AdrenoPM4Type4(reg,1,p->words+p->n)){p->ok=0;return;}
    p->words[p->n+1]=v;p->n+=2;
}
static void marble_blit_cmd(MarbleBlitPackets *p,unsigned op,unsigned count,const uint32_t *v)
{
    if(p->n+1+count>256 || AdrenoPM4Type7(op,count,p->words+p->n)){p->ok=0;return;}
    p->n++;for(unsigned i=0;i<count;i++)p->words[p->n++]=v[i];
}
static uint32_t marble_blit_expected(unsigned y)
{return y<8?0xffff8000u:y<24?0xfffdfbf7u:0xff0080ffu;}
static int marble_blit_packets_target(MarbleBlitPackets *p,uint32_t target)
{
    if(target!=0x501000 && target!=0x2001000)return 0;
    *p=(MarbleBlitPackets){.ok=1};
    /* Clean before invalidate; Gen7 events have no timestamp payload. */
    static const uint32_t before[]={0x21,0x20,0x19,0x18,0x35};
    for(unsigned i=0;i<5;i++)marble_blit_cmd(p,0x46,1,before+i);
    uint32_t marker=12;marble_blit_cmd(p,0x65,1,&marker);
    marble_blit_cmd(p,0x26,0,0);
    marble_blit_reg(p,0x8e07,1); /* fast clear disabled; no concurrent resolve */
    /* Mesa FD725: four CCUs, depth cache 256K each at 0; color at 1MB.
     * Full color/depth caches. No GMEM address is mapped as system RAM. */
    marble_blit_reg(p,0x88e5,0x80000000u);
    for(unsigned band=0;band<3;band++) {
        uint32_t color=marble_blit_expected(band==0?0:band==1?8:24);
        unsigned top=band==0?0:band==1?8:24,bottom=band==0?8:band==1?24:32;
        marble_blit_reg(p,0x8405,top<<16);
        marble_blit_reg(p,0x8406,31|((bottom-1)<<16));
        marble_blit_reg(p,0x8c2c,(color>>16)&255);
        marble_blit_reg(p,0x8c2d,(color>>8)&255);
        marble_blit_reg(p,0x8c2e,color&255);
        marble_blit_reg(p,0x8c2f,255);
        /* UNORM8 ifmt is ZERO (the old A660 example's 0x10 is invalid). */
        marble_blit_reg(p,0x8c00,0x00f03080);
        marble_blit_reg(p,0x8400,0x00f03080);
        marble_blit_reg(p,0xb2d2,1u<<29); /* 2D texture; no raw-copy */
        marble_blit_reg(p,0xa9bf,0x0000f180); /* float output, UNORM8 */
        marble_blit_reg(p,0x8c01,0);
        marble_blit_reg(p,0x8c17,0x430); /* BGRA8888, linear, WXYZ */
        marble_blit_reg(p,0x8c18,target);
        marble_blit_reg(p,0x8c19,0);
        marble_blit_reg(p,0x8c1a,2); /* 128 byte pitch, encoded /64 */
        marble_blit_reg(p,0x8c34,0); /* no array layers */
        uint32_t label=0x3f;marble_blit_cmd(p,0x46,1,&label);
        uint32_t op=3;marble_blit_cmd(p,0x2c,1,&op);
    }
    uint32_t clean=0x21;marble_blit_cmd(p,0x46,1,&clean);
    clean=0x20;marble_blit_cmd(p,0x46,1,&clean);
    clean=0x31;marble_blit_cmd(p,0x46,1,&clean);
    clean=0x35;marble_blit_cmd(p,0x46,1,&clean);
    marble_blit_cmd(p,0x26,0,0);
    uint32_t fence[]={0x08000031,0x300004,0,3};
    marble_blit_cmd(p,0x46,4,fence);
    AdrenoPM4Packet plan[128];size_t count=0;
    return p->ok && !AdrenoPM4Plan(p->words,p->n,plan,128,&count);
}
static int marble_blit_submit_target(const MarbleCpIo *io,volatile uint32_t *ring,
    volatile uint32_t *mem,volatile uint32_t *scratch,unsigned start,uint32_t fence,
    uint32_t target,MarbleBlitResult *r)
{
    if(!r)return 0;*r=(MarbleBlitResult){0};
    if(!io || !io->read || !io->write || !io->barrier || !io->health || !io->delay ||
        !ring || !mem || !scratch || start>=4096 || fence<3 ||
        mem[0]!=start || mem[1]!=fence-1 || io->health(io->cookie))return 0;
    MarbleBlitPackets p;if(!marble_blit_packets_target(&p,target))return 0;
    /* CPU only seeds a value different from every expected pixel. */
    for(unsigned i=16;i<4096;i++)scratch[i]=MARBLE_BLIT_SENTINEL;
    p.words[p.n-1]=fence;
    for(unsigned i=0;i<p.n;i++)ring[(start+i)%4096]=p.words[i];
    r->words=(start+p.n)%4096;r->phase=1;io->barrier(io->cookie);r->submitted=1;
    if(io->write(io->cookie,0x807,r->words))return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;io->barrier(io->cookie);r->shadow=mem[0];r->fence=mem[1];
        if(io->health(io->cookie) || io->read(io->cookie,0x806,&r->rptr) ||
            io->read(io->cookie,0x807,&r->wptr) || io->read(io->cookie,0x201,&r->irq) ||
            io->read(io->cookie,0x821,&r->fault))return 0;
        if(r->fault || (r->irq&0x138003c2u))return 0;
        if(r->rptr==r->words && r->wptr==r->words && r->shadow==r->words && r->fence==fence) {
            r->completed=1;r->phase=2;break;
        }
        io->delay(io->cookie,10);
    }
    if(io->read(io->cookie,0x210,&r->status) || io->read(io->cookie,0x213,&r->status3))return 0;
    r->pixel[0]=scratch[1024];r->pixel[1]=scratch[1024+16*32];r->pixel[2]=scratch[1024+31*32];
    if(!r->completed || (r->status&~1u) || r->status3)return 0;
    for(unsigned i=16;i<4096;i++) {
        uint32_t want=i>=1024 && i<2048?marble_blit_expected((i-1024)/32):MARBLE_BLIT_SENTINEL;
        if(scratch[i]!=want){r->bad_index=i;r->bad_value=scratch[i];return 0;}
        if(i>=1024 && i<2048)r->pixels++;else r->guards++;
    }
    r->phase=3;return !io->health(io->cookie);
}
static int marble_blit_submit(const MarbleCpIo *io,volatile uint32_t *ring,
    volatile uint32_t *mem,volatile uint32_t *scratch,unsigned start,uint32_t fence,MarbleBlitResult *r)
{return marble_blit_submit_target(io,ring,mem,scratch,start,fence,0x501000,r);}
static int marble_blit_run(const MarbleCpIo *io,volatile uint32_t *ring,
    volatile uint32_t *mem,volatile uint32_t *scratch,const MarbleSecureResult *secure,
    MarbleBlitResult *r)
{
    if(!secure || !secure->completed || secure->after || secure->fence!=2 || secure->wptr!=28) {
        if(r)*r=(MarbleBlitResult){0};return 0;
    }
    return marble_blit_submit(io,ring,mem,scratch,28,3,r);
}
#endif
