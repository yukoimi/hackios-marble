#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_xo_rpmh.h"
static MarbleRpmhSnapshot a,b;
static void fixture(void)
{
 memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));
 a.resource_count=a.vote_count=a.active.count=1;
 a.resources[0][0]=0x300c0;a.resources[0][1]=0x2000;
 a.vote_addresses[0]=0x2000;a.votes[0][0]=2;a.active.nodes[0]=0x3000;
 a.clients[0][1]=0x4567;b=a;
 b.global[1]=0xabcd; /* Native realloc changes only the array base. */
 b.resource_count=b.vote_count=b.active.count=2;
 memcpy(b.resources[1],b.resources[0],sizeof(b.resources[0]));
 b.resources[0][0]=0x300b0;b.resources[0][1]=0x4000;
 b.vote_addresses[1]=0x4000;b.votes[1][0]=2;b.votes[1][1]=3ULL<<32;
 b.votes[1][2]=1;b.votes[1][3]=3|(1ULL<<32);
 b.votes[1][4]=3ULL<<32;b.votes[1][5]=1;b.votes[1][6]=3|(1ULL<<32);
 b.active.nodes[1]=0x5000;b.clients[0][10]=0x5000;
}
int main(void)
{
 MarbleXoRpmhProtocol p={0};uint64_t base=0x90000000;
 p.revision=0x10001;p.create=base+0x16ec;p.destroy=base+0x1760;
 p.find=base+0x1778;p.churn_all=base+0x17b4;p.issue=base+0x1890;
 assert(marble_xo_rpmh_binding(&p,base));p.churn_all=base+0x17e4;
 assert(!marble_xo_rpmh_binding(&p,base));p.churn_all=base+0x17b4;
 p.issue=base+0x1888;assert(!marble_xo_rpmh_binding(&p,base));
 fixture();assert(marble_xo_rpmh_originals(&a,&b));assert(marble_xo_rpmh_vote(&b,1));
 for(unsigned i=0;i<9;i++){fixture();b.votes[0][i]^=1;assert(!marble_xo_rpmh_originals(&a,&b));}
 for(unsigned i=0;i<10;i++){fixture();b.clients[0][i]^=1;assert(!marble_xo_rpmh_originals(&a,&b));}
 for(unsigned set=0;set<3;set++)for(unsigned bad=0;bad<5;bad++) {
  if(bad==3)continue;
  fixture();unsigned off=0x18+12*set;uint32_t word=bad;
  memcpy((unsigned char *)b.votes[1]+off,&word,4);assert(!marble_xo_rpmh_vote(&b,1));
 }
 fixture();b.votes[1][2]=0;assert(!marble_xo_rpmh_vote(&b,1));assert(marble_xo_rpmh_vote(&b,0));
 fixture();b.votes[1][8]=0x9999;assert(!marble_xo_rpmh_vote(&b,1));
 fixture();b.resources[0][2]=0x9999;assert(!marble_xo_rpmh_vote(&b,1));
 fixture();b.votes[1][0]|=1ULL<<32;assert(!marble_xo_rpmh_vote(&b,1));
 fixture();b.resource_count++;assert(!marble_xo_rpmh_originals(&a,&b));
 fixture();b.client_queues[0][0][0]^=1;assert(!marble_xo_rpmh_originals(&a,&b));
 puts("PASS explicit RPMh ABI, realloc and owner preservation, separate sleep/wake/active values/validity, active acknowledgement");
}
