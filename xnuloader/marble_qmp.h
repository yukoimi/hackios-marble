/* Read-only QMP descriptor bounds from the pinned marble qcom_aoss.c.
 * Never negotiates a link, writes a mailbox, or rings an IPCC doorbell. */
#ifndef MARBLE_QMP_H
#define MARBLE_QMP_H
#include <stdint.h>
#include <stddef.h>
#define MARBLE_QMP_MAGIC 0x4d41494cu
#define MARBLE_QMP_UP 0x0000ffffu
#define MARBLE_QMP_DOWN 0xffff0000u
typedef struct {
    uint32_t magic,version,features;
    uint32_t remote_link,remote_link_ack,remote_channel,remote_channel_ack,remote_size,remote_offset;
    uint32_t local_link,local_link_ack,local_channel,local_channel_ack,local_size,local_offset;
} MarbleQmpDescriptor;
_Static_assert(sizeof(MarbleQmpDescriptor)==0x3c,"QMP descriptor wire size");
_Static_assert(offsetof(MarbleQmpDescriptor,local_offset)==0x38,"QMP local mailbox offset");
static int marble_qmp_state(uint32_t x)
{return x==MARBLE_QMP_UP || x==MARBLE_QMP_DOWN;}
static int marble_qmp_extent(uint32_t offset,uint32_t size,uint32_t window)
{return offset>=sizeof(MarbleQmpDescriptor) && !(offset&3) && size>=68 && !(size&3) && offset<=window && size<=window-offset;}
static int marble_qmp_valid(const MarbleQmpDescriptor *d,uint32_t window)
{
    if(!d || d->magic!=MARBLE_QMP_MAGIC || d->version!=1 ||
       !marble_qmp_extent(d->local_offset,d->local_size,window) ||
       !marble_qmp_extent(d->remote_offset,d->remote_size,window))return 0;
    if(!(d->local_offset+d->local_size<=d->remote_offset ||
         d->remote_offset+d->remote_size<=d->local_offset))return 0;
    const uint32_t states[]={d->remote_link,d->remote_link_ack,d->remote_channel,d->remote_channel_ack,
        d->local_link,d->local_link_ack,d->local_channel,d->local_channel_ack};
    for(unsigned i=0;i<8;i++)if(!marble_qmp_state(states[i]))return 0;
    return 1;
}
static int marble_qmp_open(const MarbleQmpDescriptor *d)
{
    return d->remote_link==MARBLE_QMP_UP && d->remote_link_ack==MARBLE_QMP_UP &&
        d->remote_channel==MARBLE_QMP_UP && d->remote_channel_ack==MARBLE_QMP_UP &&
        d->local_link==MARBLE_QMP_UP && d->local_link_ack==MARBLE_QMP_UP &&
        d->local_channel==MARBLE_QMP_UP && d->local_channel_ack==MARBLE_QMP_UP;
}
static int marble_qmp_peer_closed(const MarbleQmpDescriptor *d)
{
    return d->remote_link==MARBLE_QMP_UP && d->remote_link_ack==MARBLE_QMP_UP &&
        d->local_link==MARBLE_QMP_UP && d->local_link_ack==MARBLE_QMP_UP &&
        d->local_channel==MARBLE_QMP_DOWN && d->local_channel_ack==MARBLE_QMP_DOWN &&
        d->remote_channel==MARBLE_QMP_DOWN && d->remote_channel_ack==MARBLE_QMP_DOWN;
}
/* UCORE_CH_STATE belongs to AOP, but UCORE_CH_STATE_ACK belongs to AP.
 * Return 1/2 only for an observed DOWN/UP edge; never acknowledge a value
 * invented by the host. Other descriptor geometry/link changes are errors. */
static int marble_qmp_borrowed_close_step(const MarbleQmpDescriptor *d,
    const MarbleQmpDescriptor *original,unsigned acks)
{
    if(!d || !original || acks>2 || !marble_qmp_valid(d,1024) ||
       d->magic!=original->magic || d->version!=original->version || d->features!=original->features ||
       d->local_link!=original->local_link || d->local_link_ack!=original->local_link_ack ||
       d->remote_link!=original->remote_link || d->remote_link_ack!=original->remote_link_ack ||
       d->local_offset!=original->local_offset || d->local_size!=original->local_size ||
       d->remote_offset!=original->remote_offset || d->remote_size!=original->remote_size ||
       d->local_channel!=MARBLE_QMP_DOWN)return -1;
    if(!acks) {
        if(d->remote_channel_ack!=MARBLE_QMP_UP)return -1;
        return d->remote_channel==MARBLE_QMP_DOWN?1:0;
    }
    if(acks==1) {
        if(d->remote_channel_ack!=MARBLE_QMP_DOWN)return -1;
        return d->remote_channel==MARBLE_QMP_UP?2:0;
    }
    if(d->remote_channel!=MARBLE_QMP_UP || d->remote_channel_ack!=MARBLE_QMP_UP)return -1;
    return d->local_channel_ack==MARBLE_QMP_DOWN?3:0;
}
#endif
