/* Real ROM input tests. No hardware or HFI notification is involved. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/marble_gpu/marble_gpu_plan.h"
#include "../../xnu/files/bsd/dev/marble_gpu/upstream/ArmSmmu4KContext.h"

static unsigned char *read_file(const char *dir,const char *name,size_t *bytes)
{
    char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);
    rewind(f);unsigned char *p=malloc((size_t)n);assert(p);
    assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*bytes=(size_t)n;return p;
}
static uint64_t tables[2048];
static uint64_t walk(uint64_t va,const ArmSmmu4KResult *result)
{
    uint64_t base=result->root_physical;
    for(unsigned level=result->start_level;level<=3;level++) {
        size_t page=(size_t)((base-result->root_physical)/4096);
        assert(page<result->table_pages);
        uint64_t entry=tables[page*512+((va>>(39-9*level))&511)];
        if(!(entry&1))return UINT64_MAX;
        assert((entry&3)==3);
        base=entry&UINT64_C(0x0000fffffffff000);
        if(level==3)return base+(va&4095);
    }
    abort();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    size_t n;unsigned char *gmu=read_file(argv[1],"gmu_gen70000.bin",&n);
    assert(n==57272);
    AdrenoFirmwareBlock blocks[32],before[32];size_t count=0;
    assert(AdrenoFirmwarePlanGmu(gmu,n,marble_gmu_regions,5,blocks,32,&count)==AdrenoFirmwareSuccess);
    assert(count==15);
    unsigned payloads=0;size_t payload_bytes=0,first_payload_header=SIZE_MAX;
    for(size_t i=0;i<count;i++)if(blocks[i].bytes){
        if(!payloads)first_payload_header=blocks[i].payload_offset-16;
        payloads++;payload_bytes+=blocks[i].bytes;
    }
    assert(payloads==4 && payload_bytes==57032);
    memset(blocks,0xa5,sizeof(blocks));memcpy(before,blocks,sizeof(blocks));
    assert(AdrenoFirmwarePlanGmu(gmu,n-1,marble_gmu_regions,5,blocks,32,&count)==AdrenoFirmwareTruncated);
    assert(count==0 && !memcmp(blocks,before,sizeof(blocks)));
    assert(AdrenoFirmwarePlanGmu(gmu,n,marble_gmu_regions,5,blocks,1,&count)==AdrenoFirmwareNoSpace);
    assert(count==0 && !memcmp(blocks,before,sizeof(blocks)));
    /* The first real payload address is zero. Move it into an unmapped hole. */
    assert(first_payload_header<n-16);
    gmu[first_payload_header]=0;gmu[first_payload_header+1]=0;
    gmu[first_payload_header+2]=0;gmu[first_payload_header+3]=0x20;
    assert(AdrenoFirmwarePlanGmu(gmu,n,marble_gmu_regions,5,blocks,32,&count)==AdrenoFirmwareUnmapped);
    assert(!memcmp(blocks,before,sizeof(blocks)));free(gmu);

    size_t mn,lengths[3];unsigned char *mdt=read_file(argv[1],"a730_zap.mdt",&mn);
    unsigned char *p0=read_file(argv[1],"a730_zap.b00",lengths);
    unsigned char *p1=read_file(argv[1],"a730_zap.b01",lengths+1);
    unsigned char *p2=read_file(argv[1],"a730_zap.b02",lengths+2);
    const uint8_t *parts[]={p0,p1,p2};
    assert(marble_gpu_zap_structure(mdt,mn,parts,lengths));
    assert(!marble_gpu_zap_structure(mdt,mn-1,parts,lengths));
    p2[0]^=1;assert(!marble_gpu_zap_structure(mdt,mn,parts,lengths));p2[0]^=1;
    lengths[1]--;assert(!marble_gpu_zap_structure(mdt,mn,parts,lengths));
    free(mdt);free(p0);free(p1);free(p2);

    ArmSmmu4KConfig cfg={32,36,0x90000000,4};
    ArmSmmu4KRange ranges[]={{0x60000000,0x91000000,8192,3,1,3}};
    ArmSmmu4KResult result;
    assert(ArmSmmu4KBuild(&cfg,ranges,1,tables,4,&result)==ArmSmmu4KSuccess);
    assert(result.leaf_pages==2 && result.table_pages==3);
    assert(walk(0x60000000,&result)==0x91000000);
    assert(walk(0x60001fff,&result)==0x91001fff);
    assert(walk(0x60002000,&result)==UINT64_MAX);
    /* Encode against the measured marble capabilities, never a fake live ACK. */
    ArmSmmu4KContextConfig context={&cfg,NULL,&result,NULL,0x4c017e18,0x5511,0,1,0,0};
    ArmSmmu4KContextImage image;
    assert(ArmSmmu4KEncodeContext(&context,&image)==ArmSmmu4KSuccess);
    assert((image.tcr2&7)==1 && !(image.tcr&(3u<<14)));
    cfg.output_bits=40;
    assert(ArmSmmu4KEncodeContext(&context,&image)==ArmSmmu4KUnsupported);
    cfg.output_bits=36;
    uint64_t saved[2048];memcpy(saved,tables,sizeof(tables));
    ranges[0].physical=cfg.table_physical;
    assert(ArmSmmu4KBuild(&cfg,ranges,1,tables,4,&result)==ArmSmmu4KOverlap);
    assert(!memcmp(saved,tables,sizeof(tables)) && !result.table_pages);
    ranges[0].physical=0x91000000;ranges[0].bytes=4096*5;
    assert(ArmSmmu4KBuild(&cfg,ranges,1,tables,4,&result)==ArmSmmu4KBudget);
    assert(!memcmp(saved,tables,sizeof(tables)));
    puts("marble GPU: real Gen7 firmware, malformed inputs, split ZAP, 4K translation and refusal tests passed; no hardware accessed");
    return 0;
}
