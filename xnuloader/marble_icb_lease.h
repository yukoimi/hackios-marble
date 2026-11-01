#ifndef MARBLE_ICB_LEASE_H
#define MARBLE_ICB_LEASE_H
#include "marble_rpmh.h"
typedef struct {
    uint64_t revision,init,create_sync_client_ex,other[12],destroy_client;
} MarbleNpaClientProtocol;
_Static_assert(offsetof(MarbleNpaClientProtocol,create_sync_client_ex)==0x10,"native NPA CreateSyncClientEx offset");
_Static_assert(offsetof(MarbleNpaClientProtocol,destroy_client)==0x78,"native NPA DestroyClient offset");
static int marble_npa_client_binding(const MarbleNpaClientProtocol *p,uint64_t base,uint64_t *create,uint64_t *destroy)
{
    if(p->revision!=0x10003 || p->create_sync_client_ex!=base+0x1448 || p->destroy_client!=base+0x15e4)return 0;
    *create=p->create_sync_client_ex;*destroy=p->destroy_client;return 1;
}
typedef struct {
    uint64_t address,head[25],list_header[2],members[64],requests[64][4];
    unsigned head_words,count,capacity;
} MarbleIcbRequestList;
static int marble_icb_requests(MarbleRpmhRead read,uint64_t address,unsigned master,MarbleIcbRequestList *s)
{
    s->address=address;s->head_words=master?25:17;
    if(!marble_rpmh_words(read,address,s->head,s->head_words) ||
       !marble_rpmh_words(read,address+8*s->head_words,s->list_header,2))return 0;
    s->count=(uint32_t)s->list_header[0];s->capacity=(uint32_t)(s->list_header[0]>>32);
    if(s->count>64 || s->capacity>128 || s->count>s->capacity)return 0;
    if(s->count && !marble_rpmh_words(read,s->list_header[1],s->members,s->count))return 0;
    for(unsigned i=0;i<s->count;i++) {
        for(unsigned j=0;j<i;j++)if(s->members[i]==s->members[j])return 0;
        if(!marble_rpmh_words(read,s->members[i],s->requests[i],4))return 0;
    }
    return 1;
}
static int marble_icb_requests_same(const MarbleIcbRequestList *a,const MarbleIcbRequestList *b,unsigned appended,uint64_t request)
{
    if(a->address!=b->address || a->head_words!=b->head_words ||
       b->count!=a->count+appended || b->capacity<a->capacity ||
       (b->capacity!=a->capacity && b->capacity!=a->capacity+10))return 0;
    for(unsigned i=0;i<a->head_words;i++)if(a->head[i]!=b->head[i])return 0;
    for(unsigned i=0;i<a->count;i++) {
        if(a->members[i]!=b->members[i])return 0;
        for(unsigned j=0;j<4;j++)if(a->requests[i][j]!=b->requests[i][j])return 0;
    }
    return !appended || b->members[a->count]==request;
}
#endif
