#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/marble_pil_segment.h"
int main(void)
{
    uint8_t data[5964],bad[5964];
    FILE *f=fopen("out/n12turbo-rom/daily-material/firmware/image/adsp2.mdt","rb");
    assert(f && fread(data,1,sizeof(data),f)==sizeof(data) && fgetc(f)==EOF);fclose(f);
    MarblePILSegmentPlan p;
    assert(marble_pil_segment_plan(data,sizeof(data),&p));
    assert(p.target==0x9fd00000 && p.origin==0x86300000 && p.file_bytes==6760 && p.memory_bytes==8192);
    assert(!marble_pil_segment_plan(data,sizeof(data)-1,&p));
    /* Reject malicious destinations, sizes, unrelocatable flags, header and
     * table counts before the diagnostic can touch any physical memory. */
    const unsigned offsets[]={0,4,16,20,28,40,42,44,45,52,68,84,88,92,96,100,104,108,112};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(bad,data,sizeof(data));bad[offsets[i]]^=0x80;
        assert(!marble_pil_segment_plan(bad,sizeof(bad),&p));
    }
    memcpy(bad,data,sizeof(data));
    /* First later LOAD segment: an overflowing extent must also fail. */
    unsigned hit=0;
    for(unsigned i=2;i<27;i++) {
        uint8_t *ph=bad+52+32*i;
        if(marble_pil_u32(ph)==1 && marble_pil_u32(ph+20)) {
            memset(ph+12,0xff,4);hit=1;break;
        }
    }
    assert(hit && !marble_pil_segment_plan(bad,sizeof(bad),&p));
    memcpy(bad,data,sizeof(data));memcpy(bad+52+2*32+12,data+84+12,4);
    assert(!marble_pil_segment_plan(bad,sizeof(bad),&p));
    puts("marble-pil-segment: pinned ROM plan and malformed bounds PASS; no device access");
    return 0;
}
