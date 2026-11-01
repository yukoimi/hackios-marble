/* Native APPS/ADSP FIFO adapter. Uses the same framing decoder as the
 * hardware-tested EFI path. One serialized owner; callback offsets are
 * relative to the qualified SMEM window, never arbitrary physical MMIO.
 * No allocation, native firmware call, IRQ enable or rail operation here.
 * Failed publication retains ownership; reset is the platform's PAS/route
 * shutdown responsibility. Discovering a ring is not a DSP-ready assertion.
 */
#ifndef MARBLE_AUDIO_GLINK_H
#define MARBLE_AUDIO_GLINK_H
#include "marble_audio_smem.h"
#include "marble_audio_wire/marble_glink_data.h"
typedef struct {
    void *cookie;
    int (*read)(void *,uint32_t,void *,uint32_t);
    int (*write)(void *,uint32_t,const void *,uint32_t);
    void (*barrier)(void *);
    int (*kick)(void *);
} MarbleAudioGlinkIO;
typedef struct {
    MarbleAudioGlinkIO io;
    MarbleAudioSmem memory;
    uint32_t head,tail,live,failed,pending_rx,tx_messages,rx_messages;
} MarbleAudioGlink;
static int mag_fail(MarbleAudioGlink *g)
{if(g)g->failed=1;return -1;}
static int mag_read(MarbleAudioGlink *g,uint32_t offset,void *out,uint32_t n)
{return offset<=MASMEM_BYTES && n<=MASMEM_BYTES-offset && g->io.read(g->io.cookie,offset,out,n);}
static int mag_write(MarbleAudioGlink *g,uint32_t offset,const void *in,uint32_t n)
{return offset<=MASMEM_BYTES && n<=MASMEM_BYTES-offset && g->io.write(g->io.cookie,offset,in,n);}
static void mag_put32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static int mag_index(MarbleAudioGlink *g,unsigned index,uint32_t *value)
{
    uint8_t bytes[4];
    if(index>3 || !mag_read(g,g->memory.item[0]+4*index,bytes,4))return 0;
    *value=marble_glink_le32(bytes);return *value<16384 && !(*value&7);
}
static int mag_ring_io(MarbleAudioGlink *g,unsigned rx,uint32_t at,uint8_t *bytes,uint32_t n,unsigned writing)
{
    if(rx>1 || at>=16384 || n>16384 || (at|n)&3)return 0;
    uint32_t first=n<16384-at?n:16384-at,base=g->memory.item[rx?2:1];
    if(writing)return mag_write(g,base+at,bytes,first) && (first==n || mag_write(g,base,bytes+first,n-first));
    return mag_read(g,base+at,bytes,first) && (first==n || mag_read(g,base,bytes+first,n-first));
}
static int marble_audio_glink_init(MarbleAudioGlink *g,const MarbleAudioGlinkIO *io)
{
    if(!g || g->live || !io || !io->read || !io->write || !io->barrier || !io->kick)return 0;
    MarbleAudioGlink next={.io=*io};
    if(!marble_audio_smem_find(io->read,io->cookie,&next.memory) || next.memory.present!=7)return 0;
    uint32_t remote_head,remote_tail;
    if(!mag_index(&next,0,&remote_tail) || !mag_index(&next,1,&next.head) ||
       !mag_index(&next,2,&next.tail) || !mag_index(&next,3,&remote_head) || next.head || next.tail)return 0;
    next.live=1;*g=next;return 1;
}
/* -1 is a latched fault, 0 is ordinary backpressure, 1 is room. The owner
 * checks this before claiming a new stream command/page. */
