/* CPU-only Gen7 candidate tables. No queue/device callbacks or start API.
 * Actual command DB aux is supplied by the caller's immutable snapshot.
 * Catalog includes all Mu DT levels; it is NOT an enabled OPP policy. */
#ifndef MARBLE_GMU_VOTES_H
#define MARBLE_GMU_VOTES_H
#include "marble_gmu_preflight.h"
#include "marble_gmu_catalog.h"
#include "upstream/AdrenoHfiVotes.h"
#define MARBLE_GMU_VOTES_ABI "MGPU-MARBLE-GMU-VOTES-v1"
typedef struct {
    AdrenoHfiStartupConfig config;
    uint32_t performance[159],bandwidth[159];
    uint32_t performance_id,bandwidth_id;
    size_t performance_words,bandwidth_words;
} MarbleGmuVotePlan;
static int marble_gmu_vote_plan(const uint8_t *db,size_t bytes,MarbleGmuVotePlan *out)
{
    if(!out)return 0;
    MarbleCmdDbEntry e[7];
    static const char *const names[]={"gfx.lvl","cx.lvl","mx.lvl","CN0","SH0","MC0","ACV"};
    for(unsigned i=0;i<7;i++)if(!marble_cmd_db_find(db,bytes,names[i],e+i) ||
        e[i].slave!=(i<3?3:5))return 0;
    MarbleGmuVotePlan p={0};AdrenoHfiStartupConfig *c=&p.config;
    uint32_t gx_levels[8]={0},gmu_levels[]={0,64,128},gx[8],gmu[3];
    c->gpu_count=8;c->gmu_count=3;
    for(unsigned i=0;i<7;i++) {
        const MarbleGmuCatalogLevel *l=marble_gmu_gpu_candidates+i;
        gx_levels[i+1]=l->level;c->gpu[i+1].hz=l->hz;
        unsigned index=0;
        if(!e[1].bytes || (e[1].bytes&1) || e[1].bytes>512)return 0;
        while(index<e[1].bytes/2 && marble_db_u16(e[1].aux+2*index)<l->cx_level)index++;
        if(index>=e[1].bytes/2)return 0;
        c->gpu_cx_votes[i+1]=index;
    }
    if(AdrenoHfiArcVotesProfile(gx_levels,8,e[0].aux,e[0].bytes,e[2].aux,e[2].bytes,gx,8,AdrenoHfiProfileMarbleGen7) ||
        AdrenoHfiArcVotesProfile(gmu_levels,3,e[1].aux,e[1].bytes,e[2].aux,e[2].bytes,gmu,3,AdrenoHfiProfileMarbleGen7))return 0;
    for(unsigned i=0;i<8;i++)c->gpu[i].vote=gx[i];
    for(unsigned i=0;i<3;i++)c->gmu[i]=(AdrenoHfiPerformanceLevel){i==0?0:i==1?200000000:500000000,gmu[i]};
    AdrenoHfiBcm cnoc={0},bcms[3]={{0}};
    cnoc.address=e[3].address;cnoc.buswidth=4;
    if(AdrenoHfiBcmAux(&cnoc,e[3].aux,e[3].bytes))return 0;
    for(unsigned i=0;i<3;i++) {
        bcms[i].address=e[4+i].address;bcms[i].buswidth=i==0?16:i==1?4:0;bcms[i].fixed=i==2;
        if(AdrenoHfiBcmAux(bcms+i,e[4+i].aux,e[4+i].bytes))return 0;
    }
    if(AdrenoHfiBuildBandwidthProfile(marble_gmu_ddr_candidates,12,bcms,3,&cnoc,c,AdrenoHfiProfileMarbleGen7) ||
        AdrenoHfiStartupValidateProfile(c,AdrenoHfiProfileMarbleGen7) ||
        AdrenoHfiStartupEncodeProfile(c,AdrenoHfiStagePerformance,&p.performance_id,p.performance,159,
            &p.performance_words,AdrenoHfiProfileMarbleGen7) ||
        AdrenoHfiStartupEncodeProfile(c,AdrenoHfiStageBandwidth,&p.bandwidth_id,p.bandwidth,159,
            &p.bandwidth_words,AdrenoHfiProfileMarbleGen7))return 0;
    *out=p;return 1;
}
#endif
