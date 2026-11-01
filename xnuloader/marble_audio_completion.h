/* Pure completion gate: early STOP may omit idle dwell only after all DMA
 * and graph messages have retired and physical ownership is restored. */
#ifndef MARBLE_AUDIO_COMPLETION_H
#define MARBLE_AUDIO_COMPLETION_H
#include <stdint.h>
typedef struct {
    unsigned graph_next,graph_sent,graph_tx,graph_reply,graph_bad;
    unsigned pcm_sent,pcm_tx,pcm_reply,pcm_bad,pcm_fragment,pcm_kind,pcm_error,pcm_metadata_error,pcm_zero,pcm_faults_ok;
    unsigned map_finished,map_sent,map_tx,map_reply,map_bad,map_error0,map_error1,map_fragment;
    unsigned lease_active,route_active,pins_active,pins_restored,amp_dirty;
} MarbleAudioCompletion;
static int marble_audio_complete(const MarbleAudioCompletion *s)
{
    return s && s->graph_next==5 && s->graph_sent==31 && s->graph_tx==31 && s->graph_reply==31 && !s->graph_bad &&
        s->pcm_sent==1 && s->pcm_tx==1 && s->pcm_reply==1 && !s->pcm_bad && !s->pcm_fragment && s->pcm_kind==1 &&
        !s->pcm_error && !s->pcm_metadata_error && s->pcm_zero==1 && s->pcm_faults_ok==1 &&
        s->map_finished==1 && s->map_sent==3 && s->map_tx==3 && s->map_reply==3 && !s->map_bad &&
        !s->map_error0 && !s->map_error1 && !s->map_fragment && !s->lease_active && !s->route_active &&
        !s->pins_active && s->pins_restored==1 && !s->amp_dirty;
}
#endif
