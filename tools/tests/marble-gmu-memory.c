#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gmu_memory.h"
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KAudit.h"
static uint64_t tables[8*512];
static int descriptor(void *cookie,uint64_t pa,uint64_t *out)
{
    (void)cookie;if((pa&7) || pa<0x91000000 || pa-0x91000000>=sizeof(tables))return 1;
    *out=tables[(pa-0x91000000)/8];return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n==57272);rewind(f);
    unsigned char *fw=malloc((size_t)n);assert(fw && fread(fw,1,(size_t)n,f)==(size_t)n);fclose(f);
    AdrenoFirmwareBlock blocks[32],copy[32];size_t count;
    assert(AdrenoFirmwarePlanGmu(fw,(size_t)n,marble_gmu_regions,5,blocks,32,&count)==AdrenoFirmwareSuccess);
    MarbleGmuMemoryPlan p;
    assert(marble_gmu_memory_plan(blocks,count,0x90000000,MARBLE_GMU_BACKING_BYTES,&p));
    assert(p.count==6 && p.pages==51 && p.bytes==0x33000 && p.ram_payloads==1 && p.tcm_payloads==3);
    static const uint64_t addresses[]={0x4000,0x100000,0x200000,0x300000,0x60000000,0x60001000};
    static const uint64_t sizes[]={0xe000,0x9000,0x13000,0x7000,0x1000,0x1000};
    for(size_t i=0;i<6;i++) {
        assert(p.ranges[i].iova==addresses[i] && p.ranges[i].bytes==sizes[i]);
        assert(p.ranges[i].permissions==(i<4?15:11));
    }
    ArmSmmu4KConfig cfg={32,36,0x91000000,51};ArmSmmu4KResult result;
    assert(ArmSmmu4KBuild(&cfg,p.ranges,6,tables,4,&result)==ArmSmmu4KCapacity);
    assert(ArmSmmu4KBuild(&cfg,p.ranges,6,tables,8,&result)==ArmSmmu4KSuccess);
    assert(result.table_pages==6 && result.leaf_pages==51);
    unsigned char visited[8];ArmSmmu4KAuditRegion region={p.ranges,6,8,4096,visited,8};
    ArmSmmu4KTableIo io={NULL,descriptor};ArmSmmu4KTableAuditResult audit;
    assert(ArmSmmu4KAuditTables(&io,&cfg,&result,&region,result.root_physical,&audit)==ArmSmmu4KSuccess);
    assert(audit.complete && audit.table_pages==6 && audit.leaf_pages==51 && audit.descriptor_reads==3072);
    /* Every leaf permission/cache/address bit mutation must be detected. */
    unsigned mutations=0;
    for(size_t i=0;i<result.table_pages*512;i++)if((tables[i]&3)==3 && (tables[i]&(1u<<10))) {
        for(unsigned bit=2;bit<64;bit++) {
            tables[i]^=UINT64_C(1)<<bit;
            assert(ArmSmmu4KAuditTables(&io,&cfg,&result,&region,result.root_physical,&audit)!=ArmSmmu4KSuccess);
            tables[i]^=UINT64_C(1)<<bit;mutations++;
        }
    }
    assert(mutations==51*62);
    for(unsigned i=0;i<11;i++) {
        memcpy(copy,blocks,sizeof(blocks));copy[i].value=UINT32_MAX;
        assert(!marble_gmu_memory_plan(copy,count,0x90000000,MARBLE_GMU_BACKING_BYTES,&p) && !p.count);
        memcpy(copy,blocks,sizeof(blocks));copy[i].type=2;
        assert(!marble_gmu_memory_plan(copy,count,0x90000000,MARBLE_GMU_BACKING_BYTES,&p));
    }
    memcpy(copy,blocks,sizeof(blocks));copy[10].address=copy[9].address;
    assert(!marble_gmu_memory_plan(copy,count,0x90000000,MARBLE_GMU_BACKING_BYTES,&p));
    memcpy(copy,blocks,sizeof(blocks));copy[12].address=0x80000;
    assert(!marble_gmu_memory_plan(copy,count,0x90000000,MARBLE_GMU_BACKING_BYTES,&p));
    assert(!marble_gmu_memory_plan(blocks,count,0x90000000,0x32000,&p));
    assert(!marble_gmu_memory_plan(blocks,count,0x90000001,MARBLE_GMU_BACKING_BYTES,&p));
    assert(!marble_gmu_memory_plan(blocks,count,UINT64_C(1)<<36,MARBLE_GMU_BACKING_BYTES,&p));
    printf("GMU memory fixture: real firmware, 6 reservations/51 leaves/6 tables, 4-table refusal, %u descriptor mutations, metadata/range/ownership failures PASS; synthetic PAs only, no hardware proof\n",mutations);
    free(fw);return 0;
}
