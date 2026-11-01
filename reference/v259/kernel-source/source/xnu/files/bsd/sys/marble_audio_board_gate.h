/* Optional sideband inside the existing 16K owner allocation. The v1
 * 320-byte mailbox and its 2/4/6/8 sequence are deliberately unchanged.
 * Producers own disjoint cache lines; adapters publish odd/data/even with
 * cache maintenance and double-read snapshots. This is trusted endpoint
 * evidence, not independent DSP observation or proof of audible playback. */
#ifndef MARBLE_AUDIO_BOARD_GATE_H
#define MARBLE_AUDIO_BOARD_GATE_H
#include "marble_audio_owner.h"
#include "marble_audio_stream.h"
#define MABG_OFFSET 512u
#define MABG_MAGIC 0x3154474554554d41ULL
#define MABG_DRAINED 3u
enum { MABG_BOTH_POWERED_DOWN=1,MABG_BOTH_RESET=2,MABG_BUS_RESTORED=4 };
typedef struct {
    uint64_t magic,epoch;
    uint32_t version,bytes,periods,muted;
    uint8_t reserved[32];
} MarbleAudioBoardGateInfo;
typedef struct {
    uint64_t epoch;
    uint32_t sequence,flags,submitted,completed;
    uint8_t reserved[40];
} MarbleAudioBoardGateQuiet;
typedef struct {
    MarbleAudioBoardGateInfo info;
    MarbleAudioProof drained;
    MarbleAudioBoardGateQuiet quiet;
} MarbleAudioBoardGate;
_Static_assert(sizeof(MarbleAudioBoardGate)==256 &&
    offsetof(MarbleAudioBoardGate,drained)==64 && offsetof(MarbleAudioBoardGate,quiet)==192,
    "board gate producer cache lines");
static int marble_audio_board_gate_info_valid(const MarbleAudioBoardGateInfo *p,uint64_t epoch)
{
    return p && epoch && p->magic==MABG_MAGIC && p->epoch==epoch && p->version==1 &&
        p->bytes==256 && p->periods==8 && p->muted==1 && maowner_zero(p->reserved,32);
}
static int marble_audio_board_gate_drained_valid(const MarbleAudioProof *p,const MarbleAudioProof *configured,uint64_t epoch)
{
    if(!p || !configured || p->phase!=MABG_DRAINED || p->submitted!=8 || p->completed!=8 ||
       !marble_audio_proof_valid(configured,epoch,MAPROOF_CONFIGURE) ||
       p->handle!=configured->handle || p->start_cookie!=configured->start_cookie)return 0;
    for(unsigned i=0;i<28;i++)if(p->map[i]!=configured->map[i])return 0;
    for(unsigned i=0;i<32;i++)if(p->start[i]!=configured->start[i])return 0;
    MarbleAudioProof check=*p;check.phase=MAPROOF_CONFIGURE;check.submitted=1;check.completed=0;
    return marble_audio_proof_valid(&check,epoch,MAPROOF_CONFIGURE);
}
static int marble_audio_board_gate_export(const MarbleAudioStream *s,const MarbleAudioProof *configured,MarbleAudioProof *p)
{
    if(!s || !p || !s->live || s->failed || s->pending || s->phase!=MAS_STOP ||
       s->limit!=8 || s->submitted!=8 || s->completed!=8 || !s->tx || !s->reply ||
       !s->start_retired || !s->handle || s->handle!=s->proof.handle)return 0;
    MarbleAudioProof proof=s->proof;proof.phase=MABG_DRAINED;proof.submitted=8;proof.completed=8;
    if(!marble_audio_board_gate_drained_valid(&proof,configured,proof.epoch))return 0;
    *p=proof;return 1;
}
static int marble_audio_board_gate_quiet_valid(const MarbleAudioBoardGateQuiet *p,uint64_t epoch)
{
    return p && epoch && p->epoch==epoch && p->sequence==2 && p->flags==7 &&
        p->submitted==8 && p->completed==8 && maowner_zero(p->reserved,40);
}
#endif
