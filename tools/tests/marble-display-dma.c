#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../../xnuloader/marble_display_dma.h"
int main(void)
{
    uint32_t words[66]={0};words[2]=0x09600438;words[4]=0x10e0;words[6]=0xb8000000;
    assert(marble_display_payload_disjoint(words,0xb8000000,10368000));
    assert(!marble_display_payload_disjoint(words,0x9fd00000,10368000));
    assert(!marble_display_payload_disjoint(words,0xb8000000,10368004));
    /* Every nonzero extra status/plane and every changed active field refuses,
     * including Demura enable, a source in the DSP region, or an extra rect. */
    for(unsigned i=0;i<66;i++) {
        uint32_t saved=words[i];words[i]^=4;
        assert(!marble_display_payload_disjoint(words,0xb8000000,10368000));words[i]=saved;
    }
    words[6]=0xa1400000;assert(!marble_display_payload_disjoint(words,0xb8000000,10368000));
    puts("marble-display-dma: exact GOP profile, extra planes and overlap refusal PASS");return 0;
}
