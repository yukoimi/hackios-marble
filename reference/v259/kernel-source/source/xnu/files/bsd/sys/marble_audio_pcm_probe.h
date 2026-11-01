/* Diagnostic PCM copy only: original guard precedes this final <=64 LSB
 * ceiling. No speaker calibration, gain configuration or audible enable. */
#ifndef MARBLE_AUDIO_PCM_PROBE_H
#define MARBLE_AUDIO_PCM_PROBE_H
#include <stdint.h>
typedef struct {uint32_t peak,nonzero,hash;} MarbleAudioPcmCheck;
static int16_t marble_audio_pcm_probe_sample(int16_t sample)
{
    int32_t value=(int32_t)sample/16;
    if(value>64)value=64;if(value< -64)value= -64;
    return (int16_t)value;
}
static int marble_audio_pcm_probe_page(const volatile uint8_t *p,unsigned bytes,unsigned pcm,MarbleAudioPcmCheck *out)
{
    if(!p || !out || bytes!=4096 || pcm>1)return 0;
    MarbleAudioPcmCheck r={.hash=2166136261u};
    for(unsigned i=0;i<4096;i++) {
        uint8_t value=p[i];r.hash=(r.hash^value)*16777619u;
        if((!pcm || i>=1024) && value)return 0;
    }
    if(pcm)for(unsigned i=0;i<512;i++) {
        int32_t sample=(int16_t)((unsigned)p[2*i]|(unsigned)p[2*i+1]<<8);
        unsigned magnitude=sample<0?-sample:sample;
        if(magnitude>64)return 0;
        if(magnitude>r.peak)r.peak=magnitude;r.nonzero+=sample!=0;
    }
    *out=r;return 1;
}
#endif
