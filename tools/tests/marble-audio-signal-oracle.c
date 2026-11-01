#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/redwood_audio.h>
#include "../marble-audio-signal.h"
#include "../../xnu/files/bsd/sys/marble_audio_pcm_probe.h"
#include "../../xnu/files/bsd/sys/marble_audio_queue.h"
static MarbleAudioQueue queue;
#ifdef MARBLE_SIGNAL_STREAM24
static int16_t guarded[5888*2];
static void stream24(void)
{
    int16_t samples[2048];
    for(unsigned first=0;first<5888;) {
        unsigned count=first==0?512:first==512?1024:256;
        for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++)samples[2*i+c]=marble_audio_signal_sample(first+i,c);
        redwood_guard_process(samples,count);
        for(unsigned i=0;i<count*2;i++)guarded[first*2+i]=marble_audio_pcm_probe_sample(samples[i]);
        first+=count;
    }
    assert(marble_audio_queue_push(&queue,guarded,512)==512);unsigned next=512;
    for(unsigned block=0;block<24;block++) {
        if(block>=3 && next<5888) {
            unsigned n=block==3?1024:256;
            assert(marble_audio_queue_push(&queue,guarded+next*2,n)==n);next+=n;
        }
        uint8_t page[4096]={0};MarbleAudioPcmCheck r;
        assert(marble_audio_queue_pop(&queue,samples,256)==(block==2?0u:256u));
        for(unsigned i=0;i<512;i++){page[2*i]=(uint8_t)samples[i];page[2*i+1]=(uint8_t)((uint16_t)samples[i]>>8);}
        assert(marble_audio_pcm_probe_page(page,4096,1,&r));
        assert(block==2?(!r.peak && !r.nonzero):(r.peak>0 && r.peak<=64 && r.nonzero>400));
        printf("period=%u peak=%u nonzero=%u hash=%x\n",block,r.peak,r.nonzero,r.hash);
    }
    assert(next==5888 && queue.head==5888 && queue.tail==5888 && queue.consumed==5888 && queue.silence==256 && !queue.dropped);
}
#endif
int main(void)
{
    int16_t samples[2048];redwood_guard_configure(true,0);redwood_guard_attenuation(0);redwood_guard_monitor(0);
#ifdef MARBLE_SIGNAL_STREAM24
    stream24();return 0;
#endif
    for(unsigned block=0;block<2;block++) {
#ifdef MARBLE_SIGNAL_REFILL
        if(block)break;
        unsigned first=0,count=512;
#else
        unsigned first=block*1024,count=1024;
#endif
        for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++)samples[2*i+c]=marble_audio_signal_sample(first+i,c);
        redwood_guard_process(samples,count);
        for(unsigned i=0;i<count*2;i++)samples[i]=marble_audio_pcm_probe_sample(samples[i]);
        assert(marble_audio_queue_push(&queue,samples,count)==count);
    }
    for(unsigned block=0;block<8;block++) {
        uint8_t page[4096]={0};MarbleAudioPcmCheck r;
#ifdef MARBLE_SIGNAL_REFILL
        if(block==3)for(unsigned part=0;part<2;part++) {
            unsigned first=part?1536:512,count=part?256:1024;
            for(unsigned i=0;i<count;i++)for(unsigned c=0;c<2;c++)samples[2*i+c]=marble_audio_signal_sample(first+i,c);
            redwood_guard_process(samples,count);
            for(unsigned i=0;i<count*2;i++)samples[i]=marble_audio_pcm_probe_sample(samples[i]);
            assert(marble_audio_queue_push(&queue,samples,count)==count);
        }
        assert(marble_audio_queue_pop(&queue,samples,256)==(block==2?0u:256u));
#else
        assert(marble_audio_queue_pop(&queue,samples,256)==256);
#endif
        for(unsigned i=0;i<512;i++){page[2*i]=(uint8_t)samples[i];page[2*i+1]=(uint8_t)((uint16_t)samples[i]>>8);}
        assert(marble_audio_pcm_probe_page(page,4096,1,&r));
#ifdef MARBLE_SIGNAL_REFILL
        assert(block==2?(!r.nonzero && !r.peak):(r.nonzero>400 && r.peak<=64));
#else
        assert(r.nonzero>400 && r.peak<=64);
#endif
        printf("period=%u peak=%u nonzero=%u hash=%x\n",block,r.peak,r.nonzero,r.hash);
    }
}
