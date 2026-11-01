#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_audio_pins.h"
static uint32_t cfg[7],io[7];static unsigned calls,fail,after,writes;
static int read_pin(void *p,unsigned i,uint32_t *c,uint32_t *v)
{(void)p;assert(i<7);if(++calls==fail)return 0;*c=cfg[i];*v=io[i];return 1;}
static int write_pin(void *p,unsigned i,unsigned reg,uint32_t v)
{
    (void)p;assert(i>=3 && i<7 && reg<=1);writes++;unsigned bad=++calls==fail;
    if(bad && !after)return 0;
    if(reg) {assert(!(v&~2u));io[i]=v;}else cfg[i]=v;
    return !bad;
}
static void reset(void){calls=fail=after=writes=0;for(unsigned i=0;i<7;i++){cfg[i]=1;io[i]=0;}}
int main(void)
{
    MarbleAudioPinOps ops={0,read_pin,write_pin};MarbleAudioPins l={0};reset();
    assert(marble_audio_pins_begin(&ops,&l) && l.active && writes==6 && !l.failed);
    for(unsigned i=3;i<7;i++)assert(cfg[i]==marble_audio_pins_cfg[i-3] && io[i]==(i<5?2u:0u));
    assert(!marble_audio_pins_begin(&ops,&l));assert(marble_audio_pins_end(&ops,&l) && !l.active && l.restored && writes==12);
    unsigned count=calls;
    for(unsigned at=1;at<=count;at++)for(unsigned phase=0;phase<2;phase++) {
        reset();l=(MarbleAudioPins){0};fail=at;after=phase;
        int ok=marble_audio_pins_begin(&ops,&l);
        if(l.active){if(ok)marble_audio_pins_end(&ops,&l);fail=0;if(l.active)assert(marble_audio_pins_end(&ops,&l));}
        for(unsigned i=0;i<7;i++)assert(cfg[i]==1 && !(io[i]&2));
    }
    reset();l=(MarbleAudioPins){0};io[0]=2;assert(!marble_audio_pins_begin(&ops,&l) && !writes);
    reset();l=(MarbleAudioPins){0};assert(marble_audio_pins_begin(&ops,&l));cfg[5]=0xdead;unsigned before=writes;assert(!marble_audio_pins_end(&ops,&l) && writes==before && l.active);
    puts("PASS board-specific TDM mux/output/drive/bias and exact restore; amp boundary, every read/write failure before/after, foreign state refusal");
}
