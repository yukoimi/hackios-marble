/* RAM-only adapter for the existing 48kHz/S16/stereo audio sink.
 * Caller holds the audio lock for push/pop/stats. No DSP/MMIO/resampling.
 * Index distance is unsigned to preserve wrap; invalid distance fails closed.
 */
#ifndef MARBLE_AUDIO_QUEUE_H
#define MARBLE_AUDIO_QUEUE_H
#include <stdint.h>
#define MAQ_CAPACITY 4096u
#define MAQ_MAX_WRITE 1024u
#define MAQ_MAX_POP 256u
#define MAQ_MAGIC 0x5141554du
typedef struct {
    uint32_t head,tail,failed;
    uint64_t accepted,dropped,consumed,silence;
    int16_t samples[MAQ_CAPACITY*2];
} MarbleAudioQueue;
typedef struct {
    uint32_t magic,version,requested,actual,queued,failed,capacity,reserved;
    uint64_t accepted,dropped,consumed,silence;
    int16_t samples[MAQ_MAX_POP*2];
} MarbleAudioQueueTransfer;
/* _IOWR('M', 1, MarbleAudioQueueTransfer); keep this fixed ABI shared with
 * the static integration program without importing user/kernel SDK headers. */
#define MAQ_IOCTL (0xc0000000u | ((uint32_t)sizeof(MarbleAudioQueueTransfer)<<16) | (0x4du<<8) | 1u)
_Static_assert(sizeof(MarbleAudioQueueTransfer)==1088,"fixed audio queue ABI");
static int marble_audio_queue_valid(MarbleAudioQueue *q)
{
    if(!q || q->failed)return 0;
    if((uint32_t)(q->head-q->tail)>MAQ_CAPACITY) {q->failed=1;return 0;}
    return 1;
}
static uint32_t marble_audio_queue_push(MarbleAudioQueue *q,const int16_t *p,uint32_t count)
{
    if(!p || !count || count>MAQ_MAX_WRITE || !marble_audio_queue_valid(q))return 0;
    uint32_t room=MAQ_CAPACITY-(uint32_t)(q->head-q->tail),n=count<room?count:room;
    for(uint32_t i=0;i<n;i++) {
        uint32_t at=(q->head+i)&(MAQ_CAPACITY-1);
        q->samples[2*at]=p[2*i];q->samples[2*at+1]=p[2*i+1];
    }
    q->head+=n;q->accepted+=n;q->dropped+=count-n;return n;
}
static uint32_t marble_audio_queue_pop(MarbleAudioQueue *q,int16_t *p,uint32_t count)
{
    if(!p || count>MAQ_MAX_POP || !marble_audio_queue_valid(q))return 0;
    uint32_t queued=q->head-q->tail,n=count<queued?count:queued;
    for(uint32_t i=0;i<n;i++) {
        uint32_t at=(q->tail+i)&(MAQ_CAPACITY-1);
        p[2*i]=q->samples[2*at];p[2*i+1]=q->samples[2*at+1];
        q->samples[2*at]=q->samples[2*at+1]=0;
    }
    for(uint32_t i=n;i<count;i++)p[2*i]=p[2*i+1]=0;
    q->tail+=n;q->consumed+=n;q->silence+=count-n;return n;
}
#endif
