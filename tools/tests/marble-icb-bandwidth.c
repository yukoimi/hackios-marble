#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_icb_bandwidth.h"
static MarbleRpmhSnapshot a,b;
static void cache_fixture(void)
{
 memset(&a,0,sizeof(a));a.active.count=1;a.active.nodes[0]=0x1234;
 a.clients[0][6]=0xffffffff00000002ULL;
 for(unsigned i=0;i<4;i++)a.client_queues[0][i][0]=i<2?0x10:0x18;
 a.resource_count=a.vote_count=1;a.resources[0][0]=0x50000;a.resources[0][1]=0x3000;
 a.vote_addresses[0]=0x3000;a.votes[0][0]=2;b=a;
 b.clients[0][6]++;b.global[1]=0x9999;
}
int main(void)
{
 uint64_t base=0x90000000;MarbleNpaVectorProtocol p={0};
 p.revision=0x10003;p.create=base+0x1448;p.destroy=base+0x15e4;
 p.complete=base+0x1474;p.vector=base+0x162c;
 assert(marble_icb_bandwidth_binding(&p,base));
 p.vector=base+0x1644;assert(!marble_icb_bandwidth_binding(&p,base));
 p.vector=base+0x162c;p.destroy=base+0x15b8;assert(!marble_icb_bandwidth_binding(&p,base));
 MarbleIcbBandwidth r={4,0,1000000,0,0,0};uint32_t words[8];memcpy(words,&r,sizeof(r));
 assert(words[0]==4 && !words[1] && words[2]==1000000);
 for(unsigned i=3;i<8;i++)assert(!words[i]);
 r.ib=MARBLE_SCM_PEAK_BYTES;memcpy(words,&r,sizeof(r));
 assert(MARBLE_SCM_PEAK_BYTES==4294967295000ULL && words[0]==4 && !words[1]);
 assert(((uint64_t)words[3]<<32|words[2])==MARBLE_SCM_PEAK_BYTES && words[3]!=0);
 for(unsigned i=4;i<8;i++)assert(!words[i]);
 uint64_t v[4]={1000000,0,0xabcd,0};assert(marble_icb_bandwidth_request(v,0xabcd,1000000));
 for(unsigned i=0;i<4;i++){v[i]^=1;assert(!marble_icb_bandwidth_request(v,0xabcd,1000000));v[i]^=1;}
 v[0]=0;assert(marble_icb_bandwidth_request(v,0xabcd,0));
 static MarbleIcbRequestList old,now;old.address=now.address=0x1000;old.head_words=now.head_words=25;
 now.capacity=10;now.head[14]=1000000;
 assert(marble_icb_bandwidth_lists(&old,&now,1000000));
 for(unsigned i=0;i<25;i++)if(i!=14){now.head[i]^=1;assert(!marble_icb_bandwidth_lists(&old,&now,1000000));now.head[i]^=1;}
 now.head[14]++;assert(!marble_icb_bandwidth_lists(&old,&now,1000000));now.head[14]--;
 old.head_words=now.head_words=17;now.head[14]=0;now.head[10]=1000000;
 assert(marble_icb_bandwidth_lists(&old,&now,1000000));
 old.head[4]=now.head[4]=670000000;now.head[10]=670000000;
 assert(marble_icb_bandwidth_lists(&old,&now,1000000));
 cache_fixture();assert(marble_icb_bandwidth_originals(&a,&b,0x1234));
 assert(!marble_icb_bandwidth_originals(&a,&b,0x5678));
 for(unsigned i=0;i<9;i++){cache_fixture();b.votes[0][i]^=1;assert(!marble_icb_bandwidth_originals(&a,&b,0x1234));}
 cache_fixture();b.clients[0][6]+=20;assert(!marble_icb_bandwidth_originals(&a,&b,0x1234));
 cache_fixture();b.clients[0][6]^=1ULL<<32;assert(!marble_icb_bandwidth_originals(&a,&b,0x1234));
 cache_fixture();b.clients[0][8]=1;assert(!marble_icb_bandwidth_originals(&a,&b,0x1234));
 MarbleIcbHardware hw={0};uint32_t encoded=0;hw.words[2]=1000000ULL<<32;
 assert(marble_icb_bandwidth_bcm(&hw,&encoded) && !encoded);
 hw.words[5]=1;assert(marble_icb_bandwidth_bcm(&hw,&encoded) && encoded==0x20000001);
 hw.words[5]=126000000;assert(marble_icb_bandwidth_bcm(&hw,&encoded) && encoded==0x2000007e);
 hw.words[7]=1000001;assert(marble_icb_bandwidth_bcm(&hw,&encoded) && encoded==0x2000807e);
 hw.words[7]=UINT64_MAX;assert(!marble_icb_bandwidth_bcm(&hw,&encoded));
 hw.words[7]=0;hw.words[5]=UINT64_MAX;assert(marble_icb_bandwidth_bcm(&hw,&encoded) && encoded==0x20003fff);
 hw.words[5]=MARBLE_SCM_PEAK_BYTES;assert(marble_icb_bandwidth_bcm(&hw,&encoded) && encoded==0x20003fff);
 uint64_t vote[9]={2,0x6000007e00000000ULL,1,0x16000007eULL,0,0,0,4,0};
 assert(marble_icb_bandwidth_added_vote(vote,0x2000007e));
 assert(!marble_icb_bandwidth_added_vote(vote,0x2000007f));
 for(unsigned i=0;i<9;i++){vote[i]^=1;assert(!marble_icb_bandwidth_added_vote(vote,0x2000007e));vote[i]^=1;}
 puts("PASS explicit vector/complete binding, native type3 byte layout/word count and owned request/release state");
}
