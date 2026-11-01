/* Marble clock transaction, GMU held reset and GX off throughout.
 * Register facts: Xiaomi 48952ed3 gpucc/gcc-waipio.c, debugcc-waipio.c,
 * clk-rcg2.c and clk-debug.c. No GPU PLL or regulator programming here. */
#ifndef MARBLE_GMU_CLOCKS_H
#define MARBLE_GMU_CLOCKS_H
#include <stdint.h>
#define MARBLE_GMU_CLOCKS_ABI "MGPU-MARBLE-GMU-CLOCKS-v1"
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,unsigned,uint32_t *);
    int (*write)(void *,unsigned,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*guard)(void *);
} MarbleClockIo;
typedef struct {
    uint32_t before[13],after[13],short_count[2],full_count[2],hz[2];
    unsigned phase,changed,configured,measured,restored,retain,refused;
} MarbleClockResult;
/* Blocks 0=GCC, 1=GPUCC. Status/measurement registers are never writable. */
static const unsigned marble_clock_block[13]={0,1,1,1,1,0,0,0,0,0,1,1,1};
static const unsigned marble_clock_reg[13]={0x62000,0x9318,0x931c,0x93ec,0x93f0,
    0x72000,0x72004,0x72008,0x7200c,0x72038,0x9564,0x9270,0x9274};
static int marble_clock_access(unsigned block,unsigned offset,unsigned bytes,unsigned write)
{
    if(bytes!=4 || block>1)return 0;
    if(!write && block==0 && offset==0x7203c)return 1;
    for(unsigned i=0;i<13;i++)if(marble_clock_block[i]==block && marble_clock_reg[i]==offset)return 1;
    return 0;
}
static int marble_clock_poll(const MarbleClockIo *io,unsigned block,unsigned reg,uint32_t mask,uint32_t want,unsigned limit,uint32_t *last)
{
    for(unsigned us=0;us<=limit;us+=5) {
        if(io->read(io->cookie,block,reg,last))return 0;
        if((*last&mask)==want)return 1;
        io->delay(io->cookie,5);
    }
    return 0;
}
static int marble_clock_config(const MarbleClockIo *io,unsigned cmd,uint32_t cfg)
{
    uint32_t value;
    if(io->write(io->cookie,1,cmd+4,cfg) || io->read(io->cookie,1,cmd,&value) ||
        io->write(io->cookie,1,cmd,value|1) || !marble_clock_poll(io,1,cmd,1,0,500,&value) ||
        io->read(io->cookie,1,cmd+4,&value))return 0;
    return value==cfg;
}
static int marble_clock_count(const MarbleClockIo *io,uint32_t base,unsigned ticks,uint32_t *out)
{
    uint32_t value;void *c=io->cookie;
    base&=~0x3fffffu;
    if(io->write(c,0,0x72038,base|0x200000))return 0;
    io->delay(c,1);
    if(io->write(c,0,0x72038,base) || io->write(c,0,0x72038,base|ticks) ||
        io->write(c,0,0x72038,base|ticks|0x100000))return 0;
    int done=marble_clock_poll(io,0,0x7203c,0x2000000,0x2000000,40000,&value);
    if(io->write(c,0,0x72038,base|ticks))return 0;
    if(!done)return 0;
    *out=value&0x1ffffff;return 1;
}
/* work: 0 success, -1 stopped failure, -2 hardware still owns resources. */
static int marble_clock_transaction_work(const MarbleClockIo *io,MarbleClockResult *r,
    int (*work)(void *),void *work_cookie)
{
    if(!r)return 0;
    *r=(MarbleClockResult){0};
    if(!io || !io->read || !io->write || !io->delay || !io->guard || io->guard(io->cookie)) {r->refused=1;return 0;}
    void *c=io->cookie;uint32_t value;unsigned configured=0;int work_ok=1;
    for(unsigned i=0;i<13;i++)if(io->read(c,marble_clock_block[i],marble_clock_reg[i],r->before+i)) {r->refused=1;return 0;}
    /* Only the observed parked XO configuration is eligible. Do not take a
     * live measurement engine or alter another driver's selected root. */
    if((r->before[1]&1) || (r->before[3]&1) || (r->before[2]&0x10371f) ||
        (r->before[4]&0x10371f) || (r->before[9]&0x300000) ||
        (r->before[7]&1) || (r->before[12]&1) || io->guard(c)) {r->refused=1;return 0;}
    r->phase=1;r->changed=1;r->retain=1;
    if(io->write(c,0,0x62000,r->before[0]|0x10000))goto cleanup;
    io->delay(c,10);
    r->phase=2;
    configured=1;
    if(!marble_clock_config(io,0x9318,(r->before[2]&~0x10371fu)|0x100602u))goto cleanup;
    configured=2;
    if(!marble_clock_config(io,0x93ec,(r->before[4]&~0x10371fu)|0x100603u) || io->guard(c))goto cleanup;
    r->configured=1;r->phase=3;
    if(io->write(c,0,0x72000,(r->before[5]&~0x3ffu)|0x16e) ||
        io->write(c,0,0x72004,(r->before[6]&~15u)|1) ||
        io->write(c,1,0x9270,(r->before[11]&~15u)|1) ||
        io->write(c,0,0x72008,r->before[7]|1) ||
        io->write(c,1,0x9274,r->before[12]|1) ||
        io->write(c,0,0x7200c,r->before[8]|1))goto cleanup;
    for(unsigned i=0;i<2;i++) {
        if(io->write(c,1,0x9564,(r->before[10]&~255u)|(i?0x20:0x1e)) || io->guard(c) ||
            !marble_clock_count(io,r->before[9],0x1000,r->short_count+i) ||
            !marble_clock_count(io,r->before[9],0x20000,r->full_count+i) ||
            r->short_count[i]==r->full_count[i])goto cleanup;
        r->hz[i]=(uint32_t)(((uint64_t)r->full_count[i]*10+15)*4800000*4/(0x20000*10+35));
    }
    r->measured=r->hz[0]>=190000000 && r->hz[0]<=210000000 && r->hz[1]>=142500000 && r->hz[1]<=157500000;
    if(r->measured && work) {
        int status=work(work_cookie);work_ok=status==0;
        if(status==-2)return 0;
    }
cleanup:
    /* No parent vote may be withdrawn until both roots are restored. Keep
     * RAM and power if cleanup fails; the caller must cold-reset the phone. */
    r->phase=4;int safe=1;
    if(io->write(c,0,0x72038,r->before[9]))safe=0;
    for(unsigned i=13;i-->5;)if(i!=9 && io->write(c,marble_clock_block[i],marble_clock_reg[i],r->before[i]))safe=0;
    if(configured>=2 && !marble_clock_config(io,0x93ec,r->before[4]))safe=0;
    if(configured>=1 && !marble_clock_config(io,0x9318,r->before[2]))safe=0;
    if(safe && io->write(c,0,0x62000,r->before[0]))safe=0;
    for(unsigned i=0;i<13;i++) {
        if(io->read(c,marble_clock_block[i],marble_clock_reg[i],r->after+i))safe=0;
        uint32_t mask=(i==1 || i==3)?0x7fffffffu:(i==7 || i==8 || i==12)?1u:0xffffffffu;
        if((r->after[i]^r->before[i])&mask)safe=0;
    }
    if(io->guard(c))safe=0;
    r->restored=safe;r->retain=!safe;r->phase=5;
    (void)value;
    return r->configured && r->measured && r->restored && work_ok;
}
static int marble_clock_transaction(const MarbleClockIo *io,MarbleClockResult *r)
{return marble_clock_transaction_work(io,r,0,0);}
#endif
