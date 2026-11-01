#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../../xnuloader/marble_glink_wire.h"
static void word(uint8_t *p,uint32_t n)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
int main(void)
{
    uint8_t fifo[1024],out[1100],message[48]={0};uint32_t available=0;MarbleGlinkControl m;
    for(unsigned i=0;i<sizeof(fifo);i++)fifo[i]=(uint8_t)i;
    for(unsigned head=0;head<1024;head+=8)for(unsigned tail=0;tail<1024;tail+=8) {
        assert(marble_glink_ring(1024,head,tail,&available));
        for(unsigned count=0;count<=available;count+=8) {
            memset(out,0xa5,sizeof(out));assert(marble_glink_copy(fifo,1024,head,tail,0,out,count));
            for(unsigned i=0;i<count;i++)assert(out[i]==fifo[(tail+i)%1024]);
            assert(out[count]==0xa5);
        }
        assert(!marble_glink_copy(fifo,1024,head,tail,available,out,1));
        assert(!marble_glink_copy(fifo,1024,head,tail,UINT32_MAX,out,1));
    }
    assert(!marble_glink_ring(1024,1024,0,&available));assert(!marble_glink_ring(1024,0,1,&available));
    assert(!marble_glink_ring(65544,0,0,&available));assert(!marble_glink_ring(0,0,0,&available));
    word(message,1u<<16);word(message+4,1);assert(marble_glink_control(message,8,&m)==1 && m.command==0);
    for(unsigned bytes=0;bytes<8;bytes++)assert(marble_glink_control(message,bytes,&m)==0);
    for(unsigned command=0;command<65536;command++) {
        word(message,command|(1u<<16));word(message+4,0);
        int expected=command==0 || command==1 || command==3 || command==11 || command==15;
        assert((marble_glink_control(message,8,&m)==1)==expected);
    }
    word(message,13);assert(marble_glink_control(message,8,&m)==1);word(message+4,1);assert(marble_glink_control(message,8,&m)==-1);
    for(unsigned length=0;length<=48;length++) {
        memset(message,'A',sizeof(message));word(message,2|(7u<<16));word(message+4,length);
        if(length && length<=40)message[8+length-1]=0;
        if(length<2 || length>32)assert(marble_glink_control(message,48,&m)==-1);
        else {
            unsigned total=(8+length+7)&~7u;
            for(unsigned n=8;n<total;n++)assert(marble_glink_control(message,n,&m)==0);
            assert(marble_glink_control(message,total,&m)==1 && m.bytes==total && m.channel==7);
            assert(strlen(m.name)==length-1);
            message[8]=0;assert(marble_glink_control(message,total,&m)==-1);
        }
    }
    word(message+4,UINT32_MAX);assert(marble_glink_control(message,48,&m)==-1);
    uint8_t intents[520]={0};word(intents,5|(3u<<16));word(intents+4,64);
    for(unsigned i=0;i<64;i++) {word(intents+8+8*i,512);word(intents+12+8*i,i+1);}
    assert(marble_glink_control(intents,sizeof(intents),&m)==-1);
    assert(marble_glink_frame(intents,sizeof(intents),&m,1)==1 && m.bytes==520);
    word(intents+12,0);assert(marble_glink_frame(intents,sizeof(intents),&m,1)==1);word(intents+12,1);
    for(unsigned n=8;n<520;n++)assert(marble_glink_frame(intents,n,&m,1)==0);
    word(intents+12+8*63,1);assert(marble_glink_frame(intents,sizeof(intents),&m,1)==-1);
    word(intents+12+8*63,64);word(intents+8,65537);assert(marble_glink_frame(intents,sizeof(intents),&m,1)==-1);
    word(intents+8,0);assert(marble_glink_frame(intents,sizeof(intents),&m,1)==-1);
    word(intents+4,65);assert(marble_glink_frame(intents,sizeof(intents),&m,1)==-1);
    word(message,4|(1u<<16));word(message+4,0);assert(marble_glink_frame(message,8,&m,1)==1);
    assert(marble_glink_control(message,8,&m)==-1);assert(marble_glink_frame(message,8,&m,3)==-1);
    struct {uint64_t first;MarbleGlinkRemoteIntent values[512];uint64_t last;} table;
    memset(&table,0xa5,sizeof(table));unsigned count=0,index=0,added=0;
    for(unsigned i=0;i<512;i++) {
        assert(marble_glink_intent_accept(table.values,512,&count,i,128,&index,&added));
        assert(count==i+1 && index==i && added==1);
    }
    assert(!marble_glink_intent_accept(table.values,512,&count,512,128,&index,&added));assert(count==512);
    assert(marble_glink_intent_accept(table.values,512,&count,0,128,&index,&added) && !added && !index && count==512);
    assert(!marble_glink_intent_accept(table.values,512,&count,0,512,&index,&added));
    assert(!marble_glink_intent_accept(table.values,513,&count,512,128,&index,&added));
    assert(!marble_glink_intent_accept(table.values,0,&count,512,128,&index,&added));
    count=513;assert(!marble_glink_intent_accept(table.values,512,&count,512,128,&index,&added));
    assert(table.first==0xa5a5a5a5a5a5a5a5ULL && table.last==0xa5a5a5a5a5a5a5a5ULL);
    assert(marble_glink_control(NULL,8,&m)==-1);assert(marble_glink_control(message,8,NULL)==-1);
    puts("PASS GLINK: every 1024-byte ring position/wrap, bounded copies, 65536 commands, open truncation/length/NUL/overflow, isolated app ACK/intent metadata; no DATA");
}
