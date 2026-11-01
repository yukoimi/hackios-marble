#include <assert.h>
#include <string.h>
#include "../../xnuloader/marble_smp2p.h"
int main(void)
{
    MarbleSmp2pItem p;marble_smp2p_initialize(&p);
    assert(marble_smp2p_valid(&p,0,2));assert(!marble_smp2p_valid(&p,2,0));
    assert(MARBLE_SMP2P_OUTBOUND==429 && MARBLE_SMP2P_INBOUND==443);
    assert(marble_smp2p_name(p.entries[0].name,"master-kernel"));
    assert(!p.entries[0].value && p.version_features==0x101 && !p.flags);
    for(unsigned count=0;count<65536;count++) {
        p.valid_entries=(uint16_t)count;
        assert(marble_smp2p_valid(&p,0,2)==(count<=1));
    }
    marble_smp2p_initialize(&p);p.valid_entries=2;p.entries[1]=p.entries[0];
    assert(!marble_smp2p_valid(&p,0,2));
    memset(p.entries[1].name,'x',16);assert(!marble_smp2p_valid(&p,0,2));
    strcpy(p.entries[1].name,"other");assert(marble_smp2p_valid(&p,0,2));
    p.flags=4;assert(!marble_smp2p_valid(&p,0,2));
    marble_smp2p_initialize(&p);p.version_features=0x100;assert(!marble_smp2p_valid(&p,0,2));
    marble_smp2p_initialize(&p);p.local_pid=2;p.remote_pid=0;
    memset(p.entries[0].name,0,16);strcpy(p.entries[0].name,"slave-kernel");p.entries[0].value=2;
    uint32_t value=0;assert(marble_smp2p_slave_value(&p,&value) && value==2);
    p.remote_pid=2;assert(!marble_smp2p_slave_value(&p,&value));
    MarbleSmp2pStop s={0};assert(!marble_smp2p_mark_stop(&s));
    assert(marble_smp2p_observe(&s,0));assert(marble_smp2p_observe(&s,2));
    assert(s.phase==1 && s.ready_seen);assert(marble_smp2p_mark_stop(&s));
    assert(marble_smp2p_observe(&s,6));assert(s.phase==2);
    assert(marble_smp2p_observe(&s,14));assert(s.phase==3 && s.stop_ack_seen);
    for(unsigned v=0;v<256;v++) {
        MarbleSmp2pStop t={0};int good=marble_smp2p_observe(&t,v);
        assert(good==!(v&9));
        if(good && (v&2))assert(marble_smp2p_mark_stop(&t));else assert(!marble_smp2p_mark_stop(&t));
    }
    MarbleSmp2pStop stale={0};assert(!marble_smp2p_observe(&stale,10));
    stale=(MarbleSmp2pStop){0};assert(marble_smp2p_observe(&stale,2));assert(!marble_smp2p_observe(&stale,10));
    stale=(MarbleSmp2pStop){0};assert(marble_smp2p_observe(&stale,2));assert(marble_smp2p_mark_stop(&stale));assert(!marble_smp2p_observe(&stale,3));
    return 0;
}
