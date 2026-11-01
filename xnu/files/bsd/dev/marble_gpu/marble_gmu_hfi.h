/* Gen7 HFI transport activation and two table ACKs, before CORE_FW_START.
 * Caller holds the FW_INIT/TCM/clock/CX/SMMU leases throughout and must reset
 * and drain the real master on every outcome. No GX/DCVS/start request here.
 * Shared queues must use a CPU uncached view, never a racing cache alias. */
#ifndef MARBLE_GMU_HFI_H
#define MARBLE_GMU_HFI_H
#include "upstream/AdrenoHfi.h"
#define MARBLE_GMU_HFI_ABI "MGPU-MARBLE-GMU-HFI-v1"
typedef struct {
    void *cookie;
    int (*read)(void *,unsigned,uint32_t *);
    int (*write)(void *,unsigned,uint32_t);
    void (*delay)(void *,unsigned);
    int (*health)(void *);
} MarbleHfiIo;
typedef struct {
    unsigned ready,commands,acks,failed_stage,polls;
    uint32_t ctrl,fw_result,irq,header,error,response[16];
    size_t response_words;
    AdrenoHfiStatus transport;
    uint32_t ack_header[2],ack_return[2];
} MarbleHfiResult;
static int marble_hfi_access(unsigned reg,unsigned write)
{
    if(write)return reg==0x16618 || reg==0x16650 || reg==0x16644;
    return reg==0x16600 || reg==0x16070 || reg==0x16648 || reg==0x16658;
}
static int marble_hfi_health(const MarbleHfiIo *io,MarbleHfiResult *r)
{
    return io->health(io->cookie) || io->read(io->cookie,0x16070,&r->fw_result) ||
        (r->fw_result&0xe00) || io->read(io->cookie,0x16648,&r->irq) || (r->irq&0x8000);
}
static int marble_hfi_tables(const MarbleHfiIo *io,AdrenoHfi *h,
    const uint32_t *performance,const uint32_t *bandwidth,MarbleHfiResult *r)
{
    if(!r)return 0;*r=(MarbleHfiResult){0};
    if(!io || !io->read || !io->write || !io->delay || !io->health ||
        !h || h->profile!=AdrenoHfiProfileMarbleGen7 || !performance || !bandwidth ||
        h->pending_header || h->poisoned || h->sequence)return 0;
    r->failed_stage=1;
    if(marble_hfi_health(io,r) || (r->fw_result&0x1ff)!=0x100 ||
        io->read(io->cookie,0x16600,&r->ctrl) || (r->ctrl&~1u) || io->write(io->cookie,0x16618,1))return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;
        if(marble_hfi_health(io,r) || io->read(io->cookie,0x16600,&r->ctrl))return 0;
        if(r->ctrl&1){r->ready=1;break;}
        io->delay(io->cookie,10);
    }
    if(!r->ready)return 0;
    for(unsigned cmd=0;cmd<2;cmd++) {
        r->failed_stage=cmd+2;
        if(marble_hfi_health(io,r))return 0;
        r->header=0;
        r->transport=AdrenoHfiSend(h,cmd?3:4,cmd?bandwidth:performance,cmd?159:58,&r->header);
        if(r->header)++r->commands;
        if(r->transport!=AdrenoHfiSuccess)return 0;
        unsigned ack=0;
        for(unsigned n=0;n<=10000;n++) {
            ++r->polls;
            if(marble_hfi_health(io,r))return 0;
            r->transport=AdrenoHfiPoll(h,r->response,16,&r->response_words,&r->error);
            if(r->transport==AdrenoHfiSuccess){r->ack_header[cmd]=h->last_ack_header;r->ack_return[cmd]=h->last_ack_return;++r->acks;ack=1;break;}
            if(r->transport!=AdrenoHfiEmpty)return 0;
            io->delay(io->cookie,10);
        }
        if(!ack || marble_hfi_health(io,r))return 0;
        if(io->write(io->cookie,0x16644,1))return 0;
    }
    r->failed_stage=0;return 1;
}
/* Protocol command from gen7_hfi_send_core_fw_start: handle=0. Does not
 * request a default GPU/BW vote, START, OOB, IFPC or RSCC sleep/wake. The
 * caller's real platform health guard still requires GX collapsed. */
