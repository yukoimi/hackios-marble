/* Wire layout from the pinned marble Linux drivers/soc/qcom/smp2p.c.
 * DT qcom,smem is [INBOUND, OUTBOUND]: ADSP->APPS 443, APPS->ADSP 429.
 * This header handles bounded byte layouts only; no MMIO, SMEM or allocation. */
#ifndef MARBLE_SMP2P_H
#define MARBLE_SMP2P_H
#include <stdint.h>
#include <stddef.h>
#define MARBLE_SMP2P_MAGIC 0x504d5324u
#define MARBLE_SMP2P_OUTBOUND 429u
#define MARBLE_SMP2P_INBOUND 443u
typedef struct { char name[16];uint32_t value; } MarbleSmp2pEntry;
typedef struct {
    uint32_t magic,version_features;
    uint16_t local_pid,remote_pid,total_entries,valid_entries;
    uint32_t flags;
    MarbleSmp2pEntry entries[16];
} MarbleSmp2pItem;
_Static_assert(sizeof(MarbleSmp2pItem)==340,"SMP2P exact wire size");
_Static_assert(offsetof(MarbleSmp2pItem,entries)==20,"SMP2P entries offset");
_Static_assert(offsetof(MarbleSmp2pEntry,value)==16,"SMP2P value offset");
static int marble_smp2p_name(const char a[16],const char *b)
{
    for(unsigned i=0;i<16;i++) {if(a[i]!=b[i])return 0;if(!a[i])return 1;}
    return 0;
}
static int marble_smp2p_valid(const MarbleSmp2pItem *p,unsigned local,unsigned remote)
{
    if(!p || p->magic!=MARBLE_SMP2P_MAGIC || (p->version_features&255)!=1 ||
       p->local_pid!=local || p->remote_pid!=remote || local==remote ||
       p->total_entries!=16 || p->valid_entries>16 || (p->flags&~3u))return 0;
    for(unsigned i=0;i<p->valid_entries;i++) {
        unsigned end=0;for(unsigned j=0;j<16;j++)if(!p->entries[i].name[j]) {end=j;break;}
        if(!end)return 0;
        for(unsigned j=0;j<i;j++)if(marble_smp2p_name(p->entries[i].name,p->entries[j].name))return 0;
    }
    return 1;
}
static void marble_smp2p_initialize(MarbleSmp2pItem *p)
{
    uint8_t *bytes=(void *)p;for(unsigned i=0;i<sizeof(*p);i++)bytes[i]=0;
    p->magic=MARBLE_SMP2P_MAGIC;p->local_pid=0;p->remote_pid=2;
    p->total_entries=16;p->valid_entries=1;
    static const char name[]="master-kernel";
    for(unsigned i=0;i<sizeof(name);i++)p->entries[0].name[i]=name[i];
    /* Callers publish this word only after the rest of the structure. */
    p->version_features=0x101;
}
static int marble_smp2p_slave_value(const MarbleSmp2pItem *p,uint32_t *value)
{
    if(!value || !marble_smp2p_valid(p,2,0))return 0;
    for(unsigned i=0;i<p->valid_entries;i++)if(marble_smp2p_name(p->entries[i].name,"slave-kernel")) {
        *value=p->entries[i].value;return 1;
    }
    return 0;
}
typedef struct { unsigned phase,ready_seen,stop_ack_seen;uint32_t previous; } MarbleSmp2pStop;
/* phase: 0 waiting ready, 1 ready, 2 stop requested, 3 acked, 4 invalid.
 * DT slave-kernel: fatal bit0, ready bit1, stop-ack bit3. A pre-existing or
 * unsolicited ACK must never authorize PAS shutdown or payload restoration. */
static int marble_smp2p_observe(MarbleSmp2pStop *s,uint32_t value)
{
    if(!s || s->phase>=4)return 0;
    if((value&1) || ((value&8) && s->phase<2)) {s->phase=4;return 0;}
    if(s->phase==0 && (value&2)) {s->phase=1;s->ready_seen=1;}
    if(s->phase==2 && (value&8) && !(s->previous&8)) {s->phase=3;s->stop_ack_seen=1;}
    s->previous=value;return 1;
}
static int marble_smp2p_mark_stop(MarbleSmp2pStop *s)
{
    if(!s || s->phase!=1 || !s->ready_seen || (s->previous&9))return 0;
    s->phase=2;return 1;
}
#endif
