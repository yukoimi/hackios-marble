/* Explicit finite live-producer experiment. Read-only status ioctl: no
 * firmware controls, no pointers, no DMA contents and no write commands. */
#ifndef MARBLE_AUDIO_REFILL_H
#define MARBLE_AUDIO_REFILL_H
#include <stdint.h>
#define MAREFILL_MAGIC 0x3146524du
#define MAREFILL_FRAMES 1792u
enum { MAREFILL_PREBUFFER=1,MAREFILL_RUNNING,MAREFILL_WAITING,MAREFILL_RETIRED,MAREFILL_FAILED };
typedef struct {
    uint32_t magic,version,bytes,state;
    uint64_t epoch;
    uint32_t feed,published,submitted,completed,queued,pending,consumed,silence,reserved[2];
} MarbleAudioRefillStatus;
_Static_assert(sizeof(MarbleAudioRefillStatus)==64,"fixed read-only refill ABI");
#define MAREFILL_IOCTL (0xc0000000u | (64u<<16) | (0x4du<<8) | 2u)
static int marble_audio_refill_request_valid(const MarbleAudioRefillStatus *p)
{
    if(!p || p->magic!=MAREFILL_MAGIC || p->version!=1 || p->bytes!=64)return 0;
    const uint8_t *b=(const uint8_t *)p;for(unsigned i=12;i<64;i++)if(b[i])return 0;
    return 1;
}
#endif
