/* Real firmware and real upstream AArch64 barriers; QEMU userspace fixture.
 * All queue responses below are synthetic test data, never hardware evidence. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_resources.h"
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/AdrenoHfi.h"
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KAudit.h"
static uint32_t memory[4096] __attribute__((aligned(16384)));
static uint32_t reference[4096];
extern void marble_source_initialize(uint32_t *,uint32_t);
static uint64_t tables[8*512];
static unsigned notifications,refuse_notify;
static int notify(void *c) {(void)c;notifications++;return refuse_notify?-1:0;}
static int descriptor(void *cookie,uint64_t pa,uint64_t *v)
{
    (void)cookie;if((pa&7) || pa<0x91000000 || pa-0x91000000>=sizeof(tables))return 1;
    *v=tables[(pa-0x91000000)/8];return 0;
}
static void transport(AdrenoHfiProfile profile)
{
    AdrenoHfi h;uint32_t header,error,out[16],payload=0xaabbccdd;size_t count;
    unsigned off=profile==AdrenoHfiProfileMarbleGen7?0x350:0x1000;
    assert(AdrenoHfiInitializeStoppedProfile(&h,memory,sizeof(memory),0x60006000,NULL,notify,profile)==AdrenoHfiSuccess);
    if(profile==AdrenoHfiProfileMarbleGen7)assert(marble_hfi_image_valid(memory,sizeof(memory),h.iova));
    else {
        assert(memory[1]==168);
        for(unsigned q=0;q<3;q++)assert(memory[7+12*q]==h.iova+(q+1)*4096 && memory[8+12*q]==(0xa00|q));
    }
    uint32_t *cmd=memory+off/4,*response=memory+(off+4096)/4;
    /* Command wrap and matching response: preserve the upstream transport. */
    memory[16]=memory[17]=1020;
    assert(AdrenoHfiSend(&h,1,&payload,1,&header)==AdrenoHfiSuccess);
    assert(cmd[1020]==header && cmd[1021]==payload && cmd[1022]==0xfafafafa && memory[17]==0);
    assert(AdrenoHfiSend(&h,1,&payload,1,&error)==AdrenoHfiBusy);
    assert(AdrenoHfiPoll(&h,out,16,&count,&error)==AdrenoHfiEmpty);
    response[0]=(4u<<8)|(1u<<16)|1;response[1]=header;response[2]=0;response[3]=0x193842;
    memory[29]=4;
    assert(AdrenoHfiPoll(&h,out,16,&count,&error)==AdrenoHfiSuccess && count==1 && out[0]==0x193842 && !h.pending_header);
    assert(AdrenoHfiInitializeStoppedProfile(&h,memory,sizeof(memory),0x60006000,NULL,notify,profile)==AdrenoHfiSuccess);
    assert(AdrenoHfiExchange(&h,1,NULL,0,3,NULL,out,16,&count,&error)==AdrenoHfiTimeout && h.poisoned);
    assert(AdrenoHfiSend(&h,1,NULL,0,&header)==AdrenoHfiPoisoned);
    assert(AdrenoHfiInitializeStoppedProfile(&h,memory,sizeof(memory),0x60006000,NULL,notify,profile)==AdrenoHfiSuccess);
    refuse_notify=1;
    assert(AdrenoHfiSend(&h,1,NULL,0,&header)==AdrenoHfiNotifyError && h.poisoned && h.pending_header);
    refuse_notify=0;
    assert(AdrenoHfiInitializeStoppedProfile(&h,memory,sizeof(memory),0x60006000,NULL,notify,profile)==AdrenoHfiSuccess);
    memory[7]^=4;unsigned before=notifications;
    assert(AdrenoHfiSend(&h,1,NULL,0,&header)==AdrenoHfiCorrupt && h.poisoned && notifications==before);
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t *fw=malloc(57272);assert(fw);
    assert(fread(fw,1,57272,f)==57272);fclose(f);
    AdrenoFirmwareBlock blocks[32];size_t n;MarbleGmuMemoryPlan p;MarbleGmuResourcePlan r;
    assert(AdrenoFirmwarePlanGmu(fw,57272,marble_gmu_regions,5,blocks,32,&n)==AdrenoFirmwareSuccess);
    assert(marble_gmu_memory_plan(blocks,n,0x90000000,0x34000,&p));
    assert(marble_gmu_resources_plan(&p,0x92000000,0x92004000,&r));
    assert(r.count==8 && r.pages==59 && r.log_iova==0x60002000 && r.hfi_iova==0x60006000 && r.log_register==0x60002003);
    assert(r.ranges[6].permissions==11 && r.ranges[7].permissions==11);
    assert(!marble_gmu_resources_plan(&p,0x92000001,0x92004000,&r));
    assert(!marble_gmu_resources_plan(&p,0x92000000,0x92000000,&r));
    assert(!marble_gmu_resources_plan(&p,0x90030000,0x92004000,&r));
    assert(!marble_gmu_resources_plan(&p,UINT64_C(1)<<36,0x92004000,&r));
    MarbleGmuMemoryPlan bad=p;bad.ranges[5].iova=0x60000000;
    assert(!marble_gmu_resources_plan(&bad,0x92000000,0x92004000,&r));
    assert(marble_gmu_resources_plan(&p,0x92000000,0x92004000,&r));
    ArmSmmu4KConfig cfg={32,36,0x91000000,59};ArmSmmu4KResult built;
    assert(ArmSmmu4KBuild(&cfg,r.ranges,r.count,tables,8,&built)==ArmSmmu4KSuccess && built.leaf_pages==59 && built.table_pages==6);
    unsigned char visited[8];ArmSmmu4KAuditRegion region={r.ranges,8,8,4096,visited,8};
    ArmSmmu4KTableIo io={NULL,descriptor};ArmSmmu4KTableAuditResult audit;
    assert(ArmSmmu4KAuditTables(&io,&cfg,&built,&region,built.root_physical,&audit)==ArmSmmu4KSuccess && audit.leaf_pages==59);
    unsigned mutations=0;
    for(size_t i=0;i<built.table_pages*512;i++)if((tables[i]&3)==3 && (tables[i]&(1u<<10))) {
        for(unsigned bit=2;bit<64;bit++) {
            tables[i]^=UINT64_C(1)<<bit;
            assert(ArmSmmu4KAuditTables(&io,&cfg,&built,&region,built.root_physical,&audit)!=ArmSmmu4KSuccess);
            tables[i]^=UINT64_C(1)<<bit;mutations++;
        }
    }
    assert(mutations==59*62);
    AdrenoHfi h;
    assert(AdrenoHfiInitializeStoppedProfile(&h,memory,sizeof(memory),r.hfi_iova,NULL,notify,AdrenoHfiProfileMarbleGen7)==AdrenoHfiSuccess);
    assert(!notifications && marble_hfi_image_valid(memory,sizeof(memory),r.hfi_iova));
    marble_source_initialize(reference,r.hfi_iova);
    assert(!memcmp(memory,reference,sizeof(memory)));
    for(unsigned i=0;i<4096;i++) {
        memory[i]^=1;assert(!marble_hfi_image_valid(memory,sizeof(memory),r.hfi_iova));memory[i]^=1;
    }
    assert(AdrenoHfiInitializeStoppedProfile(&h,memory,sizeof(memory),r.hfi_iova,NULL,notify,(AdrenoHfiProfile)99)==AdrenoHfiInvalid);
    assert(marble_hfi_image_valid(memory,sizeof(memory),r.hfi_iova));
    assert(AdrenoHfiInitializeStopped(&h,memory,sizeof(memory),r.hfi_iova,NULL,notify)==AdrenoHfiSuccess && h.profile==AdrenoHfiProfileSheng);
    transport(AdrenoHfiProfileSheng);transport(AdrenoHfiProfileMarbleGen7);
    printf("GMU resources fixture PASS: real firmware, 8 ranges/59 leaves/6 tables, %u descriptor mutations, 4096 queue-image mutations, byte-exact Xiaomi source oracle, both HFI profiles wrap/ACK/timeout/poison; synthetic transport only\n",mutations);
    free(fw);return 0;
}
