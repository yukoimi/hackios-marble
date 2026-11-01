/* Native ICB route trees: bounded RAM metadata snapshots only. */
#ifndef MARBLE_ICB_H
#define MARBLE_ICB_H
#include "marble_rpmh.h"
#define MARBLE_ICB_TREES 8
#define MARBLE_ICB_NODES 4096
typedef struct {
    uint64_t address,words[4],route[2],pairs[32];
    unsigned tree;
} MarbleIcbNode;
typedef struct {
    uint64_t config,header[3],resources[MARBLE_ICB_TREES],resource_words[MARBLE_ICB_TREES][3];
    unsigned trees,count;
    MarbleIcbNode nodes[MARBLE_ICB_NODES];
} MarbleIcbSnapshot;
static int marble_icb_tree(MarbleRpmhRead read,uint64_t at,unsigned tree,
    uint64_t lo,uint64_t hi,unsigned depth,MarbleIcbSnapshot *s)
{
    if(!at)return 1;
    if(depth>=32 || s->count>=MARBLE_ICB_NODES)return 0;
    for(unsigned i=0;i<s->count;i++)if(s->nodes[i].address==at)return 0;
    MarbleIcbNode *n=&s->nodes[s->count++];n->address=at;n->tree=tree;
    if(!marble_rpmh_words(read,at,n->words,4))return 0;
    uint64_t key=n->words[0];
    if(key<lo || key>hi || (key>>32)>1024 || (uint32_t)key>1024 ||
       !marble_rpmh_words(read,n->words[1],n->route,2) ||
       !(uint32_t)n->route[0] || (uint32_t)n->route[0]>16 ||
       !marble_rpmh_words(read,n->route[1],n->pairs,2*(uint32_t)n->route[0]))return 0;
    for(unsigned i=0;i<2*(uint32_t)n->route[0];i++)if(!n->pairs[i])return 0;
    if(n->words[2] && (!key || !marble_icb_tree(read,n->words[2],tree,lo,key-1,depth+1,s)))return 0;
    if(n->words[3] && (key==UINT64_MAX || !marble_icb_tree(read,n->words[3],tree,key+1,hi,depth+1,s)))return 0;
    return 1;
}
static int marble_icb_snapshot(MarbleRpmhRead read,uint64_t config,MarbleIcbSnapshot *s)
{
    s->config=config;s->count=0;s->trees=0;
    if(!marble_rpmh_words(read,config,s->header,3) ||
       !(uint32_t)s->header[1] || (uint32_t)s->header[1]>MARBLE_ICB_TREES)return 0;
    s->trees=(uint32_t)s->header[1];
    if(!marble_rpmh_words(read,s->header[2],s->resources,s->trees))return 0;
    for(unsigned i=0;i<s->trees;i++) {
        if(!marble_rpmh_words(read,s->resources[i],s->resource_words[i],3) ||
           !marble_icb_tree(read,s->resource_words[i][2],i,0,UINT64_MAX,0,s))return 0;
    }
    return s->count!=0;
}
#endif
