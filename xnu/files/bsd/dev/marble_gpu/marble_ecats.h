/* Marble QSMMUv500 arm TBU transaction. Register protocol from MiCode
 * Xiaomi_Kernel_OpenSource 48952ed36228217531482b39d5bef13e7fd808ec,
 * drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c arm_tbu_impl.
 * Uses UniXNU's I/O interface; no platform mapping, context or stream lease
 * is synthesized here. Caller keeps all masters reset and owns SID5/CB0.
 */
#ifndef MARBLE_ECATS_H
#define MARBLE_ECATS_H
#include "upstream/ArmSmmu4KProgram.h"
#define MARBLE_ECATS_ABI "MGPU-MARBLE-ECATS-v1"
typedef struct {
    unsigned phase,writes,polls,completed,restored,retain;
    uint64_t par,physical;
} MarbleEcatsResult;
enum {MarbleEcatsOk,MarbleEcatsArgument,MarbleEcatsBusy,MarbleEcatsIo,MarbleEcatsTimeout};
enum {MarbleEcatsPrivileged=1,MarbleEcatsWrite=2,MarbleEcatsInstruction=4};
static int marble_tbu_write_allowed(size_t at,unsigned bytes)
{return bytes==8 && (at==0 || at==8 || at==0x18 || at==0x30);}
static int marble_ecats_write(const ArmSmmu4KIo *io,size_t at,uint64_t value,MarbleEcatsResult *r)
{++r->writes;return io->write64(io->cookie,at,value);}
static int marble_ecats_poll(const ArmSmmu4KIo *io,unsigned mask,unsigned expected,
    unsigned budget,MarbleEcatsResult *r)
{
    for(unsigned i=0;i<budget;i++) {
        uint32_t ack;++r->polls;
        if(io->read32(io->cookie,0x20,&ack))return MarbleEcatsIo;
        if((ack&mask)==expected)return io->barrier(io->cookie)?MarbleEcatsIo:MarbleEcatsOk;
    }
    return MarbleEcatsTimeout;
}
/* Positive and negative translations both return Ok when completed. The
 * caller must inspect PAR.F, the *actual* decoded PA and context faults.
 * IO callbacks must provide a bounded delay (board uses 1us/ACK read).
 * Never return reusable resources based on a failed write alone. */
static int marble_ecats_sid_access(const ArmSmmu4KIo *io,unsigned sid,uint64_t iova,unsigned access,
    unsigned budget,MarbleEcatsResult *r)
{
    if(!r)return MarbleEcatsArgument;
    *r=(MarbleEcatsResult){0};
    if(!io || !io->read32 || !io->read64 || !io->write64 || !io->barrier ||
        io->register_bytes<0x38 || !budget || budget>1000 || sid>0x3ff || iova>UINT32_MAX ||
        (access&~7u) || ((access&MarbleEcatsWrite) && (access&MarbleEcatsInstruction)))return MarbleEcatsArgument;
    static const size_t regs[]={0,8,0x18,0x30};
    uint64_t saved[4],v;uint32_t ack;
    r->phase=1;
    for(unsigned i=0;i<4;i++)if(io->read64(io->cookie,regs[i],saved+i))return MarbleEcatsIo;
    if(io->read32(io->cookie,0x20,&ack))return MarbleEcatsIo;
    /* Actual marble idle HALT=0x80182000, ACK=0x100000. Preserve background
     * HALT configuration exactly; only SID and HALT_REQ belong to this lease.
     * ACK is read-only: only its two documented low bits determine idle. */
    if((saved[0]&((UINT64_C(1)<<16)|0x3ff)) || saved[2] || (ack&3) || saved[1]>UINT32_MAX ||
        (saved[3]&~(UINT64_C(255)<<36)))return MarbleEcatsBusy;
    r->retain=1;
    int status=MarbleEcatsIo;
    if(marble_ecats_write(io,0,saved[0]|(1u<<16),r) || io->barrier(io->cookie))goto cleanup;
    status=marble_ecats_poll(io,3,2,budget,r);
    if(status)goto cleanup;
    r->phase=2;status=MarbleEcatsIo;
    if(marble_ecats_write(io,0,saved[0]|(1u<<16)|sid,r) || marble_ecats_write(io,8,iova,r) ||
        marble_ecats_write(io,0x30,UINT64_C(255)<<36,r) ||
        /* Always nonsecure and WB read/write allocate; explicit AXPROT. */
        marble_ecats_write(io,0x18,0xbd | ((access&1)?0x40:0) |
            ((access&2)?2:0) | ((access&4)?0x100:0),r) || io->barrier(io->cookie))goto cleanup;
    r->phase=3;
    status=MarbleEcatsTimeout;
    for(unsigned i=0;i<budget;i++) {
        ++r->polls;
        if(io->read32(io->cookie,0x20,&ack) || io->read64(io->cookie,0x28,&r->par)) {
            status=MarbleEcatsIo;goto cleanup;
        }
        /* Xiaomi also completes on PAR.F: some faulting requests keep the
         * running bit set until trigger is cleared. Cleanup must drain it. */
        if(!(ack&1) || (r->par&1)) {status=MarbleEcatsOk;break;}
    }
    if(status || io->barrier(io->cookie)) {if(!status)status=MarbleEcatsIo;goto cleanup;}
    /* Qualcomm DEBUG_PAR.PA is bits47:12 containing the complete 36-bit
     * physical byte address, unlike architected SMMU PAR page-base format. */
    r->physical=(r->par>>12)&UINT64_C(0xfffffffff);r->completed=1;
cleanup:
    r->phase=4;
    /* Stop trigger, verify ECATS idle, then restore debug data and release
     * halt. On any uncertainty leave retain=1 for external reset. */
    if(marble_ecats_write(io,0x18,0,r) || io->barrier(io->cookie) ||
        marble_ecats_poll(io,1,0,budget,r) || io->read64(io->cookie,0x18,&v) || v)
        return status?status:MarbleEcatsIo;
    if(marble_ecats_write(io,8,saved[1],r) || marble_ecats_write(io,0x30,saved[3],r) ||
        marble_ecats_write(io,0,saved[0],r) || io->barrier(io->cookie) ||
        marble_ecats_poll(io,3,0,budget,r))return status?status:MarbleEcatsIo;
    for(unsigned i=0;i<4;i++)if(io->read64(io->cookie,regs[i],&v) || v!=saved[i])
        return status?status:MarbleEcatsIo;
    r->restored=1;r->retain=0;r->phase=5;
    return status;
}
static int marble_ecats_access(const ArmSmmu4KIo *io,uint64_t iova,unsigned access,unsigned budget,MarbleEcatsResult *r)
{return marble_ecats_sid_access(io,5,iova,access,budget,r);}
static int marble_ecats_request(const ArmSmmu4KIo *io,uint64_t iova,unsigned budget,MarbleEcatsResult *r)
{return marble_ecats_access(io,iova,0,budget,r);}
#endif
