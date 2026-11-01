#ifndef MARBLE_ICB_BANDWIDTH_H
#define MARBLE_ICB_BANDWIDTH_H
#include "marble_icb_lease.h"
#define MARBLE_SCM_PEAK_BYTES (UINT64_C(0xffffffff)*UINT64_C(1000))
typedef struct {
 uint64_t revision,init,create,complete,other[11],destroy,modify,impulse,vector;
} MarbleNpaVectorProtocol;
_Static_assert(offsetof(MarbleNpaVectorProtocol,complete)==0x18,"NPA CompleteRequest offset");
_Static_assert(offsetof(MarbleNpaVectorProtocol,vector)==0x90,"NPA IssueVectorRequest offset");
typedef struct {uint32_t type,pad;uint64_t ib,ab;uint32_t latency,pad2;} MarbleIcbBandwidth;
_Static_assert(sizeof(MarbleIcbBandwidth)==32,"native ICB type3 vector size");
static int marble_icb_bandwidth_binding(const MarbleNpaVectorProtocol *p,uint64_t base)
{
 return p->revision==0x10003 && p->create==base+0x1448 && p->destroy==base+0x15e4 &&
     p->complete==base+0x1474 && p->vector==base+0x162c;
}
static int marble_icb_bandwidth_request(const uint64_t words[4],uint64_t owner,uint64_t ib)
{
 return words[0]==ib && !words[1] && words[2]==owner && !words[3];
}
/* Mu ICBDxe 3734 copies live total to state+30 before recomputing it.
 * Native master state starts at40; slave starts at20. The history word is
 * not a pending queue or current vote and must reflect the held request. */
static int marble_icb_bandwidth_lists(const MarbleIcbRequestList *a,const MarbleIcbRequestList *b,uint64_t peak)
{
 if(a->address!=b->address || a->head_words!=b->head_words || (a->head_words!=25 && a->head_words!=17) ||
    a->count!=b->count || b->capacity<a->capacity || b->capacity>128 ||
    (b->capacity!=a->capacity && b->capacity!=a->capacity+10))return 0;
 unsigned live=a->head_words==25?8:4,history=live+6;
 uint64_t expected=a->head[live]>peak?a->head[live]:peak;
 if(b->head[history]!=expected)return 0;
 for(unsigned i=0;i<a->head_words;i++)if(i!=history && a->head[i]!=b->head[i])return 0;
 for(unsigned i=0;i<a->count;i++) {
  if(a->members[i]!=b->members[i])return 0;
  for(unsigned j=0;j<4;j++)if(a->requests[i][j]!=b->requests[i][j])return 0;
 }
 return 1;
}
static int marble_icb_bandwidth_originals(const MarbleRpmhSnapshot *a,const MarbleRpmhSnapshot *b,uint64_t owner)
{
 if(b->resource_count<a->resource_count || b->resource_count>a->resource_count+8 ||
    b->vote_count<a->vote_count || b->vote_count>a->vote_count+8 ||
    b->active.count!=a->active.count || b->pool.count!=a->pool.count ||
    b->global[2]!=a->global[2] || b->global[3]!=a->global[3] ||
    memcmp(a->manager,b->manager,sizeof(a->manager)))return 0;
 for(unsigned i=0;i<a->resource_count;i++) {
  unsigned found=0;
  for(unsigned j=0;j<b->resource_count;j++)if(a->resources[i][0]==b->resources[j][0]) {
   if(memcmp(a->resources[i],b->resources[j],sizeof(a->resources[i])))return 0;found++;
  }
  if(found!=1)return 0;
 }
 for(unsigned i=0;i<a->vote_count;i++) {
  unsigned found=0;
  for(unsigned j=0;j<b->vote_count;j++)if(a->vote_addresses[i]==b->vote_addresses[j]) {
   if(memcmp(a->votes[i],b->votes[j],sizeof(a->votes[i])))return 0;found++;
  }
  if(found!=1)return 0;
 }
 unsigned owned=0;
 for(unsigned i=0;i<a->active.count;i++) {
  if(a->active.nodes[i]!=b->active.nodes[i] ||
     memcmp(a->client_queues[i],b->client_queues[i],sizeof(a->client_queues[i])))return 0;
  if(a->active.nodes[i]==owner) {
   owned++;
   uint32_t old=(uint32_t)a->clients[i][6],now=(uint32_t)b->clients[i][6];
   if(now<old || now-old>16 || a->clients[i][6]>>32!=UINT32_MAX || b->clients[i][6]>>32!=UINT32_MAX)return 0;
   for(unsigned j=0;j<11;j++)if(j!=6 && a->clients[i][j]!=b->clients[i][j])return 0;
   for(unsigned j=0;j<4;j+=2) {
    const uint64_t *q=b->client_queues[i][j];
    if(q[0]!=(j<2?0x10u:0x18u) || q[1] || q[2])return 0;
   }
  } else if(memcmp(a->clients[i],b->clients[i],sizeof(a->clients[i])))return 0;
 }
 return owned==1;
}
typedef struct {uint64_t address,words[12];} MarbleIcbHardware;
/* Exact Mu 5250/52f0 algorithm for bandwidth BCMs (kind0), before commit bit.
 * Compute from the PRE-request aggregate, independently of the returned vote. */
static int marble_icb_bandwidth_bcm(const MarbleIcbHardware *n,uint32_t *value)
{
 const uint64_t *w=n->words;uint32_t unit=(uint32_t)(w[2]>>32);
 if((uint32_t)w[1]!=0 || !unit || w[7]>UINT64_MAX-w[8] || w[7]+w[8]>UINT64_MAX-(unit-1))return 0;
 uint64_t ib=w[5]>w[6]?w[5]:w[6],ab=(w[7]+w[8]+unit-1)/unit;
 ib=ib?(ib<unit?1:ib/unit):0;
 if(ib>0x3fff)ib=0x3fff;if(ab>0x3fff)ab=0x3fff;
 *value=(uint32_t)(ib|(ab<<14));if(*value)*value|=0x20000000;
 return 1;
}
static int marble_icb_bandwidth_added_vote(const uint64_t vote[9],uint32_t expected)
{
 uint32_t active=(uint32_t)vote[3],ack=(uint32_t)(vote[1]>>32);
 return vote[0]==2 && !(uint32_t)vote[1] && vote[2]==1 &&
     (active&~0x40000000u)==expected && (ack&~0x40000000u)==expected &&
     (vote[3]>>32)==1 && !vote[4] && !vote[5] && !vote[6] && vote[7]==4 && !vote[8];
}
#endif
