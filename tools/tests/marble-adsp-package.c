#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../xnuloader/marble_adsp_package.h"
int main(int argc,char **argv)
{
 MarbleAdspPackage p={0x313030505344414dULL,1,MARBLE_ADSP_PACKAGE_BYTES,4096,118784,122880,30057264,0};
 assert(marble_adsp_package_geometry(&p,MARBLE_ADSP_PACKAGE_BYTES));
 uint64_t *fields=(uint64_t *)(void *)&p;
 for(unsigned i=0;i<8;i++){fields[i]^=1;assert(!marble_adsp_package_geometry(&p,MARBLE_ADSP_PACKAGE_BYTES));fields[i]^=1;}
 for(unsigned i=0;i<4096;i++)assert(!marble_adsp_package_geometry(&p,i));
 assert(!marble_adsp_package_geometry(&p,UINT64_MAX));
 const uint8_t *driver=NULL,*files=NULL;
 assert(!marble_adsp_package_open(NULL,0,&driver,&files));
 assert(!marble_adsp_package_open(&p,sizeof(p),&driver,&files));
 assert(!marble_adsp_package_open((const void *)(uintptr_t)(UINTPTR_MAX-31),64,&driver,&files));
 assert(!marble_adsp_package_open(&p,MARBLE_ADSP_PACKAGE_BYTES,NULL,&files));
 assert(marble_adsp_package_fnv((const uint8_t *)"hello",5)==0xa430d84680aabd0bULL);
 if(argc==2) {
  FILE *f=fopen(argv[1],"rb");assert(f);uint8_t *raw=malloc((size_t)MARBLE_ADSP_PACKAGE_BYTES);assert(raw);
  assert(fread(raw,1,(size_t)MARBLE_ADSP_PACKAGE_BYTES,f)==MARBLE_ADSP_PACKAGE_BYTES && fgetc(f)==EOF);fclose(f);
  assert(marble_adsp_package_open(raw,MARBLE_ADSP_PACKAGE_BYTES,&driver,&files));
  assert(driver==raw+4096 && files==raw+122880);
  raw[4096+512]^=1;assert(!marble_adsp_package_open(raw,MARBLE_ADSP_PACKAGE_BYTES,&driver,&files));raw[4096+512]^=1;
  raw[122880+30057263]^=1;assert(!marble_adsp_package_open(raw,MARBLE_ADSP_PACKAGE_BYTES,&driver,&files));
  free(raw);puts("PASS actual pinned RAM bundle accepted; mutations to either input refused");
 }
 puts("PASS fixed RAM package geometry, truncation/overflow/null outputs, exact FNV byte hashing");
}
