/* Bounded native RPMh metadata reader. No allocation, MMIO or firmware calls.
 * Layout is qualified separately against the supplied marble RpmhDxe. */
#ifndef MARBLE_RPMH_H
#define MARBLE_RPMH_H
#include <stdint.h>
#include <stddef.h>
#define MARBLE_RPMH_CLIENTS 64
#define MARBLE_RPMH_RESOURCES 512
#define MARBLE_RPMH_VOTES 1024
typedef int (*MarbleRpmhRead)(uint64_t,uint64_t *);
/* Last bounded reader stage, for refusal diagnostics only. */
static unsigned marble_rpmh_stage,marble_rpmh_index;
typedef struct {
    uint64_t address,header[3],nodes[MARBLE_RPMH_CLIENTS];
    uint32_t count;
} MarbleRpmhQueue;
static int marble_rpmh_words(MarbleRpmhRead read,uint64_t at,uint64_t *out,unsigned n)
{
    if(!at || (at&3) || n>1024 || at>UINT64_MAX-(uint64_t)n*8)return 0;
    for(unsigned i=0;i<n;i++)if(!read(at+8*i,out+i))return 0;
    return 1;
}
static int marble_rpmh_queue(MarbleRpmhRead read,uint64_t at,unsigned link,MarbleRpmhQueue *q)
{
    q->address=at;q->count=0;
    if(!marble_rpmh_words(read,at,q->header,3) || (uint32_t)q->header[0]!=link ||
       q->header[0]>>32>MARBLE_RPMH_CLIENTS)return 0;
    unsigned count=(unsigned)(q->header[0]>>32);uint64_t node=q->header[1],last=0;
    if(!count)return !node && !q->header[2];
    while(node && q->count<count) {
        for(unsigned i=0;i<q->count;i++)if(q->nodes[i]==node)return 0;
        if(node>UINT64_MAX-link || (node&3))return 0;
        q->nodes[q->count++]=node;last=node;
        if(!read(node+link,&node))return 0;
    }
    return !node && q->count==count && last==q->header[2];
}
typedef struct {
    uint64_t global[4],manager[2];
    MarbleRpmhQueue active,pool;
    uint64_t clients[MARBLE_RPMH_CLIENTS][11];
    uint64_t client_queues[MARBLE_RPMH_CLIENTS][4][3];
    uint64_t resources[MARBLE_RPMH_RESOURCES][3];
    uint64_t vote_addresses[MARBLE_RPMH_VOTES],votes[MARBLE_RPMH_VOTES][9];
    uint32_t resource_count,vote_count;
} MarbleRpmhSnapshot;
static int marble_rpmh_snapshot(MarbleRpmhRead read,uint64_t global,MarbleRpmhSnapshot *s)
{
    s->vote_count=0;s->resource_count=0;marble_rpmh_stage=1;marble_rpmh_index=0;
    if(!marble_rpmh_words(read,global,s->global,4) || (uint32_t)s->global[0]!=1 ||
       (s->global[0]>>32)>MARBLE_RPMH_RESOURCES)return 0;
    marble_rpmh_stage=2;
    if(!marble_rpmh_words(read,s->global[3],s->manager,2))return 0;
    marble_rpmh_stage=3;
    if(!marble_rpmh_queue(read,s->manager[0],0x50,&s->active))return 0;
    marble_rpmh_stage=4;
    if(!marble_rpmh_queue(read,s->manager[1],0x50,&s->pool))return 0;
    for(unsigned i=0;i<s->active.count;i++) {
        marble_rpmh_stage=5;marble_rpmh_index=i;
        if(!marble_rpmh_words(read,s->active.nodes[i],s->clients[i],11))return 0;
        for(unsigned j=0;j<4;j++) {
            marble_rpmh_stage=6+j;
            /* Native clients own four inq headers, at +10,+18,+20,+28. */
            if(!marble_rpmh_words(read,s->clients[i][2+j],s->client_queues[i][j],3) ||
               (uint32_t)s->client_queues[i][j][0]!=(j<2?0x10:0x18))return 0;
        }
        for(unsigned j=0;j<s->pool.count;j++)if(s->active.nodes[i]==s->pool.nodes[j])return 0;
    }
    s->resource_count=(uint32_t)(s->global[0]>>32);
    for(unsigned i=0;i<s->resource_count;i++) {
        marble_rpmh_stage=10;marble_rpmh_index=i;
        uint64_t at=s->global[1]+24*i;
        if(at<s->global[1] || !marble_rpmh_words(read,at,s->resources[i],3))return 0;
        uint32_t address=(uint32_t)s->resources[i][0];
        if(!address || (i && address<=(uint32_t)s->resources[i-1][0]))return 0;
        uint64_t vote=s->resources[i][1];unsigned depth=0;
        while(vote) {
            marble_rpmh_stage=11;
            if(s->vote_count>=MARBLE_RPMH_VOTES || depth++>=8)return 0;
            for(unsigned j=0;j<s->vote_count;j++)if(s->vote_addresses[j]==vote)return 0;
            unsigned n=s->vote_count++;
            s->vote_addresses[n]=vote;
            if(!marble_rpmh_words(read,vote,s->votes[n],9))return 0;
            vote=s->votes[n][8];
        }
    }
    marble_rpmh_stage=12;
    return 1;
}
static int marble_rpmh_client_empty(MarbleRpmhRead read,uint64_t node,uint64_t name)
{
    uint64_t words[11],q[3];
    if(!marble_rpmh_words(read,node,words,11) || (uint32_t)words[0]!=2 || words[1]!=name ||
       (uint32_t)words[6] || (uint32_t)words[7] || words[8] || words[9])return 0;
    for(unsigned i=0;i<4;i++) {
        if(!marble_rpmh_words(read,words[2+i],q,3) ||
           q[0]!=(i<2?0x10u:0x18u) || q[1] || q[2])return 0;
    }
    return 1;
}
#endif
