/* UEFI LoadImage/StartImage memory-buffer chain. ABI checked against official
 * tianocore/edk2 edk2-stable202408 MdePkg/Include/Uefi/UefiSpec.h. */
#include "efi.h"
#include "marble_efi_chain.h"
#include "marble_efi_payload.h"
void *memcpy(void *to,const void *from,size_t n)
{uint8_t *d=to;const uint8_t *s=from;for(size_t i=0;i<n;i++)d[i]=s[i];return to;}
void *memset(void *to,int value,size_t n)
{uint8_t *d=to;for(size_t i=0;i<n;i++)d[i]=(uint8_t)value;return to;}
void *memmove(void *to,const void *from,size_t n)
{
    uint8_t *d=to;const uint8_t *s=from;
    if((uintptr_t)d<(uintptr_t)s)for(size_t i=0;i<n;i++)d[i]=s[i];
    else for(size_t i=n;i;i--)d[i-1]=s[i-1];return to;
}
_Static_assert(offsetof(efi_boot_services,load_image)==200,"UEFI LoadImage ABI");
_Static_assert(offsetof(efi_boot_services,start_image)==208,"UEFI StartImage ABI");
typedef efi_status (EFIAPI *MarbleLoadImage)(uint8_t,efi_handle,void *,void *,uint64_t,efi_handle *);
typedef efi_status (EFIAPI *MarbleStartImage)(efi_handle,uint64_t *,char16 **);
typedef efi_status (EFIAPI *MarbleUnloadImage)(efi_handle);
efi_status EFIAPI efi_main(efi_handle parent,efi_system_table *st)
{
    if(!st || !parent || !st->bs || st->hdr.signature!=0x5453595320494249ULL ||
       st->bs->hdr.signature!=0x56524553544f4f42ULL || st->bs->hdr.header_size<240)return EFI_ERROR_BIT|2;
    efi_boot_services *bs=st->bs;
    if(!bs->allocate_pages || !bs->free_pages || !bs->load_image || !bs->start_image || !bs->set_watchdog_timer ||
       MARBLE_EFI_RAW_BYTES<512 || MARBLE_EFI_RAW_BYTES>1048576)return EFI_ERROR_BIT|2;
    /* Retain recovery on every failure before the verified child starts. The
     * normal loader establishes its own watchdog policy in efi_main. */
    efi_status armed=bs->set_watchdog_timer(30,0,0,0);if(EFI_ERROR(armed))return armed;
    uint64_t address=0,pages=(MARBLE_EFI_RAW_BYTES+4095)/4096;
    efi_status rc=bs->allocate_pages(AllocateAnyPages,EfiLoaderData,pages,&address);
    if(EFI_ERROR(rc))return rc;
    if(!address)return EFI_ERROR_BIT|9;
    uint8_t *raw=(uint8_t *)(uintptr_t)address;
    if(!marble_chain_unpack(marble_efi_block,sizeof(marble_efi_block),raw,MARBLE_EFI_RAW_BYTES,marble_efi_sha256)) {
        bs->free_pages(address,pages);return EFI_ERROR_BIT|1;
    }
    efi_handle child=0;
    rc=((MarbleLoadImage)bs->load_image)(0,parent,0,raw,MARBLE_EFI_RAW_BYTES,&child);
    if(EFI_ERROR(rc) || !child) {
        /* Security violation may return a loaded but unstartable child. */
        if(child && (!bs->unload_image || EFI_ERROR(((MarbleUnloadImage)bs->unload_image)(child))))return rc?rc:EFI_ERROR_BIT|1;
        bs->free_pages(address,pages);return rc?rc:EFI_ERROR_BIT|1;
    }
    uint64_t exit_size=0;
    rc=((MarbleStartImage)bs->start_image)(child,&exit_size,0);
    /* Keep the entire verified source until StartImage returns. The firmware
     * owns its relocated child image separately from this source allocation. */
    efi_status released=bs->free_pages(address,pages);
    return EFI_ERROR(rc)?rc:released;
}
