#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../xnuloader/efi.h"
#include "../../xnuloader/marble_pil_ramfs.h"
static union {uint64_t align;uint8_t data[512];} blob;
static void fixture(void)
{
    memset(&blob,0,sizeof(blob));memcpy(blob.data,"MPILFS1\0",8);
    *(uint32_t *)(blob.data+8)=2;*(uint32_t *)(blob.data+12)=64;
    *(uint64_t *)(blob.data+16)=sizeof(blob.data);
    MarblePILFileEntry *e=(void *)(blob.data+32);
    for(unsigned i=0;i<2;i++) {
        memcpy(e[i].name,i?"slpi2.mdt":"adsp2.mdt",10);
        e[i].offset=256+i*64;e[i].size=16;
        for(unsigned j=0;j<16;j++)blob.data[e[i].offset+j]=(uint8_t)(i*32+j);
        e[i].fingerprint=marble_pil_fnv(blob.data+e[i].offset,16);
    }
}
int main(void)
{
    fixture();assert(marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    MarblePILFile *root=marble_pil_file_new(NULL,1),*file=NULL;
    assert(root && !root->open(root,&file,u"\\IMAGE\\ADSP2.MDT",1,0) && file);
    uint8_t data[256];uint64_t bytes=256;
    assert(!file->read(file,&bytes,data) && bytes==16 && data[15]==15);
    bytes=256;assert(!file->read(file,&bytes,data) && bytes==0);
    assert(!file->set_position(file,8));bytes=5;assert(!file->read(file,&bytes,data) && bytes==5 && data[0]==8 && data[4]==12);
    assert(file->set_position(file,17)==MPFS_INVALID);
    assert(!file->set_position(file,~0ULL));bytes=1;assert(!file->read(file,&bytes,data) && !bytes);
    bytes=0;assert(file->get_info(file,&marble_pil_file_info_guid,&bytes,NULL)==EFI_BUFFER_TOO_SMALL && bytes==100);
    bytes=sizeof(data);assert(!file->get_info(file,&marble_pil_file_info_guid,&bytes,data));
    assert(*(uint64_t *)(data+8)==16 && *(uint64_t *)(data+72)==1);
    bytes=1;assert(file->write(file,&bytes,"x")==MPFS_WRITE_PROTECTED);
    assert(file->set_info(file,&marble_pil_file_info_guid,0,NULL)==MPFS_WRITE_PROTECTED);
    assert(!file->close(file) && file->read(file,&bytes,data)==MPFS_INVALID);
    assert(root->open(root,&file,u"\\image\\..\\adsp2.mdt",1,0)==MPFS_NOT_FOUND && !file);
    assert(root->open(root,&file,u"\\image\\adsp2.mdt",3,0)==MPFS_WRITE_PROTECTED && !file);
    assert(!root->close(root));
    fixture();MarblePILFileEntry *e=(void *)(blob.data+32);
    e[0].offset=UINT64_MAX;assert(!marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    fixture();e[0].size=UINT64_MAX;assert(!marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    fixture();e[1].offset=e[0].offset;assert(!marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    fixture();blob.data[256]^=1;assert(!marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    fixture();memcpy(e[0].name,"../x",5);assert(!marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    fixture();*(uint32_t *)(blob.data+8)=UINT32_MAX;assert(!marble_pil_ramfs_init(blob.data,sizeof(blob.data)));
    puts("PIL RAM filesystem bounds, read-only policy, seeking, EOF and file-info tests PASS");
    return 0;
}
