/* Read-only APPS/ADSP private-SMEM discovery. Layouts reused from the
 * existing redwood_adsp.c and pinned Linux smem.c (v205 audit).
 * Callback reads only the DT-qualified 2MiB SMEM RAM window; no allocation,
 * FIFO advancement, IPCC acknowledgement, DSP start or payload access.
 */
#ifndef MARBLE_AUDIO_SMEM_H
#define MARBLE_AUDIO_SMEM_H
#include <stdint.h>
#define MASMEM_BYTES 0x200000u
typedef int (*MarbleAudioSmemRead)(void *,uint32_t,void *,uint32_t);
typedef struct {
    uint32_t partition,partition_bytes,cacheline,present;
    uint32_t item[3],bytes[3],cached[3],entries;
} MarbleAudioSmem;
static uint16_t masmem16(const uint8_t *p)
{return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t masmem32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int masmem_read(MarbleAudioSmemRead read,void *cookie,uint32_t offset,void *out,uint32_t n)
{return read && out && offset<=MASMEM_BYTES && n<=MASMEM_BYTES-offset && read(cookie,offset,out,n);}
static int masmem_item(MarbleAudioSmem *r,const uint8_t *entry,uint32_t at,unsigned cached)
{
    uint32_t id=masmem16(entry+2),allocated=masmem32(entry+4),pad=masmem16(entry+8),hdr=masmem16(entry+10);
    if(masmem16(entry)!=0xa5a5 || !allocated || pad>=allocated || (allocated&7))return 0;
    if(id>=478 && id<=480) {
        unsigned index=id-478;
        if(r->present&(1u<<index))return 0;
        uint32_t bytes=allocated-pad;
        if(bytes!=(index?16384u:32u))return 0;
        uint32_t offset=cached?at-allocated:at+16+hdr;
        if(offset&7)return 0;
        r->item[index]=r->partition+offset;r->bytes[index]=bytes;
        r->cached[index]=cached;r->present|=1u<<index;
    }
    return 1;
}
static int marble_audio_smem_find(MarbleAudioSmemRead read,void *cookie,MarbleAudioSmem *out)
{
    if(!read || !out)return 0;
    uint8_t toc[32],entry[48],header[32],after[32];MarbleAudioSmem r={0};
    const uint32_t table=MASMEM_BYTES-4096;
    if(!masmem_read(read,cookie,table,toc,32) || masmem32(toc)!=0x434f5424 || masmem32(toc+4)!=1)return 0;
    uint32_t count=masmem32(toc+8);
    if(!count || count>64)return 0;
    for(uint32_t i=0;i<count;i++) {
        if(!masmem_read(read,cookie,table+32+i*48,entry,48))return 0;
        uint32_t offset=masmem32(entry),bytes=masmem32(entry+4);
        unsigned a=masmem16(entry+12),b=masmem16(entry+14);
        if(!((a==0 && b==2)||(a==2 && b==0)))continue;
        if(r.partition || offset<4096 || (offset&4095) || bytes!=0x30000 || offset>table || bytes>table-offset)return 0;
        uint32_t cacheline=masmem32(entry+16);
        if(cacheline<16 || cacheline>4096 || (cacheline&(cacheline-1)))return 0;
        r.partition=offset;r.partition_bytes=bytes;r.cacheline=cacheline;
    }
    if(!r.partition || !masmem_read(read,cookie,r.partition,header,32) || masmem32(header)!=0x54525024 ||
       !((masmem16(header+4)==0 && masmem16(header+6)==2)||(masmem16(header+4)==2 && masmem16(header+6)==0)) ||
       masmem32(header+8)!=r.partition_bytes)return 0;
    uint32_t uncached=masmem32(header+12),cached=masmem32(header+16),at=32;
    if(uncached<32 || uncached>cached || cached>r.partition_bytes || ((uncached|cached)&7))return 0;
    while(at<uncached) {
        if(++r.entries>512 || uncached-at<16 || !masmem_read(read,cookie,r.partition+at,entry,16))return 0;
        uint32_t n=masmem32(entry+4),pad=masmem16(entry+10);
        if(pad>uncached-at-16 || n>uncached-at-16-pad || !masmem_item(&r,entry,at,0))return 0;
        at+=16+pad+n;
    }
    uint32_t stride=(16+r.cacheline-1)&~(r.cacheline-1);
    at=r.partition_bytes-stride;
    while(at>cached) {
        if(++r.entries>512 || !masmem_read(read,cookie,r.partition+at,entry,16))return 0;
        uint32_t n=masmem32(entry+4);
        if(n>at || at-n<cached || stride>at-n || !masmem_item(&r,entry,at,1))return 0;
        at-=n+stride;
    }
    if(!masmem_read(read,cookie,r.partition,after,32))return 0;
    for(unsigned i=0;i<32;i++)if(header[i]!=after[i])return 0;
    if((r.present&3)!=3 || r.cached[0] || r.cached[1] || (r.present&4 && r.cached[2]))return 0;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<i;j++)
        if((r.present&(1u<<i)) && (r.present&(1u<<j)) && r.item[i]<r.item[j]+r.bytes[j] && r.item[j]<r.item[i]+r.bytes[i])return 0;
    *out=r;return 1;
}
#endif
