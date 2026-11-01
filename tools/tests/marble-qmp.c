#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_qmp.h"
int main(void)
{
    MarbleQmpDescriptor d={0};d.magic=MARBLE_QMP_MAGIC;d.version=1;
    d.local_offset=0x100;d.remote_offset=0x200;d.local_size=d.remote_size=0x100;
    uint32_t *states[]={&d.local_link,&d.local_link_ack,&d.local_channel,&d.local_channel_ack,
        &d.remote_link,&d.remote_link_ack,&d.remote_channel,&d.remote_channel_ack};
    for(unsigned i=0;i<8;i++)*states[i]=MARBLE_QMP_UP;
    assert(marble_qmp_valid(&d,0x400) && marble_qmp_open(&d));
    assert(!marble_qmp_peer_closed(&d));
    for(unsigned i=0;i<8;i++) {
        *states[i]=MARBLE_QMP_DOWN;assert(marble_qmp_valid(&d,0x400) && !marble_qmp_open(&d));
        *states[i]=0;assert(!marble_qmp_valid(&d,0x400));*states[i]=MARBLE_QMP_UP;
    }
    d.local_size=UINT32_MAX;assert(!marble_qmp_valid(&d,0x400));d.local_size=0x100;
    d.local_offset=0xffffff00;assert(!marble_qmp_valid(&d,0x400));d.local_offset=0x100;
    d.remote_offset=0x100;assert(!marble_qmp_valid(&d,0x400));d.remote_offset=0x200;
    for(unsigned off=0;off<=0x440;off++)for(unsigned size=0;size<=0x440;size++) {
        int expected=off>=0x3c && !(off&3) && size>=68 && !(size&3) && off<=0x400 && size<=0x400-off;
        assert(marble_qmp_extent(off,size,0x400)==expected);
    }
    for(unsigned mask=0;mask<256;mask++) {
        for(unsigned i=0;i<8;i++)*states[i]=(mask&(1u<<i))?MARBLE_QMP_DOWN:MARBLE_QMP_UP;
        assert(marble_qmp_peer_closed(&d)==(mask==0xcc));
        assert(marble_qmp_open(&d)==(mask==0));
    }
    for(unsigned i=0;i<8;i++)*states[i]=MARBLE_QMP_UP;
    d.local_channel=d.local_channel_ack=MARBLE_QMP_DOWN;
    MarbleQmpDescriptor original=d;
    assert(marble_qmp_borrowed_close_step(&d,&original,0)==0);
    d.remote_channel=MARBLE_QMP_DOWN;
    assert(marble_qmp_borrowed_close_step(&d,&original,0)==1);
    d.remote_channel_ack=MARBLE_QMP_DOWN;
    assert(marble_qmp_borrowed_close_step(&d,&original,1)==0);
    d.remote_channel=MARBLE_QMP_UP;
    assert(marble_qmp_borrowed_close_step(&d,&original,1)==2);
    d.remote_channel_ack=MARBLE_QMP_UP;
    assert(marble_qmp_borrowed_close_step(&d,&original,2)==3);
    d.local_channel_ack=MARBLE_QMP_UP;
    assert(marble_qmp_borrowed_close_step(&d,&original,2)==0);
    d.local_channel_ack=MARBLE_QMP_DOWN;
    for(unsigned i=0;i<sizeof(d)/4;i++) {
        MarbleQmpDescriptor changed=d;
        ((uint32_t *)&changed)[i]^=1;
        assert(marble_qmp_borrowed_close_step(&changed,&original,2)==-1);
    }
    assert(marble_qmp_borrowed_close_step(&d,&original,3)==-1);
    assert(marble_qmp_borrowed_close_step(NULL,&original,0)==-1);
    assert(marble_qmp_borrowed_close_step(&d,NULL,0)==-1);
    assert(marble_qmp_borrowed_close_step(&d,&original,1)==-1);
    assert(!marble_qmp_valid(NULL,0x400));d.version=2;assert(!marble_qmp_valid(&d,0x400));
    puts("PASS QMP layout: 256 open/peer-close states, 1185921 extent boundaries, wrap, overlap, version");
}
