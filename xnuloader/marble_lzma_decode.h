/* Bounded LZMA-Alone adapter for the unmodified public-domain SDK19 decoder
 * vendored from EDK2 edk2-stable202408. One synchronous caller, no allocator
 * protocol, no storage, no executable-memory jump. Output is verified by
 * the existing SHA256/PE/LoadImage chain before any child can execute. */
#ifndef MARBLE_LZMA_DECODE_H
#define MARBLE_LZMA_DECODE_H
#pragma push_macro("EFIAPI")
#undef EFIAPI
#include "vendor/lzma/LzmaDec.h"
#pragma pop_macro("EFIAPI")
static uint64_t marble_lzma_probabilities[4096];
static unsigned marble_lzma_allocated;
static void *marble_lzma_allocate(ISzAllocPtr allocator,size_t bytes)
{
    (void)allocator;
    if(marble_lzma_allocated || !bytes || bytes>sizeof(marble_lzma_probabilities))return 0;
    marble_lzma_allocated=1;return marble_lzma_probabilities;
}
static void marble_lzma_free(ISzAllocPtr allocator,void *address)
{(void)allocator;if(address==marble_lzma_probabilities)marble_lzma_allocated=0;}
static int marble_lzma_decode(const uint8_t *in,size_t bytes,uint8_t *out,size_t capacity)
{
    if(!in || !out || bytes<18 || bytes>1048576 || capacity<512 || capacity>1048576 || marble_lzma_allocated)return 0;
    /* Exactly the packer's lc3/lp0/pb2 and 1MiB dictionary. No unbounded
     * attacker-selected probability allocation or output size. */
    if(in[0]!=0x5d || in[1] || in[2] || in[3]!=0x10 || in[4])return 0;
    uint64_t declared=0;for(unsigned i=0;i<8;i++)declared|=(uint64_t)in[5+i]<<(8*i);
    if(declared!=UINT64_MAX && declared!=capacity)return 0;
    SizeT source=bytes-13,destination=capacity;ELzmaStatus status=LZMA_STATUS_NOT_SPECIFIED;
    ISzAlloc allocator={marble_lzma_allocate,marble_lzma_free};
    SRes result=LzmaDecode(out,&destination,in+13,&source,in,5,LZMA_FINISH_END,&status,&allocator);
    return result==SZ_OK && !marble_lzma_allocated && destination==capacity && source==bytes-13 && status==LZMA_STATUS_FINISHED_WITH_MARK;
}
#endif
