#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define XNU_MARBLE_TDM_PCM_GRAPH_ONLY 1
#define XNU_MARBLE_AUDIO_MFC 1
#include "../../xnuloader/marble_tdm_run.h"
#include "../../xnuloader/marble_pcm_data.h"
int main(void)
{
    uint8_t graph[608],data[68];
    assert(MARBLE_TDM_RUN_OPEN_BYTES==608 && MARBLE_PCM_DATA_BYTES==1024);
    assert(MARBLE_PCM_DATA_FRAMES==256 && MARBLE_PCM_DATA_RATE==48000 && MARBLE_PCM_DATA_BITS==16);
    assert(marble_tdm_run_command(graph,608,0)==608);
    assert(marble_gpr_u32(graph+16)==MARBLE_TDM_RUN_TOKEN);
    for(unsigned i=0;i<2;i++) {
        assert(marble_pcm_data_command_index(data,68,0x80001234,i)==68);
        assert(marble_gpr_u32(data+16)==MARBLE_PCM_DATA_TOKEN+i && marble_gpr_u32(data+36)==1024);
    }
    assert(!marble_pcm_data_command_index(data,68,0x80001234,2));
    puts("PASS explicit MFC graph selection and two 1024-byte48kHz/S16/stereo WREP tokens; no hardware operations");
}
