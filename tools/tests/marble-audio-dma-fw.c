#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_audio_dma.h"
static unsigned reads,fail_at,change,real_counts;static void print(const char *fmt,...){(void)fmt;}
static int read32(unsigned offset,uint32_t *out)
{
    reads++;if(reads==fail_at)return 0;*out=0;
    if(offset==0x20)*out=real_counts?0x4c017e71:0x48000078;else if(offset==0x24)*out=real_counts?0x6000004b:78;
    else if(offset==0x800+4*3)*out=0x80001801;
    else if(offset==0xc00+4*3)*out=1u<<16;
    if(change && reads>244 && offset==0x800)*out=1;return 1;
}
#define MARBLE_AUDIO_DMA_READ32 read32
#include "../../xnuloader/marble_audio_dma_fw.h"
int main(void)
{
    assert(marble_audio_dma_inventory() && reads==488 && marble_audio_dma_done);
    reads=0;change=1;assert(!marble_audio_dma_inventory() && reads==488);
    reads=0;change=0;fail_at=2;assert(!marble_audio_dma_inventory() && reads==2);
    reads=0;fail_at=0;real_counts=1;assert(marble_audio_dma_inventory() && reads==460);
    puts("PASS actual passive DMA inventory: 488 reads, stable snapshot required, read failure stops; no write interface exists");
}
