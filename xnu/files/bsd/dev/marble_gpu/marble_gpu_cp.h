/* Minimal Gen7 CP transaction. Header encoding is unmodified AdrenoPM4;
 * CP init/fence layout follows pinned NativeAdreno and Xiaomi Gen7 sources.
 * The caller owns a powered GX OOB lease and audited SID0/0x400 native4K
 * mappings. This tests CP DMA, not shader rendering or userspace acceleration. */
#ifndef MARBLE_GPU_CP_H
#define MARBLE_GPU_CP_H
#include "upstream/AdrenoPM4.h"
#include "marble_gen7_hwcg.h"
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,uint32_t *); /* GPU dword indices */
    int (*write)(void *,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*health)(void *);
    void (*barrier)(void *);
} MarbleCpIo;
typedef struct {
    uint32_t words,rptr,wptr,shadow,fence,marker,scratch,status,status3,irq,fault,halt;
    unsigned configured,kicked,completed,polls,halted,phase,halt_requested;
    uint32_t bus_before[4],bus_after[4];
    unsigned bus_changed,bus_restored;
} MarbleCpResult;
static int marble_cp_access(unsigned a,unsigned write)
{
    for(unsigned i=0;i<4;i++)if(a==marble_gen7_gbif[i][0])return 1;
    if(!write)return a==0x800 || a==0x801 || a==0x802 || a==0x804 || a==0x805 ||
        a==0x809 || a==0x80a || a==0x80b || a==0x806 || a==0x807 || a==0x808 || a==0x885 || a==0x210 ||
        a==0x213 || a==0x201 || a==0x821 || a==0x830 || a==0x831 || a==0x844 || a==0xf400;
    for(unsigned i=0;i<MARBLE_GEN7_HWCG_COUNT;i++)
        if(a==marble_gen7_hwcg[i][0])return 1;
    for(unsigned i=0;i<MARBLE_GEN7_UCHE_COUNT;i++)
        if(a==marble_gen7_uche[i][0])return 1;
    if(a==marble_gen7_gbif[4][0])return 1;
    switch(a) {
    case 0x37:case 0x38:case 0x800:case 0x801:case 0x802:case 0x804:case 0x805:
    case 0x807:case 0x808:case 0x830:case 0x831:case 0x841:case 0x844:case 0x885:
    case 0x98d:case 0xa98:case 0xa99:case 0xad0:case 0xada:case 0xb30:case 0xb31:return 1;
    default:return 0;
    }
}
static int marble_cp_packets(uint32_t out[21],uint32_t marker)
{
    if(!out || !marker)return 0;
    if(AdrenoPM4Type7(0x17,1,out) || AdrenoPM4Type7(0x48,7,out+2) ||
        AdrenoPM4Type4(0x885,1,out+10) || AdrenoPM4Type7(0x3d,3,out+12) ||
        AdrenoPM4Type7(0x46,4,out+16))return 0;
    out[1]=1u<<27;
    /* DMS disabled, as Xiaomi's !dms_enabled path (bit11). */
    out[3]=0x94b;out[4]=3;out[5]=0x20000000;out[6]=2;
    out[7]=0x400000;out[8]=0;out[9]=1u<<31;
    out[11]=marker;out[13]=0x500000;out[14]=0;out[15]=marker;
    out[17]=0x08000031;out[18]=0x300004;out[19]=0;out[20]=1;
    AdrenoPM4Packet packets[5];size_t count=0;
    return !AdrenoPM4Plan(out,21,packets,5,&count) && count==5;
}
static int marble_cp_run(const MarbleCpIo *io,volatile uint32_t *ring,
    volatile uint32_t *memptrs,volatile uint32_t *scratch,uint32_t marker,
    unsigned held,MarbleCpResult *r)
{
    if(!r)return 0;*r=(MarbleCpResult){0};
    if(!io || !io->read || !io->write || !io->delay || !io->health || !io->barrier ||
        !ring || !memptrs || !scratch || !held || !marker || io->health(io->cookie))return 0;
    uint32_t packets[21];if(!marble_cp_packets(packets,marker))return 0;
    for(unsigned i=0;i<16;i++)memptrs[i]=0;
    scratch[0]=~marker;
    for(unsigned i=0;i<21;i++)ring[i]=packets[i];
    io->barrier(io->cookie);r->phase=1;r->words=21;
    for(unsigned i=0;i<4;i++)
        if(io->read(io->cookie,marble_gen7_gbif[i][0],r->bus_before+i))return 0;
    r->bus_changed=1;
    for(unsigned i=0;i<MARBLE_GEN7_GBIF_COUNT;i++)
        if(io->health(io->cookie) || io->write(io->cookie,
            marble_gen7_gbif[i][0],marble_gen7_gbif[i][1]))return 0;
    for(unsigned i=0;i<MARBLE_GEN7_UCHE_COUNT;i++)
        if(io->health(io->cookie) || io->write(io->cookie,
            marble_gen7_uche[i][0],marble_gen7_uche[i][1]))return 0;
    /* Exact Gen7_4_0 clock facts, resolved from Xiaomi's core descriptor.
     * GX collapse discards this state before the caller releases its lease. */
    for(unsigned i=0;i<MARBLE_GEN7_HWCG_COUNT;i++)
        if(io->health(io->cookie) || io->write(io->cookie,
            marble_gen7_hwcg[i][0],marble_gen7_hwcg[i][1]))return 0;
    static const uint32_t init[][2]={
        /* A cold CP stays untouched until SQE/RB addresses are installed.
         * Writing 3 here would also set bit0 before the base is valid. */
        {0x38,0},{0x37,0xffffffffu},{0x98d,1},
        {0x844,0x6f},{0xad0,0xf},{0xb31,0xf},
        {0x841,1},{0xada,1},{0xb30,1},
        {0x800,0x200000},{0x801,0},{0x802,0x20b},
        {0x804,0x300000},{0x805,0},{0xa98,0x300008},{0xa99,0},
        {0x830,0x100000},{0x831,0},{0x807,0},{0x885,0}};
    for(unsigned i=0;i<sizeof(init)/sizeof(init[0]);i++)
        if(io->health(io->cookie) || io->write(io->cookie,init[i][0],init[i][1]))return 0;
    uint32_t lo,hi,apriv;
    if(io->read(io->cookie,0x830,&lo) || io->read(io->cookie,0x831,&hi) ||
        io->read(io->cookie,0x844,&apriv) || lo!=0x100000 || hi || apriv!=0x6f)return 0;
    r->configured=1;r->phase=2;
    if(io->write(io->cookie,0x808,1))return 0;
    io->barrier(io->cookie);
    /* A failed posted kick still requires real reset/drain before releasing RAM. */
    r->kicked=1;
    if(io->write(io->cookie,0x807,21))return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;io->barrier(io->cookie);
        r->shadow=memptrs[0];r->fence=memptrs[1];r->marker=scratch[0];
        if(io->health(io->cookie) || io->read(io->cookie,0x806,&r->rptr) ||
            io->read(io->cookie,0x807,&r->wptr) || io->read(io->cookie,0x885,&r->scratch) ||
            io->read(io->cookie,0x201,&r->irq) || io->read(io->cookie,0x821,&r->fault))break;
        if(r->fault || (r->irq&0x138003c2u))break;
        if(r->rptr==21 && r->wptr==21 && r->fence==1 && r->marker==marker && r->scratch==marker) {
            r->completed=1;r->phase=3;break;
        }
        io->delay(io->cookie,10);
    }
    if(io->read(io->cookie,0x210,&r->status) || io->read(io->cookie,0x213,&r->status3))return 0;
    return r->completed && !io->health(io->cookie);
}
static int marble_cp_halt(const MarbleCpIo *io,MarbleCpResult *r)
{
    if(!r->configured && !r->kicked)return 1;
    if(io->write(io->cookie,0x808,3) || io->read(io->cookie,0x808,&r->halt))return 0;
    /* Linux issues this request without checking bit1. On marble a completed
     * CP returns 1 after the write of 3. Record that fact; do not pretend it
     * acknowledges halt or permits freeing DMA memory. The caller must still
     * prove CM3 reset, GX+CX drain and collapse before restoring/releasing. */
    io->barrier(io->cookie);r->halt_requested=1;r->halted=!!(r->halt&2);return 1;
}
/* Caller must first confirm CM3 reset, GX drain, CX drain and GX collapse.
 * Never restore CX bus registers on a timeout alone. */
static int marble_cp_restore_bus(const MarbleCpIo *io,MarbleCpResult *r,unsigned drained_collapsed)
{
    if(!r->bus_changed)return 1;
    if(!drained_collapsed)return 0;
    for(unsigned i=0;i<4;i++)
        if(io->write(io->cookie,marble_gen7_gbif[i][0],r->bus_before[i]))return 0;
    io->barrier(io->cookie);
    for(unsigned i=0;i<4;i++)
        if(io->read(io->cookie,marble_gen7_gbif[i][0],r->bus_after+i) ||
            r->bus_after[i]!=r->bus_before[i])return 0;
    r->bus_restored=1;return 1;
}
#endif
