/* One bounded resource-owner session. Each producer owns whole cache lines.
 * Header is immutable after publication; owner/client snapshots use an even
 * sequence, published only after data and barriers. Odd means in progress.
 * Hardware adapters must supply cache maintenance and enforce time/thermal
 * limits. A valid mailbox is NOT proof that a DSP or DMA lease exists. */
#ifndef MARBLE_AUDIO_OWNER_H
#define MARBLE_AUDIO_OWNER_H
#include <stdint.h>
#include <stddef.h>
#include "marble_audio_proof.h"
#define MAOWNER_MAGIC 0x31524e574f55414dULL
#define MAOWNER_PAGE_BYTES 16384u
enum { MAOWNER_PREPARING=1,MAOWNER_RUNNING,MAOWNER_CONFIGURED,
       MAOWNER_PAS_OFF,MAOWNER_RELEASED,MAOWNER_FAILED };
enum { MAOWNER_READY=1,MAOWNER_CONFIGURE,MAOWNER_RETIRE,MAOWNER_UNMAPPED };
enum { MAOWNER_PAS_VERIFIED=1,MAOWNER_ROUTE_RESTORED=2,MAOWNER_TABLES_RELEASED=4 };
typedef struct {
    uint64_t magic,epoch;
    uint32_t version,bytes;
    uint64_t smem_pa;
    uint32_t smem_bytes,periods;
    uint64_t ipcc_pa;
    uint8_t reserved[16];
} MarbleAudioOwnerInfo;
typedef struct {
    uint64_t epoch;
    uint32_t sequence,state;
    uint64_t page_pa,deadline_us;
    uint32_t acknowledged,flags,submitted,completed;
    uint8_t reserved[16];
} MarbleAudioOwnerGrant;
typedef struct {
    uint64_t epoch;
    uint32_t sequence,command,head,tail,tx,rx;
    uint8_t reserved[32];
} MarbleAudioOwnerRequest;
typedef struct {
    MarbleAudioOwnerInfo info;
    MarbleAudioOwnerGrant owner;
    MarbleAudioOwnerRequest client;
    MarbleAudioProof proof;
} MarbleAudioOwnerMailbox;
_Static_assert(sizeof(MarbleAudioOwnerInfo)==64 && sizeof(MarbleAudioOwnerGrant)==64 &&
    sizeof(MarbleAudioOwnerRequest)==64 && sizeof(MarbleAudioOwnerMailbox)==320,"audio owner cache-line ABI");
_Static_assert(offsetof(MarbleAudioOwnerMailbox,owner)==64 && offsetof(MarbleAudioOwnerMailbox,client)==128 &&
    offsetof(MarbleAudioOwnerMailbox,proof)==192,"audio owner producer boundaries");
static int maowner_zero(const void *data,unsigned n)
{const uint8_t *p=data;for(unsigned i=0;i<n;i++)if(p[i])return 0;return 1;}
static int marble_audio_owner_info_valid_periods(const MarbleAudioOwnerInfo *p,unsigned periods)
{
    return p && p->magic==MAOWNER_MAGIC && p->epoch && p->version==1 && p->bytes==320 &&
        p->smem_pa==0x80900000ULL && p->smem_bytes==0x200000 && p->ipcc_pa==0xed18000 &&
        (periods==2 || periods==8) && p->periods==periods && maowner_zero(p->reserved,sizeof(p->reserved));
}
static int marble_audio_owner_grant_valid_periods(const MarbleAudioOwnerGrant *p,uint64_t epoch,unsigned periods)
{
    if((periods!=2 && periods!=8) || !p || !epoch || p->epoch!=epoch || !p->sequence || (p->sequence&1) ||
       p->state<MAOWNER_PREPARING || p->state>MAOWNER_FAILED || p->acknowledged>8 || (p->acknowledged&1) ||
       p->submitted>periods || p->completed>p->submitted || !maowner_zero(p->reserved,sizeof(p->reserved)))return 0;
    if(p->state==MAOWNER_FAILED)return p->flags==0;
    if(p->state==MAOWNER_PREPARING)return !p->page_pa && !p->deadline_us && !p->flags;
    if(p->state==MAOWNER_RELEASED)return !p->page_pa && p->flags==7 && p->acknowledged==8 && p->submitted==periods && p->completed==periods;
    if(p->page_pa<4096 || p->page_pa>=(1ULL<<36) || (p->page_pa&4095) || !p->deadline_us)return 0;
    if(p->state==MAOWNER_PAS_OFF)return p->flags==3 && p->acknowledged==6 && p->submitted==periods && p->completed==periods;
    return !p->flags && p->acknowledged==(p->state==MAOWNER_RUNNING?2u:4u);
}
static int marble_audio_owner_request_valid_periods(const MarbleAudioOwnerRequest *p,const MarbleAudioProof *proof,uint64_t epoch,unsigned periods)
{
    if((periods!=2 && periods!=8) || !p || !proof || !epoch || p->epoch!=epoch || p->command<1 || p->command>4 ||
       p->sequence!=2*p->command || ((p->head|p->tail)&7) || p->head>=16384 || p->tail>=16384 ||
       p->tx>256 || p->rx>2048 || !maowner_zero(p->reserved,sizeof(p->reserved)))return 0;
    if(p->command==MAOWNER_CONFIGURE || p->command==MAOWNER_RETIRE)
        return marble_audio_proof_valid(proof,epoch,p->command==MAOWNER_CONFIGURE?MAPROOF_CONFIGURE:MAPROOF_RETIRED) &&
            (p->command!=MAOWNER_RETIRE || (proof->submitted==periods && proof->completed==periods));
    return maowner_zero(proof,sizeof(*proof)) &&
        (p->command!=MAOWNER_READY || (!p->head && !p->tail && !p->tx && !p->rx));
}
/* Legacy wrappers remain strictly two periods. The eight-period caller must
 * opt in at every ownership boundary; it cannot weaken a two-period session. */
static int marble_audio_owner_info_valid(const MarbleAudioOwnerInfo *p)
{return marble_audio_owner_info_valid_periods(p,2);}
static int marble_audio_owner_grant_valid(const MarbleAudioOwnerGrant *p,uint64_t epoch)
{return marble_audio_owner_grant_valid_periods(p,epoch,2);}
static int marble_audio_owner_request_valid(const MarbleAudioOwnerRequest *p,const MarbleAudioProof *proof,uint64_t epoch)
{return marble_audio_owner_request_valid_periods(p,proof,epoch,2);}
#endif
