/* Fixed RAM-only bundle: pinned PAS-refusal/no-AUTO derivative and ROM files.
 * This parser never installs a driver or interprets executable instructions. */
#ifndef MARBLE_ADSP_PACKAGE_H
#define MARBLE_ADSP_PACKAGE_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
 uint64_t magic,version,total,driver_offset,driver_bytes,files_offset,files_bytes,flags;
} MarbleAdspPackage;
_Static_assert(sizeof(MarbleAdspPackage)==64,"RAM ADSP bundle header size");
#define MARBLE_ADSP_PACKAGE_BYTES 30180144ULL
static int marble_adsp_package_geometry(const MarbleAdspPackage *p,uint64_t bytes)
{
 return p && bytes==MARBLE_ADSP_PACKAGE_BYTES && p->magic==0x313030505344414dULL && p->version==1 &&
     p->total==bytes && p->driver_offset==4096 && p->driver_bytes==118784 &&
     p->files_offset==122880 && p->files_bytes==30057264 && !p->flags;
}
static uint64_t marble_adsp_package_fnv(const uint8_t *p,uint64_t n)
{
 uint64_t h=0xcbf29ce484222325ULL;for(uint64_t i=0;i<n;i++)h=(h^p[i])*0x100000001b3ULL;return h;
}
static int marble_adsp_package_open(const void *raw,uint64_t bytes,const uint8_t **driver,const uint8_t **files)
{
 if(!raw || !driver || !files || ((uintptr_t)raw&7) || (uintptr_t)raw>UINTPTR_MAX-bytes || bytes<64)return 0;
 const MarbleAdspPackage *p=raw;
 if(!marble_adsp_package_geometry(p,bytes))return 0;
 const uint8_t *d=(const uint8_t *)raw+p->driver_offset,*f=(const uint8_t *)raw+p->files_offset;
 if(marble_adsp_package_fnv(d,p->driver_bytes)!=0x663099bf3f2c5867ULL ||
    marble_adsp_package_fnv(f,p->files_bytes)!=0xe41e8a04729dcffaULL)return 0;
 *driver=d;*files=f;return 1;
}
#endif
