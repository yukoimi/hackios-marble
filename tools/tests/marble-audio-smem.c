#include "marble-audio-smem-fixture.h"
int main(void)
{
    MarbleAudioSmem out;
    for(unsigned rx=0;rx<2;rx++) {
        fixture(rx);assert(marble_audio_smem_find(read_bytes,0,&out));
        assert(out.partition==0x1cf000 && out.partition_bytes==0x30000 && out.present==(rx?7:3));
        assert(out.item[0]==0x1cf030 && out.item[1]==0x1cf060 && out.bytes[0]==32 && out.bytes[1]==16384);
        if(rx)assert(out.item[2]==0x1d3070 && out.bytes[2]==16384);
    }
    const unsigned bad_at[]={0x1ff000,0x1ff004,0x1ff008,0x1ff020,0x1ff024,0x1ff02c,0x1ff030,0x1cf000,0x1cf004,0x1cf008,0x1cf00c,0x1cf010,0x1cf020,0x1cf024,0x1cf028,0x1cf02a};
    for(unsigned i=0;i<sizeof(bad_at)/sizeof(*bad_at);i++) {
        fixture(1);put32(bad_at[i],0xffffffff);MarbleAudioSmem saved;memset(&saved,0x5a,sizeof(saved));out=saved;
        assert(!marble_audio_smem_find(read_bytes,0,&out) && !memcmp(&out,&saved,sizeof(out)));
    }
    fixture(1);put32(0x1cf050,0xa5a5|478u<<16);assert(!marble_audio_smem_find(read_bytes,0,&out));
    fixture(1);put32(0x1cf028,33);assert(!marble_audio_smem_find(read_bytes,0,&out));
    /* Cached non-target item, with a valid descending walk. */
    fixture(0);put32(0x1cf010,0x2ff40);put32(0x1fefc0,0xa5a5|200u<<16);put32(0x1fefc4,128);
    assert(marble_audio_smem_find(read_bytes,0,&out) && out.entries==3);
    /* Moving allocator metadata is not a stable transport view. */
    fixture(1);changed=1;assert(!marble_audio_smem_find(read_bytes,0,&out));
    assert(reads<600);puts("PASS native SMEM discovery: exact APPS/ADSP partition, before/after RX allocation, cached walk, stable allocator snapshot, bounds/padding/duplicate rejection; read callback only");
}
