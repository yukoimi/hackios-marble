/* Exact observed single GOP pipe profile. Unknown/stale extra planes refuse. */
#ifndef MARBLE_DISPLAY_DMA_H
#define MARBLE_DISPLAY_DMA_H
static int marble_display_payload_disjoint(const uint32_t *words,uint64_t gop,uint64_t bytes)
{
    if(!words || gop!=0xb8000000 || bytes!=10368000 || words[0] || words[1])return 0;
    static const uint32_t pipe0[]={0x09600438,0,0x10e0,0,0xb8000000,0,0,0};
    for(unsigned i=0;i<8;i++)if(words[2+i]!=pipe0[i])return 0;
    for(unsigned i=10;i<66;i++)if(words[i])return 0;
    return 1;
}
#endif
