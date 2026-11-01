/* Bounded LZ4 transport for an ordinary AArch64 EFI application. The UEFI
 * image loader performs PE relocation and execution; this code never jumps
 * into decoded bytes or accesses a disk/block protocol. */
#ifndef MARBLE_EFI_CHAIN_H
#define MARBLE_EFI_CHAIN_H
#include <stdint.h>
#include <stddef.h>
#include "vendor/sha256/sha256.h"
#ifdef MARBLE_CHAIN_LZMA
#include "marble_lzma_decode.h"
#endif
extern int LZ4_decompress_safe(const char *,char *,int,int);
static unsigned marble_chain_u16(const uint8_t *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8;}
static uint32_t marble_chain_u32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int marble_chain_pe(const uint8_t *p,size_t n)
{
    if(!p || n<512 || n>1048576 || p[0]!='M' || p[1]!='Z')return 0;
    uint32_t pe=marble_chain_u32(p+60);
    if(pe>n-264 || marble_chain_u32(p+pe)!=0x4550 || marble_chain_u16(p+pe+4)!=0xaa64)return 0;
    unsigned count=marble_chain_u16(p+pe+6),opt=marble_chain_u16(p+pe+20);
    if(!count || count>16 || opt<240 || pe+24u+opt>n || count>(n-pe-24u-opt)/40)return 0;
    const uint8_t *o=p+pe+24;uint32_t image=marble_chain_u32(o+56),headers=marble_chain_u32(o+60);
    uint32_t entry=marble_chain_u32(o+16),section=marble_chain_u32(o+32),file=marble_chain_u32(o+36);
    if(marble_chain_u16(o)!=0x20b || marble_chain_u16(o+68)!=10 ||
       !image || image>32*1024*1024 || !headers || headers>n || headers>image ||
       !entry || entry>=image || file<512 || file>65536 || (file&(file-1)) ||
       section<4096 || section>65536 || (section&(section-1)) || section<file)return 0;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *s=o+opt+i*40;uint32_t size=marble_chain_u32(s+16),raw=marble_chain_u32(s+20);
        uint32_t va=marble_chain_u32(s+12),vsize=marble_chain_u32(s+8);
        if(raw>n || size>n-raw || va>image || vsize>image-va || size>image-va)return 0;
    }
    return 1;
}
static int marble_chain_unpack(const uint8_t *packed,size_t packed_bytes,uint8_t *raw,size_t raw_bytes,const uint8_t expected[32])
{
    if(!packed || !raw || !expected || !packed_bytes || packed_bytes>1048576 || raw_bytes<512 || raw_bytes>1048576)return 0;
#ifdef MARBLE_CHAIN_LZMA
    if(!marble_lzma_decode(packed,packed_bytes,raw,raw_bytes))return 0;
#else
    if(LZ4_decompress_safe((const char *)packed,(char *)raw,(int)packed_bytes,(int)raw_bytes)!=(int)raw_bytes)return 0;
#endif
    SHA256_CTX context;uint8_t hash[32];sha256_init(&context);sha256_update(&context,raw,raw_bytes);sha256_final(&context,hash);
    unsigned different=0;for(unsigned i=0;i<32;i++)different|=hash[i]^expected[i];
    return !different && marble_chain_pe(raw,raw_bytes);
}
#endif
