/* Synthetic MMIO/DMA fixture, never GPU execution evidence. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_cp.h"
typedef struct {uint32_t regs[0x4000],ring[4096],mem[16],scratch[16];unsigned reads,writes,fr,fw,mode,us,delivered;} Device;
static Device d;
static int rd(void *c,unsigned a,uint32_t *v)
{Device *p=c;assert(marble_cp_access(a,0));if(++p->reads==p->fr)return -1;*v=p->regs[a];return 0;}
static int wr(void *c,unsigned a,uint32_t v)
{
    Device *p=c;assert(marble_cp_access(a,1));unsigned fail=++p->writes==p->fw;
    if(fail && !p->delivered)return -1;
    p->regs[a]=v;
    if(a==0x807 && v==21 && p->mode!=1) {
        for(unsigned i=0;i<MARBLE_GEN7_HWCG_COUNT;i++)
            assert(p->regs[marble_gen7_hwcg[i][0]]==marble_gen7_hwcg[i][1]);
        for(unsigned i=0;i<MARBLE_GEN7_GBIF_COUNT;i++)
            assert(p->regs[marble_gen7_gbif[i][0]]==marble_gen7_gbif[i][1]);
        for(unsigned i=0;i<MARBLE_GEN7_UCHE_COUNT;i++)
            assert(p->regs[marble_gen7_uche[i][0]]==marble_gen7_uche[i][1]);
        assert(p->regs[0x808]==1 && p->regs[0x830]==0x100000);
        AdrenoPM4Packet packets[5];size_t count=0;assert(!AdrenoPM4Plan(p->ring,21,packets,5,&count) && count==5);
        assert(p->ring[3]==0x94b && p->ring[13]==0x500000 && p->ring[18]==0x300004);
        p->regs[0x806]=p->mode==2?20:21;p->regs[0x885]=p->ring[11];
        p->mem[0]=21;p->mem[1]=p->mode==3?0:1;p->scratch[0]=p->mode==4?0:p->ring[15];
        if(p->mode==5)p->regs[0x821]=1;
        if(p->mode==6)p->regs[0x201]=1u<<23;
    }
    if(a==0x808 && v==3 && p->mode==7)p->regs[a]=1;
    return fail?-1:0;
}
static void delay_us(void *c,unsigned us){Device *p=c;p->us+=us;assert(p->us<=100010);}
static void barrier(void *c){(void)c;}
static int health(void *c){(void)c;return 0;}
static void init(unsigned mode){memset(&d,0,sizeof(d));d.mode=mode;}
int main(void)
{
    MarbleCpIo io={&d,rd,wr,delay_us,health,barrier};MarbleCpResult r;
    init(0);assert(marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));
    assert(r.completed && r.fence==1 && r.marker==0x4d475031 && r.rptr==21);
    unsigned nr=d.reads,nw=d.writes;assert(marble_cp_halt(&io,&r) && r.halted);
    unsigned pre=d.writes;
    assert(!marble_cp_restore_bus(&io,&r,0) && d.writes==pre);
    assert(marble_cp_restore_bus(&io,&r,1) && r.bus_restored);
    for(unsigned i=0;i<4;i++)assert(!d.regs[marble_gen7_gbif[i][0]]);
    for(unsigned n=1;n<=nr;n++){init(0);d.fr=n;assert(!marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));}
    for(unsigned n=1;n<=nw;n++)for(unsigned delivered=0;delivered<2;delivered++) {
        init(0);d.fw=n;d.delivered=delivered;
        assert(!marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));
    }
    for(unsigned mode=1;mode<=6;mode++){init(mode);assert(!marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));assert(marble_cp_halt(&io,&r));}
    for(unsigned n=1;n<=4;n++) {
        init(0);assert(marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));
        d.fr=d.reads+n;assert(!marble_cp_restore_bus(&io,&r,1) && !r.bus_restored);
        for(unsigned delivered=0;delivered<2;delivered++) {
            init(0);assert(marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));
            d.fw=d.writes+n;d.delivered=delivered;
            assert(!marble_cp_restore_bus(&io,&r,1) && !r.bus_restored);
        }
    }
    init(7);assert(marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));
    assert(marble_cp_halt(&io,&r) && r.halt_requested && !r.halted && r.halt==1);
    assert(!marble_cp_restore_bus(&io,&r,0));
    init(0);assert(marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));d.fw=d.writes+1;
    assert(!marble_cp_halt(&io,&r) && !r.halt_requested);
    init(0);assert(marble_cp_run(&io,d.ring,d.mem,d.scratch,0x4d475031,1,&r));d.fr=d.reads+1;
    assert(!marble_cp_halt(&io,&r) && !r.halt_requested);
    init(0);assert(!marble_cp_run(&io,d.ring,d.mem,d.scratch,1,0,&r) && !d.writes);
    init(0);assert(!marble_cp_run(&io,d.ring,d.mem,d.scratch,0,1,&r) && !d.writes);
    assert(!marble_cp_access(0xf400,1) && !marble_cp_access(0xe01,1));
    assert(!marble_cp_access(0xaf,1) && marble_cp_access(0x260,1));
    printf("CP fixtures: %u reads/%u before+after-delivery write failures, timeout, partial RPTR/fence/marker, HW fault, IRQ, halt read-as-1 and request failures, bus restore failures PASS; no hardware claim\n",nr,nw);
}
