#ifndef MARBLE_XO_RPMH_H
#define MARBLE_XO_RPMH_H
#include "marble_rpmh.h"
/* Revision is a real table slot. Keep offsets explicit at the call boundary. */
typedef struct {
    uint64_t revision,init,valid,num,create_ex,create_explicit,create;
    uint64_t invalidate,callback,destroy,find,churn_all,churn_single;
    uint64_t finish_amc,finish_sleep_wake,issue_set,invalidate_sleep;
    uint64_t invalidate_wake,read,issue;
} MarbleXoRpmhProtocol;
_Static_assert(offsetof(MarbleXoRpmhProtocol,churn_all)==0x58,"RPMh ChurnAll offset");
_Static_assert(offsetof(MarbleXoRpmhProtocol,issue)==0x98,"RPMh IssueCommand offset");
static int marble_xo_rpmh_binding(const MarbleXoRpmhProtocol *p,uint64_t base)
{
    return base && base<UINT64_MAX-0xe0f8 && p->revision==0x10001 &&
        p->create==base+0x16ec && p->destroy==base+0x1760 &&
        p->find==base+0x1778 && p->churn_all==base+0x17b4 && p->issue==base+0x1890;
}
/* A native resource-array realloc is expected. Compare retained owners by
 * address and vote-list identity/value, never by the old array base. */
static int marble_xo_rpmh_originals(const MarbleRpmhSnapshot *a,const MarbleRpmhSnapshot *b)
{
    if(b->resource_count!=a->resource_count+1 || b->vote_count!=a->vote_count+1 ||
       b->active.count!=a->active.count+1 || b->pool.count!=a->pool.count ||
       b->global[2]!=a->global[2] || b->global[3]!=a->global[3] ||
       memcmp(a->manager,b->manager,sizeof(a->manager)))return 0;
    for(unsigned i=0;i<a->resource_count;i++) {
        unsigned found=0;
        for(unsigned j=0;j<b->resource_count;j++)if(a->resources[i][0]==b->resources[j][0]) {
            if(memcmp(a->resources[i],b->resources[j],sizeof(a->resources[i])))return 0;
            found++;
        }
        if(found!=1)return 0;
    }
    for(unsigned i=0;i<a->vote_count;i++) {
        unsigned found=0;
        for(unsigned j=0;j<b->vote_count;j++)if(a->vote_addresses[i]==b->vote_addresses[j]) {
            if(memcmp(a->votes[i],b->votes[j],sizeof(a->votes[i])))return 0;
            found++;
        }
        if(found!=1)return 0;
    }
    for(unsigned i=0;i<a->active.count;i++) {
        if(a->active.nodes[i]!=b->active.nodes[i] ||
           memcmp(a->clients[i],b->clients[i],10*sizeof(uint64_t)) ||
           memcmp(a->client_queues[i],b->client_queues[i],sizeof(a->client_queues[i])))return 0;
    }
    return 1;
}
static uint32_t marble_xo_rpmh_u32(const uint64_t *words,unsigned offset)
{
    return (uint32_t)(words[offset/8]>>(32*((offset&7)/4)));
}
static int marble_xo_rpmh_vote(const MarbleRpmhSnapshot *s,unsigned require_ack)
{
    uint64_t ptr=0;unsigned found=0;
    for(unsigned i=0;i<s->resource_count;i++)if((uint32_t)s->resources[i][0]==0x300b0) {
        if(s->resources[i][0]!=0x300b0 || s->resources[i][2])return 0;
        ptr=s->resources[i][1];found++;
    }
    if(found!=1 || !ptr)return 0;
    for(unsigned i=0;i<s->vote_count;i++)if(s->vote_addresses[i]==ptr) {
        const uint64_t *v=s->votes[i];
        if((uint32_t)v[0]!=2 || v[8])return 0;
        for(unsigned set=0;set<3;set++) {
            unsigned off=0x18+12*set;
            if(marble_xo_rpmh_u32(v,off)!=3 ||
               (marble_xo_rpmh_u32(v,off+4)&255)!=1)return 0;
        }
        return !require_ack || (marble_xo_rpmh_u32(v,0xc)==3 && (v[2]&255)==1 && (uint32_t)(v[0]>>32)==0);
    }
    return 0;
}
#endif
