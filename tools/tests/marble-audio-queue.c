#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/sys/marble_audio_queue.h"
static MarbleAudioQueue q;
static int16_t source[2048],out[514];
static void pattern(unsigned base)
{for(unsigned i=0;i<1024;i++){source[2*i]=(int16_t)(base+i);source[2*i+1]=(int16_t)~(base+i);}}
int main(void)
{
    for(unsigned pass=0;pass<20;pass++) {
        memset(&q,0,sizeof(q));if(pass&1)q.head=q.tail=UINT32_MAX-13;
        for(unsigned i=0;i<4;i++){pattern(i*1024);assert(marble_audio_queue_push(&q,source,1024)==1024);}
        assert(marble_audio_queue_push(&q,source,1024)==0 && q.dropped==1024);
        for(unsigned block=0;block<16;block++) {
            memset(out,0xa5,sizeof(out));assert(marble_audio_queue_pop(&q,out,256)==256);
            for(unsigned i=0;i<256;i++){assert(out[2*i]==(int16_t)(block*256+i));assert(out[2*i+1]==(int16_t)~(block*256+i));}
            assert(out[512]==(int16_t)0xa5a5 && out[513]==(int16_t)0xa5a5);
        }
        assert(q.accepted==4096 && q.consumed==4096 && q.head==q.tail && !q.failed);
        for(unsigned i=0;i<8192;i++)assert(!q.samples[i]);
        assert(marble_audio_queue_pop(&q,out,256)==0 && q.silence==256);
        for(unsigned i=0;i<512;i++)assert(!out[i]);
    }
    memset(&q,0,sizeof(q));pattern(42);assert(marble_audio_queue_push(&q,source,17)==17);
    assert(marble_audio_queue_pop(&q,out,32)==17 && q.silence==15);
    for(unsigned i=0;i<34;i++)assert(out[i]==source[i]);for(unsigned i=34;i<64;i++)assert(!out[i]);
    uint32_t head=q.head,tail=q.tail;
    assert(!marble_audio_queue_push(&q,source,1025) && !marble_audio_queue_push(&q,NULL,1));
    assert(!marble_audio_queue_pop(&q,out,257) && !marble_audio_queue_pop(&q,NULL,1));
    assert(q.head==head && q.tail==tail);
    q.head=q.tail+4097;assert(!marble_audio_queue_push(&q,source,1) && q.failed);
    assert(!marble_audio_queue_pop(&q,out,1));
    puts("PASS locked RAM sink queue: exact stereo ordering, full/drop accounting, partial underflow zero fill, uint32 wrap, cleared consumed slots, invalid bounds/occupancy refusal; no DSP");
}
