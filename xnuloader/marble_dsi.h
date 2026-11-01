/* Bounded AP-owned DSI v2.6 FIFO transport. Register/packet layouts:
 * pinned LineageOS sm8450 display driver references in
 * out/daily-display-fw-v1-plan/dsi-references.json. No PHY, PLL, panel
 * reset, voltage, refresh-rate or video-engine changes. */
#ifndef XNULOADER_MARBLE_DSI_H
#define XNULOADER_MARBLE_DSI_H
#include <stdint.h>
typedef struct {
    void *ctx;
    uint32_t (*read)(void *,unsigned);
    void (*write)(void *,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*blank)(void *);
} MarbleDsiOps;
typedef struct {
    uint32_t response,read_count,interrupts,status,ack_error,timeout_error;
    unsigned started,done,restored,dirty,bytes;
    uint8_t data[2];
} MarbleDsiResult;
#define MD_INT_MASKS ((1u<<1)|(1u<<9)|(1u<<11)|(1u<<13)|(1u<<15)|(1u<<17)|(1u<<21)|(1u<<25)|(1u<<29)|(1u<<31))
#define MD_OWN_DONE ((1u<<0)|(1u<<20))

static void marble_dsi_ack(const MarbleDsiOps *o)
{
    uint32_t n=o->read(o->ctx,0x110);
    /* W1C only our DMA/BTA completions. In particular do not clear the
     * preexisting video-frame-done flag or change any interrupt enables. */
    o->write(o->ctx,0x110,(n&MD_INT_MASKS)|(n&MD_OWN_DONE));
}
static int marble_dsi_parse_short(MarbleDsiResult *r)
{
    /* RDBK_DATA0's MSB is the MIPI response type (Linux ntohl path). */
    unsigned type=(r->response>>24)&0x3f;
    if(r->read_count!=4 || (type!=0x21 && type!=0x22))return 0;
    r->bytes=type==0x21?1:2;
    r->data[0]=(uint8_t)(r->response>>16);
    r->data[1]=(uint8_t)(r->response>>8);
    return 1;
}
static int marble_dsi_transfer(const MarbleDsiOps *o,MarbleDsiResult *r,unsigned command,unsigned level,unsigned write)
{
    *r=(MarbleDsiResult){0};
    /* A fixed read-only allowlist; no caller can inject panel writes or a
     * maximum-return-size change. Brightness is accepted only as two bytes;
     * a truncated one-byte reply must never become an apparent saved level. */
    unsigned expected=command==0x52?2:1;
    if(write) {if(command!=0x51 || level<8 || level>4095)return 0;}
    else if(command!=0x0a && command!=0xda && command!=0xdb && command!=0xdc && command!=0x52)return 0;
    uint32_t ctrl=o->read(o->ctx,4),dma=o->read(o->ctx,0x3c);
    uint32_t len=o->read(o->ctx,0x4c),ints=o->read(o->ctx,0x110);
    if(o->read(o->ctx,0)!=0x20060000 || (ctrl&7)!=7 ||
       (o->read(o->ctx,8)&7) || o->read(o->ctx,0x15c) ||
       o->read(o->ctx,0x50) || o->read(o->ctx,0x100) || o->read(o->ctx,0x104) ||
       (o->read(o->ctx,0x84)&0xf)!=4 || (ints&MD_OWN_DONE) ||
       o->read(o->ctx,0x68) || o->read(o->ctx,0xc0) || !o->blank(o->ctx))return 0;
    /* Preserve the panel's existing maximum-return state. Some panels may
     * decline/truncate 0x52 in this state; that is a failed probe, not grounds
     * to guess its old value or issue SET_MAX_RETURN. */
    o->write(o->ctx,0x1d4,1);o->delay(o->ctx,1);o->write(o->ctx,0x1d4,0);
    o->write(o->ctx,0x15c,0x30006);
    if(write) {
        /* Long DCS 0x51 + two bytes. The marble panel's inverted-dbv swaps
         * Linux's little-endian brightness payload to MSB first. The final
         * padding byte is not included in the packet word count. */
        o->write(o->ctx,0x17c,0xc0390003);
        o->write(o->ctx,0x17c,0xff000051|((level>>8)<<8)|((level&255)<<16));
    } else {
        o->write(o->ctx,0x17c,0xa0060000|command); /* last, embedded BTA, DCS read */
        o->write(o->ctx,0x17c,0); /* odd DWORD requires an extra zero DWORD */
    }
    o->write(o->ctx,0x3c,(dma&~((1u<<31)|(1u<<30)))|(1u<<28));
    o->write(o->ctx,0x4c,write?8:4);
    o->write(o->ctx,0x90,1);
    r->started=1;
    for(unsigned i=0;i<500;i++) {
        r->interrupts=o->read(o->ctx,0x110);
        r->read_count=o->read(o->ctx,0x1d4)>>16;
        if((r->interrupts&1) && (write || r->read_count)) {r->done=1;break;}
        o->delay(o->ctx,10);
    }
    r->status=o->read(o->ctx,8);
    r->ack_error=o->read(o->ctx,0x68);
    r->timeout_error=o->read(o->ctx,0xc0);
    r->response=o->read(o->ctx,0x6c);
    /* An in-flight/failed engine is never forcibly reset. Keep its clock
     * lease and make the loader dirty until reboot instead of guessing. */
    if(r->status&7) {r->dirty=1;return 0;}
    o->write(o->ctx,0x15c,0);
    o->write(o->ctx,0x1ec,1);o->delay(o->ctx,1);o->write(o->ctx,0x1ec,0);
    marble_dsi_ack(o);
    o->write(o->ctx,0x3c,dma);o->write(o->ctx,0x4c,len);
    r->restored=o->read(o->ctx,0x3c)==dma && o->read(o->ctx,0x4c)==len &&
        o->read(o->ctx,0x15c)==0 && o->read(o->ctx,4)==ctrl &&
        (o->read(o->ctx,0x110)&MD_INT_MASKS)==(ints&MD_INT_MASKS);
    r->dirty=!r->restored || r->ack_error || r->timeout_error;
    return r->done && !r->dirty && (write || (marble_dsi_parse_short(r) && r->bytes==expected));
}
static int marble_dsi_read_short(const MarbleDsiOps *o,MarbleDsiResult *r,unsigned command)
{return marble_dsi_transfer(o,r,command,0,0);}
static int marble_dsi_read_power(const MarbleDsiOps *o,MarbleDsiResult *r)
{return marble_dsi_read_short(o,r,0x0a);}
static int marble_dsi_write_brightness(const MarbleDsiOps *o,MarbleDsiResult *r,unsigned level)
{return marble_dsi_transfer(o,r,0x51,level,1);}
static unsigned marble_dsi_dim_target(unsigned original)
{
    if(original<32 || original>4095)return 0;
    unsigned target=original*3/4;
    /* Stay on the same side of the vendor BL_SYNC threshold. No HBM, DC,
     * vendor-page or dimming-mode command is needed by this limited probe. */
    if(original>327 && target<=327)target=328;
    if(target<8 || original-target<16 || ((original>327)!=(target>327)))return 0;
    return target;
}
typedef struct {
    unsigned original,level,prepared,active,dirty,changes;
} MarbleBrightnessLease;
static int marble_brightness_prepare(const MarbleDsiOps *o,MarbleBrightnessLease *p)
{
    if(p->active || p->dirty)return 0;
    *p=(MarbleBrightnessLease){0};MarbleDsiResult r;
    const unsigned cmds[]={0x0a,0xda,0xdb,0xdc,0x52,0x52};
    const unsigned ids[]={0x9c,0xc2,0xb6,0x32};
    for(unsigned i=0;i<6;i++) {
        if(!marble_dsi_read_short(o,&r,cmds[i])) {p->dirty=r.dirty;return 0;}
        if(i<4) {if(r.data[0]!=ids[i])return 0;}
        else {
            unsigned level=((unsigned)r.data[0]<<8)|r.data[1];
            if(i==4)p->original=level;
            else if(level!=p->original)return 0;
        }
    }
    if(!marble_dsi_dim_target(p->original))return 0;
    p->level=p->original;p->prepared=1;return 1;
}
static int marble_brightness_set(const MarbleDsiOps *o,MarbleBrightnessLease *p,unsigned level)
{
    unsigned minimum=p->original>327?328:8;
    if(!p->prepared || p->dirty || level<minimum || level>p->original)return 0;
    if(level==p->level)return 1;
    MarbleDsiResult r;
    /* Armed before the first write: a clean timeout can still have changed
     * panel state. Such a failure must not waive the restoration obligation. */
    p->active=1;p->changes++;
    if(!marble_dsi_write_brightness(o,&r,level)) {p->dirty=r.dirty;return 0;}
    if(!marble_dsi_read_short(o,&r,0x52)) {p->dirty=r.dirty;return 0;}
    if((((unsigned)r.data[0]<<8)|r.data[1])!=level)return 0;
    p->level=level;return 1;
}
static int marble_brightness_restore(const MarbleDsiOps *o,MarbleBrightnessLease *p)
{
    if(p->dirty)return 0;
    if(!p->active)return 1;
    MarbleDsiResult r;
    if(!marble_dsi_write_brightness(o,&r,p->original) ||
       !marble_dsi_read_short(o,&r,0x52) || (((unsigned)r.data[0]<<8)|r.data[1])!=p->original) {
        p->dirty=1;return 0;
    }
    p->level=p->original;p->active=0;return 1;
}
#endif
