#ifndef MARBLE_AUDIO_SMEM_FIXTURE_H
#define MARBLE_AUDIO_SMEM_FIXTURE_H
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/sys/marble_audio_smem.h"
static uint8_t memory[MASMEM_BYTES];
static unsigned reads,changed;
static void put32(uint32_t at,uint32_t v)
{for(unsigned i=0;i<4;i++)memory[at+i]=(uint8_t)(v>>(8*i));}
static int read_bytes(void *cookie,uint32_t at,void *out,uint32_t n)
{
    (void)cookie;assert(at<=sizeof(memory) && n<=sizeof(memory)-at);reads++;
    memcpy(out,memory+at,n);
    if(changed && at==0x1cf000 && reads>3)((uint8_t *)out)[12]^=8;
    return 1;
}
static unsigned add(unsigned at,unsigned id,unsigned n)
{
    put32(0x1cf000+at,0xa5a5|id<<16);put32(0x1cf000+at+4,n);return at+16+n;
}
static void fixture(unsigned remote)
{
    memset(memory,0,sizeof(memory));reads=changed=0;
    unsigned toc=MASMEM_BYTES-4096;put32(toc,0x434f5424);put32(toc+4,1);put32(toc+8,1);
    put32(toc+32,0x1cf000);put32(toc+36,0x30000);put32(toc+44,2u<<16);put32(toc+48,64);
    put32(0x1cf000,0x54525024);put32(0x1cf004,2u<<16);put32(0x1cf008,0x30000);
    unsigned at=add(32,478,32);at=add(at,479,16384);if(remote)at=add(at,480,16384);
    put32(0x1cf00c,at);put32(0x1cf010,0x30000);
}
#endif
