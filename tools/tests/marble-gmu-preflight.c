/* Synthetic command DB and clock callbacks, never hardware evidence. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_preflight.h"
static uint8_t db[512],original[512];
static void u16(size_t at,unsigned v){db[at]=(uint8_t)v;db[at+1]=(uint8_t)(v>>8);}
static void u32(size_t at,uint32_t v){u16(at,v);u16(at+2,v>>16);}
static void fixture(void)
{
    memset(db,0,sizeof(db));u32(0,1);memcpy(db+4,"\xdb\x30\x03\x0c",4);
    u16(8,3);u16(10,0);u16(12,128);u16(14,2);u16(16,0x100);
    u16(24,5);u16(26,48);u16(28,160);u16(30,2);u16(32,0x200);
    const char *names[]={"cx.lvl","mxc.lvl","CN0","SH0"};
    for(unsigned i=0;i<4;i++) {
        unsigned at=144+24*i;memcpy(db+at,names[i],strlen(names[i]));
        u32(at+16,(i<2?0x30000:0x50000)+4*i);u16(at+20,8);u16(at+22,8*(i&1));
    }
    for(unsigned i=0;i<4;i++)u16(144+128+2*i,16*i);
    for(unsigned i=0;i<4;i++)u16(144+136+2*i,32*i);
    u32(144+160,1000);u16(144+164,16);db[144+166]=2;
    u32(144+168,2000);u16(144+172,32);db[144+174]=3;
}
static void refused(const char *name,size_t bytes)
{
    MarbleCmdDbEntry out,before;memset(&out,0xa5,sizeof(out));memcpy(&before,&out,sizeof(out));
    assert(!marble_cmd_db_find(db,bytes,name,&out));assert(!memcmp(&out,&before,sizeof(out)));
}
static unsigned calls,guard_calls,fail_at,mutate,guard_fail;
static int read_clock(void *c,unsigned at,uint32_t *v)
{
    (void)c;assert(marble_gmu_clock_read_allowed(at,4));unsigned n=calls++;
    if(calls==fail_at)return -1;
    *v=at;
    if(n>=8 && mutate==n-7)*v^=1;
    return 0;
}
static int guard(void *c){(void)c;return ++guard_calls==guard_fail;}
int main(void)
{
    fixture();memcpy(original,db,sizeof(db));MarbleCmdDbEntry out;
    assert(marble_cmd_db_find(db,sizeof(db),"cx.lvl",&out) && out.address==0x30000 && out.bytes==8 && out.aux==db+272);
    assert(marble_cmd_db_find(db,sizeof(db),"SH0",&out) && out.address==0x5000c && out.version==0x200);
    refused("missing",sizeof(db));refused("123456789",sizeof(db));refused("",sizeof(db));
    refused("SH0",MARBLE_CMD_DB_BYTES+1);
    for(size_t bytes=0;bytes<320;bytes++)refused("SH0",bytes);
    const unsigned bad[][2]={{4,0},{8,6},{14,513},{10,0xffff},{12,0xffff},{144+20,0xffff},{144+22,0xffff},{144+16,1}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        memcpy(db,original,sizeof(db));u16(bad[i][0],bad[i][1]);refused("cx.lvl",sizeof(db));
    }
    memcpy(db,original,sizeof(db));memcpy(db+168,db+144,8);refused("cx.lvl",sizeof(db));
    memcpy(db,original,sizeof(db));u16(24,3);u32(144+48+16,0);
    assert(marble_cmd_db_find(db,sizeof(db),"cx.lvl",&out)); /* repeated ARC section, metadata */
    refused("CN0",sizeof(db));refused("SH0",sizeof(db)); /* metadata/mismatched address cannot escape */
    memcpy(db,original,sizeof(db));u16(40,4);u16(46,1);refused("cx.lvl",sizeof(db));
    memcpy(db,original,sizeof(db));
    /* Every field of a firmware-controlled byte snapshot can change. Bounds
     * and sanitizer checks must hold even when a mutation is still valid. */
    for(unsigned at=0;at<sizeof(db);at++)for(unsigned bit=0;bit<8;bit++) {
        db[at]^=(uint8_t)(1u<<bit);(void)marble_cmd_db_find(db,sizeof(db),"SH0",&out);db[at]^=(uint8_t)(1u<<bit);
    }
    MarbleGmuClockIo io={NULL,read_clock,guard};MarbleGmuClockResult r;
    assert(marble_gmu_clock_snapshot(&io,&r) && r.reads==16 && r.stable && r.guarded && guard_calls==2);
    for(unsigned i=1;i<=16;i++) {
        calls=guard_calls=0;fail_at=i;assert(!marble_gmu_clock_snapshot(&io,&r) && !r.guarded && calls==i);
    }
    fail_at=0;
    for(unsigned i=1;i<=8;i++) {
        calls=guard_calls=0;mutate=i;assert(!marble_gmu_clock_snapshot(&io,&r) && !r.guarded);
    }
    mutate=0;
    for(unsigned i=1;i<=2;i++) {
        calls=guard_calls=0;guard_fail=i;assert(!marble_gmu_clock_snapshot(&io,&r) && !r.guarded);
        assert(calls==(i==1?0:16));
    }
    for(unsigned at=0;at<0xa000;at++)for(unsigned bytes=0;bytes<9;bytes++) {
        unsigned expected=0;for(unsigned i=0;i<8;i++)expected|=bytes==4 && at==marble_gmu_clock_offsets[i];
        assert(marble_gmu_clock_read_allowed(at,bytes)==(int)expected);
    }
    assert(marble_gmu_clock_nominal(0,0x602)==200000000);
    assert(marble_gmu_clock_nominal(1,0x603)==150000000);
    assert(marble_gmu_clock_nominal(0,0x303)==500000000);
    assert(marble_gmu_clock_nominal(1,0x504)==240000000);
    assert(marble_gmu_clock_nominal(1,0x503)==300000000);
    assert(marble_gmu_clock_nominal(0,0)==19200000 && marble_gmu_clock_nominal(1,1)==19200000);
    assert(!marble_gmu_clock_nominal(0,0x1602) && !marble_gmu_clock_nominal(1,0x703));
    puts("GMU preflight: 4096 DB mutations, truncation/overflow/ambiguity refusals; 16 read failures, 8 unstable registers, 2 lost guards; readonly fence PASS");
    return 0;
}
