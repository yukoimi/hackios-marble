/* Separate v2 finite streaming window. Legacy owner/gate validators remain
 * strictly v1/two/eight periods. Normalized copies below reuse their wire,
 * epoch, address, flags and reserved-byte checks; actual messages keep 24.
 * This is a muted 128ms test window, not a long-running playback provider. */
#ifndef MARBLE_AUDIO_STREAM_WINDOW_H
#define MARBLE_AUDIO_STREAM_WINDOW_H
#include "marble_audio_board_gate.h"
#define MAWINDOW_PERIODS 24u
#define MAWINDOW_INPUT_FRAMES 5888u
static int mawindow_info_valid(const MarbleAudioOwnerInfo *p)
{
    if(!p || p->version!=2 || p->periods!=24)return 0;
    MarbleAudioOwnerInfo c=*p;c.version=1;c.periods=8;
    return marble_audio_owner_info_valid_periods(&c,8);
}
static int mawindow_grant_valid(const MarbleAudioOwnerGrant *p,uint64_t epoch)
{
    if(!p || p->submitted>24 || p->completed>p->submitted)return 0;
    MarbleAudioOwnerGrant c=*p;
    if(p->state==MAOWNER_PAS_OFF || p->state==MAOWNER_RELEASED) {
        if(p->submitted!=24 || p->completed!=24)return 0;
        c.submitted=c.completed=8;
    } else {
        if(p->state!=MAOWNER_FAILED && (p->submitted || p->completed))return 0;
        c.submitted=c.completed=0;
    }
    return marble_audio_owner_grant_valid_periods(&c,epoch,8);
}
static int mawindow_request_valid(const MarbleAudioOwnerRequest *p,const MarbleAudioProof *proof,uint64_t epoch)
{
    if(!p || !proof)return 0;
    MarbleAudioProof c=*proof;
    if(p->command==MAOWNER_RETIRE) {
        if(proof->submitted!=24 || proof->completed!=24 || !marble_audio_proof_valid(proof,epoch,MAPROOF_RETIRED))return 0;
        c.submitted=c.completed=8;
    }
    return marble_audio_owner_request_valid_periods(p,&c,epoch,8);
}
static int mawindow_board_info_valid(const MarbleAudioBoardGateInfo *p,uint64_t epoch)
{
    if(!p || p->version!=2 || p->periods!=24)return 0;
    MarbleAudioBoardGateInfo c=*p;c.version=1;c.periods=8;
    return marble_audio_board_gate_info_valid(&c,epoch);
}
static int mawindow_drained_valid(const MarbleAudioProof *p,const MarbleAudioProof *configured,uint64_t epoch)
{
    if(!p || p->submitted!=24 || p->completed!=24)return 0;
    MarbleAudioProof c=*p;c.submitted=c.completed=8;
    return marble_audio_board_gate_drained_valid(&c,configured,epoch);
}
static int mawindow_drained_export(const MarbleAudioStream *s,const MarbleAudioProof *configured,MarbleAudioProof *out)
{
    if(!s || !out || s->limit!=24 || s->submitted!=24 || s->completed!=24)return 0;
    MarbleAudioStream c=*s;c.limit=c.submitted=c.completed=8;
    MarbleAudioProof proof;
    if(!marble_audio_board_gate_export(&c,configured,&proof))return 0;
    proof.submitted=proof.completed=24;
    if(!mawindow_drained_valid(&proof,configured,proof.epoch))return 0;
    *out=proof;return 1;
}
static int mawindow_quiet_valid(const MarbleAudioBoardGateQuiet *p,uint64_t epoch)
{
    if(!p || p->submitted!=24 || p->completed!=24)return 0;
    MarbleAudioBoardGateQuiet c=*p;c.submitted=c.completed=8;
    return marble_audio_board_gate_quiet_valid(&c,epoch);
}
#endif