static int marble_hfi_core_start(const MarbleHfiIo *io,AdrenoHfi *h,MarbleHfiResult *r)
{
    if(!r)return 0;*r=(MarbleHfiResult){0};r->failed_stage=1;
    if(!io || !io->read || !io->write || !io->delay || !io->health || !h || h->profile!=AdrenoHfiProfileMarbleGen7 || h->sequence!=2 ||
        h->pending_header || h->poisoned || marble_hfi_health(io,r))return 0;
    uint32_t handle=0;
    r->transport=AdrenoHfiSend(h,14,&handle,1,&r->header);
    r->commands=!!r->header;
    if(r->transport!=AdrenoHfiSuccess)return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;
        if(marble_hfi_health(io,r))return 0;
        r->transport=AdrenoHfiPoll(h,r->response,16,&r->response_words,&r->error);
        if(r->transport==AdrenoHfiSuccess) {
            r->acks=1;r->ack_header[0]=h->last_ack_header;r->ack_return[0]=h->last_ack_return;
            if(marble_hfi_health(io,r) || io->write(io->cookie,0x16644,1))return 0;
            r->failed_stage=0;return 1;
        }
        if(r->transport!=AdrenoHfiEmpty)return 0;
        io->delay(io->cookie,10);
    }
    return 0;
}
/* Bounded modern request after CORE_FW_START. The board restricts this to
 * its verified lowest table vote and the matching prepare-slumber command.
 * This is NOT the GPU OOB power-on request. Stop/drain remains external. */
static int marble_hfi_low_vote(const MarbleHfiIo *io,AdrenoHfi *h,unsigned slumber,MarbleHfiResult *r)
{
    if(!r)return 0;*r=(MarbleHfiResult){0};r->failed_stage=1;
    if(!io || !io->read || !io->write || !io->delay || !io->health || !h || slumber>1 ||
        h->profile!=AdrenoHfiProfileMarbleGen7 ||
        (slumber?!(h->sequence==4 || (h->sequence==5 && h->last_ack_return==0x50010a)):h->sequence!=3) ||
        h->pending_header || h->poisoned || marble_hfi_health(io,r))return 0;
    uint32_t data[3]={1,1,1};
    r->transport=AdrenoHfiSend(h,slumber?33:30,data,slumber?2:3,&r->header);
    r->commands=!!r->header;
    if(r->transport!=AdrenoHfiSuccess)return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;
        if(marble_hfi_health(io,r))return 0;
        r->transport=AdrenoHfiPoll(h,r->response,16,&r->response_words,&r->error);
        if(r->transport==AdrenoHfiSuccess) {
            r->acks=1;r->ack_header[0]=h->last_ack_header;r->ack_return[0]=h->last_ack_return;
            if(marble_hfi_health(io,r) || io->write(io->cookie,0x16644,1))return 0;
            r->failed_stage=0;return 1;
        }
        if(r->transport!=AdrenoHfiEmpty)return 0;
        io->delay(io->cookie,10);
    }
    return 0;
}
/* gen7_gmu_hfi_start_msg: header only. Caller MUST hold acknowledged GPU OOB
 * force until START and subsequent GX accesses finish. No CP kick here. */
static int marble_hfi_gpu_start(const MarbleHfiIo *io,AdrenoHfi *h,unsigned oob_held,MarbleHfiResult *r)
{
    if(!r)return 0;*r=(MarbleHfiResult){0};r->failed_stage=1;
    if(!io || !io->read || !io->write || !io->delay || !io->health || !h || !oob_held ||
        h->profile!=AdrenoHfiProfileMarbleGen7 || h->sequence!=4 ||
        h->last_ack_return!=0x40041e || h->pending_header || h->poisoned || marble_hfi_health(io,r))return 0;
    r->transport=AdrenoHfiSend(h,10,0,0,&r->header);r->commands=!!r->header;
    if(r->transport!=AdrenoHfiSuccess)return 0;
    for(unsigned n=0;n<=10000;n++) {
        ++r->polls;
        if(marble_hfi_health(io,r))return 0;
        r->transport=AdrenoHfiPoll(h,r->response,16,&r->response_words,&r->error);
        if(r->transport==AdrenoHfiSuccess) {
            r->acks=1;r->ack_header[0]=h->last_ack_header;r->ack_return[0]=h->last_ack_return;
            if(h->last_ack_return!=r->header || marble_hfi_health(io,r) || io->write(io->cookie,0x16644,1))return 0;
            r->failed_stage=0;return 1;
        }
        if(r->transport!=AdrenoHfiEmpty)return 0;
        io->delay(io->cookie,10);
    }
    return 0;
}
#endif
