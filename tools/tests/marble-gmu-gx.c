/* Synthetic MMIO ownership/teardown fixture, never a hardware proof. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_gx.h"
typedef struct {uint32_t gmu[0x20000/4],gpu[0x1000/4],cc[0xa000/4];unsigned nr,nw,fr,fw,mode,drained;} Device;
static Device d;
static int rd(void *c,unsigned b,unsigned a,uint32_t *v)
{
    Device *p=c;assert(marble_gx_access(b,a,0));if(++p->nr==p->fr)return -1;
    if(b==1)assert(p->cc[0x905c/4]&0x80000000u);
    *v=b==0?p->gmu[a/4]:b==1?p->gpu[a/4]:p->cc[a/4];return 0;
}
static int wr(void *c,unsigned b,unsigned a,uint32_t v)
{
    Device *p=c;assert(marble_gx_access(b,a,1));if(++p->nw==p->fw)return -1;
    if(b==1)assert(p->cc[0x905c/4]&0x80000000u);
    if(b==0 && a==0x16650 && v==0x40000000) {
        assert(!p->gmu[0x16000/4]);
        if(p->mode!=1)p->gmu[0x16648/4]|=0x80000000u;
        if(p->mode!=2){p->cc[0x905c/4]|=0x80000000u;p->gmu[0x16340/4]=0;}
    }
    if(b==0 && a==0x16644)p->gmu[0x16648/4]&=~v;
    if(b==1 && a==0x58 && p->mode!=3)p->gpu[0x5c/4]=v;
    if(b==1 && a==0x10c)assert(p->drained && p->gmu[0x16000/4]==1);
    if(b==2 && a==0x905c) {
        assert(p->drained && p->gmu[0x16000/4]==1);
        p->cc[a/4]=(v&0x7fffffffu)|((p->mode==4 || !(v&1))?0x80000000u:0);
        return 0;
    }
    if(b==2)p->cc[a/4]=v;else if(b==1)p->gpu[a/4]=v;
    return 0;
}
static void wait_us(void *c,unsigned us){(void)c;(void)us;}
static int health(void *c){(void)c;return 0;}
static void init(unsigned mode)
{memset(&d,0,sizeof(d));d.mode=mode;d.gmu[0x16000/4]=1;d.gmu[0x16340/4]=3;d.cc[0x905c/4]=1;d.cc[0x9504/4]=1;}
static int run(MarbleGxIo *io,MarbleGxResult *r)
{
    if(!marble_gx_prepare(io,r))return 0;
    d.gmu[0x16000/4]=0;
    int live=marble_gx_oob(io,r);d.gmu[0x16000/4]=1;
    int stop=marble_gx_stop(io,r,0);d.drained=1;
    if(!stop)stop=marble_gx_stop(io,r,1);
    return live && !stop;
}
int main(void)
{
    MarbleGxIo io={&d,rd,wr,wait_us,health};MarbleGxResult r;
    init(0);assert(run(&io,&r));assert(r.powered && r.halted && r.collapsed && r.restored);
    unsigned nr=d.nr,nw=d.nw;
    for(unsigned n=1;n<=nr;n++){init(0);d.fr=n;assert(!run(&io,&r));}
    for(unsigned n=1;n<=nw;n++){init(0);d.fw=n;assert(!run(&io,&r));}
    for(unsigned mode=1;mode<=4;mode++){init(mode);assert(!run(&io,&r));if(mode<=2)assert(r.collapsed && r.restored);}
    init(0);assert(marble_gx_prepare(&io,&r));assert(marble_gx_stop(&io,&r,1)==-1);
    init(0);d.cc[0x905c/4]|=2;assert(!marble_gx_prepare(&io,&r) && !d.nw);
    init(0);d.cc[0x9504/4]=0;assert(!marble_gx_prepare(&io,&r) && !d.nw);
    init(0);d.cc[0x9504/4]=0x3c0011;assert(run(&io,&r) && r.after[1]==0x3c0011);
    init(0);assert(marble_gx_prepare(&io,&r));d.gmu[0x16000/4]=0;
    assert(marble_gx_acquire(&io,&r) && r.held && r.phase==1);
    unsigned held_writes=d.nw;assert(!marble_gx_acquire(&io,&r) && d.nw==held_writes);
    assert(marble_gx_release(&io,&r) && !r.held && r.phase==2);
    for(unsigned b=0;b<4;b++)for(unsigned a=0;a<0x40000;a+=4) {
        unsigned allowed=(b==0 && (a==0x16650 || a==0x16644)) || (b==1 && (a==0x58 || a==0x10c));
        for(unsigned i=0;i<5;i++)allowed|=b==2 && a==marble_gx_saved[i];
        assert(marble_gx_access(b,a,1)==!!allowed);
    }
    printf("GX fixture: %u reads/%u writes injected, OOB/rail/halt/collapse timeouts, CM3-reset and dual-drain ordering, exact MMIO fences PASS; no hardware claim\n",nr,nw);
}