static int marble_audio_glink_space(MarbleAudioGlink *g,uint32_t frame_bytes)
{
    if(!g || !g->live || g->failed)return -1;
    uint32_t head,tail,used;
    if(frame_bytes<8 || frame_bytes>1040 || frame_bytes&7 || !mag_index(g,1,&head) || head!=g->head ||
       !mag_index(g,0,&tail) || !marble_glink_ring(16384,head,tail,&used))return mag_fail(g);
    return 16384-used>=frame_bytes+16;
}
static int marble_audio_glink_send(MarbleAudioGlink *g,unsigned command,unsigned channel,uint32_t parameter,const uint8_t *payload,unsigned bytes)
{
    if(!g || !g->live || g->failed)return -1;
    unsigned valid=channel<=65535 && ((command<=1 && channel==1 && bytes==0 && parameter<=1) ||
        (command==2 && channel==1 && parameter==10 && bytes==10 && payload) ||
        (command==4 && channel && !parameter && !bytes) ||
        (command==5 && channel==1 && parameter==1 && bytes==8 && payload) ||
        (command==9 && channel==1 && bytes>=32 && bytes<=1032 && payload) ||
        (command==14 && channel==1 && parameter==1 && !bytes));
    if(!valid)return mag_fail(g);
    if(command==2) {
        static const char name[]="adsp_apps";
        for(unsigned i=0;i<10;i++)if(payload[i]!=(uint8_t)name[i])return mag_fail(g);
    }
    if(command==5 && (marble_glink_le32(payload)!=512 || marble_glink_le32(payload+4)!=1))return mag_fail(g);
    unsigned total=8+((bytes+7)&~7u);int space=marble_audio_glink_space(g,total);
    if(space!=1)return space;
    uint8_t frame[1040]={0};mag_put32(frame,command|(channel<<16));mag_put32(frame+4,parameter);
    for(unsigned i=0;i<bytes;i++)frame[8+i]=payload[i];
    if(!mag_ring_io(g,0,g->head,frame,total,1))return mag_fail(g);
    g->io.barrier(g->io.cookie);
    uint32_t head=(g->head+total)&16383;uint8_t encoded[4];mag_put32(encoded,head);
    if(!mag_write(g,g->memory.item[0]+4,encoded,4))return mag_fail(g);
    g->head=head;g->tx_messages++;g->io.barrier(g->io.cookie);
    if(!g->io.kick(g->io.cookie))return mag_fail(g);
    return 1;
}
/* Peek dispatches nothing and never advances the FIFO. A fully validated
 * message is committed only after its owner has handled/retired it. */
static int marble_audio_glink_peek(MarbleAudioGlink *g,uint8_t *frame,unsigned capacity,MarbleGlinkControl *message)
{
    if(!g || !g->live || g->failed || !frame || !message || capacity<528)return -1;
    if(g->pending_rx)return mag_fail(g);
    uint32_t head,tail,available;
    if(!mag_index(g,3,&head) || !mag_index(g,2,&tail) || tail!=g->tail || !marble_glink_ring(16384,head,tail,&available))return mag_fail(g);
    if(!available)return 0;
    g->io.barrier(g->io.cookie);
    unsigned n=available<528?available:528;
    if(!mag_ring_io(g,1,tail,frame,n,0))return mag_fail(g);
    MarbleGlinkControl m;int decoded=marble_glink_frame(frame,n,&m,2);
    if(decoded<0)return mag_fail(g);
    if(!decoded)return 0;
    g->pending_rx=m.bytes;*message=m;return 1;
}
static int marble_audio_glink_commit_rx(MarbleAudioGlink *g)
{
    if(!g || !g->live || g->failed)return 0;
    uint32_t tail;
    if(!g->pending_rx || !mag_index(g,2,&tail) || tail!=g->tail) {mag_fail(g);return 0;}
    uint8_t encoded[4];tail=(tail+g->pending_rx)&16383;mag_put32(encoded,tail);
    g->io.barrier(g->io.cookie);
    if(!mag_write(g,g->memory.item[0]+8,encoded,4)) {mag_fail(g);return 0;}
    g->tail=tail;g->pending_rx=0;g->rx_messages++;g->io.barrier(g->io.cookie);return 1;
}
#endif
