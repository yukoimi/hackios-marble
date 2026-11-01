#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../../xnuloader/marble_audio_context.h"
static unsigned reads,active,changed,fail;
static void print(const char *f,...){(void)f;}
static int read32(unsigned o,uint32_t *v)
{
    reads++;if(reads==fail)return 0;*v=0;
    assert(o==0x28 || o==0x1014 || o==0x1814 || (o>=0x85000 && o<=0x857f4));
    if(o==0x85000 && active)*v=1;
    if(changed && reads==16)*v=2;
    return 1;
}
#define MARBLE_AUDIO_CONTEXT_READ32 read32
#include "../../xnuloader/marble_audio_context_fw.h"
int main(void)
{
    MarbleAudioDmaSnapshot s={.id0=0x4c017e71,.id1=0x6000004b};
    for(unsigned i=0;i<5;i++){s.smr[i]=0x80000000|i;s.s2cr[i]=i;}
    assert(marble_audio_context_candidate(&s)==5);
    assert(marble_audio_context_inventory(&s) && reads==30);
    reads=0;active=1;assert(!marble_audio_context_inventory(&s));
    reads=0;active=0;changed=1;assert(!marble_audio_context_inventory(&s));
    reads=0;changed=0;fail=3;assert(!marble_audio_context_inventory(&s) && reads==3);
    s.smr[8]=0x80001801;assert(marble_audio_context_candidate(&s)==0xffffffffu);
    s.smr[8]=0;s.gfsr=1;assert(marble_audio_context_candidate(&s)==0xffffffffu);
    s.gfsr=0;s.id1=0x5000004b;assert(marble_audio_context_candidate(&s)==0xffffffffu);
    puts("PASS candidate bounds, route/SID conflict, active, unstable and failed context reads");
}
