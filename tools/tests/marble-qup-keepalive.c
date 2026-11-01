#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_qup_keepalive.h"
static MarbleRpmhSnapshot s,before,after;
static void idle_fixture(void)
{
    memset(&before,0,sizeof(before));before.resource_count=before.vote_count=2;
    before.resources[0][0]=0x300b0;before.resources[0][1]=0x12340000;
    before.resources[1][0]=0x5004c;before.resources[1][1]=0x12350000;
    before.vote_addresses[0]=0x12340000;before.vote_addresses[1]=0x12350000;
    const uint64_t idle[]={2,0,1,0x100000000ULL,0,0,0,4,0};
    memcpy(before.votes[1],idle,sizeof(idle));before.active.count=1;
    after=before;after.active.count=2;
}
static void test_cached_zero(void)
{
    idle_fixture();assert(marble_qup_keepalive_baseline(&before,0x5004c)==2);
    assert(marble_qup_keepalive_baseline(&before,0x50050)==1);
    assert(marble_qup_keepalive_others(&before,&after,0x5004c,2));
    /* Every baseline bit matters: nonzero/invalid/pending/other DRV rejected. */
    for(unsigned w=0;w<9;w++)for(unsigned bit=0;bit<64;bit++) {
        idle_fixture();before.votes[1][w]^=1ULL<<bit;
        assert(!marble_qup_keepalive_baseline(&before,0x5004c));
    }
    idle_fixture();before.resources[1][2]=1;assert(!marble_qup_keepalive_baseline(&before,0x5004c));
    idle_fixture();before.resources[0][0]=0x5004c;assert(!marble_qup_keepalive_baseline(&before,0x5004c));
    idle_fixture();before.vote_addresses[0]=before.vote_addresses[1];assert(!marble_qup_keepalive_baseline(&before,0x5004c));
    /* Only the designated DRV2 vote may change; all other owners stay exact. */
    for(unsigned w=0;w<9;w++) {
        idle_fixture();after.votes[0][w]^=1;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
        idle_fixture();after.votes[1][w]^=1;assert(marble_qup_keepalive_others(&before,&after,0x5004c,2));
    }
    for(unsigned i=0;i<2;i++)for(unsigned w=0;w<3;w++) {
        idle_fixture();after.resources[i][w]^=1;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    }
    for(unsigned w=0;w<10;w++) {
        idle_fixture();after.clients[0][w]^=1;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    }
    for(unsigned q=0;q<4;q++)for(unsigned w=0;w<3;w++) {
        idle_fixture();after.client_queues[0][q][w]^=1;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    }
    idle_fixture();after.vote_count++;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    idle_fixture();after.active.nodes[0]++;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    idle_fixture();after.global[0]++;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    idle_fixture();after.manager[0]++;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
    idle_fixture();after.pool.header[0]++;assert(!marble_qup_keepalive_others(&before,&after,0x5004c,2));
}
static void drain_fixture(void)
{
    memset(&s,0,sizeof(s));s.active.count=1;s.active.nodes[0]=0x12340000;
    s.clients[0][0]=2;s.clients[0][1]=0x12350000;s.clients[0][6]=0xffffffff00000001ULL;
    for(unsigned j=0;j<4;j++)s.client_queues[0][j][0]=j<2?0x10:0x18;
}
static int drained(void) {return marble_qup_keepalive_drained(&s,0,0x12340000,0x12350000,1);}
static void test_drain(void)
{
    drain_fixture();assert(drained());
    for(unsigned j=1;j<4;j+=2) {
        drain_fixture();s.client_queues[0][j][0]|=1ULL<<32;
        s.client_queues[0][j][1]=s.client_queues[0][j][2]=0x12360000;assert(drained());
        s.client_queues[0][j][2]++;assert(!drained());
    }
    for(unsigned j=0;j<4;j++)for(unsigned w=0;w<3;w++) {
        drain_fixture();s.client_queues[0][j][w]^=1;assert(!drained());
    }
    for(unsigned w=6;w<10;w++) {
        drain_fixture();s.clients[0][w]^=1;assert(!drained());
    }
    drain_fixture();s.clients[0][6]=1;assert(!drained());
    drain_fixture();s.active.nodes[0]++;assert(!drained());
    drain_fixture();s.clients[0][1]++;assert(!drained());
    drain_fixture();assert(!marble_qup_keepalive_drained(&s,0,0x12340000,0x12350000,0));
}
static void put(unsigned offset,uint32_t value)
{memcpy((uint8_t *)s.votes[0]+offset,&value,4);}
static void fixture(void)
{
    memset(&s,0,sizeof(s));s.resource_count=s.vote_count=1;
    s.resources[0][0]=0x50000;s.resources[0][1]=0x12340000;
    s.vote_addresses[0]=0x12340000;s.votes[0][0]=2;
    put(0xc,MARBLE_QUP_KEEPALIVE_DATA);put(0x10,1);
    put(0x18,MARBLE_QUP_KEEPALIVE_DATA);put(0x1c,1);
    put(0x30,MARBLE_QUP_KEEPALIVE_DATA);put(0x34,1);
}
int main(void)
{
    test_cached_zero();
    test_drain();
    assert(MARBLE_QUP_KEEPALIVE_DATA==((1u<<30)|(1u<<29)|(1u<<14)|1));
    fixture();assert(marble_qup_keepalive_vote(&s,0x50000));
    uint32_t expected=0;before=s;after=s;
    assert(marble_qup_keepalive_peak(&before,&after,0x50000,0,&expected));assert(expected==0x20004001);
    assert(!marble_qup_keepalive_peak(&before,&after,0x50000,1,&expected));
    assert(!marble_qup_keepalive_peak(&before,&after,0x50004,0,&expected));
    assert(!marble_qup_keepalive_peak(&before,&after,0x50000,0,NULL));
    before.votes[0][1]=0;assert(!marble_qup_keepalive_peak(&before,&after,0x50000,0,&expected));
    before=s;after.votes[0][1]=0;assert(!marble_qup_keepalive_peak(&before,&after,0x50000,0,&expected));
    for(unsigned set=0;set<3;set++) {
        unsigned off=0x18+12*set;
        for(unsigned bit=0;bit<32;bit++) {
            fixture();uint32_t v=marble_xo_rpmh_u32(s.votes[0],off);put(off,v^(1u<<bit));
            assert(!marble_qup_keepalive_vote(&s,0x50000));
        }
        fixture();put(off+4,set==1?1:0);assert(!marble_qup_keepalive_vote(&s,0x50000));
    }
    fixture();put(0xc,0);assert(!marble_qup_keepalive_vote(&s,0x50000));
    fixture();put(0x10,0);assert(!marble_qup_keepalive_vote(&s,0x50000));
    fixture();s.votes[0][0]|=1ULL<<32;assert(!marble_qup_keepalive_vote(&s,0x50000));
    fixture();s.votes[0][8]=1;assert(!marble_qup_keepalive_vote(&s,0x50000));
    fixture();assert(!marble_qup_keepalive_vote(&s,0x300b0));assert(!marble_qup_keepalive_vote(&s,0x50001));
    fixture();s.resource_count=2;s.resources[1][0]=0x50000;assert(!marble_qup_keepalive_vote(&s,0x50000));
    fixture();s.resources[0][2]=1;assert(!marble_qup_keepalive_vote(&s,0x50000));
    puts("PASS minimal QUP2 BCM: exact x=y=1, ACTIVE/WAKE only, no SLEEP, active ACK, no pending vote, address class, duplicates and data/validity mutations");
}
