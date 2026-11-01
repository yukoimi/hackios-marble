/* Integer 1 kHz triangle, 48 kHz S16 stereo, opposite channel phase.
 * Shared by the static device writer and the offline original-guard oracle. */
#ifndef MARBLE_AUDIO_SIGNAL_H
#define MARBLE_AUDIO_SIGNAL_H
#include <stdint.h>
static int16_t marble_audio_signal_sample(unsigned frame,unsigned channel)
{
    int p=(int)(frame%48),v=(p<=12?p:p<=36?24-p:p-48)*42;
    return (int16_t)(channel?-v:v);
}
#endif
