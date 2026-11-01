/* Marble/Adreno725 board inputs; no MMIO or device activation in this file. */
#ifndef MARBLE_GPU_PLAN_H
#define MARBLE_GPU_PLAN_H
#include "upstream/AdrenoFirmware.h"
#include "upstream/ArmSmmu4K.h"

#define MARBLE_GPU_CHIP_ID UINT32_C(0x07030002)
#define MARBLE_GPU_ABI "MGPU-MARBLE-PREFLIGHT-v1"
/* Mu-marble DT; deliberately independent of the sheng/A740 board profile. */
static const uint64_t marble_gpu_windows[] = {
    0x03d00000,0x40000, 0x03d68000,0x37000, 0x03d90000,0xa000,
    0x03d50000,0x10000, 0x03da0000,0x40000, 0x0b290000,0x10000,
    0x03d40000,0x10000
};
/* Mu qsmmu-v500: TCU, gfx0 TBU, and gfx0 status. SID5 is in 0..0x3ff. */
static const uint64_t marble_tbu_windows[] = {
    0x03de6000,0x20, 0x03de9000,0x1000, 0x03de6200,8
};
/* Xiaomi Gen7 source's gen7_gmu_vma[], not A740's firmware layout. */
static const AdrenoFirmwareRegion marble_gmu_regions[] = {
    {0x00000000,0x4000,0}, {0x00004000,0xffc000,1},
    {0x10004000,0x4000,2}, {0x60000000,0x20000000,3},
    {0xc0000000,0x20000000,4}
};
static uint32_t marble_gpu_le32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned marble_gpu_le16(const uint8_t *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8;}

/* Check split image structure only. Qualcomm signature authentication is a
 * separate hardware step; these checks never call TrustZone. */
static int marble_gpu_zap_structure(const uint8_t *mdt,size_t size,
    const uint8_t *const parts[3],const size_t lengths[3])
{
    if(!mdt || !parts || !lengths || size<52 || mdt[0]!=0x7f ||
        mdt[1]!='E' || mdt[2]!='L' || mdt[3]!='F' || mdt[4]!=1 || mdt[5]!=1 ||
        marble_gpu_le16(mdt+40)!=52 || marble_gpu_le16(mdt+42)!=32 ||
        marble_gpu_le16(mdt+44)!=3)return 0;
    uint32_t offset=marble_gpu_le32(mdt+28);
    if(offset!=52 || offset>size || 96>size-offset)return 0;
    for(unsigned i=0;i<3;i++) {
        const uint8_t *ph=mdt+offset+i*32;
        if(!parts[i] || lengths[i]!=marble_gpu_le32(ph+16))return 0;
    }
    /* Metadata is ELF+PHDR followed by the hash segment. b01 is loadable. */
    if(lengths[0]!=148 || lengths[2]>size-148 || size!=148+lengths[2] ||
        marble_gpu_le32(mdt+offset+32)!=1 ||
        marble_gpu_le32(mdt+offset+32+16)>marble_gpu_le32(mdt+offset+32+20))return 0;
    for(size_t i=0;i<148;i++)if(mdt[i]!=parts[0][i])return 0;
    for(size_t i=0;i<lengths[2];i++)if(mdt[148+i]!=parts[2][i])return 0;
    return 1;
}
#endif
