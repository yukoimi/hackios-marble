/* Marble GMU prerequisites: read-only clock observation and bounded command DB.
 * Wire format/register facts: pinned Xiaomi 48952ed3 cmd-db.c, clk-rcg2.c,
 * gpucc-waipio.c and Mu-marble DT. No vote, clock write or GMU start API.
 */
#ifndef MARBLE_GMU_PREFLIGHT_H
#define MARBLE_GMU_PREFLIGHT_H
#include <stdint.h>
#include <stddef.h>
#ifndef MARBLE_CMD_DB_FREESTANDING
#include <string.h>
#endif
#define MARBLE_GMU_PREFLIGHT_ABI "MGPU-MARBLE-GMU-PREFLIGHT-v1"
#define MARBLE_CMD_DB_PHYS UINT64_C(0x80860000)
#define MARBLE_CMD_DB_BYTES 0x20000u
#define MARBLE_CMD_DB_HEADER 144u
typedef struct {
    uint32_t address;
    uint16_t slave,version,bytes;
    const uint8_t *aux;
} MarbleCmdDbEntry;
typedef struct {unsigned code,slot,entry;} MarbleCmdDbIssue;
static int marble_db_refuse(MarbleCmdDbIssue *issue,unsigned code,unsigned slot,unsigned entry)
{if(issue)*issue=(MarbleCmdDbIssue){code,slot,entry};return 0;}
static uint16_t marble_db_u16(const uint8_t *p)
{return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t marble_db_u32(const uint8_t *p)
{return marble_db_u16(p)|(uint32_t)marble_db_u16(p+2)<<16;}
static int marble_db_span(size_t offset,size_t bytes,size_t total)
{return offset<=total && bytes<=total-offset;}
/* A complete immutable CPU snapshot is required. Never dereference firmware
 * pointers or use database addresses for MMIO. Output is unchanged on refusal.
 * Checksum is unused by Linux and is NOT authenticated here. */
static int marble_cmd_db_find_diagnostic(const uint8_t *db,size_t bytes,const char *name,
    MarbleCmdDbEntry *out,MarbleCmdDbIssue *issue)
{
    if(issue)*issue=(MarbleCmdDbIssue){0};
    static const uint8_t magic[4]={0xdb,0x30,0x03,0x0c};
    if(!db || !name || !out || bytes<MARBLE_CMD_DB_HEADER || bytes>MARBLE_CMD_DB_BYTES ||
        memcmp(db+4,magic,4) || !marble_db_u32(db))return marble_db_refuse(issue,1,0,0);
    size_t n=0;while(n<9 && name[n])n++;
    if(!n || n>8)return marble_db_refuse(issue,2,0,0);
    uint8_t id[8]={0};memcpy(id,name,n);
    size_t payload=bytes-MARBLE_CMD_DB_HEADER;unsigned found=0,ended=0;
    MarbleCmdDbEntry result={0};
    for(unsigned s=0;s<8;s++) {
        const uint8_t *h=db+8+s*16;unsigned slave=marble_db_u16(h);
        if(!slave){ended=1;continue;}
        /* Multiple directory sections can share a hardware type. Marble's
         * second ARC section contains metadata (e.g. cx.mol, address zero),
         * not writable RPMh resources. Linux searches all such sections. */
        if(ended || slave<3 || slave>5)return marble_db_refuse(issue,3,s,0);
        size_t headers=marble_db_u16(h+2),data=marble_db_u16(h+4),count=marble_db_u16(h+6);
        if(!count || count>512 || !marble_db_span(headers,count*24,payload) || data>payload)return marble_db_refuse(issue,4,s,0);
        for(size_t i=0;i<count;i++) {
            const uint8_t *e=db+MARBLE_CMD_DB_HEADER+headers+i*24;
            size_t length=marble_db_u16(e+20),offset=marble_db_u16(e+22);
            uint32_t address=marble_db_u32(e+16);
            if(!marble_db_span(data+offset,length,payload))return marble_db_refuse(issue,5,s,(unsigned)i);
            if(!memcmp(e,id,8)) {
                /* Only a requested resource can become an output address.
                 * Unrelated address-zero metadata is bounded but not used. */
                if(!address || (address&3) || ((address>>16)&7)!=slave)
                    return marble_db_refuse(issue,8,s,(unsigned)i);
                if(found++)return marble_db_refuse(issue,6,s,(unsigned)i); /* ambiguous resources are refused */
                result=(MarbleCmdDbEntry){address,(uint16_t)slave,marble_db_u16(h+8),
                    (uint16_t)length,db+MARBLE_CMD_DB_HEADER+data+offset};
            }
        }
    }
    if(found!=1)return marble_db_refuse(issue,7,0,0);
    *out=result;return 1;
}
static int marble_cmd_db_find(const uint8_t *db,size_t bytes,const char *name,MarbleCmdDbEntry *out)
{return marble_cmd_db_find_diagnostic(db,bytes,name,out,NULL);}
#ifndef MARBLE_CMD_DB_ONLY
enum { MARBLE_GMU_CLOCK_REGS=8 };
static const unsigned marble_gmu_clock_offsets[MARBLE_GMU_CLOCK_REGS]={
    0x9318,0x931c,0x93ec,0x93f0,0x9430,0x942c,0x913c,0x9148};
static int marble_gmu_clock_read_allowed(unsigned offset,unsigned bytes)
{
    if(bytes!=4)return 0;
    for(unsigned i=0;i<MARBLE_GMU_CLOCK_REGS;i++)if(offset==marble_gmu_clock_offsets[i])return 1;
    return 0;
}
/* Nominal table lookup only, NOT a measured effective frequency. A live RCG
 * may show its safe XO selection when the clock framework parks the root.
 * Unknown parent/divider/mode remains unknown; never substitute a rate. */
static uint32_t marble_gmu_clock_nominal(unsigned hub,uint32_t cfg)
{
    if(hub>1 || (cfg&0x3000))return 0; /* M/N mode absent on these RCGs */
    unsigned parent=(cfg>>8)&7,hid=cfg&31;
    if(!parent && hid<=1)return 19200000;
    if(!hub && parent==6 && hid==2)return 200000000;
    if(!hub && parent==3 && hid==3)return 500000000;
    if(hub && parent==6 && hid==3)return 150000000;
    if(hub && parent==5 && hid==4)return 240000000;
    if(hub && parent==5 && hid==3)return 300000000;
    return 0;
}
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,uint32_t *);
    int (*guard)(void *);
} MarbleGmuClockIo;
typedef struct {uint32_t value[MARBLE_GMU_CLOCK_REGS];unsigned reads,stable,guarded;} MarbleGmuClockResult;
static int marble_gmu_clock_snapshot(const MarbleGmuClockIo *io,MarbleGmuClockResult *r)
{
    if(!io || !io->read || !io->guard || !r)return 0;
    *r=(MarbleGmuClockResult){0};
    if(io->guard(io->cookie))return 0;
    for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<MARBLE_GMU_CLOCK_REGS;i++) {
        uint32_t value;
        if(io->read(io->cookie,marble_gmu_clock_offsets[i],&value))return 0;
        r->reads++;
        if(!pass)r->value[i]=value;
        else if(r->value[i]!=value)return 0;
    }
    r->stable=1;
    if(io->guard(io->cookie))return 0;
    r->guarded=1;return 1;
}
#endif /* MARBLE_CMD_DB_ONLY */
#endif
