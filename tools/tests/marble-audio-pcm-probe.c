#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/redwood_audio.h>
#include "../../xnu/files/bsd/sys/marble_audio_pcm_probe.h"
#include "../../xnu/files/bsd/sys/marble_audio_queue.h"
static MarbleAudioQueue queue;
int main(void)
{
    for(int i=-32768;i<=32767;i++) {
        int value=marble_audio_pcm_probe_sample((int16_t)i);
        assert(value>=-64 && value<=64);
        assert(!i || !value || ((i<0)==(value<0)));
    }
    int16_t samples[2048];unsigned nonzero=0;
    redwood_guard_configure(true,0);redwood_guard_attenuation(0);redwood_guard_monitor(0);
    for(unsigned block=0;block<2;block++) {
        for(unsigned i=0;i<1024;i++) {samples[2*i]=(int16_t)(block*1024+i);samples[2*i+1]=(int16_t)~(block*1024+i);}
        redwood_guard_process(samples,1024);
        for(unsigned i=0;i<2048;i++)samples[i]=marble_audio_pcm_probe_sample(samples[i]);
        assert(marble_audio_queue_push(&queue,samples,1024)==1024);
    }
    for(unsigned period=0;period<8;period++) {
        uint8_t page[4096]={0};MarbleAudioPcmCheck r;
        assert(marble_audio_queue_pop(&queue,samples,256)==256);
        for(unsigned i=0;i<512;i++){page[2*i]=(uint8_t)samples[i];page[2*i+1]=(uint8_t)((uint16_t)samples[i]>>8);}
        assert(marble_audio_pcm_probe_page(page,4096,1,&r));assert(r.peak<=64);nonzero+=r.nonzero;
        if(r.nonzero)assert(!marble_audio_pcm_probe_page(page,4096,0,&r));
        page[1024]=1;assert(!marble_audio_pcm_probe_page(page,4096,1,&r));page[1024]=0;
        page[0]=65;page[1]=0;assert(!marble_audio_pcm_probe_page(page,4096,1,&r));
        page[0]=191;page[1]=255;assert(!marble_audio_pcm_probe_page(page,4096,1,&r));
    }
    assert(nonzero>0 && queue.consumed==2048 && !queue.failed && queue.head==queue.tail);
    printf("PASS original guard + bounded PCM: all S16 values, stereo queue/device pattern, eight pages, nonzero=%u, <=64 LSB, reject +/-65 and nonzero DMA tail; no sound claim\n",nonzero);
}
