#ifndef MARBLE_AUDIO_RUNTIME_BOUND_H
#define MARBLE_AUDIO_RUNTIME_BOUND_H
#include <stdint.h>
/* A round number/typed UNMAP is not a wall-clock bound. Evaluate only after
 * the final thermal read, including time spent in preempting firmware. */
static int marble_audio_runtime_bound(uint64_t elapsed_us,unsigned typed_complete,unsigned thermal_ok)
{return typed_complete==1 && thermal_ok==1 && elapsed_us>=300000 && elapsed_us<600000;}
#endif
