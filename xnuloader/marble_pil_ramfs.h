/* Read-only, bounded EFI RAM file service for pinned local ROM inputs.
 * No backing disk, MMIO, asynchronous I/O, write/create or delete support. */
#ifndef XNULOADER_MARBLE_PIL_RAMFS_H
#define XNULOADER_MARBLE_PIL_RAMFS_H
#define MPFS_INVALID (EFI_ERROR_BIT|2)
#define MPFS_UNSUPPORTED (EFI_ERROR_BIT|3)
#define MPFS_WRITE_PROTECTED (EFI_ERROR_BIT|8)
#define MPFS_NOT_FOUND (EFI_ERROR_BIT|14)
typedef struct { char name[32];uint64_t offset,size,fingerprint,reserved; } MarblePILFileEntry;
typedef struct MarblePILFile MarblePILFile;
struct MarblePILFile {
    uint64_t revision;
    efi_status (EFIAPI *open)(MarblePILFile *,MarblePILFile **,const char16 *,uint64_t,uint64_t);
    efi_status (EFIAPI *close)(MarblePILFile *);
    efi_status (EFIAPI *remove)(MarblePILFile *);
    efi_status (EFIAPI *read)(MarblePILFile *,uint64_t *,void *);
    efi_status (EFIAPI *write)(MarblePILFile *,uint64_t *,const void *);
    efi_status (EFIAPI *get_position)(MarblePILFile *,uint64_t *);
    efi_status (EFIAPI *set_position)(MarblePILFile *,uint64_t);
    efi_status (EFIAPI *get_info)(MarblePILFile *,const efi_guid *,uint64_t *,void *);
    efi_status (EFIAPI *set_info)(MarblePILFile *,const efi_guid *,uint64_t,const void *);
    efi_status (EFIAPI *flush)(MarblePILFile *);
    const MarblePILFileEntry *entry;
    uint64_t position;unsigned used,directory;
};
_Static_assert(sizeof(MarblePILFileEntry)==64,"RAM file entry ABI");
_Static_assert(offsetof(MarblePILFile,flush)==0x50,"EFI file protocol revision1 ABI");
static const efi_guid marble_pil_file_info_guid={0x9576e92,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static struct {
    const uint8_t *data;uint64_t bytes;unsigned count;
    MarblePILFile handles[8];
    unsigned opens,reads,writes,closes,infos;
} marble_pil_ramfs;
static uint64_t marble_pil_fnv(const void *raw,uint64_t bytes)
{
    const uint8_t *data=raw;uint64_t v=0xcbf29ce484222325ULL;
    for(uint64_t i=0;i<bytes;i++)v=(v^data[i])*0x100000001b3ULL;
    return v;
}
static int marble_pil_equal(const char *a,const char *b)
{
    for(unsigned i=0;i<64;i++) {if(a[i]!=b[i])return 0;if(!a[i])return 1;}
    return 0;
}
static int marble_pil_file_name(const char *name)
{
    unsigned prefix=0;
    if(!memcmp(name,"adsp2.",6))prefix=6;
    else if(!memcmp(name,"slpi2.",6))prefix=6;
    else return 0;
    return (!memcmp(name+prefix,"mdt\0",4)) ||
        (name[prefix]=='b' && name[prefix+1]>='0' && name[prefix+1]<='9' &&
         name[prefix+2]>='0' && name[prefix+2]<='9' && !name[prefix+3]);
}
static int marble_pil_ramfs_init(const void *raw,uint64_t bytes)
{
    const uint8_t *data=raw;
    if(!raw || bytes<32 || bytes>0x3f00000 || memcmp(data,"MPILFS1\0",8))return 0;
    uint32_t count=*(const uint32_t *)(data+8),entry_size=*(const uint32_t *)(data+12);
    if(!count || count>64 || entry_size!=64 || *(const uint64_t *)(data+16)!=bytes || 32+count*64>bytes)return 0;
    const MarblePILFileEntry *entries=(const void *)(data+32);
    for(unsigned i=0;i<count;i++) {
        const MarblePILFileEntry *e=&entries[i];
        if(!marble_pil_file_name(e->name) || e->reserved || !e->size ||
           e->offset<32+count*64 || (e->offset&7) || e->offset>bytes || e->size>bytes-e->offset)return 0;
        for(unsigned j=0;j<i;j++) {
            const MarblePILFileEntry *p=&entries[j];
            if(!memcmp(e->name,p->name,32) || (e->offset<p->offset+p->size && p->offset<e->offset+e->size))return 0;
        }
        if(marble_pil_fnv(data+e->offset,e->size)!=e->fingerprint)return 0;
    }
    memset(&marble_pil_ramfs,0,sizeof(marble_pil_ramfs));
    marble_pil_ramfs.data=data;marble_pil_ramfs.bytes=bytes;marble_pil_ramfs.count=count;
    return 1;
}
static efi_status EFIAPI marble_pil_file_close(MarblePILFile *f)
{
    if(!f || !f->used)return MPFS_INVALID;
    f->used=0;marble_pil_ramfs.closes++;return 0;
}
static efi_status EFIAPI marble_pil_file_delete(MarblePILFile *f)
{ (void)f;marble_pil_ramfs.writes++;return MPFS_WRITE_PROTECTED; }
static efi_status EFIAPI marble_pil_file_write(MarblePILFile *f,uint64_t *size,const void *data)
{ (void)f;(void)size;(void)data;marble_pil_ramfs.writes++;return MPFS_WRITE_PROTECTED; }
static efi_status EFIAPI marble_pil_file_set_info(MarblePILFile *f,const efi_guid *guid,uint64_t size,const void *data)
{ (void)f;(void)guid;(void)size;(void)data;marble_pil_ramfs.writes++;return MPFS_WRITE_PROTECTED; }
static efi_status EFIAPI marble_pil_file_flush(MarblePILFile *f)
{ return f && f->used?0:MPFS_INVALID; }
static efi_status EFIAPI marble_pil_file_read(MarblePILFile *f,uint64_t *size,void *out)
{
    if(!f || !f->used || !size || (!out && *size))return MPFS_INVALID;
    if(f->directory)return MPFS_UNSUPPORTED;
    uint64_t left=f->entry->size-f->position,bytes=*size<left?*size:left;
    if(bytes)memcpy(out,marble_pil_ramfs.data+f->entry->offset+f->position,(size_t)bytes);
    f->position+=bytes;*size=bytes;marble_pil_ramfs.reads++;return 0;
}
static efi_status EFIAPI marble_pil_file_get_position(MarblePILFile *f,uint64_t *position)
{
    if(!f || !f->used || !position)return MPFS_INVALID;
    *position=f->position;return 0;
}
static efi_status EFIAPI marble_pil_file_set_position(MarblePILFile *f,uint64_t position)
{
    if(!f || !f->used || f->directory)return MPFS_INVALID;
    if(position==~0ULL)position=f->entry->size;
    if(position>f->entry->size)return MPFS_INVALID;
    f->position=position;return 0;
}
static efi_status EFIAPI marble_pil_file_get_info(MarblePILFile *f,const efi_guid *guid,uint64_t *size,void *out)
{
    if(!f || !f->used || !guid || !size)return MPFS_INVALID;
    if(memcmp(guid,&marble_pil_file_info_guid,sizeof(*guid)))return MPFS_UNSUPPORTED;
    const char *name=f->directory?"image":f->entry->name;uint64_t length=0;
    while(name[length])length++;
    uint64_t need=80+(length+1)*2;
    if(*size<need || !out) {*size=need;return EFI_BUFFER_TOO_SMALL;}
    memset(out,0,(size_t)need);uint64_t *words=out;
    words[0]=need;words[1]=words[2]=f->directory?0:f->entry->size;
    words[9]=f->directory?0x11:1; /* directory/read-only attributes */
    char16 *wide=(void *)((uint8_t *)out+80);
    for(uint64_t i=0;i<length;i++)wide[i]=(unsigned char)name[i];
    *size=need;marble_pil_ramfs.infos++;return 0;
}
static MarblePILFile *marble_pil_file_new(const MarblePILFileEntry *,unsigned);
static efi_status EFIAPI marble_pil_file_open(MarblePILFile *parent,MarblePILFile **out,const char16 *wide,uint64_t mode,uint64_t attributes)
{
    (void)attributes;
    if(!parent || !parent->used || !out || !wide)return MPFS_INVALID;
    *out=NULL;
    if(mode!=1) {marble_pil_ramfs.writes++;return MPFS_WRITE_PROTECTED;}
    char name[64];unsigned n=0;
    while(wide[n] && n<sizeof(name)-1) {
        unsigned c=wide[n];if(c>127)return MPFS_NOT_FOUND;
        name[n]=(char)(c>='A' && c<='Z'?c+32:c);n++;
    }
    if(wide[n])return MPFS_NOT_FOUND;
    name[n]=0;const char *relative=name;
    if(*relative=='\\')relative++;
    if(!*relative || marble_pil_equal(relative,"image") || marble_pil_equal(relative,"image\\")) {
        *out=marble_pil_file_new(NULL,1);return *out?0:(EFI_ERROR_BIT|9);
    }
    if(!memcmp(relative,"image\\",6))relative+=6;
    if(!marble_pil_file_name(relative))return MPFS_NOT_FOUND;
    const MarblePILFileEntry *entries=(const void *)(marble_pil_ramfs.data+32);
    for(unsigned i=0;i<marble_pil_ramfs.count;i++)if(marble_pil_equal(relative,entries[i].name)) {
        *out=marble_pil_file_new(&entries[i],0);return *out?0:(EFI_ERROR_BIT|9);
    }
    return MPFS_NOT_FOUND;
}
static MarblePILFile *marble_pil_file_new(const MarblePILFileEntry *entry,unsigned directory)
{
    for(unsigned i=0;i<8;i++)if(!marble_pil_ramfs.handles[i].used) {
        MarblePILFile *f=&marble_pil_ramfs.handles[i];memset(f,0,sizeof(*f));
        f->revision=0x10000;f->open=marble_pil_file_open;f->close=marble_pil_file_close;
        f->remove=marble_pil_file_delete;f->read=marble_pil_file_read;f->write=marble_pil_file_write;
        f->get_position=marble_pil_file_get_position;f->set_position=marble_pil_file_set_position;
        f->get_info=marble_pil_file_get_info;f->set_info=marble_pil_file_set_info;f->flush=marble_pil_file_flush;
        f->entry=entry;f->directory=directory;f->used=1;marble_pil_ramfs.opens++;return f;
    }
    return NULL;
}
#endif
