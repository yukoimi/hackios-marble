#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_audio_completion.h"
int main(void)
{
    MarbleAudioCompletion good={.graph_next=5,.graph_sent=31,.graph_tx=31,.graph_reply=31,
        .pcm_sent=1,.pcm_tx=1,.pcm_reply=1,.pcm_kind=1,.pcm_zero=1,.pcm_faults_ok=1,
        .map_finished=1,.map_sent=3,.map_tx=3,.map_reply=3,.pins_restored=1};
    assert(marble_audio_complete(&good));assert(!marble_audio_complete(NULL));
    /* Every completion/ownership/error field is necessary; no malformed or
     * unretired DMA, pending graph, map failure or live pins may pass. */
    for(unsigned offset=0;offset<sizeof(good);offset+=sizeof(unsigned))for(unsigned bit=0;bit<32;bit++) {
        MarbleAudioCompletion bad=good;unsigned word;
        memcpy(&word,(char *)&bad+offset,sizeof(word));word^=1u<<bit;
        memcpy((char *)&bad+offset,&word,sizeof(word));assert(!marble_audio_complete(&bad));
    }
    puts("PASS early STOP gate rejects every missing retirement, malformed PCM, DMA fault, map error, partial assembly, retained route/pin and dirty amplifier");
}
