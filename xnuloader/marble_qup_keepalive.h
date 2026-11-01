/* QUP2's post-init BCM keepalive: x=y=1 for ACTIVE/WAKE only.
 * waipio.c + bcm-voter.c + tcs.h from the pinned board kernel. Never use
 * init's 16000 vote, SLEEP votes, CPU/GPU frequency or voltage requests. */
#ifndef MARBLE_QUP_KEEPALIVE_H
#define MARBLE_QUP_KEEPALIVE_H
#include "marble_xo_rpmh.h"
#define MARBLE_QUP_KEEPALIVE_DATA 0x60004001u
/* 1: uncached; 2: exact observed native DRV2 idle/zero cache; 0: refuse.
 * Native resource votes are per DRV, not per client. Never overwrite a
 * nonzero vote, another DRV, pending command, SLEEP vote or vote chain. */
static unsigned marble_qup_keepalive_baseline(const MarbleRpmhSnapshot *s,uint32_t address)
{
    if(!s || !address || (address&3) || ((address>>16)&7)!=5)return 0;
    unsigned found=0;uint64_t pointer=0;
    for(unsigned i=0;i<s->resource_count;i++)if((uint32_t)s->resources[i][0]==address) {
        if(s->resources[i][0]!=address || s->resources[i][2])return 0;
        pointer=s->resources[i][1];found++;
    }
    if(!found)return 1;
    if(found!=1 || !pointer)return 0;
    static const uint64_t idle[]={2,0,1,0x100000000ULL,0,0,0,4,0};
    unsigned matched=0;
    for(unsigned i=0;i<s->vote_count;i++)if(s->vote_addresses[i]==pointer) {
        if(memcmp(s->votes[i],idle,sizeof(idle)))return 0;
        matched++;
    }
    return matched==1?2:0;
}
static int marble_qup_keepalive_others(const MarbleRpmhSnapshot *a,const MarbleRpmhSnapshot *b,uint32_t address,unsigned baseline)
{
    if(baseline==1)return marble_xo_rpmh_originals(a,b);
    if(baseline!=2 || marble_qup_keepalive_baseline(a,address)!=2 ||
       b->resource_count!=a->resource_count || b->vote_count!=a->vote_count ||
       b->active.count!=a->active.count+1 || b->active.address!=a->active.address ||
       b->pool.count!=a->pool.count || b->pool.address!=a->pool.address ||
       memcmp(a->pool.header,b->pool.header,sizeof(a->pool.header)) ||
       memcmp(a->global,b->global,sizeof(a->global)) || memcmp(a->manager,b->manager,sizeof(a->manager)) ||
       memcmp(a->resources,b->resources,a->resource_count*sizeof(a->resources[0])))return 0;
    uint64_t target=0;
    for(unsigned i=0;i<a->resource_count;i++)if(a->resources[i][0]==address)target=a->resources[i][1];
    for(unsigned i=0;i<a->vote_count;i++) {
        if(a->vote_addresses[i]!=b->vote_addresses[i])return 0;
        if(a->vote_addresses[i]!=target && memcmp(a->votes[i],b->votes[i],sizeof(a->votes[i])))return 0;
    }
    for(unsigned i=0;i<a->active.count;i++)if(a->active.nodes[i]!=b->active.nodes[i] ||
       memcmp(a->clients[i],b->clients[i],10*sizeof(uint64_t)) ||
       memcmp(a->client_queues[i],b->client_queues[i],sizeof(a->client_queues[i])))return 0;
    return target!=0;
}
static int marble_qup_keepalive_vote(const MarbleRpmhSnapshot *s,uint32_t address)
{
    if(!s || !address || (address&3) || ((address>>16)&7)!=5)return 0;
    uint64_t pointer=0;unsigned found=0;
    for(unsigned i=0;i<s->resource_count;i++)if((uint32_t)s->resources[i][0]==address) {
        if(s->resources[i][0]!=address || s->resources[i][2])return 0;
        pointer=s->resources[i][1];found++;
    }
    if(found!=1 || !pointer)return 0;
    for(unsigned i=0;i<s->vote_count;i++)if(s->vote_addresses[i]==pointer) {
        const uint64_t *v=s->votes[i];
        if((uint32_t)v[0]!=2 || v[0]>>32 || v[8] || marble_xo_rpmh_u32(v,0xc)!=MARBLE_QUP_KEEPALIVE_DATA || (v[2]&255)!=1)return 0;
        for(unsigned set=0;set<3;set++) {
            unsigned off=0x18+12*set;uint32_t data=marble_xo_rpmh_u32(v,off),valid=marble_xo_rpmh_u32(v,off+4)&255;
            if(set==1) {if(data || valid)return 0;}
            else if(data!=MARBLE_QUP_KEEPALIVE_DATA || valid!=1)return 0;
        }
        return 1;
    }
    return 0;
}
/* Same native completion contract used by the validated XO lease. Queues
 * 1/3 are reusable allocation pools, not outstanding hardware commands. */
static int marble_qup_keepalive_drained(const MarbleRpmhSnapshot *s,unsigned n,uint64_t handle,uint64_t name,uint32_t last)
{
    if(!s || n>=s->active.count || !last || last==UINT32_MAX || s->active.nodes[n]!=handle ||
       (uint32_t)s->clients[n][0]!=2 || s->clients[n][1]!=name ||
       (uint32_t)s->clients[n][6]!=last || (uint32_t)(s->clients[n][6]>>32)!=UINT32_MAX ||
       s->clients[n][7] || s->clients[n][8] || s->clients[n][9])return 0;
    for(unsigned j=0;j<4;j++) {
        const uint64_t *q=s->client_queues[n][j];
        if(j==0 || j==2) {if(q[0]!=(j<2?0x10u:0x18u) || q[1] || q[2])return 0;}
        else {
            unsigned count=(unsigned)(q[0]>>32);
            if((uint32_t)q[0]!=(j<2?0x10u:0x18u) || count>1 ||
               (!count && (q[1] || q[2])) || (count && (!q[1] || q[1]!=q[2])))return 0;
        }
    }
    return 1;
}
static int marble_qup_keepalive_peak(const MarbleRpmhSnapshot *before,const MarbleRpmhSnapshot *held,uint32_t address,uint32_t logical,uint32_t *expected)
{
    /* The ICB graph's QUP aggregate is still zero. A separately qualified
     * retained QUP lease supplies the positive floor; verify BOTH snapshots
     * rather than pretending that the ICB bandwidth generated that floor. */
    if(!expected || logical || !marble_qup_keepalive_vote(before,address) || !marble_qup_keepalive_vote(held,address))return 0;
    *expected=MARBLE_QUP_KEEPALIVE_DATA&~0x40000000u;return 1;
}
#endif
